// Copyright (C) 2026 The Android Open Source Project
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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "gfxstream/host/address_space_device.h"
#include "gfxstream/host/address_space_graphics.h"
#include "gfxstream/host/address_space_graphics_types.h"
#include "gfxstream/host/mem_stream.h"
#include "gfxstream/host/ring_buffer.h"
#include "gfxstream/host/stream_utils.h"
#include "render-utils/address_space_operations.h"
#include "render-utils/snapshot_operations.h"
#include "renderer_impl.h"

namespace gfxstream {
namespace host {
namespace {

constexpr uint32_t kTestInitialStreamBufSize = 128 * 1024;
constexpr uint32_t kTestSmallAuxBufSize = 4096;
constexpr uint32_t kTestSmallFlushInterval = 1024;

AsgConsumerCreateInfo MakeCreateInfo(char* ringStorage, char* auxBuffer, uint32_t auxBufSize,
                                     uint32_t flushInterval) {
    AsgConsumerCreateInfo info = {};
    info.version = 1;
    info.ring_storage = ringStorage;
    info.buffer = auxBuffer;
    info.buffer_size = auxBufSize;
    info.buffer_flush_interval = flushInterval;
    info.callbacks.onUnavailableRead = []() { return AsgOnUnavailableReadStatus::kExit; };
    info.callbacks.getPtr = [](uint64_t) -> char* { return nullptr; };
    return info;
}

struct asg_context AttachGuestContext(char* ringStorage, char* auxBuffer, uint32_t auxBufSize) {
    struct asg_context ctx = {};
    ctx.to_host = reinterpret_cast<struct ring_buffer*>(
        ringStorage + offsetof(struct asg_ring_storage, to_host));
    ctx.to_host_large_xfer.ring = reinterpret_cast<struct ring_buffer*>(
        ringStorage + offsetof(struct asg_ring_storage, to_host_large_xfer));
    ctx.from_host_large_xfer.ring = reinterpret_cast<struct ring_buffer*>(
        ringStorage + offsetof(struct asg_ring_storage, from_host_large_xfer));
    ctx.buffer = auxBuffer;
    ctx.buffer_size = auxBufSize;
    ctx.host_state = reinterpret_cast<asg_host_state*>(&ctx.to_host->state);
    ctx.ring_config = reinterpret_cast<asg_ring_config*>(ctx.to_host->config);
    ring_buffer_init_view_only(&ctx.to_host_large_xfer.view,
                               reinterpret_cast<uint8_t*>(auxBuffer), auxBufSize);
    ring_buffer_init_view_only(&ctx.from_host_large_xfer.view,
                               reinterpret_cast<uint8_t*>(auxBuffer), auxBufSize);
    return ctx;
}

void WriteSyntheticRingStreamSnapshot(MemStream* stream, uint32_t ioBufSize, uint32_t ioFree,
                                      bool haveBuf, const std::vector<char>& writeBuf,
                                      uint32_t ringConfigBufSize,
                                      const struct ring_buffer& toHost,
                                      const struct ring_buffer& toHostLarge,
                                      const struct ring_buffer& fromHostLarge,
                                      uint32_t savedAuxBufSize,
                                      const std::vector<uint8_t>& savedAuxBytes) {
    // IOStream::save() header.
    stream->putBe32(ioBufSize);
    stream->putBe32(ioFree);
    stream->putByte(haveBuf ? 1 : 0);

    // RingStream::onSave(): mReadBuffer (empty).
    stream->putBe32(0);

    // RingStream::onSave(): mWriteBuffer.
    stream->putBe32(static_cast<uint32_t>(writeBuf.size()));
    if (!writeBuf.empty()) {
        stream->write(writeBuf.data(), writeBuf.size());
    }

    // mUnavailableReadCount.
    stream->putBe32(0);

    // SaveRingConfig().
    stream->putBe32(ringConfigBufSize);
    stream->putBe32(kTestSmallFlushInterval);
    stream->putBe32(12);  // host_consumed_pos
    stream->putBe32(34);  // guest_write_pos
    stream->putBe32(1);   // transfer_mode
    stream->putBe32(0);   // transfer_size
    stream->putBe32(0);   // in_error

    // SaveAsgContext().
    stream->write(&toHost, sizeof(struct ring_buffer));
    stream->write(&toHostLarge, sizeof(struct ring_buffer));
    stream->write(&fromHostLarge, sizeof(struct ring_buffer));
    stream->putBe32(savedAuxBufSize);
    if (!savedAuxBytes.empty()) {
        stream->write(savedAuxBytes.data(), savedAuxBytes.size());
    }
}

TEST(RingStreamSnapshotTest, IdleSaveOmitsSharedBufferAndWriteBuffer) {
    std::vector<char> ringStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<char> auxBuffer(kAsgWriteBufferSize, 0);
    // Pre-fill tail of auxBuffer with a sentinel pattern to verify load preserves backing memory.
    for (size_t i = kAsgWriteBufferSize - 256; i < kAsgWriteBufferSize; ++i) {
        auxBuffer[i] = static_cast<char>((i & 0xFF) ^ 0xA5);
    }

    auto info = MakeCreateInfo(ringStorage.data(), auxBuffer.data(), kAsgWriteBufferSize,
                               kAsgWriteStepSize);
    RingStream saveStreamObj(info, kTestInitialStreamBufSize);

    // Send a reply and flush so mWriteBuffer grows to 128 KiB and then returns to idle
    // (m_buf == nullptr).
    const std::vector<uint8_t> reply1 = {0x10, 0x20, 0x30, 0x40, 0x55, 0x66, 0x77, 0x88};
    ASSERT_EQ(saveStreamObj.writeFully(reply1.data(), reply1.size()), 0);

    auto saveGuestCtx = AttachGuestContext(ringStorage.data(), auxBuffer.data(),
                                           kAsgWriteBufferSize);
    saveGuestCtx.ring_config->host_consumed_pos = 128;
    saveGuestCtx.ring_config->guest_write_pos = 256;
    saveGuestCtx.ring_config->transfer_mode = 2;

    MemStream snapshot;
    saveStreamObj.save(&snapshot);

    // Must omit both the 1 MiB auxiliary transfer buffer and the idle 128 KiB mWriteBuffer.
    EXPECT_LT(snapshot.writtenSize(), 16 * 1024U);

    // Simulate backing-store restore: auxBuffer comes from goldfish_address_space / RingBlob,
    // while ringStorage starts clean before RingStream::load() + reloadRingConfig().
    std::vector<char> restoredRingStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<char> restoredAuxBuffer = auxBuffer;
    auto loadInfo = MakeCreateInfo(restoredRingStorage.data(), restoredAuxBuffer.data(),
                                   kAsgWriteBufferSize, kAsgWriteStepSize);
    RingStream loadStreamObj(loadInfo, kTestInitialStreamBufSize);

    snapshot.rewind();
    loadStreamObj.load(&snapshot);
    loadStreamObj.reloadRingConfig();

    // Backing-store auxiliary buffer must be completely preserved untouched.
    EXPECT_EQ(restoredAuxBuffer, auxBuffer);

    auto loadGuestCtx = AttachGuestContext(restoredRingStorage.data(), restoredAuxBuffer.data(),
                                           kAsgWriteBufferSize);
    EXPECT_EQ(loadGuestCtx.ring_config->host_consumed_pos, 128U);
    EXPECT_EQ(loadGuestCtx.ring_config->guest_write_pos, 256U);
    EXPECT_EQ(loadGuestCtx.ring_config->transfer_mode, 2U);

    // Guest reads reply1 committed before snapshot using the restored ring pointers and
    // backing-store aux buffer.
    ASSERT_EQ(ring_buffer_available_read(loadGuestCtx.from_host_large_xfer.ring,
                                         &loadGuestCtx.from_host_large_xfer.view),
              reply1.size());
    std::vector<uint8_t> received1(reply1.size(), 0);
    EXPECT_EQ(ring_buffer_view_read(loadGuestCtx.from_host_large_xfer.ring,
                                    &loadGuestCtx.from_host_large_xfer.view,
                                    received1.data(), received1.size(), 1),
              1);
    EXPECT_EQ(received1, reply1);

    // Subsequent alloc/flush on the restored idle RingStream must work normally.
    const std::vector<uint8_t> reply2 = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    ASSERT_EQ(loadStreamObj.writeFully(reply2.data(), reply2.size()), 0);
    ASSERT_EQ(ring_buffer_available_read(loadGuestCtx.from_host_large_xfer.ring,
                                         &loadGuestCtx.from_host_large_xfer.view),
              reply2.size());
    std::vector<uint8_t> received2(reply2.size(), 0);
    EXPECT_EQ(ring_buffer_view_read(loadGuestCtx.from_host_large_xfer.ring,
                                    &loadGuestCtx.from_host_large_xfer.view,
                                    received2.data(), received2.size(), 1),
              1);
    EXPECT_EQ(received2, reply2);
}

TEST(RingStreamSnapshotTest, ActiveSavePreservesUnflushedWriteBuffer) {
    std::vector<char> ringStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<char> auxBuffer(kTestSmallAuxBufSize, 0);
    auto info = MakeCreateInfo(ringStorage.data(), auxBuffer.data(), kTestSmallAuxBufSize,
                               kTestSmallFlushInterval);
    RingStream saveStreamObj(info, 4096);

    const std::vector<uint8_t> pending = {0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49};
    unsigned char* dst = saveStreamObj.alloc(pending.size());
    ASSERT_NE(dst, nullptr);
    std::memcpy(dst, pending.data(), pending.size());

    MemStream snapshot;
    saveStreamObj.save(&snapshot);

    std::vector<char> restoredRingStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<char> restoredAuxBuffer = auxBuffer;
    auto loadInfo = MakeCreateInfo(restoredRingStorage.data(), restoredAuxBuffer.data(),
                                   kTestSmallAuxBufSize, kTestSmallFlushInterval);
    RingStream loadStreamObj(loadInfo, 4096);

    snapshot.rewind();
    loadStreamObj.load(&snapshot);

    // Flushing the restored stream must commit the buffered bytes saved while m_buf != nullptr.
    EXPECT_EQ(loadStreamObj.flush(), static_cast<int>(pending.size()));

    auto loadGuestCtx = AttachGuestContext(restoredRingStorage.data(), restoredAuxBuffer.data(),
                                           kTestSmallAuxBufSize);
    ASSERT_EQ(ring_buffer_available_read(loadGuestCtx.from_host_large_xfer.ring,
                                         &loadGuestCtx.from_host_large_xfer.view),
              pending.size());
    std::vector<uint8_t> received(pending.size(), 0);
    EXPECT_EQ(ring_buffer_view_read(loadGuestCtx.from_host_large_xfer.ring,
                                    &loadGuestCtx.from_host_large_xfer.view,
                                    received.data(), received.size(), 1),
              1);
    EXPECT_EQ(received, pending);
}

TEST(RingStreamSnapshotTest, BackwardsCompatibleLoadWithNonZeroBufferSize) {
    // Prepare a real ring state with reply bytes written into a legacy aux buffer.
    std::vector<char> legacyRingStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<uint8_t> legacyAuxBytes(kTestSmallAuxBufSize, 0);
    auto legacyInfo = MakeCreateInfo(legacyRingStorage.data(),
                                     reinterpret_cast<char*>(legacyAuxBytes.data()),
                                     kTestSmallAuxBufSize, kTestSmallFlushInterval);
    RingStream legacyProducer(legacyInfo, 4096);
    const std::vector<uint8_t> reply = {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45};
    ASSERT_EQ(legacyProducer.writeFully(reply.data(), reply.size()), 0);

    auto legacyCtx = AttachGuestContext(legacyRingStorage.data(),
                                        reinterpret_cast<char*>(legacyAuxBytes.data()),
                                        kTestSmallAuxBufSize);

    MemStream legacySnapshot;
    std::vector<char> legacyIdleWriteBuf(4096, 0);
    WriteSyntheticRingStreamSnapshot(&legacySnapshot, /*ioBufSize=*/4096, /*ioFree=*/0,
                                     /*haveBuf=*/false, legacyIdleWriteBuf,
                                     /*ringConfigBufSize=*/kTestSmallAuxBufSize,
                                     *legacyCtx.to_host, *legacyCtx.to_host_large_xfer.ring,
                                     *legacyCtx.from_host_large_xfer.ring,
                                     /*savedAuxBufSize=*/kTestSmallAuxBufSize, legacyAuxBytes);

    // Destination auxBuffer starts zeroed; LoadAsgContext must restore it from the legacy stream.
    std::vector<char> restoredRingStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<char> restoredAuxBuffer(kTestSmallAuxBufSize, 0);
    auto loadInfo = MakeCreateInfo(restoredRingStorage.data(), restoredAuxBuffer.data(),
                                   kTestSmallAuxBufSize, kTestSmallFlushInterval);
    RingStream loadStreamObj(loadInfo, 4096);

    legacySnapshot.rewind();
    loadStreamObj.load(&legacySnapshot);

    EXPECT_EQ(std::memcmp(restoredAuxBuffer.data(), legacyAuxBytes.data(), kTestSmallAuxBufSize),
              0);

    auto loadGuestCtx = AttachGuestContext(restoredRingStorage.data(), restoredAuxBuffer.data(),
                                           kTestSmallAuxBufSize);
    ASSERT_EQ(ring_buffer_available_read(loadGuestCtx.from_host_large_xfer.ring,
                                         &loadGuestCtx.from_host_large_xfer.view),
              reply.size());
    std::vector<uint8_t> received(reply.size(), 0);
    EXPECT_EQ(ring_buffer_view_read(loadGuestCtx.from_host_large_xfer.ring,
                                    &loadGuestCtx.from_host_large_xfer.view,
                                    received.data(), received.size(), 1),
              1);
    EXPECT_EQ(received, reply);
}

TEST(RingStreamSnapshotTest, LoadHardeningClampsUntrustedBufSizeAndBufferSize) {
    struct ring_buffer emptyRing = {};
    ring_buffer_init(&emptyRing);

    // Case 1 & 2: m_bufsize = 0xFFFFFFFF with empty mWriteBuffer (both haveBuf=false and
    // haveBuf=true) must reset m_bufsize to mInitialBufSize and never attempt a 4 GiB alloc.
    for (bool haveBuf : {false, true}) {
        MemStream crafted;
        WriteSyntheticRingStreamSnapshot(&crafted, /*ioBufSize=*/0xFFFFFFFFU,
                                         /*ioFree=*/0xFFFFFFFFU, haveBuf,
                                         /*writeBuf=*/{},
                                         /*ringConfigBufSize=*/kTestSmallAuxBufSize, emptyRing,
                                         emptyRing, emptyRing, /*savedAuxBufSize=*/0, {});

        std::vector<char> ringStorage(kAsgConsumerRingStorageSize, 0);
        std::vector<char> auxBuffer(kTestSmallAuxBufSize, 0);
        auto info = MakeCreateInfo(ringStorage.data(), auxBuffer.data(), kTestSmallAuxBufSize,
                                   kTestSmallFlushInterval);
        RingStream streamObj(info, 4096);

        crafted.rewind();
        streamObj.load(&crafted);

        const std::vector<uint8_t> payload(64, 0x5A);
        unsigned char* dst = streamObj.alloc(payload.size());
        ASSERT_NE(dst, nullptr);
        std::memcpy(dst, payload.data(), payload.size());
        EXPECT_EQ(streamObj.flush(), static_cast<int>(payload.size()));

        auto guestCtx = AttachGuestContext(ringStorage.data(), auxBuffer.data(),
                                           kTestSmallAuxBufSize);
        std::vector<uint8_t> received(payload.size(), 0);
        EXPECT_EQ(ring_buffer_view_read(guestCtx.from_host_large_xfer.ring,
                                        &guestCtx.from_host_large_xfer.view,
                                        received.data(), received.size(), 1),
                  1);
        EXPECT_EQ(received, payload);
    }

    // Case 3a: valid savedAuxBufSize == 0 with oversized ring_config.buffer_size clamps
    // ring_config.buffer_size to mBufSize.
    constexpr size_t kGuardBytes = 128;
    constexpr char kGuardFill = static_cast<char>(0xCC);
    std::vector<char> ringStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<char> guardedAuxBuffer(kTestSmallAuxBufSize + kGuardBytes, kGuardFill);
    std::memset(guardedAuxBuffer.data(), 0, kTestSmallAuxBufSize);

    MemStream clampOnlyStream;
    WriteSyntheticRingStreamSnapshot(&clampOnlyStream, /*ioBufSize=*/4096, /*ioFree=*/0,
                                     /*haveBuf=*/false, /*writeBuf=*/{},
                                     /*ringConfigBufSize=*/kTestSmallAuxBufSize + 4096, emptyRing,
                                     emptyRing, emptyRing, /*savedAuxBufSize=*/0, {});

    auto info = MakeCreateInfo(ringStorage.data(), guardedAuxBuffer.data(), kTestSmallAuxBufSize,
                               kTestSmallFlushInterval);
    RingStream streamObj(info, 4096);
    clampOnlyStream.rewind();
    streamObj.load(&clampOnlyStream);
    EXPECT_FALSE(clampOnlyStream.hasErrors());
    streamObj.reloadRingConfig();

    auto guestCtx = AttachGuestContext(ringStorage.data(), guardedAuxBuffer.data(),
                                       kTestSmallAuxBufSize);
    EXPECT_EQ(guestCtx.ring_config->buffer_size, kTestSmallAuxBufSize);

    // Case 3b: oversized loaded_buffer_size in LoadAsgContext is rejected without writing into or
    // past context.buffer AND without mutating pre-existing ring headers or mSavedRingConfig.
    RingStream rollbackObj(info, 4096);
    guestCtx.ring_config->transfer_size = 99u;
    MemStream baselineSnapshot;
    rollbackObj.save(&baselineSnapshot);
    baselineSnapshot.rewind();
    rollbackObj.load(&baselineSnapshot);
    const asg_type1_xfer preExistingXfer = {.offset = 16, .size = 32};
    ASSERT_EQ(ring_buffer_write(guestCtx.to_host, &preExistingXfer, sizeof(preExistingXfer), 1),
              1);

    const std::vector<uint8_t> oversizedAuxBytes(kTestSmallAuxBufSize + kGuardBytes, 0x33);
    MemStream craftedOverflow;
    WriteSyntheticRingStreamSnapshot(&craftedOverflow, /*ioBufSize=*/4096, /*ioFree=*/0,
                                     /*haveBuf=*/false, /*writeBuf=*/{},
                                     /*ringConfigBufSize=*/kTestSmallAuxBufSize + 4096, emptyRing,
                                     emptyRing, emptyRing,
                                     /*savedAuxBufSize=*/kTestSmallAuxBufSize + kGuardBytes,
                                     oversizedAuxBytes);
    craftedOverflow.rewind();
    rollbackObj.load(&craftedOverflow);
    EXPECT_TRUE(craftedOverflow.hasErrors());
    rollbackObj.reloadRingConfig();

    EXPECT_EQ(guestCtx.ring_config->buffer_size, kTestSmallAuxBufSize);
    EXPECT_EQ(guestCtx.ring_config->transfer_size, 99u);
    EXPECT_EQ(ring_buffer_available_read(guestCtx.to_host, nullptr), sizeof(preExistingXfer));
    for (size_t i = 0; i < kTestSmallAuxBufSize; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(guardedAuxBuffer[i]), 0U);
    }
    for (size_t i = kTestSmallAuxBufSize; i < guardedAuxBuffer.size(); ++i) {
        EXPECT_EQ(guardedAuxBuffer[i], kGuardFill) << "Guard overwritten at index " << i;
    }

