// Copyright 2019 The Android Open Source Project
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

#include "gfxstream/host/address_space_device.h"

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gfxstream/host/address_space_graphics.h"
#include "gfxstream/host/address_space_service.h"
#include "gfxstream/common/logging.h"
#include "render-utils/address_space_operations.h"

namespace gfxstream {
namespace host {

using gfxstream::Stream;

class AddressSpaceDeviceState {
  public:
    AddressSpaceDeviceState() = default;
    ~AddressSpaceDeviceState() = default;

    uint32_t genHandle() {
        std::unique_lock<std::mutex> lock(mContextsMutex);
        mSnapshotCv.wait(lock, [this] {
            return mActiveSnapshotOps == 0 && !mClearInProgress;
        });

        auto res = mHandleIndex;
        if (!res) {
            ++res;
            mHandleIndex += 2;
        } else {
            ++mHandleIndex;
        }

        return res;
    }

    void destroyHandle(uint32_t handle) {
        std::shared_ptr<AddressSpaceDeviceContext> context;

        {
            std::unique_lock<std::mutex> lock(mContextsMutex);
            mSnapshotCv.wait(lock, [this] {
                return mActiveSnapshotOps == 0 && mCreatingContextsCount == 0 &&
                       !mClearInProgress;
            });

            auto contextDescriptionIt = mContexts.find(handle);
            if (contextDescriptionIt == mContexts.end()) {
                return;
            }
            context = std::move(contextDescriptionIt->second.device_context);
            mContexts.erase(contextDescriptionIt);
            if (context) {
                ++mDestroyingContextsCount;
            }
        }

        finishContextDestruction(std::move(context));
    }

    void createInstance(const struct AddressSpaceCreateInfo& create) {
        const bool usesSharedBlock =
            static_cast<AddressSpaceDeviceType>(create.type) !=
            AddressSpaceDeviceType::VirtioGpuGraphics;
        const bool blockWhilePreSaved = usesSharedBlock || create.createRenderThread;
        {
            std::unique_lock<std::mutex> lock(mContextsMutex);
            mSnapshotCv.wait(lock, [this, blockWhilePreSaved] {
                return mActiveSnapshotOps == 0 && !mClearInProgress &&
                       (!blockWhilePreSaved || mSaveState != SaveState::PreSaved);
            });
            ++mCreatingContextsCount;
        }

        std::shared_ptr<AddressSpaceDeviceContext> newContext =
            buildAddressSpaceDeviceContext(create);
        std::shared_ptr<AddressSpaceDeviceContext> oldContext;
        {
            std::lock_guard<std::mutex> lock(mContextsMutex);
            auto& contextDesc = mContexts[create.handle];
            oldContext = std::move(contextDesc.device_context);
            contextDesc.device_context = std::move(newContext);
            if (oldContext) {
                ++mDestroyingContextsCount;
            }
            --mCreatingContextsCount;
            if (mCreatingContextsCount == 0) {
                mSnapshotCv.notify_all();
            }
        }
        finishContextDestruction(std::move(oldContext));
    }

    void ping(uint32_t handle) {
        std::unique_lock<std::mutex> lock(mContextsMutex);
        mSnapshotCv.wait(lock, [this] {
            return mActiveSnapshotOps == 0 && mCreatingContextsCount == 0 &&
                   mSaveState != SaveState::PreSaved && !mClearInProgress;
        });
        auto& contextDesc = mContexts[handle];
        AddressSpaceDevicePingInfo* pingInfo = contextDesc.pingInfo;

        const uint64_t phys_addr = pingInfo->phys_addr;

        AddressSpaceDeviceContext *device_context = contextDesc.device_context.get();
        if (device_context) {
            device_context->perform(pingInfo);
        } else {
            // The first ioctl establishes the device type
            struct AddressSpaceCreateInfo create = {};
            create.type = static_cast<uint32_t>(pingInfo->metadata);
            create.physAddr = phys_addr;

            contextDesc.device_context = buildAddressSpaceDeviceContext(create);
            pingInfo->metadata = contextDesc.device_context ? 0 : -1;
        }
    }

