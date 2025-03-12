// Copyright 2019 The Android Open Source Project
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

#include <cstdarg>
#include <cstdint>

#include "FrameBuffer.h"
#include "GfxStreamAgents.h"
#include "VirtioGpuFrontend.h"
#include "aemu/base/Metrics.h"
#include "aemu/base/system/System.h"
#include "gfxstream/Strings.h"
#include "gfxstream/host/Features.h"
#include "gfxstream/host/Tracing.h"
#include "host-common/FeatureControl.h"
#include "host-common/GraphicsAgentFactory.h"
#include "host-common/android_pipe_common.h"
#include "host-common/android_pipe_device.h"
#include "host-common/globals.h"
#include "host-common/opengles-pipe.h"
#include "host-common/opengles.h"
#include "host-common/refcount-pipe.h"
#include "host-common/vm_operations.h"
#include "vulkan/VulkanDispatch.h"
#include "render-utils/RenderLib.h"
#include "vk_util.h"

extern "C" {
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "host-common/goldfish_pipe.h"
}  // extern "C"

#define MAX_DEBUG_BUFFER_SIZE 512
#define ELLIPSIS "...\0"
#define ELLIPSIS_LEN 4

// Define the typedef for emulogger
typedef void (*emulogger)(char severity, const char* file, unsigned int line,
                          int64_t timestamp_us, const char* message);

// Template to enable the method call if gfxstream_logger_t equals emulogger
template<typename T>
typename std::enable_if<std::is_same<T, emulogger>::value, bool>::type
call_logger_if_valid(T logger, char severity, const char* file, unsigned int line, int64_t timestamp_us, const char* message) {
    // Call the logger and return true if the type matches
    if (!logger) { return false; }
    logger(severity, file, line, timestamp_us, message);
    return true;
}

// Template for invalid logger types (returns false if types don't match)
template<typename T>
typename std::enable_if<!std::is_same<T, emulogger>::value, bool>::type
call_logger_if_valid(T, char, const char*, unsigned int, int64_t, const char*) {
    // Return false if the type doesn't match
    return false;
}

void* globalUserData = nullptr;
stream_renderer_debug_callback globalDebugCallback = nullptr;

static void append_truncation_marker(char* buf, int remaining_size) {
    // Safely append truncation marker "..." if buffer has enough space
    if (remaining_size >= ELLIPSIS_LEN) {
        strncpy(buf + remaining_size - ELLIPSIS_LEN, ELLIPSIS, ELLIPSIS_LEN);
    } else if (remaining_size >= 1) {
        buf[remaining_size - 1] = '\0';
    } else {
        // Oh oh.. In theory this shouldn't happen.
        assert(false);
    }
}

static void log_with_prefix(char*& buf, int& remaining_size, const char* file, int line,
                            const char* pretty_function) {
    // Add logging prefix if necessary
    int formatted_len = snprintf(buf, remaining_size, "[%s(%d)] %s ", file, line, pretty_function);

    // Handle potential truncation
    if (formatted_len >= remaining_size) {
        append_truncation_marker(buf, remaining_size);
        remaining_size = 0;
    } else {
        buf += formatted_len;             // Adjust buf
        remaining_size -= formatted_len;  // Reduce remaining buffer size
    }
}

static char translate_severity(uint32_t type) {
    switch (type) {
        case STREAM_RENDERER_DEBUG_ERROR:
            return 'E';
        case STREAM_RENDERER_DEBUG_WARN:
            return 'W';
        case STREAM_RENDERER_DEBUG_INFO:
            return 'I';
        case STREAM_RENDERER_DEBUG_DEBUG:
            return 'D';
        default:
            return 'D';
    }
}

using android::AndroidPipe;
using android::base::MetricsLogger;
using gfxstream::host::VirtioGpuFrontend;

static VirtioGpuFrontend* sFrontend() {
    static VirtioGpuFrontend* p = new VirtioGpuFrontend;
    return p;
}

extern "C" {

void stream_renderer_log(uint32_t type, const char* file, int line, const char* pretty_function,
                         const char* format, ...) {

    char printbuf[MAX_DEBUG_BUFFER_SIZE];
    char* buf = printbuf;
    int remaining_size = MAX_DEBUG_BUFFER_SIZE;
    static_assert(MAX_DEBUG_BUFFER_SIZE > 4);

    // Add the logging prefix if needed
#ifdef CONFIG_AEMU
    static gfxstream_logger_t gfx_logger = get_gfx_stream_logger();
    if (!gfx_logger) {
        log_with_prefix(buf, remaining_size, file, line, pretty_function);
    }
#else
    log_with_prefix(buf, remaining_size, file, line, pretty_function);
#endif

    // Format the message with variable arguments
    va_list args;
    va_start(args, format);
    int formatted_len = vsnprintf(buf, remaining_size, format, args);
    va_end(args);

    // Handle potential truncation
    if (formatted_len >= remaining_size) {
        append_truncation_marker(buf, remaining_size);
    }

#ifdef CONFIG_AEMU
    // Forward to emulator?
    if (call_logger_if_valid(gfx_logger, translate_severity(type), file, line, 0, printbuf)) {
        return;
    }
#endif

    // To a gfxstream debugger?
    if (globalUserData && globalDebugCallback) {
        struct stream_renderer_debug debug = {0};
        debug.debug_type = type;
        debug.message = &printbuf[0];
        globalDebugCallback(globalUserData, &debug);
    } else {
        // Cannot use logging routines, fallback to stderr
        const char* logLevel = "error";
        if (type == STREAM_RENDERER_DEBUG_WARN) {
            logLevel = "warning";
        } else if (type == STREAM_RENDERER_DEBUG_INFO) {
            logLevel = "info";
        } else if (type == STREAM_RENDERER_DEBUG_DEBUG) {
            logLevel = "debug";
        }
        fprintf(stderr, "stream_renderer_log [%s]: %s\n", logLevel, printbuf);
    }
}

VG_EXPORT int stream_renderer_resource_create(struct stream_renderer_resource_create_args* args,
                                              struct iovec* iov, uint32_t num_iovs) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_create()");

    return sFrontend()->createResource(args, iov, num_iovs);
}

