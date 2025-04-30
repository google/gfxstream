/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "RenderControl.h"

#include <inttypes.h>
#include <string.h>

#include <atomic>
#include <limits>
#include <memory>

#include "ChecksumCalculatorThreadInfo.h"
#include "FrameBuffer.h"
#include "GLESVersionDetector.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "RenderThreadInfo.h"
#include "RenderThreadInfoGl.h"
#include "RenderThreadInfoVk.h"
#include "SyncThread.h"
#include "gfxstream/Tracing.h"
#include "compressedTextureFormats/AstcCpuDecompressor.h"
#include "gfxstream/host/logging.h"
#include "gfxstream/host/guest_operations.h"
#include "gfxstream/host/renderer_operations.h"
#include "gfxstream/host/sync_device.h"
#include "gfxstream/host/Tracing.h"
#include "vulkan/VkCommonOperations.h"
#include "vulkan/VkDecoderGlobalState.h"

namespace gfxstream {

using gfxstream::base::AutoLock;
using gfxstream::base::Lock;
using gl::EmulatedEglFenceSync;
using gl::GLES_DISPATCH_MAX_VERSION_2;
using gl::GLES_DISPATCH_MAX_VERSION_3_0;
using gl::GLES_DISPATCH_MAX_VERSION_3_1;
using gl::GLESApi;
using gl::GLESApi_CM;
using gl::GLESDispatchMaxVersion;
using gl::RenderThreadInfoGl;

#define DEBUG 0
#define DEBUG_GRALLOC_SYNC 0
#define DEBUG_EGL_SYNC 0

#define RENDERCONTROL_DPRINT(...)         \
    do {                                  \
        if (DEBUG) {                      \
            fprintf(stderr, __VA_ARGS__); \
        }                                 \
    } while (0)

#if DEBUG_GRALLOC_SYNC
#define GRSYNC_DPRINT RENDERCONTROL_DPRINT
#else
#define GRSYNC_DPRINT(...)
#endif

#if DEBUG_EGL_SYNC
#define EGLSYNC_DPRINT RENDERCONTROL_DPRINT
#else
#define EGLSYNC_DPRINT(...)
#endif

// GrallocSync is a class that helps to reflect the behavior of
// gralloc_lock/gralloc_unlock on the guest.
// If we don't use this, apps that use gralloc buffers (such as webcam)
// will have out-of-order frames,
// as GL calls from different threads in the guest
// are allowed to arrive at the host in any ordering.
class GrallocSync {
public:
    GrallocSync() {
        // Having in-order webcam frames is nice, but not at the cost
        // of potential deadlocks;
        // we need to be careful of what situations in which
        // we actually lock/unlock the gralloc color buffer.
        //
        // To avoid deadlock:
        // we require rcColorBufferCacheFlush to be called
        // whenever gralloc_lock is called on the guest,
        // and we require rcUpdateWindowColorBuffer to be called
        // whenever gralloc_unlock is called on the guest.
        //
        // Some versions of the system image optimize out
        // the call to rcUpdateWindowColorBuffer in the case of zero
        // width/height, but since we're using that as synchronization,
        // that lack of calling can lead to a deadlock on the host
        // in many situations
        // (switching camera sides, exiting benchmark apps, etc).
        // So, we put GrallocSync under the feature control.
        mEnabled = FrameBuffer::getFB()->getFeatures().GrallocSync.enabled;

        // There are two potential tricky situations to handle:
        // a. Multiple users of gralloc buffers that all want to
        // call gralloc_lock. This is obeserved to happen on older APIs
        // (<= 19).
        // b. The pipe doesn't have to preserve ordering of the
        // gralloc_lock and gralloc_unlock commands themselves.
        //
        // To handle a), notice the situation is one of one type of user
        // needing multiple locks that needs to exclude concurrent use
        // by another type of user. This maps well to a read/write lock,
        // where gralloc_lock and gralloc_unlock users are readers
        // and rcFlushWindowColorBuffer is the writer.
        // From the perspective of the host preparing and posting
        // buffers, these are indeed read/write operations.
        //
        // To handle b), we give up on locking when the state is observed
        // to be bad. lockState tracks how many color buffer locks there are.
        // If lockState < 0, it means we definitely have an unlock before lock
        // sort of situation, and should give up.
        lockState = 0;
    }

