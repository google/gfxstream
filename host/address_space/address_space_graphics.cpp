// Copyright 2025 The Android Open Source Project
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

#include "gfxstream/host/address_space_graphics.h"

#include <memory>
#include <optional>

#include "gfxstream/AlignedBuf.h"
#include "gfxstream/host/address_space_device.h"
#include "gfxstream/host/logging.h"
#include "gfxstream/host/sub_allocator.h"
#include "render-utils/address_space_operations.h"

namespace gfxstream {
namespace host {

using android::base::AutoLock;
using android::base::Lock;

struct AllocationCreateInfo {
    bool virtioGpu;
    bool hostmemRegisterFixed;
    bool fromLoad;
    uint64_t size;
    uint64_t hostmemId;
    void *externalAddr;
    std::optional<uint32_t> dedicatedContextHandle;
};

struct Block {
    char* buffer = nullptr;
    uint64_t bufferSize = 0;
    SubAllocator* subAlloc = nullptr;
    uint64_t offsetIntoPhys = 0; // guest claimShared/mmap uses this
    bool isEmpty = true;
    std::optional<uint32_t> dedicatedContextHandle;
    bool usesVirtioGpuHostmem = false;
    uint64_t hostmemId = 0;
    bool external = false;
};

class Globals {
  public:
    Globals() : mPerContextBufferSize(kAsgWriteBufferSize) { }
    ~Globals() { clear(); }

    void setConsumer(android::emulation::asg::ConsumerInterface iface) {
        mConsumerInterface = iface;
    }

    android::emulation::asg::ConsumerInterface getConsumerInterface() {
        if (!mConsumerInterface.create) {
            GFXSTREAM_FATAL("Missing ASG consumer create interface.");
        }
        if (!mConsumerInterface.destroy) {
            GFXSTREAM_FATAL("Missing ASG consumer destroy interface.");
        }
        if (!mConsumerInterface.globalPreSave) {
            GFXSTREAM_FATAL("Missing ASG consumer globalPreSave interface.");
        }
        if (!mConsumerInterface.preSave) {
            GFXSTREAM_FATAL("Missing ASG consumer preSave interface.");
        }
        if (!mConsumerInterface.save) {
            GFXSTREAM_FATAL("Missing ASG consumer create interface.");
        }
        if (!mConsumerInterface.postSave) {
            GFXSTREAM_FATAL("Missing ASG consumer postSave interface.");
        }
        if (!mConsumerInterface.globalPostSave) {
            GFXSTREAM_FATAL("Missing ASG consumer globalPostSave interface.");
        }
        return mConsumerInterface;
    }

    void clear() {
        for (auto& block: mRingBlocks) {
            if (block.isEmpty) continue;
            destroyBlockLocked(block);
        }

        for (auto& block: mBufferBlocks) {
            if (block.isEmpty) continue;
            destroyBlockLocked(block);
        }

        for (auto& block: mCombinedBlocks) {
            if (block.isEmpty) continue;
            destroyBlockLocked(block);
        }

        mRingBlocks.clear();
        mBufferBlocks.clear();
        mCombinedBlocks.clear();
    }

    uint64_t perContextBufferSize() const {
        return mPerContextBufferSize;
    }

