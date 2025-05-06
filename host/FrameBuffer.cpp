/*
* Copyright (C) 2011-2015 The Android Open Source Project
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

#include "FrameBuffer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <iomanip>

#if defined(__linux__)
#include <sys/resource.h>
#endif

#include "ContextHelper.h"
#include "Hwc2.h"
#include "NativeSubWindow.h"
#include "RenderThreadInfo.h"
#include "SyncThread.h"
#include "gfxstream/LayoutResolver.h"
#include "gfxstream/Metrics.h"
#include "gfxstream/SharedLibrary.h"
#include "gfxstream/Tracing.h"
#include "gfxstream/containers/Lookup.h"
#include "aemu/base/files/StreamSerializing.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/system/System.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "GLESVersionDetector.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "PostWorkerGl.h"
#include "RenderControl.h"
#include "RenderThreadInfoGl.h"
#include "gl/YUVConverter.h"
#include "gl/gles2_dec/gles2_dec.h"
#include "gl/glestranslator/EGL/EglGlobalInfo.h"
#endif
#include "gfxstream/host/Tracing.h"
#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/guest_operations.h"
#include "gfxstream/host/logging.h"
#include "gfxstream/host/renderer_operations.h"
#include "gfxstream/host/vm_operations.h"
#include "gfxstream/host/window_operations.h"
#include "render-utils/MediaNative.h"
#include "vulkan/DisplayVk.h"
#include "vulkan/PostWorkerVk.h"
#include "vulkan/VkCommonOperations.h"
#include "vulkan/VkDecoderGlobalState.h"

namespace gfxstream {

using gfxstream::base::AutoLock;
using gfxstream::base::MetricEventVulkanOutOfMemory;
using android::base::Stream;
using gfxstream::base::WorkerProcessingResult;
using emugl::GfxApiLogger;
using gfxstream::host::FeatureSet;

#if GFXSTREAM_ENABLE_HOST_GLES
using gl::DisplaySurfaceGl;
using gl::EmulatedEglConfig;
using gl::EmulatedEglConfigList;
using gl::EmulatedEglContext;
using gl::EmulatedEglContextMap;
using gl::EmulatedEglContextPtr;
using gl::EmulatedEglFenceSync;
using gl::EmulatedEglWindowSurface;
using gl::EmulatedEglWindowSurfaceMap;
using gl::EmulatedEglWindowSurfacePtr;
using gl::EmulationGl;
using gl::GLES_DISPATCH_MAX_VERSION_2;
using gl::GLESApi;
using gl::GLESApi_2;
using gl::GLESApi_CM;
using gl::GLESDispatchMaxVersion;
using gl::RenderThreadInfoGl;
using gl::s_egl;
using gl::s_gles2;
using gl::TextureDraw;
using gl::YUVConverter;
using gl::YUVPlane;
#endif

using vk::AstcEmulationMode;
using vk::VkEmulation;

FrameBuffer* FrameBuffer::s_theFrameBuffer = NULL;
HandleType FrameBuffer::s_nextHandle = 0;

// A condition variable needed to wait for framebuffer initialization.
namespace {
struct InitializedGlobals {
    gfxstream::base::Lock lock;
    gfxstream::base::ConditionVariable condVar;
};

bool postOnlyOnMainThread() {
#if defined(__APPLE__) && !defined(QEMU_NEXT)
    return true;
#else
    return false;
#endif
}

}  // namespace

// |sInitialized| caches the initialized framebuffer state - this way
// happy path doesn't need to lock the mutex.
static std::atomic<bool> sInitialized{false};
static InitializedGlobals* sGlobals() {
    static InitializedGlobals* g = new InitializedGlobals;
    return g;
}

void FrameBuffer::waitUntilInitialized() {
    if (sInitialized.load(std::memory_order_relaxed)) {
        return;
    }

#if SNAPSHOT_PROFILE > 1
    const auto startTime = gfxstream::base::getHighResTimeUs();
#endif
    {
        AutoLock l(sGlobals()->lock);
        sGlobals()->condVar.wait(
                &l, [] { return sInitialized.load(std::memory_order_acquire); });
    }
#if SNAPSHOT_PROFILE > 1
    printf("Waited for FrameBuffer initialization for %.03f ms\n",
           (gfxstream::base::getHighResTimeUs() - startTime) / 1000.0);
#endif
}

void MaybeIncreaseFileDescriptorSoftLimit() {
#if defined(__linux__)
    // Cuttlefish with Gfxstream on Nvidia and SwiftShader often hits the default nofile
    // soft limit (1024) when running large test suites.
    struct rlimit nofileLimits = {
        .rlim_cur = 0,
        .rlim_max = 0,
    };

    int ret = getrlimit(RLIMIT_NOFILE, &nofileLimits);
    if (ret) {
        GFXSTREAM_ERROR("Warning: failed to query nofile limits.");
        return;
    }

    const auto softLimit = nofileLimits.rlim_cur;
    const auto hardLimit = nofileLimits.rlim_max;

    constexpr const rlim_t kDesiredNofileSoftLimit = 4096;

    if (softLimit < kDesiredNofileSoftLimit) {
        if (softLimit == hardLimit) {
            GFXSTREAM_ERROR("Warning: unable to raise nofile soft limit - already at hard limit.");
            return;
        }

        if (kDesiredNofileSoftLimit > hardLimit) {
            GFXSTREAM_ERROR(
                "Warning: unable to raise nofile soft limit to desired %d - hard limit is %d.",
                static_cast<int>(kDesiredNofileSoftLimit), static_cast<int>(hardLimit));
        }

        const rlim_t requestedSoftLimit = std::min(kDesiredNofileSoftLimit, hardLimit);

        struct rlimit requestedNofileLimits = {
            .rlim_cur = requestedSoftLimit,
            .rlim_max = hardLimit,
        };

        ret = setrlimit(RLIMIT_NOFILE, &requestedNofileLimits);
        if (ret) {
            GFXSTREAM_ERROR("Warning: failed to raise nofile soft limit to %d: %s (%d)",
                            static_cast<int>(requestedSoftLimit), strerror(errno), errno);
            return;
        }

        GFXSTREAM_INFO("Raised nofile soft limit to %d.", static_cast<int>(requestedSoftLimit));
    } else {
        GFXSTREAM_INFO("Not raising nofile soft limit from %d.", static_cast<int>(softLimit));
    }
#endif
}

bool FrameBuffer::initialize(int width, int height, const gfxstream::host::FeatureSet& features,
                             bool useSubWindow, bool egl2egl) {
    GFXSTREAM_DEBUG("FrameBuffer::initialize");

    if (s_theFrameBuffer != NULL) {
        return true;
    }

    MaybeIncreaseFileDescriptorSoftLimit();

    gfxstream::base::initializeTracing();
    gfxstream::host::InitializeTracing();

    //
    // allocate space for the FrameBuffer object
    //
    std::unique_ptr<FrameBuffer> fb(new FrameBuffer(width, height, features, useSubWindow));
    if (!fb) {
        GFXSTREAM_DEBUG("Failed to create fb");
        GFXSTREAM_ERROR("Failed to create fb\n");
        return false;
    }

    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "FrameBuffer::Init()");

    std::unique_ptr<emugl::RenderDocWithMultipleVkInstances> renderDocMultipleVkInstances = nullptr;
    if (!gfxstream::base::getEnvironmentVariable("ANDROID_EMU_RENDERDOC").empty()) {
        SharedLibrary* renderdocLib = nullptr;
#ifdef _WIN32
        renderdocLib = SharedLibrary::open(R"(C:\Program Files\RenderDoc\renderdoc.dll)");
#elif defined(__linux__)
        renderdocLib = SharedLibrary::open("librenderdoc.so");
#endif
        fb->m_renderDoc = emugl::RenderDoc::create(renderdocLib);
        if (fb->m_renderDoc) {
            GFXSTREAM_INFO("RenderDoc integration enabled.");
            renderDocMultipleVkInstances =
                std::make_unique<emugl::RenderDocWithMultipleVkInstances>(*fb->m_renderDoc);
            if (!renderDocMultipleVkInstances) {
                GFXSTREAM_ERROR(
                    "Failed to initialize RenderDoc with multiple VkInstances. Can't capture any "
                    "information from guest VkInstances with RenderDoc.");
            }
        }
    }
    // Initialize Vulkan emulation state
    //
    // Note: This must happen before any use of s_egl,
    // or it's possible that the existing EGL display and contexts
    // used by underlying EGL driver might become invalid,
    // preventing new contexts from being created that share
    // against those contexts.
    vk::VulkanDispatch* vkDispatch = nullptr;
    if (fb->m_features.Vulkan.enabled) {
        vkDispatch = vk::vkDispatch(false /* not for testing */);

        gfxstream::host::BackendCallbacks callbacks{
            .registerProcessCleanupCallback =
                [fb = fb.get()](void* key, uint64_t contextId, std::function<void()> callback) {
                    fb->registerProcessCleanupCallback(key, contextId, callback);
                },
            .unregisterProcessCleanupCallback =
                [fb = fb.get()](void* key) { fb->unregisterProcessCleanupCallback(key); },
            .invalidateColorBuffer =
                [fb = fb.get()](uint32_t colorBufferHandle) {
                    fb->invalidateColorBufferForVk(colorBufferHandle);
                },
            .flushColorBuffer =
                [fb = fb.get()](uint32_t colorBufferHandle) {
                    fb->flushColorBufferFromVk(colorBufferHandle);
                },
            .flushColorBufferFromBytes =
                [fb = fb.get()](uint32_t colorBufferHandle, const void* bytes, size_t bytesSize) {
                    fb->flushColorBufferFromVkBytes(colorBufferHandle, bytes, bytesSize);
                },
            .scheduleAsyncWork =
                [fb = fb.get()](std::function<void()> work, std::string description) {
                    auto promise = std::make_shared<AutoCancelingPromise>();
                    auto future = promise->GetFuture();
                    SyncThread::get()->triggerGeneral(
                        [promise = std::move(promise), work = std::move(work)]() mutable {
                            work();
                            promise->MarkComplete();
                        },
                        description);
                    return future;
                },
#ifdef CONFIG_AEMU
            .registerVulkanInstance =
                [fb = fb.get()](uint64_t id, const char* appName) {
                    fb->registerVulkanInstance(id, appName);
                },
            .unregisterVulkanInstance =
                [fb = fb.get()](uint64_t id) { fb->unregisterVulkanInstance(id); },
#endif
        };
        fb->m_emulationVk = vk::VkEmulation::create(vkDispatch, callbacks, fb->m_features);
        if (fb->m_emulationVk) {
            vk::VkDecoderGlobalState::initialize(fb->m_emulationVk.get());
        } else {
            GFXSTREAM_ERROR(
                "Failed to initialize global Vulkan emulation. Disable the Vulkan support.");
        }
    }
    if (fb->m_emulationVk) {
        fb->m_vulkanEnabled = true;
        if (fb->m_features.VulkanNativeSwapchain.enabled) {
            fb->m_vkInstance = fb->m_emulationVk->getInstance();
        }

        auto vulkanUuidOpt = fb->m_emulationVk->getDeviceUuid();
        if (vulkanUuidOpt) {
            fb->m_vulkanUUID = *vulkanUuidOpt;
        } else {
            GFXSTREAM_WARNING("Doesn't support id properties, no vulkan device UUID");
        }
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    // Do not initialize GL emulation if the guest is using ANGLE.
    if (!fb->m_features.GuestVulkanOnly.enabled) {
        fb->m_emulationGl = EmulationGl::create(width, height, fb->m_features, useSubWindow, egl2egl);
        if (!fb->m_emulationGl) {
            GFXSTREAM_ERROR("Failed to initialize GL emulation.");
            return false;
        }
    }
#endif

    fb->m_useVulkanComposition = fb->m_features.GuestVulkanOnly.enabled ||
                                 fb->m_features.VulkanNativeSwapchain.enabled;

    vk::VkEmulation::Features vkEmulationFeatures = {
        .glInteropSupported = false,  // Set later.
        .deferredCommands =
            gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_DISABLE_DEFERRED_COMMANDS")
                .empty(),
        .createResourceWithRequirements =
            gfxstream::base::getEnvironmentVariable(
                "ANDROID_EMU_VK_DISABLE_USE_CREATE_RESOURCES_WITH_REQUIREMENTS")
                .empty(),
        .useVulkanComposition = fb->m_useVulkanComposition,
        .useVulkanNativeSwapchain = fb->m_features.VulkanNativeSwapchain.enabled,
        .guestRenderDoc = std::move(renderDocMultipleVkInstances),
        .astcLdrEmulationMode = AstcEmulationMode::Gpu,
        .enableEtc2Emulation = true,
        .enableYcbcrEmulation = false,
        .guestVulkanOnly = fb->m_features.GuestVulkanOnly.enabled,
        .useDedicatedAllocations = false,  // Set later.
    };

    //
    // Cache the GL strings so we don't have to think about threading or
    // current-context when asked for them.
    //
    bool useVulkanGraphicsDiagInfo = fb->m_emulationVk &&
                                     fb->m_features.VulkanNativeSwapchain.enabled &&
                                     fb->m_features.GuestVulkanOnly.enabled;

    if (useVulkanGraphicsDiagInfo) {
        fb->m_graphicsAdapterVendor = fb->m_emulationVk->getGpuVendor();
        fb->m_graphicsAdapterName = fb->m_emulationVk->getGpuName();
        fb->m_graphicsApiVersion = fb->m_emulationVk->getGpuVersionString();
        fb->m_graphicsApiExtensions = fb->m_emulationVk->getInstanceExtensionsString();
        fb->m_graphicsDeviceExtensions = fb->m_emulationVk->getDeviceExtensionsString();
    } else if (fb->m_emulationGl) {
#if GFXSTREAM_ENABLE_HOST_GLES
        fb->m_graphicsAdapterVendor = fb->m_emulationGl->getGlesVendor();
        fb->m_graphicsAdapterName = fb->m_emulationGl->getGlesRenderer();
        fb->m_graphicsApiVersion = fb->m_emulationGl->getGlesVersionString();
        fb->m_graphicsApiExtensions = fb->m_emulationGl->getGlesExtensionsString();
        fb->m_graphicsDeviceExtensions = "N/A";
#endif
    } else {
        fb->m_graphicsAdapterVendor = "N/A";
        fb->m_graphicsAdapterName = "N/A";
        fb->m_graphicsApiVersion = "N/A";
        fb->m_graphicsApiExtensions = "N/A";
        fb->m_graphicsDeviceExtensions = "N/A";
    }

    // Attempt to get the device UUID of the gles and match with Vulkan. If
    // they match, interop is possible. If they don't, then don't trust the
    // result of interop query to egl and fall back to CPU copy, as we might
    // have initialized Vulkan devices and GLES contexts from different
    // physical devices.

    bool vulkanInteropSupported = true;
    // First, if the VkEmulation instance doesn't support ext memory capabilities,
    // it won't support uuids.
    if (!fb->m_emulationVk || !fb->m_emulationVk->supportsPhysicalDeviceIDProperties()) {
        vulkanInteropSupported = false;
    }
    if (!fb->m_emulationGl) {
        vulkanInteropSupported = false;
    } else {
#if GFXSTREAM_ENABLE_HOST_GLES
        if (!fb->m_emulationGl->isGlesVulkanInteropSupported()) {
            vulkanInteropSupported = false;
        }
        const auto& glesDeviceUuid = fb->m_emulationGl->getGlesDeviceUuid();
        if (!glesDeviceUuid  || glesDeviceUuid != fb->m_vulkanUUID) {
            vulkanInteropSupported = false;
        }
#endif
    }

    if (gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_ICD") == "lavapipe"
            || gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_ICD") == "swiftshader") {
        vulkanInteropSupported = false;
        GFXSTREAM_DEBUG("vk icd software rendering, disable interop");
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (vulkanInteropSupported && fb->m_emulationGl && fb->m_emulationGl->isMesa()) {
        // Mesa currently expects dedicated allocations for external memory sharing
        // between GL and VK. See b/265186355.
        vkEmulationFeatures.useDedicatedAllocations = true;
    }
#endif

    GFXSTREAM_DEBUG("glvk interop final: %d", vulkanInteropSupported);
    vkEmulationFeatures.glInteropSupported = vulkanInteropSupported;
    if (fb->m_emulationVk && fb->m_features.Vulkan.enabled) {
        fb->m_emulationVk->initFeatures(std::move(vkEmulationFeatures));

        auto* display = fb->m_emulationVk->getDisplay();
        if (display) {
            fb->m_displayVk = display;
            fb->m_displaySurfaceUsers.push_back(fb->m_displayVk);
        }
    }

    if (fb->m_emulationVk && fb->m_useVulkanComposition) {
        fb->m_compositor = fb->m_emulationVk->getCompositor();
        if (!fb->m_compositor) {
            GFXSTREAM_ERROR("Failed to get CompositorVk from VkEmulation.");
            return false;
        }
        GFXSTREAM_DEBUG("Performing composition using CompositorVk.");
    } else {
        GFXSTREAM_DEBUG("Performing composition using CompositorGl.");
#if GFXSTREAM_ENABLE_HOST_GLES
        auto compositorGl = fb->m_emulationGl->getCompositor();
        fb->m_compositor = compositorGl;
#endif
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (fb->m_emulationGl) {
        auto displayGl = fb->m_emulationGl->getDisplay();
        fb->m_displayGl = displayGl;
        fb->m_displaySurfaceUsers.push_back(displayGl);
    }
#endif

    GFXSTREAM_INFO("Graphics Adapter Vendor %s", fb->m_graphicsAdapterVendor.c_str());
    GFXSTREAM_INFO("Graphics Adapter %s", fb->m_graphicsAdapterName.c_str());
    GFXSTREAM_INFO("Graphics API Version %s", fb->m_graphicsApiVersion.c_str());
    GFXSTREAM_INFO("Graphics API Extensions %s", fb->m_graphicsApiExtensions.c_str());
    GFXSTREAM_INFO("Graphics Device Extensions %s", fb->m_graphicsDeviceExtensions.c_str());

    if (fb->m_useVulkanComposition) {
        fb->m_postWorker.reset(new PostWorkerVk(fb.get(), fb->m_compositor, fb->m_displayVk));
    } else {
        const bool shouldPostOnlyOnMainThread = postOnlyOnMainThread();

#if GFXSTREAM_ENABLE_HOST_GLES
        PostWorkerGl* postWorkerGl =
            new PostWorkerGl(shouldPostOnlyOnMainThread, fb.get(), fb->m_compositor,
                             fb->m_displayGl, fb->m_emulationGl.get());
        fb->m_postWorker.reset(postWorkerGl);
        fb->m_displaySurfaceUsers.push_back(postWorkerGl);
#endif
    }

    // Start up the single sync thread. If we are using Vulkan native
    // swapchain, then don't initialize SyncThread worker threads with EGL
    // contexts.
    SyncThread::initialize(
        /* hasGL */ fb->m_emulationGl != nullptr, fb->getHealthMonitor());

    // Start the vsync thread
    const uint64_t kOneSecondNs = 1000000000ULL;
    fb->m_vsyncThread.reset(new VsyncThread((uint64_t)kOneSecondNs / (uint64_t)fb->m_vsyncHz));

    //
    // Keep the singleton framebuffer pointer
    //
    s_theFrameBuffer = fb.release();
    {
        AutoLock lock(sGlobals()->lock);
        sInitialized.store(true, std::memory_order_release);
        sGlobals()->condVar.broadcastAndUnlock(&lock);
    }

    // Nothing else to do - we're ready to rock!
    return true;
}

void FrameBuffer::finalize() {
    FrameBuffer* fb = s_theFrameBuffer;
    s_theFrameBuffer = nullptr;
    if (fb) {
        delete fb;
    }
}

FrameBuffer::FrameBuffer(int p_width, int p_height, const gfxstream::host::FeatureSet& features, bool useSubWindow)
    : m_features(features),
      m_framebufferWidth(p_width),
      m_framebufferHeight(p_height),
      m_windowWidth(p_width),
      m_windowHeight(p_height),
      m_useSubWindow(useSubWindow),
      m_fpsStats(getenv("SHOW_FPS_STATS") != nullptr),
      m_readbackThread(
          [this](FrameBuffer::Readback&& readback) { return sendReadbackWorkerCmd(readback); }),
      m_refCountPipeEnabled(features.RefCountPipe.enabled),
      m_noDelayCloseColorBufferEnabled(features.NoDelayCloseColorBuffer.enabled ||
                                       features.Minigbm.enabled),
      m_postThread([this](Post&& post) { return postWorkerFunc(post); }),
      m_logger(CreateMetricsLogger()),
      m_healthMonitor(CreateHealthMonitor(*m_logger)) {
    mDisplayActiveConfigId = 0;
    mDisplayConfigs[0] = {p_width, p_height, 160, 160};
    uint32_t displayId = 0;
    if (createDisplay(&displayId) < 0) {
        GFXSTREAM_ERROR( "Failed to create default display");
    }

    setDisplayPose(displayId, 0, 0, getWidth(), getHeight(), 0);
}

FrameBuffer::~FrameBuffer() {
    AutoLock fbLock(m_lock);

    m_postThread.enqueue({PostCmd::Exit});
    m_postThread.join();
    m_postWorker.reset();

    // Run other cleanup callbacks
    // Avoid deadlock by first storing a separate list of callbacks
    std::vector<std::function<void()>> callbacks;
    for (auto procIte : m_procOwnedCleanupCallbacks)
    {
        for (auto it : procIte.second) {
            callbacks.push_back(it.second);
        }
    }
    m_procOwnedCleanupCallbacks.clear();

    fbLock.unlock();

    for (auto cb : callbacks) {
        cb();
    }

    fbLock.lock();

    if (m_useSubWindow) {
        removeSubWindow_locked();
    }

    m_readbackThread.enqueue({ReadbackCmd::Exit});
    m_readbackThread.join();

    m_vsyncThread.reset();

    SyncThread::destroy();

    sweepColorBuffersLocked();

    m_buffers.clear();
    {
        AutoLock lock(m_colorBufferMapLock);
        m_colorbuffers.clear();
    }
    m_colorBufferDelayedCloseList.clear();

#if GFXSTREAM_ENABLE_HOST_GLES
    m_windows.clear();
    m_contexts.clear();

    for (auto it : m_platformEglContexts) {
        destroySharedTrivialContext(it.second.context, it.second.surface);
    }
#endif

    if (m_emulationGl) {
        m_emulationGl.reset();
    }
    if (m_emulationVk) {
        m_emulationVk.reset();
    }

    sInitialized.store(false, std::memory_order_relaxed);
}

WorkerProcessingResult
FrameBuffer::sendReadbackWorkerCmd(const Readback& readback) {
    ensureReadbackWorker();
    switch (readback.cmd) {
    case ReadbackCmd::Init:
        m_readbackWorker->init();
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::GetPixels:
        m_readbackWorker->getPixels(readback.displayId, readback.pixelsOut, readback.bytes);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::AddRecordDisplay:
        m_readbackWorker->initReadbackForDisplay(readback.displayId, readback.width, readback.height);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::DelRecordDisplay:
        m_readbackWorker->deinitReadbackForDisplay(readback.displayId);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::Exit:
        return WorkerProcessingResult::Stop;
    }
    return WorkerProcessingResult::Stop;
}

WorkerProcessingResult FrameBuffer::postWorkerFunc(Post& post) {
    auto annotations = std::make_unique<EventHangMetadata::HangAnnotations>();
    if (m_healthMonitor)
        annotations->insert(
            {"Post command opcode", std::to_string(static_cast<uint64_t>(post.cmd))});
    auto watchdog = WATCHDOG_BUILDER(m_healthMonitor.get(), "PostWorker main function")
                        .setAnnotations(std::move(annotations))
                        .build();
    switch (post.cmd) {
        case PostCmd::Post: {
            // We wrap the callback like this to workaround a bug in the MS STL implementation.
            auto packagePostCmdCallback =
                std::shared_ptr<Post::CompletionCallback>(std::move(post.completionCallback));
            std::unique_ptr<Post::CompletionCallback> postCallback =
                std::make_unique<Post::CompletionCallback>(
                    [packagePostCmdCallback](std::shared_future<void> waitForGpu) {
                        SyncThread::get()->triggerGeneral(
                            [composeCallback = std::move(packagePostCmdCallback), waitForGpu] {
                                (*composeCallback)(waitForGpu);
                            },
                            "Wait for post");
                    });
            m_postWorker->post(post.cb, std::move(postCallback));
            decColorBufferRefCountNoDestroy(post.cbHandle);
            break;
        }
        case PostCmd::Viewport:
            m_postWorker->viewport(post.viewport.width,
                                   post.viewport.height);
            break;
        case PostCmd::Compose: {
            std::unique_ptr<FlatComposeRequest> composeRequest;
            std::unique_ptr<Post::CompletionCallback> composeCallback;
            if (post.composeVersion <= 1) {
                composeCallback = std::move(post.completionCallback);
                composeRequest = ToFlatComposeRequest((ComposeDevice*)post.composeBuffer.data());
            } else {
                // std::shared_ptr(std::move(...)) is WA for MSFT STL implementation bug:
                // https://developercommunity.visualstudio.com/t/unable-to-move-stdpackaged-task-into-any-stl-conta/108672
                auto packageComposeCallback =
                    std::shared_ptr<Post::CompletionCallback>(std::move(post.completionCallback));
                composeCallback = std::make_unique<Post::CompletionCallback>(
                    [packageComposeCallback](
                        std::shared_future<void> waitForGpu) {
                        SyncThread::get()->triggerGeneral(
                            [composeCallback = std::move(packageComposeCallback), waitForGpu] {
                                (*composeCallback)(waitForGpu);
                            },
                            "Wait for host composition");
                    });
                composeRequest = ToFlatComposeRequest((ComposeDevice_v2*)post.composeBuffer.data());
            }
            m_postWorker->compose(std::move(composeRequest), std::move(composeCallback));
            break;
        }
        case PostCmd::Clear:
            m_postWorker->clear();
            break;
        case PostCmd::Screenshot:
            m_postWorker->screenshot(
                    post.screenshot.cb, post.screenshot.screenwidth,
                    post.screenshot.screenheight, post.screenshot.format,
                    post.screenshot.type, post.screenshot.rotation,
                    post.screenshot.pixels, post.screenshot.rect);
            decColorBufferRefCountNoDestroy(post.cbHandle);
            break;
        case PostCmd::Block:
            m_postWorker->block(std::move(post.block->scheduledSignal),
                                std::move(post.block->continueSignal));
            break;
        case PostCmd::Exit:
            m_postWorker->exit();
            return WorkerProcessingResult::Stop;
        default:
            break;
    }
    return WorkerProcessingResult::Continue;
}

std::future<void> FrameBuffer::sendPostWorkerCmd(Post post) {
    bool expectedPostThreadStarted = false;
    if (m_postThreadStarted.compare_exchange_strong(expectedPostThreadStarted, true)) {
        m_postThread.start();
    }

    bool shouldPostOnlyOnMainThread = postOnlyOnMainThread();
    // If we want to run only in the main thread and we are actually running
    // in the main thread already, don't use the PostWorker thread. Ideally,
    // PostWorker should handle this and dispatch directly, but we'll need to
    // transfer ownership of the thread to PostWorker.
    // TODO(lfy): do that refactor
    // For now, this fixes a screenshot issue on macOS.
    std::future<void> res = std::async(std::launch::deferred, [] {});
    res.wait();
    if (shouldPostOnlyOnMainThread && (PostCmd::Screenshot == post.cmd) &&
        get_gfxstream_window_operations().is_current_thread_ui_thread()) {
        post.cb->readToBytesScaled(post.screenshot.screenwidth, post.screenshot.screenheight,
                                   post.screenshot.format, post.screenshot.type,
                                   post.screenshot.rotation, post.screenshot.rect,
                                   post.screenshot.pixels);
    } else {
        std::future<void> completeFuture =
            m_postThread.enqueue(Post(std::move(post)));
        if (!shouldPostOnlyOnMainThread ||
            (PostCmd::Screenshot == post.cmd &&
             !get_gfxstream_window_operations().is_current_thread_ui_thread())) {
            res = std::move(completeFuture);
        }
    }
    return res;
}

void FrameBuffer::setPostCallback(Renderer::OnPostCallback onPost, void* onPostContext,
                                  uint32_t displayId, bool useBgraReadback) {
    AutoLock lock(m_lock);
    if (onPost) {
        uint32_t w, h;
        if (!get_gfxstream_multi_display_operations().get_display_info(
                displayId, nullptr, nullptr, &w, &h, nullptr, nullptr, nullptr)) {
            GFXSTREAM_ERROR("display %d not exist, cancelling OnPost callback", displayId);
            return;
        }
        if (m_onPost.find(displayId) != m_onPost.end()) {
            GFXSTREAM_ERROR("display %d already configured for recording", displayId);
            return;
        }
        m_onPost[displayId].cb = onPost;
        m_onPost[displayId].context = onPostContext;
        m_onPost[displayId].displayId = displayId;
        m_onPost[displayId].width = w;
        m_onPost[displayId].height = h;
        m_onPost[displayId].img = new unsigned char[4 * w * h];
        m_onPost[displayId].readBgra = useBgraReadback;
        bool expectedReadbackThreadStarted = false;
        if (m_readbackThreadStarted.compare_exchange_strong(expectedReadbackThreadStarted, true)) {
            m_readbackThread.start();
            m_readbackThread.enqueue({ ReadbackCmd::Init });
        }
        std::future<void> completeFuture = m_readbackThread.enqueue(
            {ReadbackCmd::AddRecordDisplay, displayId, nullptr, 0, w, h});
        completeFuture.wait();
    } else {
        std::future<void> completeFuture = m_readbackThread.enqueue(
            {ReadbackCmd::DelRecordDisplay, displayId});
        completeFuture.wait();
        m_onPost.erase(displayId);
    }
}

static void subWindowRepaint(void* param) {
    GFXSTREAM_DEBUG("call repost from subWindowRepaint callback");
    auto fb = static_cast<FrameBuffer*>(param);
    fb->repost();
}

bool FrameBuffer::setupSubWindow(FBNativeWindowType p_window,
                                 int wx,
                                 int wy,
                                 int ww,
                                 int wh,
                                 int fbw,
                                 int fbh,
                                 float dpr,
                                 float zRot,
                                 bool deleteExisting,
                                 bool hideWindow) {
    GFXSTREAM_DEBUG("Begin setupSubWindow");
    if (!m_useSubWindow) {
        GFXSTREAM_ERROR("%s: Cannot create native sub-window in this configuration\n",
                        __FUNCTION__);
        return false;
    }

    // Do a quick check before even taking the lock - maybe we don't need to
    // do anything here.

    const bool shouldCreateSubWindow = !m_subWin || deleteExisting;

    // On Mac, since window coordinates are Y-up and not Y-down, the
    // subwindow may not change dimensions, but because the main window
    // did, the subwindow technically needs to be re-positioned. This
    // can happen on rotation, so a change in Z-rotation can be checked
    // for this case. However, this *should not* be done on Windows/Linux,
    // because the functions used to resize a native window on those hosts
    // will block if the shape doesn't actually change, freezing the
    // emulator.
    const bool shouldMoveSubWindow =
        !shouldCreateSubWindow &&
        !(m_x == wx && m_y == wy && m_windowWidth == ww && m_windowHeight == wh
#if defined(__APPLE__)
          && m_zRot == zRot
#endif
        );

    const bool redrawSubwindow =
        shouldCreateSubWindow || shouldMoveSubWindow || m_zRot != zRot || m_dpr != dpr ||
        m_windowContentFullWidth != fbw || m_windowContentFullHeight != fbh;
    if (!shouldCreateSubWindow && !shouldMoveSubWindow && !redrawSubwindow) {
        assert(sInitialized.load(std::memory_order_relaxed));
        GFXSTREAM_DEBUG("Exit setupSubWindow (nothing to do)");
#if SNAPSHOT_PROFILE > 1
        // printf("FrameBuffer::%s(): nothing to do at %lld ms\n", __func__,
               // (long long)System::get()->getProcessTimes().wallClockMs);
#endif
        return true;
    }

#if SNAPSHOT_PROFILE > 1
    // printf("FrameBuffer::%s(%s): start at %lld ms\n", __func__,
    //        deleteExisting ? "deleteExisting" : "keepExisting",
    //        (long long)System::get()->getProcessTimes().wallClockMs);
#endif
    class ScopedPromise {
       public:
        ~ScopedPromise() { mPromise.set_value(); }
        std::future<void> getFuture() { return mPromise.get_future(); }
        DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedPromise);
        static std::tuple<std::unique_ptr<ScopedPromise>, std::future<void>> create() {
            auto scopedPromise = std::unique_ptr<ScopedPromise>(new ScopedPromise());
            auto future = scopedPromise->mPromise.get_future();
            return std::make_tuple(std::move(scopedPromise), std::move(future));
        }

       private:
        ScopedPromise() = default;
        std::promise<void> mPromise;
    };
    std::unique_ptr<ScopedPromise> postWorkerContinueSignal;
    std::future<void> postWorkerContinueSignalFuture;
    std::tie(postWorkerContinueSignal, postWorkerContinueSignalFuture) = ScopedPromise::create();
    {
        auto watchdog =
            WATCHDOG_BUILDER(m_healthMonitor.get(), "Wait for other tasks on PostWorker")
                .setTimeoutMs(6000)
                .build();
        blockPostWorker(std::move(postWorkerContinueSignalFuture)).wait();
    }
    if (m_displayVk) {
        auto watchdog = WATCHDOG_BUILDER(m_healthMonitor.get(), "Draining the VkQueue")
                            .setTimeoutMs(6000)
                            .build();
        m_displayVk->drainQueues();
    }
    auto lockWatchdog =
        WATCHDOG_BUILDER(m_healthMonitor.get(), "Wait for the FrameBuffer global lock").build();
    auto lockWatchdogId = lockWatchdog->release();
    AutoLock mutex(m_lock);
    if (lockWatchdogId.has_value()) {
        m_healthMonitor->stopMonitoringTask(lockWatchdogId.value());
    }

    if (deleteExisting) {
        removeSubWindow_locked();
    }

    bool success = false;

    // If the subwindow doesn't exist, create it with the appropriate dimensions
    if (!m_subWin) {
        // Create native subwindow for FB display output
        m_x = wx;
        m_y = wy;
        m_windowWidth = ww;
        m_windowHeight = wh;

        if (!hideWindow) {
            m_subWin = createSubWindow(p_window, m_x, m_y, m_windowWidth, m_windowHeight, dpr,
                                       subWindowRepaint, this, hideWindow);
        }
        if (m_subWin) {
            m_nativeWindow = p_window;

            if (m_displayVk) {
                m_displaySurface = m_emulationVk->createDisplaySurface(
                    m_subWin, m_windowWidth * dpr, m_windowHeight * dpr);
            } else if (m_emulationGl) {
#if GFXSTREAM_ENABLE_HOST_GLES
                m_displaySurface = m_emulationGl->createWindowSurface(m_windowWidth * dpr,
                                                                      m_windowHeight * dpr,
                                                                      m_subWin);
#endif
            } else {
                GFXSTREAM_FATAL("Unhandled window surface creation.");
            }

            if (m_displaySurface) {
                // Some backends use a default display surface. Unbind from that before
                // binding the new display surface. which potentially needs to be unbound.
                for (auto* displaySurfaceUser : m_displaySurfaceUsers) {
                    displaySurfaceUser->unbindFromSurface();
                }

                // TODO: Make RenderDoc a DisplaySurfaceUser.
                if (m_displayVk) {
                    if (m_renderDoc) {
                        m_renderDoc->call(emugl::RenderDoc::kSetActiveWindow,
                                          RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_vkInstance),
                                          reinterpret_cast<RENDERDOC_WindowHandle>(m_subWin));
                    }
                }

                m_px = 0;
                m_py = 0;
                for (auto* displaySurfaceUser : m_displaySurfaceUsers) {
                    displaySurfaceUser->bindToSurface(m_displaySurface.get());
                }
                success = true;
            } else {
                // Display surface creation failed.
                if (m_emulationGl) {
                    // NOTE: This can typically happen with software-only renderers like OSMesa.
                    destroySubWindow(m_subWin);
                    m_subWin = (EGLNativeWindowType)0;
                } else {
                    GFXSTREAM_FATAL("Failed to create DisplaySurface.");
                }
            }
        }
    }

    auto watchdog = WATCHDOG_BUILDER(m_healthMonitor.get(), "Updating subwindow state").build();
    // At this point, if the subwindow doesn't exist, it is because it either
    // couldn't be created
    // in the first place or the EGLSurface couldn't be created.
    if (m_subWin) {
        if (!shouldMoveSubWindow) {
            // Ensure that at least viewport parameters are properly updated.
            success = true;
        } else {
            // Only attempt to update window geometry if anything has actually
            // changed.
            m_x = wx;
            m_y = wy;
            m_windowWidth = ww;
            m_windowHeight = wh;

            {
                auto moveWatchdog =
                    WATCHDOG_BUILDER(m_healthMonitor.get(), "Moving subwindow").build();
                success = moveSubWindow(m_nativeWindow, m_subWin, m_x, m_y, m_windowWidth,
                                        m_windowHeight, dpr);
            }
            m_displaySurface->updateSize(m_windowWidth * dpr, m_windowHeight * dpr);
        }
        // We are safe to unblock the PostWorker thread now, because we have completed all the
        // operations that could modify the state of the m_subWin. We need to unblock the PostWorker
        // here because we may need to send and wait for other tasks dispatched to the PostWorker
        // later, e.g. the viewport command or the post command issued later.
        postWorkerContinueSignal.reset();

        if (success && redrawSubwindow) {
            // Subwin creation or movement was successful,
            // update viewport and z rotation and draw
            // the last posted color buffer.
            m_dpr = dpr;
            m_zRot = zRot;
            if (m_displayVk == nullptr) {
                Post postCmd;
                postCmd.cmd = PostCmd::Viewport;
                postCmd.viewport.width = fbw;
                postCmd.viewport.height = fbh;
                sendPostWorkerCmd(std::move(postCmd));

                if (m_lastPostedColorBuffer) {
                    GFXSTREAM_DEBUG("setupSubwindow: draw last posted cb");
                    postImpl(m_lastPostedColorBuffer,
                        [](std::shared_future<void> waitForGpu) {}, false);
                } else {
                    Post clearCmd;
                    clearCmd.cmd = PostCmd::Clear;
                    sendPostWorkerCmd(std::move(clearCmd));
                }
            }
            m_windowContentFullWidth = fbw;
            m_windowContentFullHeight = fbh;
        }
    }

    mutex.unlock();

    // Nobody ever checks for the return code, so there will be no retries or
    // even aborted run; if we don't mark the framebuffer as initialized here
    // its users will hang forever; if we do mark it, they will crash - which
    // is a better outcome (crash report == bug fixed).
    AutoLock lock(sGlobals()->lock);
    sInitialized.store(true, std::memory_order_relaxed);
    sGlobals()->condVar.broadcastAndUnlock(&lock);

#if SNAPSHOT_PROFILE > 1
    // printf("FrameBuffer::%s(): end at %lld ms\n", __func__,
    //        (long long)System::get()->getProcessTimes().wallClockMs);
#endif

    GFXSTREAM_DEBUG("Exit setupSubWindow (successful setup)");
    return success;
}