    // lockColorBufferPrepare is designed to handle
    // gralloc_lock/unlock requests, and uses the read lock.
    // When rcFlushWindowColorBuffer is called (when frames are posted),
    // we use the write lock (see GrallocSyncPostLock).
    void lockColorBufferPrepare() {
        int newLockState = ++lockState;
        if (mEnabled && newLockState == 1) {
            mGrallocColorBufferLock.lockRead();
        } else if (mEnabled) {
            GRSYNC_DPRINT("warning: recursive/multiple locks from guest!");
        }
    }
    void unlockColorBufferPrepare() {
        int newLockState = --lockState;
        if (mEnabled && newLockState == 0) mGrallocColorBufferLock.unlockRead();
    }
    gfxstream::base::ReadWriteLock mGrallocColorBufferLock;
private:
    bool mEnabled;
    std::atomic<int> lockState;
    DISALLOW_COPY_ASSIGN_AND_MOVE(GrallocSync);
};

class GrallocSyncPostLock : public gfxstream::base::AutoWriteLock {
public:
    GrallocSyncPostLock(GrallocSync& grallocsync) :
        gfxstream::base::AutoWriteLock(grallocsync.mGrallocColorBufferLock) { }
};

static GrallocSync* sGrallocSync() {
    static GrallocSync* g = new GrallocSync;
    return g;
}

static const GLint rendererVersion = 1;

// GLAsyncSwap version history:
// "ANDROID_EMU_NATIVE_SYNC": original version
// "ANDROIDEMU_native_sync_v2": +cleanup of sync objects
// "ANDROIDEMU_native_sync_v3": EGL_KHR_wait_sync
// "ANDROIDEMU_native_sync_v4": Correct eglGetSyncAttrib via rcIsSyncSignaled
// (We need all the different strings to not be prefixes of any other
// due to how they are checked for in the GL extensions on the guest)
static const char* kAsyncSwapStrV2 = "ANDROID_EMU_native_sync_v2";
static const char* kAsyncSwapStrV3 = "ANDROID_EMU_native_sync_v3";
static const char* kAsyncSwapStrV4 = "ANDROID_EMU_native_sync_v4";

// DMA version history:
// "ANDROID_EMU_dma_v1": add dma device and rcUpdateColorBufferDMA and do
// yv12 conversion on the GPU
// "ANDROID_EMU_dma_v2": adds DMA support glMapBufferRange (and unmap)
static const char* kDma1Str = "ANDROID_EMU_dma_v1";
static const char* kDma2Str = "ANDROID_EMU_dma_v2";
static const char* kDirectMemStr = "ANDROID_EMU_direct_mem";

// GLESDynamicVersion: up to 3.1 so far
static const char* kGLESDynamicVersion_2 = "ANDROID_EMU_gles_max_version_2";
static const char* kGLESDynamicVersion_3_0 = "ANDROID_EMU_gles_max_version_3_0";
static const char* kGLESDynamicVersion_3_1 = "ANDROID_EMU_gles_max_version_3_1";

// HWComposer Host Composition
static const char* kHostCompositionV1 = "ANDROID_EMU_host_composition_v1";
static const char* kHostCompositionV2 = "ANDROID_EMU_host_composition_v2";

// Vulkan
static const char* kVulkanFeatureStr = "ANDROID_EMU_vulkan";
static const char* kDeferredVulkanCommands = "ANDROID_EMU_deferred_vulkan_commands";
static const char* kVulkanNullOptionalStrings = "ANDROID_EMU_vulkan_null_optional_strings";
static const char* kVulkanCreateResourcesWithRequirements = "ANDROID_EMU_vulkan_create_resources_with_requirements";

// treat YUV420_888 as NV21
static const char* kYUV420888toNV21 = "ANDROID_EMU_YUV420_888_to_NV21";

// Cache YUV frame
static const char* kYUVCache = "ANDROID_EMU_YUV_Cache";

// GL protocol v2
static const char* kAsyncUnmapBuffer = "ANDROID_EMU_async_unmap_buffer";
// Vulkan: Correct marshaling for ignored handles
static const char* kVulkanIgnoredHandles = "ANDROID_EMU_vulkan_ignored_handles";

// virtio-gpu-next
static const char* kVirtioGpuNext = "ANDROID_EMU_virtio_gpu_next";

// address space subdevices
static const char* kHasSharedSlotsHostMemoryAllocator = "ANDROID_EMU_has_shared_slots_host_memory_allocator";

// vulkan free memory sync
static const char* kVulkanFreeMemorySync = "ANDROID_EMU_vulkan_free_memory_sync";

// virtio-gpu native sync
static const char* kVirtioGpuNativeSync = "ANDROID_EMU_virtio_gpu_native_sync";

// Struct defs for VK_KHR_shader_float16_int8
static const char* kVulkanShaderFloat16Int8 = "ANDROID_EMU_vulkan_shader_float16_int8";

// Async queue submit
static const char* kVulkanAsyncQueueSubmit = "ANDROID_EMU_vulkan_async_queue_submit";

// Host side tracing
static const char* kHostSideTracing = "ANDROID_EMU_host_side_tracing";

// Some frame commands we can easily make async
// rcMakeCurrent
// rcCompose
// rcDestroySyncKHR
static const char* kAsyncFrameCommands = "ANDROID_EMU_async_frame_commands";

// Queue submit with commands
static const char* kVulkanQueueSubmitWithCommands = "ANDROID_EMU_vulkan_queue_submit_with_commands";

// Batched descriptor set update
static const char* kVulkanBatchedDescriptorSetUpdate = "ANDROID_EMU_vulkan_batched_descriptor_set_update";

// Synchronized glBufferData call
static const char* kSyncBufferData = "ANDROID_EMU_sync_buffer_data";

// Async vkQSRI
static const char* kVulkanAsyncQsri = "ANDROID_EMU_vulkan_async_qsri";

// Read color buffer DMA
static const char* kReadColorBufferDma = "ANDROID_EMU_read_color_buffer_dma";

// Multiple display configs
static const char* kHWCMultiConfigs= "ANDROID_EMU_hwc_multi_configs";

static constexpr const uint64_t kInvalidPUID = std::numeric_limits<uint64_t>::max();

static void rcTriggerWait(uint64_t glsync_ptr,
                          uint64_t thread_ptr,
                          uint64_t timeline);

void registerTriggerWait() {
    gfxstream_sync_register_trigger_wait(rcTriggerWait);
}

static GLint rcGetRendererVersion()
{
    registerTriggerWait();

    sGrallocSync();
    return rendererVersion;
}

static EGLint rcGetEGLVersion(EGLint* major, EGLint* minor)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return EGL_FALSE;
    }
    fb->getEmulationGl().getEglVersion(major, minor);

    return EGL_TRUE;
}

static EGLint rcQueryEGLString(EGLenum name, void* buffer, EGLint bufferSize)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    const char* str = gl::s_egl.eglQueryString(fb->getDisplay(), name);
    if (!str) {
        return 0;
    }

    std::string eglStr(str);
    if ((fb->getMaxGLESVersion() >= GLES_DISPATCH_MAX_VERSION_3_0) &&
        fb->getFeatures().GlesDynamicVersion.enabled &&
        eglStr.find("EGL_KHR_create_context") == std::string::npos) {
        eglStr += "EGL_KHR_create_context ";
    }

    int len = eglStr.size() + 1;
    if (!buffer || len > bufferSize) {
        return -len;
    }

    strcpy((char *)buffer, eglStr.c_str());
    return len;
}

static bool shouldEnableAsyncSwap(const gfxstream::host::FeatureSet& features) {
    bool isPhone = true;
    bool playStoreImage = features.PlayStoreImage.enabled;
    return features.GlAsyncSwap.enabled &&
           gfxstream_sync_device_exists() && (isPhone || playStoreImage) &&
           sizeof(void*) == 8;
}

static bool shouldEnableVulkan(const gfxstream::host::FeatureSet& features) {
    // TODO: Restrict further to devices supporting external memory.
    FrameBuffer* fb = FrameBuffer::getFB();
    return features.Vulkan.enabled && fb->hasEmulationVk() &&
           vk::VkDecoderGlobalState::get()->getHostFeatureSupport().supportsVulkan;
}

static bool shouldEnableDeferredVulkanCommands() {
    auto supportInfo = vk::VkDecoderGlobalState::get()->getHostFeatureSupport();
    return supportInfo.supportsVulkan &&
           supportInfo.useDeferredCommands;
}

static bool shouldEnableCreateResourcesWithRequirements() {
    auto supportInfo = vk::VkDecoderGlobalState::get()->getHostFeatureSupport();
    return supportInfo.supportsVulkan &&
           supportInfo.useCreateResourcesWithRequirements;
}

static bool shouldEnableVulkanShaderFloat16Int8(const gfxstream::host::FeatureSet& features) {
    return shouldEnableVulkan(features) && features.VulkanShaderFloat16Int8.enabled;
}

static bool shouldEnableAsyncQueueSubmit(const gfxstream::host::FeatureSet& features) {
    return shouldEnableVulkan(features);
}

static bool shouldEnableVulkanAsyncQsri(const gfxstream::host::FeatureSet& features) {
    return shouldEnableVulkan(features) &&
        (features.GlAsyncSwap.enabled ||
         (features.VirtioGpuNativeSync.enabled &&
          features.VirtioGpuFenceContexts.enabled));
}

static bool shouldEnableVsyncGatedSyncFences(const gfxstream::host::FeatureSet& features) {
    return shouldEnableAsyncSwap(features);
}

