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

#include <cstdint>

#include "FrameBuffer.h"
#include "GfxStreamAgents.h"
#include "VirtioGpuFrontend.h"
#include "aemu/base/Metrics.h"
#include "aemu/base/system/System.h"
#include "gfxstream/Strings.h"
#include "gfxstream/host/Features.h"
#include "gfxstream/host/Tracing.h"
#include "gfxstream/host/logging.h"
#include "host-common/FeatureControl.h"
#include "host-common/GraphicsAgentFactory.h"
#include "host-common/android_pipe_common.h"
#include "host-common/android_pipe_device.h"
#include "host-common/globals.h"
#include "host-common/opengles-pipe.h"
#include "host-common/opengles.h"
#include "host-common/refcount-pipe.h"
#include "host-common/vm_operations.h"
#include "render-utils/RenderLib.h"
#include "vk_util.h"
#include "vulkan/VulkanDispatch.h"

extern "C" {
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "host-common/goldfish_pipe.h"
}  // extern "C"

using android::AndroidPipe;
using android::base::MetricsLogger;
using gfxstream::host::LogLevel;
using gfxstream::host::VirtioGpuFrontend;

static VirtioGpuFrontend* sFrontend() {
    static VirtioGpuFrontend* p = new VirtioGpuFrontend;
    return p;
}

extern "C" {

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
    GFXSTREAM_ERROR("Snapshot save requested without support.");
    return -EINVAL;
#endif
}

VG_EXPORT int stream_renderer_restore(const char* dir) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_restore()");

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    return sFrontend()->restore(dir);
#else
    GFXSTREAM_ERROR("Snapshot save requested without support.");
    return -EINVAL;
#endif
}

VG_EXPORT int stream_renderer_resume() {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_resume()");

    // TODO: move resumeAll() here after kumquat updated.

    return 0;
}

