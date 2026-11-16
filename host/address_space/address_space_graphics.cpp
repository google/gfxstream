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
#include "gfxstream/common/logging.h"
#include "gfxstream/host/sub_allocator.h"
#include "render-utils/address_space_operations.h"

namespace gfxstream {
namespace host {

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
    Globals() {}
    ~Globals() { clear(); }

    void setConsumer(ConsumerInterface iface) {
        std::lock_guard<std::mutex> lock(mMutex);
        mConsumerInterface = iface;
    }

    ConsumerInterface getConsumerInterface() {
        std::lock_guard<std::mutex> lock(mMutex);
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
        std::lock_guard<std::mutex> lock(mMutex);
        clearLocked();
    }

    Allocation newAllocation(struct AllocationCreateInfo& create,
                             std::vector<Block>& existingBlocks) {
        std::lock_guard<std::mutex> lock(mMutex);

        if (create.size > kAsgBlockSize) {
            GFXSTREAM_FATAL("wanted size 0x%llx which is "
                            "greater than block size 0x%llx",
                            (unsigned long long)create.size,
                            (unsigned long long)kAsgBlockSize);
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
        struct AllocationCreateInfo create = {};
        create.size = kAsgConsumerRingStorageSize;
        return newAllocation(create, mRingBlocks);
    }

    void freeRingStorage(const Allocation& alloc) {
        if (alloc.isView) return;
        deleteAllocation(alloc, mRingBlocks);
    }

    Allocation allocBuffer() {
        struct AllocationCreateInfo create = {};
        create.size = kAsgWriteBufferSize;
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

        struct AllocationCreateInfo create = {};
        create.size = kAsgConsumerRingStorageSize + kAsgWriteBufferSize;
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
        res.size = kAsgConsumerRingStorageSize;
        res.isView = true;
        if (!alloc.buffer || alloc.size < kAsgConsumerRingStorageSize) {
            res.buffer = nullptr;
            return res;
        }
        res.buffer = alloc.buffer;
        return res;
    }

    Allocation allocBufferViewIntoCombined(const Allocation& alloc) {
        Allocation res = alloc;
        res.size = kAsgWriteBufferSize;
        res.isView = true;
        constexpr uint64_t kMinCombinedSize =
            kAsgConsumerRingStorageSize + kAsgWriteBufferSize;
        if (!alloc.buffer || alloc.size < kMinCombinedSize) {
            res.buffer = nullptr;
            return res;
        }
        res.buffer = alloc.buffer + kAsgConsumerRingStorageSize;
        return res;
    }

    void freeRingAndBuffer(const Allocation& alloc) {
        deleteAllocation(alloc, mCombinedBlocks);
    }

    bool preSave() {
        // mConsumerInterface.globalPreSave();
        return true;
    }

    void save(gfxstream::Stream* stream,
              const std::unordered_set<uint32_t>* activeHandles) {
        std::lock_guard<std::mutex> lock(mMutex);
        stream->putBe64(mRingBlocks.size());
        stream->putBe64(mBufferBlocks.size());
        stream->putBe64(mCombinedBlocks.size());

        for (const auto& block: mRingBlocks) {
            saveBlockLocked(stream, block, activeHandles);
        }

        for (const auto& block: mBufferBlocks) {
            saveBlockLocked(stream, block, activeHandles);
        }

        for (const auto& block: mCombinedBlocks) {
            saveBlockLocked(stream, block, activeHandles);
        }
    }

    bool postSave() {
        // mConsumerInterface.globalPostSave();
        return true;
    }

    bool load(gfxstream::Stream* stream,
              const std::optional<AddressSpaceDeviceLoadResources>& resources) {
        std::function<void()> globalPreLoad;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            clearLocked();
            globalPreLoad = mConsumerInterface.globalPreLoad;
        }
        if (globalPreLoad) {
            globalPreLoad();
        }

        std::lock_guard<std::mutex> lock(mMutex);
        static constexpr uint64_t kMaxAsgBlocks = 65536;
        uint64_t ringBlockCount = stream->getBe64();
        uint64_t bufferBlockCount = stream->getBe64();
        uint64_t combinedBlockCount = stream->getBe64();
        if (stream->hasErrors() || ringBlockCount > kMaxAsgBlocks ||
            bufferBlockCount > kMaxAsgBlocks || combinedBlockCount > kMaxAsgBlocks) {
            stream->addError("Globals::load: invalid ASG block count");
            return false;
        }

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

        return !stream->hasErrors();
    }

