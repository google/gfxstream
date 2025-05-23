// Copyright (C) 2018 The Android Open Source Project
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
#ifndef __COMMON_EMULATOR_FEATURE_INFO_H
#define __COMMON_EMULATOR_FEATURE_INFO_H

// SyncImpl determines the presence of host/guest OpenGL fence sync
// capabilities. It corresponds exactly to EGL_ANDROID_native_fence_sync
// capability, but for the emulator, we need to make sure that
// OpenGL pipe protocols match, so we use a special extension name
// here.
// SYNC_IMPL_NONE means that the native fence sync capability is
// not present, and we will end up using the equivalent of glFinish
// in order to preserve buffer swapping order.
// SYNC_IMPL_NATIVE_SYNC means that we do have native fence sync
// capability, and we will use a fence fd to synchronize buffer swaps.
enum SyncImpl {
    SYNC_IMPL_NONE = 0,
    SYNC_IMPL_NATIVE_SYNC_V2 = 1, // ANDROID_native_fence_sync
    SYNC_IMPL_NATIVE_SYNC_V3 = 2, // KHR_wait_sync
    SYNC_IMPL_NATIVE_SYNC_V4 = 3, // Correct eglGetSyncAttribKHR
};

// Interface for native sync:
// Use the highest that shows up
static const char kRCNativeSyncV2[] = "ANDROID_EMU_native_sync_v2";
static const char kRCNativeSyncV3[] = "ANDROID_EMU_native_sync_v3";
static const char kRCNativeSyncV4[] = "ANDROID_EMU_native_sync_v4";

// DMA for OpenGL
enum DmaImpl {
    DMA_IMPL_NONE = 0,
    DMA_IMPL_v1 = 1,
};

static const char kDmaExtStr_v1[] = "ANDROID_EMU_dma_v1";

// OpenGL ES max supported version
enum GLESMaxVersion {
    GLES_MAX_VERSION_2 = 0,
    GLES_MAX_VERSION_3_0 = 1,
    GLES_MAX_VERSION_3_1 = 2,
    GLES_MAX_VERSION_3_2 = 3,
};

static const char kGLESMaxVersion_2[] = "ANDROID_EMU_gles_max_version_2";
static const char kGLESMaxVersion_3_0[] = "ANDROID_EMU_gles_max_version_3_0";
static const char kGLESMaxVersion_3_1[] = "ANDROID_EMU_gles_max_version_3_1";
static const char kGLESMaxVersion_3_2[] = "ANDROID_EMU_gles_max_version_3_2";

enum HostComposition {
    HOST_COMPOSITION_NONE = 0,
    HOST_COMPOSITION_V1,
    HOST_COMPOSITION_V2,
};

static const char kHostCompositionV1[] = "ANDROID_EMU_host_composition_v1";
static const char kHostCompositionV2[] = "ANDROID_EMU_host_composition_v2";

// No querying errors from host extension
static const char kGLESNoHostError[] = "ANDROID_EMU_gles_no_host_error";

// Host to guest memory mapping
static const char kGLDirectMem[] = "ANDROID_EMU_direct_mem";

// Vulkan host support
// To be delivered/enabled when at least the following is working/available:
// - HOST_COHERENT memory mapping
// - Full gralloc interop: External memory, AHB
static const char kVulkan[] = "ANDROID_EMU_vulkan";

// Deferred Vulkan commands
static const char kDeferredVulkanCommands[] = "ANDROID_EMU_deferred_vulkan_commands";

// Vulkan null optional strings
static const char kVulkanNullOptionalStrings[] = "ANDROID_EMU_vulkan_null_optional_strings";

// Vulkan create resources with requirements
static const char kVulkanCreateResourcesWithRequirements[] = "ANDROID_EMU_vulkan_create_resources_with_requirements";

// Vulkan ignored handles
static const char kVulkanIgnoredHandles[] = "ANDROID_EMU_vulkan_ignored_handles";

// YUV host cache
static const char kYUVCache[] = "ANDROID_EMU_YUV_Cache";

// GL protocol v2
static const char kAsyncUnmapBuffer[] = "ANDROID_EMU_async_unmap_buffer";

// virtio-gpu-next
static const char kVirtioGpuNext[] = "ANDROID_EMU_virtio_gpu_next";

static const char kHasSharedSlotsHostMemoryAllocator[] = "ANDROID_EMU_has_shared_slots_host_memory_allocator";

// Vulkan free memory sync
static const char kVulkanFreeMemorySync[] = "ANDROID_EMU_vulkan_free_memory_sync";

// virtio-gpu syncfd support
static const char kVirtioGpuNativeSync[] = "ANDROID_EMU_virtio_gpu_native_sync";

// Vulkan extension that required a protocol update (new marshaling structs)
static const char kVulkanShaderFloat16Int8[] = "ANDROID_EMU_vulkan_shader_float16_int8";

// Vulkan async queue submit
static const char kVulkanAsyncQueueSubmit[] = "ANDROID_EMU_vulkan_async_queue_submit";