const char* maxVersionToFeatureString(GLESDispatchMaxVersion version) {
    switch (version) {
        case GLES_DISPATCH_MAX_VERSION_2:
            return kGLESDynamicVersion_2;
        case GLES_DISPATCH_MAX_VERSION_3_0:
            return kGLESDynamicVersion_3_0;
        case GLES_DISPATCH_MAX_VERSION_3_1:
            return kGLESDynamicVersion_3_1;
        default:
            return kGLESDynamicVersion_2;
    }
}

static bool shouldEnableQueueSubmitWithCommands(const gfxstream::host::FeatureSet& features) {
    return shouldEnableVulkan(features) && features.VulkanQueueSubmitWithCommands.enabled;
}

static bool shouldEnableBatchedDescriptorSetUpdate(const gfxstream::host::FeatureSet& features) {
    return shouldEnableVulkan(features) &&
        shouldEnableQueueSubmitWithCommands(features) &&
        features.VulkanBatchedDescriptorSetUpdate.enabled;
}

// OpenGL ES 3.x support involves changing the GL_VERSION string, which is
// assumed to be formatted in the following way:
// "OpenGL ES-CM 1.m <vendor-info>" or
// "OpenGL ES M.m <vendor-info>"
// where M is the major version number and m is minor version number.  If the
// GL_VERSION string doesn't reflect the maximum available version of OpenGL
// ES, many apps will not be able to detect support.  We need to mess with the
// version string in the first place since the underlying backend (whether it
// is Translator, SwiftShader, ANGLE, et al) may not advertise a GL_VERSION
// string reflecting their maximum capabilities.
std::string replaceESVersionString(const std::string& prev,
                                   const std::string& newver) {

    // There is no need to fiddle with the string
    // if we are in a ES 1.x context.
    // Such contexts are considered as a special case that must
    // be untouched.
    if (prev.find("ES-CM") != std::string::npos) {
        return prev;
    }

    size_t esStart = prev.find("ES ");
    size_t esEnd = prev.find(" ", esStart + 3);

    if (esStart == std::string::npos ||
        esEnd == std::string::npos) {
        // Account for out-of-spec version strings.
        GFXSTREAM_ERROR("%s: Error: unexpected OpenGL ES version string %s", __func__,
                        prev.c_str());
        return prev;
    }

    std::string res = prev.substr(0, esStart + 3);
    res += newver;
    res += prev.substr(esEnd);

    return res;
}

// If the GLES3 feature is disabled, we also want to splice out
// OpenGL extensions that should not appear in a GLES2 system.
void removeExtension(std::string& currExts, const std::string& toRemove) {
    size_t pos = currExts.find(toRemove);

    if (pos != std::string::npos)
        currExts.erase(pos, toRemove.length());
}