    // Case 4: truncated mWriteBuffer stream fails cleanly and clears buffers instead of retaining
    // uninitialized resize_noinit bytes. Set m_free < m_bufsize so IOStream::flush() would invoke
    // commitBuffer() if m_buf had not been reset to nullptr.
    MemStream truncatedWriteBuf;
    truncatedWriteBuf.putBe32(4096);        // IOStream::m_bufsize
    truncatedWriteBuf.putBe32(4096 - 128);  // IOStream::m_free (128 dirty bytes claimed)
    truncatedWriteBuf.putByte(1);           // haveBuf
    truncatedWriteBuf.putBe32(0);           // mReadBuffer length = 0
    truncatedWriteBuf.putBe32(256);         // mWriteBuffer claims 256 bytes, but stream ends here
    truncatedWriteBuf.rewind();

    RingStream truncatedObj(info, 4096);
    truncatedObj.load(&truncatedWriteBuf);
    EXPECT_EQ(truncatedObj.flush(), 0);
    EXPECT_EQ(ring_buffer_available_read(guestCtx.from_host_large_xfer.ring,
                                         &guestCtx.from_host_large_xfer.view),
              0u);

    // Case 5: corrupt 4 GiB (0xFFFFFFFF) mReadBuffer / mWriteBuffer length prefixes and truncated
    // length prefixes reject cleanly via LoadBoundedBuffer without calling SmallVector::resize_noinit
    // (which would abort() on OOM).
    MemStream hugeReadBufLen;
    hugeReadBufLen.putBe32(4096);
    hugeReadBufLen.putBe32(0);
    hugeReadBufLen.putByte(0);
    hugeReadBufLen.putBe32(0xFFFFFFFFu);  // mReadBuffer length > maxReadBufBytes
    hugeReadBufLen.rewind();
    RingStream hugeReadObj(info, 4096);
    hugeReadObj.load(&hugeReadBufLen);
    EXPECT_TRUE(hugeReadBufLen.hasErrors());