// A flag to _not_ ignore host opengl errors (now host opengl errors are ignored by default)
static const char kGLESUseHostError[] = "ANDROID_EMU_gles_use_host_error";

// Host side tracing
static const char kHostSideTracing[] = "ANDROID_EMU_host_side_tracing";

// Make current async
static const char kAsyncFrameCommands[] = "ANDROID_EMU_async_frame_commands";

// Queue submit with commands
static const char kVulkanQueueSubmitWithCommands[] = "ANDROID_EMU_vulkan_queue_submit_with_commands";
//
// Synchronized glBufferData call
static const char kSyncBufferData[] = "ANDROID_EMU_sync_buffer_data";

// Batched descriptor set update
static const char kVulkanBatchedDescriptorSetUpdate[] = "ANDROID_EMU_vulkan_batched_descriptor_set_update";

// Async QSRI
static const char kVulkanAsyncQsri[] = "ANDROID_EMU_vulkan_async_qsri";

// DMA for readback
static const char kReadColorBufferDma[] = "ANDROID_EMU_read_color_buffer_dma";

// HWC multiple display configs
static const char kHWCMultiConfigs[] = "ANDROID_EMU_hwc_multi_configs";

// Vulkan auxiliary command memory
static const char kVulkanAuxCommandMemory[] = "ANDROID_EMU_vulkan_aux_command_memory";

// Struct describing available emulator features
struct EmulatorFeatureInfo {

    EmulatorFeatureInfo() :
        syncImpl(SYNC_IMPL_NONE),
        dmaImpl(DMA_IMPL_NONE),
        hostComposition(HOST_COMPOSITION_NONE),
        glesMaxVersion(GLES_MAX_VERSION_2),
        hasDirectMem(false),
        hasVulkan(false),
        hasDeferredVulkanCommands(false),
        hasVulkanNullOptionalStrings(false),
        hasVulkanCreateResourcesWithRequirements(false),
        hasVulkanIgnoredHandles(false),
        hasYUVCache (false),
        hasAsyncUnmapBuffer (false),
        hasVirtioGpuNext (false),
        hasSharedSlotsHostMemoryAllocator(false),
        hasVulkanFreeMemorySync(false),
        hasVirtioGpuNativeSync(false),
        hasVulkanShaderFloat16Int8(false),
        hasVulkanAsyncQueueSubmit(false),
        hasHostSideTracing(false),
        hasAsyncFrameCommands(false),
        hasVulkanQueueSubmitWithCommands(false),
        hasVulkanBatchedDescriptorSetUpdate(false),
        hasSyncBufferData(false),
        hasVulkanAsyncQsri(false),
        hasReadColorBufferDma(false),
        hasHWCMultiConfigs(false),
        hasVulkanAuxCommandMemory(false)
    { }

    SyncImpl syncImpl;
    DmaImpl dmaImpl;
    HostComposition hostComposition;
    GLESMaxVersion glesMaxVersion;
    bool hasDirectMem;
    bool hasVulkan;
    bool hasDeferredVulkanCommands;
    bool hasVulkanNullOptionalStrings;
    bool hasVulkanCreateResourcesWithRequirements;
    bool hasVulkanIgnoredHandles;
    bool hasYUVCache;
    bool hasAsyncUnmapBuffer;
    bool hasVirtioGpuNext;
    bool hasSharedSlotsHostMemoryAllocator;
    bool hasVulkanFreeMemorySync;
    bool hasVirtioGpuNativeSync;
    bool hasVulkanShaderFloat16Int8;
    bool hasVulkanAsyncQueueSubmit;
    bool hasHostSideTracing;
    bool hasAsyncFrameCommands;
    bool hasVulkanQueueSubmitWithCommands;
    bool hasVulkanBatchedDescriptorSetUpdate;
    bool hasSyncBufferData;
    bool hasVulkanAsyncQsri;
    bool hasReadColorBufferDma;
    bool hasHWCMultiConfigs;
    bool hasVulkanAuxCommandMemory; // This feature tracks if vulkan command buffers should be stored in an auxiliary shared memory
};

// This should be ABI identical with the variant in ResourceTracker.h
struct EmulatorGfxStreamVkFeatureInfo {
    bool hasDirectMem;
    bool hasVulkan;
    bool hasDeferredVulkanCommands;
    bool hasVulkanNullOptionalStrings;
    bool hasVulkanCreateResourcesWithRequirements;
    bool hasVulkanIgnoredHandles;
    bool hasVirtioGpuNext;
    bool hasVulkanFreeMemorySync;
    bool hasVirtioGpuNativeSync;
    bool hasVulkanShaderFloat16Int8;
    bool hasVulkanAsyncQueueSubmit;
    bool hasVulkanQueueSubmitWithCommands;
    bool hasVulkanBatchedDescriptorSetUpdate;
    bool hasVulkanAsyncQsri;
    bool hasVulkanAuxCommandMemory;
    bool setupComplete;
};

#endif // __COMMON_EMULATOR_FEATURE_INFO_H
