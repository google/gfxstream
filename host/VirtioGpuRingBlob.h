// Copyright (C) 2024 The Android Open Source Project
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

#include <memory>
#include <variant>

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include "VirtioGpuResourceSnapshot.pb.h"
#endif
#include "gfxstream/AlignedBuf.h"
#include "gfxstream/memory/SharedMemory.h"

namespace gfxstream {
namespace host {

// LINT.IfChange(virtio_gpu_ring_blob)

struct AlignedMemory {
    void* addr = nullptr;

    AlignedMemory(size_t align, size_t size) : addr(gfxstream::aligned_buf_alloc(align, size)) {}

    ~AlignedMemory() {
        if (addr != nullptr) {
            gfxstream::aligned_buf_free(addr);
        }
    }

    // AlignedMemory is neither copyable nor movable.
    AlignedMemory(const AlignedMemory& other) = delete;
    AlignedMemory& operator=(const AlignedMemory& other) = delete;
    AlignedMemory(AlignedMemory&& other) = delete;
    AlignedMemory& operator=(AlignedMemory&& other) = delete;
};

// Memory used as a ring buffer for communication between the guest and host.
class RingBlob {
   public:
    static std::unique_ptr<RingBlob> CreateWithShmem(uint32_t id, uint64_t size);
    static std::unique_ptr<RingBlob> CreateWithHostMemory(uint32_t, uint64_t size, uint64_t alignment);

    bool isExportable() const;

    // Only valid if `isExportable()` returns `true`.
    gfxstream::base::SharedMemory::handle_type releaseHandle();

    void* map();

    uint64_t size() const { return mSize; }

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    std::optional<gfxstream::host::snapshot::VirtioGpuRingBlobSnapshot> Snapshot();

    static std::optional<std::unique_ptr<RingBlob>> Restore(
        const gfxstream::host::snapshot::VirtioGpuRingBlobSnapshot& snapshot);
#endif

  private:
    RingBlob(uint32_t id,
             uint64_t size,
             uint64_t alignment,
             std::variant<std::unique_ptr<AlignedMemory>,
                          std::unique_ptr<gfxstream::base::SharedMemory>> memory);

    const uint64_t mId;
    const uint64_t mSize;
    const uint64_t mAlignment;
    std::variant<std::unique_ptr<AlignedMemory>,
                 std::unique_ptr<gfxstream::base::SharedMemory>> mMemory;
};

// LINT.ThenChange(VirtioGpuRingBlobSnapshot.h:virtio_gpu_ring_blob)

}  // namespace host
}  // namespace gfxstream