    MemStream hugeWriteBufLen;
    hugeWriteBufLen.putBe32(4096);
    hugeWriteBufLen.putBe32(0);
    hugeWriteBufLen.putByte(1);
    hugeWriteBufLen.putBe32(0);            // mReadBuffer length = 0
    hugeWriteBufLen.putBe32(0xFFFFFFFFu);  // mWriteBuffer length > kMaxRingStreamIoBufferBytes
    hugeWriteBufLen.rewind();
    RingStream hugeWriteObj(info, 4096);
    hugeWriteObj.load(&hugeWriteBufLen);
    EXPECT_TRUE(hugeWriteBufLen.hasErrors());
    EXPECT_EQ(hugeWriteObj.flush(), 0);
}

struct ScopedThread {
    std::thread thread;
    std::function<void()> onUnwind;

    explicit ScopedThread(std::thread t, std::function<void()> unwind = {})
        : thread(std::move(t)), onUnwind(std::move(unwind)) {}

    ScopedThread(const ScopedThread&) = delete;
    ScopedThread& operator=(const ScopedThread&) = delete;

    void join() {
        if (thread.joinable()) {
            thread.join();
        }
    }

    ~ScopedThread() {
        if (thread.joinable()) {
            if (onUnwind) {
                onUnwind();
            }
            thread.join();
        }
    }
};

class FailingWriteStream : public gfxstream::StreamWithErrorLogger {
  public:
    explicit FailingWriteStream(size_t failAfterBytes = 0) : mFailAfterBytes(failAfterBytes) {}

    ssize_t read(void*, size_t) override { return -1; }
    ssize_t write(const void*, size_t size) override {
        if (mWritten + size > mFailAfterBytes) {
            return -1;
        }
        mWritten += size;
        return static_cast<ssize_t>(size);
    }

  private:
    size_t mFailAfterBytes = 0;
    size_t mWritten = 0;
};

// Simulates an ASG RenderThread that commits a reply into RingStream's context.buffer before
// parking for snapshot, verifying that gfxstream_address_space_pre_save() waits for it to park
// before VirtioGpuFrontend snapshots the RingBlob backing memory.
class AddressSpaceDeviceSnapshotTest : public ::testing::Test {
  protected:
    std::vector<char>& allocBackingBuffer(
            size_t size = kAsgConsumerRingStorageSize + kAsgWriteBufferSize) {
        mBackingBuffers.push_back(std::make_unique<std::vector<char>>(size, 0));
        return *mBackingBuffers.back();
    }

    template <typename T, typename... Args>
    std::shared_ptr<T> makeRetainedState(Args&&... args) {
        auto state = std::make_shared<T>(std::forward<Args>(args)...);
        mRetainedStates.push_back(state);
        return state;
    }

    void TearDown() override {
        GetAsgOperations().clear();
        AddressSpaceGraphicsContext::clear();
        AddressSpaceGraphicsContext::setConsumer({});
        gfxstream_address_space_set_load_resources({});
        mRetainedStates.clear();
        mBackingBuffers.clear();
    }

  private:
    std::vector<std::unique_ptr<std::vector<char>>> mBackingBuffers;
    std::vector<std::shared_ptr<void>> mRetainedStates;
};

TEST_F(AddressSpaceDeviceSnapshotTest, PreSaveQuiescesConsumerBeforeBackingStoreSnapshot) {
    struct MockConsumer {
        std::unique_ptr<RingStream> ringStream;
        std::mutex mu;
        std::condition_variable cv;
        bool pauseRequested = false;
        bool parked = false;
        bool resumeRequested = false;
        bool exiting = false;
        std::optional<MemStream> cachedSnapshot;
        std::thread worker;
    };

    struct TestState {
        std::vector<uint8_t> inflightReply = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        std::atomic<int> preSaveCalls{0};
        std::atomic<int> waitForPauseCalls{0};
        std::atomic<int> postSaveCalls{0};
        MockConsumer* restoredConsumerPtr = nullptr;
    };
    auto state = makeRetainedState<TestState>();

    std::vector<char>& combinedMemory = allocBackingBuffer();
    std::vector<char>& restoredCombinedMemory = allocBackingBuffer();

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo& info, Stream* loadStream) -> ConsumerHandle {
            auto* c = new MockConsumer();
            c->ringStream = std::make_unique<RingStream>(info, kTestInitialStreamBufSize);
            if (loadStream) {
                c->ringStream->load(loadStream);
                c->ringStream->reloadRingConfig();
                state->restoredConsumerPtr = c;
            } else {
                const auto reply = state->inflightReply;
                c->worker = std::thread([c, reply]() {
                    std::unique_lock<std::mutex> lock(c->mu);
                    c->cv.wait(lock, [c]() { return c->pauseRequested || c->exiting; });
                    if (c->exiting) {
                        return;
                    }
                    lock.unlock();

                    // Simulate an in-flight command committing reply bytes into context.buffer
                    // after preSave is signaled, before parking for snapshot.
                    c->ringStream->writeFully(reply.data(), reply.size());

                    MemStream saved;
                    c->ringStream->save(&saved);

                    lock.lock();
                    c->cachedSnapshot = std::move(saved);
                    c->parked = true;
                    c->cv.notify_all();
                    c->cv.wait(lock, [c]() { return c->resumeRequested || c->exiting; });
                    c->parked = false;
                    c->pauseRequested = false;
                    c->resumeRequested = false;
                });
            }
            return c;
        },
        .destroy = [](ConsumerHandle handle) {
            auto* c = static_cast<MockConsumer*>(handle);
            {
                std::lock_guard<std::mutex> lock(c->mu);
                c->exiting = true;
                c->cv.notify_all();
            }
            if (c->worker.joinable()) {
                c->worker.join();
            }
            delete c;
        },
        .preSave = [state](ConsumerHandle handle) {
            ++state->preSaveCalls;
            auto* c = static_cast<MockConsumer*>(handle);
            {
                std::lock_guard<std::mutex> lock(c->mu);
                c->pauseRequested = true;
                c->cv.notify_all();
            }
        },
        .globalPreSave = []() {},
        .save = [](ConsumerHandle handle, Stream* stream) {
            auto* c = static_cast<MockConsumer*>(handle);
            std::unique_lock<std::mutex> lock(c->mu);
            c->cv.wait(lock, [c]() { return c->parked; });
            ASSERT_TRUE(c->cachedSnapshot.has_value());
            c->cachedSnapshot->rewind();
            stream->write(c->cachedSnapshot->buffer().data(),
                          c->cachedSnapshot->writtenSize());
        },
        .globalPostSave = []() {},
        .postSave = [state](ConsumerHandle handle) {
            ++state->postSaveCalls;
            auto* c = static_cast<MockConsumer*>(handle);
            {
                std::lock_guard<std::mutex> lock(c->mu);
                c->resumeRequested = true;
                c->cv.notify_all();
            }
        },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [state](ConsumerHandle handle) -> bool {
            ++state->waitForPauseCalls;
            auto* c = static_cast<MockConsumer*>(handle);
            std::unique_lock<std::mutex> lock(c->mu);
            c->cv.wait(lock, [c]() { return c->parked; });
            return true;
        },
    });

    auto ops = GetAsgOperations();
    const uint32_t handle = ops.gen_handle();
    AddressSpaceCreateInfo createInfo = {};
    createInfo.handle = handle;
    createInfo.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    createInfo.createRenderThread = true;
    createInfo.externalAddr = combinedMemory.data();
    createInfo.externalAddrSize = combinedMemory.size();
    createInfo.virtioGpuContextId = 7;
    ops.create_instance(createInfo);

    // 1. Pre-save must pause and wait for the worker thread to finish committing its reply
    //    and park before returning.
    EXPECT_EQ(gfxstream_address_space_pre_save(), 0);
    EXPECT_EQ(state->preSaveCalls.load(), 1);
    EXPECT_EQ(state->waitForPauseCalls.load(), 1);
    EXPECT_EQ(state->postSaveCalls.load(), 0);

    // Calling pre_save again while already in PreSaved state is an error and must not
    // re-invoke preSave or waitForPause.
    EXPECT_NE(gfxstream_address_space_pre_save(), 0);
    EXPECT_EQ(state->preSaveCalls.load(), 1);
    EXPECT_EQ(state->waitForPauseCalls.load(), 1);

    // 2. Snapshot backing RingBlob memory while consumer is parked (simulating snapshotFrontend).
    const std::vector<char> ringBlobSnapshot = combinedMemory;

    // 3. Save ASG state while already pre-saved: must not call preSave again or postSave yet.
    MemStream asgStream;
    EXPECT_EQ(gfxstream_address_space_save_memory_state(&asgStream), 0);
    EXPECT_EQ(state->preSaveCalls.load(), 1);
    EXPECT_EQ(state->postSaveCalls.load(), 0);

    // 4. Post-save resumes the consumer once; calling post_save again without pre_save fails.
    EXPECT_EQ(gfxstream_address_space_post_save(), 0);
    EXPECT_EQ(state->postSaveCalls.load(), 1);
    EXPECT_NE(gfxstream_address_space_post_save(), 0);
    EXPECT_EQ(state->postSaveCalls.load(), 1);

    // 5. Restore RingBlob backing store first, then ASG state, and verify guest reads the exact
    //    reply bytes committed before parking (no torn snapshot).
    restoredCombinedMemory = ringBlobSnapshot;
    AddressSpaceDeviceLoadResources loadResources;
    loadResources.contextExternalMemoryMap[handle] = {
        .externalAddress = restoredCombinedMemory.data(),
        .externalAddressSize = restoredCombinedMemory.size(),
    };
    ASSERT_EQ(gfxstream_address_space_set_load_resources(std::move(loadResources)), 0);

    asgStream.rewind();
    ASSERT_EQ(gfxstream_address_space_load_memory_state(&asgStream), 0);
    ASSERT_NE(state->restoredConsumerPtr, nullptr);

    auto restoredGuestCtx = AttachGuestContext(
        restoredCombinedMemory.data(),
        restoredCombinedMemory.data() + kAsgConsumerRingStorageSize,
        kAsgWriteBufferSize);
    ASSERT_EQ(ring_buffer_available_read(restoredGuestCtx.from_host_large_xfer.ring,
                                         &restoredGuestCtx.from_host_large_xfer.view),
              state->inflightReply.size());
    std::vector<uint8_t> received(state->inflightReply.size(), 0);
    EXPECT_EQ(ring_buffer_view_read(restoredGuestCtx.from_host_large_xfer.ring,
                                    &restoredGuestCtx.from_host_large_xfer.view,
                                    received.data(), received.size(), 1),
              1);
    EXPECT_EQ(received, state->inflightReply);
}