bool FrameBuffer::removeSubWindow() {
    if (!m_useSubWindow) {
        GFXSTREAM_ERROR("Cannot remove native sub-window in this configuration");
        return false;
    }
    AutoLock lock(sGlobals()->lock);
    sInitialized.store(false, std::memory_order_relaxed);
    sGlobals()->condVar.broadcastAndUnlock(&lock);

    AutoLock mutex(m_lock);
    return removeSubWindow_locked();
}

bool FrameBuffer::removeSubWindow_locked() {
    if (!m_useSubWindow) {
        GFXSTREAM_ERROR("Cannot remove native sub-window in this configuration");
        return false;
    }
    bool removed = false;
    if (m_subWin) {
        for (auto* displaySurfaceUser : m_displaySurfaceUsers) {
            displaySurfaceUser->unbindFromSurface();
        }
        m_displaySurface.reset();

        destroySubWindow(m_subWin);

        m_subWin = (EGLNativeWindowType)0;
        removed = true;
    }
    return removed;
}

HandleType FrameBuffer::genHandle_locked() {
    HandleType id;
    do {
        id = ++s_nextHandle;
    } while (id == 0 ||
#if GFXSTREAM_ENABLE_HOST_GLES
             m_contexts.find(id) != m_contexts.end() || m_windows.find(id) != m_windows.end() ||
#endif
             m_colorbuffers.find(id) != m_colorbuffers.end() ||
             m_buffers.find(id) != m_buffers.end());

    return id;
}