    void pingAtHva(uint32_t handle, AddressSpaceDevicePingInfo* pingInfo) {
        std::unique_lock<std::mutex> lock(mContextsMutex);
        mSnapshotCv.wait(lock, [this] {
            return mActiveSnapshotOps == 0 && mCreatingContextsCount == 0 &&
                   mSaveState != SaveState::PreSaved && !mClearInProgress;
        });
        auto& contextDesc = mContexts[handle];

        const uint64_t phys_addr = pingInfo->phys_addr;

        AddressSpaceDeviceContext *device_context = contextDesc.device_context.get();
        if (device_context) {
            device_context->perform(pingInfo);
        } else {
            struct AddressSpaceCreateInfo create = {};
            create.type = static_cast<uint32_t>(pingInfo->metadata);
            create.physAddr = phys_addr;

            contextDesc.device_context = buildAddressSpaceDeviceContext(create);
            pingInfo->metadata = contextDesc.device_context ? 0 : -1;
        }
    }

    void registerDeallocationCallback(uint64_t gpa, void* context, address_space_device_deallocation_callback_t func) {
        std::lock_guard<std::mutex> lock(mContextsMutex);
        auto& currentCallbacks = mDeallocationCallbacks[gpa];

        DeallocationCallbackEntry entry = {
            context,
            func,
        };

        currentCallbacks.push_back(entry);
    }

    void runDeallocationCallbacks(uint64_t gpa) {
        std::lock_guard<std::mutex> lock(mContextsMutex);

        auto it = mDeallocationCallbacks.find(gpa);
        if (it == mDeallocationCallbacks.end()) return;

        auto& callbacks = it->second;

        for (auto& entry: callbacks) {
            entry.func(entry.context, gpa);
        }

        mDeallocationCallbacks.erase(gpa);
    }

    AddressSpaceDeviceContext* handleToContext(uint32_t handle) {
        std::lock_guard<std::mutex> lock(mContextsMutex);
        if (mContexts.find(handle) == mContexts.end()) return nullptr;

        auto& contextDesc = mContexts[handle];
        return contextDesc.device_context.get();
    }

    bool preSave() const {
        uint32_t handleIndex = 0;
        std::vector<ContextEntrySnapshot> initialContexts;
        if (!beginPreSave(&handleIndex, &initialContexts)) {
            return false;
        }

        std::shared_ptr<const ActiveSnapshot> active;
        const bool ok = runPreSave(handleIndex, initialContexts, &active);
        initialContexts.clear();
        endSnapshotOp(ok ? SaveState::PreSaved : SaveState::Reset,
                      /*updateSnapshot=*/true,
                      ok ? std::move(active) : nullptr);
        return ok;
    }

    bool postSave() const {
        std::shared_ptr<const ActiveSnapshot> active;
        if (!beginPostSave(&active)) {
            return false;
        }

        const bool ok = resumeSnapshot(active.get());
        active.reset();
        endSnapshotOp(ok ? SaveState::PostSaved : SaveState::Reset,
                      /*updateSnapshot=*/false, nullptr);
        return ok;
    }

    bool save(Stream* stream) const {
        if (!stream) {
            GFXSTREAM_ERROR("Null stream passed to ASG save().");
            return false;
        }
        bool managePrePostSave = false;
        uint32_t handleIndex = 0;
        std::vector<ContextEntrySnapshot> initialContexts;
        std::shared_ptr<const ActiveSnapshot> active;
        if (!beginSave(&managePrePostSave, &handleIndex, &initialContexts, &active)) {
            return false;
        }

        if (managePrePostSave) {
            const bool preOk = runPreSave(handleIndex, initialContexts, &active);
            initialContexts.clear();
            if (!preOk) {
                endSnapshotOp(SaveState::Reset, /*updateSnapshot=*/true, nullptr);
                return false;
            }
            setSaveStateLocked(SaveState::Saving);
        }

        writeSnapshotStream(stream, *active);
        const bool streamOk = !stream->hasErrors();
        if (!streamOk) {
            GFXSTREAM_ERROR("Stream write error while saving ASG state.");
        }

        if (!managePrePostSave) {
            active.reset();
            endSnapshotOp(SaveState::PreSaved, /*updateSnapshot=*/false, nullptr);
            return streamOk;
        }

        setSaveStateLocked(SaveState::PostSaving);
        const bool postOk = resumeSnapshot(active.get());
        active.reset();
        const bool allOk = postOk && streamOk;
        endSnapshotOp(allOk ? SaveState::PostSaved : SaveState::Reset,
                      /*updateSnapshot=*/true, nullptr);
        return allOk;
    }