VG_EXPORT int stream_renderer_import_resource(
    uint32_t res_handle, const struct stream_renderer_handle* import_handle,
    const struct stream_renderer_import_data* import_data) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_import_resource()");

    return sFrontend()->importResource(res_handle, import_handle, import_data);
}

VG_EXPORT void stream_renderer_resource_unref(uint32_t res_handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_unref()");

    sFrontend()->unrefResource(res_handle);
}

VG_EXPORT void stream_renderer_context_destroy(uint32_t handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_context_destroy()");

    sFrontend()->destroyContext(handle);
}

VG_EXPORT int stream_renderer_submit_cmd(struct stream_renderer_command* cmd) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_submit_cmd()");

    return sFrontend()->submitCmd(cmd);
}

VG_EXPORT int stream_renderer_transfer_read_iov(uint32_t handle, uint32_t ctx_id, uint32_t level,
                                                uint32_t stride, uint32_t layer_stride,
                                                struct stream_renderer_box* box, uint64_t offset,
                                                struct iovec* iov, int iovec_cnt) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_transfer_read_iov()");

    return sFrontend()->transferReadIov(handle, offset, box, iov, iovec_cnt);
}

VG_EXPORT int stream_renderer_transfer_write_iov(uint32_t handle, uint32_t ctx_id, int level,
                                                 uint32_t stride, uint32_t layer_stride,
                                                 struct stream_renderer_box* box, uint64_t offset,
                                                 struct iovec* iovec, unsigned int iovec_cnt) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_transfer_write_iov()");

    return sFrontend()->transferWriteIov(handle, offset, box, iovec, iovec_cnt);
}

VG_EXPORT void stream_renderer_get_cap_set(uint32_t set, uint32_t* max_ver, uint32_t* max_size) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_get_cap_set()");

    GFXSTREAM_TRACE_NAME_TRACK(GFXSTREAM_TRACE_TRACK_FOR_CURRENT_THREAD(),
                               "Main Virtio Gpu Thread");

    // `max_ver` not useful
    return sFrontend()->getCapset(set, max_size);
}

VG_EXPORT void stream_renderer_fill_caps(uint32_t set, uint32_t version, void* caps) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_fill_caps()");

    // `version` not useful
    return sFrontend()->fillCaps(set, caps);
}

VG_EXPORT int stream_renderer_resource_attach_iov(int res_handle, struct iovec* iov, int num_iovs) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_attach_iov()");

    return sFrontend()->attachIov(res_handle, iov, num_iovs);
}

VG_EXPORT void stream_renderer_resource_detach_iov(int res_handle, struct iovec** iov,
                                                   int* num_iovs) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_detach_iov()");

    return sFrontend()->detachIov(res_handle);
}

VG_EXPORT void stream_renderer_ctx_attach_resource(int ctx_id, int res_handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_ctx_attach_resource()");

    sFrontend()->attachResource(ctx_id, res_handle);
}

VG_EXPORT void stream_renderer_ctx_detach_resource(int ctx_id, int res_handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_ctx_detach_resource()");

    sFrontend()->detachResource(ctx_id, res_handle);
}

VG_EXPORT int stream_renderer_resource_get_info(int res_handle,
                                                struct stream_renderer_resource_info* info) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_get_info()");

    return sFrontend()->getResourceInfo(res_handle, info);
}

VG_EXPORT void stream_renderer_flush(uint32_t res_handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_flush()");

    sFrontend()->flushResource(res_handle);
}

VG_EXPORT int stream_renderer_create_blob(uint32_t ctx_id, uint32_t res_handle,
                                          const struct stream_renderer_create_blob* create_blob,
                                          const struct iovec* iovecs, uint32_t num_iovs,
                                          const struct stream_renderer_handle* handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_create_blob()");

    sFrontend()->createBlob(ctx_id, res_handle, create_blob, handle);
    return 0;
}

VG_EXPORT int stream_renderer_export_blob(uint32_t res_handle,
                                          struct stream_renderer_handle* handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_export_blob()");

    return sFrontend()->exportBlob(res_handle, handle);
}

VG_EXPORT int stream_renderer_resource_map(uint32_t res_handle, void** hvaOut, uint64_t* sizeOut) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_map()");

    return sFrontend()->resourceMap(res_handle, hvaOut, sizeOut);
}

VG_EXPORT int stream_renderer_resource_unmap(uint32_t res_handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_unmap()");

    return sFrontend()->resourceUnmap(res_handle);
}