bool FrameBuffer::isFormatSupported(GLenum format) {
    bool supported = true;
    if (m_emulationGl) {
        supported &= m_emulationGl->isFormatSupported(format);
    }
    if (m_emulationVk) {
        supported &= m_emulationVk->isFormatSupported(format);
    }
    return supported;
}

HandleType FrameBuffer::createColorBuffer(int p_width,
                                          int p_height,
                                          GLenum p_internalFormat,
                                          FrameworkFormat p_frameworkFormat) {

    AutoLock mutex(m_lock);
    sweepColorBuffersLocked();
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    return createColorBufferWithResourceHandleLocked(p_width, p_height, p_internalFormat,
                                                     p_frameworkFormat, genHandle_locked());
}

void FrameBuffer::createColorBufferWithResourceHandle(int p_width, int p_height,
                                                      GLenum p_internalFormat,
                                                      FrameworkFormat p_frameworkFormat,
                                                      HandleType handle) {
    {
        AutoLock mutex(m_lock);
        sweepColorBuffersLocked();

        AutoLock colorBufferMapLock(m_colorBufferMapLock);

        // Check for handle collision
        if (m_colorbuffers.count(handle) != 0) {
            // emugl::emugl_crash_reporter(
            //     "FATAL: color buffer with handle %u already exists",
            //     handle);
            GFXSTREAM_FATAL("");
        }

        createColorBufferWithResourceHandleLocked(p_width, p_height, p_internalFormat,
                                                  p_frameworkFormat, handle);
    }
}

HandleType FrameBuffer::createColorBufferWithResourceHandleLocked(int p_width, int p_height,
                                                                  GLenum p_internalFormat,
                                                                  FrameworkFormat p_frameworkFormat,
                                                                  HandleType handle) {
    ColorBufferPtr cb =
        ColorBuffer::create(m_emulationGl.get(), m_emulationVk.get(), p_width, p_height,
                            p_internalFormat, p_frameworkFormat, handle, nullptr /*stream*/);
    if (cb.get() == nullptr) {
        GFXSTREAM_FATAL("Failed to create ColorBuffer:%d format:%d framework-format:%d with:%d height:%d",
                        handle, p_internalFormat, p_frameworkFormat, p_width, p_height);
    }

    assert(m_colorbuffers.count(handle) == 0);
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed. We only memoize the mapping from handle to ColorBuffer.
    // Explicitly set refcount to 1 to avoid the colorbuffer being added to
    // m_colorBufferDelayedCloseList in FrameBuffer::onLoad().
    if (m_refCountPipeEnabled) {
        m_colorbuffers.try_emplace(handle, ColorBufferRef{std::move(cb), 1, false, 0});
    } else {
        const int apiLevel = get_gfxstream_guest_android_api_level();
        // pre-O and post-O use different color buffer memory management
        // logic
        if (apiLevel > 0 && apiLevel < 26) {
            m_colorbuffers.try_emplace(handle, ColorBufferRef{std::move(cb), 1, false, 0});

            RenderThreadInfo* tInfo = RenderThreadInfo::get();
            uint64_t puid = tInfo->m_puid;
            if (puid) {
                m_procOwnedColorBuffers[puid].insert(handle);
            }

        } else {
            m_colorbuffers.try_emplace(handle, ColorBufferRef{std::move(cb), 0, false, 0});
        }
    }

    return handle;
}

HandleType FrameBuffer::createBuffer(uint64_t p_size, uint32_t memoryProperty) {
    AutoLock mutex(m_lock);
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    return createBufferWithResourceHandleLocked(p_size, genHandle_locked(), memoryProperty);
}

void FrameBuffer::createBufferWithResourceHandle(uint64_t size, HandleType handle) {
    AutoLock mutex(m_lock);
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    if (m_buffers.count(handle) != 0) {
        GFXSTREAM_FATAL("Buffer already exists with handle %d", handle);
    }

    createBufferWithResourceHandleLocked(size, handle, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

HandleType FrameBuffer::createBufferWithResourceHandleLocked(int p_size, HandleType handle,
                                                             uint32_t memoryProperty) {
    if (m_buffers.count(handle) != 0) {
        GFXSTREAM_FATAL("Buffer already exists with handle %d", handle);
    }

    BufferPtr buffer(Buffer::create(m_emulationGl.get(), m_emulationVk.get(), p_size, handle));
    if (!buffer) {
        GFXSTREAM_ERROR("Create buffer failed.");
        return 0;
    }

    m_buffers[handle] = {std::move(buffer)};

    return handle;
}

int FrameBuffer::openColorBuffer(HandleType p_colorbuffer) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled) return 0;

    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c;
    {
        AutoLock colorBuffermapLock(m_colorBufferMapLock);
        c = m_colorbuffers.find(p_colorbuffer);
        if (c == m_colorbuffers.end()) {
            // bad colorbuffer handle
            GFXSTREAM_ERROR("FB: openColorBuffer cb handle %d not found", p_colorbuffer);
            return -1;
        }
        c->second.refcount++;
        markOpened(&c->second);
    }

    uint64_t puid = tInfo ? tInfo->m_puid : 0;
    if (puid) {
        m_procOwnedColorBuffers[puid].insert(p_colorbuffer);
    }
    return 0;
}

void FrameBuffer::closeColorBuffer(HandleType p_colorbuffer) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled) {
        return;
    }

    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    std::vector<HandleType> toCleanup;

    AutoLock mutex(m_lock);
    uint64_t puid = tInfo ? tInfo->m_puid : 0;
    if (puid) {
        auto ite = m_procOwnedColorBuffers.find(puid);
        if (ite != m_procOwnedColorBuffers.end()) {
            const auto& cb = ite->second.find(p_colorbuffer);
            if (cb != ite->second.end()) {
                ite->second.erase(cb);
                if (closeColorBufferLocked(p_colorbuffer)) {
                    toCleanup.push_back(p_colorbuffer);
                }
            }
        }
    } else {
        if (closeColorBufferLocked(p_colorbuffer)) {
            toCleanup.push_back(p_colorbuffer);
        }
    }
}

void FrameBuffer::closeBuffer(HandleType p_buffer) {
    AutoLock mutex(m_lock);

    auto it = m_buffers.find(p_buffer);
    if (it == m_buffers.end()) {
        GFXSTREAM_ERROR("Failed to find Buffer:%d", p_buffer);
        return;
    }

    m_buffers.erase(it);
}

