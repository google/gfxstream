// Copyright 2023 The Android Open Source Project
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

#ifndef VIRTGPU_GFXSTREAM_RENDERER_H
#define VIRTGPU_GFXSTREAM_RENDERER_H

/* An implementation of virtio-gpu-3d that streams rendering commands. */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if !defined(GFXSTREAM_NO_IOVEC)
struct iovec {
    void* iov_base; /* Starting address */
    size_t iov_len; /* Length in bytes */
};
#endif
#else
#include <sys/uio.h>
#endif

struct stream_renderer_box {
    uint32_t x, y, z;
    uint32_t w, h, d;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Versioning
 */
#define STREAM_RENDERER_VERSION_MAJOR 0
#define STREAM_RENDERER_VERSION_MINOR 1
#define STREAM_RENDERER_VERSION_PATCH 2

#ifdef _WIN32
#define VG_EXPORT __declspec(dllexport)
#else
#define VG_EXPORT __attribute__((visibility("default")))
#endif

struct stream_renderer_resource_create_args {
    uint32_t handle;
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
};

#define STREAM_RENDERER_FLAG_FENCE (1 << 0)
#define STREAM_RENDERER_FLAG_FENCE_RING_IDX (1 << 1)
#define STREAM_RENDERER_FLAG_FENCE_SHAREABLE (1 << 2)
struct stream_renderer_fence {
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t ring_idx;
};

#define STREAM_HANDLE_TYPE_MEM_OPAQUE_FD 0x1
#define STREAM_HANDLE_TYPE_MEM_DMABUF 0x2
#define STREAM_HANDLE_TYPE_MEM_OPAQUE_WIN32 0x3
#define STREAM_HANDLE_TYPE_MEM_SHM 0x4
#define STREAM_HANDLE_TYPE_MEM_ZIRCON 0x5

#define STREAM_HANDLE_TYPE_SIGNAL_OPAQUE_FD 0x10
#define STREAM_HANDLE_TYPE_SIGNAL_SYNC_FD 0x20
#define STREAM_HANDLE_TYPE_SIGNAL_OPAQUE_WIN32 0x30
#define STREAM_HANDLE_TYPE_SIGNAL_ZIRCON 0x40
#define STREAM_HANDLE_TYPE_SIGNAL_EVENT_FD 0x50

#define STREAM_HANDLE_TYPE_PLATFORM_SCREEN_BUFFER_QNX 0x01000000
#define STREAM_HANDLE_TYPE_PLATFORM_EGL_NATIVE_PIXMAP 0x02000000

struct stream_renderer_handle {
    int64_t os_handle;
    uint32_t handle_type;
};

// @user_data: custom user data passed during `stream_renderer_init`
// @type: one of STREAM_RENDERER_DEBUG_*
// @string: null-terminated C-string
#define STREAM_RENDERER_DEBUG_ERROR 0x1
#define STREAM_RENDERER_DEBUG_WARN 0x2
#define STREAM_RENDERER_DEBUG_INFO 0x3
#define STREAM_RENDERER_DEBUG_DEBUG 0x4
struct stream_renderer_debug {
    uint32_t debug_type;
    const char* message;
};

// Callback for writing a fence.
typedef void (*stream_renderer_fence_callback)(void* user_data,
                                               struct stream_renderer_fence* fence_data);

// Callback for allowing debug prints or possibly even aborts.
typedef void (*stream_renderer_debug_callback)(void* user_data,
                                               struct stream_renderer_debug* debug);

// Parameters - data passed to initialize the renderer, with the goal of avoiding FFI breakages.
// To change the data a parameter is passing safely, you should create a new parameter and
// deprecate the old one. The old parameter may be removed after sufficient time.
//
// STREAM_RENDERER_PARAM_NULL: Reserved value
//
// The following are required for correct operation:
// STREAM_RENDERER_PARAM_USER_DATA: User data, for custom use by VMM.
// STREAM_RENDERER_PARAM_RENDERER_FLAGS: Bitwise flags for the renderer.
// STREAM_RENDERER_PARAM_FENCE_CALLBACK: A function of the type `stream_renderer_fence_callback`

// The following are optional:
// STREAM_RENDERER_PARAM_WIN0_WIDTH: The width of window[0], when using surface-based rendering
// STREAM_RENDERER_PARAM_WIN0_HEIGHT: The height of window[0], when using surface-based rendering
#define STREAM_RENDERER_PARAM_NULL 0
#define STREAM_RENDERER_PARAM_USER_DATA 1
#define STREAM_RENDERER_PARAM_RENDERER_FLAGS 2
#define STREAM_RENDERER_PARAM_FENCE_CALLBACK 3
#define STREAM_RENDERER_PARAM_WIN0_WIDTH 4
#define STREAM_RENDERER_PARAM_WIN0_HEIGHT 5
#define STREAM_RENDERER_PARAM_DEBUG_CALLBACK 6

// An entry in the stream renderer parameters list.
// The key should be one of STREAM_RENDERER_PARAM_*
// The value can be either a uint64_t or cast to a pointer to a struct, depending on if the
// parameter needs to pass data bigger than a single uint64_t.
struct stream_renderer_param {
    uint64_t key;
    uint64_t value;
};

// Entry point for the stream renderer.
// Pass a list of parameters to configure the renderer. The available ones are listed above. If a
// parameter is not supported, the renderer will ignore it and warn in stderr.
// Return value 0 indicates success, and a negative number indicates failure.
VG_EXPORT int stream_renderer_init(struct stream_renderer_param* stream_renderer_params,
                                   uint64_t num_params);

VG_EXPORT void stream_renderer_teardown(void);

VG_EXPORT int stream_renderer_resource_create(struct stream_renderer_resource_create_args* args,
                                              struct iovec* iov, uint32_t num_iovs);

VG_EXPORT void stream_renderer_resource_unref(uint32_t res_handle);
VG_EXPORT void stream_renderer_context_destroy(uint32_t handle);

struct stream_renderer_command {
    uint32_t ctx_id;
    uint32_t cmd_size;
    uint8_t* cmd;