    void setLoadResources(AddressSpaceDeviceLoadResources resources) {
        std::lock_guard<std::mutex> lock(mContextsMutex);
        mLoadResources = std::move(resources);
    }

    bool load(Stream* stream) {
        if (!stream) {
            GFXSTREAM_ERROR("Null stream passed to ASG load().");
            return false;
        }
        // Destroy all existing contexts and hold mClearInProgress across the entire load
        // sequence until installLoadedContextsAndFinishClear() swaps in the loaded state.
        beginClear();
        ClearGuard clearGuard(this);

        std::optional<AddressSpaceDeviceLoadResources> loadResources;
        {
            std::lock_guard<std::mutex> lock(mContextsMutex);
            loadResources = mLoadResources;
        }

        if (!AddressSpaceGraphicsContext::globalStateLoad(stream, loadResources)) {
            return false;
        }

        const uint32_t handleIndex = stream->getBe32();
        const size_t size = stream->getBe32();
        if (stream->hasErrors()) {
            return false;
        }

        std::unordered_map<uint32_t, AddressSpaceContextDescription> contexts;
        for (size_t i = 0; i < size; ++i) {
            if (!loadSingleContextEntry(stream, &contexts)) {
                return false;
            }
        }

        installLoadedContextsAndFinishClear(handleIndex, std::move(contexts));
        clearGuard.release();

        return true;
    }

    void clear() {
        beginClear();
        finishClear();
    }

    bool addMemoryMapping(uint64_t gpa, void *ptr, uint64_t size) {
        std::lock_guard<std::mutex> lock(mMemoryMappingsLock);
        return addMemoryMappingLocked(gpa, ptr, size);
    }

    bool removeMemoryMapping(uint64_t gpa, uint64_t size) {
        std::lock_guard<std::mutex> lock(mMemoryMappingsLock);
        return removeMemoryMappingLocked(gpa, size);
    }

    void *getHostPtr(uint64_t gpa) const {
        std::lock_guard<std::mutex> lock(mMemoryMappingsLock);
        return getHostPtrLocked(gpa);
    }

  private:
    std::unique_ptr<AddressSpaceDeviceContext> buildAddressSpaceDeviceContext(
            const struct AddressSpaceCreateInfo& create) {
        switch (static_cast<AddressSpaceDeviceType>(create.type)) {
            case AddressSpaceDeviceType::Graphics:
            case AddressSpaceDeviceType::VirtioGpuGraphics:
                return std::unique_ptr<AddressSpaceDeviceContext>(new AddressSpaceGraphicsContext(create));
            default:
                GFXSTREAM_FATAL("Unhandled address space context type: %d", create.type);
                return nullptr;
        }
    }

    bool addMemoryMappingLocked(uint64_t gpa, void *ptr, uint64_t size) {
        if (mMemoryMappings.insert({gpa, {ptr, size}}).second) {
            return true;
        } else {
            GFXSTREAM_ERROR("Failed: hva %p -> gpa [0x%llx 0x%llx]",
                            ptr, (unsigned long long)gpa, (unsigned long long)size);
            return false;
        }
    }

    bool removeMemoryMappingLocked(uint64_t gpa, uint64_t size) {
        if (mMemoryMappings.erase(gpa) > 0) {
            return true;
        } else {
            GFXSTREAM_FATAL("Failed: gpa [0x%llx 0x%llx]",
                            (unsigned long long)gpa, (unsigned long long)size);
            return false;
        }
    }

    void *getHostPtrLocked(uint64_t gpa) const {
        auto i = mMemoryMappings.lower_bound(gpa); // i->first >= gpa (or i==end)
        if ((i != mMemoryMappings.end()) && (i->first == gpa)) {
            return i->second.first;  // gpa is exactly the beginning of the range
        } else if (i == mMemoryMappings.begin()) {
            return nullptr;  // can't '--i', see below
        } else {
            --i;

            if ((i->first + i->second.second) > gpa) {
                // move the host ptr by +(gpa-base)
                return static_cast<char *>(i->second.first) + (gpa - i->first);
            } else {
                return nullptr;  // the range does not cover gpa
            }
        }
    }