TEST_F(AddressSpaceDeviceSnapshotTest, StandaloneSaveAndEarlyPostSaveManageLifecycle) {
    struct TestState {
        int preSaveCalls = 0;
        int waitForPauseCalls = 0;
        int saveCalls = 0;
        int postSaveCalls = 0;
        int dummyConsumer = 0;
    };
    auto state = makeRetainedState<TestState>();

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo&, Stream*) -> ConsumerHandle {
            return &state->dummyConsumer;
        },
        .destroy = [](ConsumerHandle) {},
        .preSave = [state](ConsumerHandle) { ++state->preSaveCalls; },
        .globalPreSave = []() {},
        .save = [state](ConsumerHandle, Stream*) { ++state->saveCalls; },
        .globalPostSave = []() {},
        .postSave = [state](ConsumerHandle) { ++state->postSaveCalls; },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [state](ConsumerHandle) -> bool {
            ++state->waitForPauseCalls;
            return true;
        },
    });

    std::vector<char>& combinedMemory = allocBackingBuffer();
    auto ops = GetAsgOperations();
    AddressSpaceCreateInfo createInfo = {};
    createInfo.handle = ops.gen_handle();
    createInfo.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    createInfo.createRenderThread = true;
    createInfo.externalAddr = combinedMemory.data();
    createInfo.externalAddrSize = combinedMemory.size();
    ops.create_instance(createInfo);

    // Standalone save_memory_state manages preSave -> waitForPause -> save -> postSave.
    MemStream stream;
    EXPECT_EQ(gfxstream_address_space_save_memory_state(&stream), 0);
    EXPECT_EQ(state->preSaveCalls, 1);
    EXPECT_EQ(state->waitForPauseCalls, 1);
    EXPECT_EQ(state->saveCalls, 1);
    EXPECT_EQ(state->postSaveCalls, 1);

    // Early abort path: pre_save followed directly by post_save without save_memory_state.
    EXPECT_EQ(gfxstream_address_space_pre_save(), 0);
    EXPECT_EQ(state->preSaveCalls, 2);
    EXPECT_EQ(state->waitForPauseCalls, 2);
    EXPECT_EQ(state->postSaveCalls, 1);

    EXPECT_EQ(gfxstream_address_space_post_save(), 0);
    EXPECT_EQ(state->postSaveCalls, 2);
    EXPECT_EQ(state->saveCalls, 1);
}

TEST_F(AddressSpaceDeviceSnapshotTest, SaveStateValidationAndFailureRollback) {
    EXPECT_EQ(kFramebufferSnapshotVersion, 7u);

    struct ConsumerState {
        bool paused = false;
        bool waited = false;
        bool failWait = false;
    };
    struct TestState {
        ConsumerState consumers[2];
        int nextConsumer = 0;
        int preSaveCalls = 0;
        int saveCalls = 0;
        int postSaveCalls = 0;
        int cancelPreSaveCalls = 0;
        bool postSaveOnUnpausedObserved = false;
        bool checkSavingGuard = false;
    };
    auto state = makeRetainedState<TestState>();

    auto ops = GetAsgOperations();
    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo&, Stream*) -> ConsumerHandle {
            return &state->consumers[state->nextConsumer++ % 2];
        },
        .destroy = [](ConsumerHandle) {},
        .preSave = [state](ConsumerHandle handle) {
            ++state->preSaveCalls;
            auto* c = static_cast<ConsumerState*>(handle);
            c->paused = true;
            c->waited = false;
        },
        .globalPreSave = []() {},
        .save = [state](ConsumerHandle, Stream* s) {
            ++state->saveCalls;
            if (state->checkSavingGuard) {
                state->checkSavingGuard = false;
                // While SaveState::Saving is active, post_save and pre_save must be rejected.
                EXPECT_NE(gfxstream_address_space_post_save(), 0);
                EXPECT_NE(gfxstream_address_space_pre_save(), 0);
            }
            MemStream nestedStream;
            nestedStream.write("payload", 7);
            saveStream(s, nestedStream);
        },
        .globalPostSave = []() {},
        .postSave = [state](ConsumerHandle handle) {
            ++state->postSaveCalls;
            auto* c = static_cast<ConsumerState*>(handle);
            if (!c->paused || !c->waited || c->failWait) {
                state->postSaveOnUnpausedObserved = true;
            }
            c->paused = false;
        },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [](ConsumerHandle handle) -> bool {
            auto* c = static_cast<ConsumerState*>(handle);
            c->waited = true;
            return !c->failWait;
        },
        .cancelPreSave = [state](ConsumerHandle handle) {
            ++state->cancelPreSaveCalls;
            auto* c = static_cast<ConsumerState*>(handle);
            c->paused = false;
        },
    });

    std::vector<char>& combinedMemory0 = allocBackingBuffer();
    std::vector<char>& combinedMemory1 = allocBackingBuffer();
    for (std::vector<char>* mem : {&combinedMemory0, &combinedMemory1}) {
        AddressSpaceCreateInfo createInfo = {};
        createInfo.handle = ops.gen_handle();
        createInfo.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
        createInfo.createRenderThread = true;
        createInfo.externalAddr = mem->data();
        createInfo.externalAddrSize = mem->size();
        ops.create_instance(createInfo);
    }

    // Calling post_save in initial Reset state must fail.
    EXPECT_NE(gfxstream_address_space_post_save(), 0);
    EXPECT_EQ(state->postSaveCalls, 0);

    // Failing waitForPause on one context aborts unpaused/failed contexts via cancelPreSave,
    // resumes any already-paused contexts via postSave, and resets SaveState to Reset.
    state->consumers[0].failWait = true;
    EXPECT_NE(gfxstream_address_space_pre_save(), 0);
    EXPECT_EQ(state->preSaveCalls, 2);
    EXPECT_EQ(state->postSaveCalls + state->cancelPreSaveCalls, 2);
    EXPECT_GE(state->cancelPreSaveCalls, 1);
    EXPECT_FALSE(state->postSaveOnUnpausedObserved);

    MemStream failedStream;
    EXPECT_NE(gfxstream_address_space_save_memory_state(&failedStream), 0);
    EXPECT_EQ(state->preSaveCalls, 4);
    EXPECT_EQ(state->saveCalls, 0);
    EXPECT_EQ(state->postSaveCalls + state->cancelPreSaveCalls, 4);
    EXPECT_FALSE(state->postSaveOnUnpausedObserved);

    // Explicit pre_save -> save -> post_save rejects post_save during SaveState::Saving.
    state->consumers[0].failWait = false;
    state->checkSavingGuard = true;
    const int postSavesBeforeOkCycle = state->postSaveCalls;
    EXPECT_EQ(gfxstream_address_space_pre_save(), 0);
    MemStream guardedStream;
    EXPECT_EQ(gfxstream_address_space_save_memory_state(&guardedStream), 0);
    EXPECT_EQ(state->saveCalls, 2);
    EXPECT_EQ(state->postSaveCalls, postSavesBeforeOkCycle);
    EXPECT_EQ(gfxstream_address_space_post_save(), 0);
    EXPECT_EQ(state->postSaveCalls, postSavesBeforeOkCycle + 2);
    EXPECT_FALSE(state->postSaveOnUnpausedObserved);

    // Stream write errors (including a short write on the final context's MemStream::save()
    // payload) propagate as failure while still unparking contexts cleanly.
    FailingWriteStream failingStandaloneStream;
    EXPECT_NE(gfxstream_address_space_save_memory_state(&failingStandaloneStream), 0);
    EXPECT_EQ(state->postSaveCalls, postSavesBeforeOkCycle + 4);

    ASSERT_GT(guardedStream.writtenSize(), 3u);
    EXPECT_EQ(gfxstream_address_space_pre_save(), 0);
    FailingWriteStream failingFinalPayloadStream(guardedStream.writtenSize() - 2u);
    EXPECT_NE(gfxstream_address_space_save_memory_state(&failingFinalPayloadStream), 0);
    EXPECT_EQ(gfxstream_address_space_post_save(), 0);
    EXPECT_EQ(state->postSaveCalls, postSavesBeforeOkCycle + 6);
    EXPECT_FALSE(state->postSaveOnUnpausedObserved);
}

struct ParkedWorkerConsumer {
    AsgConsumerCreateInfo createInfo;
    std::thread worker;
    std::mutex mu;
    std::condition_variable cv;
    bool parkedInSnapshotOp = false;
    bool allowLeaveSnapshotOp = false;
    bool workerFinished = false;
};

void runParkedWorker(ParkedWorkerConsumer* consumer) {
    while (true) {
        const auto status = consumer->createInfo.callbacks.onUnavailableRead();
        if (status == AsgOnUnavailableReadStatus::kExit) {
            std::lock_guard<std::mutex> lock(consumer->mu);
            consumer->workerFinished = true;
            consumer->cv.notify_all();
            return;
        }
        if (status != AsgOnUnavailableReadStatus::kPauseForSnapshot) {
            continue;
        }
        std::unique_lock<std::mutex> lock(consumer->mu);
        consumer->parkedInSnapshotOp = true;
        consumer->allowLeaveSnapshotOp = false;
        consumer->cv.notify_all();
        // Mirror RenderThread::doSnapshotOp(): block until postSave/resume unparks us,
        // even if exiting was signaled while parked.
        const bool unparked = consumer->cv.wait_for(
            lock, std::chrono::seconds(5),
            [&] { return consumer->allowLeaveSnapshotOp; });
        EXPECT_TRUE(unparked);
        consumer->parkedInSnapshotOp = false;
        consumer->cv.notify_all();
        if (!unparked) {
            consumer->workerFinished = true;
            return;
        }
    }
}

