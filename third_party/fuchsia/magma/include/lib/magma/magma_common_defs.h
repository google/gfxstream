// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MAGMA_CLIENT_INCLUDE_LIB_MAGMA_MAGMA_COMMON_DEFS_H_
#define LIB_MAGMA_CLIENT_INCLUDE_LIB_MAGMA_MAGMA_COMMON_DEFS_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// This header is C, so clang-tidy shouldn't recommend using C++ features.
// NOLINTBEGIN(modernize-use-using)

// LINT.IfChange(version)
// This version should be incremented whenever the Magma API changes.
#define MAGMA_API_VERSION 1
// LINT.ThenChange()

// LINT.IfChange
#if defined(__Fuchsia__)
// The entrypoints should be exported from libmagma_client.a. ICDs should use a
// version script to re-exporting magma entrypoints.
#define MAGMA_EXPORT __attribute__((visibility("default")))
#else
#define MAGMA_EXPORT
#endif

typedef uint64_t magma_query_t;

// This is a list of vendor-neutral queries that can be passed to magma_query.
// Returns the hardware vendor ID (simple result) - should be the PCI ID of the GPU vendor
// if possible, or the Khronos vendor ID otherwise.
#define MAGMA_QUERY_VENDOR_ID ((magma_query_t)0)
// Returns the hardware device ID (simple result)
#define MAGMA_QUERY_DEVICE_ID ((magma_query_t)1)
// Returns the version of the vendor interfaces supported by the system driver (simple result).
#define MAGMA_QUERY_VENDOR_VERSION ((magma_query_t)2)
// Returns true if MAGMA_QUERY_TOTAL_TIME is supported (simple result)
#define MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED ((magma_query_t)3)
// 4 was MAGMA_QUERY_MINIMUM_MAPPABLE_ADDRESS
// Upper 32bits: max inflight messages, lower 32bits: max inflight memory (MB) (simple result)
#define MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS ((magma_query_t)5)

// All vendor-specific queries IDs that can be passed to magma_query must be >=
// MAGMA_QUERY_VENDOR_PARAM_0.
#define MAGMA_QUERY_VENDOR_PARAM_0 ((magma_query_t)10000)

// This is a list of vendor-neutral queries that can be passed to magma_query.
// Returns a struct magma_total_time_query_result (buffer result)
#define MAGMA_QUERY_TOTAL_TIME ((magma_query_t)500)

// reserved ID to represent an invalid object
#define MAGMA_INVALID_OBJECT_ID ((uint64_t)0ull)

// All vendor-specific command buffer flags must be >=
// MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0.
#define MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0 ((uint64_t)(1ull << 16))

typedef int32_t magma_status_t;

// possible values for magma_status_t
#define MAGMA_STATUS_OK ((magma_status_t)(0))
#define MAGMA_STATUS_INTERNAL_ERROR ((magma_status_t)(-1))
#define MAGMA_STATUS_INVALID_ARGS ((magma_status_t)(-2))
#define MAGMA_STATUS_ACCESS_DENIED ((magma_status_t)(-3))
#define MAGMA_STATUS_MEMORY_ERROR ((magma_status_t)(-4))
#define MAGMA_STATUS_CONTEXT_KILLED ((magma_status_t)(-5))
#define MAGMA_STATUS_CONNECTION_LOST ((magma_status_t)(-6))
#define MAGMA_STATUS_TIMED_OUT ((magma_status_t)(-7))
#define MAGMA_STATUS_UNIMPLEMENTED ((magma_status_t)(-8))
// This error means that an object was not in the right state for an operation on it.
#define MAGMA_STATUS_BAD_STATE ((magma_status_t)(-9))
#define MAGMA_STATUS_ALIAS_FOR_LAST MAGMA_STATUS_BAD_STATE

typedef uint32_t magma_cache_operation_t;
// possible values for magma_cache_operation_t
#define MAGMA_CACHE_OPERATION_CLEAN ((magma_cache_operation_t)0)
#define MAGMA_CACHE_OPERATION_CLEAN_INVALIDATE ((magma_cache_operation_t)1)

typedef uint32_t magma_cache_policy_t;
// possible values for magma_cache_policy_t
#define MAGMA_CACHE_POLICY_CACHED ((magma_cache_policy_t)0)
#define MAGMA_CACHE_POLICY_WRITE_COMBINING ((magma_cache_policy_t)1)
#define MAGMA_CACHE_POLICY_UNCACHED ((magma_cache_policy_t)2)