static EGLint rcGetGLString(EGLenum name, void* buffer, EGLint bufferSize) {
    RenderThreadInfoGl* const tInfo = RenderThreadInfoGl::get();

    // whatever we end up returning,
    // it will have a terminating \0,
    // so account for it here.
    std::string glStr;

    if (tInfo && tInfo->currContext.get()) {
        const char *str = nullptr;
        if (tInfo->currContext->clientVersion() > GLESApi_CM) {
            str = (const char*)gl::s_gles2.glGetString(name);
        }
        else {
            str = (const char*)gl::s_gles1.glGetString(name);
        }
        if (str) {
            glStr += str;
        }
    }

    FrameBuffer* fb = FrameBuffer::getFB();

    const gfxstream::host::FeatureSet& features = fb->getFeatures();

    // We add the maximum supported GL protocol number into GL_EXTENSIONS

    // filter extensions by name to match guest-side support
    GLESDispatchMaxVersion maxVersion = fb->getMaxGLESVersion();
    if (name == GL_EXTENSIONS) {
        glStr = gl::filterExtensionsBasedOnMaxVersion(features, maxVersion, glStr);
    }

    bool isChecksumEnabled = features.GlPipeChecksum.enabled;
    bool asyncSwapEnabled = shouldEnableAsyncSwap(features);
    bool virtioGpuNativeSyncEnabled = features.VirtioGpuNativeSync.enabled;
    bool dma1Enabled = features.GlDma.enabled;
    bool dma2Enabled = features.GlDma2.enabled;
    bool directMemEnabled = features.GlDirectMem.enabled;
    bool hostCompositionEnabled = features.HostComposition.enabled;
    bool vulkanEnabled = shouldEnableVulkan(features);
    bool deferredVulkanCommandsEnabled =
        shouldEnableVulkan(features) && shouldEnableDeferredVulkanCommands();
    bool vulkanNullOptionalStringsEnabled =
        shouldEnableVulkan(features) && features.VulkanNullOptionalStrings.enabled;
    bool vulkanCreateResourceWithRequirementsEnabled =
        shouldEnableVulkan(features) && shouldEnableCreateResourcesWithRequirements();
    bool YUV420888toNV21Enabled = features.Yuv420888ToNv21.enabled;
    bool YUVCacheEnabled = features.YuvCache.enabled;
    bool AsyncUnmapBufferEnabled = features.AsyncComposeSupport.enabled;
    bool vulkanIgnoredHandlesEnabled =
        shouldEnableVulkan(features) && features.VulkanIgnoredHandles.enabled;
    bool virtioGpuNextEnabled = features.VirtioGpuNext.enabled;
    bool hasSharedSlotsHostMemoryAllocatorEnabled =
        features.HasSharedSlotsHostMemoryAllocator.enabled;
    bool vulkanFreeMemorySyncEnabled =
        shouldEnableVulkan(features);
    bool vulkanShaderFloat16Int8Enabled = shouldEnableVulkanShaderFloat16Int8(features);
    bool vulkanAsyncQueueSubmitEnabled = shouldEnableAsyncQueueSubmit(features);
    bool vulkanQueueSubmitWithCommands = shouldEnableQueueSubmitWithCommands(features);
    bool vulkanBatchedDescriptorSetUpdate = shouldEnableBatchedDescriptorSetUpdate(features);
    bool syncBufferDataEnabled = true;
    bool vulkanAsyncQsri = shouldEnableVulkanAsyncQsri(features);
    bool readColorBufferDma = directMemEnabled && hasSharedSlotsHostMemoryAllocatorEnabled;
    bool hwcMultiConfigs = features.HwcMultiConfigs.enabled;

    if (isChecksumEnabled && name == GL_EXTENSIONS) {
        glStr += ChecksumCalculatorThreadInfo::getMaxVersionString();
        glStr += " ";
    }

    if (asyncSwapEnabled && name == GL_EXTENSIONS) {
        glStr += kAsyncSwapStrV2;
        glStr += " "; // for compatibility with older system images
        // Only enable EGL_KHR_wait_sync (and above) for host gpu.
        if (get_gfxstream_renderer() == SELECTED_RENDERER_HOST) {
            glStr += kAsyncSwapStrV3;
            glStr += " ";
            glStr += kAsyncSwapStrV4;
            glStr += " ";
        }
    }

    if (dma1Enabled && name == GL_EXTENSIONS) {
        glStr += kDma1Str;
        glStr += " ";
    }

    if (dma2Enabled && name == GL_EXTENSIONS) {
        glStr += kDma2Str;
        glStr += " ";
    }

    if (directMemEnabled && name == GL_EXTENSIONS) {
        glStr += kDirectMemStr;
        glStr += " ";
    }

    if (hostCompositionEnabled && name == GL_EXTENSIONS) {
        glStr += kHostCompositionV1;
        glStr += " ";
    }

    if (hostCompositionEnabled && name == GL_EXTENSIONS) {
        glStr += kHostCompositionV2;
        glStr += " ";
    }

    if (vulkanEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanFeatureStr;
        glStr += " ";
    }

    if (deferredVulkanCommandsEnabled && name == GL_EXTENSIONS) {
        glStr += kDeferredVulkanCommands;
        glStr += " ";
    }

    if (vulkanNullOptionalStringsEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanNullOptionalStrings;
        glStr += " ";
    }

    if (vulkanCreateResourceWithRequirementsEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanCreateResourcesWithRequirements;
        glStr += " ";
    }

    if (YUV420888toNV21Enabled && name == GL_EXTENSIONS) {
        glStr += kYUV420888toNV21;
        glStr += " ";
    }

    if (YUVCacheEnabled && name == GL_EXTENSIONS) {
        glStr += kYUVCache;
        glStr += " ";
    }

    if (AsyncUnmapBufferEnabled && name == GL_EXTENSIONS) {
        glStr += kAsyncUnmapBuffer;
        glStr += " ";
    }

    if (vulkanIgnoredHandlesEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanIgnoredHandles;
        glStr += " ";
    }

    if (virtioGpuNextEnabled && name == GL_EXTENSIONS) {
        glStr += kVirtioGpuNext;
        glStr += " ";
    }

    if (hasSharedSlotsHostMemoryAllocatorEnabled && name == GL_EXTENSIONS) {
        glStr += kHasSharedSlotsHostMemoryAllocator;
        glStr += " ";
    }

    if (vulkanFreeMemorySyncEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanFreeMemorySync;
        glStr += " ";
    }

    if (vulkanShaderFloat16Int8Enabled && name == GL_EXTENSIONS) {
        glStr += kVulkanShaderFloat16Int8;
        glStr += " ";
    }

    if (vulkanAsyncQueueSubmitEnabled && name == GL_EXTENSIONS) {
        glStr += kVulkanAsyncQueueSubmit;
        glStr += " ";
    }

    if (vulkanQueueSubmitWithCommands && name == GL_EXTENSIONS) {
        glStr += kVulkanQueueSubmitWithCommands;
        glStr += " ";
    }

    if (vulkanBatchedDescriptorSetUpdate && name == GL_EXTENSIONS) {
        glStr += kVulkanBatchedDescriptorSetUpdate;
        glStr += " ";
    }

    if (virtioGpuNativeSyncEnabled && name == GL_EXTENSIONS) {
        glStr += kVirtioGpuNativeSync;
        glStr += " ";
    }

    if (syncBufferDataEnabled && name == GL_EXTENSIONS) {
        glStr += kSyncBufferData;
        glStr += " ";
    }

    if (vulkanAsyncQsri && name == GL_EXTENSIONS) {
        glStr += kVulkanAsyncQsri;
        glStr += " ";
    }

    if (readColorBufferDma && name == GL_EXTENSIONS) {
        glStr += kReadColorBufferDma;
        glStr += " ";
    }

    if (hwcMultiConfigs && name == GL_EXTENSIONS) {
        glStr += kHWCMultiConfigs;
        glStr += " ";
    }

    if (name == GL_EXTENSIONS) {
        GLESDispatchMaxVersion guestExtVer = GLES_DISPATCH_MAX_VERSION_2;
        if (features.GlesDynamicVersion.enabled) {
            // If the image is in ES 3 mode, add GL_OES_EGL_image_external_essl3 for better Skia support.
            glStr += "GL_OES_EGL_image_external_essl3 ";
            guestExtVer = maxVersion;
        }

        // If we have a GLES3 implementation, add the corresponding
        // GLESv2 extensions as well.
        if (maxVersion > GLES_DISPATCH_MAX_VERSION_2) {
            glStr += "GL_OES_vertex_array_object ";
        }

        // ASTC LDR compressed texture support.
        const std::string& glExtensions =
            FrameBuffer::getFB()->hasEmulationGl()
                ? FrameBuffer::getFB()->getEmulationGl().getGlesExtensionsString()
                : "<no GL emulation>";
        const bool hasNativeAstc =
            glExtensions.find("GL_KHR_texture_compression_astc_ldr") != std::string::npos;
        const bool hasAstcDecompressor = vk::AstcCpuDecompressor::get().available();
        if (hasNativeAstc || hasAstcDecompressor) {
            glStr += "GL_KHR_texture_compression_astc_ldr ";
        } else {
            RENDERCONTROL_DPRINT(
                "rcGetGLString: ASTC not supported. CPU decompressor? %d. GL extensions: %s",
                hasAstcDecompressor, glExtensions.c_str());
        }

        // Host side tracing support.
        glStr += kHostSideTracing;
        glStr += " ";

        if (features.AsyncComposeSupport.enabled) {
            // Async makecurrent support.
            glStr += kAsyncFrameCommands;
            glStr += " ";
        }

        glStr += maxVersionToFeatureString(guestExtVer);
        glStr += " ";
    }

    if (name == GL_VERSION) {
        if (features.GlesDynamicVersion.enabled) {
            switch (maxVersion) {
            // Underlying GLES implmentation's max version string
            // is allowed to be higher than the version of the request
            // for the context---it can create a higher version context,
            // and return simply the max possible version overall.
            case GLES_DISPATCH_MAX_VERSION_2:
                glStr = replaceESVersionString(glStr, "2.0");
                break;
            case GLES_DISPATCH_MAX_VERSION_3_0:
                glStr = replaceESVersionString(glStr, "3.0");
                break;
            case GLES_DISPATCH_MAX_VERSION_3_1:
                glStr = replaceESVersionString(glStr, "3.1");
                break;
            default:
                break;
            }
        } else {
            glStr = replaceESVersionString(glStr, "2.0");
        }
    }

    int nextBufferSize = glStr.size() + 1;

    if (!buffer || nextBufferSize > bufferSize) {
        return -nextBufferSize;
    }

    snprintf((char *)buffer, nextBufferSize, "%s", glStr.c_str());
    return nextBufferSize;
}

static EGLint rcGetNumConfigs(uint32_t* p_numAttribs)
{
    int numConfigs = 0, numAttribs = 0;

    FrameBuffer::getFB()->getConfigs()->getPackInfo(&numConfigs, &numAttribs);
    if (p_numAttribs) {
        *p_numAttribs = static_cast<uint32_t>(numAttribs);
    }
    return numConfigs;
}

static EGLint rcGetConfigs(uint32_t bufSize, GLuint* buffer)
{
    GLuint bufferSize = (GLuint)bufSize;
    return FrameBuffer::getFB()->getConfigs()->packConfigs(bufferSize, buffer);
}