    // Assumes that blocks have been loaded,
    // and that alloc has its blockIndex/offsetIntoPhys fields filled already
    void fillAllocFromLoad(Allocation& alloc, AddressSpaceGraphicsContext::AllocType allocType) {
        std::lock_guard<std::mutex> lock(mMutex);
        alloc.buffer = nullptr;
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
    void clearLocked() {
        for (auto& block : mRingBlocks) {
            if (!block.isEmpty) {
                destroyBlockLocked(block);
            }
        }

        for (auto& block : mBufferBlocks) {
            if (!block.isEmpty) {
                destroyBlockLocked(block);
            }
        }

        for (auto& block : mCombinedBlocks) {
            if (!block.isEmpty) {
                destroyBlockLocked(block);
            }
        }

        mRingBlocks.clear();
        mBufferBlocks.clear();
        mCombinedBlocks.clear();
    }

    void saveBlockLocked(
        gfxstream::Stream* stream,
        const Block& block,
        const std::unordered_set<uint32_t>* activeHandles) {
        const bool excludedDedicatedBlock =
            activeHandles && block.dedicatedContextHandle &&
            activeHandles->find(*block.dedicatedContextHandle) == activeHandles->end();
        if (block.isEmpty || excludedDedicatedBlock) {
            stream->putBe32(0);
            return;
        }
        stream->putBe32(1);

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
            const ssize_t written = stream->write(block.buffer, block.bufferSize);
            if (written != static_cast<ssize_t>(block.bufferSize)) {
                stream->addError("saveBlockLocked: expected %llu, got %zd",
                                 static_cast<unsigned long long>(block.bufferSize), written);
            }
        }
    }

    void loadBlockLocked(gfxstream::Stream* stream,
                         const std::optional<AddressSpaceDeviceLoadResources>& resources,
                         Block& block) {
        block = Block{};
        const uint32_t filled = stream->getBe32();
        if (!filled || stream->hasErrors()) {
            return;
        }

        struct AllocationCreateInfo create = {};
        create.size = stream->getBe64(); // `bufferSize`
        const uint64_t savedOffsetIntoPhys = stream->getBe64();
        const uint32_t dedicatedTag = stream->getBe32();
        if (dedicatedTag == 1) {
            create.dedicatedContextHandle = stream->getBe32();
        }
        create.virtioGpu = stream->getBe32();
        create.hostmemId = stream->getBe64();
        if (stream->hasErrors() || dedicatedTag > 1) {
            stream->addError("loadBlockLocked: invalid or truncated block header");
            return;
        }
        if (!create.virtioGpu || !create.dedicatedContextHandle) {
            GFXSTREAM_ERROR("Failed to load ASG block: expected dedicated virtio-gpu block.");
            stream->addError("loadBlockLocked: non-dedicated or non-virtio-gpu block");
            return;
        }
        if (!resources) {
            GFXSTREAM_ERROR("Failed to load ASG block: missing external memory resources.");
            stream->addError("loadBlockLocked: missing load resources");
            return;
        }

        const auto externalMemoryIt =
            resources->contextExternalMemoryMap.find(*create.dedicatedContextHandle);
        if (externalMemoryIt == resources->contextExternalMemoryMap.end()) {
            GFXSTREAM_ERROR("Failed to load ASG block: missing external memory for handle %u.",
                            *create.dedicatedContextHandle);
            stream->addError("loadBlockLocked: missing external memory replacement");
            return;
        }
        const auto& externalMemory = externalMemoryIt->second;
        constexpr uint64_t kMinDedicatedBlockSize =
            kAsgConsumerRingStorageSize + kAsgWriteBufferSize;
        if (!externalMemory.externalAddress || create.size < kMinDedicatedBlockSize ||
            create.size > externalMemory.externalAddressSize) {
            GFXSTREAM_ERROR(
                "Failed to load ASG block: external buffer bounds mismatch (%llu vs %llu).",
                static_cast<unsigned long long>(create.size),
                static_cast<unsigned long long>(externalMemory.externalAddressSize));
            stream->addError("loadBlockLocked: external buffer bounds mismatch");
            return;
        }
        create.externalAddr = externalMemory.externalAddress;
        create.hostmemRegisterFixed = true;
        create.fromLoad = true;

        fillBlockLocked(block, create);
        block.offsetIntoPhys = savedOffsetIntoPhys;
        block.subAlloc->load(stream);
    }