bool FrameBuffer::closeColorBufferLocked(HandleType p_colorbuffer, bool forced) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled) {
        return false;
    }
    bool deleted = false;
    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);

        if (m_noDelayCloseColorBufferEnabled) forced = true;

        ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
        if (c == m_colorbuffers.end()) {
            // This is harmless: it is normal for guest system to issue
            // closeColorBuffer command when the color buffer is already
            // garbage collected on the host. (we don't have a mechanism
            // to give guest a notice yet)
            return false;
        }

        // The guest can and will gralloc_alloc/gralloc_free and then
        // gralloc_register a buffer, due to API level (O+) or
        // timing issues.
        // So, we don't actually close the color buffer when refcount
        // reached zero, unless it has been opened at least once already.
        // Instead, put it on a 'delayed close' list to return to it later.
        if (--c->second.refcount == 0) {
            if (forced) {
                eraseDelayedCloseColorBufferLocked(c->first, c->second.closedTs);
                m_colorbuffers.erase(c);
                deleted = true;
            } else {
                c->second.closedTs = gfxstream::base::getUnixTimeUs();
                m_colorBufferDelayedCloseList.push_back({c->second.closedTs, p_colorbuffer});
            }
        }
    }

    performDelayedColorBufferCloseLocked(false);

    return deleted;
}

void FrameBuffer::decColorBufferRefCountNoDestroy(HandleType p_colorbuffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        return;
    }

    if (--c->second.refcount == 0) {
        c->second.closedTs = gfxstream::base::getUnixTimeUs();
        m_colorBufferDelayedCloseList.push_back({c->second.closedTs, p_colorbuffer});
    }
}

void FrameBuffer::performDelayedColorBufferCloseLocked(bool forced) {
    // Let's wait just long enough to make sure it's not because of instant
    // timestamp change (end of previous second -> beginning of a next one),
    // but not for long - this is a workaround for race conditions, and they
    // are quick.
    static constexpr uint64_t kColorBufferClosingDelayUs = 1000000LL;

    const auto now = gfxstream::base::getUnixTimeUs();
    auto it = m_colorBufferDelayedCloseList.begin();
    while (it != m_colorBufferDelayedCloseList.end() &&
           (forced ||
           it->ts + kColorBufferClosingDelayUs <= now)) {
        if (it->cbHandle != 0) {
            AutoLock colorBufferMapLock(m_colorBufferMapLock);
            const auto& cb = m_colorbuffers.find(it->cbHandle);
            if (cb != m_colorbuffers.end()) {
                m_colorbuffers.erase(cb);
            }
        }
        ++it;
    }
    m_colorBufferDelayedCloseList.erase(
                m_colorBufferDelayedCloseList.begin(), it);
}

void FrameBuffer::eraseDelayedCloseColorBufferLocked(
        HandleType cb, uint64_t ts)
{
    // Find the first delayed buffer with a timestamp <= |ts|
    auto it = std::lower_bound(
                  m_colorBufferDelayedCloseList.begin(),
                  m_colorBufferDelayedCloseList.end(), ts,
                  [](const ColorBufferCloseInfo& ci, uint64_t ts) {
        return ci.ts < ts;
    });
    while (it != m_colorBufferDelayedCloseList.end() &&
           it->ts == ts) {
        // if this is the one we need - clear it out.
        if (it->cbHandle == cb) {
            it->cbHandle = 0;
            break;
        }
        ++it;
    }
}

void FrameBuffer::createGraphicsProcessResources(uint64_t puid) {
    bool inserted = false;
    {
        AutoLock mutex(m_procOwnedResourcesLock);
        inserted = m_procOwnedResources.try_emplace(puid, ProcessResources::create()).second;
    }
    if (!inserted) {
        GFXSTREAM_WARNING("Failed to create process resource for puid %" PRIu64 ".", puid);
    }
}

std::unique_ptr<ProcessResources> FrameBuffer::removeGraphicsProcessResources(uint64_t puid) {
    std::unordered_map<uint64_t, std::unique_ptr<ProcessResources>>::node_type node;
    {
        AutoLock mutex(m_procOwnedResourcesLock);
        node = m_procOwnedResources.extract(puid);
    }
    if (node.empty()) {
        GFXSTREAM_WARNING("Failed to find process resource for puid %" PRIu64 ".", puid);
        return nullptr;
    }
    std::unique_ptr<ProcessResources> res = std::move(node.mapped());
    return res;
}

void FrameBuffer::cleanupProcGLObjects(uint64_t puid) {
    bool renderThreadWithThisPuidExists = false;

    do {
        renderThreadWithThisPuidExists = false;
        RenderThreadInfo::forAllRenderThreadInfos(
            [puid, &renderThreadWithThisPuidExists](RenderThreadInfo* i) {
            if (i->m_puid == puid) {
                renderThreadWithThisPuidExists = true;

                bool shouldExit = false;
                i->m_shouldExit.compare_exchange_strong(shouldExit, true);
            }
        });
        gfxstream::base::sleepUs(10000);
    } while (renderThreadWithThisPuidExists);


    AutoLock mutex(m_lock);

    cleanupProcGLObjects_locked(puid);

    // Run other cleanup callbacks
    // Avoid deadlock by first storing a separate list of callbacks
    std::vector<std::function<void()>> callbacks;

    {
        auto procIte = m_procOwnedCleanupCallbacks.find(puid);
        if (procIte != m_procOwnedCleanupCallbacks.end()) {
            for (auto it : procIte->second) {
                callbacks.push_back(it.second);
            }
            m_procOwnedCleanupCallbacks.erase(procIte);
        }
    }

    mutex.unlock();

    for (auto cb : callbacks) {
        cb();
    }
}

std::vector<HandleType> FrameBuffer::cleanupProcGLObjects_locked(uint64_t puid, bool forced) {
    std::vector<HandleType> colorBuffersToCleanup;
    {
        std::unique_ptr<RecursiveScopedContextBind> bind = nullptr;
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
        }
        // Clean up window surfaces
        if (m_emulationGl) {
            auto procIte = m_procOwnedEmulatedEglWindowSurfaces.find(puid);
            if (procIte != m_procOwnedEmulatedEglWindowSurfaces.end()) {
                for (auto whndl : procIte->second) {
                    auto w = m_windows.find(whndl);
                    // TODO(b/265186226): figure out if we are leaking?
                    if (w == m_windows.end()) {
                        continue;
                    }
                    if (!m_guestManagedColorBufferLifetime) {
                        if (m_refCountPipeEnabled) {
                            if (decColorBufferRefCountLocked(w->second.second)) {
                                colorBuffersToCleanup.push_back(w->second.second);
                            }
                        } else {
                            if (closeColorBufferLocked(w->second.second, forced)) {
                                colorBuffersToCleanup.push_back(w->second.second);
                            }
                        }
                    }
                    m_windows.erase(w);
                }
                m_procOwnedEmulatedEglWindowSurfaces.erase(procIte);
            }
        }
#endif

        // Clean up color buffers.
        // A color buffer needs to be closed as many times as it is opened by
        // the guest process, to give the correct reference count.
        // (Note that a color buffer can be shared across guest processes.)
        {
            if (!m_guestManagedColorBufferLifetime) {
                auto procIte = m_procOwnedColorBuffers.find(puid);
                if (procIte != m_procOwnedColorBuffers.end()) {
                    for (auto cb : procIte->second) {
                        if (closeColorBufferLocked(cb, forced)) {
                            colorBuffersToCleanup.push_back(cb);
                        }
                    }
                    m_procOwnedColorBuffers.erase(procIte);
                }
            }
        }

#if GFXSTREAM_ENABLE_HOST_GLES
        // Clean up EGLImage handles
        if (m_emulationGl) {
            auto procImagesIt = m_procOwnedEmulatedEglImages.find(puid);
            if (procImagesIt != m_procOwnedEmulatedEglImages.end()) {
                for (auto image : procImagesIt->second) {
                    m_images.erase(image);
                }
                m_procOwnedEmulatedEglImages.erase(procImagesIt);
            }
        }
#endif
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    // Unbind before cleaning up contexts
    // Cleanup render contexts
    if (m_emulationGl) {
        auto procIte = m_procOwnedEmulatedEglContexts.find(puid);
        if (procIte != m_procOwnedEmulatedEglContexts.end()) {
            for (auto ctx : procIte->second) {
                m_contexts.erase(ctx);
            }
            m_procOwnedEmulatedEglContexts.erase(procIte);
        }
    }
#endif

    return colorBuffersToCleanup;
}

void FrameBuffer::markOpened(ColorBufferRef* cbRef) {
    cbRef->opened = true;
    eraseDelayedCloseColorBufferLocked(cbRef->cb->getHndl(), cbRef->closedTs);
    cbRef->closedTs = 0;
}

void FrameBuffer::readBuffer(HandleType handle, uint64_t offset, uint64_t size, void* bytes) {
    AutoLock mutex(m_lock);

    BufferPtr buffer = findBuffer(handle);
    if (!buffer) {
        GFXSTREAM_ERROR("Failed to read buffer: buffer %d not found.", handle);
        return;
    }

    buffer->readToBytes(offset, size, bytes);
}

void FrameBuffer::readColorBuffer(HandleType p_colorbuffer, int x, int y, int width, int height,
                                  GLenum format, GLenum type, void* outPixels, uint64_t outPixelsSize) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "FrameBuffer::readColorBuffer()",
                          "ColorBuffer", p_colorbuffer);

    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return;
    }

    colorBuffer->readToBytes(x, y, width, height, format, type, outPixels, outPixelsSize);
}

void FrameBuffer::readColorBufferYUV(HandleType p_colorbuffer, int x, int y, int width, int height,
                                     void* outPixels, uint32_t outPixelsSize) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return;
    }

    colorBuffer->readYuvToBytes(x, y, width, height, outPixels, outPixelsSize);
}

bool FrameBuffer::updateBuffer(HandleType p_buffer, uint64_t offset, uint64_t size, void* bytes) {
    AutoLock mutex(m_lock);

    BufferPtr buffer = findBuffer(p_buffer);
    if (!buffer) {
        GFXSTREAM_ERROR("Failed to update buffer: buffer %d not found.", p_buffer);
        return false;
    }

    return buffer->updateFromBytes(offset, size, bytes);
}

bool FrameBuffer::updateColorBuffer(HandleType p_colorbuffer,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    GLenum format,
                                    GLenum type,
                                    void* pixels) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "FrameBuffer::updateColorBuffer()",
                          "ColorBuffer", p_colorbuffer);

    if (width == 0 || height == 0) {
        return false;
    }

    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    colorBuffer->updateFromBytes(x, y, width, height, format, type, pixels);

    return true;
}

bool FrameBuffer::updateColorBufferFromFrameworkFormat(HandleType p_colorbuffer, int x, int y,
                                                       int width, int height,
                                                       FrameworkFormat fwkFormat, GLenum format,
                                                       GLenum type, void* pixels, void* metadata) {
    if (width == 0 || height == 0) {
        return false;
    }

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    (*c).second.cb->updateFromBytes(x, y, width, height, fwkFormat, format, type, pixels, metadata);
    return true;
}

bool FrameBuffer::getColorBufferInfo(
    HandleType p_colorbuffer, int* width, int* height, GLint* internalformat,
    FrameworkFormat* frameworkFormat) {

    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    *width = colorBuffer->getWidth();
    *height = colorBuffer->getHeight();
    *internalformat = colorBuffer->getFormat();
    if (frameworkFormat) {
        *frameworkFormat = colorBuffer->getFrameworkFormat();
    }

    return true;
}

bool FrameBuffer::getBufferInfo(HandleType p_buffer, int* size) {
    AutoLock mutex(m_lock);

    BufferMap::iterator c(m_buffers.find(p_buffer));
    if (c == m_buffers.end()) {
        // Bad buffer handle.
        return false;
    }

    auto buf = (*c).second.buffer;
    *size = buf->getSize();
    return true;
}

bool FrameBuffer::post(HandleType p_colorbuffer, bool needLockAndBind) {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_features.GuestVulkanOnly.enabled) {
        flushColorBufferFromGl(p_colorbuffer);
    }
#endif

    auto res = postImplSync(p_colorbuffer, needLockAndBind);
    if (res) setGuestPostedAFrame();
    return res;
}

void FrameBuffer::postWithCallback(HandleType p_colorbuffer, Post::CompletionCallback callback,
                                   bool needLockAndBind) {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_features.GuestVulkanOnly.enabled) {
        flushColorBufferFromGl(p_colorbuffer);
    }
#endif

    AsyncResult res = postImpl(p_colorbuffer, callback, needLockAndBind);
    if (res.Succeeded()) {
        setGuestPostedAFrame();
    }

    if (!res.CallbackScheduledOrFired()) {
        // If postImpl fails, we have not fired the callback. postWithCallback
        // should always ensure the callback fires.
        std::shared_future<void> callbackRes = std::async(std::launch::deferred, [] {});
        callback(callbackRes);
    }
}

bool FrameBuffer::postImplSync(HandleType p_colorbuffer, bool needLockAndBind, bool repaint) {
    std::promise<void> promise;
    std::future<void> completeFuture = promise.get_future();
    auto posted = postImpl(
        p_colorbuffer,
        [&](std::shared_future<void> waitForGpu) {
            waitForGpu.wait();
            promise.set_value();
        },
        needLockAndBind, repaint);
    if (posted.CallbackScheduledOrFired()) {
        completeFuture.wait();
    }

    return posted.Succeeded();
}

AsyncResult FrameBuffer::postImpl(HandleType p_colorbuffer, Post::CompletionCallback callback,
                                  bool needLockAndBind, bool repaint) {
    ColorBufferPtr colorBuffer = nullptr;
    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        ColorBufferMap::iterator c = m_colorbuffers.find(p_colorbuffer);
        if (c != m_colorbuffers.end()) {
            colorBuffer = c->second.cb;
            c->second.refcount++;
            markOpened(&c->second);
        }
    }
    if (!colorBuffer) {
        return AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;
    }

    std::optional<AutoLock> lock;
#if GFXSTREAM_ENABLE_HOST_GLES
    std::optional<RecursiveScopedContextBind> bind;
#endif
    if (needLockAndBind) {
        lock.emplace(m_lock);
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            bind.emplace(getPbufferSurfaceContextHelper());
        }
#endif
    }
    AsyncResult ret = AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;

    m_lastPostedColorBuffer = p_colorbuffer;

    colorBuffer->touch();
    if (m_subWin) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // If there is no sub-window, don't display anything, the client will
        // rely on m_onPost to get the pixels instead.
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
    }

    //
    // output FPS and performance usage statistics
    //
    if (m_fpsStats) {
        long long currTime = gfxstream::base::getHighResTimeUs() / 1000;
        m_statsNumFrames++;
        if (currTime - m_statsStartTime >= 1000) {
            if (m_fpsStats) {
                float dt = (float)(currTime - m_statsStartTime) / 1000.0f;
                printf("FPS: %5.3f \n", (float)m_statsNumFrames / dt);
                m_statsNumFrames = 0;
            }
            m_statsStartTime = currTime;
        }
    }

    //
    // Send framebuffer (without FPS overlay) to callback
    //
    if (!m_onPost.empty()) {
        for (auto& iter : m_onPost) {
            ColorBufferPtr cb;
            if (iter.first == 0) {
                cb = colorBuffer;
            } else {
                uint32_t displayColorBufferHandle = 0;
                if (getDisplayColorBuffer(iter.first, &displayColorBufferHandle) < 0) {
                    GFXSTREAM_ERROR("Failed to get color buffer for display %d, skip onPost",
                                    iter.first);
                    continue;
                }

                cb = findColorBuffer(displayColorBufferHandle);
                if (!cb) {
                    GFXSTREAM_ERROR("Failed to find ColorBuffer %d, skip onPost",
                                    displayColorBufferHandle);
                    continue;
                }
            }

            if (asyncReadbackSupported()) {
                ensureReadbackWorker();
                const auto status = m_readbackWorker->doNextReadback(
                    iter.first, cb.get(), iter.second.img, repaint, iter.second.readBgra);
                if (status == ReadbackWorker::DoNextReadbackResult::OK_READY_FOR_READ) {
                    doPostCallback(iter.second.img, iter.first);
                }
            } else {
    #if GFXSTREAM_ENABLE_HOST_GLES
                cb->glOpReadback(iter.second.img, iter.second.readBgra);
    #endif
                doPostCallback(iter.second.img, iter.first);
            }
        }
    }

    if (!m_subWin) {  // m_subWin is supposed to be false
        decColorBufferRefCountLocked(p_colorbuffer);
    }

    return ret;
}