#define MAGMA_DUMP_TYPE_NORMAL ((uint32_t)(1 << 0))

#define MAGMA_PERF_COUNTER_RESULT_DISCONTINUITY ((uint32_t)(1 << 0))

typedef uint32_t magma_format_t;

// Values must match fuchsia.sysmem.PixelFormatType
#define MAGMA_FORMAT_INVALID ((magma_format_t)0)
#define MAGMA_FORMAT_R8G8B8A8 ((magma_format_t)1)
#define MAGMA_FORMAT_BGRA32 ((magma_format_t)101)
#define MAGMA_FORMAT_I420 ((magma_format_t)102)
#define MAGMA_FORMAT_M420 ((magma_format_t)103)
#define MAGMA_FORMAT_NV12 ((magma_format_t)104)
#define MAGMA_FORMAT_YUY2 ((magma_format_t)105)
#define MAGMA_FORMAT_MJPEG ((magma_format_t)106)
#define MAGMA_FORMAT_YV12 ((magma_format_t)107)
#define MAGMA_FORMAT_BGR24 ((magma_format_t)108)
#define MAGMA_FORMAT_RGB565 ((magma_format_t)109)
#define MAGMA_FORMAT_RGB332 ((magma_format_t)110)
#define MAGMA_FORMAT_RGB2220 ((magma_format_t)111)
#define MAGMA_FORMAT_L8 ((magma_format_t)112)
#define MAGMA_FORMAT_R8 ((magma_format_t)113)
#define MAGMA_FORMAT_R8G8 ((magma_format_t)114)

typedef uint64_t magma_format_modifier_t;
// These must match the fuchsia.sysmem format modifier values.
#define MAGMA_FORMAT_MODIFIER_LINEAR ((magma_format_modifier_t)0x0000000000000000)

#define MAGMA_FORMAT_MODIFIER_INTEL_X_TILED ((magma_format_modifier_t)0x0100000000000001)
#define MAGMA_FORMAT_MODIFIER_INTEL_Y_TILED ((magma_format_modifier_t)0x0100000000000002)
#define MAGMA_FORMAT_MODIFIER_INTEL_YF_TILED ((magma_format_modifier_t)0x0100000000000003)

#define MAGMA_FORMAT_MODIFIER_INTEL_Y_TILED_CCS ((magma_format_modifier_t)0x0100000001000002)
#define MAGMA_FORMAT_MODIFIER_INTEL_YF_TILED_CCS ((magma_format_modifier_t)0x0100000001000003)

#define MAGMA_FORMAT_MODIFIER_ARM_YUV_BIT ((magma_format_modifier_t)0x10)
#define MAGMA_FORMAT_MODIFIER_ARM_SPLIT_BLOCK_BIT ((magma_format_modifier_t)0x20)
#define MAGMA_FORMAT_MODIFIER_ARM_SPARSE_BIT ((magma_format_modifier_t)0x40)
#define MAGMA_FORMAT_MODIFIER_ARM_BCH_BIT ((magma_format_modifier_t)0x800)
#define MAGMA_FORMAT_MODIFIER_ARM_TE_BIT ((magma_format_modifier_t)0x1000)
#define MAGMA_FORMAT_MODIFIER_ARM_TILED_HEADER_BIT ((magma_format_modifier_t)0x2000)

#define MAGMA_FORMAT_MODIFIER_ARM ((magma_format_modifier_t)0x0800000000000000)
#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16 ((magma_format_modifier_t)0x0800000000000001)
#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_32X8 ((magma_format_modifier_t)0x0800000000000002)
#define MAGMA_FORMAT_MODIFIER_ARM_LINEAR_TE ((magma_format_modifier_t)0x0800000000001000)
#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_TE ((magma_format_modifier_t)0x0800000000001001)
#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_32X8_TE ((magma_format_modifier_t)0x0800000000001002)

#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER \
  ((magma_format_modifier_t)0x0800000000002011)

#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV \
  ((magma_format_modifier_t)0x0800000000000071)
#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE \
  ((magma_format_modifier_t)0x0800000000001071)

#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TILED_HEADER \
  ((magma_format_modifier_t)0x0800000000002071)
#define MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE_TILED_HEADER \
  ((magma_format_modifier_t)0x0800000000003071)