static EGLint rcChooseConfig(EGLint *attribs,
                             uint32_t attribs_size,
                             uint32_t *configs,
                             uint32_t configs_size)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    if (attribs_size == 0) {
        if (configs && configs_size > 0) {
            // Pick the first config
            *configs = 0;
            if (attribs) *attribs = EGL_NONE;
        }
    }

    return fb->getConfigs()->chooseConfig(
            attribs, (EGLint*)configs, (EGLint)configs_size);
}

static EGLint rcGetFBParam(EGLint param)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }
    return fb->getDisplayConfigsParam(0, param);
}

static uint32_t rcCreateContext(uint32_t config,
                                uint32_t share, uint32_t glVersion)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    HandleType ret = fb->createEmulatedEglContext(config, share, (GLESApi)glVersion);
    return ret;
}

static void rcDestroyContext(uint32_t context)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    fb->destroyEmulatedEglContext(context);
}

static uint32_t rcCreateWindowSurface(uint32_t config,
                                      uint32_t width, uint32_t height)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    return fb->createEmulatedEglWindowSurface(config, width, height);
}

static void rcDestroyWindowSurface(uint32_t windowSurface)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_VERBOSE("%s: framebuffer cannot be found!", __func__);
        return;
    }

    fb->destroyEmulatedEglWindowSurface(windowSurface);
}

static uint32_t rcCreateColorBuffer(uint32_t width,
                                    uint32_t height, GLenum internalFormat)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    return fb->createColorBuffer(width, height, internalFormat,
                                 FRAMEWORK_FORMAT_GL_COMPATIBLE);
}

static uint32_t rcCreateColorBufferDMA(uint32_t width,
                                       uint32_t height, GLenum internalFormat,
                                       int frameworkFormat)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    return fb->createColorBuffer(width, height, internalFormat,
                                 (FrameworkFormat)frameworkFormat);
}

static int rcOpenColorBuffer2(uint32_t colorbuffer)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }
    return fb->openColorBuffer( colorbuffer );
}

static void rcOpenColorBuffer(uint32_t colorbuffer)
{
    (void) rcOpenColorBuffer2(colorbuffer);
}

static void rcCloseColorBuffer(uint32_t colorbuffer)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_VERBOSE("%s: framebuffer cannot be found!", __func__);
        return;
    }
    fb->closeColorBuffer( colorbuffer );
}

static int rcFlushWindowColorBuffer(uint32_t windowSurface)
{
    GRSYNC_DPRINT("waiting for gralloc cb lock");
    GrallocSyncPostLock lock(*sGrallocSync());
    GRSYNC_DPRINT("lock gralloc cb lock {");

    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GRSYNC_DPRINT("unlock gralloc cb lock");
        return -1;
    }

    HandleType colorBufferHandle = fb->getEmulatedEglWindowSurfaceColorBufferHandle(windowSurface);

    if (!fb->flushEmulatedEglWindowSurfaceColorBuffer(windowSurface)) {
        GRSYNC_DPRINT("unlock gralloc cb lock }");
        return -1;
    }

    // Make the GL updates visible to other backings if necessary.
    if (colorBufferHandle != 0) {
        fb->flushColorBufferFromGl(colorBufferHandle);
    }

    GRSYNC_DPRINT("unlock gralloc cb lock }");

    return 0;
}

// Note that even though this calls rcFlushWindowColorBuffer,
// the "Async" part is in the return type, which is void
// versus return type int for rcFlushWindowColorBuffer.
//
// The different return type, even while calling the same
// functions internally, will end up making the encoder
// and decoder use a different protocol. This is because
// the encoder generally obeys the following conventions:
//
// - The encoder will immediately send and wait for a command
//   result if the return type is not void.
// - The encoder will cache the command in a buffer and send
//   at a convenient time if the return type is void.
//
// It can also be expensive performance-wise to trigger
// sending traffic back to the guest. Generally, the more we avoid
// encoding commands that perform two-way traffic, the better.
//
// Hence, |rcFlushWindowColorBufferAsync| will avoid extra traffic;
// with return type void,
// the guest will not wait until this function returns,
// nor will it immediately send the command,
// resulting in more asynchronous behavior.
static void rcFlushWindowColorBufferAsync(uint32_t windowSurface)
{
    rcFlushWindowColorBuffer(windowSurface);
}

static void rcSetWindowColorBuffer(uint32_t windowSurface,
                                   uint32_t colorBuffer)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }
    fb->setEmulatedEglWindowSurfaceColorBuffer(windowSurface, colorBuffer);
}

static EGLint rcMakeCurrent(uint32_t context,
                            uint32_t drawSurf, uint32_t readSurf)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return EGL_FALSE;
    }

    bool ret = fb->bindContext(context, drawSurf, readSurf);

    return (ret ? EGL_TRUE : EGL_FALSE);
}

static void rcFBPost(uint32_t colorBuffer)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    fb->post(colorBuffer);
}

static void rcFBSetSwapInterval(EGLint interval)
{
   // XXX: TBD - should be implemented
}

static void rcBindTexture(uint32_t colorBuffer)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    // Update for GL use if necessary.
    fb->invalidateColorBufferForGl(colorBuffer);

    fb->bindColorBufferToTexture(colorBuffer);
}

static void rcBindRenderbuffer(uint32_t colorBuffer)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    // Update for GL use if necessary.
    fb->invalidateColorBufferForGl(colorBuffer);

    fb->bindColorBufferToRenderbuffer(colorBuffer);
}

static EGLint rcColorBufferCacheFlush(uint32_t colorBuffer,
                                      EGLint postCount, int forRead)
{
    // gralloc_lock() on the guest calls rcColorBufferCacheFlush
    GRSYNC_DPRINT("waiting for gralloc cb lock");
    sGrallocSync()->lockColorBufferPrepare();
    GRSYNC_DPRINT("lock gralloc cb lock {");
    return 0;
}

static void rcReadColorBuffer(uint32_t colorBuffer,
                              GLint x, GLint y,
                              GLint width, GLint height,
                              GLenum format, GLenum type, void* pixels)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    fb->readColorBuffer(colorBuffer, x, y, width, height, format, type, pixels);
}

static int rcUpdateColorBuffer(uint32_t colorBuffer,
                               GLint x, GLint y,
                               GLint width, GLint height,
                               GLenum format, GLenum type, void* pixels)
{
    FrameBuffer *fb = FrameBuffer::getFB();

    if (!fb) {
        GRSYNC_DPRINT("unlock gralloc cb lock");
        sGrallocSync()->unlockColorBufferPrepare();
        return -1;
    }

    fb->updateColorBuffer(colorBuffer, x, y, width, height, format, type, pixels);

    GRSYNC_DPRINT("unlock gralloc cb lock");
    sGrallocSync()->unlockColorBufferPrepare();

    return 0;
}

