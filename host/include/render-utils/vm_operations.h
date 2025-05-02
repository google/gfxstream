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

#pragma once

#include <cstdint>

// Map a given host virtual address into a given guest physical address.
typedef void (*gfxstream_vm_map_user_memory_t)(uint64_t gpa, void* hva, uint64_t size);

// Unmap a given guest physical address.
typedef void (*gfxstream_vm_unmap_user_memory_t)(uint64_t gpa, uint64_t size);

// Lookup if a given guest physical address is associated with a host virtual
// address.
typedef void* (*gfxstream_vm_lookup_user_memory_t)(uint64_t gpa);

typedef void (*gfxstream_vm_register_vulkan_instance_t)(uint64_t id, const char* name);
typedef void (*gfxstream_vm_unregister_vulkan_instance_t)(uint64_t id);

typedef enum GfxstreamSnapshotSkipReason {
    GFXSTREAM_SNAPSHOT_SKIP_REASON_UNKNOWN = 0,
    GFXSTREAM_SNAPSHOT_SKIP_REASON_UNSUPPORTED_VK_APP = 1,
    GFXSTREAM_SNAPSHOT_SKIP_REASON_UNSUPPORTED_VK_API = 2,
} GfxstreamSnapshotSkipReason;
typedef void (*gfxstream_vm_set_skip_snapshot_save_t)(bool used);
typedef void (*gfxstream_vm_set_skip_snapshot_save_reason_t)(uint32_t reason);
typedef void (*gfxstream_vm_set_snapshot_uses_vulkan_t)(void);

typedef struct gfxstream_vm_ops {
    gfxstream_vm_map_user_memory_t map_user_memory;
    gfxstream_vm_unmap_user_memory_t unmap_user_memory;
    gfxstream_vm_unmap_user_memory_t unmap_user_memory_async;
    gfxstream_vm_lookup_user_memory_t lookup_user_memory;

    gfxstream_vm_register_vulkan_instance_t register_vulkan_instance;
    gfxstream_vm_unregister_vulkan_instance_t unregister_vulkan_instance;

    gfxstream_vm_set_skip_snapshot_save_t set_skip_snapshot_save;
    gfxstream_vm_set_skip_snapshot_save_reason_t set_skip_snapshot_save_reason;
    gfxstream_vm_set_snapshot_uses_vulkan_t set_snapshot_uses_vulkan;
} gfxstream_vm_ops;