typedef uint32_t magma_colorspace_t;

// Must match fuchsia.sysmem.ColorSpaceType values.
#define MAGMA_COLORSPACE_INVALID ((magma_colorspace_t)0)
#define MAGMA_COLORSPACE_SRGB ((magma_colorspace_t)1)
#define MAGMA_COLORSPACE_REC601_NTSC ((magma_colorspace_t)2)
#define MAGMA_COLORSPACE_REC601_NTSC_FULL_RANGE ((magma_colorspace_t)3)
#define MAGMA_COLORSPACE_REC601_PAL ((magma_colorspace_t)4)
#define MAGMA_COLORSPACE_REC601_PAL_FULL_RANGE ((magma_colorspace_t)5)
#define MAGMA_COLORSPACE_REC709 ((magma_colorspace_t)6)
#define MAGMA_COLORSPACE_REC2020 ((magma_colorspace_t)7)
#define MAGMA_COLORSPACE_REC2100 ((magma_colorspace_t)8)

typedef uint32_t magma_coherency_domain_t;

#define MAGMA_COHERENCY_DOMAIN_CPU ((magma_coherency_domain_t)0)
#define MAGMA_COHERENCY_DOMAIN_RAM ((magma_coherency_domain_t)1)
#define MAGMA_COHERENCY_DOMAIN_INACCESSIBLE ((magma_coherency_domain_t)2)

#define MAGMA_POLL_TYPE_SEMAPHORE ((uint32_t)1)
#define MAGMA_POLL_TYPE_HANDLE ((uint32_t)2)

#define MAGMA_POLL_CONDITION_READABLE ((uint32_t)1)
#define MAGMA_POLL_CONDITION_SIGNALED ((uint32_t)3)

typedef uint32_t magma_buffer_range_op_t;

// Eagerly populate hardware page tables with the pages mapping in this range, committing pages as
// needed. This is not needed for MAGMA_MAP_FLAG_GROWABLE allocations, since the page tables
// will be populated on demand.
#define MAGMA_BUFFER_RANGE_OP_POPULATE_TABLES ((magma_buffer_range_op_t)1)
// Commit memory on the client thread. Hardware page tables may not be populated. This should be
// used
// before POPULATE_TABLES to ensure the expensive work of committing pages happens with the
// correct priority and without blocking the processing in the MSD of commands from other threads
// from the same connection.
#define MAGMA_BUFFER_RANGE_OP_COMMIT ((magma_buffer_range_op_t)2)
// Depopulate hardware page table mappings for this range. This prevents the hardware from
// accessing
// pages in that range, but the pages retain their contents.
#define MAGMA_BUFFER_RANGE_OP_DEPOPULATE_TABLES ((magma_buffer_range_op_t)3)
// Decommit memory wholy on the client thread. This may fail if the MSD currently has the page
// tables populated.
#define MAGMA_BUFFER_RANGE_OP_DECOMMIT ((magma_buffer_range_op_t)4)

// Set `is_secure` flag in the `BufferMemorySettings` so protected memory is allocated.
#define MAGMA_SYSMEM_FLAG_PROTECTED ((uint32_t)(1 << 0))
// This flag is only used to modify the name of the buffer to signal that the client requested it
// using vkAllocateMemory or similar.
#define MAGMA_SYSMEM_FLAG_FOR_CLIENT ((uint32_t)(1 << 2))

#define MAGMA_MAX_IMAGE_PLANES ((uint32_t)4)

#define MAGMA_MAX_DRM_FORMAT_MODIFIERS ((uint32_t)16)

// Normal bool doesn't have to be a particular size.
typedef uint8_t magma_bool_t;

typedef uint64_t magma_device_t;

typedef uint64_t magma_buffer_t;

typedef uint64_t magma_semaphore_t;

typedef uint64_t magma_perf_count_pool_t;

typedef uint64_t magma_connection_t;

// An opaque handle that corresponds to a fuchsia.sysmem.Allocator connection to sysmem.
typedef uint64_t magma_sysmem_connection_t;

// Corresponds to a zx_handle_t on Fuchsia.
typedef uint32_t magma_handle_t;

// An ID for a buffer that can be used to refer to it when submitting command buffers. Only valid
// within a single connection.
typedef uint64_t magma_buffer_id_t;

typedef uint64_t magma_semaphore_id_t;