TEST_F(AddressSpaceDeviceSnapshotTest, DestroyAndClearParkedContextsAndNewContextDuringSave) {
    struct TestState {
        std::atomic<int> createdConsumers{0};
        std::atomic<int> saveCalls{0};
        std::atomic<int> postSaveCalls{0};
        std::atomic<bool> saveOnUnpausedObserved{false};
    };
    auto state = makeRetainedState<TestState>();

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo& info, Stream* loadStream) -> ConsumerHandle {
            ++state->createdConsumers;
            if (loadStream) {
                EXPECT_EQ(loadStream->getByte(), 1);
            }
            auto* c = new ParkedWorkerConsumer();
            c->createInfo = info;
            c->worker = std::thread(runParkedWorker, c);
            return c;
        },
        .destroy = [](ConsumerHandle handle) {
            auto* c = static_cast<ParkedWorkerConsumer*>(handle);
            {
                std::unique_lock<std::mutex> lock(c->mu);
                const bool finished = c->cv.wait_for(
                    lock, std::chrono::seconds(5), [&] { return c->workerFinished; });
                EXPECT_TRUE(finished);
                if (!finished) {
                    c->allowLeaveSnapshotOp = true;
                    c->cv.notify_all();
                }
            }
            if (c->worker.joinable()) {
                c->worker.join();
            }
            delete c;
        },
        .preSave = [](ConsumerHandle) {},
        .globalPreSave = []() {},
        .save = [state](ConsumerHandle handle, Stream* stream) {
            ++state->saveCalls;
            auto* c = static_cast<ParkedWorkerConsumer*>(handle);
            {
                std::lock_guard<std::mutex> lock(c->mu);
                if (!c->parkedInSnapshotOp) {
                    state->saveOnUnpausedObserved.store(true);
                }
            }
            stream->putByte(1);
        },
        .globalPostSave = []() {},
        .postSave = [state](ConsumerHandle handle) {
            ++state->postSaveCalls;
            auto* c = static_cast<ParkedWorkerConsumer*>(handle);
            std::lock_guard<std::mutex> lock(c->mu);
            c->allowLeaveSnapshotOp = true;
            c->cv.notify_all();
        },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [](ConsumerHandle handle) -> bool {
            auto* c = static_cast<ParkedWorkerConsumer*>(handle);
            std::unique_lock<std::mutex> lock(c->mu);
            return c->cv.wait_for(
                lock, std::chrono::seconds(5), [&] { return c->parkedInSnapshotOp; });
        },
    });

    auto ops = GetAsgOperations();
    std::vector<char>& mem1 = allocBackingBuffer();
    std::vector<char>& mem2 = allocBackingBuffer();
    std::vector<char>& mem3 = allocBackingBuffer();
    std::vector<char>& mem4 = allocBackingBuffer();

    auto createCtx = [&](std::vector<char>* mem, bool createRenderThread = true) {
        AddressSpaceCreateInfo createInfo = {};
        createInfo.handle = ops.gen_handle();
        createInfo.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
        createInfo.createRenderThread = createRenderThread;
        createInfo.externalAddr = mem->data();
        createInfo.externalAddrSize = mem->size();
        ops.create_instance(createInfo);
        return createInfo.handle;
    };

    const uint32_t handle1 = createCtx(&mem1);
    const uint32_t handle2 = createCtx(&mem2);
    ASSERT_NE(handle1, 0u);
    ASSERT_NE(handle2, 0u);
    EXPECT_EQ(state->createdConsumers.load(), 2);

    // Park both handle1 and handle2 in simulated RenderThread::doSnapshotOp().
    ASSERT_EQ(gfxstream_address_space_pre_save(), 0);

    // Destroying handle1 while PreSaved must unpark its worker via postSave before destroy() joins.
    ops.destroy_handle(handle1);
    EXPECT_EQ(state->postSaveCalls.load(), 1);

    // Creating lazy handle3 (createRenderThread = false) after pre_save() does not spawn a
    // consumer; save() reuses the pre-saved snapshot of live handle2.
    const uint32_t handle3 = createCtx(&mem3, /*createRenderThread=*/false);
    ASSERT_NE(handle3, 0u);
    EXPECT_EQ(state->createdConsumers.load(), 2);

    // Creating handle4 with createRenderThread = true while PreSaved must block on
    // mSaveState != SaveState::PreSaved so no unpaused RenderThread races with Renderer::save().
    const uint32_t handle4 = ops.gen_handle();
    std::atomic<bool> createWithThreadFinished{false};
    ScopedThread createWithThread(
        std::thread([ops, handle4, &mem4, &createWithThreadFinished] {
            AddressSpaceCreateInfo createInfo = {};
            createInfo.handle = handle4;
            createInfo.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
            createInfo.createRenderThread = true;
            createInfo.externalAddr = mem4.data();
            createInfo.externalAddrSize = mem4.size();
            ops.create_instance(createInfo);
            createWithThreadFinished.store(true);
        }),
        [ops] { ops.clear(); });

    MemStream stream;
    EXPECT_EQ(gfxstream_address_space_save_memory_state(&stream), 0);
    EXPECT_EQ(state->saveCalls.load(), 1);
    EXPECT_FALSE(state->saveOnUnpausedObserved.load());
    EXPECT_EQ(state->createdConsumers.load(), 2);
    EXPECT_FALSE(createWithThreadFinished.load());

    // Clearing while still PreSaved must resume parked handle2 via postSave before destroying
    // handle2 and unpaused handle3, and unblock createWithThread once clear finishes.
    ops.clear();
    EXPECT_EQ(state->postSaveCalls.load(), 2);
    createWithThread.join();
    EXPECT_TRUE(createWithThreadFinished.load());
    EXPECT_EQ(state->createdConsumers.load(), 3);
    ops.clear();

    // Restoring the snapshot with external memory for only live pre-saved handle2 (omitting
    // destroyed handle1 and post-pre_save handle3/handle4) must succeed without GFXSTREAM_FATAL.
    AddressSpaceDeviceLoadResources loadResources;
    loadResources.contextExternalMemoryMap[handle2] = {
        .externalAddress = mem2.data(),
        .externalAddressSize = mem2.size(),
    };
    ASSERT_EQ(gfxstream_address_space_set_load_resources(std::move(loadResources)), 0);
    stream.rewind();
    EXPECT_EQ(gfxstream_address_space_load_memory_state(&stream), 0);
    ops.clear();
}

TEST_F(AddressSpaceDeviceSnapshotTest, ConcurrentDestroyAndClearSerializeAgainstInFlightSave) {
    struct TestState {
        std::mutex gateMu;
        std::condition_variable gateCv;
        bool inSave = false;
        bool allowSaveFinish = false;
        bool saveCompleted = false;
        std::atomic<bool> destroyDuringSaveObserved{false};
        std::atomic<bool> destroyCompleted{false};
        std::atomic<bool> destroyCompletedBeforeReturn{false};
        std::atomic<int> postSaveCalls{0};
        int dummyConsumer = 0;
    };
    auto state = makeRetainedState<TestState>();
    auto unblockSaveGate = [state] {
        std::lock_guard<std::mutex> lock(state->gateMu);
        state->allowSaveFinish = true;
        state->gateCv.notify_all();
    };

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo&, Stream*) -> ConsumerHandle {
            return &state->dummyConsumer;
        },
        .destroy = [state](ConsumerHandle) {
            std::lock_guard<std::mutex> lock(state->gateMu);
            if (!state->saveCompleted) {
                state->destroyDuringSaveObserved.store(true);
            }
            state->destroyCompleted.store(true);
        },
        .preSave = [](ConsumerHandle) {},
        .globalPreSave = []() {},
        .save = [state](ConsumerHandle, Stream* stream) {
            {
                std::unique_lock<std::mutex> lock(state->gateMu);
                state->inSave = true;
                state->gateCv.notify_all();
                state->gateCv.wait_for(lock, std::chrono::seconds(5),
                                       [state] { return state->allowSaveFinish; });
                state->saveCompleted = true;
            }
            stream->putByte(1);
        },
        .globalPostSave = []() {},
        .postSave = [state](ConsumerHandle) { ++state->postSaveCalls; },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [](ConsumerHandle) -> bool { return true; },
        .cancelPreSave = [](ConsumerHandle) {},
    });

    auto ops = GetAsgOperations();
    std::vector<char>& mem1 = allocBackingBuffer();
    AddressSpaceCreateInfo create1 = {};
    create1.handle = ops.gen_handle();
    create1.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    create1.createRenderThread = true;
    create1.externalAddr = mem1.data();
    create1.externalAddrSize = mem1.size();
    ops.create_instance(create1);

    ASSERT_EQ(gfxstream_address_space_pre_save(), 0);

    MemStream saveStream1;
    ScopedThread saveThread1(
        std::thread([&saveStream1] {
            EXPECT_EQ(gfxstream_address_space_save_memory_state(&saveStream1), 0);
        }),
        unblockSaveGate);

    {
        std::unique_lock<std::mutex> lock(state->gateMu);
        ASSERT_TRUE(state->gateCv.wait_for(lock, std::chrono::seconds(5),
                                           [state] { return state->inSave; }));
    }

    // Concurrent destroy_handle must wait for in-flight save() and complete consumer destroy()
    // before destroy_handle() returns so callers can safely free backing RingBlob memory.
    const uint32_t handle1 = create1.handle;
    ScopedThread destroyThread(
        std::thread([ops, handle1, state] {
            ops.destroy_handle(handle1);
            state->destroyCompletedBeforeReturn.store(state->destroyCompleted.load());
        }),
        unblockSaveGate);

    unblockSaveGate();
    saveThread1.join();
    destroyThread.join();

    EXPECT_FALSE(state->destroyDuringSaveObserved.load());
    EXPECT_TRUE(state->destroyCompletedBeforeReturn.load());
    EXPECT_EQ(gfxstream_address_space_post_save(), 0);

    // Reset gate and verify concurrent clear() waits for in-flight standalone save() and cleans
    // up memory mappings under mMemoryMappingsLock without unparking mid-save.
    {
        std::lock_guard<std::mutex> lock(state->gateMu);
        state->inSave = false;
        state->allowSaveFinish = false;
        state->saveCompleted = false;
        state->destroyCompleted.store(false);
        state->destroyDuringSaveObserved.store(false);
    }

    std::vector<char>& mem2 = allocBackingBuffer();
    AddressSpaceCreateInfo create2 = {};
    create2.handle = ops.gen_handle();
    create2.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    create2.createRenderThread = true;
    create2.externalAddr = mem2.data();
    create2.externalAddrSize = mem2.size();
    ops.create_instance(create2);

    std::vector<char>& mappedPage = allocBackingBuffer(4096);
    std::memset(mappedPage.data(), 0xAB, mappedPage.size());
    constexpr uint64_t kMappedGpa = 0x90000000ULL;
    ASSERT_EQ(ops.add_memory_mapping(kMappedGpa, mappedPage.data(), mappedPage.size()), 1);
    EXPECT_EQ(ops.get_host_ptr(kMappedGpa), mappedPage.data());

    MemStream saveStream2;
    ScopedThread saveThread2(
        std::thread([&saveStream2] {
            EXPECT_EQ(gfxstream_address_space_save_memory_state(&saveStream2), 0);
        }),
        unblockSaveGate);

    {
        std::unique_lock<std::mutex> lock(state->gateMu);
        ASSERT_TRUE(state->gateCv.wait_for(lock, std::chrono::seconds(5),
                                           [state] { return state->inSave; }));
    }

    std::atomic<bool> clearFinished{false};
    ScopedThread clearThread(
        std::thread([ops, &clearFinished] {
            ops.clear();
            clearFinished.store(true);
        }),
        unblockSaveGate);

    {
        std::lock_guard<std::mutex> lock(state->gateMu);
        EXPECT_FALSE(clearFinished.load());
        state->allowSaveFinish = true;
        state->gateCv.notify_all();
    }
    saveThread2.join();
    clearThread.join();

    EXPECT_FALSE(state->destroyDuringSaveObserved.load());
    EXPECT_TRUE(state->destroyCompleted.load());
    EXPECT_EQ(ops.get_host_ptr(kMappedGpa), nullptr);
}