    uint32_t mHandleIndex = 1;

    mutable std::mutex mContextsMutex;
    std::unordered_map<uint32_t, AddressSpaceContextDescription> mContexts;

    mutable std::mutex mMemoryMappingsLock;
    std::map<uint64_t, std::pair<void *, uint64_t>> mMemoryMappings;  // do not save/load

    struct DeallocationCallbackEntry {
        void* context;
        address_space_device_deallocation_callback_t func;
    };
    std::map<uint64_t, std::vector<DeallocationCallbackEntry>> mDeallocationCallbacks; // do not save/load, users re-register on load

    enum class SaveState {
        Reset,
        PreSaving,
        PreSaved,
        Saving,
        PostSaving,
        PostSaved,
    };

    struct ContextEntrySnapshot {
        uint32_t handle = 0;
        uint64_t pingInfoGpa = 0;
        std::shared_ptr<AddressSpaceDeviceContext> context;
    };

    struct SnapshotContextEntry {
        uint32_t handle = 0;
        uint64_t pingInfoGpa = 0;
        bool hadContext = false;
        std::weak_ptr<AddressSpaceDeviceContext> weakContext;
    };

    struct ActiveSnapshot {
        uint32_t handleIndex = 0;
        std::vector<SnapshotContextEntry> entries;
    };

    static const char* toString(SaveState state) {
        switch (state) {
            case SaveState::Reset:
                return "Reset";
            case SaveState::PreSaving:
                return "PreSaving";
            case SaveState::PreSaved:
                return "PreSaved";
            case SaveState::Saving:
                return "Saving";
            case SaveState::PostSaving:
                return "PostSaving";
            case SaveState::PostSaved:
                return "PostSaved";
        }
        return "Unknown";
    }

    void snapshotContextsLocked(
            uint32_t* outHandleIndex,
            std::vector<ContextEntrySnapshot>* outSnapshot) const {
        if (outHandleIndex) {
            *outHandleIndex = mHandleIndex;
        }
        outSnapshot->clear();
        outSnapshot->reserve(mContexts.size());
        for (const auto& [handle, desc] : mContexts) {
            outSnapshot->push_back({handle, desc.pingInfoGpa, desc.device_context});
        }
    }

    bool beginPreSave(
            uint32_t* outHandleIndex,
            std::vector<ContextEntrySnapshot>* outContexts) const {
        std::unique_lock<std::mutex> lock(mContextsMutex);
        mSnapshotCv.wait(lock, [this] {
            return mCreatingContextsCount == 0 && mDestroyingContextsCount == 0 &&
                   !mClearInProgress;
        });
        if (mSaveState != SaveState::Reset && mSaveState != SaveState::PostSaved) {
            GFXSTREAM_ERROR("Invalid ASG save state %s in preSave(); expected Reset or PostSaved.",
                            toString(mSaveState));
            return false;
        }
        mSaveState = SaveState::PreSaving;
        ++mActiveSnapshotOps;
        snapshotContextsLocked(outHandleIndex, outContexts);
        return true;
    }

    bool beginPostSave(std::shared_ptr<const ActiveSnapshot>* outActive) const {
        std::unique_lock<std::mutex> lock(mContextsMutex);
        mSnapshotCv.wait(lock, [this] {
            return mCreatingContextsCount == 0 && mDestroyingContextsCount == 0 &&
                   !mClearInProgress;
        });
        if (mSaveState != SaveState::PreSaved) {
            GFXSTREAM_ERROR("Invalid ASG save state %s in postSave(); expected PreSaved.",
                            toString(mSaveState));
            return false;
        }
        mSaveState = SaveState::PostSaving;
        ++mActiveSnapshotOps;
        *outActive = std::exchange(mActiveSnapshot, nullptr);
        return true;
    }