    Allocation newAllocation(struct AllocationCreateInfo& create,
                             std::vector<Block>& existingBlocks) {
        std::lock_guard<std::mutex> lock(mMutex);

        if (create.size > ADDRESS_SPACE_GRAPHICS_BLOCK_SIZE) {
            GFXSTREAM_FATAL("wanted size 0x%llx which is "
                            "greater than block size 0x%llx",
                            (unsigned long long)create.size,
                            (unsigned long long)ADDRESS_SPACE_GRAPHICS_BLOCK_SIZE);
        }

        Allocation res;

        size_t index = 0;
        for (index = 0; index < existingBlocks.size(); index++) {
            auto& block = existingBlocks[index];

            if (block.isEmpty) {
                fillBlockLocked(block, create);
            }

            if (block.dedicatedContextHandle != create.dedicatedContextHandle) {
                continue;
            }

            auto buf = block.subAlloc->alloc(create.size);
            if (buf) {
                res.buffer = (char*)buf;
                res.blockIndex = index;
                res.offsetIntoPhys =
                    block.offsetIntoPhys +
                    block.subAlloc->getOffset(buf);
                res.size = create.size;
                res.dedicatedContextHandle = create.dedicatedContextHandle;
                res.hostmemId = create.hostmemId;
                return res;
            } else {
                // block full
            }
        }

        Block newBlock;
        fillBlockLocked(newBlock, create);

        auto buf = newBlock.subAlloc->alloc(create.size);

        if (!buf) {
            GFXSTREAM_FATAL(
                "failed to allocate size 0x%llx "
                "(no free slots or out of host memory)",
                (unsigned long long)create.size);
        }

        existingBlocks.push_back(newBlock);

        res.buffer = (char*)buf;
        res.blockIndex = index;
        res.offsetIntoPhys =
            newBlock.offsetIntoPhys +
            newBlock.subAlloc->getOffset(buf);
        res.size = create.size;
        res.dedicatedContextHandle = create.dedicatedContextHandle;
        res.hostmemId = create.hostmemId;

        return res;
    }

    void deleteAllocation(const Allocation& alloc, std::vector<Block>& existingBlocks) {
        if (!alloc.buffer) return;

        std::lock_guard<std::mutex> lock(mMutex);

        if (existingBlocks.size() <= alloc.blockIndex) {
            GFXSTREAM_FATAL(
                "should be a block at index %zu "
                "but it is not found", alloc.blockIndex);
        }

        auto& block = existingBlocks[alloc.blockIndex];

        if (block.external) {
            destroyBlockLocked(block);
            return;
        }

        if (!block.subAlloc->free(alloc.buffer)) {
            GFXSTREAM_FATAL(
                "failed to free %p (block start: %p)",
                alloc.buffer,
                block.buffer);
        }

        if (shouldDestryBlockLocked(block)) {
            destroyBlockLocked(block);
        }
    }

    Allocation allocRingStorage() {
        struct AllocationCreateInfo create = {0};
        create.size = sizeof(struct asg_ring_storage);
        return newAllocation(create, mRingBlocks);
    }

    void freeRingStorage(const Allocation& alloc) {
        if (alloc.isView) return;
        deleteAllocation(alloc, mRingBlocks);
    }

    Allocation allocBuffer() {
        struct AllocationCreateInfo create = {0};
        create.size = mPerContextBufferSize;
        return newAllocation(create, mBufferBlocks);
    }

    void freeBuffer(const Allocation& alloc) {
        if (alloc.isView) return;
        deleteAllocation(alloc, mBufferBlocks);
    }

    Allocation allocRingAndBufferStorageDedicated(const struct AddressSpaceCreateInfo& asgCreate) {
        if (!asgCreate.handle) {
            GFXSTREAM_FATAL("Dedicated ASG allocation requested without dedicated handle.\n");
        }

        struct AllocationCreateInfo create = {0};
        create.size = sizeof(struct asg_ring_storage) + mPerContextBufferSize;
        create.dedicatedContextHandle = asgCreate.handle;
        create.virtioGpu = true;
        if (asgCreate.externalAddr) {
            create.externalAddr = asgCreate.externalAddr;
            if (asgCreate.externalAddrSize < static_cast<uint64_t>(create.size)) {
                GFXSTREAM_FATAL("External address size too small\n");
            }
            create.size = asgCreate.externalAddrSize;
        }

        return newAllocation(create, mCombinedBlocks);
    }

    Allocation allocRingViewIntoCombined(const Allocation& alloc) {
        Allocation res = alloc;
        res.buffer = alloc.buffer;
        res.size = sizeof(struct asg_ring_storage);
        res.isView = true;
        return res;
    }

    Allocation allocBufferViewIntoCombined(const Allocation& alloc) {
        Allocation res = alloc;
        res.buffer = alloc.buffer + sizeof(asg_ring_storage);
        res.size = mPerContextBufferSize;
        res.isView = true;
        return res;
    }