VG_EXPORT int stream_renderer_context_create(uint32_t ctx_id, uint32_t nlen, const char* name,
                                             uint32_t context_init) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_context_create()");

    return sFrontend()->createContext(ctx_id, nlen, name, context_init);
}

VG_EXPORT int stream_renderer_create_fence(const struct stream_renderer_fence* fence) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_create_fence()");

    if (fence->flags & STREAM_RENDERER_FLAG_FENCE_SHAREABLE) {
        int ret = sFrontend()->acquireContextFence(fence->ctx_id, fence->fence_id);
        if (ret) {
            return ret;
        }
    }

    if (fence->flags & STREAM_RENDERER_FLAG_FENCE_RING_IDX) {
        sFrontend()->createFence(fence->fence_id, VirtioGpuRingContextSpecific{
                                                      .mCtxId = fence->ctx_id,
                                                      .mRingIdx = fence->ring_idx,
                                                  });
    } else {
        sFrontend()->createFence(fence->fence_id, VirtioGpuRingGlobal{});
    }

    return 0;
}

VG_EXPORT int stream_renderer_export_fence(uint64_t fence_id,
                                           struct stream_renderer_handle* handle) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_export_fence()");

    return sFrontend()->exportFence(fence_id, handle);
}

VG_EXPORT void* stream_renderer_platform_create_shared_egl_context() {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_platform_create_shared_egl_context()");

    return sFrontend()->platformCreateSharedEglContext();
}

VG_EXPORT int stream_renderer_platform_destroy_shared_egl_context(void* context) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_platform_destroy_shared_egl_context()");

    return sFrontend()->platformDestroySharedEglContext(context);
}

VG_EXPORT int stream_renderer_resource_map_info(uint32_t res_handle, uint32_t* map_info) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_resource_map_info()");

    return sFrontend()->resourceMapInfo(res_handle, map_info);
}

VG_EXPORT int stream_renderer_vulkan_info(uint32_t res_handle,
                                          struct stream_renderer_vulkan_info* vulkan_info) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                          "stream_renderer_vulkan_info()");

    return sFrontend()->vulkanInfo(res_handle, vulkan_info);
}

VG_EXPORT int stream_renderer_suspend() {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_suspend()");

    // TODO: move pauseAllPreSave() here after kumquat updated.

    return 0;
}

VG_EXPORT int stream_renderer_snapshot(const char* dir) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_snapshot()");

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    return sFrontend()->snapshot(dir);
#else
    stream_renderer_error("Snapshot save requested without support.");
    return -EINVAL;
#endif
}

VG_EXPORT int stream_renderer_restore(const char* dir) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_restore()");

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    return sFrontend()->restore(dir);
#else
    stream_renderer_error("Snapshot save requested without support.");
    return -EINVAL;
#endif
}

VG_EXPORT int stream_renderer_resume() {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_resume()");

    // TODO: move resumeAll() here after kumquat updated.

    return 0;
}