typedef struct magma_poll_item {
  union {
    magma_semaphore_t semaphore;
    magma_handle_t handle;
  };
  uint32_t type;
  uint32_t condition;
  uint32_t result;
  uint32_t unused;
} magma_poll_item_t;

// A buffer referenced by a command buffer descriptor
typedef struct magma_exec_resource {
  magma_buffer_id_t buffer_id;
  uint64_t offset;
  uint64_t length;
} magma_exec_resource_t;

// A resource to be executed by a command buffer descriptor
typedef struct magma_exec_command_buffer {
  uint32_t resource_index;
  uint32_t unused;
  uint64_t start_offset;
} __attribute__((__aligned__(8))) magma_exec_command_buffer_t;

typedef struct magma_command_descriptor {
  // The count of `resources` that may be referenced by the hardware. These must have been
  // mapped to the hardware previously.
  uint32_t resource_count;
  // The count of `command_buffers` to be executed as a unit.
  uint32_t command_buffer_count;
  // The count of `semaphore_ids` to be waited upon before beginning execution; these
  // will be reset after all have been signaled.
  uint32_t wait_semaphore_count;
  // The count of `semaphore_ids` to be signaled after execution is complete.
  uint32_t signal_semaphore_count;

  struct magma_exec_resource* resources;
  struct magma_exec_command_buffer* command_buffers;
  // List of semaphore IDs: first the wait semaphore IDs, followed by signal semaphore IDs.
  uint64_t* semaphore_ids;

  uint64_t flags;

} __attribute__((__aligned__(8))) magma_command_descriptor_t;

typedef struct magma_inline_command_buffer {
  void* data;
  uint64_t size;
  uint64_t* semaphore_ids;
  uint32_t semaphore_count;
} magma_inline_command_buffer_t;

typedef struct magma_total_time_query_result {
  uint64_t gpu_time_ns;        // GPU time in ns since driver start.
  uint64_t monotonic_time_ns;  // monotonic clock time measured at same time CPU time was.
} magma_total_time_query_result_t;

typedef struct magma_buffer_offset {
  uint64_t buffer_id;
  uint64_t offset;
  uint64_t length;
} magma_buffer_offset_t;

// The top 16 bits are reserved for vendor-specific flags.
#define MAGMA_MAP_FLAG_VENDOR_SHIFT ((uint64_t)16)

#define MAGMA_MAP_FLAG_READ ((uint64_t)(1 << 0))
#define MAGMA_MAP_FLAG_WRITE ((uint64_t)(1 << 1))
#define MAGMA_MAP_FLAG_EXECUTE ((uint64_t)(1 << 2))
#define MAGMA_MAP_FLAG_GROWABLE ((uint64_t)(1 << 3))
#define MAGMA_MAP_FLAG_VENDOR_0 ((uint64_t)(1 << MAGMA_MAP_FLAG_VENDOR_SHIFT))

typedef struct magma_buffer_info {
  uint64_t committed_byte_count;
  uint64_t size;
} magma_buffer_info_t;

#define MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE ((uint32_t)1)
#define MAGMA_IMAGE_CREATE_FLAGS_VULKAN_USAGE ((uint32_t)(1 << 1))

typedef struct magma_image_create_info {
  // A format specified by DRM (Linux Direct Rendering Manager)
  uint64_t drm_format;
  // The last modifier must be DRM_FORMAT_MOD_INVALID
  uint64_t drm_format_modifiers[MAGMA_MAX_DRM_FORMAT_MODIFIERS];
  uint32_t width;
  uint32_t height;
  // If MAGMA_IMAGE_CREATE_FLAGS_VULKAN_USAGE is provided, Vulkan usage flags
  // should be provided in the upper 32 bits.
  uint64_t flags;
} magma_image_create_info_t;

typedef struct magma_image_info {
  uint64_t plane_strides[MAGMA_MAX_IMAGE_PLANES];
  uint32_t plane_offsets[MAGMA_MAX_IMAGE_PLANES];
  uint64_t drm_format_modifier;
  uint32_t coherency_domain;
  uint32_t unused;
} magma_image_info_t;

// LINT.ThenChange(version)
// NOLINTEND(modernize-use-using)

#if defined(__cplusplus)
}
#endif

#endif  // LIB_MAGMA_CLIENT_INCLUDE_LIB_MAGMA_MAGMA_COMMON_DEFS_H_