static int rcUpdateColorBufferDMA(uint32_t colorBuffer,
                                  GLint x, GLint y,
                                  GLint width, GLint height,
                                  GLenum format, GLenum type,
                                  void* pixels, uint32_t pixels_size)
{
    FrameBuffer *fb = FrameBuffer::getFB();

    if (!fb) {
        GRSYNC_DPRINT("unlock gralloc cb lock");
        sGrallocSync()->unlockColorBufferPrepare();
        return -1;
    }

    fb->updateColorBuffer(colorBuffer, x, y, width, height,
                          format, type, pixels);

    GRSYNC_DPRINT("unlock gralloc cb lock");
    sGrallocSync()->unlockColorBufferPrepare();

    return 0;
}

static uint32_t rcCreateClientImage(uint32_t context, EGLenum target, GLuint buffer)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    return fb->createEmulatedEglImage(context, target, buffer);
}

static int rcDestroyClientImage(uint32_t image)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_VERBOSE("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    return fb->destroyEmulatedEglImage(image);
}

static void rcSelectChecksumHelper(uint32_t protocol, uint32_t reserved) {
    ChecksumCalculatorThreadInfo::setVersion(protocol);
}

// |rcTriggerWait| is called from the goldfish sync
// kernel driver whenever a native fence fd is created.
// We will then need to use the host to find out
// when to signal that native fence fd. We use
// SyncThread for that.
static void rcTriggerWait(uint64_t eglsync_ptr,
                          uint64_t thread_ptr,
                          uint64_t timeline) {
    if (thread_ptr == 1) {
        // Is vulkan sync fd;
        // just signal right away for now
        EGLSYNC_DPRINT("vkFence=0x%llx timeline=0x%llx", eglsync_ptr,
                       thread_ptr, timeline);
        SyncThread::get()->triggerWaitVk(reinterpret_cast<VkFence>(eglsync_ptr),
                                         timeline);
    } else if (thread_ptr == 2) {
        EGLSYNC_DPRINT("vkFence=0x%llx timeline=0x%llx", eglsync_ptr,
                       thread_ptr, timeline);
        SyncThread::get()->triggerWaitVkQsri(reinterpret_cast<VkImage>(eglsync_ptr), timeline);
    } else {
        EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(eglsync_ptr);
        FrameBuffer *fb = FrameBuffer::getFB();
        if (fb && fenceSync && fenceSync->isCompositionFence()) {
            fb->scheduleVsyncTask([eglsync_ptr, fenceSync, timeline](uint64_t) {
                (void)eglsync_ptr;
                EGLSYNC_DPRINT(
                    "vsync: eglsync=0x%llx fenceSync=%p thread_ptr=0x%llx "
                    "timeline=0x%llx",
                    eglsync_ptr, fenceSync, thread_ptr, timeline);
                SyncThread::get()->triggerWait(fenceSync, timeline);
            });
        } else {
            EGLSYNC_DPRINT(
                    "eglsync=0x%llx fenceSync=%p thread_ptr=0x%llx "
                    "timeline=0x%llx",
                    eglsync_ptr, fenceSync, thread_ptr, timeline);
            SyncThread::get()->triggerWait(fenceSync, timeline);
        }
    }
}

// |rcCreateSyncKHR| implements the guest's |eglCreateSyncKHR| by calling the
// host's implementation of |eglCreateSyncKHR|. A SyncThread is also notified
// for purposes of signaling any native fence fd's that get created in the
// guest off the sync object created here.
static void rcCreateSyncKHR(EGLenum type,
                            EGLint* attribs,
                            uint32_t num_attribs,
                            int destroyWhenSignaled,
                            uint64_t* outSync,
                            uint64_t* outSyncThread) {
    // Usually we expect rcTriggerWait to be registered
    // at the beginning in rcGetRendererVersion, called
    // on init for all contexts.
    // But if we are loading from snapshot, that's not
    // guaranteed, and we need to make sure
    // rcTriggerWait is registered.
    gfxstream_sync_register_trigger_wait(rcTriggerWait);

    FrameBuffer* fb = FrameBuffer::getFB();

    fb->createEmulatedEglFenceSync(type,
                                   destroyWhenSignaled,
                                   outSync,
                                   outSyncThread);

    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    if (tInfo && outSync && shouldEnableVsyncGatedSyncFences(fb->getFeatures())) {
        auto fenceSync = reinterpret_cast<EmulatedEglFenceSync*>(outSync);
        fenceSync->setIsCompositionFence(tInfo->m_isCompositionThread);
    }
}

// |rcClientWaitSyncKHR| implements |eglClientWaitSyncKHR|
// on the guest through using the host's existing
// |eglClientWaitSyncKHR| implementation, which is done
// through the EmulatedEglFenceSync object.
static EGLint rcClientWaitSyncKHR(uint64_t handle,
                                  EGLint flags,
                                  uint64_t timeout) {
    RenderThreadInfoGl* const tInfo = RenderThreadInfoGl::get();
    if (!tInfo) {
        GFXSTREAM_FATAL("Render thread GL not available.");
    }

    FrameBuffer *fb = FrameBuffer::getFB();

    EGLSYNC_DPRINT("handle=0x%lx flags=0x%x timeout=%" PRIu64,
                handle, flags, timeout);

    EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(handle);

    if (!fenceSync) {
        EGLSYNC_DPRINT("fenceSync null, return condition satisfied");
        return EGL_CONDITION_SATISFIED_KHR;
    }

    // Sometimes a gralloc-buffer-only thread is doing stuff with sync.
    // This happens all the time with YouTube videos in the browser.
    // In this case, create a context on the host just for syncing.
    if (!tInfo->currContext) {
        uint32_t gralloc_sync_cxt, gralloc_sync_surf;
        fb->createTrivialContext(0, // There is no context to share.
                                 &gralloc_sync_cxt,
                                 &gralloc_sync_surf);
        fb->bindContext(gralloc_sync_cxt,
                        gralloc_sync_surf,
                        gralloc_sync_surf);
        // This context is then cleaned up when the render thread exits.
    }

    return fenceSync->wait(timeout);
}

static void rcWaitSyncKHR(uint64_t handle,
                                  EGLint flags) {
    RenderThreadInfoGl* const tInfo = RenderThreadInfoGl::get();
    if (!tInfo) {
        GFXSTREAM_FATAL("Render thread GL not available.");
    }

    FrameBuffer *fb = FrameBuffer::getFB();

    EGLSYNC_DPRINT("handle=0x%lx flags=0x%x", handle, flags);

    EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(handle);

    if (!fenceSync) { return; }

    // Sometimes a gralloc-buffer-only thread is doing stuff with sync.
    // This happens all the time with YouTube videos in the browser.
    // In this case, create a context on the host just for syncing.
    if (!tInfo->currContext) {
        uint32_t gralloc_sync_cxt, gralloc_sync_surf;
        fb->createTrivialContext(0, // There is no context to share.
                                 &gralloc_sync_cxt,
                                 &gralloc_sync_surf);
        fb->bindContext(gralloc_sync_cxt,
                        gralloc_sync_surf,
                        gralloc_sync_surf);
        // This context is then cleaned up when the render thread exits.
    }

    fenceSync->waitAsync();
}