void FrameBuffer::doPostCallback(void* pixels, uint32_t displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        GFXSTREAM_ERROR("Cannot find post callback function for display %d", displayId);
        return;
    }
    iter->second.cb(iter->second.context, displayId, iter->second.width, iter->second.height, -1,
                    GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char*)pixels);
}

void FrameBuffer::getPixels(void* pixels, uint32_t bytes, uint32_t displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        GFXSTREAM_ERROR("Display %d not configured for recording yet", displayId);
        return;
    }
    std::future<void> completeFuture =
        m_readbackThread.enqueue({ReadbackCmd::GetPixels, displayId, pixels, bytes});
    completeFuture.wait();
}

void FrameBuffer::flushReadPipeline(int displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        GFXSTREAM_ERROR("Cannot find onPost pixels for display %d", displayId);
        return;
    }

    ensureReadbackWorker();

    const auto status = m_readbackWorker->flushPipeline(displayId);
    if (status == ReadbackWorker::FlushResult::OK_READY_FOR_READ) {
        doPostCallback(nullptr, displayId);
    }
}

void FrameBuffer::ensureReadbackWorker() {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (!m_readbackWorker) {
        if (!m_emulationGl) {
            GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
        }
        m_readbackWorker = m_emulationGl->getReadbackWorker();
    }
#endif
}

static void sFrameBuffer_ReadPixelsCallback(void* pixels, uint32_t bytes, uint32_t displayId) {
    FrameBuffer::getFB()->getPixels(pixels, bytes, displayId);
}

static void sFrameBuffer_FlushReadPixelPipeline(int displayId) {
    FrameBuffer::getFB()->flushReadPipeline(displayId);
}

bool FrameBuffer::asyncReadbackSupported() {
#if GFXSTREAM_ENABLE_HOST_GLES
    return m_emulationGl && m_emulationGl->isAsyncReadbackSupported();
#else
    return false;
#endif
}

Renderer::ReadPixelsCallback FrameBuffer::getReadPixelsCallback() {
    return sFrameBuffer_ReadPixelsCallback;
}

Renderer::FlushReadPixelPipeline FrameBuffer::getFlushReadPixelPipeline() {
    return sFrameBuffer_FlushReadPixelPipeline;
}

bool FrameBuffer::repost(bool needLockAndBind) {
    GFXSTREAM_DEBUG("Reposting framebuffer.");
    if (m_displayVk) {
        setGuestPostedAFrame();
        return true;
    }
    if (m_lastPostedColorBuffer && sInitialized.load(std::memory_order_relaxed)) {
        GFXSTREAM_DEBUG("Has last posted colorbuffer and is initialized; post.");
        auto res = postImplSync(m_lastPostedColorBuffer, needLockAndBind, true);
        if (res) setGuestPostedAFrame();
        return res;
    } else {
        GFXSTREAM_DEBUG("No repost: no last posted color buffer");
        if (!sInitialized.load(std::memory_order_relaxed)) {
            GFXSTREAM_DEBUG("No repost: initialization is not finished.");
        }
    }
    return false;
}

template <class Collection>
static void saveProcOwnedCollection(Stream* stream, const Collection& c) {
    // Exclude empty handle lists from saving as they add no value but only
    // increase the snapshot size; keep the format compatible with
    // android::base::saveCollection() though.
    const int count = std::count_if(
        c.begin(), c.end(),
        [](const typename Collection::value_type& pair) { return !pair.second.empty(); });
    stream->putBe32(count);
    for (const auto& pair : c) {
        if (pair.second.empty()) {
            continue;
        }
        stream->putBe64(pair.first);
        saveCollection(stream, pair.second, [](Stream* s, HandleType h) { s->putBe32(h); });
    }
}

template <class Collection>
static void loadProcOwnedCollection(Stream* stream, Collection* c) {
    loadCollection(stream, c, [](Stream* stream) -> typename Collection::value_type {
        const int processId = stream->getBe64();
        typename Collection::mapped_type handles;
        loadCollection(stream, &handles, [](Stream* s) { return s->getBe32(); });
        return {processId, std::move(handles)};
    });
}

int FrameBuffer::getScreenshot(unsigned int nChannels, unsigned int* width, unsigned int* height,
                               uint8_t* pixels, size_t* cPixels, int displayId, int desiredWidth,
                               int desiredHeight, int desiredRotation, Rect rect) {
#ifdef CONFIG_AEMU
   if (get_gfxstream_should_skip_draw()) {
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }
#else
    return 0;
#endif

    AutoLock mutex(m_lock);
    uint32_t w, h, cb, screenWidth, screenHeight;
    if (!get_gfxstream_multi_display_operations().get_display_info(displayId, nullptr, nullptr, &w,
                                                                   &h, nullptr, nullptr, nullptr)) {
        GFXSTREAM_ERROR("Screenshot of invalid display %d", displayId);
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }
    if (nChannels != 3 && nChannels != 4) {
        GFXSTREAM_ERROR("Screenshot only support 3(RGB) or 4(RGBA) channels");
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }
    get_gfxstream_multi_display_operations().get_display_color_buffer(displayId, &cb);
    if (displayId == 0) {
        cb = m_lastPostedColorBuffer;
    }
    ColorBufferPtr colorBuffer = findColorBuffer(cb);
    if (!colorBuffer) {
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }

    screenWidth = (desiredWidth == 0) ? w : desiredWidth;
    screenHeight = (desiredHeight == 0) ? h : desiredHeight;

    bool useSnipping = (rect.size.w != 0 && rect.size.h != 0);
    if (useSnipping) {
        if (desiredWidth == 0 || desiredHeight == 0) {
            GFXSTREAM_ERROR(
                "Must provide non-zero desiredWidth and desireRectanlge "
                "when using rectangle snipping");
            *width = 0;
            *height = 0;
            *cPixels = 0;
            return -1;
        }
        if ((rect.pos.x < 0 || rect.pos.y < 0) ||
            (desiredWidth < rect.pos.x + rect.size.w || desiredHeight < rect.pos.y + rect.size.h)) {
            return -1;
        }
    }

    if (useSnipping) {
        *width = rect.size.w;
        *height = rect.size.h;
    } else {
        *width = screenWidth;
        *height = screenHeight;
    }

    int needed =
        useSnipping ? (nChannels * rect.size.w * rect.size.h) : (nChannels * (*width) * (*height));

    if (*cPixels < (size_t)needed) {
        *cPixels = needed;
        return -2;
    }
    *cPixels = needed;
    if (desiredRotation == GFXSTREAM_ROTATION_90 || desiredRotation == GFXSTREAM_ROTATION_270) {
        std::swap(*width, *height);
        std::swap(screenWidth, screenHeight);
        std::swap(rect.size.w, rect.size.h);
    }
    // Transform the x, y coordinates given the rotation.
    // Assume (0, 0) represents the top left corner of the screen.
    if (useSnipping) {
        int x = 0, y = 0;
        switch (desiredRotation) {
            case GFXSTREAM_ROTATION_0:
                x = rect.pos.x;
                y = rect.pos.y;
                break;
            case GFXSTREAM_ROTATION_90:
                x = rect.pos.y;
                y = rect.pos.x;
                break;
            case GFXSTREAM_ROTATION_180:
                x = screenWidth - rect.pos.x - rect.size.w;
                y = rect.pos.y;
                break;
            case GFXSTREAM_ROTATION_270:
                x = rect.pos.y;
                y = screenHeight - rect.pos.x - rect.size.h;
                break;
        }
        rect.pos.x = x;
        rect.pos.y = y;
    }

    GLenum format = nChannels == 3 ? GL_RGB : GL_RGBA;
    Post scrCmd;
    scrCmd.cmd = PostCmd::Screenshot;
    scrCmd.screenshot.cb = colorBuffer.get();
    scrCmd.screenshot.screenwidth = screenWidth;
    scrCmd.screenshot.screenheight = screenHeight;
    scrCmd.screenshot.format = format;
    scrCmd.screenshot.type = GL_UNSIGNED_BYTE;
    scrCmd.screenshot.rotation = desiredRotation;
    scrCmd.screenshot.pixels = pixels;
    scrCmd.screenshot.rect = rect;

    std::future<void> completeFuture = sendPostWorkerCmd(std::move(scrCmd));

    mutex.unlock();
    completeFuture.wait();
    return 0;
}

void FrameBuffer::onLastColorBufferRef(uint32_t handle) {
    if (!mOutstandingColorBufferDestroys.trySend((HandleType)handle)) {
        GFXSTREAM_ERROR(
            "warning: too many outstanding "
            "color buffer destroys. leaking handle 0x%x",
            handle);
    }
}

bool FrameBuffer::decColorBufferRefCountLocked(HandleType p_colorbuffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    const auto& it = m_colorbuffers.find(p_colorbuffer);
    if (it != m_colorbuffers.end()) {
        it->second.refcount -= 1;
        if (it->second.refcount == 0) {
            m_colorbuffers.erase(p_colorbuffer);
            return true;
        }
    }
    return false;
}

bool FrameBuffer::compose(uint32_t bufferSize, void* buffer, bool needPost) {
    std::promise<void> promise;
    std::future<void> completeFuture = promise.get_future();
    auto composeRes =
        composeWithCallback(bufferSize, buffer, [&](std::shared_future<void> waitForGpu) {
            waitForGpu.wait();
            promise.set_value();
        });
    if (!composeRes.Succeeded()) {
        return false;
    }

    if (composeRes.CallbackScheduledOrFired()) {
        completeFuture.wait();
    }

#ifdef CONFIG_AEMU
    const auto& multiDisplay = get_gfxstream_multi_display_operations();
    const bool is_pixel_fold = multiDisplay.is_pixel_fold();
    if (needPost) {
        // AEMU with -no-window mode uses this code path.
        ComposeDevice* composeDevice = (ComposeDevice*)buffer;

        switch (composeDevice->version) {
            case 1: {
                post(composeDevice->targetHandle, true);
                break;
            }
            case 2: {
                ComposeDevice_v2* composeDeviceV2 = (ComposeDevice_v2*)buffer;
                if (is_pixel_fold || composeDeviceV2->displayId == 0) {
                    post(composeDeviceV2->targetHandle, true);
                }
                break;
            }
            default: {
                return false;
            }
        }
    }
#endif

    return true;
}

AsyncResult FrameBuffer::composeWithCallback(uint32_t bufferSize, void* buffer,
                                             Post::CompletionCallback callback) {
    ComposeDevice* p = (ComposeDevice*)buffer;
    AutoLock mutex(m_lock);

    switch (p->version) {
        case 1: {
            Post composeCmd;
            composeCmd.composeVersion = 1;
            composeCmd.composeBuffer.resize(bufferSize);
            memcpy(composeCmd.composeBuffer.data(), buffer, bufferSize);
            composeCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
            composeCmd.cmd = PostCmd::Compose;
            sendPostWorkerCmd(std::move(composeCmd));
            return AsyncResult::OK_AND_CALLBACK_SCHEDULED;
        }

        case 2: {
            // support for multi-display
            ComposeDevice_v2* p2 = (ComposeDevice_v2*)buffer;
            if (p2->displayId != 0) {
                mutex.unlock();
                setDisplayColorBuffer(p2->displayId, p2->targetHandle);
                mutex.lock();
            }
            Post composeCmd;
            composeCmd.composeVersion = 2;
            composeCmd.composeBuffer.resize(bufferSize);
            memcpy(composeCmd.composeBuffer.data(), buffer, bufferSize);
            composeCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
            composeCmd.cmd = PostCmd::Compose;
            sendPostWorkerCmd(std::move(composeCmd));
            return AsyncResult::OK_AND_CALLBACK_SCHEDULED;
        }

        default:
            GFXSTREAM_ERROR("yet to handle composition device version: %d", p->version);
            return AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;
    }
}

void FrameBuffer::onSave(Stream* stream, const ITextureSaverPtr& textureSaver) {
    // Things we do not need to snapshot:
    //     m_eglSurface
    //     m_eglContext
    //     m_pbufSurface
    //     m_pbufContext
    //     m_prevContext
    //     m_prevReadSurf
    //     m_prevDrawSurf
    AutoLock mutex(m_lock);

    std::unique_ptr<RecursiveScopedContextBind> bind;
#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        // Some snapshot commands try using GL.
        bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
        if (!bind->isOk()) {
            GFXSTREAM_ERROR("Failed to make context current for saving snapshot.");
        }

        // eglPreSaveContext labels all guest context textures to be saved
        // (textures created by the host are not saved!)
        // eglSaveAllImages labels all EGLImages (both host and guest) to be saved
        // and save all labeled textures and EGLImages.
        if (s_egl.eglPreSaveContext && s_egl.eglSaveAllImages) {
            for (const auto& ctx : m_contexts) {
                s_egl.eglPreSaveContext(getDisplay(), ctx.second->getEGLContext(), stream);
            }
            s_egl.eglSaveAllImages(getDisplay(), stream, &textureSaver);
        }
    }
#endif

    // Don't save subWindow's x/y/w/h here - those are related to the current
    // emulator UI state, not guest state that we're saving.
    stream->putBe32(m_framebufferWidth);
    stream->putBe32(m_framebufferHeight);
    stream->putFloat(m_dpr);
    stream->putBe32(mDisplayActiveConfigId);
    saveCollection(stream, mDisplayConfigs,
                   [](Stream* s, const std::map<int, DisplayConfig>::value_type& pair) {
                       s->putBe32(pair.first);
                       s->putBe32(pair.second.w);
                       s->putBe32(pair.second.h);
                       s->putBe32(pair.second.dpiX);
                       s->putBe32(pair.second.dpiY);
                   });

    stream->putBe32(m_useSubWindow);
    stream->putBe32(/*Obsolete m_eglContextInitialized =*/1);

    stream->putBe32(m_fpsStats);
    stream->putBe32(m_statsNumFrames);
    stream->putBe64(m_statsStartTime);

    // Save all contexts.
    // Note: some of the contexts might not be restored yet. In such situation
    // we skip reading from GPU (for non-texture objects) or force a restore in
    // previous eglPreSaveContext and eglSaveAllImages calls (for texture
    // objects).
    // TODO: skip reading from GPU even for texture objects.
#if GFXSTREAM_ENABLE_HOST_GLES
    saveCollection(
        stream, m_contexts,
        [](Stream* s, const EmulatedEglContextMap::value_type& pair) { pair.second->onSave(s); });
#endif

    // We don't need to save |m_colorBufferCloseTsMap| here - there's enough
    // information to reconstruct it when loading.
    uint64_t now = gfxstream::base::getUnixTimeUs();

    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        stream->putByte(m_guestManagedColorBufferLifetime);
        saveCollection(stream, m_colorbuffers,
                       [now](Stream* s, const ColorBufferMap::value_type& pair) {
                           pair.second.cb->onSave(s);
                           s->putBe32(pair.second.refcount);
                           s->putByte(pair.second.opened);
                           s->putBe32(std::max<uint64_t>(0, now - pair.second.closedTs));
                       });
    }
    stream->putBe32(m_lastPostedColorBuffer);
#if GFXSTREAM_ENABLE_HOST_GLES
    saveCollection(stream, m_windows,
                   [](Stream* s, const EmulatedEglWindowSurfaceMap::value_type& pair) {
                       pair.second.first->onSave(s);
                       s->putBe32(pair.second.second);  // Color buffer handle.
                   });
#endif

#if GFXSTREAM_ENABLE_HOST_GLES
    saveProcOwnedCollection(stream, m_procOwnedEmulatedEglWindowSurfaces);
#endif
    saveProcOwnedCollection(stream, m_procOwnedColorBuffers);
#if GFXSTREAM_ENABLE_HOST_GLES
    saveProcOwnedCollection(stream, m_procOwnedEmulatedEglImages);
    saveProcOwnedCollection(stream, m_procOwnedEmulatedEglContexts);