    // Unstable: do not use until release strictly greater than 0.1.2
    uint32_t num_in_fences;
    struct stream_renderer_handle* fences;
};

VG_EXPORT int stream_renderer_submit_cmd(struct stream_renderer_command* cmd);

VG_EXPORT int stream_renderer_transfer_read_iov(uint32_t handle, uint32_t ctx_id, uint32_t level,
                                                uint32_t stride, uint32_t layer_stride,
                                                struct stream_renderer_box* box, uint64_t offset,
                                                struct iovec* iov, int iovec_cnt);
VG_EXPORT int stream_renderer_transfer_write_iov(uint32_t handle, uint32_t ctx_id, int level,
                                                 uint32_t stride, uint32_t layer_stride,
                                                 struct stream_renderer_box* box, uint64_t offset,
                                                 struct iovec* iovec, unsigned int iovec_cnt);
VG_EXPORT void stream_renderer_get_cap_set(uint32_t set, uint32_t* max_ver, uint32_t* max_size);
VG_EXPORT void stream_renderer_fill_caps(uint32_t set, uint32_t version, void* caps);

VG_EXPORT int stream_renderer_resource_attach_iov(int res_handle, struct iovec* iov, int num_iovs);
VG_EXPORT void stream_renderer_resource_detach_iov(int res_handle, struct iovec** iov,
                                                   int* num_iovs);
VG_EXPORT void stream_renderer_ctx_attach_resource(int ctx_id, int res_handle);
VG_EXPORT void stream_renderer_ctx_detach_resource(int ctx_id, int res_handle);

struct stream_renderer_create_blob {
    uint32_t blob_mem;
    uint32_t blob_flags;
    uint64_t blob_id;
    uint64_t size;
};

#define STREAM_BLOB_MEM_GUEST 1
#define STREAM_BLOB_MEM_HOST3D 2
#define STREAM_BLOB_MEM_HOST3D_GUEST 3

#define STREAM_BLOB_FLAG_USE_MAPPABLE 1
#define STREAM_BLOB_FLAG_USE_SHAREABLE 2
#define STREAM_BLOB_FLAG_USE_CROSS_DEVICE 4
#define STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE 8

VG_EXPORT int stream_renderer_create_blob(uint32_t ctx_id, uint32_t res_handle,
                                          const struct stream_renderer_create_blob* create_blob,
                                          const struct iovec* iovecs, uint32_t num_iovs,
                                          const struct stream_renderer_handle* handle);

VG_EXPORT int stream_renderer_export_blob(uint32_t res_handle,
                                          struct stream_renderer_handle* handle);

VG_EXPORT int stream_renderer_resource_map(uint32_t res_handle, void** hvaOut, uint64_t* sizeOut);
VG_EXPORT int stream_renderer_resource_unmap(uint32_t res_handle);

VG_EXPORT int stream_renderer_context_create(uint32_t ctx_id, uint32_t nlen, const char* name,
                                             uint32_t context_init);

VG_EXPORT int stream_renderer_create_fence(const struct stream_renderer_fence* fence);

#define STREAM_RENDERER_MAP_CACHE_MASK 0x0f
#define STREAM_RENDERER_MAP_CACHE_NONE 0x00
#define STREAM_RENDERER_MAP_CACHE_CACHED 0x01
#define STREAM_RENDERER_MAP_CACHE_UNCACHED 0x02
#define STREAM_RENDERER_MAP_CACHE_WC 0x03
VG_EXPORT int stream_renderer_resource_map_info(uint32_t res_handle, uint32_t* map_info);

// Unique identifier for a GPU device.
struct stream_renderer_device_id {
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
};

struct stream_renderer_vulkan_info {
    uint32_t memory_index;
    struct stream_renderer_device_id device_id;
};

VG_EXPORT int stream_renderer_vulkan_info(uint32_t res_handle,
                                          struct stream_renderer_vulkan_info* vulkan_info);

#ifdef __cplusplus
}  // extern "C"
#endif

// based on VIRGL_RENDERER_USE* and friends
enum RendererFlags {
    STREAM_RENDERER_FLAGS_USE_EGL_BIT = 1 << 0,
    STREAM_RENDERER_FLAGS_THREAD_SYNC = 1 << 1,
    STREAM_RENDERER_FLAGS_USE_GLX_BIT = 1 << 2,
    STREAM_RENDERER_FLAGS_USE_SURFACELESS_BIT = 1 << 3,
    STREAM_RENDERER_FLAGS_USE_GLES_BIT = 1 << 4,
    STREAM_RENDERER_FLAGS_USE_VK_BIT = 1 << 5,
    STREAM_RENDERER_FLAGS_USE_EXTERNAL_BLOB = 1 << 6,
    STREAM_RENDERER_FLAGS_USE_SYSTEM_BLOB = 1 << 7,
    STREAM_RENDERER_FLAGS_VULKAN_NATIVE_SWAPCHAIN_BIT = 1 << 8,
    // Unstable: do not use until a release greater than 0.1.2
    STREAM_RENDERER_FLAGS_VULKAN_EXTERNAL_SYNC = 1 << 31,
};

#endif