    bool beginSave(
            bool* outManagePrePostSave,
            uint32_t* outHandleIndex,
            std::vector<ContextEntrySnapshot>* outInitialContexts,
            std::shared_ptr<const ActiveSnapshot>* outActive) const {
        std::unique_lock<std::mutex> lock(mContextsMutex);
        mSnapshotCv.wait(lock, [this] {
            return mCreatingContextsCount == 0 && mDestroyingContextsCount == 0 &&
                   !mClearInProgress;
        });
        if (mSaveState == SaveState::PreSaved) {
            if (!mActiveSnapshot) {
                GFXSTREAM_ERROR("Missing active ASG context snapshot in save().");
                mSaveState = SaveState::Reset;
                mSnapshotCv.notify_all();
                return false;
            }
            *outManagePrePostSave = false;
            mSaveState = SaveState::Saving;
            ++mActiveSnapshotOps;
            *outActive = mActiveSnapshot;
            return true;
        }
        if (mSaveState == SaveState::Reset || mSaveState == SaveState::PostSaved) {
            *outManagePrePostSave = true;
            mSaveState = SaveState::PreSaving;
            ++mActiveSnapshotOps;
            snapshotContextsLocked(outHandleIndex, outInitialContexts);
            return true;
        }
        GFXSTREAM_ERROR("Invalid ASG save state %s in save().", toString(mSaveState));
        return false;
    }

    void setSaveStateLocked(SaveState nextState) const {
        std::lock_guard<std::mutex> lock(mContextsMutex);
        mSaveState = nextState;
    }

    void endSnapshotOp(
            SaveState nextState,
            bool updateSnapshot,
            std::shared_ptr<const ActiveSnapshot> nextSnapshot) const {
        std::lock_guard<std::mutex> lock(mContextsMutex);
        mSaveState = nextState;
        if (updateSnapshot) {
            mActiveSnapshot = std::move(nextSnapshot);
        }
        --mActiveSnapshotOps;
        if (mActiveSnapshotOps == 0) {
            mSnapshotCv.notify_all();
        }
    }

    void finishContextDestruction(
            std::shared_ptr<AddressSpaceDeviceContext> context) {
        if (!context) {
            return;
        }
        // Destroy `context` synchronously outside `mContextsMutex` before returning
        // so callers never free external RingBlob memory while RenderThread is alive.
        context.reset();
        std::lock_guard<std::mutex> lock(mContextsMutex);
        --mDestroyingContextsCount;
        if (mCreatingContextsCount == 0 && mDestroyingContextsCount == 0) {
            mSnapshotCv.notify_all();
        }
    }

    struct ClearGuard {
        AddressSpaceDeviceState* state = nullptr;
        explicit ClearGuard(AddressSpaceDeviceState* s) : state(s) {}
        ClearGuard(const ClearGuard&) = delete;
        ClearGuard& operator=(const ClearGuard&) = delete;
        ~ClearGuard() {
            if (state) {
                AddressSpaceGraphicsContext::clear();
                state->finishClear();
            }
        }
        void release() { state = nullptr; }
    };

    bool loadSingleContextEntry(
            Stream* stream,
            std::unordered_map<uint32_t, AddressSpaceContextDescription>* contexts) {
        const uint32_t handle = stream->getBe32();
        const uint64_t pingInfoGpa = stream->getBe64();
        const uint8_t hasContext = stream->getByte();
        if (stream->hasErrors()) {
            return false;
        }

        std::unique_ptr<AddressSpaceDeviceContext> context;
        if (hasContext == 1) {
            struct AddressSpaceCreateInfo create = {};
            create.type = static_cast<uint32_t>(stream->getBe32());
            create.physAddr = pingInfoGpa;
            create.fromSnapshot = true;
            if (stream->hasErrors()) {
                return false;
            }

            context = buildAddressSpaceDeviceContext(create);
            if (!context || !context->load(stream) || stream->hasErrors()) {
                return false;
            }
        } else if (hasContext != 0) {
            return false;
        }

        auto& desc = (*contexts)[handle];
        desc.pingInfoGpa = pingInfoGpa;
        if (desc.pingInfoGpa == ~0ULL) {
            GFXSTREAM_WARNING("Restoring hva-only ping.");
        } else {
            desc.pingInfo = nullptr;
        }
        desc.device_context = std::move(context);
        return true;
    }

    void beginClear() {
        std::shared_ptr<const ActiveSnapshot> snapshotToResume;
        std::vector<std::shared_ptr<AddressSpaceDeviceContext>> contextsToDestroy;
        detachAllForClear(&snapshotToResume, &contextsToDestroy);
        if (snapshotToResume) {
            resumeSnapshot(snapshotToResume.get());
            snapshotToResume.reset();
        }
        // Destroy contexts outside mContextsMutex after resuming parked snapshots.
        contextsToDestroy.clear();
        clearRemainingMappings();
    }