    void freeRingAndBuffer(const Allocation& alloc) {
        deleteAllocation(alloc, mCombinedBlocks);
    }

    void preSave() {
        // mConsumerInterface.globalPreSave();
    }

    void save(gfxstream::Stream* stream) {
        stream->putBe64(mRingBlocks.size());
        stream->putBe64(mBufferBlocks.size());
        stream->putBe64(mCombinedBlocks.size());

        for (const auto& block: mRingBlocks) {
            saveBlockLocked(stream, block);
        }

        for (const auto& block: mBufferBlocks) {
            saveBlockLocked(stream, block);
        }

        for (const auto& block: mCombinedBlocks) {
            saveBlockLocked(stream, block);
        }
    }

    void postSave() {
        // mConsumerInterface.globalPostSave();
    }

    bool load(gfxstream::Stream* stream,
              const std::optional<AddressSpaceDeviceLoadResources>& resources) {
        clear();
        mConsumerInterface.globalPreLoad();

        uint64_t ringBlockCount = stream->getBe64();
        uint64_t bufferBlockCount = stream->getBe64();
        uint64_t combinedBlockCount = stream->getBe64();

        mRingBlocks.resize(ringBlockCount);
        mBufferBlocks.resize(bufferBlockCount);
        mCombinedBlocks.resize(combinedBlockCount);

        for (auto& block: mRingBlocks) {
            loadBlockLocked(stream, resources, block);
        }

        for (auto& block: mBufferBlocks) {
            loadBlockLocked(stream, resources, block);
        }

        for (auto& block: mCombinedBlocks) {
            loadBlockLocked(stream, resources, block);
        }

        return true;
    }

    // Assumes that blocks have been loaded,
    // and that alloc has its blockIndex/offsetIntoPhys fields filled already
    void fillAllocFromLoad(Allocation& alloc, AddressSpaceGraphicsContext::AllocType allocType) {
        switch (allocType) {
            case AddressSpaceGraphicsContext::AllocType::AllocTypeRing:
                if (mRingBlocks.size() <= alloc.blockIndex) return;
                fillAllocFromLoad(mRingBlocks[alloc.blockIndex], alloc);
                break;
            case AddressSpaceGraphicsContext::AllocType::AllocTypeBuffer:
                if (mBufferBlocks.size() <= alloc.blockIndex) return;
                fillAllocFromLoad(mBufferBlocks[alloc.blockIndex], alloc);
                break;
            case AddressSpaceGraphicsContext::AllocType::AllocTypeCombined:
                if (mCombinedBlocks.size() <= alloc.blockIndex) return;
                fillAllocFromLoad(mCombinedBlocks[alloc.blockIndex], alloc);
                break;
            default:
                GFXSTREAM_FATAL("Unhandled alloc type.");
                break;
        }
    }

private:

    void saveBlockLocked(
        gfxstream::Stream* stream,
        const Block& block) {

        if (block.isEmpty) {
            stream->putBe32(0);
            return;
        } else {
            stream->putBe32(1);
        }

        stream->putBe64(block.bufferSize);
        stream->putBe64(block.offsetIntoPhys);
        if (block.dedicatedContextHandle) {
            stream->putBe32(1);
            stream->putBe32(*block.dedicatedContextHandle);
        } else {
            stream->putBe32(0);
        }
        stream->putBe32(block.usesVirtioGpuHostmem);
        stream->putBe64(block.hostmemId);
        block.subAlloc->save(stream);
        if (!block.external) {
            stream->write(block.buffer, block.bufferSize);
        }
    }

