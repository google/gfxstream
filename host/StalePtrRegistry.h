/*
* Copyright (C) 2017 The Android Open Source Project
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
#pragma once

#include "gfxstream/containers/Lookup.h"
#include "render-utils/stream.h"
#include "gfxstream/host/stream_utils.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/Compiler.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace gfxstream {

// The purpose of StalePtrRegistry is to track integers corresponding to
// host-side pointers that may be invalidated after snapshots.
template <class T>
class StalePtrRegistry {
public:
    StalePtrRegistry() = default;

    void addPtr(T* ptr) {
        gfxstream::base::AutoWriteLock lock(mLock);
        mPtrs[asHandle(ptr)] = { ptr, Staleness::Live };
    }

    void removePtr(T* ptr) {
        gfxstream::base::AutoWriteLock lock(mLock);
        uint64_t handle = asHandle(ptr);
        mPtrs.erase(handle);
    }

    void remapStalePtr(uint64_t handle, T* newptr) {
        gfxstream::base::AutoWriteLock lock(mLock);
        mPtrs[handle] = { newptr, Staleness::PrevSnapshot };
    }

    T* getPtr(uint64_t handle, T* defaultPtr = nullptr,
              bool removeFromStaleOnGet = false) {
        gfxstream::base::AutoReadLock lock(mLock);

        // return |defaultPtr| if not found.
        T* res = defaultPtr;

        Entry* it = nullptr;

        if ((it = gfxstream::base::find(mPtrs, handle)))
            res = it->ptr;

        if (removeFromStaleOnGet &&
            it && it->staleness == Staleness::PrevSnapshot) {
            lock.unlockRead();
            gfxstream::base::AutoWriteLock wrlock(mLock);
            mPtrs.erase(handle);
        }

        return res;
    }

    void makeCurrentPtrsStale() {
        gfxstream::base::AutoWriteLock lock(mLock);
        for (auto& it : mPtrs) {
            it.second.staleness =
                Staleness::PrevSnapshot;
        }
    }

    size_t numCurrEntries() const {
        return countWithStaleness(Staleness::Live);
    }

    size_t numStaleEntries() const {
        return countWithStaleness(Staleness::PrevSnapshot);
    }

    void onSave(gfxstream::Stream* stream) {
        gfxstream::base::AutoReadLock lock(mLock);
        saveCollection(
                stream, mPtrs,
                [](gfxstream::Stream* stream,
                   const std::pair<uint64_t, Entry>& entry) {
                    stream->putBe64(entry.first);
                });
    }

    void onLoad(gfxstream::Stream* stream) {
        gfxstream::base::AutoWriteLock lock(mLock);
        loadCollection(
                stream, &mPtrs,
                [](gfxstream::Stream* stream) {
                    uint64_t handle = stream->getBe64();
                    return std::make_pair(
                               handle,
                               (Entry){ nullptr, Staleness::PrevSnapshot });
                });
    }
private:
    static uint64_t asHandle(const T* ptr) {
        return (uint64_t)(uintptr_t)ptr;
    }

    static T* asPtr(uint64_t handle) {
        return (T*)(uintptr_t)handle;
    }

    enum class Staleness {
        Live,
        PrevSnapshot,
    };
    struct Entry {
        T* ptr;
        Staleness staleness;
    };

    using PtrMap = std::unordered_map<uint64_t, Entry>;

    size_t countWithStaleness(Staleness check) const {
        gfxstream::base::AutoReadLock lock(mLock);
        return std::count_if(mPtrs.begin(), mPtrs.end(),
                   [check](const typename PtrMap::value_type& entry) {
                       return entry.second.staleness == check;
                   });
    }

    mutable gfxstream::base::ReadWriteLock mLock;
    PtrMap mPtrs;

    DISALLOW_COPY_AND_ASSIGN(StalePtrRegistry);
};

}  // namespace gfxstream
