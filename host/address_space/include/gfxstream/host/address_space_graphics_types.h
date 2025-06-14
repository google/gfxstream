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

#include <functional>
#include <optional>

#include "gfxstream/host/ring_buffer.h"
#include "render-utils/address_space_graphics_types.h"

// This file defines common types for address space graphics and provides
// documentation.

// Address space graphics======================================================
//
// Basic idea
//
// Address space graphics (ASG) is a subdevice of the address space device that
// provides a way to run graphics commands and data with fewer VM exits by
// leveraging shared memory ring buffers.
//
// Each GL/Vk thread in the guest is associated with a context (asg_context).
// asg_context consists of pointers into the shared memory that view it as a
// collection of ring buffers and a common write buffer.
//
// Consumer concept
//
// ASG does not assume a particular rendering backend (though we will use
// RenderThread's). This is for ease of coding/testing and flexibility; the
// implementation is not coupled to emugl/libOpenglRender.
//
// Instead, there is the concept of a "Consumer" of ASG that will do something
// with the data arriving from the shared memory region, and possibly reply
// back to the guest. We register functions to construct and deconstruct
// Consumers as part of emulator init (setConsumer).
//
// Guest workflow
//
// 1. Open address space device
//
// 2. Create the graphics context as the subdevice
//
// 3. ping(ASG_GET_RING) to get the offset/size of the ring buffer admin. info
//
// 4. ping(ASG_GET_BUFFER) to get the offset/size of the shared transfer buffer.
//
// 5. ioctl(CLAIM_SHARED) and mmap on those two offset/size pairs to get a
// guest-side mapping.
//
// 6. call asg_context_create on the ring and buffer pointers to create the asg_context.
//
// 7. Now the guest and host share asg_context pts and can communicate.
//
// 8. But usually the guest will sometimes need to ping(ASG_NOTIFY_AVAILABLE)
// so that the host side (which is usually a separate thread that we don't want
// to spin too much) wakes up and processes data.

#define ADDRESS_SPACE_GRAPHICS_DEVICE_ID 0
#define ADDRESS_SPACE_GRAPHICS_PAGE_SIZE 4096
#define ADDRESS_SPACE_GRAPHICS_BLOCK_SIZE (16ULL * 1048576ULL)