static const GoldfishPipeServiceOps goldfish_pipe_service_ops = {
    // guest_open()
    [](GoldfishHwPipe* hwPipe) -> GoldfishHostPipe* {
        return static_cast<GoldfishHostPipe*>(android_pipe_guest_open(hwPipe));
    },
    // guest_open_with_flags()
    [](GoldfishHwPipe* hwPipe, uint32_t flags) -> GoldfishHostPipe* {
        return static_cast<GoldfishHostPipe*>(android_pipe_guest_open_with_flags(hwPipe, flags));
    },
    // guest_close()
    [](GoldfishHostPipe* hostPipe, GoldfishPipeCloseReason reason) {
        static_assert((int)GOLDFISH_PIPE_CLOSE_GRACEFUL == (int)PIPE_CLOSE_GRACEFUL,
                      "Invalid PIPE_CLOSE_GRACEFUL value");
        static_assert((int)GOLDFISH_PIPE_CLOSE_REBOOT == (int)PIPE_CLOSE_REBOOT,
                      "Invalid PIPE_CLOSE_REBOOT value");
        static_assert((int)GOLDFISH_PIPE_CLOSE_LOAD_SNAPSHOT == (int)PIPE_CLOSE_LOAD_SNAPSHOT,
                      "Invalid PIPE_CLOSE_LOAD_SNAPSHOT value");
        static_assert((int)GOLDFISH_PIPE_CLOSE_ERROR == (int)PIPE_CLOSE_ERROR,
                      "Invalid PIPE_CLOSE_ERROR value");

        android_pipe_guest_close(hostPipe, static_cast<PipeCloseReason>(reason));
    },
    // guest_pre_load()
    [](QEMUFile* file) { (void)file; },
    // guest_post_load()
    [](QEMUFile* file) { (void)file; },
    // guest_pre_save()
    [](QEMUFile* file) { (void)file; },
    // guest_post_save()
    [](QEMUFile* file) { (void)file; },
    // guest_load()
    [](QEMUFile* file, GoldfishHwPipe* hwPipe, char* force_close) -> GoldfishHostPipe* {
        (void)file;
        (void)hwPipe;
        (void)force_close;
        return nullptr;
    },
    // guest_save()
    [](GoldfishHostPipe* hostPipe, QEMUFile* file) {
        (void)hostPipe;
        (void)file;
    },
    // guest_poll()
    [](GoldfishHostPipe* hostPipe) {
        static_assert((int)GOLDFISH_PIPE_POLL_IN == (int)PIPE_POLL_IN, "invalid POLL_IN values");
        static_assert((int)GOLDFISH_PIPE_POLL_OUT == (int)PIPE_POLL_OUT, "invalid POLL_OUT values");
        static_assert((int)GOLDFISH_PIPE_POLL_HUP == (int)PIPE_POLL_HUP, "invalid POLL_HUP values");

        return static_cast<GoldfishPipePollFlags>(android_pipe_guest_poll(hostPipe));
    },
    // guest_recv()
    [](GoldfishHostPipe* hostPipe, GoldfishPipeBuffer* buffers, int numBuffers) -> int {
        // NOTE: Assumes that AndroidPipeBuffer and GoldfishPipeBuffer
        //       have exactly the same layout.
        static_assert(sizeof(AndroidPipeBuffer) == sizeof(GoldfishPipeBuffer),
                      "Invalid PipeBuffer sizes");
    // We can't use a static_assert with offsetof() because in msvc, it uses
    // reinterpret_cast.
    // TODO: Add runtime assertion instead?
    // https://developercommunity.visualstudio.com/content/problem/22196/static-assert-cannot-compile-constexprs-method-tha.html
#ifndef _MSC_VER
        static_assert(offsetof(AndroidPipeBuffer, data) == offsetof(GoldfishPipeBuffer, data),
                      "Invalid PipeBuffer::data offsets");
        static_assert(offsetof(AndroidPipeBuffer, size) == offsetof(GoldfishPipeBuffer, size),
                      "Invalid PipeBuffer::size offsets");
#endif
        return android_pipe_guest_recv(hostPipe, reinterpret_cast<AndroidPipeBuffer*>(buffers),
                                       numBuffers);
    },
    // wait_guest_recv()
    [](GoldfishHostPipe* hostPipe) { android_pipe_wait_guest_recv(hostPipe); },
    // guest_send()
    [](GoldfishHostPipe** hostPipe, const GoldfishPipeBuffer* buffers, int numBuffers) -> int {
        return android_pipe_guest_send(reinterpret_cast<void**>(hostPipe),
                                       reinterpret_cast<const AndroidPipeBuffer*>(buffers),
                                       numBuffers);
    },
    // wait_guest_send()
    [](GoldfishHostPipe* hostPipe) { android_pipe_wait_guest_send(hostPipe); },
    // guest_wake_on()
    [](GoldfishHostPipe* hostPipe, GoldfishPipeWakeFlags wakeFlags) {
        android_pipe_guest_wake_on(hostPipe, static_cast<int>(wakeFlags));
    },
    // dma_add_buffer()
    [](void* pipe, uint64_t paddr, uint64_t sz) {
        // not considered for virtio
    },
    // dma_remove_buffer()
    [](uint64_t paddr) {
        // not considered for virtio
    },
    // dma_invalidate_host_mappings()
    []() {
        // not considered for virtio
    },
    // dma_reset_host_mappings()
    []() {
        // not considered for virtio
    },
    // dma_save_mappings()
    [](QEMUFile* file) { (void)file; },
    // dma_load_mappings()
    [](QEMUFile* file) { (void)file; },
};