#endif

    // TODO(b/309858017): remove if when ready to bump snapshot version
    if (m_features.VulkanSnapshots.enabled) {
        AutoLock procResourceLock(m_procOwnedResourcesLock);
        stream->putBe64(m_procOwnedResources.size());
        for (const auto& element : m_procOwnedResources) {
            stream->putBe64(element.first);
            stream->putBe32(element.second->getSequenceNumberPtr()->load());
        }
    }

    // Save Vulkan state
    if (m_features.VulkanSnapshots.enabled && vk::VkDecoderGlobalState::get()) {
        vk::VkDecoderGlobalState::get()->save(stream);
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        if (s_egl.eglPostSaveContext) {
            for (const auto& ctx : m_contexts) {
                s_egl.eglPostSaveContext(getDisplay(), ctx.second->getEGLContext(), stream);
            }
            // We need to run the post save step for m_eglContext
            // to mark their texture handles dirty
            if (getContext() != EGL_NO_CONTEXT) {
                s_egl.eglPostSaveContext(getDisplay(), getContext(), stream);
            }
        }

        EmulatedEglFenceSync::onSave(stream);
    }
#endif
}

bool FrameBuffer::onLoad(Stream* stream,
                         const ITextureLoaderPtr& textureLoader) {
    AutoLock lock(m_lock);
    // cleanups
    {
        sweepColorBuffersLocked();

        std::unique_ptr<RecursiveScopedContextBind> bind;
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            // Some snapshot commands try using GL.
            bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
            if (!bind->isOk()) {
                GFXSTREAM_ERROR("Failed to make context current for loading snapshot.");
            }
        }
#endif

        bool cleanupComplete = false;
        {
            AutoLock colorBufferMapLock(m_colorBufferMapLock);
            if (m_procOwnedCleanupCallbacks.empty() && m_procOwnedColorBuffers.empty() &&
#if GFXSTREAM_ENABLE_HOST_GLES
                m_procOwnedEmulatedEglContexts.empty() && m_procOwnedEmulatedEglImages.empty() &&
                m_procOwnedEmulatedEglWindowSurfaces.empty() &&
#endif
                (
#if GFXSTREAM_ENABLE_HOST_GLES
                    !m_contexts.empty() || !m_windows.empty() ||
#endif
                    m_colorbuffers.size() > m_colorBufferDelayedCloseList.size())) {
                // we are likely on a legacy system image, which does not have
                // process owned objects. We need to force cleanup everything
#if GFXSTREAM_ENABLE_HOST_GLES
                m_contexts.clear();
                m_windows.clear();
#endif
                m_colorbuffers.clear();
                cleanupComplete = true;
            }
        }
        if (!cleanupComplete) {
            std::vector<HandleType> colorBuffersToCleanup;

#if GFXSTREAM_ENABLE_HOST_GLES
            while (m_procOwnedEmulatedEglWindowSurfaces.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                    m_procOwnedEmulatedEglWindowSurfaces.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
#endif
            while (m_procOwnedColorBuffers.size()) {
                auto cleanupHandles =
                    cleanupProcGLObjects_locked(m_procOwnedColorBuffers.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
#if GFXSTREAM_ENABLE_HOST_GLES
            while (m_procOwnedEmulatedEglImages.size()) {
                auto cleanupHandles =
                    cleanupProcGLObjects_locked(m_procOwnedEmulatedEglImages.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
            while (m_procOwnedEmulatedEglContexts.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                    m_procOwnedEmulatedEglContexts.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
#endif

            std::vector<std::function<void()>> cleanupCallbacks;

            while (m_procOwnedCleanupCallbacks.size()) {
                auto it = m_procOwnedCleanupCallbacks.begin();
                while (it != m_procOwnedCleanupCallbacks.end()) {
                    for (auto it2 : it->second) {
                        cleanupCallbacks.push_back(it2.second);
                    }
                    it = m_procOwnedCleanupCallbacks.erase(it);
                }
            }

            {
                AutoLock mutex(m_procOwnedResourcesLock);
                m_procOwnedResources.clear();
            }

            performDelayedColorBufferCloseLocked(true);

            lock.unlock();

            for (auto cb : cleanupCallbacks) {
                cb();
            }

            lock.lock();
            cleanupComplete = true;
        }
        m_colorBufferDelayedCloseList.clear();
#if GFXSTREAM_ENABLE_HOST_GLES
        assert(m_contexts.empty());
        assert(m_windows.empty());
#endif
        {
            AutoLock colorBufferMapLock(m_colorBufferMapLock);
            if (!m_colorbuffers.empty()) {
                GFXSTREAM_ERROR("warning: on load, stale colorbuffers: %zu", m_colorbuffers.size());
                m_colorbuffers.clear();
            }
            assert(m_colorbuffers.empty());
        }
#ifdef SNAPSHOT_PROFILE
        uint64_t texTime = gfxstream::base::getUnixTimeUs();
#endif
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            if (s_egl.eglLoadAllImages) {
                s_egl.eglLoadAllImages(getDisplay(), stream, &textureLoader);
            }
        }
#endif
#ifdef SNAPSHOT_PROFILE
        printf("Texture load time: %lld ms\n",
               (long long)(gfxstream::base::getUnixTimeUs() - texTime) / 1000);
#endif
    }
    // See comment about subwindow position in onSave().
    m_framebufferWidth = stream->getBe32();
    m_framebufferHeight = stream->getBe32();
    m_dpr = stream->getFloat();
    mDisplayActiveConfigId = stream->getBe32();
    loadCollection(stream, &mDisplayConfigs,
                   [](Stream* s) -> std::map<int, DisplayConfig>::value_type {
                       int idx = static_cast<int>(s->getBe32());
                       int w = static_cast<int>(s->getBe32());
                       int h = static_cast<int>(s->getBe32());
                       int dpiX = static_cast<int>(s->getBe32());
                       int dpiY = static_cast<int>(s->getBe32());
                       return {idx, {w, h, dpiX, dpiY}};
                   });

    // TODO: resize the window
    //
    m_useSubWindow = stream->getBe32();
    /*Obsolete m_eglContextInitialized =*/stream->getBe32();

    m_fpsStats = stream->getBe32();
    m_statsNumFrames = stream->getBe32();
    m_statsStartTime = stream->getBe64();

#if GFXSTREAM_ENABLE_HOST_GLES
    loadCollection(
        stream, &m_contexts, [this](Stream* stream) -> EmulatedEglContextMap::value_type {
            if (!m_emulationGl) {
                GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
            }

            auto context = m_emulationGl->loadEmulatedEglContext(stream);
            auto contextHandle = context ? context->getHndl() : 0;
            return {contextHandle, std::move(context)};
        });
    assert(!gfxstream::base::find(m_contexts, 0));
#endif

    auto now = gfxstream::base::getUnixTimeUs();
    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        m_guestManagedColorBufferLifetime = stream->getByte();
        loadCollection(
            stream, &m_colorbuffers, [this, now](Stream* stream) -> ColorBufferMap::value_type {
                ColorBufferPtr cb =
                    ColorBuffer::onLoad(m_emulationGl.get(), m_emulationVk.get(), stream);
                const HandleType handle = cb->getHndl();
                const unsigned refCount = stream->getBe32();
                const bool opened = stream->getByte();
                const uint64_t closedTs = now - stream->getBe32();
                if (refCount == 0) {
                    m_colorBufferDelayedCloseList.push_back({closedTs, handle});
                }
                return {handle, ColorBufferRef{std::move(cb), refCount, opened, closedTs}};
            });
    }
    m_lastPostedColorBuffer = static_cast<HandleType>(stream->getBe32());
    GFXSTREAM_DEBUG("Got lasted posted color buffer from snapshot");

    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
#if GFXSTREAM_ENABLE_HOST_GLES
        loadCollection(
            stream, &m_windows, [this](Stream* stream) -> EmulatedEglWindowSurfaceMap::value_type {
                if (!m_emulationGl) {
                    GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
                }

                auto window =
                    m_emulationGl->loadEmulatedEglWindowSurface(stream, m_colorbuffers, m_contexts);

                HandleType handle = window->getHndl();
                HandleType colorBufferHandle = stream->getBe32();
                return {handle, {std::move(window), colorBufferHandle}};
            });
#endif
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    loadProcOwnedCollection(stream, &m_procOwnedEmulatedEglWindowSurfaces);
#endif
    loadProcOwnedCollection(stream, &m_procOwnedColorBuffers);
#if GFXSTREAM_ENABLE_HOST_GLES
    loadProcOwnedCollection(stream, &m_procOwnedEmulatedEglImages);
    loadProcOwnedCollection(stream, &m_procOwnedEmulatedEglContexts);
#endif
    // TODO(b/309858017): remove if when ready to bump snapshot version
    if (m_features.VulkanSnapshots.enabled) {
        size_t resourceCount = stream->getBe64();
        for (size_t i = 0; i < resourceCount; i++) {
            uint64_t puid = stream->getBe64();
            uint32_t sequenceNumber = stream->getBe32();
            std::unique_ptr<ProcessResources> processResources = ProcessResources::create();
            processResources->getSequenceNumberPtr()->store(sequenceNumber);
            {
                AutoLock mutex(m_procOwnedResourcesLock);
                m_procOwnedResources.emplace(puid, std::move(processResources));
            }
        }
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        if (s_egl.eglPostLoadAllImages) {
            s_egl.eglPostLoadAllImages(getDisplay(), stream);
        }
    }

    registerTriggerWait();
#endif

    {
        std::unique_ptr<RecursiveScopedContextBind> bind;
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            // Some snapshot commands try using GL.
            bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
            if (!bind->isOk()) {
                GFXSTREAM_ERROR("Failed to make context current for loading snapshot.");
            }
        }
#endif

        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        for (auto& it : m_colorbuffers) {
            if (it.second.cb) {
                it.second.cb->touch();
            }
        }
    }

    // Restore Vulkan state
    if (m_features.VulkanSnapshots.enabled && vk::VkDecoderGlobalState::get()) {
        lock.unlock();
        GfxApiLogger gfxLogger;
        vk::VkDecoderGlobalState::get()->load(stream, gfxLogger, m_healthMonitor.get());
        lock.lock();
    }

    repost(false);

#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        EmulatedEglFenceSync::onLoad(stream);
    }
#endif

    return true;
    // TODO: restore memory management
}

void FrameBuffer::lock() { m_lock.lock(); }

void FrameBuffer::unlock() { m_lock.unlock(); }

ColorBufferPtr FrameBuffer::findColorBuffer(HandleType p_colorbuffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        return nullptr;
    } else {
        return c->second.cb;
    }
}

BufferPtr FrameBuffer::findBuffer(HandleType p_buffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    BufferMap::iterator b(m_buffers.find(p_buffer));
    if (b == m_buffers.end()) {
        return nullptr;
    } else {
        return b->second.buffer;
    }
}

void FrameBuffer::registerProcessCleanupCallback(void* key, uint64_t contextId,
                                                 std::function<void()> cb) {
    AutoLock mutex(m_lock);

    auto& callbackMap = m_procOwnedCleanupCallbacks[contextId];
    if (!callbackMap.insert({key, std::move(cb)}).second) {
        GFXSTREAM_ERROR("%s: tried to override existing key %p ", __func__, key);
    }
}

void FrameBuffer::unregisterProcessCleanupCallback(void* key) {
    AutoLock mutex(m_lock);
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    if (!tInfo) return;

    auto& callbackMap = m_procOwnedCleanupCallbacks[tInfo->m_puid];
    auto erasedCount = callbackMap.erase(key);
    if (erasedCount == 0) {
        GFXSTREAM_ERROR(
            "%s: tried to erase nonexistent key %p "
            "associated with process %llu",
            __func__, key, (unsigned long long)(tInfo->m_puid));
    }
}

const ProcessResources* FrameBuffer::getProcessResources(uint64_t puid) {
    {
        AutoLock mutex(m_procOwnedResourcesLock);
        auto i = m_procOwnedResources.find(puid);
        if (i != m_procOwnedResources.end()) {
            return i->second.get();
        }
    }
    GFXSTREAM_ERROR("Failed to find process owned resources for puid %" PRIu64 ".", puid);
    return nullptr;
}

int FrameBuffer::createDisplay(uint32_t* displayId) {
    return get_gfxstream_multi_display_operations().create_display(displayId);
}

int FrameBuffer::createDisplay(uint32_t displayId) {
    return get_gfxstream_multi_display_operations().create_display(&displayId);
}

int FrameBuffer::destroyDisplay(uint32_t displayId) {
    return get_gfxstream_multi_display_operations().destroy_display(displayId);
}

int FrameBuffer::setDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer) {
    return get_gfxstream_multi_display_operations().set_display_color_buffer(displayId,
                                                                             colorBuffer);
}

int FrameBuffer::getDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer) {
    return get_gfxstream_multi_display_operations().get_display_color_buffer(displayId,
                                                                             colorBuffer);
}

int FrameBuffer::getColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId) {
    return get_gfxstream_multi_display_operations().get_color_buffer_display(colorBuffer,
                                                                             displayId);
}

int FrameBuffer::getDisplayPose(uint32_t displayId, int32_t* x, int32_t* y, uint32_t* w,
                                uint32_t* h) {
    return get_gfxstream_multi_display_operations().get_display_pose(displayId, x, y, w, h);
}

int FrameBuffer::setDisplayPose(uint32_t displayId, int32_t x, int32_t y, uint32_t w, uint32_t h,
                                uint32_t dpi) {
    return get_gfxstream_multi_display_operations().set_display_pose(displayId, x, y, w, h, dpi);
}

void FrameBuffer::sweepColorBuffersLocked() {
    HandleType handleToDestroy = 0;
    while (mOutstandingColorBufferDestroys.tryReceive(&handleToDestroy)) {
        decColorBufferRefCountLocked(handleToDestroy);
    }
}

std::future<void> FrameBuffer::blockPostWorker(std::future<void> continueSignal) {
    std::promise<void> scheduled;
    std::future<void> scheduledFuture = scheduled.get_future();
    Post postCmd = {
        .cmd = PostCmd::Block,
        .block = std::make_unique<Post::Block>(Post::Block{
            .scheduledSignal = std::move(scheduled),
            .continueSignal = std::move(continueSignal),
        }),
    };
    sendPostWorkerCmd(std::move(postCmd));
    return scheduledFuture;
}

void FrameBuffer::asyncWaitForGpuVulkanWithCb(uint64_t deviceHandle, uint64_t fenceHandle,
                                              FenceCompletionCallback cb) {
    (void)deviceHandle;
    SyncThread::get()->triggerWaitVkWithCompletionCallback((VkFence)fenceHandle, std::move(cb));
}

void FrameBuffer::asyncWaitForGpuVulkanQsriWithCb(uint64_t image, FenceCompletionCallback cb) {
    SyncThread::get()->triggerWaitVkQsriWithCompletionCallback((VkImage)image, std::move(cb));
}

void FrameBuffer::setGuestManagedColorBufferLifetime(bool guestManaged) {
    m_guestManagedColorBufferLifetime = guestManaged;
}

std::unique_ptr<BorrowedImageInfo> FrameBuffer::borrowColorBufferForComposition(
    uint32_t colorBufferHandle, bool colorBufferIsTarget) {
    ColorBufferPtr colorBufferPtr = findColorBuffer(colorBufferHandle);
    if (!colorBufferPtr) {
        GFXSTREAM_ERROR("Failed to get borrowed image info for ColorBuffer:%d", colorBufferHandle);
        return nullptr;
    }

    if (m_useVulkanComposition) {
        invalidateColorBufferForVk(colorBufferHandle);
    } else {
#if GFXSTREAM_ENABLE_HOST_GLES
        invalidateColorBufferForGl(colorBufferHandle);
#endif
    }

    const auto api = m_useVulkanComposition ? ColorBuffer::UsedApi::kVk : ColorBuffer::UsedApi::kGl;
    return colorBufferPtr->borrowForComposition(api, colorBufferIsTarget);
}

