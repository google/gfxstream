// Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include <stdlib.h>
#include <unordered_map>

#include "gfxstream/containers/Lookup.h"
#include "gfxstream/containers/EntityManager.h"
#include "gfxstream/synchronization/Lock.h"

namespace gfxstream {
namespace base {

template <size_t maxIndex,
          class IndexType, // must be castable to uint64_t
          class Data>
class HybridEntityManager {
public:
    using EM = EntityManager<32, 16, 16, Data>;
    using IterFunc = typename EM::IteratorFunc;
    using ConstIterFunc = typename EM::ConstIteratorFunc;
    using Handle = typename EM::EntityHandle;

    HybridEntityManager() : mEntityManager(maxIndex + 1) { }
    ~HybridEntityManager() { clear(); }

    uint64_t add(Data data, size_t type) {
        uint64_t nextIndex = 0;
        {
            AutoWriteLock wl(mEntityManagerLock);
            nextIndex = (uint64_t)mEntityManager.nextFreeIndex();
            if (nextIndex < maxIndex) {
                uint64_t resultHandle = mEntityManager.add(std::move(data), type);
                if (EM::getHandleIndex(resultHandle) != nextIndex) {
                    fprintf(stderr, "%s: fatal: handle indices mismatch. wanted 0x%llx got 0x%llx\n", __func__,
                            (unsigned long long)nextIndex,
                            (unsigned long long)EM::getHandleIndex(resultHandle));
                    abort();
                }
                return resultHandle;
            }
        }

        AutoWriteLock wl(mMapLock);
        if (mIndexForMap == 0) {
            mIndexForMap = maxIndex;
        }
        nextIndex = mIndexForMap;
        auto emplaced = mMap.emplace(nextIndex, std::move(data));
        if (!emplaced.second) {
            fprintf(stderr, "%s: fatal: trying to insert duplicated entry 0x%llx\n", __func__,
                    (unsigned long long)nextIndex);
            abort();
        }
        ++mIndexForMap;
        return EM::makeHandle(nextIndex, 1, type);
    }

    uint64_t addFixed(IndexType index, Data data, size_t type) {
        uint64_t index_u64 = (uint64_t)EM::getHandleIndex(index);
        if (index_u64 < maxIndex) {
            AutoWriteLock wl(mEntityManagerLock);
            return mEntityManager.addFixed(index, std::move(data), type);
        } else {
            AutoWriteLock wl(mMapLock);
            // Fixed allocations require us to update mIndexForMap to catch up with it.
            mIndexForMap = std::max(index_u64 + 1, mIndexForMap);
            auto emplaced = mMap.emplace(index_u64, std::move(data));
            if (!emplaced.second) {
                fprintf(stderr, "%s: fatal: trying to insert duplicated entry 0x%llx\n", __func__,
                        (unsigned long long)index_u64);
                abort();
            }
            return index;
        }
    }

    void clear() {
        {
            AutoWriteLock wl(mEntityManagerLock);
            mEntityManager.clear();
        }
        {
            AutoWriteLock wl(mMapLock);
            mMap.clear();
            mIndexForMap = 0;
        }
    }

    void remove(IndexType index) {
        uint64_t index_u64 = (uint64_t)EM::getHandleIndex(index);
        if (index_u64 < maxIndex) {
            AutoWriteLock wl(mEntityManagerLock);
            mEntityManager.remove(index);
        } else {
            AutoWriteLock wl(mMapLock);
            mMap.erase(index_u64);
        }
    }

    Data* get(IndexType index) {
        uint64_t index_u64 = (uint64_t)EM::getHandleIndex(index);
        if (index_u64 < maxIndex) {
            AutoWriteLock wl(mEntityManagerLock);
            return mEntityManager.get(index);
        } else {
            AutoWriteLock wl(mMapLock);
            auto res = gfxstream::base::find(mMap, index_u64);
            return res;
        }
    }

    const Data* get_const(IndexType index) const {
        uint64_t index_u64 = (uint64_t)EM::getHandleIndex(index);
        if (index_u64 < maxIndex) {
            AutoReadLock rl(mEntityManagerLock);
            return mEntityManager.get_const(index);
        } else {
            AutoReadLock rl(mMapLock);
            auto res = gfxstream::base::find(mMap, index_u64);
            return res;
        }
    }

    // Calls |func| with a pointer to the data at |index|, or nullptr if there is
    // no live entry, and returns its result. Unlike get() and get_const(), the
    // lock serializing add(), remove() and clear() is held while |func| runs, so
    // the entry can not be modified or destroyed concurrently. |func| must not
    // call back into this HybridEntityManager.
    template <typename Func>
    auto withLocked(IndexType index, Func&& func) {
        uint64_t index_u64 = (uint64_t)EM::getHandleIndex(index);
        if (index_u64 < maxIndex) {
            AutoWriteLock wl(mEntityManagerLock);
            return func(mEntityManager.get(index));
        } else {
            AutoWriteLock wl(mMapLock);
            return func(gfxstream::base::find(mMap, index_u64));
        }
    }

    // Calls |func| with a const pointer to the data at |index|, or nullptr if
    // there is no live entry, while holding a shared read lock. Concurrent
    // readers do not block each other, while add(), remove(), clear(), and
    // withLocked() are serialized. |func| must not call back into this
    // HybridEntityManager.
    template <typename Func>
    auto withSharedLocked(IndexType index, Func&& func) const {
        uint64_t index_u64 = (uint64_t)EM::getHandleIndex(index);
        if (index_u64 < maxIndex) {
            AutoReadLock rl(mEntityManagerLock);
            return func(mEntityManager.get_const(index));
        } else {
            AutoReadLock rl(mMapLock);
            return func(gfxstream::base::find(mMap, index_u64));
        }
    }

    Data* getExceptZero(IndexType index) {
        Data* res = get(index);
        if (!res) return nullptr;
        if (!(*res)) return nullptr;
        return res;
    }

    const Data* getExceptZero_const(IndexType index) const {
        const Data* res = get_const(index);
        if (!res) return nullptr;
        if (!(*res)) return nullptr;
        return res;
    }

    void forEach(IterFunc func) {
        {
            AutoWriteLock wl(mEntityManagerLock);
            mEntityManager.forEach(func);
        }

        AutoWriteLock wl(mMapLock);
        for (auto& it : mMap) {
            auto handle = index2Handle(it.first);
            func(true /* live */, handle, handle, it.second);
        }
    }

    void forEachLive(IterFunc func) {
        {
            AutoWriteLock wl(mEntityManagerLock);
            mEntityManager.forEachLiveEntry(func);
        }

        AutoWriteLock wl(mMapLock);
        for (auto& it : mMap) {
            auto handle = index2Handle(it.first);
            func(true /* live */, handle, it.second);
        }
    }

    void forEachLive_const(ConstIterFunc func) const {
        {
            AutoReadLock rl(mEntityManagerLock);
            mEntityManager.forEachLiveEntry_const(func);
        }

        AutoReadLock rl(mMapLock);
        for (const auto& it : mMap) {
            auto handle = index2Handle(it.first);
            func(true /* live */, handle, it.second);
        }
    }

private:
    static Handle index2Handle(uint64_t index) {
        return EM::makeHandle((uint32_t)index, 1, 1);
    }

    EM mEntityManager;
    std::unordered_map<IndexType, Data> mMap;
    uint64_t mIndexForMap = 0;
    mutable ReadWriteLock mEntityManagerLock;
    mutable ReadWriteLock mMapLock;
};

} // namespace gfxstream
} // namespace base
