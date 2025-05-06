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

#include <inttypes.h>

extern "C" {

typedef enum AddressSpaceContextType {
    ADDRESS_SPACE_CONTEXT_TYPE_GRAPHICS = 0,
    ADDRESS_SPACE_CONTEXT_TYPE_MEDIA = 1,
    ADDRESS_SPACE_CONTEXT_TYPE_SENSORS = 2,
    ADDRESS_SPACE_CONTEXT_TYPE_POWER = 3,
    ADDRESS_SPACE_CONTEXT_TYPE_GENERIC_PIPE = 4,
    ADDRESS_SPACE_CONTEXT_TYPE_HOST_MEMORY_ALLOCATOR = 5,
    ADDRESS_SPACE_CONTEXT_TYPE_SHARED_SLOTS_HOST_MEMORY_ALLOCATOR = 6,
    ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS = 10,
} AddressSpaceContextType;

struct AddressSpaceHwFuncs;

struct AddressSpaceCreateInfo {
    uint32_t handle = 0;
    uint32_t type;
    uint64_t physAddr;
    bool fromSnapshot;
    bool createRenderThread;
    void *externalAddr;
    uint64_t externalAddrSize;
    uint32_t virtioGpuContextId;
    uint32_t virtioGpuCapsetId;
    const char *contextName;
    uint32_t contextNameSize;
};

typedef uint32_t (*address_space_device_gen_handle_t)(void);
typedef void (*address_space_device_destroy_handle_t)(uint32_t);
typedef void (*address_space_device_create_instance_t)(const struct AddressSpaceCreateInfo& create);
typedef void (*address_space_device_tell_ping_info_t)(uint32_t handle, uint64_t gpa);
typedef void (*address_space_device_ping_t)(uint32_t handle);
typedef int (*address_space_device_add_memory_mapping_t)(uint64_t gpa, void *ptr, uint64_t size);
typedef int (*address_space_device_remove_memory_mapping_t)(uint64_t gpa, void *ptr, uint64_t size);
typedef void* (*address_space_device_get_host_ptr_t)(uint64_t gpa);
typedef void* (*address_space_device_handle_to_context_t)(uint32_t handle);
typedef void (*address_space_device_clear_t)(void);
// virtio-gpu-next
typedef uint64_t (*address_space_device_hostmem_register_t)(const struct MemEntry *entry);
typedef void (*address_space_device_hostmem_unregister_t)(uint64_t id);

struct AddressSpaceDevicePingInfo {
    uint64_t phys_addr = 0;
    uint64_t size = 0;
    uint64_t metadata = 0;
    uint64_t wait_phys_addr = 0;
    uint32_t wait_flags = 0;
    uint32_t direction = 0;
};
typedef void (*address_space_device_ping_at_hva_t)(uint32_t handle, void* hva);
// deallocation callbacks
typedef void (*address_space_device_deallocation_callback_t)(void* context, uint64_t gpa);
typedef void (*address_space_device_register_deallocation_callback_t)(void* context, uint64_t gpa, address_space_device_deallocation_callback_t);
typedef void (*address_space_device_run_deallocation_callbacks_t)(uint64_t gpa);


typedef struct AddressSpaceHwFuncs {
    /* Called by the host to reserve a shared region. Guest users can then
     * suballocate into this region. This saves us a lot of KVM slots.
     * Returns the relative offset to the starting phys addr in |offset|
     * and returns 0 if successful, -errno otherwise. */
    int (*allocSharedHostRegion)(uint64_t page_aligned_size, uint64_t* offset);
    /* Called by the host to free a shared region. Only useful on teardown
     * or when loading a snapshot while the emulator is running.
     * Returns 0 if successful, -errno otherwise. */
    int (*freeSharedHostRegion)(uint64_t offset);

    /* Versions of the above but when the state is already locked. */
    int (*allocSharedHostRegionLocked)(uint64_t page_aligned_size, uint64_t* offset);
    int (*freeSharedHostRegionLocked)(uint64_t offset);

    /* Obtains the starting physical address for which the resulting offsets
     * are relative to. */
    uint64_t (*getPhysAddrStart)(void);
    uint64_t (*getPhysAddrStartLocked)(void);
    uint32_t (*getGuestPageSize)(void);

    /* Version of allocSharedHostRegionLocked but for a fixed offset */
    int (*allocSharedHostRegionFixedLocked)(uint64_t page_aligned_size, uint64_t offset);
} AddressSpaceHwFuncs;
typedef const AddressSpaceHwFuncs* (*address_space_device_control_get_hw_funcs_t)(void);

typedef struct address_space_device_control_ops {
    address_space_device_gen_handle_t gen_handle;
    address_space_device_destroy_handle_t destroy_handle;
    address_space_device_tell_ping_info_t tell_ping_info;
    address_space_device_ping_t ping;
    address_space_device_add_memory_mapping_t add_memory_mapping;
    address_space_device_remove_memory_mapping_t remove_memory_mapping;
    address_space_device_get_host_ptr_t get_host_ptr;
    address_space_device_handle_to_context_t handle_to_context;
    address_space_device_clear_t clear;
    address_space_device_hostmem_register_t hostmem_register;
    address_space_device_hostmem_unregister_t hostmem_unregister;
    address_space_device_ping_at_hva_t ping_at_hva;
    address_space_device_register_deallocation_callback_t register_deallocation_callback;
    address_space_device_run_deallocation_callbacks_t run_deallocation_callbacks;
    address_space_device_control_get_hw_funcs_t control_get_hw_funcs;
    address_space_device_create_instance_t create_instance;
} address_space_device_control_ops;

// State/config changes may only occur if the ring is empty, or the state
// is transitioning to Error. That way, the host and guest have a chance to
// synchronize on the same state.
//
// Thus far we've established how commands and data are transferred
// to and from the host. Next, let's discuss how AddressSpaceGraphicsContext
// talks to the code that actually does something with the commands
// and sends data back.

// Handled outside in address_space_device.cpp:
//
// Ping(device id): Create the device. On the host, the two rings and
// auxiliary buffer are allocated. The two rings are allocated up front.
// Both the auxiliary buffers and the rings are allocated from blocks of
// rings and auxiliary buffers. New blocks are created if we run out either
// way.
enum asg_command {
    // Ping(get_ring): Returns, in the fields:
    // metadata: offset to give to claimShared and mmap() in the guest
    // size: size to give to claimShared and mmap() in the guest
    ASG_GET_RING = 0,

    // Ping(get_buffer): Returns, in the fields:
    // metadata: offset to give to claimShared and mmap() in the guest
    // size: size to give to claimShared and mmap() in the guest
    ASG_GET_BUFFER = 1,

    // Ping(set_version): Run after the guest reads and negotiates its
    // version of the device with the host. The host now knows the guest's
    // version and can proceed with a protocol that works for both.
    // size (in): the version of the guest
    // size (out): the version of the host
    // After this command runs, the consumer is
    // implicitly created.
    ASG_SET_VERSION = 2,

    // Ping(notiy_available): Wakes up the consumer from sleep so it
    // can read data via toHost
    ASG_NOTIFY_AVAILABLE = 3,

    // Retrieve the config.
    ASG_GET_CONFIG = 4,
};

} // extern "C"