std::unique_ptr<BorrowedImageInfo> FrameBuffer::borrowColorBufferForDisplay(
    uint32_t colorBufferHandle) {
    ColorBufferPtr colorBufferPtr = findColorBuffer(colorBufferHandle);
    if (!colorBufferPtr) {
        GFXSTREAM_ERROR("Failed to get borrowed image info for ColorBuffer:%d", colorBufferHandle);
        return nullptr;
    }

    if (m_useVulkanComposition) {
        invalidateColorBufferForVk(colorBufferHandle);
    } else {
#if GFXSTREAM_ENABLE_HOST_GLES
        invalidateColorBufferForGl(colorBufferHandle);
#else
        GFXSTREAM_ERROR("Failed to invalidate ColorBuffer:%d", colorBufferHandle);
#endif
    }

    const auto api = m_useVulkanComposition ? ColorBuffer::UsedApi::kVk : ColorBuffer::UsedApi::kGl;
    return colorBufferPtr->borrowForDisplay(api);
}

void FrameBuffer::logVulkanDeviceLost() {
    if (!m_emulationVk) {
        GFXSTREAM_FATAL("Device lost without VkEmulation?");
    }
    m_emulationVk->onVkDeviceLost();
}

void FrameBuffer::logVulkanOutOfMemory(VkResult result, const char* function, int line,
                                       std::optional<uint64_t> allocationSize) {
    m_logger->logMetricEvent(MetricEventVulkanOutOfMemory{
        .vkResultCode = result,
        .function = function,
        .line = std::make_optional(line),
        .allocationSize = allocationSize,
    });
}

void FrameBuffer::setVsyncHz(int vsyncHz) {
    const uint64_t kOneSecondNs = 1000000000ULL;
    m_vsyncHz = vsyncHz;
    if (m_vsyncThread) {
        m_vsyncThread->setPeriod(kOneSecondNs / (uint64_t)m_vsyncHz);
    }
}

void FrameBuffer::scheduleVsyncTask(VsyncThread::VsyncTask task) {
    if (!m_vsyncThread) {
        GFXSTREAM_ERROR("%s: warning: no vsync thread exists", __func__);
        task(0);
        return;
    }

    m_vsyncThread->schedule(task);
}

void FrameBuffer::setDisplayConfigs(int configId, int w, int h, int dpiX, int dpiY) {
    AutoLock mutex(m_lock);
    mDisplayConfigs[configId] = {w, h, dpiX, dpiY};
    GFXSTREAM_INFO("Setting display: %d configuration to: %dx%d, dpi: %dx%d ", configId, w, h, dpiX,
                   dpiY);
}

void FrameBuffer::setDisplayActiveConfig(int configId) {
    AutoLock mutex(m_lock);
    if (mDisplayConfigs.find(configId) == mDisplayConfigs.end()) {
        GFXSTREAM_ERROR("config %d not set", configId);
        return;
    }
    mDisplayActiveConfigId = configId;
    m_framebufferWidth = mDisplayConfigs[configId].w;
    m_framebufferHeight = mDisplayConfigs[configId].h;
    setDisplayPose(0, 0, 0, getWidth(), getHeight(), 0);
    GFXSTREAM_INFO("setDisplayActiveConfig %d", configId);
}

int FrameBuffer::getDisplayConfigsCount() {
    AutoLock mutex(m_lock);
    return mDisplayConfigs.size();
}

int FrameBuffer::getDisplayConfigsParam(int configId, EGLint param) {
    AutoLock mutex(m_lock);
    if (mDisplayConfigs.find(configId) == mDisplayConfigs.end()) {
        return -1;
    }
    switch (param) {
        case FB_WIDTH:
            return mDisplayConfigs[configId].w;
        case FB_HEIGHT:
            return mDisplayConfigs[configId].h;
        case FB_XDPI:
            return mDisplayConfigs[configId].dpiX;
        case FB_YDPI:
            return mDisplayConfigs[configId].dpiY;
        case FB_FPS:
            return 60;
        case FB_MIN_SWAP_INTERVAL:
            return -1;
        case FB_MAX_SWAP_INTERVAL:
            return -1;
        default:
            return -1;
    }
}

int FrameBuffer::getDisplayActiveConfig() {
    AutoLock mutex(m_lock);
    return mDisplayActiveConfigId >= 0 ? mDisplayActiveConfigId : -1;
}

bool FrameBuffer::flushColorBufferFromVk(HandleType colorBufferHandle) {
    AutoLock mutex(m_lock);
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("%s: Failed to find ColorBuffer:%d", __func__, colorBufferHandle);
        return false;
    }
    return colorBuffer->flushFromVk();
}

bool FrameBuffer::flushColorBufferFromVkBytes(HandleType colorBufferHandle, const void* bytes,
                                              size_t bytesSize) {
    AutoLock mutex(m_lock);

    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("%s: Failed to find ColorBuffer:%d", __func__, colorBufferHandle);
        return false;
    }
    return colorBuffer->flushFromVkBytes(bytes, bytesSize);
}

bool FrameBuffer::invalidateColorBufferForVk(HandleType colorBufferHandle) {
    // It reads contents from GL, which requires a context lock.
    // Also we should not do this in PostWorkerGl, otherwise it will deadlock.
    //
    // b/283524158
    // b/273986739
    AutoLock mutex(m_lock);
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_DEBUG("Failed to find ColorBuffer: %d", colorBufferHandle);
        return false;
    }
    return colorBuffer->invalidateForVk();
}

std::optional<BlobDescriptorInfo> FrameBuffer::exportColorBuffer(HandleType colorBufferHandle) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        return std::nullopt;
    }

    return colorBuffer->exportBlob();
}

std::optional<BlobDescriptorInfo> FrameBuffer::exportBuffer(HandleType bufferHandle) {
    AutoLock mutex(m_lock);

    BufferPtr buffer = findBuffer(bufferHandle);
    if (!buffer) {
        return std::nullopt;
    }

    return buffer->exportBlob();
}

#if GFXSTREAM_ENABLE_HOST_GLES
HandleType FrameBuffer::getEmulatedEglWindowSurfaceColorBufferHandle(HandleType p_surface) {
    AutoLock mutex(m_lock);

    auto it = m_EmulatedEglWindowSurfaceToColorBuffer.find(p_surface);
    if (it == m_EmulatedEglWindowSurfaceToColorBuffer.end()) {
        return 0;
    }

    return it->second;
}

#ifdef CONFIG_AEMU
void FrameBuffer::unregisterVulkanInstance(uint64_t id) const {
    get_gfxstream_vm_operations().unregister_vulkan_instance(id);
}

void FrameBuffer::registerVulkanInstance(uint64_t id, const char* appName) const {
    auto* tInfo = RenderThreadInfo::get();
    std::string process_name;
    if (tInfo && tInfo->m_processName.has_value()) {
        process_name = tInfo->m_processName.value();
        // for deqp: com.drawelements.deqp:testercore
        // remove the ":testercore" for deqp
        auto position = process_name.find(":");
        if (position != std::string::npos) {
            process_name = process_name.substr(0, position);
        }
    } else if(appName) {
        process_name = std::string(appName);
    }
    get_gfxstream_vm_operations().register_vulkan_instance(id, process_name.c_str());
}
#endif

void FrameBuffer::createTrivialContext(HandleType shared, HandleType* contextOut,
                                       HandleType* surfOut) {
    assert(contextOut);
    assert(surfOut);

    *contextOut = createEmulatedEglContext(0, shared, GLESApi_2);
    // Zero size is formally allowed here, but SwiftShader doesn't like it and
    // fails.
    *surfOut = createEmulatedEglWindowSurface(0, 1, 1);
}

void FrameBuffer::createSharedTrivialContext(EGLContext* contextOut, EGLSurface* surfOut) {
    assert(contextOut);
    assert(surfOut);

    const EmulatedEglConfig* config = getConfigs()->get(0 /* p_config */);
    if (!config) return;

    int maj, min;
    get_gfxstream_gles_version(&maj, &min);

    const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, maj,
                                     EGL_CONTEXT_MINOR_VERSION_KHR, min, EGL_NONE};

    *contextOut = s_egl.eglCreateContext(getDisplay(), config->getHostEglConfig(),
                                         getGlobalEGLContext(), contextAttribs);

    const EGLint pbufAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

    *surfOut = s_egl.eglCreatePbufferSurface(getDisplay(), config->getHostEglConfig(), pbufAttribs);
}

void FrameBuffer::destroySharedTrivialContext(EGLContext context, EGLSurface surface) {
    if (getDisplay() != EGL_NO_DISPLAY) {
        s_egl.eglDestroyContext(getDisplay(), context);
        s_egl.eglDestroySurface(getDisplay(), surface);
    }
}

const EmulatedEglConfigList* FrameBuffer::getConfigs() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return &m_emulationGl->getEmulationEglConfigs();
}

bool FrameBuffer::setEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface,
                                                         HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    EmulatedEglWindowSurfaceMap::iterator w(m_windows.find(p_surface));
    if (w == m_windows.end()) {
        // bad surface handle
        GFXSTREAM_ERROR("bad window surface handle %#x", p_surface);
        return false;
    }

    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
        if (c == m_colorbuffers.end()) {
            GFXSTREAM_ERROR("bad color buffer handle %d", p_colorbuffer);
            // bad colorbuffer handle
            return false;
        }

        (*w).second.first->setColorBuffer((*c).second.cb);
        markOpened(&c->second);
        if (!m_guestManagedColorBufferLifetime) {
            c->second.refcount++;
        }
    }
    if (w->second.second) {
        if (!m_guestManagedColorBufferLifetime) {
            if (m_refCountPipeEnabled) {
                decColorBufferRefCountLocked(w->second.second);
            } else {
                closeColorBufferLocked(w->second.second);
            }
        }
    }

    (*w).second.second = p_colorbuffer;

    m_EmulatedEglWindowSurfaceToColorBuffer[p_surface] = p_colorbuffer;

    return true;
}

HandleType FrameBuffer::createEmulatedEglContext(int config, HandleType shareContextHandle,
                                                 GLESApi version) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);
    gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);
    // Hold the ColorBuffer map lock so that the new handle won't collide with a ColorBuffer handle.
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    EmulatedEglContextPtr shareContext = nullptr;
    if (shareContextHandle != 0) {
        auto shareContextIt = m_contexts.find(shareContextHandle);
        if (shareContextIt == m_contexts.end()) {
            GFXSTREAM_ERROR("Failed to find share EmulatedEglContext:%d", shareContextHandle);
            return 0;
        }
        shareContext = shareContextIt->second;
    }

    HandleType contextHandle = genHandle_locked();
    auto context =
        m_emulationGl->createEmulatedEglContext(config, shareContext.get(), version, contextHandle);
    if (!context) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglContext.");
        return 0;
    }

    m_contexts[contextHandle] = std::move(context);

    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    uint64_t puid = tinfo->m_puid;
    // The new emulator manages render contexts per guest process.
    // Fall back to per-thread management if the system image does not
    // support it.
    if (puid) {
        m_procOwnedEmulatedEglContexts[puid].insert(contextHandle);
    } else {  // legacy path to manage context lifetime by threads
        if (!tinfo->m_glInfo) {
            GFXSTREAM_FATAL("RenderThreadGL not available.");
        }
        tinfo->m_glInfo->m_contextSet.insert(contextHandle);
    }

    return contextHandle;
}

void FrameBuffer::destroyEmulatedEglContext(HandleType contextHandle) {
    AutoLock mutex(m_lock);
    sweepColorBuffersLocked();

    gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);
    m_contexts.erase(contextHandle);
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    uint64_t puid = tinfo->m_puid;
    // The new emulator manages render contexts per guest process.
    // Fall back to per-thread management if the system image does not
    // support it.
    if (puid) {
        auto it = m_procOwnedEmulatedEglContexts.find(puid);
        if (it != m_procOwnedEmulatedEglContexts.end()) {
            it->second.erase(contextHandle);
        }
    } else {
        if (!tinfo->m_glInfo) {
            GFXSTREAM_FATAL("RenderThreadGL not available.");
        }
        tinfo->m_glInfo->m_contextSet.erase(contextHandle);
    }
}

HandleType FrameBuffer::createEmulatedEglWindowSurface(int p_config, int p_width, int p_height) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);
    // Hold the ColorBuffer map lock so that the new handle won't collide with a ColorBuffer handle.
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    HandleType handle = genHandle_locked();

    auto window =
        m_emulationGl->createEmulatedEglWindowSurface(p_config, p_width, p_height, handle);
    if (!window) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglWindowSurface.");
        return 0;
    }

    m_windows[handle] = {std::move(window), 0};

    RenderThreadInfo* info = RenderThreadInfo::get();
    if (!info->m_glInfo) {
        GFXSTREAM_FATAL("RRenderThreadInfoGl not available.");
    }

    uint64_t puid = info->m_puid;
    if (puid) {
        m_procOwnedEmulatedEglWindowSurfaces[puid].insert(handle);
    } else {  // legacy path to manage window surface lifetime by threads
        info->m_glInfo->m_windowSet.insert(handle);
    }

    return handle;
}

void FrameBuffer::destroyEmulatedEglWindowSurface(HandleType p_surface) {
    if (m_shuttingDown) {
        return;
    }
    AutoLock mutex(m_lock);
    destroyEmulatedEglWindowSurfaceLocked(p_surface);
}

std::vector<HandleType> FrameBuffer::destroyEmulatedEglWindowSurfaceLocked(HandleType p_surface) {
    std::vector<HandleType> colorBuffersToCleanUp;
    const auto w = m_windows.find(p_surface);
    if (w != m_windows.end()) {
        RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
        if (!m_guestManagedColorBufferLifetime) {
            if (m_refCountPipeEnabled) {
                if (decColorBufferRefCountLocked(w->second.second)) {
                    colorBuffersToCleanUp.push_back(w->second.second);
                }
            } else {
                if (closeColorBufferLocked(w->second.second)) {
                    colorBuffersToCleanUp.push_back(w->second.second);
                }
            }
        }
        m_windows.erase(w);
        RenderThreadInfo* tinfo = RenderThreadInfo::get();
        uint64_t puid = tinfo->m_puid;
        if (puid) {
            auto ite = m_procOwnedEmulatedEglWindowSurfaces.find(puid);
            if (ite != m_procOwnedEmulatedEglWindowSurfaces.end()) {
                ite->second.erase(p_surface);
            }
        } else {
            if (!tinfo->m_glInfo) {
                GFXSTREAM_FATAL("RenderThreadGL not available.");
            }
            tinfo->m_glInfo->m_windowSet.erase(p_surface);
        }
    }
    return colorBuffersToCleanUp;
}

void FrameBuffer::createEmulatedEglFenceSync(EGLenum type, int destroyWhenSignaled,
                                             uint64_t* outSync, uint64_t* outSyncThread) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    // TODO(b/233939967): move RenderThreadInfoGl usage to EmulationGl.
    RenderThreadInfoGl* const info = RenderThreadInfoGl::get();
    if (!info) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }
    if (!info->currContext) {
        uint32_t syncContext;
        uint32_t syncSurface;
        createTrivialContext(0,  // There is no context to share.
                             &syncContext, &syncSurface);
        bindContext(syncContext, syncSurface, syncSurface);
        // This context is then cleaned up when the render thread exits.
    }

    auto sync = m_emulationGl->createEmulatedEglFenceSync(type, destroyWhenSignaled);
    if (!sync) {
        return;
    }

    if (outSync) {
        *outSync = (uint64_t)(uintptr_t)sync.release();
    }
    if (outSyncThread) {
        *outSyncThread = reinterpret_cast<uint64_t>(SyncThread::get());
    }
}