    void loadBlockLocked(gfxstream::Stream* stream,
                         const std::optional<AddressSpaceDeviceLoadResources>& resources,
                         Block& block) {
        uint32_t filled = stream->getBe32();
        struct AllocationCreateInfo create = {0};

        if (!filled) {
            block.isEmpty = true;
            return;
        } else {
            block.isEmpty = false;
        }

        create.size = stream->getBe64(); // `bufferSize`
        block.offsetIntoPhys = stream->getBe64();
        if (stream->getBe32() == 1) {
            create.dedicatedContextHandle = stream->getBe32();
        }
        create.virtioGpu = stream->getBe32();

        if (!create.virtioGpu) {
            GFXSTREAM_FATAL("Unhandled non virtio-gpu block.");
        }
        if (!create.dedicatedContextHandle) {
            GFXSTREAM_FATAL("Virtio GPU backed blocks are expected to have dedicated context.");
        }

        // Blocks whose memory are backed Virtio GPU resource do not own the external
        // memory. The external memory must be re-loaded outside of ASG and provided via
        // `resources`.
        if (!resources) {
            GFXSTREAM_FATAL(
                "Failed to load ASG context global block: "
                "Virtio GPU backed blocks need external memory resources for loading.\n");
        }

        const auto externalMemoryIt =
            resources->contextExternalMemoryMap.find(*create.dedicatedContextHandle);
        if (externalMemoryIt == resources->contextExternalMemoryMap.end()) {
            GFXSTREAM_FATAL(
                "Failed to load ASG context global block: "
                "Virtio GPU backed blocks an need external memory replacement.\n");
        }
        const auto& externalMemory = externalMemoryIt->second;
        create.externalAddr = externalMemory.externalAddress;

        create.hostmemRegisterFixed = true;
        create.fromLoad = true;
        create.hostmemId = stream->getBe64();

        fillBlockLocked(block, create);

        block.subAlloc->load(stream);

        if (!block.external) {
            stream->read(block.buffer, block.bufferSize);
        }
    }

    void fillAllocFromLoad(const Block& block, Allocation& alloc) {
        alloc.buffer = block.buffer + (alloc.offsetIntoPhys - block.offsetIntoPhys);
        alloc.dedicatedContextHandle = block.dedicatedContextHandle;
        alloc.hostmemId = block.hostmemId;
    }

    void fillBlockLocked(Block& block, struct AllocationCreateInfo& create) {
        if (!create.virtioGpu) {
            GFXSTREAM_FATAL("Unhandled non virtio-gpu allocation.");
        }
        if (!create.dedicatedContextHandle) {
            GFXSTREAM_FATAL("Unhandled non virtio-gpu non dedicated allocation.");
        }
        if (!create.externalAddr) {
            GFXSTREAM_FATAL("Cannot use dedicated allocation without virtio-gpu hostmem id");
        }

        block.external = true;
        block.buffer = (char*)create.externalAddr;
        block.bufferSize = create.size;
        block.subAlloc = new SubAllocator(block.buffer, block.bufferSize, ADDRESS_SPACE_GRAPHICS_PAGE_SIZE);
        block.offsetIntoPhys = 0;
        block.isEmpty = false;
        block.usesVirtioGpuHostmem = create.virtioGpu;
        block.hostmemId = create.hostmemId;
        block.dedicatedContextHandle = create.dedicatedContextHandle;
    }

    void destroyBlockLocked(Block& block) {
        if (!block.external) {
            GFXSTREAM_FATAL("Unhandled non-external block.");
        }

        delete block.subAlloc;

        block.isEmpty = true;
    }

    bool shouldDestryBlockLocked(const Block& block) const {
        return block.subAlloc->empty();
    }

