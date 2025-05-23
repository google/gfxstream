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

#include "RingStream.h"

#include <assert.h>
#include <memory.h>

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

void LoadRingConfig(Stream* stream, struct asg_ring_config* config) {
    config->buffer_size = stream->getBe32();
    config->flush_interval = stream->getBe32();
    config->host_consumed_pos = stream->getBe32();
    config->guest_write_pos = stream->getBe32();
    config->transfer_mode = stream->getBe32();
    config->transfer_size = stream->getBe32();
    config->in_error = stream->getBe32();
}

}  // namespace

RingStream::RingStream(const AsgConsumerCreateInfo& info, size_t bufsize) :
    IOStream(bufsize),
    mContext(CreateContext(info)),
    mSavedRingConfig(*mContext.ring_config),
    mCallbacks(info.callbacks) {}

RingStream::~RingStream() = default;

void RingStream::reloadRingConfig() {
    *mContext.ring_config = mSavedRingConfig;
}

void* RingStream::allocBuffer(size_t minSize) {
    if (mWriteBuffer.size() < minSize) {
        mWriteBuffer.resize_noinit(minSize);
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

const unsigned char* RingStream::readRaw(void* buf, size_t* inout_len) {
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

        if (mShouldExit) {
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

            if (mShouldExit) {
                return nullptr;
            }

            if (mShouldExitForSnapshot && mInSnapshotOperation) {
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
                            mShouldExit = true;
                            break;
                        }
                        case AsgOnUnavailableReadStatus::kSleep: {
                            sleeping = true;
                            break;
                        }
                        case AsgOnUnavailableReadStatus::kPauseForSnapshot: {
                            mShouldExitForSnapshot = true;
                            break;
                        }
                        case AsgOnUnavailableReadStatus::kResumeAfterSnapshot: {
                            mShouldExitForSnapshot = false;
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
        if (*current + xfersPtr[i].size > ptrEnd) {
            // Save in a temp buffer or we'll get stuck
            if (begin == *current && i == 0) {
                const char* src = mContext.buffer + xfersPtr[i].offset;
                mReadBuffer.resize_noinit(xfersPtr[i].size);
                memcpy(mReadBuffer.data(), src, xfersPtr[i].size);
                mReadBufferLeft = xfersPtr[i].size;
                ring_buffer_advance_read(
                        mContext.to_host, sizeof(struct asg_type1_xfer), 1);
                __atomic_fetch_add(&mContext.ring_config->host_consumed_pos, xfersPtr[i].size, __ATOMIC_RELEASE);
            }
            return;
        }
        const char* src = mContext.buffer + xfersPtr[i].offset;
        memcpy(*current, src, xfersPtr[i].size);
        ring_buffer_advance_read(
                mContext.to_host, sizeof(struct asg_type1_xfer), 1);
        __atomic_fetch_add(&mContext.ring_config->host_consumed_pos, xfersPtr[i].size, __ATOMIC_RELEASE);
        *current += xfersPtr[i].size;
        *count += xfersPtr[i].size;

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
    return gfxstream::g_gfxstream_dma_get_host_addr(guest_paddr);
}

void RingStream::unlockDma(uint64_t guest_paddr) { gfxstream::g_gfxstream_dma_unlock(guest_paddr); }

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
    stream->write(mReadBuffer.data() + mReadBuffer.size() - mReadBufferLeft,
                  mReadBufferLeft);

    gfxstream::saveBuffer(stream, mWriteBuffer);

    stream->putBe32(mUnavailableReadCount);

    SaveRingConfig(stream, mSavedRingConfig);
}

unsigned char* RingStream::onLoad(gfxstream::Stream* stream) {
    gfxstream::loadBuffer(stream, &mReadBuffer);
    mReadBufferLeft = mReadBuffer.size();

    gfxstream::loadBuffer(stream, &mWriteBuffer);

    mUnavailableReadCount = stream->getBe32();

    LoadRingConfig(stream, &mSavedRingConfig);

    return reinterpret_cast<unsigned char*>(mWriteBuffer.data());
}

}  // namespace gfxstream