TEST_F(AddressSpaceDeviceSnapshotTest,
       SnapshotAbortFlagsFallbackCancelAndConcurrentSaveDuringDestroy) {
    // 1. RingStream::consumeAbortedReadForSnapshot() distinguishes snapshot aborts from exits, and
    //    a resume() racing with kPauseForSnapshot does not leave mShouldExitForSnapshot armed for
    //    the next snapshot.
    std::vector<char> ringStorage(kAsgConsumerRingStorageSize, 0);
    std::vector<char> auxBuffer(kTestSmallAuxBufSize, 0);
    std::atomic<int> unavailableStep{0};
    RingStream* ringStreamPtr = nullptr;
    AsgConsumerCreateInfo ringInfo =
        MakeCreateInfo(ringStorage.data(), auxBuffer.data(), kTestSmallAuxBufSize,
                       kTestSmallFlushInterval);
    ringInfo.callbacks.onUnavailableRead = [&]() {
        const int step = unavailableStep.fetch_add(1);
        if (step == 0) {
            return AsgOnUnavailableReadStatus::kPauseForSnapshot;
        }
        if (step == 1) {
            // Simulate cancelPausePreSnapshot() calling resume() right before readRaw() handles
            // kPauseForSnapshot.
            ringStreamPtr->resume();
            return AsgOnUnavailableReadStatus::kPauseForSnapshot;
        }
        if (step == 2) {
            auto guestCtx = AttachGuestContext(ringStorage.data(), auxBuffer.data(),
                                               kTestSmallAuxBufSize);
            constexpr uint32_t kPayload = 0x12345678u;
            std::memcpy(auxBuffer.data(), &kPayload, sizeof(kPayload));
            asg_type1_xfer xfer = {.offset = 0, .size = sizeof(kPayload)};
            ring_buffer_write(guestCtx.to_host, &xfer, sizeof(xfer), 1);
            return AsgOnUnavailableReadStatus::kContinue;
        }
        if (step == 3) {
            return AsgOnUnavailableReadStatus::kPauseForSnapshot;
        }
        return AsgOnUnavailableReadStatus::kExit;
    };
    RingStream ringStream(ringInfo, kTestInitialStreamBufSize);
    ringStreamPtr = &ringStream;
    EXPECT_FALSE(ringStream.consumeAbortedReadForSnapshot());

    ringStream.pausePreSnapshot();
    uint32_t word = 0;
    EXPECT_EQ(ringStream.read(&word, sizeof(word)), 0u);
    // Rapid cancel + restart + cancel before RenderThread consumes the pending abort must not
    // clear mAbortedReadForSnapshot prematurely.
    ringStream.resume();
    ringStream.pausePreSnapshot();
    ringStream.resume();
    EXPECT_TRUE(ringStream.consumeAbortedReadForSnapshot());
    EXPECT_FALSE(ringStream.consumeAbortedReadForSnapshot());

    // Start a snapshot that gets canceled via resume() inside step == 1 before kPauseForSnapshot
    // returns, then delivers 4 bytes on step == 2.
    ringStream.pausePreSnapshot();
    EXPECT_EQ(ringStream.read(&word, sizeof(word)), sizeof(word));
    EXPECT_EQ(word, 0x12345678u);
    EXPECT_FALSE(ringStream.consumeAbortedReadForSnapshot());

    // Subsequent snapshot must still invoke onUnavailableRead() (step == 3) rather than aborting
    // prematurely from a stale mShouldExitForSnapshot flag.
    ringStream.pausePreSnapshot();
    EXPECT_EQ(ringStream.read(&word, sizeof(word)), 0u);
    EXPECT_EQ(unavailableStep.load(), 4);
    EXPECT_TRUE(ringStream.consumeAbortedReadForSnapshot());

    ringStream.resume();
    EXPECT_EQ(ringStream.read(&word, sizeof(word)), 0u);
    EXPECT_FALSE(ringStream.consumeAbortedReadForSnapshot());

    // 2. Fallback cancelPreSave (when ConsumerInterface::cancelPreSave is unset) does not re-wait
    //    or invoke postSave on a consumer whose waitForPause already failed, cleanly resumes other
    //    paused contexts, does not hang on teardown while exiting, and serializes beginSave()
    //    against in-flight destroy_handle().
    struct TrackedParkedConsumer {
        ParkedWorkerConsumer parked;
        int index = 0;
        bool neverPauses = false;
    };
    struct FallbackTestState {
        std::atomic<int> createdIdx{0};
        std::atomic<bool> failFirstWait{true};
        std::atomic<int> failedIdx{-1};
        std::atomic<int> preSaveCounts[3] = {{0}, {0}, {0}};
        std::atomic<int> waitCounts[3] = {{0}, {0}, {0}};
        std::atomic<int> postSaveCounts[3] = {{0}, {0}, {0}};
        std::mutex destroyGateMu;
        std::condition_variable destroyGateCv;
        bool gateDestroy = false;
        bool enteredDestroy = false;
        bool allowDestroyFinish = false;
    };
    auto state = makeRetainedState<FallbackTestState>();
    auto unblockDestroyGate = [state] {
        std::lock_guard<std::mutex> gateLock(state->destroyGateMu);
        state->gateDestroy = false;
        state->allowDestroyFinish = true;
        state->destroyGateCv.notify_all();
    };

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo& info, Stream*) -> ConsumerHandle {
            auto* tc = new TrackedParkedConsumer();
            tc->index = state->createdIdx.fetch_add(1);
            tc->neverPauses = (tc->index == 2);
            tc->parked.createInfo = info;
            if (!tc->neverPauses) {
                tc->parked.worker = std::thread(runParkedWorker, &tc->parked);
            }
            return tc;
        },
        .destroy = [state](ConsumerHandle handle) {
            auto* tc = static_cast<TrackedParkedConsumer*>(handle);
            auto* c = &tc->parked;
            {
                std::unique_lock<std::mutex> gateLock(state->destroyGateMu);
                if (state->gateDestroy) {
                    state->enteredDestroy = true;
                    state->destroyGateCv.notify_all();
                    state->destroyGateCv.wait_for(
                        gateLock, std::chrono::seconds(5),
                        [state] { return state->allowDestroyFinish; });
                }
            }
            if (!tc->neverPauses) {
                std::unique_lock<std::mutex> lock(c->mu);
                const bool finished = c->cv.wait_for(
                    lock, std::chrono::seconds(5), [&] { return c->workerFinished; });
                EXPECT_TRUE(finished);
                if (!finished) {
                    c->allowLeaveSnapshotOp = true;
                    c->cv.notify_all();
                }
            }
            if (c->worker.joinable()) {
                c->worker.join();
            }
            delete tc;
        },
        .preSave = [state](ConsumerHandle handle) {
            auto* tc = static_cast<TrackedParkedConsumer*>(handle);
            ++state->preSaveCounts[tc->index];
        },
        .globalPreSave = []() {},
        .save = [](ConsumerHandle, Stream* s) { s->putByte(1); },
        .globalPostSave = []() {},
        .postSave = [state](ConsumerHandle handle) {
            auto* tc = static_cast<TrackedParkedConsumer*>(handle);
            ++state->postSaveCounts[tc->index];
            auto* c = &tc->parked;
            std::lock_guard<std::mutex> lock(c->mu);
            c->allowLeaveSnapshotOp = true;
            c->cv.notify_all();
        },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [state](ConsumerHandle handle) -> bool {
            auto* tc = static_cast<TrackedParkedConsumer*>(handle);
            ++state->waitCounts[tc->index];
            if (tc->neverPauses) {
                return false;
            }
            if (state->failFirstWait.exchange(false)) {
                state->failedIdx.store(tc->index);
                return false;
            }
            auto* c = &tc->parked;
            std::unique_lock<std::mutex> lock(c->mu);
            return c->cv.wait_for(
                lock, std::chrono::seconds(5), [&] { return c->parkedInSnapshotOp; });
        },
        // Intentionally leave .cancelPreSave unset ({}) to test fallback path.
    });

    auto ops = GetAsgOperations();
    std::vector<char>& memA = allocBackingBuffer();
    std::vector<char>& memB = allocBackingBuffer();
    std::vector<char>& memC = allocBackingBuffer();
    auto makeCtx = [&](std::vector<char>* mem) {
        AddressSpaceCreateInfo ci = {};
        ci.handle = ops.gen_handle();
        ci.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
        ci.createRenderThread = true;
        ci.externalAddr = mem->data();
        ci.externalAddrSize = mem->size();
        ops.create_instance(ci);
        return ci.handle;
    };
    const uint32_t hA = makeCtx(&memA);
    const uint32_t hB = makeCtx(&memB);
    ASSERT_NE(hA, 0u);
    ASSERT_NE(hB, 0u);

    // First pre_save fails on the first visited context (i == 0); fallback cancelPreSave must NOT
    // re-wait or call postSave on that context, while waiting + calling postSave once on the
    // second context (i == 1).
    EXPECT_NE(gfxstream_address_space_pre_save(), 0);
    const int failed = state->failedIdx.load();
    ASSERT_TRUE(failed == 0 || failed == 1);
    const int other = 1 - failed;
    EXPECT_EQ(state->waitCounts[failed].load(), 1);
    EXPECT_EQ(state->postSaveCounts[failed].load(), 0);
    EXPECT_EQ(state->waitCounts[other].load(), 1);
    EXPECT_EQ(state->postSaveCounts[other].load(), 1);

    // Second pre_save succeeds without calling preSave twice on the context that remained in
    // preSaveActive, then block destroy_handle(hA) inside .destroy and verify save() waits for
    // mDestroyingContextsCount == 0 before proceeding.
    ASSERT_EQ(gfxstream_address_space_pre_save(), 0);
    EXPECT_EQ(state->preSaveCounts[failed].load(), 1);
    EXPECT_EQ(state->preSaveCounts[other].load(), 2);
    {
        std::lock_guard<std::mutex> gateLock(state->destroyGateMu);
        state->gateDestroy = true;
        state->enteredDestroy = false;
        state->allowDestroyFinish = false;
    }
    std::atomic<bool> destroyReturned{false};
    ScopedThread destroyThread(
        std::thread([ops, hA, &destroyReturned] {
            ops.destroy_handle(hA);
            destroyReturned.store(true);
        }),
        unblockDestroyGate);
    {
        std::unique_lock<std::mutex> gateLock(state->destroyGateMu);
        ASSERT_TRUE(state->destroyGateCv.wait_for(
            gateLock, std::chrono::seconds(5), [state] { return state->enteredDestroy; }));
    }

    std::atomic<bool> saveFinished{false};
    MemStream stream;
    ScopedThread saveThread(
        std::thread([&stream, &saveFinished] {
            EXPECT_EQ(gfxstream_address_space_save_memory_state(&stream), 0);
            saveFinished.store(true);
        }),
        unblockDestroyGate);

    {
        std::lock_guard<std::mutex> gateLock(state->destroyGateMu);
        EXPECT_FALSE(saveFinished.load());
        state->gateDestroy = false;
        state->allowDestroyFinish = true;
        state->destroyGateCv.notify_all();
    }
    destroyThread.join();
    saveThread.join();
    EXPECT_TRUE(destroyReturned.load());
    EXPECT_TRUE(saveFinished.load());
    EXPECT_EQ(gfxstream_address_space_post_save(), 0);

    // Create index 2 (neverPauses == true): failed pre_save and subsequent teardown via clear()
    // must never re-wait or invoke postSave on the unpaused consumer.
    const uint32_t hC = makeCtx(&memC);
    ASSERT_NE(hC, 0u);
    EXPECT_NE(gfxstream_address_space_pre_save(), 0);
    EXPECT_EQ(state->waitCounts[2].load(), 1);
    EXPECT_EQ(state->postSaveCounts[2].load(), 0);
    ops.clear();
    EXPECT_EQ(state->waitCounts[2].load(), 1);
    EXPECT_EQ(state->postSaveCounts[2].load(), 0);
}