void FrameBuffer::drainGlRenderThreadResources() {
    // If we're already exiting then snapshot should not contain
    // this thread information at all.
    if (isShuttingDown()) {
        return;
    }

    // Release references to the current thread's context/surfaces if any
    bindContext(0, 0, 0);

    drainGlRenderThreadSurfaces();
    drainGlRenderThreadContexts();

    if (!s_egl.eglReleaseThread()) {
        GFXSTREAM_ERROR("Error: RenderThread @%p failed to eglReleaseThread()", this);
    }
}

void FrameBuffer::drainGlRenderThreadContexts() {
    if (isShuttingDown()) {
        return;
    }

    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }

    if (tinfo->m_contextSet.empty()) {
        return;
    }

    AutoLock mutex(m_lock);
    gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);
    for (const HandleType contextHandle : tinfo->m_contextSet) {
        m_contexts.erase(contextHandle);
    }
    tinfo->m_contextSet.clear();
}

void FrameBuffer::drainGlRenderThreadSurfaces() {
    if (isShuttingDown()) {
        return;
    }

    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }

    if (tinfo->m_windowSet.empty()) {
        return;
    }

    std::vector<HandleType> colorBuffersToCleanup;

    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
    for (const HandleType winHandle : tinfo->m_windowSet) {
        const auto winIt = m_windows.find(winHandle);
        if (winIt != m_windows.end()) {
            if (const HandleType oldColorBufferHandle = winIt->second.second) {
                if (!m_guestManagedColorBufferLifetime) {
                    if (m_refCountPipeEnabled) {
                        if (decColorBufferRefCountLocked(oldColorBufferHandle)) {
                            colorBuffersToCleanup.push_back(oldColorBufferHandle);
                        }
                    } else {
                        if (closeColorBufferLocked(oldColorBufferHandle)) {
                            colorBuffersToCleanup.push_back(oldColorBufferHandle);
                        }
                    }
                }
                m_windows.erase(winIt);
            }
        }
    }
    tinfo->m_windowSet.clear();
}

EmulationGl& FrameBuffer::getEmulationGl() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return *m_emulationGl;
}

VkEmulation& FrameBuffer::getEmulationVk() {
    if (!m_emulationVk) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return *m_emulationVk;
}

EGLDisplay FrameBuffer::getDisplay() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return m_emulationGl->mEglDisplay;
}

EGLSurface FrameBuffer::getWindowSurface() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    if (!m_emulationGl->mWindowSurface) {
        return EGL_NO_SURFACE;
    }

    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(m_emulationGl->mWindowSurface->getImpl());

    return displaySurfaceGl->getSurface();
}

EGLContext FrameBuffer::getContext() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return m_emulationGl->mEglContext;
}

EGLContext FrameBuffer::getConfig() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return m_emulationGl->mEglConfig;
}

EGLContext FrameBuffer::getGlobalEGLContext() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    if (!m_emulationGl->mPbufferSurface) {
        GFXSTREAM_FATAL("FrameBuffer pbuffer surface not available.");
    }

    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(m_emulationGl->mPbufferSurface->getImpl());

    return displaySurfaceGl->getContextForShareContext();
}

EmulatedEglContextPtr FrameBuffer::getContext_locked(HandleType p_context) {
    return gfxstream::base::findOrDefault(m_contexts, p_context);
}

EmulatedEglWindowSurfacePtr FrameBuffer::getWindowSurface_locked(HandleType p_windowsurface) {
    return gfxstream::base::findOrDefault(m_windows, p_windowsurface).first;
}

TextureDraw* FrameBuffer::getTextureDraw() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->mTextureDraw.get();
}

bool FrameBuffer::isFastBlitSupported() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->isFastBlitSupported();
}

void FrameBuffer::disableFastBlitForTesting() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    m_emulationGl->disableFastBlitForTesting();
}

HandleType FrameBuffer::createEmulatedEglImage(HandleType contextHandle, EGLenum target,
                                               GLuint buffer) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);

    EmulatedEglContext* context = nullptr;
    if (contextHandle) {
        gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);

        auto it = m_contexts.find(contextHandle);
        if (it == m_contexts.end()) {
            GFXSTREAM_ERROR("Failed to find EmulatedEglContext:%d", contextHandle);
            return false;
        }

        context = it->second.get();
    }

    auto image = m_emulationGl->createEmulatedEglImage(context, target,
                                                       reinterpret_cast<EGLClientBuffer>(buffer));
    if (!image) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglImage");
        return false;
    }

    HandleType imageHandle = image->getHandle();

    m_images[imageHandle] = std::move(image);

    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        m_procOwnedEmulatedEglImages[puid].insert(imageHandle);
    }
    return imageHandle;
}

EGLBoolean FrameBuffer::destroyEmulatedEglImage(HandleType imageHandle) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);

    auto imageIt = m_images.find(imageHandle);
    if (imageIt == m_images.end()) {
        GFXSTREAM_ERROR("Failed to find EmulatedEglImage:%d", imageHandle);
        return false;
    }
    auto& image = imageIt->second;

    EGLBoolean success = image->destroy();
    m_images.erase(imageIt);

    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        m_procOwnedEmulatedEglImages[puid].erase(imageHandle);
        // We don't explicitly call m_procOwnedEmulatedEglImages.erase(puid) when the
        // size reaches 0, since it could go between zero and one many times in
        // the lifetime of a process. It will be cleaned up by
        // cleanupProcGLObjects(puid) when the process is dead.
    }
    return success;
}

bool FrameBuffer::flushEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface) {
    AutoLock mutex(m_lock);

    auto it = m_windows.find(p_surface);
    if (it == m_windows.end()) {
        GFXSTREAM_ERROR("FB::flushEmulatedEglWindowSurfaceColorBuffer: window handle %#x not found",
                        p_surface);
        // bad surface handle
        return false;
    }

    EmulatedEglWindowSurface* surface = it->second.first.get();
    surface->flushColorBuffer();

    return true;
}

GLESDispatchMaxVersion FrameBuffer::getMaxGLESVersion() {
    if (!m_emulationGl) {
        return GLES_DISPATCH_MAX_VERSION_2;
    }
    return m_emulationGl->getGlesMaxDispatchVersion();
}

void FrameBuffer::fillGLESUsages(android_studio::EmulatorGLESUsages* usages) {
    if (s_egl.eglFillUsages) {
        s_egl.eglFillUsages(usages);
    }
}

void* FrameBuffer::platformCreateSharedEglContext(void) {
    AutoLock lock(m_lock);

    EGLContext context = 0;
    EGLSurface surface = 0;
    createSharedTrivialContext(&context, &surface);

    void* underlyingContext = s_egl.eglGetNativeContextANDROID(getDisplay(), context);
    if (!underlyingContext) {
        GFXSTREAM_ERROR("Error: Underlying egl backend could not produce a native EGL context.");
        return nullptr;
    }

    m_platformEglContexts[underlyingContext] = {context, surface};

#if defined(__QNX__)
    EGLDisplay currDisplay = eglGetCurrentDisplay();
    EGLSurface currRead = eglGetCurrentSurface(EGL_READ);
    EGLSurface currDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface currContext = eglGetCurrentContext();
    // Make this context current to ensure thread-state is initialized
    s_egl.eglMakeCurrent(getDisplay(), surface, surface, context);
    // Revert back to original state
    s_egl.eglMakeCurrent(currDisplay, currRead, currDraw, currContext);
#endif

    return underlyingContext;
}

bool FrameBuffer::platformDestroySharedEglContext(void* underlyingContext) {
    AutoLock lock(m_lock);

    auto it = m_platformEglContexts.find(underlyingContext);
    if (it == m_platformEglContexts.end()) {
        GFXSTREAM_ERROR(
            "Error: Could not find underlying egl context %p (perhaps already destroyed?)",
            underlyingContext);
        return false;
    }

    destroySharedTrivialContext(it->second.context, it->second.surface);

    m_platformEglContexts.erase(it);

    return true;
}

bool FrameBuffer::flushColorBufferFromGl(HandleType colorBufferHandle) {
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("%s: Failed to find ColorBuffer:%d", __func__, colorBufferHandle);
        return false;
    }
    return colorBuffer->flushFromGl();
}

bool FrameBuffer::invalidateColorBufferForGl(HandleType colorBufferHandle) {
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_DEBUG("Failed to find ColorBuffer: %d", colorBufferHandle);
        return false;
    }
    return colorBuffer->invalidateForGl();
}

ContextHelper* FrameBuffer::getPbufferSurfaceContextHelper() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    if (!m_emulationGl->mPbufferSurface) {
        GFXSTREAM_FATAL("EGL emulation pbuffer surface not available.");
    }
    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(m_emulationGl->mPbufferSurface->getImpl());

    return displaySurfaceGl->getContextHelper();
}

bool FrameBuffer::bindColorBufferToTexture(HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpBindToTexture();
}

bool FrameBuffer::bindColorBufferToTexture2(HandleType p_colorbuffer) {
    // This is only called when using multi window display
    // It will deadlock when posting from main thread.
    std::unique_ptr<AutoLock> mutex;
    if (!postOnlyOnMainThread()) {
        mutex = std::make_unique<AutoLock>(m_lock);
    }

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpBindToTexture2();
}

bool FrameBuffer::bindColorBufferToRenderbuffer(HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpBindToRenderbuffer();
}

bool FrameBuffer::bindContext(HandleType p_context, HandleType p_drawSurface,
                              HandleType p_readSurface) {
    if (m_shuttingDown) {
        return false;
    }

    AutoLock mutex(m_lock);

    EmulatedEglWindowSurfacePtr draw, read;
    EmulatedEglContextPtr ctx;

    //
    // if this is not an unbind operation - make sure all handles are good
    //
    if (p_context || p_drawSurface || p_readSurface) {
        ctx = getContext_locked(p_context);
        if (!ctx) return false;
        auto drawWindowIt = m_windows.find(p_drawSurface);
        if (drawWindowIt == m_windows.end()) {
            // bad surface handle
            return false;
        }
        draw = (*drawWindowIt).second.first;

        if (p_readSurface != p_drawSurface) {
            auto readWindowIt = m_windows.find(p_readSurface);
            if (readWindowIt == m_windows.end()) {
                // bad surface handle
                return false;
            }
            read = (*readWindowIt).second.first;
        } else {
            read = draw;
        }
    } else {
        // if unbind operation, sweep color buffers
        sweepColorBuffersLocked();
    }

    if (!s_egl.eglMakeCurrent(getDisplay(), draw ? draw->getEGLSurface() : EGL_NO_SURFACE,
                              read ? read->getEGLSurface() : EGL_NO_SURFACE,
                              ctx ? ctx->getEGLContext() : EGL_NO_CONTEXT)) {
        GFXSTREAM_ERROR("eglMakeCurrent failed");
        return false;
    }

    //
    // Bind the surface(s) to the context
    //
    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGl not available.");
    }

    EmulatedEglWindowSurfacePtr bindDraw, bindRead;
    if (draw.get() == NULL && read.get() == NULL) {
        // Unbind the current read and draw surfaces from the context
        bindDraw = tinfo->currDrawSurf;
        bindRead = tinfo->currReadSurf;
    } else {
        bindDraw = draw;
        bindRead = read;
    }

    if (bindDraw.get() != NULL && bindRead.get() != NULL) {
        if (bindDraw.get() != bindRead.get()) {
            bindDraw->bind(ctx, EmulatedEglWindowSurface::BIND_DRAW);
            bindRead->bind(ctx, EmulatedEglWindowSurface::BIND_READ);
        } else {
            bindDraw->bind(ctx, EmulatedEglWindowSurface::BIND_READDRAW);
        }
    }

    //
    // update thread info with current bound context
    //
    tinfo->currContext = ctx;
    tinfo->currDrawSurf = draw;
    tinfo->currReadSurf = read;
    if (ctx) {
        if (ctx->clientVersion() > GLESApi_CM)
            tinfo->m_gl2Dec.setContextData(&ctx->decoderContextData());
        else
            tinfo->m_glDec.setContextData(&ctx->decoderContextData());
    } else {
        tinfo->m_glDec.setContextData(NULL);
        tinfo->m_gl2Dec.setContextData(NULL);
    }
    return true;
}

void FrameBuffer::createYUVTextures(uint32_t type, uint32_t count, int width, int height,
                                    uint32_t* output) {
    FrameworkFormat format = static_cast<FrameworkFormat>(type);
    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
    for (uint32_t i = 0; i < count; ++i) {
        if (format == FRAMEWORK_FORMAT_NV12) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height, format, m_features.Yuv420888ToNv21.enabled,
                                         YUVPlane::Y, &output[2 * i]);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::UV,
                                         &output[2 * i + 1]);
        } else if (format == FRAMEWORK_FORMAT_YUV_420_888) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::Y,
                                         &output[3 * i]);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::U,
                                         &output[3 * i + 1]);
            YUVConverter::createYUVGLTex(GL_TEXTURE2, width / 2, height / 2, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::V,
                                         &output[3 * i + 2]);
        }
    }
}

void FrameBuffer::destroyYUVTextures(uint32_t type, uint32_t count, uint32_t* textures) {
    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
    if (type == FRAMEWORK_FORMAT_NV12) {
        s_gles2.glDeleteTextures(2 * count, textures);
    } else if (type == FRAMEWORK_FORMAT_YUV_420_888) {
        s_gles2.glDeleteTextures(3 * count, textures);
    }
}

void FrameBuffer::updateYUVTextures(uint32_t type, uint32_t* textures, void* privData, void* func) {
    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());

    yuv_updater_t updater = (yuv_updater_t)func;
    uint32_t gtextures[3] = {0, 0, 0};

    if (type == FRAMEWORK_FORMAT_NV12) {
        gtextures[0] = s_gles2.glGetGlobalTexName(textures[0]);
        gtextures[1] = s_gles2.glGetGlobalTexName(textures[1]);
    } else if (type == FRAMEWORK_FORMAT_YUV_420_888) {
        gtextures[0] = s_gles2.glGetGlobalTexName(textures[0]);
        gtextures[1] = s_gles2.glGetGlobalTexName(textures[1]);
        gtextures[2] = s_gles2.glGetGlobalTexName(textures[2]);
    }

#ifdef __APPLE__
    EGLContext prevContext = s_egl.eglGetCurrentContext();
    auto mydisp = EglGlobalInfo::getInstance()->getDisplayFromDisplayType(EGL_DEFAULT_DISPLAY);
    void* nativecontext = mydisp->getLowLevelContext(prevContext);
    struct MediaNativeCallerData callerdata;
    callerdata.ctx = nativecontext;
    callerdata.converter = nsConvertVideoFrameToNV12Textures;
    void* pcallerdata = &callerdata;
#else
    void* pcallerdata = nullptr;
#endif

    updater(privData, type, gtextures, pcallerdata);
}

void FrameBuffer::swapTexturesAndUpdateColorBuffer(uint32_t p_colorbuffer, int x, int y, int width,
                                                   int height, uint32_t format, uint32_t type,
                                                   uint32_t texture_type, uint32_t* textures) {
    {
        AutoLock mutex(m_lock);
        ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
        if (!colorBuffer) {
            // bad colorbuffer handle
            return;
        }
        colorBuffer->glOpSwapYuvTexturesAndUpdate(
            format, type, static_cast<FrameworkFormat>(texture_type), textures);
    }
}

bool FrameBuffer::readColorBufferContents(HandleType p_colorbuffer, size_t* numBytes,
                                          void* pixels) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpReadContents(numBytes, pixels);
}

void FrameBuffer::asyncWaitForGpuWithCb(uint64_t eglsync, FenceCompletionCallback cb) {
    EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(eglsync);

    if (!fenceSync) {
        GFXSTREAM_ERROR("err: fence sync 0x%llx not found", (unsigned long long)eglsync);
        return;
    }

    SyncThread::get()->triggerWaitWithCompletionCallback(fenceSync, std::move(cb));
}

const gl::GLESv2Dispatch* FrameBuffer::getGles2Dispatch() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->getGles2Dispatch();
}

const gl::EGLDispatch* FrameBuffer::getEglDispatch() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->getEglDispatch();
}

#endif

}  // namespace gfxstream