    void installLoadedContextsAndFinishClear(
            uint32_t handleIndex,
            std::unordered_map<uint32_t, AddressSpaceContextDescription> newContexts) {
        std::lock_guard<std::mutex> lock(mContextsMutex);
        mHandleIndex = handleIndex;
        mContexts = std::move(newContexts);
        mSaveState = SaveState::Reset;
        mActiveSnapshot.reset();
        mClearInProgress = false;
        mSnapshotCv.notify_all();
    }

    static std::vector<ContextEntrySnapshot> lockSnapshotEntries(
            const std::vector<SnapshotContextEntry>& entries) {
        std::vector<ContextEntrySnapshot> locked;
        locked.reserve(entries.size());
        for (const auto& entry : entries) {
            if (!entry.hadContext) {
                locked.push_back({entry.handle, entry.pingInfoGpa, nullptr});
                continue;
            }
            if (auto ctx = entry.weakContext.lock()) {
                locked.push_back({entry.handle, entry.pingInfoGpa, std::move(ctx)});
            }
        }
        return locked;
    }

    static void writeSnapshotStream(Stream* stream, const ActiveSnapshot& active) {
        const std::vector<ContextEntrySnapshot> contexts =
            lockSnapshotEntries(active.entries);
        std::unordered_set<uint32_t> activeHandles;
        activeHandles.reserve(contexts.size());
        for (const auto& entry : contexts) {
            if (entry.context) {
                activeHandles.insert(entry.handle);
            }
        }

        AddressSpaceGraphicsContext::globalStateSave(stream, &activeHandles);

        stream->putBe32(active.handleIndex);
        stream->putBe32(contexts.size());

        for (const auto& entry : contexts) {
            stream->putBe32(entry.handle);
            stream->putBe64(entry.pingInfoGpa);

            if (!entry.context) {
                stream->putByte(0);
                continue;
            }
            stream->putByte(1);
            stream->putBe32(static_cast<uint32_t>(entry.context->getDeviceType()));
            entry.context->save(stream);
        }
    }

    static bool resumeSnapshot(const ActiveSnapshot* snapshot) {
        bool ok = true;
        if (!AddressSpaceGraphicsContext::globalStatePostSave()) {
            GFXSTREAM_ERROR("Failed to post-save global ASG state.");
            ok = false;
        }
        if (!snapshot) {
            return ok;
        }
        for (const auto& entry : snapshot->entries) {
            auto ctx = entry.weakContext.lock();
            if (ctx && !ctx->postSave()) {
                GFXSTREAM_ERROR("Failed to post-save ASG context %u.", entry.handle);
                ok = false;
            }
        }
        return ok;
    }

    void detachAllForClear(
            std::shared_ptr<const ActiveSnapshot>* outSnapshot,
            std::vector<std::shared_ptr<AddressSpaceDeviceContext>>* outContexts) {
        std::unique_lock<std::mutex> lock(mContextsMutex);
        mSnapshotCv.wait(lock, [this] {
            return mActiveSnapshotOps == 0 && mCreatingContextsCount == 0 &&
                   mDestroyingContextsCount == 0 && !mClearInProgress;
        });
        mClearInProgress = true;
        if (mSaveState != SaveState::Reset && mSaveState != SaveState::PostSaved) {
            GFXSTREAM_ERROR("Clearing ASG device state while in %s state.",
                            toString(mSaveState));
        }
        mSaveState = SaveState::Reset;
        *outSnapshot = std::exchange(mActiveSnapshot, nullptr);
        outContexts->reserve(mContexts.size());
        for (auto& kv : mContexts) {
            if (kv.second.device_context) {
                outContexts->push_back(std::move(kv.second.device_context));
            }
        }
        mContexts.clear();
    }

    void clearRemainingMappings() {
        std::lock_guard<std::mutex> mappingsLock(mMemoryMappingsLock);
        std::vector<std::pair<uint64_t, uint64_t>> gpasSizesToErase;
        gpasSizesToErase.reserve(mMemoryMappings.size());
        for (const auto& mapping : mMemoryMappings) {
            gpasSizesToErase.push_back({mapping.first, mapping.second.second});
        }
        for (const auto& gpaSize : gpasSizesToErase) {
            removeMemoryMappingLocked(gpaSize.first, gpaSize.second);
        }
        mMemoryMappings.clear();
    }