static int rcDestroySyncKHR(uint64_t handle) {
    EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(handle);
    if (!fenceSync) return 0;
    fenceSync->decRef();
    return 0;
}

static void rcSetPuid(uint64_t puid) {
    if (puid == kInvalidPUID) {
        // The host process pipe implementation (GLProcessPipe) has been updated
        // to not generate a unique pipe id when running with virtio gpu and
        // instead send -1 to the guest. Ignore those requests as the PUID will
        // instead be the virtio gpu context id.
        return;
    }

    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    tInfo->m_puid = puid;
    auto* renderThreadInfoVk = vk::RenderThreadInfoVk::get();
    if (renderThreadInfoVk) {
        renderThreadInfoVk->ctx_id = puid;
    }
}

static int rcCompose(uint32_t bufferSize, void* buffer) {
    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    if (tInfo) tInfo->m_isCompositionThread = true;

    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }
    return fb->compose(bufferSize, buffer, true);
}

static int rcComposeWithoutPost(uint32_t bufferSize, void* buffer) {
    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    if (tInfo) tInfo->m_isCompositionThread = true;

    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }
    return fb->compose(bufferSize, buffer, false);
}

static int rcCreateDisplay(uint32_t* displayId) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    // Assume this API call always allocates a new displayId
    *displayId = FrameBuffer::s_invalidIdMultiDisplay;
    return fb->createDisplay(displayId);
}

static int rcCreateDisplayById(uint32_t displayId) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->createDisplay(displayId);
}

static int rcDestroyDisplay(uint32_t displayId) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->destroyDisplay(displayId);
}

static int rcSetDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->setDisplayColorBuffer(displayId, colorBuffer);
}

static int rcGetDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->getDisplayColorBuffer(displayId, colorBuffer);
}

static int rcGetColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->getColorBufferDisplay(colorBuffer, displayId);
}

static int rcGetDisplayPose(uint32_t displayId,
                            int32_t* x,
                            int32_t* y,
                            uint32_t* w,
                            uint32_t* h) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->getDisplayPose(displayId, x, y, w, h);
}

static int rcSetDisplayPose(uint32_t displayId,
                            int32_t x,
                            int32_t y,
                            uint32_t w,
                            uint32_t h) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->setDisplayPose(displayId, x, y, w, h);
}

static int rcSetDisplayPoseDpi(uint32_t displayId,
                               int32_t x,
                               int32_t y,
                               uint32_t w,
                               uint32_t h,
                               uint32_t dpi) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    return fb->setDisplayPose(displayId, x, y, w, h, dpi);
}

static void rcReadColorBufferYUV(uint32_t colorBuffer,
                                GLint x, GLint y,
                                GLint width, GLint height,
                                void* pixels, uint32_t pixels_size)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }

    fb->readColorBufferYUV(colorBuffer, x, y, width, height, pixels, pixels_size);
}

static int rcIsSyncSignaled(uint64_t handle) {
    EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(handle);
    if (!fenceSync) return 1; // assume destroyed => signaled
    return fenceSync->isSignaled() ? 1 : 0;
}

static void rcCreateColorBufferWithHandle(
    uint32_t width, uint32_t height, GLenum internalFormat, uint32_t handle)
{
    FrameBuffer *fb = FrameBuffer::getFB();

    if (!fb) {
        return;
    }

    fb->createColorBufferWithResourceHandle(width, height, internalFormat,
                                            FRAMEWORK_FORMAT_GL_COMPATIBLE, handle);
}

static uint32_t rcCreateBuffer2(uint64_t size, uint32_t memoryProperty) {
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return 0;
    }

    return fb->createBuffer(size, memoryProperty);
}

static uint32_t rcCreateBuffer(uint32_t size) {
    return rcCreateBuffer2(size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

static void rcCloseBuffer(uint32_t buffer) {
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }
    fb->closeBuffer(buffer);
}

static int rcSetColorBufferVulkanMode2(uint32_t colorBuffer, uint32_t mode,
                                       uint32_t memoryProperty) {
#define VULKAN_MODE_VULKAN_ONLY 1

    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb->hasEmulationVk()) {
        GFXSTREAM_ERROR("VkEmulation not enabled.");
        return -1;
    }
    if (!fb->getEmulationVk().setColorBufferVulkanMode(colorBuffer, mode)) {
        GFXSTREAM_ERROR("Failed to set ColorBuffer vulkan mode.");
        return -1;
    }

    return 0;
}

static int rcSetColorBufferVulkanMode(uint32_t colorBuffer, uint32_t mode) {
    return rcSetColorBufferVulkanMode2(colorBuffer, mode,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

static int32_t rcMapGpaToBufferHandle(uint32_t bufferHandle, uint64_t gpa) {
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb->hasEmulationVk()) {
        GFXSTREAM_ERROR("VkEmulation not enabled.");
        return -1;
    }
    if (fb->getEmulationVk().mapGpaToBufferHandle(bufferHandle, gpa) < 0) {
        GFXSTREAM_ERROR("Failed to map gpa %" PRIx64 " to buffer handle 0x%x.", gpa, bufferHandle);
        return -1;
    }
    return 0;
}

static int32_t rcMapGpaToBufferHandle2(uint32_t bufferHandle,
                                       uint64_t gpa,
                                       uint64_t size) {
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb->hasEmulationVk()) {
        GFXSTREAM_ERROR("VkEmulation not enabled.");
        return -1;
    }
    if (fb->getEmulationVk().mapGpaToBufferHandle(bufferHandle, gpa, size) < 0) {
        GFXSTREAM_ERROR("Failed to map gpa %" PRIx64 " to buffer handle 0x%x.", gpa, bufferHandle);
        return -1;
    }
    return 0;
}

static void rcFlushWindowColorBufferAsyncWithFrameNumber(uint32_t windowSurface, uint32_t frameNumber) {
    gfxstream::base::traceCounter("gfxstreamFrameNumber", (int64_t)frameNumber);
    rcFlushWindowColorBufferAsync(windowSurface);
}

static void rcSetTracingForPuid(uint64_t puid, uint32_t enable, uint64_t time) {
    if (enable) {
        gfxstream::base::setGuestTime(time);
        gfxstream::base::enableTracing();
    } else {
        gfxstream::base::disableTracing();
    }
}

static void rcMakeCurrentAsync(uint32_t context, uint32_t drawSurf, uint32_t readSurf) {
    AEMU_SCOPED_THRESHOLD_TRACE_CALL();
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb) { return; }

    fb->bindContext(context, drawSurf, readSurf);
}

static void rcComposeAsync(uint32_t bufferSize, void* buffer) {
    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    if (tInfo) tInfo->m_isCompositionThread = true;

    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }
    fb->compose(bufferSize, buffer, true);
}

static void rcComposeAsyncWithoutPost(uint32_t bufferSize, void* buffer) {
    RenderThreadInfo *tInfo = RenderThreadInfo::get();
    if (tInfo) tInfo->m_isCompositionThread = true;

    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        return;
    }
    fb->compose(bufferSize, buffer, false);
}