static int stream_renderer_opengles_init(uint32_t display_width, uint32_t display_height,
                                         int renderer_flags, const gfxstream::host::FeatureSet& features) {
    stream_renderer_debug("start. display dimensions: width %u height %u, renderer flags: 0x%x",
                          display_width, display_height, renderer_flags);

    // Flags processing

    // TODO: hook up "gfxstream egl" to the renderer flags
    // STREAM_RENDERER_FLAGS_USE_EGL_BIT in crosvm
    // as it's specified from launch_cvd.
    // At the moment, use ANDROID_GFXSTREAM_EGL=1
    // For test on GCE
    if (android::base::getEnvironmentVariable("ANDROID_GFXSTREAM_EGL") == "1") {
        android::base::setEnvironmentVariable("ANDROID_EGL_ON_EGL", "1");
        android::base::setEnvironmentVariable("ANDROID_EMUGL_LOG_PRINT", "1");
        android::base::setEnvironmentVariable("ANDROID_EMUGL_VERBOSE", "1");
    }
    // end for test on GCE

    android::base::setEnvironmentVariable("ANDROID_EMU_HEADLESS", "1");

    bool egl2eglByEnv = android::base::getEnvironmentVariable("ANDROID_EGL_ON_EGL") == "1";
    bool egl2eglByFlag = renderer_flags & STREAM_RENDERER_FLAGS_USE_EGL_BIT;
    bool enable_egl2egl = egl2eglByFlag || egl2eglByEnv;
    if (enable_egl2egl) {
        android::base::setEnvironmentVariable("ANDROID_GFXSTREAM_EGL", "1");
        android::base::setEnvironmentVariable("ANDROID_EGL_ON_EGL", "1");
    }

    bool surfaceless = renderer_flags & STREAM_RENDERER_FLAGS_USE_SURFACELESS_BIT;

    android::featurecontrol::productFeatureOverride();

    auto androidHw = aemu_get_android_hw();

    androidHw->hw_gltransport_asg_writeBufferSize = 1048576;
    androidHw->hw_gltransport_asg_writeStepSize = 262144;
    androidHw->hw_gltransport_asg_dataRingSize = 524288;
    androidHw->hw_gltransport_drawFlushInterval = 10000;

    EmuglConfig config;
    // Make all the console agents available.
#ifndef GFXSTREAM_MESON_BUILD
    android::emulation::injectGraphicsAgents(android::emulation::GfxStreamGraphicsAgentFactory());
#endif

    emuglConfig_init(&config, true /* gpu enabled */, "auto",
                     enable_egl2egl ? "swiftshader_indirect" : "host", 64, /* bitness */
                     surfaceless,                                          /* no window */
                     false,                                                /* blocklisted */
                     false,                                                /* has guest renderer */
                     WINSYS_GLESBACKEND_PREFERENCE_AUTO, true /* force host gpu vulkan */);

    emuglConfig_setupEnv(&config);

    gfxstream::vk::vkDispatch(false /* don't use test ICD */);

    android_prepareOpenglesEmulation();

    {
        static gfxstream::RenderLibPtr renderLibPtr = gfxstream::initLibrary();
        android_setOpenglesEmulation(renderLibPtr.get(), nullptr, nullptr);
    }

    int maj;
    int min;
#ifdef CONFIG_AEMU
    android_startOpenglesRenderer(display_width, display_height, 1, 28, getGraphicsAgents()->vm,
                                  getGraphicsAgents()->emu, getGraphicsAgents()->multi_display,
                                  &features, &maj, &min);
#else
    android_startOpenglesRenderer(display_width, display_height, 1, 28, nullptr, nullptr, nullptr,
                                  &features, &maj, &min);
#endif

    char* vendor = nullptr;
    char* renderer = nullptr;
    char* version = nullptr;

    android_getOpenglesHardwareStrings(&vendor, &renderer, &version);

    stream_renderer_info("GL strings; [%s] [%s] [%s].", vendor, renderer, version);

    auto openglesRenderer = android_getOpenglesRenderer();

    if (!openglesRenderer) {
        stream_renderer_error("No renderer started, fatal");
        return -EINVAL;
    }

#ifdef CONFIG_AEMU
    address_space_set_vm_operations(getGraphicsAgents()->vm);
#endif

    android_init_opengles_pipe();
    android_opengles_pipe_set_recv_mode(2 /* virtio-gpu */);
    android_init_refcount_pipe();

    return 0;
}

namespace {

int parseGfxstreamFeatures(const int renderer_flags,
                           const std::string& renderer_features,
                           gfxstream::host::FeatureSet& features) {
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, ExternalBlob,
        renderer_flags & STREAM_RENDERER_FLAGS_USE_EXTERNAL_BLOB);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(&features, VulkanExternalSync,
                                       renderer_flags & STREAM_RENDERER_FLAGS_VULKAN_EXTERNAL_SYNC);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, GlAsyncSwap, false);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, GlDirectMem, false);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, GlDma, false);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, GlesDynamicVersion, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, GlPipeChecksum, false);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, GuestVulkanOnly,
        (renderer_flags & STREAM_RENDERER_FLAGS_USE_VK_BIT) &&
        !(renderer_flags & STREAM_RENDERER_FLAGS_USE_GLES_BIT));
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, HostComposition, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, NativeTextureDecompression, false);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, NoDelayCloseColorBuffer, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, PlayStoreImage,
        !(renderer_flags & STREAM_RENDERER_FLAGS_USE_GLES_BIT));
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, RefCountPipe,
        /*Resources are ref counted via guest file objects.*/ false);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, SystemBlob,
        renderer_flags & STREAM_RENDERER_FLAGS_USE_SYSTEM_BLOB);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VirtioGpuFenceContexts, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VirtioGpuNativeSync, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VirtioGpuNext, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, Vulkan,
        renderer_flags & STREAM_RENDERER_FLAGS_USE_VK_BIT);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VulkanBatchedDescriptorSetUpdate, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VulkanIgnoredHandles, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VulkanNativeSwapchain,
        renderer_flags & STREAM_RENDERER_FLAGS_VULKAN_NATIVE_SWAPCHAIN_BIT);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VulkanNullOptionalStrings, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VulkanQueueSubmitWithCommands, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VulkanShaderFloat16Int8, true);
    GFXSTREAM_SET_FEATURE_ON_CONDITION(
        &features, VulkanSnapshots,
        android::base::getEnvironmentVariable("ANDROID_GFXSTREAM_CAPTURE_VK_SNAPSHOT") == "1");

    for (const std::string& renderer_feature : gfxstream::Split(renderer_features, ",")) {
        if (renderer_feature.empty()) continue;

        const std::vector<std::string>& parts = gfxstream::Split(renderer_feature, ":");
        if (parts.size() != 2) {
            stream_renderer_error("Error: invalid renderer features: %s",
                                  renderer_features.c_str());
            return -EINVAL;
        }

        const std::string& feature_name = parts[0];

        auto feature_it = features.map.find(feature_name);
        if (feature_it == features.map.end()) {
            stream_renderer_error("Error: invalid renderer feature: '%s'", feature_name.c_str());
            return -EINVAL;
        }

        const std::string& feature_status = parts[1];
        if (feature_status != "enabled" && feature_status != "disabled") {
            stream_renderer_error("Error: invalid option %s for renderer feature: %s",
                                  feature_status.c_str(), feature_name.c_str());
            return -EINVAL;
        }

        auto& feature_info = feature_it->second;
        feature_info->enabled = feature_status == "enabled";
        feature_info->reason = "Overridden via STREAM_RENDERER_PARAM_RENDERER_FEATURES";

        stream_renderer_info("Gfxstream feature %s %s", feature_name.c_str(),
                              feature_status.c_str());
    }

    if (features.SystemBlob.enabled) {
        if (!features.ExternalBlob.enabled) {
            stream_renderer_error("The SystemBlob features requires the ExternalBlob feature.");
            return -EINVAL;
        }
#ifndef _WIN32
        stream_renderer_warn("Warning: USE_SYSTEM_BLOB has only been tested on Windows");
#endif
    }
    if (features.VulkanNativeSwapchain.enabled && !features.Vulkan.enabled) {
        stream_renderer_error("can't enable vulkan native swapchain, Vulkan is disabled");
        return -EINVAL;
    }

    return 0;
}

}  // namespace