static int stream_renderer_opengles_init(uint32_t display_width, uint32_t display_height,
                                         int renderer_flags, const gfxstream::host::FeatureSet& features) {
    GFXSTREAM_DEBUG("start. display dimensions: width %u height %u, renderer flags: 0x%x",
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

    GFXSTREAM_INFO("GL strings; [%s] [%s] [%s].", vendor, renderer, version);

    auto openglesRenderer = android_getOpenglesRenderer();

    if (!openglesRenderer) {
        GFXSTREAM_ERROR("No renderer started, fatal");
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
            GFXSTREAM_ERROR("Error: invalid renderer features: %s", renderer_features.c_str());
            return -EINVAL;
        }

        const std::string& feature_name = parts[0];

        auto feature_it = features.map.find(feature_name);
        if (feature_it == features.map.end()) {
            GFXSTREAM_ERROR("Error: invalid renderer feature: '%s'", feature_name.c_str());
            return -EINVAL;
        }

        const std::string& feature_status = parts[1];
        if (feature_status != "enabled" && feature_status != "disabled") {
            GFXSTREAM_ERROR("Error: invalid option %s for renderer feature: %s",
                            feature_status.c_str(), feature_name.c_str());
            return -EINVAL;
        }

        auto& feature_info = feature_it->second;
        feature_info->enabled = feature_status == "enabled";
        feature_info->reason = "Overridden via STREAM_RENDERER_PARAM_RENDERER_FEATURES";

        GFXSTREAM_INFO("Gfxstream feature %s %s", feature_name.c_str(), feature_status.c_str());
    }

    if (features.SystemBlob.enabled) {
        if (!features.ExternalBlob.enabled) {
            GFXSTREAM_ERROR("The SystemBlob features requires the ExternalBlob feature.");
            return -EINVAL;
        }
#ifndef _WIN32
        GFXSTREAM_WARNING("Warning: USE_SYSTEM_BLOB has only been tested on Windows");
#endif
    }
    if (features.VulkanNativeSwapchain.enabled && !features.Vulkan.enabled) {
        GFXSTREAM_ERROR("can't enable vulkan native swapchain, Vulkan is disabled");
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
    stream_renderer_debug_callback log_callback = nullptr;
    bool skip_opengles = false;

    // Iterate all parameters that we support.
    GFXSTREAM_DEBUG("Reading stream renderer parameters:");
    for (uint64_t i = 0; i < num_params; ++i) {
        stream_renderer_param& param = stream_renderer_params[i];

        // Print out parameter we are processing. See comment above `printed_param_values` before
        // adding new prints.
        if (printed_param_values.find(param.key) != printed_param_values.end() ||
            param.value <= 4096) {
            GFXSTREAM_DEBUG("%s - %llu", get_param_string(param.key).c_str(),
                            static_cast<unsigned long long>(param.value));
        } else {
            // If not full value, print that it was passed.
            GFXSTREAM_DEBUG("%s", get_param_string(param.key).c_str());
        }

        // Removing every param we process will leave required_params empty if all provided.
        required_params.erase(param.key);

        switch (param.key) {
            case STREAM_RENDERER_PARAM_NULL:
                break;
            case STREAM_RENDERER_PARAM_USER_DATA: {
                renderer_cookie = reinterpret_cast<void*>(static_cast<uintptr_t>(param.value));
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
                log_callback = reinterpret_cast<stream_renderer_debug_callback>(
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
                GFXSTREAM_ERROR(
                    "Skipping unknown parameter key: %llu. May need to upgrade gfxstream.",
                    static_cast<unsigned long long>(param.key));
                break;
            }
        }
    }

    if (log_callback) {
        gfxstream::host::SetGfxstreamLogCallback([log_callback, log_user_data = renderer_cookie](
                                                     LogLevel level, const char* file, int line,
                                                     const char* function, const char* message) {
            const std::string formatted =
                GetDefaultFormattedLog(level, file, line, function, message);

            stream_renderer_debug log_info = {
                .message = formatted.c_str(),
            };

            switch (level) {
                case LogLevel::kFatal: {
                    log_info.debug_type = STREAM_RENDERER_DEBUG_ERROR;
                    break;
                }
                case LogLevel::kError: {
                    log_info.debug_type = STREAM_RENDERER_DEBUG_ERROR;
                    break;
                }
                case LogLevel::kWarning: {
                    log_info.debug_type = STREAM_RENDERER_DEBUG_WARN;
                    break;
                }
                case LogLevel::kInfo: {
                    log_info.debug_type = STREAM_RENDERER_DEBUG_INFO;
                    break;
                }
                case LogLevel::kDebug: {
                    log_info.debug_type = STREAM_RENDERER_DEBUG_DEBUG;
                    break;
                }
                case LogLevel::kVerbose: {
                    log_info.debug_type = STREAM_RENDERER_DEBUG_DEBUG;
                    break;
                }
            }

            log_callback(log_user_data, &log_info);
        });
    }

    GFXSTREAM_DEBUG("Finished reading parameters");

    // Some required params not found.
    if (required_params.size() > 0) {
        GFXSTREAM_ERROR("Missing required parameters:");
        for (uint64_t param : required_params) {
            GFXSTREAM_ERROR("%s", get_param_string(param).c_str());
        }
        GFXSTREAM_ERROR("Failing initialization intentionally");
        return -EINVAL;
    }

#if GFXSTREAM_UNSTABLE_VULKAN_EXTERNAL_SYNC
    renderer_flags |= STREAM_RENDERER_FLAGS_VULKAN_EXTERNAL_SYNC;
#endif

    gfxstream::host::FeatureSet features;
    if (skip_opengles) {
        features = gfxstream::FrameBuffer::getFB()->getFeatures();
    } else {
        int ret = parseGfxstreamFeatures(renderer_flags, renderer_features_str, features);
        if (ret) {
            GFXSTREAM_ERROR("Failed to initialize: failed to parse Gfxstream features.");
            return ret;
        }
    }

    GFXSTREAM_INFO("Gfxstream features:");
    for (const auto& [_, featureInfo] : features.map) {
        GFXSTREAM_INFO("    %s: %s (%s)", featureInfo->name.c_str(),
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
                            GFXSTREAM_ERROR(
                                "FrameBuffer not yet initialized. Dropping device lost event");
                            return;
                        }
                        fb->logVulkanDeviceLost();
                    },
                .onVkErrorOutOfMemory =
                    [](VkResult result, const char* function, int line) {
                        auto fb = gfxstream::FrameBuffer::getFB();
                        if (!fb) {
                            GFXSTREAM_ERROR(
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
                            GFXSTREAM_ERROR(
                                "FrameBuffer not yet initialized. Dropping out of memory event");
                            return;
                        }
                        fb->logVulkanOutOfMemory(result, function, line, allocationSize);
                    }}));

    if (!skip_opengles) {
        // aemu currently does its own opengles initialization in
        // qemu/android/android-emu/android/opengles.cpp.
        auto ret =
            stream_renderer_opengles_init(display_width, display_height, renderer_flags, features);
        if (ret) {
            return ret;
        }
    }

    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY, "stream_renderer_init()");

    sFrontend()->init(renderer_cookie, features, fence_callback);
    gfxstream::FrameBuffer::waitUntilInitialized();

    GFXSTREAM_INFO("Gfxstream initialized successfully!");
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

    GFXSTREAM_INFO("Gfxstream shut down completed!");
}

VG_EXPORT void gfxstream_backend_set_screen_mask(int width, int height,
                                                 const unsigned char* rgbaData) {
    android_setOpenglesScreenMask(width, height, rgbaData);
}

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

}  // extern "C"