static void rcDestroySyncKHRAsync(uint64_t handle) {
    EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(handle);
    if (!fenceSync) return;
    fenceSync->decRef();
}

static int rcReadColorBufferDMA(uint32_t colorBuffer,
                                GLint x, GLint y,
                                GLint width, GLint height,
                                GLenum format, GLenum type, void* pixels, uint32_t pixels_size)
{
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }

    fb->readColorBuffer(colorBuffer, x, y, width, height, format, type, pixels, pixels_size);
    return 0;
}

static int rcGetFBDisplayConfigsCount() {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }
    return fb->getDisplayConfigsCount();
}

static int rcGetFBDisplayConfigsParam(int configId, GLint param) {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }
    return fb->getDisplayConfigsParam(configId, param);
}

static int rcGetFBDisplayActiveConfig() {
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb) {
        GFXSTREAM_WARNING("%s: framebuffer cannot be found!", __func__);
        return -1;
    }
    return fb->getDisplayActiveConfig();
}

static void rcSetProcessMetadata(char* key, RenderControlByte* valuePtr, uint32_t valueSize) {
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    if (strcmp(key, "process_name") == 0) {
        // We know this is a c formatted string
        tInfo->m_processName = std::string((char*) valuePtr);

        GFXSTREAM_TRACE_NAME_TRACK(GFXSTREAM_TRACE_TRACK_FOR_CURRENT_THREAD(),
                                   *tInfo->m_processName);
    }
}

static int rcGetHostExtensionsString(uint32_t bufferSize, void* buffer) {
    // TODO(b/233939967): split off host extensions from GL extensions.
    return rcGetGLString(GL_EXTENSIONS, buffer, bufferSize);
}

void initRenderControlContext(renderControl_decoder_context_t *dec)
{
    dec->rcGetRendererVersion = rcGetRendererVersion;
    dec->rcGetEGLVersion = rcGetEGLVersion;
    dec->rcQueryEGLString = rcQueryEGLString;
    dec->rcGetGLString = rcGetGLString;
    dec->rcGetNumConfigs = rcGetNumConfigs;
    dec->rcGetConfigs = rcGetConfigs;
    dec->rcChooseConfig = rcChooseConfig;
    dec->rcGetFBParam = rcGetFBParam;
    dec->rcCreateContext = rcCreateContext;
    dec->rcDestroyContext = rcDestroyContext;
    dec->rcCreateWindowSurface = rcCreateWindowSurface;
    dec->rcDestroyWindowSurface = rcDestroyWindowSurface;
    dec->rcCreateColorBuffer = rcCreateColorBuffer;
    dec->rcOpenColorBuffer = rcOpenColorBuffer;
    dec->rcCloseColorBuffer = rcCloseColorBuffer;
    dec->rcSetWindowColorBuffer = rcSetWindowColorBuffer;
    dec->rcFlushWindowColorBuffer = rcFlushWindowColorBuffer;
    dec->rcMakeCurrent = rcMakeCurrent;
    dec->rcFBPost = rcFBPost;
    dec->rcFBSetSwapInterval = rcFBSetSwapInterval;
    dec->rcBindTexture = rcBindTexture;
    dec->rcBindRenderbuffer = rcBindRenderbuffer;
    dec->rcColorBufferCacheFlush = rcColorBufferCacheFlush;
    dec->rcReadColorBuffer = rcReadColorBuffer;
    dec->rcUpdateColorBuffer = rcUpdateColorBuffer;
    dec->rcOpenColorBuffer2 = rcOpenColorBuffer2;
    dec->rcCreateClientImage = rcCreateClientImage;
    dec->rcDestroyClientImage = rcDestroyClientImage;
    dec->rcSelectChecksumHelper = rcSelectChecksumHelper;
    dec->rcCreateSyncKHR = rcCreateSyncKHR;
    dec->rcClientWaitSyncKHR = rcClientWaitSyncKHR;
    dec->rcFlushWindowColorBufferAsync = rcFlushWindowColorBufferAsync;
    dec->rcDestroySyncKHR = rcDestroySyncKHR;
    dec->rcSetPuid = rcSetPuid;
    dec->rcUpdateColorBufferDMA = rcUpdateColorBufferDMA;
    dec->rcCreateColorBufferDMA = rcCreateColorBufferDMA;
    dec->rcWaitSyncKHR = rcWaitSyncKHR;
    dec->rcCompose = rcCompose;
    dec->rcCreateDisplay = rcCreateDisplay;
    dec->rcDestroyDisplay = rcDestroyDisplay;
    dec->rcSetDisplayColorBuffer = rcSetDisplayColorBuffer;
    dec->rcGetDisplayColorBuffer = rcGetDisplayColorBuffer;
    dec->rcGetColorBufferDisplay = rcGetColorBufferDisplay;
    dec->rcGetDisplayPose = rcGetDisplayPose;
    dec->rcSetDisplayPose = rcSetDisplayPose;
    dec->rcSetColorBufferVulkanMode = rcSetColorBufferVulkanMode;
    dec->rcReadColorBufferYUV = rcReadColorBufferYUV;
    dec->rcIsSyncSignaled = rcIsSyncSignaled;
    dec->rcCreateColorBufferWithHandle = rcCreateColorBufferWithHandle;
    dec->rcCreateBuffer = rcCreateBuffer;
    dec->rcCreateBuffer2 = rcCreateBuffer2;
    dec->rcCloseBuffer = rcCloseBuffer;
    dec->rcSetColorBufferVulkanMode2 = rcSetColorBufferVulkanMode2;
    dec->rcMapGpaToBufferHandle = rcMapGpaToBufferHandle;
    dec->rcMapGpaToBufferHandle2 = rcMapGpaToBufferHandle2;
    dec->rcFlushWindowColorBufferAsyncWithFrameNumber = rcFlushWindowColorBufferAsyncWithFrameNumber;
    dec->rcSetTracingForPuid = rcSetTracingForPuid;
    dec->rcMakeCurrentAsync = rcMakeCurrentAsync;
    dec->rcComposeAsync = rcComposeAsync;
    dec->rcDestroySyncKHRAsync = rcDestroySyncKHRAsync;
    dec->rcComposeWithoutPost = rcComposeWithoutPost;
    dec->rcComposeAsyncWithoutPost = rcComposeAsyncWithoutPost;
    dec->rcCreateDisplayById = rcCreateDisplayById;
    dec->rcSetDisplayPoseDpi = rcSetDisplayPoseDpi;
    dec->rcReadColorBufferDMA = rcReadColorBufferDMA;
    dec->rcGetFBDisplayConfigsCount = rcGetFBDisplayConfigsCount;
    dec->rcGetFBDisplayConfigsParam = rcGetFBDisplayConfigsParam;
    dec->rcGetFBDisplayActiveConfig = rcGetFBDisplayActiveConfig;
    dec->rcSetProcessMetadata = rcSetProcessMetadata;
    dec->rcGetHostExtensionsString = rcGetHostExtensionsString;
}

}  // namespace gfxstream
