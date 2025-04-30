/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "RenderThread.h"

#include <assert.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <unordered_map>

#include "ChannelStream.h"
#include "FrameBuffer.h"
#include "ReadBuffer.h"
#include "RenderChannelImpl.h"
#if GFXSTREAM_ENABLE_HOST_GLES
#include "RenderControl.h"
#endif
#include "RenderThreadInfo.h"
#include "RingStream.h"
#include "VkDecoderContext.h"
#include "apigen-codec-common/ChecksumCalculatorThreadInfo.h"
#include "aemu/base/files/StreamSerializing.h"
#include "gfxstream/HealthMonitor.h"
#include "gfxstream/host/logging.h"
#include "gfxstream/Metrics.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/synchronization/MessageChannel.h"
#include "gfxstream/system/System.h"
#include "vulkan/VkCommonOperations.h"

namespace gfxstream {

using gfxstream::base::AutoLock;
using gfxstream::base::EventHangMetadata;
using emugl::GfxApiLogger;
using vk::VkDecoderContext;

struct RenderThread::SnapshotObjects {
    RenderThreadInfo* threadInfo;
    ChecksumCalculator* checksumCalc;
    ChannelStream* channelStream;
    RingStream* ringStream;
    ReadBuffer* readBuffer;
};

static bool getBenchmarkEnabledFromEnv() {
    auto threadEnabled = gfxstream::base::getEnvironmentVariable("ANDROID_EMUGL_RENDERTHREAD_STATS");
    if (threadEnabled == "1") return true;
    return false;
}

// Start with a smaller buffer to not waste memory on a low-used render threads.
static constexpr int kStreamBufferSize = 128 * 1024;

// Requires this many threads on the system available to run unlimited.
static constexpr int kMinThreadsToRunUnlimited = 5;

// A thread run limiter that limits render threads to run one slice at a time.
static gfxstream::base::Lock sThreadRunLimiter;

RenderThread::RenderThread(RenderChannelImpl* channel,
                           android::base::Stream* loadStream,
                           uint32_t virtioGpuContextId)
    : gfxstream::base::Thread(gfxstream::base::ThreadFlags::MaskSignals, 2 * 1024 * 1024,
                            "RenderThread"),
      mChannel(channel),
      mRunInLimitedMode(gfxstream::base::getCpuCoreCount() < kMinThreadsToRunUnlimited),
      mContextId(virtioGpuContextId)
{
    if (loadStream) {
        const bool success = loadStream->getByte();
        if (success) {
            mStream.emplace(0);
            android::base::loadStream(loadStream, &*mStream);
            mState = SnapshotState::StartLoading;
        } else {
            mFinished.store(true, std::memory_order_relaxed);
        }
    }
}

RenderThread::RenderThread(
        struct asg_context context,
        android::base::Stream* loadStream,
        android::emulation::asg::ConsumerCallbacks callbacks,
        uint32_t contextId, uint32_t capsetId,
        std::optional<std::string> nameOpt)
    : gfxstream::base::Thread(gfxstream::base::ThreadFlags::MaskSignals, 2 * 1024 * 1024,
                            std::move(nameOpt)),
      mRingStream(
          new RingStream(context, callbacks, kStreamBufferSize)),
      mContextId(contextId), mCapsetId(capsetId) {
    if (loadStream) {
        const bool success = loadStream->getByte();
        if (success) {
            mStream.emplace(0);
            android::base::loadStream(loadStream, &*mStream);
            mState = SnapshotState::StartLoading;
        } else {
            mFinished.store(true, std::memory_order_relaxed);
        }
    }
}

// Note: the RenderThread destructor might be called from a different thread
// than from RenderThread::main() so thread specific cleanup likely belongs at
// the end of RenderThread::main().
RenderThread::~RenderThread() = default;

void RenderThread::pausePreSnapshot() {
    AutoLock lock(mLock);
    assert(mState == SnapshotState::Empty);
    mStream.emplace();
    mState = SnapshotState::StartSaving;
    if (mRingStream) {
        mRingStream->pausePreSnapshot();
        // mSnapshotSignal.broadcastAndUnlock(&lock);
    }
    if (mChannel) {
        mChannel->pausePreSnapshot();
        mSnapshotSignal.broadcastAndUnlock(&lock);
    }
}

void RenderThread::resume() {
    AutoLock lock(mLock);
    // This function can be called for a thread from pre-snapshot loading
    // state; it doesn't need to do anything.
    if (mState == SnapshotState::Empty) {
        return;
    }
    if (mRingStream) mRingStream->resume();
    waitForSnapshotCompletion(&lock);

    mNeedReloadProcessResources = true;
    mStream.clear();
    mState = SnapshotState::Empty;
    if (mChannel) mChannel->resume();
    if (mRingStream) mRingStream->resume();
    mSnapshotSignal.broadcastAndUnlock(&lock);
}

void RenderThread::save(android::base::Stream* stream) {
    bool success;
    {
        AutoLock lock(mLock);
        assert(mState == SnapshotState::StartSaving ||
               mState == SnapshotState::InProgress ||
               mState == SnapshotState::Finished);
        waitForSnapshotCompletion(&lock);
        success = mState == SnapshotState::Finished;
    }

    if (success) {
        assert(mStream);
        stream->putByte(1);
        android::base::saveStream(stream, *mStream);
    } else {
        stream->putByte(0);
    }
}

void RenderThread::waitForSnapshotCompletion(AutoLock* lock) {
    while (mState != SnapshotState::Finished &&
           !mFinished.load(std::memory_order_relaxed)) {
        mSnapshotSignal.wait(lock);
    }
}

bool RenderThread::isPausedForSnapshotLocked() const { return mState != SnapshotState::Empty; }

bool RenderThread::doSnapshotOp(const SnapshotObjects& objects, SnapshotState expectedState,
                                std::function<void()> op) {
    AutoLock lock(mLock);

    if (mState != expectedState) {
        return false;
    }
    mState = SnapshotState::InProgress;
    mSnapshotSignal.broadcastAndUnlock(&lock);

    op();

    lock.lock();

    mState = SnapshotState::Finished;
    mSnapshotSignal.broadcast();

    // Only return after we're allowed to proceed.
    while (isPausedForSnapshotLocked()) {
        mSnapshotSignal.wait(&lock);
    }

    return true;
}

bool RenderThread::loadSnapshot(const SnapshotObjects& objects) {
    return doSnapshotOp(objects, SnapshotState::StartLoading, [this, &objects] {
        objects.readBuffer->onLoad(&*mStream);
        if (objects.channelStream) objects.channelStream->load(&*mStream);
        if (objects.ringStream) objects.ringStream->load(&*mStream);
        objects.checksumCalc->load(&*mStream);
        objects.threadInfo->onLoad(&*mStream);
    });
}

bool RenderThread::saveSnapshot(const SnapshotObjects& objects) {
    return doSnapshotOp(objects, SnapshotState::StartSaving, [this, &objects] {
        objects.readBuffer->onSave(&*mStream);
        if (objects.channelStream) objects.channelStream->save(&*mStream);
        if (objects.ringStream) objects.ringStream->save(&*mStream);
        objects.checksumCalc->save(&*mStream);
        objects.threadInfo->onSave(&*mStream);
    });
}

void RenderThread::waitForFinished() {
    AutoLock lock(mLock);
    while (!mFinished.load(std::memory_order_relaxed)) {
        mFinishedSignal.wait(&lock);
    }
}

void RenderThread::sendExitSignal() {
    AutoLock lock(mLock);
    if (!mFinished.load(std::memory_order_relaxed)) {
        GFXSTREAM_FATAL("RenderThread exit signal sent before finished");
    }
    mCanExit.store(true, std::memory_order_relaxed);
    mExitSignal.broadcastAndUnlock(&lock);
}

void RenderThread::setFinished() {
    // Make sure it never happens that we wait forever for the thread to
    // save to snapshot while it was not even going to.
    {
        AutoLock lock(mLock);
        mFinished.store(true, std::memory_order_relaxed);
        if (mState != SnapshotState::Empty) {
            mSnapshotSignal.broadcastAndUnlock(&lock);
        }
    }
    {
        AutoLock lock(mLock);
        mFinishedSignal.broadcastAndUnlock(&lock);
    }
}

void RenderThread::waitForExitSignal() {
    AutoLock lock(mLock);
    GFXSTREAM_DEBUG("Waiting for exit signal RenderThread @%p", this);
    while (!mCanExit.load(std::memory_order_relaxed)) {
        mExitSignal.wait(&lock);
    }
}

intptr_t RenderThread::main() {
    if (mFinished.load(std::memory_order_relaxed)) {
        GFXSTREAM_ERROR("Error: fail loading a RenderThread @%p", this);
        return 0;
    }

    std::unique_ptr<RenderThreadInfo> tInfo = std::make_unique<RenderThreadInfo>();
    ChecksumCalculatorThreadInfo tChecksumInfo;
    ChecksumCalculator& checksumCalc = tChecksumInfo.get();
    bool needRestoreFromSnapshot = false;

    //
    // initialize decoders
#if GFXSTREAM_ENABLE_HOST_GLES
    if (!FrameBuffer::getFB()->getFeatures().GuestVulkanOnly.enabled) {
        tInfo->initGl();
    }

    initRenderControlContext(&(tInfo->m_rcDec));
#endif

    if (!mChannel && !mRingStream) {
        GFXSTREAM_DEBUG("Exited a loader RenderThread @%p", this);
        mFinished.store(true, std::memory_order_relaxed);
        return 0;
    }

    ChannelStream stream(mChannel, RenderChannel::Buffer::kSmallSize);
    IOStream* ioStream =
        mChannel ? (IOStream*)&stream : (IOStream*)mRingStream.get();

    ReadBuffer readBuf(kStreamBufferSize);
    if (mRingStream) {
        readBuf.setNeededFreeTailSize(0);
    }

    const SnapshotObjects snapshotObjects = {
        tInfo.get(), &checksumCalc, &stream, mRingStream.get(), &readBuf,
    };

    // Framebuffer initialization is asynchronous, so we need to make sure
    // it's completely initialized before running any GL commands.
    FrameBuffer::waitUntilInitialized();

    if (FrameBuffer::getFB()->hasEmulationVk()) {
        tInfo->m_vkInfo.emplace();
    }

    // This is the only place where we try loading from snapshot.
    // But the context bind / restoration will be delayed after receiving
    // the first GL command.
    if (loadSnapshot(snapshotObjects)) {
        GFXSTREAM_DEBUG("Loaded RenderThread @%p from snapshot", this);
        needRestoreFromSnapshot = true;
    } else {
        // Not loading from a snapshot: continue regular startup, read
        // the |flags|.
        uint32_t flags = 0;
        while (ioStream->read(&flags, sizeof(flags)) != sizeof(flags)) {
            // Stream read may fail because of a pending snapshot.
            if (!saveSnapshot(snapshotObjects)) {
                setFinished();
                tInfo.reset();
                waitForExitSignal();
                GFXSTREAM_DEBUG("Exited a RenderThread @%p early", this);
                return 0;
            }
        }

        // |flags| used to mean something, now they're not used.
        (void)flags;
    }

    int stats_totalBytes = 0;
    uint64_t stats_progressTimeUs = 0;
    auto stats_t0 = gfxstream::base::getHighResTimeUs() / 1000;
    bool benchmarkEnabled = getBenchmarkEnabledFromEnv();

    //
    // open dump file if RENDER_DUMP_DIR is defined
    //
    const char* dump_dir = getenv("RENDERER_DUMP_DIR");
    FILE* dumpFP = nullptr;
    if (dump_dir) {
        // size_t bsize = strlen(dump_dir) + 32;
        // char* fname = new char[bsize];
        // snprintf(fname, bsize, "%s" PATH_SEP "stream_%p", dump_dir, this);
        // dumpFP = android_fopen(fname, "wb");
        // if (!dumpFP) {
        //     fprintf(stderr, "Warning: stream dump failed to open file %s\n",
        //             fname);
        // }
        // delete[] fname;
    }

    GfxApiLogger gfxLogger;
    auto& metricsLogger = FrameBuffer::getFB()->getMetricsLogger();

    const ProcessResources* processResources = nullptr;
    bool anyProgress = false;
    while (true) {
        // Let's make sure we read enough data for at least some processing.
        uint32_t packetSize;
        if (readBuf.validData() >= 8) {
            // We know that packet size is the second int32_t from the start.
            packetSize = *(uint32_t*)(readBuf.buf() + 4);
            if (!packetSize) {
                // Emulator will get live-stuck here if packet size is read to be zero;
                // crash right away so we can see these events.
                // emugl::emugl_crash_reporter(
                //     "Guest should never send a size-0 GL packet\n");
            }
        } else {
            // Read enough data to at least be able to get the packet size next
            // time.
            packetSize = 8;
        }
        if (!anyProgress) {
            // If we didn't make any progress last time, then make sure we read at least one
            // extra byte.
            packetSize = std::max(packetSize, static_cast<uint32_t>(readBuf.validData() + 1));
        }
        int stat = 0;
        if (packetSize > readBuf.validData()) {
            stat = readBuf.getData(ioStream, packetSize);
            if (stat <= 0) {
                if (saveSnapshot(snapshotObjects)) {
                    continue;
                } else {
                    break;
                }
            } else if (needRestoreFromSnapshot) {
                // If we're using RingStream that might load before FrameBuffer
                // restores the contexts from the handles, so check again here.

                tInfo->postLoadRefreshCurrentContextSurfacePtrs();
                needRestoreFromSnapshot = false;
            }
            if (mNeedReloadProcessResources) {
                processResources = nullptr;
                mNeedReloadProcessResources = false;
            }
        }

        //
        // log received bandwidth statistics
        //
        if (benchmarkEnabled) {
            stats_totalBytes += readBuf.validData();
            auto dt = gfxstream::base::getHighResTimeUs() / 1000 - stats_t0;
            if (dt > 1000) {
                float dts = (float)dt / 1000.0f;
                printf("Used Bandwidth %5.3f MB/s, time in progress %f ms total %f ms\n", ((float)stats_totalBytes / dts) / (1024.0f*1024.0f),
                        stats_progressTimeUs / 1000.0f,
                        (float)dt);
                readBuf.printStats();
                stats_t0 = gfxstream::base::getHighResTimeUs() / 1000;
                stats_progressTimeUs = 0;
                stats_totalBytes = 0;
            }
        }

        //
        // dump stream to file if needed
        //
        if (dumpFP) {
            int skip = readBuf.validData() - stat;
            fwrite(readBuf.buf() + skip, 1, readBuf.validData() - skip, dumpFP);
            fflush(dumpFP);
        }

        bool progress = false;
        anyProgress = false;
        do {
            anyProgress |= progress;
            std::unique_ptr<EventHangMetadata::HangAnnotations> renderThreadData =
                std::make_unique<EventHangMetadata::HangAnnotations>();

            const char* contextName = nullptr;
            if (mNameOpt) {
                contextName = (*mNameOpt).c_str();
            }

            auto* healthMonitor = FrameBuffer::getFB()->getHealthMonitor();
            if (healthMonitor) {
                if (contextName) {
                    renderThreadData->insert(
                        {{"renderthread_guest_process", contextName}});
                }
                if (readBuf.validData() >= 4) {
                    renderThreadData->insert(
                        {{"first_opcode", std::to_string(*(uint32_t*)readBuf.buf())},
                         {"buffer_length", std::to_string(readBuf.validData())}});
                }
            }
            auto watchdog = WATCHDOG_BUILDER(healthMonitor, "RenderThread decode operation")
                                .setHangType(EventHangMetadata::HangType::kRenderThread)
                                .setAnnotations(std::move(renderThreadData))
                                .build();

            if (!tInfo->m_puid) {
                tInfo->m_puid = mContextId;
            }

            if (!processResources && tInfo->m_puid && tInfo->m_puid != INVALID_CONTEXT_ID) {
                processResources = FrameBuffer::getFB()->getProcessResources(tInfo->m_puid);
            }

            progress = false;
            size_t last;

            //
            // try to process some of the command buffer using the
            // Vulkan decoder
            //
            // Note: It's risky to limit Vulkan decoding to one thread,
            // so we do it outside the limiter
            if (tInfo->m_vkInfo) {
                if (tInfo->m_vkInfo->ctx_id == 0) {
                    tInfo->m_vkInfo->ctx_id = mContextId;
                }
                VkDecoderContext context = {
                    .processName = contextName,
                    .gfxApiLogger = &gfxLogger,
                    .healthMonitor = FrameBuffer::getFB()->getHealthMonitor(),
                    .metricsLogger = &metricsLogger,
                    .shouldExit = &(tInfo->m_shouldExit),
                };
                last = tInfo->m_vkInfo->m_vkDec.decode(readBuf.buf(), readBuf.validData(), ioStream,
                                                      processResources, context);
                if (last > 0) {
                    if (!processResources) {
                        GFXSTREAM_ERROR(
                            "Processed some Vulkan packets without process resources created. "
                            "That's problematic.");
                    }
                    readBuf.consume(last);
                    progress = true;
                }
            }

            std::optional<gfxstream::base::AutoLock> limitedModeLock;
            if (mRunInLimitedMode) {
                limitedModeLock.emplace(sThreadRunLimiter);
            }

            // try to process some of the command buffer using the GLESv1
            // decoder
            //
            // DRIVER WORKAROUND:
            // On Linux with NVIDIA GPU's at least, we need to avoid performing
            // GLES ops while someone else holds the FrameBuffer write lock.
            //
            // To be more specific, on Linux with NVIDIA Quadro K2200 v361.xx,
            // we get a segfault in the NVIDIA driver when glTexSubImage2D
            // is called at the same time as glXMake(Context)Current.
            //
            // To fix, this driver workaround avoids calling
            // any sort of GLES call when we are creating/destroying EGL
            // contexts.
            {
                FrameBuffer::getFB()->lockContextStructureRead();
            }

#if GFXSTREAM_ENABLE_HOST_GLES
            if (tInfo->m_glInfo) {
                {
                    last = tInfo->m_glInfo->m_glDec.decode(
                            readBuf.buf(), readBuf.validData(), ioStream, &checksumCalc);
                    if (last > 0) {
                        progress = true;
                        readBuf.consume(last);
                    }
                }

                //
                // try to process some of the command buffer using the GLESv2
                // decoder
                //
                {
                    last = tInfo->m_glInfo->m_gl2Dec.decode(readBuf.buf(), readBuf.validData(),
                                                           ioStream, &checksumCalc);

                    if (last > 0) {
                        progress = true;
                        readBuf.consume(last);
                    }
                }
            }
#endif

            FrameBuffer::getFB()->unlockContextStructureRead();
            //
            // try to process some of the command buffer using the
            // renderControl decoder
            //
#if GFXSTREAM_ENABLE_HOST_GLES
            {
                last = tInfo->m_rcDec.decode(readBuf.buf(), readBuf.validData(),
                                            ioStream, &checksumCalc);
                if (last > 0) {
                    readBuf.consume(last);
                    progress = true;
                }
            }
#endif
        } while (progress);
    }

    if (dumpFP) {
        fclose(dumpFP);
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (tInfo->m_glInfo) {
        FrameBuffer::getFB()->drainGlRenderThreadResources();
    }
#endif

    setFinished();
    // Since we now control when the thread exits, we must make sure the RenderThreadInfo is
    // destroyed after the RenderThread is finished, as the RenderThreadInfo cleanup thread is
    // waiting on the object to be destroyed.
    tInfo.reset();
    waitForExitSignal();

    GFXSTREAM_DEBUG("Exited a RenderThread @%p", this);
    return 0;
}

}  // namespace gfxstream