TEST_F(AddressSpaceDeviceSnapshotTest, LoadAndPreSavedSerializeConcurrentCreateAndPing) {
    struct TestState {
        std::mutex mu;
        std::condition_variable cv;
        bool gatePreLoad = false;
        bool inPreLoad = false;
        bool allowPreLoadFinish = false;
        std::atomic<int> createdConsumers{0};
        std::atomic<int> saveOnUnpaused{0};
        struct Consumer {
            bool preSaved = false;
        };
    };
    auto state = makeRetainedState<TestState>();
    auto unblockPreLoad = [state] {
        std::lock_guard<std::mutex> lock(state->mu);
        state->allowPreLoadFinish = true;
        state->cv.notify_all();
    };

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo&, Stream* loadStream) -> ConsumerHandle {
            ++state->createdConsumers;
            if (loadStream) {
                EXPECT_EQ(loadStream->getByte(), 1);
            }
            return new TestState::Consumer{};
        },
        .destroy = [](ConsumerHandle h) { delete static_cast<TestState::Consumer*>(h); },
        .preSave = [](ConsumerHandle h) { static_cast<TestState::Consumer*>(h)->preSaved = true; },
        .globalPreSave = []() {},
        .save = [state](ConsumerHandle h, Stream* s) {
            if (!static_cast<TestState::Consumer*>(h)->preSaved) {
                ++state->saveOnUnpaused;
            }
            s->putByte(1);
        },
        .globalPostSave = []() {},
        .postSave = [](ConsumerHandle h) {
            static_cast<TestState::Consumer*>(h)->preSaved = false;
        },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = [state]() {
            std::unique_lock<std::mutex> lock(state->mu);
            if (!state->gatePreLoad) {
                return;
            }
            state->inPreLoad = true;
            state->cv.notify_all();
            state->cv.wait_for(lock, std::chrono::seconds(5),
                               [state] { return state->allowPreLoadFinish; });
        },
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [](ConsumerHandle) -> bool { return true; },
        .cancelPreSave = [](ConsumerHandle h) {
            static_cast<TestState::Consumer*>(h)->preSaved = false;
        },
    });

    auto ops = GetAsgOperations();
    std::vector<char>& memLazy = allocBackingBuffer();
    AddressSpaceCreateInfo lazyCreate = {};
    lazyCreate.handle = ops.gen_handle();
    lazyCreate.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    lazyCreate.createRenderThread = false;
    lazyCreate.externalAddr = memLazy.data();
    lazyCreate.externalAddrSize = memLazy.size();
    ops.create_instance(lazyCreate);
    EXPECT_EQ(state->createdConsumers.load(), 0);

    // 1. While SaveState::PreSaved is active, pingAtHva(ASG_SET_VERSION) must block until
    //    post_save() finishes so it cannot spawn an unpaused consumer before save().
    ASSERT_EQ(gfxstream_address_space_pre_save(), 0);
    std::atomic<bool> pingFinished{false};
    ScopedThread pingThread(
        std::thread([ops, handle = lazyCreate.handle, &pingFinished] {
            AddressSpaceDevicePingInfo pingInfo = {};
            pingInfo.metadata = ASG_SET_VERSION;
            pingInfo.size = 1;
            ops.ping_at_hva(handle, &pingInfo);
            pingFinished.store(true);
        }),
        [] { gfxstream_address_space_post_save(); });

    MemStream snapshotStream;
    EXPECT_EQ(gfxstream_address_space_save_memory_state(&snapshotStream), 0);
    EXPECT_EQ(state->saveOnUnpaused.load(), 0);
    EXPECT_FALSE(pingFinished.load());

    EXPECT_EQ(gfxstream_address_space_post_save(), 0);
    pingThread.join();
    EXPECT_TRUE(pingFinished.load());
    EXPECT_EQ(state->createdConsumers.load(), 1);

    // Save a snapshot that includes the consumer so we can exercise load() serialization.
    MemStream fullSnapshot;
    ASSERT_EQ(gfxstream_address_space_save_memory_state(&fullSnapshot), 0);

    // 2. While load() is in progress, concurrent create_instance must wait until
    //    installLoadedContextsAndFinishClear() completes rather than racing with Globals::load()
    //    or being overwritten by installLoadedContextsAndFinishClear().
    AddressSpaceDeviceLoadResources loadResources;
    loadResources.contextExternalMemoryMap[lazyCreate.handle] = {
        .externalAddress = memLazy.data(),
        .externalAddressSize = memLazy.size(),
    };
    ASSERT_EQ(gfxstream_address_space_set_load_resources(std::move(loadResources)), 0);
    {
        std::lock_guard<std::mutex> lock(state->mu);
        state->gatePreLoad = true;
        state->inPreLoad = false;
        state->allowPreLoadFinish = false;
    }

    fullSnapshot.rewind();
    ScopedThread loadThread(
        std::thread([&fullSnapshot] {
            EXPECT_EQ(gfxstream_address_space_load_memory_state(&fullSnapshot), 0);
        }),
        unblockPreLoad);

    {
        std::unique_lock<std::mutex> lock(state->mu);
        ASSERT_TRUE(state->cv.wait_for(lock, std::chrono::seconds(5),
                                       [state] { return state->inPreLoad; }));
    }

    std::vector<char>& memConcurrent = allocBackingBuffer();
    const uint32_t newHandle = 999u;
    std::atomic<bool> createFinished{false};
    ScopedThread createThread(
        std::thread([ops, &memConcurrent, &createFinished] {
            AddressSpaceCreateInfo ci = {};
            ci.handle = newHandle;
            ci.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
            ci.createRenderThread = true;
            ci.externalAddr = memConcurrent.data();
            ci.externalAddrSize = memConcurrent.size();
            ops.create_instance(ci);
            createFinished.store(true);
        }),
        unblockPreLoad);

    {
        std::lock_guard<std::mutex> lock(state->mu);
        EXPECT_FALSE(createFinished.load());
        state->allowPreLoadFinish = true;
        state->cv.notify_all();
    }
    loadThread.join();
    createThread.join();
    EXPECT_TRUE(createFinished.load());
    EXPECT_NE(ops.handle_to_context(lazyCreate.handle), nullptr);
    EXPECT_NE(ops.handle_to_context(newHandle), nullptr);
}