    void fillAllocFromLoad(const Block& block, Allocation& alloc) {
        alloc.buffer = nullptr;
        if (block.isEmpty || !block.buffer) {
            return;
        }
        if (alloc.offsetIntoPhys < block.offsetIntoPhys) {
            return;
        }
        const uint64_t relOffset = alloc.offsetIntoPhys - block.offsetIntoPhys;
        if (relOffset > block.bufferSize || alloc.size > block.bufferSize - relOffset) {
            return;
        }
        alloc.buffer = block.buffer + relOffset;
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
        block.subAlloc = new SubAllocator(block.buffer, block.bufferSize, kAsgPageSize);
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
        block.subAlloc = nullptr;
        block.buffer = nullptr;
        block.bufferSize = 0;
        block.isEmpty = true;
    }

    bool shouldDestryBlockLocked(const Block& block) const {
        return block.subAlloc->empty();
    }

    std::mutex mMutex;
    ConsumerInterface mConsumerInterface;
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
void AddressSpaceGraphicsContext::setConsumer(ConsumerInterface iface) {
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

    const bool isVirtio =
        static_cast<AddressSpaceDeviceType>(create.type) == AddressSpaceDeviceType::VirtioGpuGraphics;

    if (isVirtio) {
        mVirtioGpuInfo = VirtioGpuInfo{};
        VirtioGpuInfo& info = *mVirtioGpuInfo;
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

    if (create.createRenderThread) {
        const AsgConsumerCreateInfo& consumerCreateInfo = {
            .version = mVersion,
            .ring_storage = mRingAllocation.buffer,
            .buffer = mBufferAllocation.buffer,
            .buffer_size = kAsgWriteBufferSize,
            .buffer_flush_interval = kAsgWriteStepSize,
            .callbacks = mConsumerCallbacks,
            .virtioGpuContextId = mVirtioGpuInfo ?
                std::optional<uint32_t>(mVirtioGpuInfo->contextId) :
                std::nullopt,
            .virtioGpuContextName = mVirtioGpuInfo ?
                std::optional<std::string>(mVirtioGpuInfo->name) :
                std::nullopt,
            .virtioGpuCapsetId = mVirtioGpuInfo ?
                std::optional<uint32_t>(mVirtioGpuInfo->capsetId) :
                std::nullopt,
        };
        mCurrentConsumer = mConsumerInterface.create(consumerCreateInfo, nullptr);
    }
}

AddressSpaceGraphicsContext::~AddressSpaceGraphicsContext() {
    if (mCurrentConsumer) {
        bool hadActivePreSave = false;
        {
            std::lock_guard<std::mutex> lock(mConsumerStateMutex);
            hadActivePreSave = mConsumerState.preSaveActive;
            mConsumerState.exiting = true;
            if (!hadActivePreSave) {
                mConsumerStateCv.notify_all();
            }
        }
        if (hadActivePreSave) {
            cancelPreSave();
        }
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

        const AsgConsumerCreateInfo& consumerCreateInfo = {
            .version = mVersion,
            .ring_storage = mRingAllocation.buffer,
            .buffer = mBufferAllocation.buffer,
            .buffer_size = kAsgWriteBufferSize,
            .buffer_flush_interval = kAsgWriteStepSize,
            .callbacks = mConsumerCallbacks,
            .virtioGpuContextId = mVirtioGpuInfo ?
                std::optional<uint32_t>(mVirtioGpuInfo->contextId) :
                std::nullopt,
            .virtioGpuContextName = mVirtioGpuInfo ?
                std::optional<std::string>(mVirtioGpuInfo->name) :
                std::nullopt,
            .virtioGpuCapsetId = mVirtioGpuInfo ?
                std::optional<uint32_t>(mVirtioGpuInfo->capsetId) :
                std::nullopt,
        };
        mCurrentConsumer = mConsumerInterface.create(consumerCreateInfo, nullptr);

        if (mVirtioGpuInfo) {
            info->metadata = mCombinedAllocation.hostmemId;
        }
        break;
    }
    case ASG_NOTIFY_AVAILABLE:
        {
            std::lock_guard<std::mutex> lock(mConsumerStateMutex);
            mConsumerState.wakeupRequested = true;
            mConsumerStateCv.notify_one();
        }
        info->metadata = 0;
        break;
    case ASG_GET_CONFIG:
        info->metadata = 0;
        break;
    }
}

AsgOnUnavailableReadStatus AddressSpaceGraphicsContext::onUnavailableRead() {
    std::unique_lock<std::mutex> lock(mConsumerStateMutex);
    gfxstream::base::ScopedLockAssertion lockAssertion(mConsumerStateMutex);
    mConsumerStateCv.wait(lock, [this]() {
        gfxstream::base::ScopedLockAssertion lockAssertion(mConsumerStateMutex);
        return mConsumerState.hasPendingWork();
    });

    if (mConsumerState.exiting) {
        return AsgOnUnavailableReadStatus::kExit;
    }
    if (mConsumerState.pausePreSnapshotRequested) {
        mConsumerState.pausePreSnapshotRequested = false;
        return AsgOnUnavailableReadStatus::kPauseForSnapshot;
    }
    if (mConsumerState.resumePostSnapshotRequested) {
        mConsumerState.resumePostSnapshotRequested = false;
        return AsgOnUnavailableReadStatus::kResumeAfterSnapshot;
    }
    if (mConsumerState.wakeupRequested) {
        mConsumerState.wakeupRequested = false;
        return AsgOnUnavailableReadStatus::kContinue;
    }
    return AsgOnUnavailableReadStatus::kContinue;
}

AddressSpaceDeviceType AddressSpaceGraphicsContext::getDeviceType() const {
    return AddressSpaceDeviceType::Graphics;
}

bool AddressSpaceGraphicsContext::preSave() const {
    if (!mCurrentConsumer) {
        return true;
    }
    if (!mConsumerInterface.preSave) {
        GFXSTREAM_ERROR("Missing ASG consumer preSave callback.");
        return false;
    }
    bool needConsumerPreSave = false;
    {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        needConsumerPreSave = !mConsumerState.preSaveActive;
    }
    if (needConsumerPreSave) {
        mConsumerInterface.preSave(mCurrentConsumer);
    }
    {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        mConsumerState.preSaveActive = true;
        mConsumerState.pauseWaitFailed = false;
        mConsumerState.resumePostSnapshotRequested = false;
        mConsumerState.pausePreSnapshotRequested = true;
        mConsumerStateCv.notify_all();
    }
    return true;
}

bool AddressSpaceGraphicsContext::waitForPreSave() const {
    if (!mCurrentConsumer) {
        return true;
    }
    if (!mConsumerInterface.waitForPause) {
        GFXSTREAM_ERROR("Missing ASG consumer waitForPause callback.");
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        mConsumerState.pauseWaitFailed = true;
        return false;
    }
    if (mConsumerInterface.waitForPause(mCurrentConsumer)) {
        return true;
    }
    std::lock_guard<std::mutex> lock(mConsumerStateMutex);
    mConsumerState.pauseWaitFailed = true;
    return false;
}

void AddressSpaceGraphicsContext::cancelPreSave() const {
    if (!mCurrentConsumer) {
        return;
    }
    if (mConsumerInterface.cancelPreSave) {
        {
            std::lock_guard<std::mutex> lock(mConsumerStateMutex);
            const bool pauseWasQueued = mConsumerState.pausePreSnapshotRequested;
            mConsumerState.preSaveActive = false;
            mConsumerState.pauseWaitFailed = false;
            mConsumerState.pausePreSnapshotRequested = false;
            if (!pauseWasQueued && !mConsumerState.exiting) {
                mConsumerState.resumePostSnapshotRequested = true;
            }
            mConsumerStateCv.notify_all();
        }
        mConsumerInterface.cancelPreSave(mCurrentConsumer);
        return;
    }

    bool shouldWaitAndPostSave = false;
    {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        if (mConsumerState.pauseWaitFailed) {
            mConsumerState.pausePreSnapshotRequested = false;
            if (mConsumerState.exiting) {
                mConsumerState.preSaveActive = false;
                mConsumerState.pauseWaitFailed = false;
            }
        } else if (mConsumerState.exiting && mConsumerState.pausePreSnapshotRequested) {
            mConsumerState.preSaveActive = false;
            mConsumerState.pausePreSnapshotRequested = false;
        } else {
            shouldWaitAndPostSave = mConsumerState.preSaveActive;
        }
        mConsumerStateCv.notify_all();
    }
    if (!shouldWaitAndPostSave) {
        return;
    }
    if (!waitForPreSave()) {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        mConsumerState.pausePreSnapshotRequested = false;
        if (mConsumerState.exiting) {
            mConsumerState.preSaveActive = false;
            mConsumerState.pauseWaitFailed = false;
        }
        mConsumerStateCv.notify_all();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        mConsumerState.preSaveActive = false;
        mConsumerState.pauseWaitFailed = false;
        mConsumerState.pausePreSnapshotRequested = false;
        if (!mConsumerState.exiting) {
            mConsumerState.resumePostSnapshotRequested = true;
        }
        mConsumerStateCv.notify_all();
    }
    if (mConsumerInterface.postSave) {
        mConsumerInterface.postSave(mCurrentConsumer);
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
    {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        stream->putBe32(mConsumerState.exiting ? 1 : 0);
    }

    saveAllocation(stream, mRingAllocation);
    saveAllocation(stream, mBufferAllocation);
    saveAllocation(stream, mCombinedAllocation);

    if (mCurrentConsumer) {
        stream->putBe32(1);
        mConsumerInterface.save(mCurrentConsumer, stream);
    } else {
        stream->putBe32(0);
    }
}

bool AddressSpaceGraphicsContext::postSave() const {
    if (!mCurrentConsumer) {
        return true;
    }
    if (!mConsumerInterface.postSave) {
        GFXSTREAM_ERROR("Missing ASG consumer postSave callback.");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        mConsumerState.preSaveActive = false;
        mConsumerState.pauseWaitFailed = false;
        mConsumerState.pausePreSnapshotRequested = false;
        mConsumerState.resumePostSnapshotRequested = true;
        mConsumerStateCv.notify_all();
    }
    mConsumerInterface.postSave(mCurrentConsumer);
    return true;
}

bool AddressSpaceGraphicsContext::load(gfxstream::Stream* stream) {
    if (!stream) {
        return false;
    }
    const uint32_t virtioGpuTag = stream->getBe32();
    if (virtioGpuTag > 1) {
        return false;
    }
    if (virtioGpuTag == 1) {
        mVirtioGpuInfo = VirtioGpuInfo{};
        VirtioGpuInfo& info = *mVirtioGpuInfo;
        info.contextId = stream->getBe32();
        info.capsetId = stream->getBe32();
        const uint32_t nameTag = stream->getBe32();
        if (nameTag > 1) {
            return false;
        }
        if (nameTag == 1) {
            info.name = stream->getString();
        }
    }

    mVersion = stream->getBe32();
    const uint32_t exitingTag = stream->getBe32();
    {
        std::lock_guard<std::mutex> lock(mConsumerStateMutex);
        mConsumerState = ConsumerState{};
        mConsumerState.exiting = (exitingTag != 0);
    }

    loadAllocation(stream, mRingAllocation);
    loadAllocation(stream, mBufferAllocation);
    loadAllocation(stream, mCombinedAllocation);
    const uint32_t consumerTag = stream->getBe32();
    if (stream->hasErrors() || consumerTag > 1) {
        return false;
    }

    if (mVirtioGpuInfo) {
        sGlobals()->fillAllocFromLoad(mCombinedAllocation, AllocType::AllocTypeCombined);
        if (!mCombinedAllocation.buffer) {
            GFXSTREAM_ERROR("Failed to restore ASG combined allocation.");
            return false;
        }
        mRingAllocation = sGlobals()->allocRingViewIntoCombined(mCombinedAllocation);
        mBufferAllocation = sGlobals()->allocBufferViewIntoCombined(mCombinedAllocation);
    } else {
        if (mRingAllocation.size < kAsgConsumerRingStorageSize ||
            mBufferAllocation.size < kAsgWriteBufferSize) {
            GFXSTREAM_ERROR("Invalid ASG ring or buffer allocation size in snapshot.");
            return false;
        }
        sGlobals()->fillAllocFromLoad(mRingAllocation, AllocType::AllocTypeRing);
        sGlobals()->fillAllocFromLoad(mBufferAllocation, AllocType::AllocTypeBuffer);
    }
    if (!mRingAllocation.buffer || !mBufferAllocation.buffer) {
        GFXSTREAM_ERROR("Failed to restore ASG ring or buffer allocation.");
        return false;
    }

    if (consumerTag == 0) {
        return true;
    }
    if (!mConsumerInterface.create) {
        GFXSTREAM_ERROR("Missing ASG consumer create callback.");
        return false;
    }
    const AsgConsumerCreateInfo consumerCreateInfo = {
        .version = mVersion,
        .ring_storage = mRingAllocation.buffer,
        .buffer = mBufferAllocation.buffer,
        .buffer_size = kAsgWriteBufferSize,
        .buffer_flush_interval = kAsgWriteStepSize,
        .callbacks = mConsumerCallbacks,
        .virtioGpuContextId = mVirtioGpuInfo ?
            std::optional<uint32_t>(mVirtioGpuInfo->contextId) :
            std::nullopt,
        .virtioGpuContextName = mVirtioGpuInfo ?
            std::optional<std::string>(mVirtioGpuInfo->name) :
            std::nullopt,
        .virtioGpuCapsetId = mVirtioGpuInfo ?
            std::optional<uint32_t>(mVirtioGpuInfo->capsetId) :
            std::nullopt,
    };
    mCurrentConsumer = mConsumerInterface.create(consumerCreateInfo, stream);
    if (!mCurrentConsumer || stream->hasErrors()) {
        return false;
    }
    if (mConsumerInterface.postLoad) {
        mConsumerInterface.postLoad(mCurrentConsumer);
    }

    return !stream->hasErrors();
}

bool AddressSpaceGraphicsContext::globalStatePreSave() {
    return sGlobals()->preSave();
}

void AddressSpaceGraphicsContext::globalStateSave(
    gfxstream::Stream* stream,
    const std::unordered_set<uint32_t>* activeHandles) {
    sGlobals()->save(stream, activeHandles);
}

bool AddressSpaceGraphicsContext::globalStatePostSave() {
    return sGlobals()->postSave();
}

bool AddressSpaceGraphicsContext::globalStateLoad(
    gfxstream::Stream* stream, const std::optional<AddressSpaceDeviceLoadResources>& resources) {
    return sGlobals()->load(stream, resources);
}

void AddressSpaceGraphicsContext::saveAllocation(gfxstream::Stream* stream, const Allocation& alloc) const {
    stream->putBe64(alloc.blockIndex);
    stream->putBe64(alloc.offsetIntoPhys);
    stream->putBe64(alloc.size);
    stream->putBe32(alloc.isView);
}

void AddressSpaceGraphicsContext::loadAllocation(gfxstream::Stream* stream, Allocation& alloc) {
    alloc.blockIndex = stream->getBe64();
    alloc.offsetIntoPhys = stream->getBe64();
    alloc.size = stream->getBe64();
    alloc.isView = stream->getBe32();
}

}  // namespace host
}  // namespace gfxstream