    std::mutex mMutex;
    uint64_t mPerContextBufferSize = 0;
    android::emulation::asg::ConsumerInterface mConsumerInterface;
    std::vector<Block> mRingBlocks;
    std::vector<Block> mBufferBlocks;
    std::vector<Block> mCombinedBlocks;
};

static Globals* sGlobals() {
    static Globals* g = new Globals;
    return g;
}

// static
void AddressSpaceGraphicsContext::clear() {
    sGlobals()->clear();
}

// static
void AddressSpaceGraphicsContext::setConsumer(android::emulation::asg::ConsumerInterface iface) {
    sGlobals()->setConsumer(iface);
}

AddressSpaceGraphicsContext::AddressSpaceGraphicsContext(
    const struct AddressSpaceCreateInfo& create)
    : mConsumerCallbacks{
        .onUnavailableRead = [this] {
            return onUnavailableRead();
        },
        .getPtr = [](uint64_t physAddr) {
            return (char*)sAddressSpaceDeviceGetHostPtr(physAddr);
        },
      },
      mConsumerInterface(sGlobals()->getConsumerInterface()) {
    if (create.fromSnapshot) {
        // Use load() instead to initialize
        return;
    }

    const bool isVirtio = (create.type == AddressSpaceDeviceType::VirtioGpuGraphics);
    if (isVirtio) {
        VirtioGpuInfo& info = mVirtioGpuInfo.emplace();
        info.contextId = create.virtioGpuContextId;
        info.capsetId = create.virtioGpuCapsetId;
        if (create.contextNameSize) {
            info.name = std::string(create.contextName, create.contextNameSize);
        }

        mCombinedAllocation = sGlobals()->allocRingAndBufferStorageDedicated(create);
        mRingAllocation = sGlobals()->allocRingViewIntoCombined(mCombinedAllocation);
        mBufferAllocation = sGlobals()->allocBufferViewIntoCombined(mCombinedAllocation);
    } else {
        mRingAllocation = sGlobals()->allocRingStorage();
        mBufferAllocation = sGlobals()->allocBuffer();
    }

    if (!mRingAllocation.buffer) {
        GFXSTREAM_FATAL(
            "Failed to allocate ring for ASG context");
    }

    if (!mBufferAllocation.buffer) {
        GFXSTREAM_FATAL(
            "Failed to allocate buffer for ASG context");
    }

    mHostContext = asg_context_create(
        mRingAllocation.buffer,
        mBufferAllocation.buffer,
        sGlobals()->perContextBufferSize());
    mHostContext.ring_config->buffer_size =
        sGlobals()->perContextBufferSize();
    mHostContext.ring_config->flush_interval = kAsgWriteStepSize;
    mHostContext.ring_config->host_consumed_pos = 0;
    mHostContext.ring_config->guest_write_pos = 0;
    mHostContext.ring_config->transfer_mode = 1;
    mHostContext.ring_config->transfer_size = 0;
    mHostContext.ring_config->in_error = 0;

    mSavedConfig = *mHostContext.ring_config;

    if (create.createRenderThread) {
        mCurrentConsumer =
            mConsumerInterface.create(mHostContext, nullptr, mConsumerCallbacks,
                                      mVirtioGpuInfo ? mVirtioGpuInfo->contextId : 0,
                                      mVirtioGpuInfo ? mVirtioGpuInfo->capsetId : 0,
                                      mVirtioGpuInfo ? mVirtioGpuInfo->name : std::nullopt);
    }
}

AddressSpaceGraphicsContext::~AddressSpaceGraphicsContext() {
    if (mCurrentConsumer) {
        mExiting = 1;
        *(mHostContext.host_state) = ASG_HOST_STATE_EXIT;
        mConsumerMessages.send(ConsumerCommand::Exit);
        mConsumerInterface.destroy(mCurrentConsumer);
    }

    sGlobals()->freeBuffer(mBufferAllocation);
    sGlobals()->freeRingStorage(mRingAllocation);
    sGlobals()->freeRingAndBuffer(mCombinedAllocation);
}

void AddressSpaceGraphicsContext::perform(AddressSpaceDevicePingInfo* info) {
    switch (static_cast<asg_command>(info->metadata)) {
    case ASG_GET_RING:
        info->metadata = mRingAllocation.offsetIntoPhys;
        info->size = mRingAllocation.size;
        break;
    case ASG_GET_BUFFER:
        info->metadata = mBufferAllocation.offsetIntoPhys;
        info->size = mBufferAllocation.size;
        break;
    case ASG_SET_VERSION: {
        auto guestVersion = (uint32_t)info->size;
        info->size = (uint64_t)(mVersion > guestVersion ? guestVersion : mVersion);
        mVersion = (uint32_t)info->size;
        mCurrentConsumer = mConsumerInterface.create(
            mHostContext, nullptr /* no load stream */, mConsumerCallbacks, 0, 0,
            std::nullopt);

        if (mVirtioGpuInfo) {
            info->metadata = mCombinedAllocation.hostmemId;
        }
        break;
    }
    case ASG_NOTIFY_AVAILABLE:
        mConsumerMessages.trySend(ConsumerCommand::Wakeup);
        info->metadata = 0;
        break;
    case ASG_GET_CONFIG:
        *mHostContext.ring_config = mSavedConfig;
        info->metadata = 0;
        break;
    }
}

int AddressSpaceGraphicsContext::onUnavailableRead() {
    static const uint32_t kMaxUnavailableReads = 8;

    ++mUnavailableReadCount;
    ring_buffer_yield();

    ConsumerCommand cmd;

    if (mExiting) {
        mUnavailableReadCount = kMaxUnavailableReads;
    }

    if (mUnavailableReadCount >= kMaxUnavailableReads) {
        mUnavailableReadCount = 0;

sleep:
        *(mHostContext.host_state) = ASG_HOST_STATE_NEED_NOTIFY;
        mConsumerMessages.receive(&cmd);

        switch (cmd) {
            case ConsumerCommand::Wakeup:
                *(mHostContext.host_state) = ASG_HOST_STATE_CAN_CONSUME;
                break;
            case ConsumerCommand::Exit:
                *(mHostContext.host_state) = ASG_HOST_STATE_EXIT;
                return -1;
            case ConsumerCommand::Sleep:
                goto sleep;
            case ConsumerCommand::PausePreSnapshot:
                return -2;
            case ConsumerCommand::ResumePostSnapshot:
                return -3;
            default:
                GFXSTREAM_FATAL(
                    "AddressSpaceGraphicsContext::onUnavailableRead: "
                    "Unknown command: 0x%x\n",
                    (uint32_t)cmd);
        }

        return 1;
    }
    return 0;
}

AddressSpaceDeviceType AddressSpaceGraphicsContext::getDeviceType() const {
    return AddressSpaceDeviceType::Graphics;
}

void AddressSpaceGraphicsContext::preSave() const {
    if (mCurrentConsumer) {
        mConsumerInterface.preSave(mCurrentConsumer);
        mConsumerMessages.send(ConsumerCommand::PausePreSnapshot);
    }
}

void AddressSpaceGraphicsContext::save(gfxstream::Stream* stream) const {
    if (mVirtioGpuInfo) {
        const VirtioGpuInfo& info = *mVirtioGpuInfo;
        stream->putBe32(1);
        stream->putBe32(info.contextId);
        stream->putBe32(info.capsetId);
        if (info.name) {
            stream->putBe32(1);
            stream->putString(*info.name);
        } else {
            stream->putBe32(0);
        }
    } else {
        stream->putBe32(0);
    }

    stream->putBe32(mVersion);
    stream->putBe32(mExiting);
    stream->putBe32(mUnavailableReadCount);

    saveAllocation(stream, mRingAllocation);
    saveAllocation(stream, mBufferAllocation);
    saveAllocation(stream, mCombinedAllocation);

    saveRingConfig(stream, mSavedConfig);

    if (mCurrentConsumer) {
        stream->putBe32(1);
        mConsumerInterface.save(mCurrentConsumer, stream);
    } else {
        stream->putBe32(0);
    }
}

void AddressSpaceGraphicsContext::postSave() const {
    if (mCurrentConsumer) {
        mConsumerMessages.send(ConsumerCommand::ResumePostSnapshot);
        mConsumerInterface.postSave(mCurrentConsumer);
    }
}

bool AddressSpaceGraphicsContext::load(gfxstream::Stream* stream) {
    const bool hasVirtioGpuInfo = (stream->getBe32() == 1);
    if (hasVirtioGpuInfo) {
        VirtioGpuInfo& info = mVirtioGpuInfo.emplace();
        info.contextId = stream->getBe32();
        info.capsetId = stream->getBe32();
        const bool hasName = (stream->getBe32() == 1);
        if (hasName) {
            info.name = stream->getString();
        }
    }

    mVersion = stream->getBe32();
    mExiting = stream->getBe32();
    mUnavailableReadCount = stream->getBe32();

    loadAllocation(stream, mRingAllocation);
    loadAllocation(stream, mBufferAllocation);
    loadAllocation(stream, mCombinedAllocation);

    if (mVirtioGpuInfo) {
        sGlobals()->fillAllocFromLoad(mCombinedAllocation, AllocType::AllocTypeCombined);
        mRingAllocation = sGlobals()->allocRingViewIntoCombined(mCombinedAllocation);
        mBufferAllocation = sGlobals()->allocBufferViewIntoCombined(mCombinedAllocation);
    } else {
        sGlobals()->fillAllocFromLoad(mRingAllocation, AllocType::AllocTypeRing);
        sGlobals()->fillAllocFromLoad(mBufferAllocation, AllocType::AllocTypeBuffer);
    }

    mHostContext = asg_context_create(
        mRingAllocation.buffer,
        mBufferAllocation.buffer,
        sGlobals()->perContextBufferSize());
    mHostContext.ring_config->buffer_size = sGlobals()->perContextBufferSize();
    mHostContext.ring_config->flush_interval = kAsgWriteStepSize;

    // In load, the live ring config state is in shared host/guest ram.
    //
    // mHostContext.ring_config->host_consumed_pos = 0;
    // mHostContext.ring_config->transfer_mode = 1;
    // mHostContext.ring_config->transfer_size = 0;
    // mHostContext.ring_config->in_error = 0;

    loadRingConfig(stream, mSavedConfig);

    const bool hasConsumer = stream->getBe32() == 1;
    if (hasConsumer) {
        mCurrentConsumer =
            mConsumerInterface.create(mHostContext, stream, mConsumerCallbacks,
                                      mVirtioGpuInfo ? mVirtioGpuInfo->contextId : 0,
                                      mVirtioGpuInfo ? mVirtioGpuInfo->capsetId : 0,
                                      mVirtioGpuInfo ? mVirtioGpuInfo->name : std::nullopt);
        mConsumerInterface.postLoad(mCurrentConsumer);
    }

    return true;
}

void AddressSpaceGraphicsContext::globalStatePreSave() {
    sGlobals()->preSave();
}

void AddressSpaceGraphicsContext::globalStateSave(gfxstream::Stream* stream) {
    sGlobals()->save(stream);
}

void AddressSpaceGraphicsContext::globalStatePostSave() {
    sGlobals()->postSave();
}

bool AddressSpaceGraphicsContext::globalStateLoad(
    gfxstream::Stream* stream, const std::optional<AddressSpaceDeviceLoadResources>& resources) {
    return sGlobals()->load(stream, resources);
}

void AddressSpaceGraphicsContext::saveRingConfig(gfxstream::Stream* stream, const struct asg_ring_config& config) const {
    stream->putBe32(config.buffer_size);
    stream->putBe32(config.flush_interval);
    stream->putBe32(config.host_consumed_pos);
    stream->putBe32(config.guest_write_pos);
    stream->putBe32(config.transfer_mode);
    stream->putBe32(config.transfer_size);
    stream->putBe32(config.in_error);
}

void AddressSpaceGraphicsContext::saveAllocation(gfxstream::Stream* stream, const Allocation& alloc) const {
    stream->putBe64(alloc.blockIndex);
    stream->putBe64(alloc.offsetIntoPhys);
    stream->putBe64(alloc.size);
    stream->putBe32(alloc.isView);
}

void AddressSpaceGraphicsContext::loadRingConfig(gfxstream::Stream* stream, struct asg_ring_config& config) {
    config.buffer_size = stream->getBe32();
    config.flush_interval = stream->getBe32();
    config.host_consumed_pos = stream->getBe32();
    config.guest_write_pos = stream->getBe32();
    config.transfer_mode = stream->getBe32();
    config.transfer_size = stream->getBe32();
    config.in_error = stream->getBe32();
}

void AddressSpaceGraphicsContext::loadAllocation(gfxstream::Stream* stream, Allocation& alloc) {
    alloc.blockIndex = stream->getBe64();
    alloc.offsetIntoPhys = stream->getBe64();
    alloc.size = stream->getBe64();
    alloc.isView = stream->getBe32();
}

}  // namespace host
}  // namespace gfxstream