TEST_F(AddressSpaceDeviceSnapshotTest,
       CreateDestroyRaceMissingWaitForPauseAndFailedLoadPostLoadCleanup) {
    auto ops = GetAsgOperations();

    // 1. Omitting ConsumerInterface::waitForPause when a consumer is active must fail pre_save()
    //    and save_memory_state() explicitly instead of silently risking torn snapshots.
    int dummyConsumer = 0;
    std::atomic<int> cancelCalls{0};
    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [&dummyConsumer](const AsgConsumerCreateInfo&, Stream*) -> ConsumerHandle {
            return &dummyConsumer;
        },
        .destroy = [](ConsumerHandle) {},
        .preSave = [](ConsumerHandle) {},
        .globalPreSave = []() {},
        .save = [](ConsumerHandle, Stream* s) { s->putByte(1); },
        .globalPostSave = []() {},
        .postSave = [](ConsumerHandle) {},
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        // Intentionally omit .waitForPause.
        .cancelPreSave = [&cancelCalls](ConsumerHandle) { ++cancelCalls; },
    });

    std::vector<char>& memMissingWait = allocBackingBuffer();
    AddressSpaceCreateInfo missingWaitCi = {};
    missingWaitCi.handle = ops.gen_handle();
    missingWaitCi.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    missingWaitCi.createRenderThread = true;
    missingWaitCi.externalAddr = memMissingWait.data();
    missingWaitCi.externalAddrSize = memMissingWait.size();
    ops.create_instance(missingWaitCi);

    EXPECT_NE(gfxstream_address_space_pre_save(), 0);
    EXPECT_EQ(cancelCalls.load(), 1);
    MemStream missingWaitStream;
    EXPECT_NE(gfxstream_address_space_save_memory_state(&missingWaitStream), 0);
    EXPECT_EQ(cancelCalls.load(), 2);
    ops.clear();

    // 2. Concurrent destroy_handle(handle) must wait for in-flight create_instance(handle)
    //    (mCreatingContextsCount == 0) so destroy_handle cannot return before newContext is
    //    inserted and destroyed.
    struct CreateDestroyGate {
        std::mutex mu;
        std::condition_variable cv;
        bool inCreate = false;
        bool allowCreateFinish = false;
        std::atomic<int> destroyedCount{0};
        int token = 7;
    };
    auto gate = makeRetainedState<CreateDestroyGate>();
    auto unblockCreate = [gate] {
        std::lock_guard<std::mutex> lock(gate->mu);
        gate->allowCreateFinish = true;
        gate->cv.notify_all();
    };

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [gate](const AsgConsumerCreateInfo&, Stream*) -> ConsumerHandle {
            std::unique_lock<std::mutex> lock(gate->mu);
            gate->inCreate = true;
            gate->cv.notify_all();
            gate->cv.wait_for(lock, std::chrono::seconds(5),
                              [gate] { return gate->allowCreateFinish; });
            return &gate->token;
        },
        .destroy = [gate](ConsumerHandle) { ++gate->destroyedCount; },
        .preSave = [](ConsumerHandle) {},
        .globalPreSave = []() {},
        .save = [](ConsumerHandle, Stream*) {},
        .globalPostSave = []() {},
        .postSave = [](ConsumerHandle) {},
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [](ConsumerHandle) -> bool { return true; },
        .cancelPreSave = [](ConsumerHandle) {},
    });

    std::vector<char>& memRace = allocBackingBuffer();
    const uint32_t raceHandle = ops.gen_handle();
    ScopedThread createRaceThread(
        std::thread([ops, raceHandle, &memRace] {
            AddressSpaceCreateInfo ci = {};
            ci.handle = raceHandle;
            ci.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
            ci.createRenderThread = true;
            ci.externalAddr = memRace.data();
            ci.externalAddrSize = memRace.size();
            ops.create_instance(ci);
        }),
        unblockCreate);

    {
        std::unique_lock<std::mutex> lock(gate->mu);
        ASSERT_TRUE(gate->cv.wait_for(lock, std::chrono::seconds(5),
                                      [gate] { return gate->inCreate; }));
    }

    std::atomic<bool> destroyRaceFinished{false};
    ScopedThread destroyRaceThread(
        std::thread([ops, raceHandle, &destroyRaceFinished] {
            ops.destroy_handle(raceHandle);
            destroyRaceFinished.store(true);
        }),
        unblockCreate);

    {
        std::lock_guard<std::mutex> lock(gate->mu);
        EXPECT_FALSE(destroyRaceFinished.load());
        gate->allowCreateFinish = true;
        gate->cv.notify_all();
    }
    createRaceThread.join();
    destroyRaceThread.join();
    EXPECT_TRUE(destroyRaceFinished.load());
    EXPECT_EQ(gate->destroyedCount.load(), 1);
    EXPECT_EQ(ops.handle_to_context(raceHandle), nullptr);

    // 3. Failed ASG snapshot load (truncated global blocks or truncated second context consumer
    //    stream) must not leave deleted RenderThread* entries in
    //    RendererImpl::mAdditionalPostLoadRenderThreads when RendererImpl::resumeAll() runs.
    auto renderer = std::make_shared<RendererImpl>();
    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [renderer](const AsgConsumerCreateInfo& info, Stream* loadStream) {
            return renderer->addressSpaceGraphicsConsumerCreate(info, loadStream);
        },
        .destroy = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerDestroy(consumer);
        },
        .preSave = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerPreSave(consumer);
        },
        .globalPreSave = [renderer]() { renderer->pauseAllPreSave(); },
        .save = [renderer](void* consumer, Stream* stream) {
            renderer->addressSpaceGraphicsConsumerSave(consumer, stream);
        },
        .globalPostSave = [renderer]() { renderer->resumeAll(); },
        .postSave = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerPostSave(consumer);
        },
        .postLoad = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(consumer);
        },
        .globalPreLoad = []() {},
        .reloadRingConfig = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerReloadRingConfig(consumer);
        },
        .waitForPause = [renderer](void* consumer) {
            return renderer->addressSpaceGraphicsConsumerWaitForPause(consumer);
        },
        .cancelPreSave = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerCancelPreSave(consumer);
        },
    });

    std::vector<char>& memA = allocBackingBuffer();
    std::vector<char>& memB = allocBackingBuffer();
    auto makeRendererCtx = [&](std::vector<char>* mem) {
        AddressSpaceCreateInfo ci = {};
        ci.handle = ops.gen_handle();
        ci.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
        ci.createRenderThread = true;
        ci.externalAddr = mem->data();
        ci.externalAddrSize = mem->size();
        ops.create_instance(ci);
        return ci.handle;
    };
    const uint32_t hA = makeRendererCtx(&memA);
    const uint32_t hB = makeRendererCtx(&memB);

    MemStream validSnap;
    ASSERT_EQ(gfxstream_address_space_save_memory_state(&validSnap), 0);
    ASSERT_GT(validSnap.writtenSize(), 32u);

    auto installResources = [&] {
        AddressSpaceDeviceLoadResources res;
        res.contextExternalMemoryMap[hA] = {memA.data(), memA.size()};
        res.contextExternalMemoryMap[hB] = {memB.data(), memB.size()};
        ASSERT_EQ(gfxstream_address_space_set_load_resources(std::move(res)), 0);
    };

    // Empty and mid-global-block truncated snapshots fail cleanly without GFXSTREAM_FATAL.
    installResources();
    MemStream emptySnap;
    EXPECT_NE(gfxstream_address_space_load_memory_state(&emptySnap), 0);

    installResources();
    MemStream truncatedGlobal(28);
    ASSERT_EQ(truncatedGlobal.write(validSnap.buffer().data(), 28), 28);
    truncatedGlobal.rewind();
    EXPECT_NE(gfxstream_address_space_load_memory_state(&truncatedGlobal), 0);

    // Undersized external memory buffer vs stream block bufferSize must fail load cleanly.
    std::vector<char> tinyBacking(64, 0);
    {
        AddressSpaceDeviceLoadResources undersizedRes;
        undersizedRes.contextExternalMemoryMap[hA] = {tinyBacking.data(), tinyBacking.size()};
        undersizedRes.contextExternalMemoryMap[hB] = {memB.data(), memB.size()};
        ASSERT_EQ(gfxstream_address_space_set_load_resources(std::move(undersizedRes)), 0);
    }
    validSnap.rewind();
    EXPECT_NE(gfxstream_address_space_load_memory_state(&validSnap), 0);

    // Context referencing an empty global block with non-zero offsetIntoPhys must fail cleanly
    // without nullptr pointer-arithmetic UB or wild-pointer bypass.
    MemStream emptyBlockWithOffset;
    emptyBlockWithOffset.putBe64(0);   // ringBlockCount
    emptyBlockWithOffset.putBe64(0);   // bufferBlockCount
    emptyBlockWithOffset.putBe64(1);   // combinedBlockCount
    emptyBlockWithOffset.putBe32(0);   // combinedBlocks[0]: filled = 0 (empty)
    emptyBlockWithOffset.putBe32(2);   // handleIndex
    emptyBlockWithOffset.putBe32(1);   // contexts.size()
    emptyBlockWithOffset.putBe32(hA);  // handle
    emptyBlockWithOffset.putBe64(0);   // pingInfoGpa
    emptyBlockWithOffset.putByte(1);   // hasContext
    emptyBlockWithOffset.putBe32(ADDRESS_SPACE_CONTEXT_TYPE_GRAPHICS);
    emptyBlockWithOffset.putBe32(1);   // hasVirtioGpuInfo
    emptyBlockWithOffset.putBe32(1);   // contextId
    emptyBlockWithOffset.putBe32(0);   // capsetId
    emptyBlockWithOffset.putBe32(0);   // hasName
    emptyBlockWithOffset.putBe32(1);   // mVersion
    emptyBlockWithOffset.putBe32(0);   // exiting
    for (int i = 0; i < 2; ++i) {      // mRingAllocation, mBufferAllocation
        emptyBlockWithOffset.putBe64(0);
        emptyBlockWithOffset.putBe64(0);
        emptyBlockWithOffset.putBe64(0);
        emptyBlockWithOffset.putBe32(1);
    }
    emptyBlockWithOffset.putBe64(0);       // mCombinedAllocation.blockIndex = 0 (empty!)
    emptyBlockWithOffset.putBe64(0x4000);  // non-zero offsetIntoPhys
    emptyBlockWithOffset.putBe64(kAsgConsumerRingStorageSize + kAsgWriteBufferSize);
    emptyBlockWithOffset.putBe32(0);
    emptyBlockWithOffset.putBe32(0);       // consumerTag = 0
    emptyBlockWithOffset.rewind();
    installResources();
    EXPECT_NE(gfxstream_address_space_load_memory_state(&emptyBlockWithOffset), 0);

    // Truncate final byte of 2nd context's consumer payload so 1st context registers in
    // mAdditionalPostLoadRenderThreads and 2nd context triggers stream->hasErrors() rollback.
    installResources();
    const size_t cutLen = validSnap.writtenSize() - 1;
    MemStream truncatedSecondCtx(cutLen);
    ASSERT_EQ(truncatedSecondCtx.write(validSnap.buffer().data(), cutLen),
              static_cast<ssize_t>(cutLen));
    truncatedSecondCtx.rewind();
    EXPECT_NE(gfxstream_address_space_load_memory_state(&truncatedSecondCtx), 0);
    renderer->resumeAll();

    // Verify ClearGuard cleared sGlobals() on partial load failure so reusing handle hA with a
    // fresh backing buffer allocates cleanly rather than matching stale sGlobals() blocks.
    std::vector<char>& memReused = allocBackingBuffer();
    AddressSpaceCreateInfo reuseCi = {};
    reuseCi.handle = hA;
    reuseCi.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    reuseCi.createRenderThread = true;
    reuseCi.externalAddr = memReused.data();
    reuseCi.externalAddrSize = memReused.size();
    ops.create_instance(reuseCi);
    EXPECT_NE(ops.handle_to_context(hA), nullptr);
    ops.clear();
}

}  // namespace
}  // namespace host
}  // namespace gfxstream