VG_EXPORT int stream_renderer_init(struct stream_renderer_param* stream_renderer_params,
                                   uint64_t num_params) {
    // Required parameters.
    std::unordered_set<uint64_t> required_params{STREAM_RENDERER_PARAM_USER_DATA,
                                                 STREAM_RENDERER_PARAM_RENDERER_FLAGS,
                                                 STREAM_RENDERER_PARAM_FENCE_CALLBACK};

    // String names of the parameters.
    std::unordered_map<uint64_t, std::string> param_strings{
        {STREAM_RENDERER_PARAM_USER_DATA, "USER_DATA"},
        {STREAM_RENDERER_PARAM_RENDERER_FLAGS, "RENDERER_FLAGS"},
        {STREAM_RENDERER_PARAM_FENCE_CALLBACK, "FENCE_CALLBACK"},
        {STREAM_RENDERER_PARAM_WIN0_WIDTH, "WIN0_WIDTH"},
        {STREAM_RENDERER_PARAM_WIN0_HEIGHT, "WIN0_HEIGHT"},
        {STREAM_RENDERER_PARAM_DEBUG_CALLBACK, "DEBUG_CALLBACK"},
        {STREAM_RENDERER_SKIP_OPENGLES_INIT, "SKIP_OPENGLES_INIT"},
        {STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_INSTANT_EVENT,
         "METRICS_CALLBACK_ADD_INSTANT_EVENT"},
        {STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_INSTANT_EVENT_WITH_DESCRIPTOR,
         "METRICS_CALLBACK_ADD_INSTANT_EVENT_WITH_DESCRIPTOR"},
        {STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_INSTANT_EVENT_WITH_METRIC,
         "METRICS_CALLBACK_ADD_INSTANT_EVENT_WITH_METRIC"},
        {STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_VULKAN_OUT_OF_MEMORY_EVENT,
         "METRICS_CALLBACK_ADD_VULKAN_OUT_OF_MEMORY_EVENT"},
        {STREAM_RENDERER_PARAM_METRICS_CALLBACK_SET_ANNOTATION, "METRICS_CALLBACK_SET_ANNOTATION"},
        {STREAM_RENDERER_PARAM_METRICS_CALLBACK_ABORT, "METRICS_CALLBACK_ABORT"}};

    // Print full values for these parameters:
    // Values here must not be pointers (e.g. callback functions), to avoid potentially identifying
    // someone via ASLR. Pointers in ASLR are randomized on boot, which means pointers may be
    // different between users but similar across a single user's sessions.
    // As a convenience, any value <= 4096 is also printed, to catch small or null pointer errors.
    std::unordered_set<uint64_t> printed_param_values{STREAM_RENDERER_PARAM_RENDERER_FLAGS,
                                                      STREAM_RENDERER_PARAM_WIN0_WIDTH,
                                                      STREAM_RENDERER_PARAM_WIN0_HEIGHT};

    // We may have unknown parameters, so this function is lenient.
    auto get_param_string = [&](uint64_t key) -> std::string {
        auto param_string = param_strings.find(key);
        if (param_string != param_strings.end()) {
            return param_string->second;
        } else {
            return "Unknown param with key=" + std::to_string(key);
        }
    };

    // Initialization data.
    uint32_t display_width = 0;
    uint32_t display_height = 0;
    void* renderer_cookie = nullptr;
    int renderer_flags = 0;
    std::string renderer_features_str;
    stream_renderer_fence_callback fence_callback = nullptr;
    bool skip_opengles = false;

    // Iterate all parameters that we support.
    stream_renderer_debug("Reading stream renderer parameters:");
    for (uint64_t i = 0; i < num_params; ++i) {
        stream_renderer_param& param = stream_renderer_params[i];

        // Print out parameter we are processing. See comment above `printed_param_values` before
        // adding new prints.
        if (printed_param_values.find(param.key) != printed_param_values.end() ||
            param.value <= 4096) {
            stream_renderer_debug("%s - %llu", get_param_string(param.key).c_str(),
                                  static_cast<unsigned long long>(param.value));
        } else {
            // If not full value, print that it was passed.
            stream_renderer_debug("%s", get_param_string(param.key).c_str());
        }

        // Removing every param we process will leave required_params empty if all provided.
        required_params.erase(param.key);

        switch (param.key) {
            case STREAM_RENDERER_PARAM_NULL:
                break;
            case STREAM_RENDERER_PARAM_USER_DATA: {
                renderer_cookie = reinterpret_cast<void*>(static_cast<uintptr_t>(param.value));
                globalUserData = renderer_cookie;
                break;
            }
            case STREAM_RENDERER_PARAM_RENDERER_FLAGS: {
                renderer_flags = static_cast<int>(param.value);
                break;
            }
            case STREAM_RENDERER_PARAM_FENCE_CALLBACK: {
                fence_callback = reinterpret_cast<stream_renderer_fence_callback>(
                    static_cast<uintptr_t>(param.value));
                break;
            }
            case STREAM_RENDERER_PARAM_WIN0_WIDTH: {
                display_width = static_cast<uint32_t>(param.value);
                break;
            }
            case STREAM_RENDERER_PARAM_WIN0_HEIGHT: {
                display_height = static_cast<uint32_t>(param.value);
                break;
            }
            case STREAM_RENDERER_PARAM_DEBUG_CALLBACK: {
                globalDebugCallback = reinterpret_cast<stream_renderer_debug_callback>(
                    static_cast<uintptr_t>(param.value));
                break;
            }
            case STREAM_RENDERER_SKIP_OPENGLES_INIT: {
                skip_opengles = static_cast<bool>(param.value);
                break;
            }
            case STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_INSTANT_EVENT: {
                MetricsLogger::add_instant_event_callback =
                    reinterpret_cast<stream_renderer_param_metrics_callback_add_instant_event>(
                        static_cast<uintptr_t>(param.value));
                break;
            }
            case STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_INSTANT_EVENT_WITH_DESCRIPTOR: {
                MetricsLogger::add_instant_event_with_descriptor_callback = reinterpret_cast<
                    stream_renderer_param_metrics_callback_add_instant_event_with_descriptor>(
                    static_cast<uintptr_t>(param.value));
                break;
            }
            case STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_INSTANT_EVENT_WITH_METRIC: {
                MetricsLogger::add_instant_event_with_metric_callback = reinterpret_cast<
                    stream_renderer_param_metrics_callback_add_instant_event_with_metric>(
                    static_cast<uintptr_t>(param.value));
                break;
            }
            case STREAM_RENDERER_PARAM_METRICS_CALLBACK_ADD_VULKAN_OUT_OF_MEMORY_EVENT: {
                MetricsLogger::add_vulkan_out_of_memory_event = reinterpret_cast<
                    stream_renderer_param_metrics_callback_add_vulkan_out_of_memory_event>(
                    static_cast<uintptr_t>(param.value));
                break;
            }
            case STREAM_RENDERER_PARAM_RENDERER_FEATURES: {
                renderer_features_str =
                    std::string(reinterpret_cast<const char*>(static_cast<uintptr_t>(param.value)));
                break;
            }
            case STREAM_RENDERER_PARAM_METRICS_CALLBACK_SET_ANNOTATION: {
                MetricsLogger::set_crash_annotation_callback =
                    reinterpret_cast<stream_renderer_param_metrics_callback_set_annotation>(
                        static_cast<uintptr_t>(param.value));
                break;
            }
            case STREAM_RENDERER_PARAM_METRICS_CALLBACK_ABORT: {
                emugl::setDieFunction(
                    reinterpret_cast<stream_renderer_param_metrics_callback_abort>(
                        static_cast<uintptr_t>(param.value)));
                break;
            }
            default: {
                // We skip any parameters we don't recognize.
                stream_renderer_error(
                    "Skipping unknown parameter key: %llu. May need to upgrade gfxstream.",
                    static_cast<unsigned long long>(param.key));
                break;
            }
        }
    }
    stream_renderer_debug("Finished reading parameters");

    // Some required params not found.
    if (required_params.size() > 0) {
        stream_renderer_error("Missing required parameters:");
        for (uint64_t param : required_params) {
            stream_renderer_error("%s", get_param_string(param).c_str());
        }
        stream_renderer_error("Failing initialization intentionally");
        return -EINVAL;
    }

#if GFXSTREAM_UNSTABLE_VULKAN_EXTERNAL_SYNC
    renderer_flags |= STREAM_RENDERER_FLAGS_VULKAN_EXTERNAL_SYNC;
#endif

    gfxstream::host::FeatureSet features;
    int ret = parseGfxstreamFeatures(renderer_flags, renderer_features_str, features);
    if (ret) {
        stream_renderer_error("Failed to initialize: failed to parse Gfxstream features.");
        return ret;
    }

    stream_renderer_info("Gfxstream features:");
    for (const auto& [_, featureInfo] : features.map) {
        stream_renderer_info("    %s: %s (%s)", featureInfo->name.c_str(),
                             (featureInfo->enabled ? "enabled" : "disabled"),
                             featureInfo->reason.c_str());
    }

    gfxstream::host::InitializeTracing();

    // Set non product-specific callbacks
    gfxstream::vk::vk_util::setVkCheckCallbacks(
        std::make_unique<gfxstream::vk::vk_util::VkCheckCallbacks>(
            gfxstream::vk::vk_util::VkCheckCallbacks{
                .onVkErrorDeviceLost =
                    []() {
                        auto fb = gfxstream::FrameBuffer::getFB();
                        if (!fb) {
                            ERR("FrameBuffer not yet initialized. Dropping device lost event");
                            return;
                        }
                        fb->logVulkanDeviceLost();
                    },
                .onVkErrorOutOfMemory =
                    [](VkResult result, const char* function, int line) {
                        auto fb = gfxstream::FrameBuffer::getFB();
                        if (!fb) {
                            stream_renderer_error(
                                "FrameBuffer not yet initialized. Dropping out of memory event");
                            return;
                        }
                        fb->logVulkanOutOfMemory(result, function, line);
                    },
                .onVkErrorOutOfMemoryOnAllocation =
                    [](VkResult result, const char* function, int line,
                       std::optional<uint64_t> allocationSize) {
                        auto fb = gfxstream::FrameBuffer::getFB();
                        if (!fb) {
                            stream_renderer_error(
                                "FrameBuffer not yet initialized. Dropping out of memory event");
                            return;
                        }
                        fb->logVulkanOutOfMemory(result, function, line, allocationSize);
                    }}));

    if (!skip_opengles) {
        // aemu currently does its own opengles initialization in
        // qemu/android/android-emu/android/opengles.cpp.
        ret =
            stream_renderer_opengles_init(display_width, display_height, renderer_flags, features);
        if (ret) {
            return ret;
        }
    }

    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_init()");

    sFrontend()->init(renderer_cookie, features, fence_callback);
    gfxstream::FrameBuffer::waitUntilInitialized();

    stream_renderer_info("Gfxstream initialized successfully!");
    return 0;
}