    void finishClear() {
        std::lock_guard<std::mutex> lock(mContextsMutex);
        mClearInProgress = false;
        mSnapshotCv.notify_all();
    }

    bool runPreSave(
            uint32_t handleIndex,
            const std::vector<ContextEntrySnapshot>& contexts,
            std::shared_ptr<const ActiveSnapshot>* outActive) const {
        auto active = std::make_shared<ActiveSnapshot>();
        active->handleIndex = handleIndex;
        active->entries.reserve(contexts.size());

        std::vector<ContextEntrySnapshot> preSavedContexts;
        preSavedContexts.reserve(contexts.size());
        for (const auto& entry : contexts) {
            if (!entry.context) {
                active->entries.push_back({entry.handle, entry.pingInfoGpa, false, {}});
                continue;
            }
            if (!entry.context->preSave()) {
                GFXSTREAM_ERROR("Failed to pre-save ASG context %u.", entry.handle);
                abortPreSaveContexts(preSavedContexts, /*pausedCount=*/0,
                                     /*rollbackGlobal=*/false);
                return false;
            }
            preSavedContexts.push_back(entry);
            active->entries.push_back(
                {entry.handle, entry.pingInfoGpa, true, entry.context});
        }

        if (!AddressSpaceGraphicsContext::globalStatePreSave()) {
            GFXSTREAM_ERROR("Failed to pre-save global ASG state.");
            abortPreSaveContexts(preSavedContexts, /*pausedCount=*/0,
                                 /*rollbackGlobal=*/false);
            return false;
        }

        for (size_t i = 0; i < preSavedContexts.size(); ++i) {
            if (!preSavedContexts[i].context->waitForPreSave()) {
                GFXSTREAM_ERROR("Failed waiting for pre-save on ASG context %u.",
                                preSavedContexts[i].handle);
                abortPreSaveContexts(preSavedContexts, /*pausedCount=*/i,
                                     /*rollbackGlobal=*/true);
                return false;
            }
        }

        *outActive = std::move(active);
        return true;
    }

    static void abortPreSaveContexts(
            const std::vector<ContextEntrySnapshot>& preSavedContexts,
            size_t pausedCount,
            bool rollbackGlobal) {
        if (rollbackGlobal) {
            AddressSpaceGraphicsContext::globalStatePostSave();
        }
        for (size_t i = 0; i < preSavedContexts.size(); ++i) {
            if (i < pausedCount) {
                preSavedContexts[i].context->postSave();
            } else {
                preSavedContexts[i].context->cancelPreSave();
            }
        }
    }