// AddressSpaceGraphicsContext shares memory with
// the guest via the following layout:
extern "C" {

struct asg_ring_storage { // directly shared with guest
    char to_host[gfxstream::kAsgPageSize];
    char to_host_large_xfer[gfxstream::kAsgPageSize];
    char from_host_large_xfer[gfxstream::kAsgPageSize];
};

static_assert(gfxstream::kAsgConsumerRingStorageSize == sizeof(struct asg_ring_storage),
              "Ensure these match.");

// Set by the address space graphics device to notify the guest that the host
// has slept or is able to consume something, or we are exiting, or there is an
// error.
enum asg_host_state {
    // The host renderthread is asleep and needs to be woken up.
    ASG_HOST_STATE_NEED_NOTIFY = 0,

    // The host renderthread is active and can consume new data
    // without notification.
    ASG_HOST_STATE_CAN_CONSUME = 1,

    // Normal exit
    ASG_HOST_STATE_EXIT = 2,

    // Error: Something weird happened and we need to exit.
    ASG_HOST_STATE_ERROR = 3,

    // Rendering
    ASG_HOST_STATE_RENDERING = 4,
};

struct asg_ring_config;

// Each context has a pair of ring buffers for communication
// to and from the host. There is another ring buffer for large xfers
// to the host (all xfers from the host are already considered "large").
//
// Each context also comes with _one_ auxiliary buffer to hold both its own
// commands and to perform private DMA transfers.
struct asg_context { // ptrs into RingStorage
    struct ring_buffer* to_host;
    char* buffer;
    asg_host_state* host_state;
    asg_ring_config* ring_config;
    struct ring_buffer_with_view to_host_large_xfer;
    struct ring_buffer_with_view from_host_large_xfer;
};

// Helper function that will be common between guest and host:
// Given ring storage and a write buffer, returns asg_context that
// is the correct view into it.
inline struct asg_context asg_context_create(
    char* ring_storage,
    char* buffer,
    uint32_t buffer_size) {

    struct asg_context res;

    res.to_host =
        reinterpret_cast<struct ring_buffer*>(
            ring_storage +
            offsetof(struct asg_ring_storage, to_host));
    res.to_host_large_xfer.ring =
        reinterpret_cast<struct ring_buffer*>(
            ring_storage +
            offsetof(struct asg_ring_storage, to_host_large_xfer));
    res.from_host_large_xfer.ring =
        reinterpret_cast<struct ring_buffer*>(
            ring_storage +
            offsetof(struct asg_ring_storage, from_host_large_xfer));

    ring_buffer_init(res.to_host);

    res.buffer = buffer;
    res.host_state =
        reinterpret_cast<asg_host_state*>(
            &res.to_host->state);
    res.ring_config =
        reinterpret_cast<asg_ring_config*>(
            res.to_host->config);

    ring_buffer_view_init(
        res.to_host_large_xfer.ring,
        &res.to_host_large_xfer.view,
        (uint8_t*)res.buffer, buffer_size);

    ring_buffer_view_init(
        res.from_host_large_xfer.ring,
        &res.from_host_large_xfer.view,
        (uint8_t*)res.buffer, buffer_size);

    return res;
}

// During operation, the guest sends commands and data over the auxiliary
// buffer while using the |to_host| ring to communicate what parts of the auxiliary
// buffer is outstanding traffic needing to be consumed by the host.
// After a transfer completes to the host, the host may write back data.
// The guest then reads the results on the same auxiliary buffer
// while being notified of which parts to read via the |from_host| ring.
//
// The size of the auxiliary buffer and flush interval is defined by
// the following config.ini android_hw setting:
//
// 1) android_hw->hw_gltransport_asg_writeBufferSize
// 2) android_hw->hw_gltransport_asg_writeStepSize
//
// 1) the size for the auxiliary buffer
// 2) the step size over which commands are flushed to the host
//
// When transferring commands, command data is built up in writeStepSize
// chunks and flushed to the host when either writeStepSize is reached or
// the guest flushes explicitly.
//
// Command vs. Data Modes
//
// For command data larger than writeStepSize or when transferring data, we
// fall back to using a different mode where the entire auxiliary buffer is
// used to perform the transfer, |asg_writeBufferSize| steps at a time. The
// host is also notified of the total transport size.
//
// When writing back to the guest, it is assumed that the write buffer will
// be completely empty as the guest has already flushed and the host has
// already consumed all commands/data, and is writing back. In this case,
// the full auxiliary buffer is used at the same time for writing back to
// the guest.
//
// Larger / Shared transfers
//
// Each of |to_host| and |from_host| can contain elements of type 1, 2, or 3:
// Type 1: 8 bytes: 4 bytes offset, 4 bytes size. Relative to write buffer.
struct __attribute__((__packed__)) asg_type1_xfer {
    uint32_t offset;
    uint32_t size;
};
// Type 2: 16 bytes: 16 bytes offset into address space PCI space, 8 bytes
// size.
struct __attribute__((__packed__)) asg_type2_xfer {
    uint64_t physAddr;
    uint64_t size;
};
// Type 3: There is a large transfer of known size and the entire write buffer
// will be used to send it over.
//
// For type 1 transfers, we get the corresponding host virtual address by
// adding the offset to the beginning of the write buffer.  For type 2
// transfers, we need to calculate the guest physical address and then call
// addressspacecontrolops.gethostptr, which is slower since it goes through
// a data structure map of existing mappings.
//
// The rings never contain a mix of type 1 and 2 elements. For to_host,
// the guest initiates changes between type 1 and 2.
//
// The config fields:
//
struct asg_ring_config {
    // config[0]: size of the auxiliary buffer
    uint32_t buffer_size;

    // config[1]: flush interval for the auxiliary buffer
    uint32_t flush_interval;

    // the position of the interval in the auxiliary buffer
    // that the host has read so far
    uint32_t host_consumed_pos;

    // the start of the places the guest might write to next
    uint32_t guest_write_pos;

    // 1 if transfers are of type 1, 2 if transfers of type 2,
    // 3 if the overall transfer size is known and we are sending something large.
    uint32_t transfer_mode;

    // the size of the transfer, used if transfer size is known.
    // Set before setting config[2] to 3.
    uint32_t transfer_size;

    // error state
    uint32_t in_error;
};

}  // extern "C"