VG_EXPORT void gfxstream_backend_setup_window(void* native_window_handle, int32_t window_x,
                                              int32_t window_y, int32_t window_width,
                                              int32_t window_height, int32_t fb_width,
                                              int32_t fb_height) {
    android_showOpenglesWindow(native_window_handle, window_x, window_y, window_width,
                               window_height, fb_width, fb_height, 1.0f, 0, false, false);
}

VG_EXPORT void stream_renderer_teardown() {
    sFrontend()->teardown();

    android_finishOpenglesRenderer();
    android_hideOpenglesWindow();
    android_stopOpenglesRenderer(true);

    stream_renderer_info("Gfxstream shut down completed!");
}

VG_EXPORT void gfxstream_backend_set_screen_mask(int width, int height,
                                                 const unsigned char* rgbaData) {
    android_setOpenglesScreenMask(width, height, rgbaData);
}

const GoldfishPipeServiceOps* goldfish_pipe_get_service_ops() { return &goldfish_pipe_service_ops; }

static_assert(sizeof(struct stream_renderer_device_id) == 32,
              "stream_renderer_device_id must be 32 bytes");
static_assert(offsetof(struct stream_renderer_device_id, device_uuid) == 0,
              "stream_renderer_device_id.device_uuid must be at offset 0");