    // Not saved/loaded. Externally owned resources used during load.
    std::optional<AddressSpaceDeviceLoadResources> mLoadResources;
    mutable std::condition_variable mSnapshotCv;
    mutable std::shared_ptr<const ActiveSnapshot> mActiveSnapshot;
    mutable SaveState mSaveState = SaveState::Reset;
    mutable uint32_t mActiveSnapshotOps = 0;
    mutable uint32_t mCreatingContextsCount = 0;
    mutable uint32_t mDestroyingContextsCount = 0;
    mutable bool mClearInProgress = false;
};

static AddressSpaceDeviceState* sAddressSpaceDeviceState() {
    static AddressSpaceDeviceState* s = new AddressSpaceDeviceState;
    return s;
}

static uint32_t sAddressSpaceDeviceGenHandle() {
    return sAddressSpaceDeviceState()->genHandle();
}

static void sAddressSpaceDeviceDestroyHandle(uint32_t handle) {
    sAddressSpaceDeviceState()->destroyHandle(handle);
}

static void sAddressSpaceDeviceCreateInstance(const struct AddressSpaceCreateInfo& create) {
    sAddressSpaceDeviceState()->createInstance(create);
}

static void sAddressSpaceDeviceTellPingInfo(uint32_t handle, uint64_t gpa) {
    GFXSTREAM_FATAL("Unhandled.");
}

static void sAddressSpaceDevicePing(uint32_t handle) {
    sAddressSpaceDeviceState()->ping(handle);
}

static int sAddressSpaceDeviceAddMemoryMapping(uint64_t gpa, void *ptr, uint64_t size) {
    return sAddressSpaceDeviceState()->addMemoryMapping(gpa, ptr, size) ? 1 : 0;
}

static int sAddressSpaceDeviceRemoveMemoryMapping(uint64_t gpa, void*, uint64_t size) {
    return sAddressSpaceDeviceState()->removeMemoryMapping(gpa, size) ? 1 : 0;
}

void* sAddressSpaceDeviceGetHostPtr(uint64_t gpa) {
    return sAddressSpaceDeviceState()->getHostPtr(gpa);
}

static void* sAddressSpaceHandleToContext(uint32_t handle) {
    return (void*)(sAddressSpaceDeviceState()->handleToContext(handle));
}

static void sAddressSpaceDeviceClear() {
    sAddressSpaceDeviceState()->clear();
}

static uint64_t sAddressSpaceDeviceHostmemRegister(const struct MemEntry *entry) {
    GFXSTREAM_FATAL("Unexpected call to hostmem register.");
    return -1;
}

static void sAddressSpaceDeviceHostmemUnregister(uint64_t id) {
    GFXSTREAM_FATAL("Unexpected call to hostmem unregister.");
}

static void sAddressSpaceDevicePingAtHva(uint32_t handle, void* hva) {
    sAddressSpaceDeviceState()->pingAtHva(handle, (AddressSpaceDevicePingInfo*)hva);
}

static void sAddressSpaceDeviceRegisterDeallocationCallback(
    void* context, uint64_t gpa, address_space_device_deallocation_callback_t func) {
    sAddressSpaceDeviceState()->registerDeallocationCallback(gpa, context, func);
}

static void sAddressSpaceDeviceRunDeallocationCallbacks(uint64_t gpa) {
    sAddressSpaceDeviceState()->runDeallocationCallbacks(gpa);
}

static const struct AddressSpaceHwFuncs* sAddressSpaceDeviceControlGetHwFuncs() {
    return nullptr;
}

address_space_device_control_ops GetAsgOperations() {
    return address_space_device_control_ops{
        &sAddressSpaceDeviceGenHandle,                     // gen_handle
        &sAddressSpaceDeviceDestroyHandle,                 // destroy_handle
        &sAddressSpaceDeviceTellPingInfo,                  // tell_ping_info
        &sAddressSpaceDevicePing,                          // ping
        &sAddressSpaceDeviceAddMemoryMapping,              // add_memory_mapping
        &sAddressSpaceDeviceRemoveMemoryMapping,           // remove_memory_mapping
        &sAddressSpaceDeviceGetHostPtr,                    // get_host_ptr
        &sAddressSpaceHandleToContext,                     // handle_to_context
        &sAddressSpaceDeviceClear,                         // clear
        &sAddressSpaceDeviceHostmemRegister,               // hostmem register
        &sAddressSpaceDeviceHostmemUnregister,             // hostmem unregister
        &sAddressSpaceDevicePingAtHva,                     // ping_at_hva
        &sAddressSpaceDeviceRegisterDeallocationCallback,  // register_deallocation_callback
        &sAddressSpaceDeviceRunDeallocationCallbacks,      // run_deallocation_callbacks
        &sAddressSpaceDeviceControlGetHwFuncs,             // control_get_hw_funcs
        &sAddressSpaceDeviceCreateInstance,                // create_instance
    };
};

int gfxstream_address_space_set_load_resources(AddressSpaceDeviceLoadResources resources) {
    sAddressSpaceDeviceState()->setLoadResources(std::move(resources));
    return 0;
}

int gfxstream_address_space_pre_save() {
    return sAddressSpaceDeviceState()->preSave() ? 0 : -1;
}

int gfxstream_address_space_save_memory_state(gfxstream::Stream *stream) {
    return sAddressSpaceDeviceState()->save(stream) ? 0 : -1;
}

int gfxstream_address_space_post_save() {
    return sAddressSpaceDeviceState()->postSave() ? 0 : -1;
}

int gfxstream_address_space_load_memory_state(gfxstream::Stream *stream) {
    return sAddressSpaceDeviceState()->load(stream) ? 0 : 1;
}

}  // namespace host
}  // namespace gfxstream

