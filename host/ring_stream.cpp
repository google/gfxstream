// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ring_stream.h"

#include <assert.h>
#include <memory.h>

#include <algorithm>
#include <cstring>

#include "gfxstream/host/dma_device.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/host/stream_utils.h"
#include "gfxstream/system/System.h"
#include "render-utils/dma_device.h"
#include "render-utils/stream.h"

namespace gfxstream {
namespace {

struct asg_context CreateContext(const AsgConsumerCreateInfo& info) {
    struct asg_context context = asg_context_create(info.ring_storage, info.buffer, info.buffer_size);

    context.ring_config->buffer_size = info.buffer_size;
    context.ring_config->flush_interval = info.buffer_flush_interval;
    context.ring_config->host_consumed_pos = 0;
    context.ring_config->guest_write_pos = 0;
    context.ring_config->transfer_mode = 1;
    context.ring_config->transfer_size = 0;
    context.ring_config->in_error = 0;

    return context;
}

void SaveRingConfig(Stream* stream, const struct asg_ring_config& config) {
    stream->putBe32(config.buffer_size);
    stream->putBe32(config.flush_interval);
    stream->putBe32(config.host_consumed_pos);
    stream->putBe32(config.guest_write_pos);
    stream->putBe32(config.transfer_mode);
    stream->putBe32(config.transfer_size);
    stream->putBe32(config.in_error);
}

constexpr uint32_t kMaxRingStreamIoBufferBytes = 64u * 1024u * 1024u;

bool ReadExact(Stream* stream, void* dst, size_t size) {
    const ssize_t got = stream->read(dst, size);
    return got >= 0 && static_cast<size_t>(got) == size;
}

void WriteExact(Stream* stream, const void* src, size_t size) {
    const ssize_t written = stream->write(src, size);
    if (written != static_cast<ssize_t>(size)) {
        stream->addError("WriteExact: expected %zu, got %zd", size, written);
    }
}

bool ReadBe32Exact(Stream* stream, uint32_t* out) {
    uint8_t bytes[4] = {};
    if (!ReadExact(stream, bytes, sizeof(bytes))) {
        return false;
    }
    *out = (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
    return true;
}

bool LoadBoundedBuffer(Stream* stream, RenderChannel::Buffer* buffer, uint32_t maxBytes) {
    uint32_t len = 0;
    if (!ReadBe32Exact(stream, &len)) {
        GFXSTREAM_ERROR("LoadBoundedBuffer: truncated length prefix.");
        return false;
    }
    if (len > maxBytes) {
        GFXSTREAM_ERROR("LoadBoundedBuffer: length %u exceeds max %u.", len, maxBytes);
        return false;
    }
    buffer->clear();
    if (len == 0) {
        return true;
    }
    buffer->resize_noinit(len);
    if (!ReadExact(stream, buffer->data(), len)) {
        GFXSTREAM_ERROR("LoadBoundedBuffer: truncated payload (%u bytes).", len);
        buffer->clear();
        return false;
    }
    return true;
}

bool LoadRingConfig(Stream* stream, struct asg_ring_config* config) {
    struct asg_ring_config staged = {};
    if (!ReadBe32Exact(stream, &staged.buffer_size) ||
        !ReadBe32Exact(stream, &staged.flush_interval) ||
        !ReadBe32Exact(stream, &staged.host_consumed_pos) ||
        !ReadBe32Exact(stream, &staged.guest_write_pos) ||
        !ReadBe32Exact(stream, &staged.transfer_mode) ||
        !ReadBe32Exact(stream, &staged.transfer_size) ||
        !ReadBe32Exact(stream, &staged.in_error)) {
        return false;
    }
    *config = staged;
    return true;
}

void SaveAsgContext(Stream* stream, const struct asg_context& context) {
    WriteExact(stream, context.to_host, sizeof(struct ring_buffer));
    WriteExact(stream, context.to_host_large_xfer.ring, sizeof(struct ring_buffer));
    WriteExact(stream, context.from_host_large_xfer.ring, sizeof(struct ring_buffer));
    // context.buffer points to the shared ASG auxiliary transfer buffer
    // (1 MiB, separate from the 12 KiB asg_ring_storage serialized above),
    // which is already saved and restored by the owning backing store
    // (goldfish_address_space on legacy goldfish, and VirtioGpuFrontend
    // RingBlob on virtio-gpu, both after quiescing ASG render threads).
    // Writing 0 here avoids duplicating 1 MiB per ASG stream inside the
    // GoldfishGpu snapshot meta blob / gfxstream_asg.bin.
    stream->putBe32(0);
}

bool LoadAsgContext(Stream* stream, struct asg_context* context, uint32_t max_buffer_size) {
    struct ring_buffer stagedToHost = {};
    struct ring_buffer stagedToHostLarge = {};
    struct ring_buffer stagedFromHostLarge = {};
    if (!ReadExact(stream, &stagedToHost, sizeof(stagedToHost)) ||
        !ReadExact(stream, &stagedToHostLarge, sizeof(stagedToHostLarge)) ||
        !ReadExact(stream, &stagedFromHostLarge, sizeof(stagedFromHostLarge))) {
        GFXSTREAM_ERROR("LoadAsgContext: truncated ring_buffer state in snapshot.");
        return false;
    }
    uint32_t loaded_buffer_size = 0;
    if (!ReadBe32Exact(stream, &loaded_buffer_size)) {
        GFXSTREAM_ERROR("LoadAsgContext: truncated buffer_size in snapshot.");
        return false;
    }
    // context->buffer was allocated by the host with max_buffer_size bytes;
    // reject any snapshot whose embedded buffer_size exceeds max_buffer_size
    // rather than clamping and desynchronizing subsequent stream reads.
    if (loaded_buffer_size > max_buffer_size) {
        GFXSTREAM_ERROR("LoadAsgContext: snapshot buffer_size %u exceeds allocated %u.",
                        loaded_buffer_size, max_buffer_size);
        return false;
    }
    std::vector<uint8_t> stagedBuffer;
    if (loaded_buffer_size > 0) {
        stagedBuffer.resize(loaded_buffer_size);
        if (!ReadExact(stream, stagedBuffer.data(), loaded_buffer_size)) {
            GFXSTREAM_ERROR("LoadAsgContext: truncated buffer payload (%u bytes) in snapshot.",
                            loaded_buffer_size);
            return false;
        }
    }

    context->buffer_size = max_buffer_size;
    *context->to_host = stagedToHost;
    *context->to_host_large_xfer.ring = stagedToHostLarge;
    *context->from_host_large_xfer.ring = stagedFromHostLarge;
    if (loaded_buffer_size > 0) {
        std::memcpy(context->buffer, stagedBuffer.data(), loaded_buffer_size);
    }
    return true;
}

}  // namespace

RingStream::RingStream(const AsgConsumerCreateInfo& info, size_t bufsize) :
    IOStream(bufsize),
    mContext(CreateContext(info)),
    mSavedRingConfig(*mContext.ring_config),
    mCallbacks(info.callbacks),
    mInitialBufSize(bufsize),
    mBufSize(info.buffer_size) {}

RingStream::~RingStream() = default;

void RingStream::reloadRingConfig() {
    *mContext.ring_config = mSavedRingConfig;
}

void* RingStream::allocBuffer(size_t minSize) {
    if (mWriteBuffer.size() < minSize) {
        mWriteBuffer.resize(minSize);
    }
    return mWriteBuffer.data();
}

int RingStream::commitBuffer(size_t size) {
    size_t sent = 0;
    auto data = mWriteBuffer.data();

    size_t iters = 0;
    size_t backedOffIters = 0;
    const size_t kBackoffIters = 10000000ULL;
    while (sent < size) {
        ++iters;
        auto avail = ring_buffer_available_write(
            mContext.from_host_large_xfer.ring,
            &mContext.from_host_large_xfer.view);

        // Check if the guest process crashed.
        if (!avail) {
            if (*(mContext.host_state) == ASG_HOST_STATE_EXIT) {
                return sent;
            } else {
                ring_buffer_yield();
                if (iters > kBackoffIters) {
                    gfxstream::base::sleepUs(10);
                    ++backedOffIters;
                }
            }
            continue;
        }

        auto remaining = size - sent;
        auto todo = remaining < avail ? remaining : avail;

        ring_buffer_view_write(
            mContext.from_host_large_xfer.ring,
            &mContext.from_host_large_xfer.view,
            data + sent, todo, 1);

        sent += todo;
    }

    if (backedOffIters > 0) {
        GFXSTREAM_WARNING(
            "Backed off %zu times to avoid overloading the guest system. This "
            "may indicate resource constraints or performance issues.",
            backedOffIters);
    }
    return sent;
}

bool RingStream::shouldAbortForSnapshot() const {
    std::lock_guard<std::mutex> lock(mSnapshotStateMutex);
    return mShouldExitForSnapshot && mInSnapshotOperation;
}

void RingStream::onPauseForSnapshot() {
    std::lock_guard<std::mutex> lock(mSnapshotStateMutex);
    if (mInSnapshotOperation) {
        mShouldExitForSnapshot = true;
    }
}

void RingStream::onResumeAfterSnapshot() {
    std::lock_guard<std::mutex> lock(mSnapshotStateMutex);
    mShouldExitForSnapshot = false;
}

const unsigned char* RingStream::readRaw(void* buf, size_t* inout_len) {
    mAbortedReadForSnapshot.store(false, std::memory_order_release);
    size_t wanted = *inout_len;
    size_t count = 0U;
    auto dst = static_cast<char*>(buf);

    uint32_t ringAvailable = 0;
    uint32_t ringLargeXferAvailable = 0;

    const uint32_t maxSpins = 30;
    uint32_t spins = 0;
    bool inLargeXfer = true;

    *(mContext.host_state) = ASG_HOST_STATE_CAN_CONSUME;

    while (count < wanted) {

        if (mReadBufferLeft) {
            size_t avail = std::min<size_t>(wanted - count, mReadBufferLeft);
            memcpy(dst + count,
                    mReadBuffer.data() + (mReadBuffer.size() - mReadBufferLeft),
                    avail);
            count += avail;
            mReadBufferLeft -= avail;
            continue;
        }

        mReadBuffer.clear();

        // no read buffer left...
        if (count > 0) {  // There is some data to return.
            break;
        }

        *(mContext.host_state) = ASG_HOST_STATE_CAN_CONSUME;

        if (mShouldExit.load(std::memory_order_acquire)) {
            mAbortedReadForSnapshot.store(false, std::memory_order_release);
            return nullptr;
        }

        ringAvailable =
            ring_buffer_available_read(mContext.to_host, 0);
        ringLargeXferAvailable =
            ring_buffer_available_read(
                mContext.to_host_large_xfer.ring,
                &mContext.to_host_large_xfer.view);

        auto current = dst + count;
        auto ptrEnd = dst + wanted;

        if (ringAvailable) {
            inLargeXfer = false;
            uint32_t transferMode =
                mContext.ring_config->transfer_mode;
            switch (transferMode) {
                case 1:
                    type1Read(ringAvailable, dst, &count, &current, ptrEnd);
                    break;
                case 2:
                    type2Read(ringAvailable, &count, &current, ptrEnd);
                    break;
                case 3:
                    // emugl::emugl_crash_reporter(
                    //     "Guest should never set to "
                    //     "transfer mode 3 with ringAvailable != 0\n");
                default:
                    // emugl::emugl_crash_reporter(
                    //     "Unknown transfer mode %u\n",
                    //     transferMode);
                    break;
            }
        } else if (ringLargeXferAvailable) {
            type3Read(ringLargeXferAvailable,
                      &count, &current, ptrEnd);
            inLargeXfer = true;
            if (0 == __atomic_load_n(&mContext.ring_config->transfer_size, __ATOMIC_ACQUIRE)) {
                inLargeXfer = false;
            }
        } else {
            if (inLargeXfer && 0 != __atomic_load_n(&mContext.ring_config->transfer_size, __ATOMIC_ACQUIRE)) {
                continue;
            }

            if (inLargeXfer && 0 == __atomic_load_n(&mContext.ring_config->transfer_size, __ATOMIC_ACQUIRE)) {
                inLargeXfer = false;
            }

            if (++spins < maxSpins) {
                ring_buffer_yield();
                continue;
            } else {
                spins = 0;
            }

            if (mShouldExit.load(std::memory_order_acquire)) {
                mAbortedReadForSnapshot.store(false, std::memory_order_release);
                return nullptr;
            }

            if (shouldAbortForSnapshot()) {
                mAbortedReadForSnapshot.store(true, std::memory_order_release);
                return nullptr;
            }

            ++mUnavailableReadCount;
            if (mUnavailableReadCount >= kMaxUnavailableReads) {
                *(mContext.host_state) = ASG_HOST_STATE_NEED_NOTIFY;

                bool sleeping = false;
                do {
                    const AsgOnUnavailableReadStatus status = mCallbacks.onUnavailableRead();
                    switch (status) {
                        case AsgOnUnavailableReadStatus::kContinue: {
                            *(mContext.host_state) = ASG_HOST_STATE_CAN_CONSUME;
                            break;
                        }
                        case AsgOnUnavailableReadStatus::kExit: {
                            *(mContext.host_state) = ASG_HOST_STATE_EXIT;
                            mShouldExit.store(true, std::memory_order_release);
                            break;
                        }
                        case AsgOnUnavailableReadStatus::kSleep: {
                            sleeping = true;
                            break;
                        }
                        case AsgOnUnavailableReadStatus::kPauseForSnapshot: {
                            onPauseForSnapshot();
                            break;
                        }
                        case AsgOnUnavailableReadStatus::kResumeAfterSnapshot: {
                            onResumeAfterSnapshot();
                            break;
                        }
                    }
                } while (sleeping);
            }
            continue;
        }
    }

    *inout_len = count;
    ++mXmits;
    mTotalRecv += count;

    *(mContext.host_state) = ASG_HOST_STATE_RENDERING;
    return (const unsigned char*)buf;
}

void RingStream::type1Read(
    uint32_t available,
    char* begin,
    size_t* count, char** current, const char* ptrEnd) {

    uint32_t xferTotal = available / sizeof(struct asg_type1_xfer);

    if (mType1Xfers.size() < xferTotal) {
        mType1Xfers.resize(xferTotal * 2);
    }

    auto xfersPtr = mType1Xfers.data();

    ring_buffer_copy_contents(
        mContext.to_host, 0, xferTotal * sizeof(struct asg_type1_xfer), (uint8_t*)xfersPtr);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code-loop-increment"
#endif // __clang__
    for (uint32_t i = 0; i < xferTotal; ++i) {
        const asg_type1_xfer& xfer = xfersPtr[i];

        const uint32_t buffer_size = mContext.buffer_size;

        // Guest controls offset/size via shared memory; validate against the
        // host-allocated auxiliary buffer before dereferencing.
        if (xfer.offset >= buffer_size || xfer.size > buffer_size - xfer.offset) {
            GFXSTREAM_ERROR("Invalid type1 xfer: offset %u, size %u, buffer_size %u\n",
                            xfer.offset, xfer.size, buffer_size);
            __atomic_store_n(&mContext.ring_config->in_error, 1, __ATOMIC_RELEASE);
            return;
        }

        if (*current + xfer.size > ptrEnd) {
            // Save in a temp buffer or we'll get stuck
            if (begin == *current && i == 0) {
                const char* src = mContext.buffer + xfer.offset;
                mReadBuffer.resize_noinit(xfer.size);
                memcpy(mReadBuffer.data(), src, xfer.size);
                mReadBufferLeft = xfer.size;
                ring_buffer_advance_read(
                        mContext.to_host, sizeof(struct asg_type1_xfer), 1);
                __atomic_fetch_add(&mContext.ring_config->host_consumed_pos, xfersPtr[i].size, __ATOMIC_RELEASE);
            }
            return;
        }
        const char* src = mContext.buffer + xfer.offset;
        memcpy(*current, src, xfer.size);
        ring_buffer_advance_read(
                mContext.to_host, sizeof(struct asg_type1_xfer), 1);
        __atomic_fetch_add(&mContext.ring_config->host_consumed_pos, xfer.size, __ATOMIC_RELEASE);
        *current += xfer.size;
        *count += xfer.size;

        // TODO: Figure out why running multiple xfers here can result in data
        // corruption and remove clang diagnostic block.
        return;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__
}

void RingStream::type2Read(
    uint32_t available,
    size_t* count, char** current,const char* ptrEnd) {

    GFXSTREAM_FATAL("nyi. abort");

    uint32_t xferTotal = available / sizeof(struct asg_type2_xfer);

    if (mType2Xfers.size() < xferTotal) {
        mType2Xfers.resize(xferTotal * 2);
    }

    auto xfersPtr = mType2Xfers.data();

    ring_buffer_copy_contents(
        mContext.to_host, 0, available, (uint8_t*)xfersPtr);

    for (uint32_t i = 0; i < xferTotal; ++i) {

        if (*current + xfersPtr[i].size > ptrEnd) return;

        const char* src =
            mCallbacks.getPtr(xfersPtr[i].physAddr);

        memcpy(*current, src, xfersPtr[i].size);

        ring_buffer_advance_read(
            mContext.to_host, sizeof(struct asg_type1_xfer), 1);

        *current += xfersPtr[i].size;
        *count += xfersPtr[i].size;
    }
}

void RingStream::type3Read(
    uint32_t available,
    size_t* count, char** current, const char* ptrEnd) {

    uint32_t xferTotal = __atomic_load_n(&mContext.ring_config->transfer_size, __ATOMIC_ACQUIRE);
    uint32_t maxCanRead = ptrEnd - *current;
    uint32_t ringAvail = available;
    uint32_t actuallyRead = std::min(ringAvail, std::min(xferTotal, maxCanRead));

    // Decrement transfer_size before letting the guest proceed in ring_buffer funcs or we will race
    // to the next time the guest sets transfer_size
    __atomic_fetch_sub(&mContext.ring_config->transfer_size, actuallyRead, __ATOMIC_RELEASE);

    ring_buffer_read_fully_with_abort(
            mContext.to_host_large_xfer.ring,
            &mContext.to_host_large_xfer.view,
            *current, actuallyRead,
            1, &mContext.ring_config->in_error);

    *current += actuallyRead;
    *count += actuallyRead;
}

void* RingStream::getDmaForReading(uint64_t guest_paddr) {
    return gfxstream::host::g_gfxstream_dma_get_host_addr(guest_paddr);
}

void RingStream::unlockDma(uint64_t guest_paddr) {
    gfxstream::host::g_gfxstream_dma_unlock(guest_paddr);
}

int RingStream::writeFully(const void* buf, size_t len) {
    void* dstBuf = alloc(len);
    memcpy(dstBuf, buf, len);
    flush();
    return 0;
}

const unsigned char *RingStream::readFully( void *buf, size_t len) {
    GFXSTREAM_FATAL("not intended for use with RingStream");
    return nullptr;
}

void RingStream::onSave(gfxstream::Stream* stream) {
    stream->putBe32(mReadBufferLeft);
    if (mReadBufferLeft > 0) {
        WriteExact(stream, mReadBuffer.data() + mReadBuffer.size() - mReadBufferLeft,
                   mReadBufferLeft);
    }

    if (m_buf != nullptr) {
        if (!gfxstream::host::saveBuffer(stream, mWriteBuffer)) {
            stream->addError("RingStream::onSave: failed to save mWriteBuffer");
        }
    } else {
        stream->putBe32(0);
    }

    stream->putBe32(mUnavailableReadCount);

    mSavedRingConfig = *mContext.ring_config;

    SaveRingConfig(stream, mSavedRingConfig);

    SaveAsgContext(stream, mContext);
}

unsigned char* RingStream::onLoad(gfxstream::Stream* stream) {
    auto resetOnLoadError = [this, stream]() -> unsigned char* {
        mReadBuffer.clear();
        mWriteBuffer.clear();
        mReadBufferLeft = 0;
        m_bufsize = mInitialBufSize;
        m_free = 0;
        if (stream) {
            stream->addError("RingStream::onLoad failed");
        }
        return nullptr;
    };

    if (!stream) {
        return resetOnLoadError();
    }

    const uint32_t maxReadBufBytes = std::max<uint32_t>(mBufSize, kMaxRingStreamIoBufferBytes);
    RenderChannel::Buffer stagedReadBuffer;
    RenderChannel::Buffer stagedWriteBuffer;
    uint32_t stagedUnavailableReadCount = 0;
    struct asg_ring_config stagedRingConfig = {};
    if (!LoadBoundedBuffer(stream, &stagedReadBuffer, maxReadBufBytes) ||
        !LoadBoundedBuffer(stream, &stagedWriteBuffer, kMaxRingStreamIoBufferBytes) ||
        !ReadBe32Exact(stream, &stagedUnavailableReadCount) ||
        !LoadRingConfig(stream, &stagedRingConfig)) {
        return resetOnLoadError();
    }
    if (stagedRingConfig.buffer_size > mBufSize) {
        stagedRingConfig.buffer_size = mBufSize;
    }

    if (!LoadAsgContext(stream, &mContext, mBufSize)) {
        return resetOnLoadError();
    }

    mReadBuffer = std::move(stagedReadBuffer);
    mReadBufferLeft = mReadBuffer.size();
    mWriteBuffer = std::move(stagedWriteBuffer);
    mUnavailableReadCount = stagedUnavailableReadCount;
    mSavedRingConfig = stagedRingConfig;

    if (!mWriteBuffer.empty()) {
        m_bufsize = std::min(m_bufsize, mWriteBuffer.size());
        m_free = std::min(m_free, m_bufsize);
    } else {
        m_bufsize = mInitialBufSize;
        m_free = 0;
    }

    return mWriteBuffer.empty() ? nullptr : reinterpret_cast<unsigned char*>(mWriteBuffer.data());
}

}  // namespace gfxstream