static_assert(offsetof(struct stream_renderer_device_id, driver_uuid) == 16,
              "stream_renderer_device_id.driver_uuid must be at offset 16");

static_assert(sizeof(struct stream_renderer_vulkan_info) == 36,
              "stream_renderer_vulkan_info must be 36 bytes");
static_assert(offsetof(struct stream_renderer_vulkan_info, memory_index) == 0,
              "stream_renderer_vulkan_info.memory_index must be at offset 0");
static_assert(offsetof(struct stream_renderer_vulkan_info, device_id) == 4,
              "stream_renderer_vulkan_info.device_id must be at offset 4");

static_assert(sizeof(struct stream_renderer_param_host_visible_memory_mask_entry) == 36,
              "stream_renderer_param_host_visible_memory_mask_entry must be 36 bytes");
static_assert(offsetof(struct stream_renderer_param_host_visible_memory_mask_entry, device_id) == 0,
              "stream_renderer_param_host_visible_memory_mask_entry.device_id must be at offset 0");
static_assert(
    offsetof(struct stream_renderer_param_host_visible_memory_mask_entry, memory_type_mask) == 32,
    "stream_renderer_param_host_visible_memory_mask_entry.memory_type_mask must be at offset 32");

static_assert(sizeof(struct stream_renderer_param_host_visible_memory_mask) == 16,
              "stream_renderer_param_host_visible_memory_mask must be 16 bytes");
static_assert(offsetof(struct stream_renderer_param_host_visible_memory_mask, entries) == 0,
              "stream_renderer_param_host_visible_memory_mask.entries must be at offset 0");
static_assert(offsetof(struct stream_renderer_param_host_visible_memory_mask, num_entries) == 8,
              "stream_renderer_param_host_visible_memory_mask.num_entries must be at offset 8");

static_assert(sizeof(struct stream_renderer_param) == 16, "stream_renderer_param must be 16 bytes");
static_assert(offsetof(struct stream_renderer_param, key) == 0,
              "stream_renderer_param.key must be at offset 0");
static_assert(offsetof(struct stream_renderer_param, value) == 8,
              "stream_renderer_param.value must be at offset 8");

#ifdef CONFIG_AEMU

VG_EXPORT void stream_renderer_set_service_ops(const GoldfishPipeServiceOps* ops) {
    sFrontend()->setServiceOps(ops);
}

#endif  // CONFIG_AEMU

}  // extern "C"
