// Copyright (C) 2025 The Android Open Source Project
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

#include "gfxstream/host/vm_operations.h"

namespace gfxstream {
namespace {

void DefaultMapUserMemory(uint64_t, void*, uint64_t) {}
void DefaultUnmapUserMemory(uint64_t, uint64_t) {}
void* DefaultLookupUserMemory(uint64_t) { return nullptr; }

void DefaultRegisterVulkanInstance(uint64_t, const char*) {}
void DefaultUnregisterVulkanInstance(uint64_t) {}

void DefaultSetSkipSnapshotSave(bool) {}
void DefaultSetSkipSnapshotSaveReason(uint32_t) {}
void DefaultSetSansphotUsesVulkan() {}

gfxstream_vm_ops sGfxstreamVmOps = {
    .map_user_memory = DefaultMapUserMemory,
    .unmap_user_memory = DefaultUnmapUserMemory,
    .lookup_user_memory = DefaultLookupUserMemory,

    .register_vulkan_instance = DefaultRegisterVulkanInstance,
    .unregister_vulkan_instance = DefaultUnregisterVulkanInstance,

    .set_skip_snapshot_save = DefaultSetSkipSnapshotSave,
    .set_skip_snapshot_save_reason = DefaultSetSkipSnapshotSaveReason,
    .set_snapshot_uses_vulkan = DefaultSetSansphotUsesVulkan,
};

}  // namespace

void set_gfxstream_vm_operations(const gfxstream_vm_ops& ops) {
    sGfxstreamVmOps = ops;
}

const gfxstream_vm_ops& get_gfxstream_vm_operations() {
    return sGfxstreamVmOps;
}

}  // namespace gfxstream
