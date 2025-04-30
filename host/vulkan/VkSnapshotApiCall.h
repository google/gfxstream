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

#include <vector>

#include "gfxstream/containers/EntityManager.h"

namespace gfxstream {
namespace vk {

using VkSnapshotApiCallHandle = uint64_t;

struct VkSnapshotApiCallInfo {
    VkSnapshotApiCallHandle handle = -1;

    // Raw packet from VkDecoder.
    std::vector<uint8_t> packet;

    // Book-keeping for which handles were created by this API
    std::vector<uint64_t> createdHandles;

    // Extra boxed handles created for this API call that are not identifiable
    // solely from the API parameters itself. For example, the extra boxed `VkQueue`s
    // that are created during `vkCreateDevice()` can not be identified from the
    // parameters to `vkCreateDevice()`.
    //
    // TODO: remove this and require that all of the `new_boxed_*()` take a
    // `VkSnapshotApiCallInfo` as an argument so the creation order of the boxed
    // handles in `createdHandles` is guaranteed to match the replay order. For now,
    // this relies on careful manual ordering.
    std::vector<uint64_t> extraCreatedHandles;

    void addOrderedBoxedHandlesCreatedByCall(const uint64_t* boxedHandles,
                                             uint32_t boxedHandlesCount) {
        extraCreatedHandles.insert(extraCreatedHandles.end(), boxedHandles,
                                   boxedHandles + boxedHandlesCount);
    }
};

using VkSnapshotApiCallManager = gfxstream::base::EntityManager<32, 16, 16, VkSnapshotApiCallInfo>;

}  // namespace vk
}  // namespace gfxstream