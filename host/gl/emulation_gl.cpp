// Copyright (C) 2022 The Android Open Source Project
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

#include "emulation_gl.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "OpenGLESDispatch/OpenGLDispatchLoader.h"
#include "common/gles_context.h"
#include "display_surface_gl.h"
#include "glestranslator/egl/egl_global_info.h"
#include "gfxstream/ThreadAnnotations.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/host/color_buffer_interface.h"
#include "gfxstream/host/driver_info.h"
#include "gfxstream/host/framework_formats.h"
#include "gfxstream/host/gfxstream_format.h"
#include "gfxstream/host/global_state.h"
#include "gfxstream/host/renderer_operations.h"
#include "gfxstream/host/stream_utils.h"
#include "gfxstream/misc/StringUtils.h"
#include "gles_version_detector.h"
#include "render-utils/MediaNative.h"
#include "render-utils/RenderLib.h"
#include "render_thread_info_gl.h"
#include "yuv_converter.h"

namespace gfxstream {
namespace host {
namespace gl {
namespace {

std::optional<GfxstreamFormat> GetGfxstreamFormat(const FeatureSet& features,
                                                  FrameworkFormat format) {
    switch (format) {
        case FRAMEWORK_FORMAT_NV12:
            return GfxstreamFormat::NV12;
        case FRAMEWORK_FORMAT_YV12:
            return GfxstreamFormat::YV12;
        case FRAMEWORK_FORMAT_P010:
            return GfxstreamFormat::P010;
        case FRAMEWORK_FORMAT_YUV_420_888: {
            if (features.Yuv420888ToNv21.enabled()) {
                return GfxstreamFormat::NV21;
            } else {
                return GfxstreamFormat::YV21;
            }
        }
        default:
            return std::nullopt;
    }
}

template <class Collection>
static void saveProcOwnedCollection(gfxstream::Stream* stream, const Collection& c) {
    const int count = std::count_if(
        c.begin(), c.end(),
        [](const typename Collection::value_type& pair) { return !pair.second.empty(); });
    stream->putBe32(count);
    for (const auto& pair : c) {
        if (pair.second.empty()) {
            continue;
        }
        stream->putBe64(pair.first);
        saveCollection(stream, pair.second,
                       [](gfxstream::Stream* s, HandleType h) { s->putBe32(h); });
    }
}

template <class Collection>
static void loadProcOwnedCollection(gfxstream::Stream* stream, Collection* c) {
    loadCollection(stream, c, [](gfxstream::Stream* stream) -> typename Collection::value_type {
        const int processId = stream->getBe64();
        typename Collection::mapped_type handles;
        loadCollection(stream, &handles, [](gfxstream::Stream* s) { return s->getBe32(); });
        return {processId, std::move(handles)};
    });
}

#ifdef ENABLE_GFXSTREAM_DEBUG

static void EGLAPIENTRY EglDebugCallback(EGLenum error,
                                         const char *command,
                                         EGLint messageType,
                                         EGLLabelKHR threadLabel,
                                         EGLLabelKHR objectLabel,
                                         const char *message) {
    GFXSTREAM_DEBUG("command:%s message:%s", command, message);
}

static void GL_APIENTRY GlDebugCallback(GLenum source,
                                        GLenum type,
                                        GLuint id,
                                        GLenum severity,
                                        GLsizei length,
                                        const GLchar *message,
                                        const void *userParam) {
    GFXSTREAM_DEBUG("message:%s", message);
}

#endif // ENABLE_GFXSTREAM_DEBUG

static const GLint kGles2ContextAttribsESOrGLCompat[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,  //
    EGL_NONE,                       //
};

static const GLint kGles2ContextAttribsCoreGL[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,                                                 //
    EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,  //
    EGL_NONE,                                                                      //
};

static const GLint kGles3ContextAttribsESOrGLCompat[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,  //
    EGL_NONE,                       //
};

static const GLint kGles3ContextAttribsCoreGL[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,                                                 //
    EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,  //
    EGL_NONE,                                                                      //
};

static bool validateGles2Context(EGLDisplay display, gfxstream::host::GpuVendor& gpuVendor) {
    const GLint configAttribs[] = {// Request at least 8 bits for Red/Green/Blue
                                   EGL_RED_SIZE,
                                   8,
                                   EGL_GREEN_SIZE,
                                   8,
                                   EGL_BLUE_SIZE,
                                   8,
                                   EGL_SURFACE_TYPE,
                                   EGL_PBUFFER_BIT,
                                   EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_ES2_BIT,
                                   EGL_NONE};

    EGLint numConfigs = 0;
    EGLConfig config;
    if (!s_egl.eglChooseConfig(display, configAttribs, &config, 1, &numConfigs)) {
        GFXSTREAM_ERROR("Failed to find GLES 2.x config.");
        return false;
    }
    if (numConfigs != 1) {
        GFXSTREAM_ERROR("Failed to find exactly 1 GLES 2.x config: found %d.", numConfigs);
        return false;
    }

    const EGLint surfaceAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE,
    };

    EGLSurface surface = s_egl.eglCreatePbufferSurface(display, config, surfaceAttribs);
    if (surface == EGL_NO_SURFACE) {
        GFXSTREAM_ERROR("Failed to create GLES 2.x pbuffer surface.");
        return false;
    }

    const GLint* contextAttribs = EmulationGl::getGlesMaxContextAttribs();
    EGLContext context = s_egl.eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        GFXSTREAM_ERROR("Failed to create GLES 2.x context.");
        s_egl.eglDestroySurface(display, surface);
        return false;
    }

    if (!s_egl.eglMakeCurrent(display, surface, surface, context)) {
        GFXSTREAM_ERROR("Failed to make GLES 2.x context current.");
        s_egl.eglDestroySurface(display, surface);
        s_egl.eglDestroyContext(display, context);
        return false;
    }

    const char* extensions = (const char*)s_gles2.glGetString(GL_EXTENSIONS);
    const char* c_gpuVendorName = (const char*)s_gles2.glGetString(GL_VENDOR);
    if (c_gpuVendorName) {
        std::string gpuVendorName = c_gpuVendorName;
        gpuVendor = gfxstream::host::GetGpuVendor(gpuVendorName);
    }
    if (extensions == nullptr) {
        GFXSTREAM_ERROR("Failed to query GLES 2.x context extensions.");
        s_egl.eglDestroySurface(display, surface);
        s_egl.eglDestroyContext(display, context);
        return false;
    }

    // It is rare but some drivers actually fail this...
    if (!s_egl.eglMakeCurrent(display, EGL_NO_CONTEXT, EGL_NO_SURFACE, EGL_NO_SURFACE)) {
        GFXSTREAM_ERROR("Failed to unbind GLES 2.x context.");
        s_egl.eglDestroySurface(display, surface);
        s_egl.eglDestroyContext(display, context);
        return false;
    }

    s_egl.eglDestroyContext(display, context);
    s_egl.eglDestroySurface(display, surface);
    return true;
}

static std::optional<EGLConfig> getEmulationEglConfig(EGLDisplay display, bool allowWindowSurface) {
    GLint surfaceType = EGL_PBUFFER_BIT;

    if (allowWindowSurface) {
        surfaceType |= EGL_WINDOW_BIT;
    }

    // On Linux, we need RGB888 exactly, or eglMakeCurrent will fail,
    // as glXMakeContextCurrent needs to match the format of the
    // native pixmap.
    constexpr const EGLint kWantedRedSize = 8;
    constexpr const EGLint kWantedGreenSize = 8;
    constexpr const EGLint kWantedBlueSize = 8;

    const GLint configAttribs[] = {
        EGL_RED_SIZE, kWantedRedSize,             //
        EGL_GREEN_SIZE, kWantedGreenSize,         //
        EGL_BLUE_SIZE, kWantedBlueSize,           //
        EGL_SURFACE_TYPE, surfaceType,            //
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,  //
        EGL_NONE,                                 //
    };

    EGLint numConfigs = 0;
    s_egl.eglGetConfigs(display, nullptr, 0, &numConfigs);

    std::vector<EGLConfig> configs(numConfigs);

    EGLint numMatchedConfigs = 0;
    s_egl.eglChooseConfig(display, configAttribs, configs.data(), numConfigs, &numMatchedConfigs);

    configs.resize(numMatchedConfigs);

    for (EGLConfig config : configs) {
        EGLint foundRedSize = 0;
        s_egl.eglGetConfigAttrib(display, config, EGL_RED_SIZE, &foundRedSize);
        if (foundRedSize != kWantedRedSize) {
            continue;
        }

        EGLint foundGreenSize = 0;
        s_egl.eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &foundGreenSize);
        if (foundGreenSize != kWantedGreenSize) {
            continue;
        }

        EGLint foundBlueSize = 0;
        s_egl.eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &foundBlueSize);
        if (foundBlueSize != kWantedBlueSize) {
            continue;
        }

        return config;
    }

    return std::nullopt;
}

}  // namespace

bool EmulationGl::initDispatchers(bool eglOnEgl) {
    // Loads the glestranslator function pointers.
    if (!LazyLoadedEGLDispatch::get()) {
        GFXSTREAM_ERROR("Failed to load EGL dispatch.");
        return false;
    }
    if (!LazyLoadedGLESv1Dispatch::get()) {
        GFXSTREAM_ERROR("Failed to load GLESv1 dispatch.");
        return false;
    }
    if (!LazyLoadedGLESv2Dispatch::get()) {
        GFXSTREAM_ERROR("Failed to load GLESv2 dispatch.");
        return false;
    }

    if (s_egl.eglUseOsEglApi) {
        s_egl.eglUseOsEglApi(eglOnEgl, EGL_FALSE);
    }

    return true;
}

std::unique_ptr<EmulationGl> EmulationGl::create(uint32_t width, uint32_t height,
                                                 const gfxstream::host::FeatureSet& features,
                                                 bool allowWindowSurface,
                                                 GlobalState* globalState) {
    std::unique_ptr<EmulationGl> emulationGl(new EmulationGl());

    emulationGl->mFeatures = features;
    emulationGl->mWidth = width;
    emulationGl->mHeight = height;
    emulationGl->mGlobalState = globalState;

    emulationGl->mEglDisplay = s_egl.eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (emulationGl->mEglDisplay == EGL_NO_DISPLAY) {
        GFXSTREAM_ERROR("Failed to get EGL display.");
        return nullptr;
    }

    GFXSTREAM_DEBUG("call eglInitialize");
    if (!s_egl.eglInitialize(emulationGl->mEglDisplay,
                             &emulationGl->mEglVersionMajor,
                             &emulationGl->mEglVersionMinor)) {
        GFXSTREAM_ERROR("Failed to eglInitialize.");
        return nullptr;
    }

    if (s_egl.eglSetNativeTextureDecompressionEnabledANDROID) {
        s_egl.eglSetNativeTextureDecompressionEnabledANDROID(
            emulationGl->mEglDisplay,
            emulationGl->mFeatures.NativeTextureDecompression.enabled());
    }

    if (s_egl.eglSetProgramBinaryLinkStatusEnabledANDROID) {
        s_egl.eglSetProgramBinaryLinkStatusEnabledANDROID(
            emulationGl->mEglDisplay,
            emulationGl->mFeatures.GlProgramBinaryLinkStatus.enabled());
    }

    s_egl.eglBindAPI(EGL_OPENGL_ES_API);

#ifdef ENABLE_GFXSTREAM_DEBUG
    if (s_egl.eglDebugMessageControlKHR) {
        const EGLAttrib controls[] = {
            EGL_DEBUG_MSG_CRITICAL_KHR,
            EGL_TRUE,
            EGL_DEBUG_MSG_ERROR_KHR,
            EGL_TRUE,
            EGL_DEBUG_MSG_WARN_KHR,
            EGL_TRUE,
            EGL_DEBUG_MSG_INFO_KHR,
            EGL_FALSE,
            EGL_NONE,
            EGL_NONE,
        };

        if (s_egl.eglDebugMessageControlKHR(&EglDebugCallback, controls) == EGL_SUCCESS) {
            GFXSTREAM_DEBUG("Successfully set eglDebugMessageControlKHR");
        } else {
            GFXSTREAM_DEBUG("Failed to eglDebugMessageControlKHR");
        }
    } else {
        GFXSTREAM_DEBUG("eglDebugMessageControlKHR not available");
    }
#endif

    emulationGl->mEglVendor = s_egl.eglQueryString(emulationGl->mEglDisplay, EGL_VENDOR);

    const std::string eglExtensions = s_egl.eglQueryString(emulationGl->mEglDisplay, EGL_EXTENSIONS);
    gfxstream::base::split<std::string>(eglExtensions, " ",
                                      [&](const std::string& found) {
                                        emulationGl->mEglExtensions.insert(found);
                                      });

    if (!emulationGl->hasEglExtension("EGL_KHR_gl_texture_2D_image")) {
        GFXSTREAM_ERROR("Failed to find required EGL_KHR_gl_texture_2D_image extension.");
        return nullptr;
    }

    gfxstream::host::GpuVendor gpuVendor{gfxstream::host::GpuVendor::kUnknown};
    auto ChooseGlesVersionAndPopulateDispatch =
        [&](GLESDispatchMaxVersion maxAllowedVersion) -> bool {
        GLEScontext::dispatcher().clearDispatchFuncs();
        auto maxVersion =
            calcMaxVersionFromDispatch(emulationGl->mFeatures, emulationGl->mEglDisplay);
        if (maxVersion > maxAllowedVersion) {
            maxVersion = maxAllowedVersion;
        }
        int major = 2;
        int minor = 0;
        switch (maxVersion) {
            case GLES_DISPATCH_MAX_VERSION_2:
                major = 2;
                minor = 0;
                break;
            case GLES_DISPATCH_MAX_VERSION_3_0:
                major = 3;
                minor = 0;
                break;
            case GLES_DISPATCH_MAX_VERSION_3_1:
                major = 3;
                minor = 1;
                break;
            case GLES_DISPATCH_MAX_VERSION_3_2:
                major = 3;
                minor = 2;
                break;
            default:
                break;
        }

        set_gfxstream_gles_version(major, minor);
        emulationGl->mGlesDispatchMaxVersion = maxVersion;
        if (s_egl.eglSetMaxGLESVersion) {
            // eglSetMaxGLESVersion must be called before any context binding
            // because it changes how we initialize the dispatcher table.
            s_egl.eglSetMaxGLESVersion(emulationGl->mGlesDispatchMaxVersion);
        }

        emulationGl->mGlesVersionMajor = major;
        emulationGl->mGlesVersionMinor = minor;

        if (!validateGles2Context(emulationGl->mEglDisplay, gpuVendor)) {
            GFXSTREAM_ERROR("Failed to validate creating GLES 2.x context.");
            return false;
        }
        return true;
    };

    if (!ChooseGlesVersionAndPopulateDispatch(GLES_DISPATCH_MAX_VERSION_3_2)) {
        return nullptr;
    }
    if (emulationGl->mGlesDispatchMaxVersion > GLES_DISPATCH_MAX_VERSION_3_0) {
        if (gpuVendor == gfxstream::host::GpuVendor::kIntel) {
            // BUG: 435712974
            // Intel gpu driver has crashes when guest is running
            // gles 3.1. so we have to cap the max gles to 3.0
            if (!ChooseGlesVersionAndPopulateDispatch(GLES_DISPATCH_MAX_VERSION_3_0)) {
                return nullptr;
            }
        }
    }

    // TODO (b/207426737): Remove the Imagination-specific workaround.
    const bool disableFastBlit =
        emulationGl->mEglVendor.find("Imagination Technologies") != std::string::npos;

    emulationGl->mFastBlitSupported =
        (emulationGl->mGlesDispatchMaxVersion > GLES_DISPATCH_MAX_VERSION_2) &&
        !disableFastBlit &&
        (get_gfxstream_renderer() == SELECTED_RENDERER_HOST ||
         get_gfxstream_renderer() == SELECTED_RENDERER_SWIFTSHADER_INDIRECT ||
         get_gfxstream_renderer() == SELECTED_RENDERER_LAVAPIPE ||
         get_gfxstream_renderer() == SELECTED_RENDERER_ANGLE_INDIRECT);

    auto eglConfigOpt = getEmulationEglConfig(emulationGl->mEglDisplay, allowWindowSurface);
    if (!eglConfigOpt) {
        GFXSTREAM_ERROR("Failed to find config for emulation GL.");
        return nullptr;
    }
    emulationGl->mEglConfig = *eglConfigOpt;

    const GLint* maxContextAttribs = getGlesMaxContextAttribs();

    emulationGl->mEglContext = s_egl.eglCreateContext(emulationGl->mEglDisplay,
                                                      emulationGl->mEglConfig,
                                                      EGL_NO_CONTEXT,
                                                      maxContextAttribs);
    if (emulationGl->mEglContext == EGL_NO_CONTEXT) {
        GFXSTREAM_ERROR("Failed to create context, error 0x%x.", s_egl.eglGetError());
        return nullptr;
    }

    // Create another context which shares with the default context to be
    // used when we bind the pbuffer. This prevents switching the drawable
    // binding back and forth on the framebuffer context.
    // The main purpose of it is to solve a "blanking" behaviour we see on
    // on Mac platform when switching binded drawable for a context however
    // it is more efficient on other platforms as well.
    auto pbufferSurfaceGl = DisplaySurfaceGl::createPbufferSurface(emulationGl->mEglDisplay,
                                                                   emulationGl->mEglConfig,
                                                                   emulationGl->mEglContext,
                                                                   maxContextAttribs,
                                                                   /*width=*/1,
                                                                   /*height=*/1);
    if (!pbufferSurfaceGl) {
        GFXSTREAM_ERROR("Failed to create pbuffer display surface.");
        return nullptr;
    }
    auto* pbufferSurfaceGlPtr = pbufferSurfaceGl.get();

    emulationGl->mPbufferSurface = std::make_unique<DisplaySurface>(
        /*width=*/1,
        /*height=*/1,
        std::move(pbufferSurfaceGl));

    RecursiveScopedContextBind contextBind(pbufferSurfaceGlPtr->getContextHelper());
    if (!contextBind.isOk()) {
        GFXSTREAM_ERROR("Failed to make pbuffer context and surface current");
        return nullptr;
    }

#ifdef ENABLE_GFXSTREAM_DEBUG
    bool debugSetup = false;
    if (s_gles2.glDebugMessageCallback) {
        s_gles2.glEnable(GL_DEBUG_OUTPUT);
        s_gles2.glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        s_gles2.glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                                      GL_DEBUG_SEVERITY_HIGH, 0, nullptr, GL_TRUE);
        s_gles2.glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                                      GL_DEBUG_SEVERITY_MEDIUM, 0, nullptr, GL_TRUE);
        s_gles2.glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                                      GL_DEBUG_SEVERITY_LOW, 0, nullptr, GL_TRUE);
        s_gles2.glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                                      GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr,
                                      GL_TRUE);
        s_gles2.glDebugMessageCallback(&GlDebugCallback, nullptr);
        debugSetup = s_gles2.glGetError() == GL_NO_ERROR;
        if (!debugSetup) {
            GFXSTREAM_ERROR("Failed to set up glDebugMessageCallback");
        } else {
            GFXSTREAM_DEBUG("Successfully set up glDebugMessageCallback");
        }
    }
    if (s_gles2.glDebugMessageCallbackKHR && !debugSetup) {
        s_gles2.glDebugMessageControlKHR(GL_DONT_CARE, GL_DONT_CARE,
                                         GL_DEBUG_SEVERITY_HIGH_KHR, 0, nullptr,
                                         GL_TRUE);
        s_gles2.glDebugMessageControlKHR(GL_DONT_CARE, GL_DONT_CARE,
                                         GL_DEBUG_SEVERITY_MEDIUM_KHR, 0, nullptr,
                                         GL_TRUE);
        s_gles2.glDebugMessageControlKHR(GL_DONT_CARE, GL_DONT_CARE,
                                         GL_DEBUG_SEVERITY_LOW_KHR, 0, nullptr,
                                         GL_TRUE);
        s_gles2.glDebugMessageControlKHR(GL_DONT_CARE, GL_DONT_CARE,
                                         GL_DEBUG_SEVERITY_NOTIFICATION_KHR, 0, nullptr,
                                         GL_TRUE);
        s_gles2.glDebugMessageCallbackKHR(&GlDebugCallback, nullptr);
        debugSetup = s_gles2.glGetError() == GL_NO_ERROR;
        if (!debugSetup) {
            GFXSTREAM_ERROR("Failed to set up glDebugMessageCallbackKHR");
        } else {
            GFXSTREAM_DEBUG("Successfully set up glDebugMessageCallbackKHR");
        }
    }
    if (!debugSetup) {
        GFXSTREAM_DEBUG("glDebugMessageCallback and glDebugMessageCallbackKHR not available");
    }
#endif

    emulationGl->mGlesVendor = (const char*)s_gles2.glGetString(GL_VENDOR);
    emulationGl->mGlesRenderer = (const char*)s_gles2.glGetString(GL_RENDERER);
    emulationGl->mGlesVersion = (const char*)s_gles2.glGetString(GL_VERSION);
    emulationGl->mGlesExtensions = (const char*)s_gles2.glGetString(GL_EXTENSIONS);

    emulationGl->mEmulatedEglConfigs = std::make_unique<EmulatedEglConfigList>(
        emulationGl->mEglDisplay, emulationGl->mGlesDispatchMaxVersion, emulationGl->mFeatures,
        emulationGl->mGlesVendor);
    if (emulationGl->mEmulatedEglConfigs->empty()) {
        GFXSTREAM_ERROR("Failed to initialize emulated configs.");
        return nullptr;
    }

    const bool hasEsOrEs2Context =
        std::any_of(emulationGl->mEmulatedEglConfigs->begin(),
                    emulationGl->mEmulatedEglConfigs->end(), [](const EmulatedEglConfig& config) {
                        const GLint renderableType = config.getRenderableType();
                        return renderableType & (EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT);
                    });
    if (!hasEsOrEs2Context) {
        GFXSTREAM_ERROR("Failed to find any usable guest EGL configs.");
        return nullptr;
    }

    s_gles2.glGetError();
    GLint numDeviceUuids = 0;
    s_gles2.glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &numDeviceUuids);
    if (numDeviceUuids == 1) {
        GlesUuid uuid{};
        s_gles2.glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, uuid.data());
        emulationGl->mGlesDeviceUuid = uuid;
    }

    emulationGl->mGlesVulkanInteropSupported = false;
    if (s_egl.eglQueryVulkanInteropSupportANDROID) {
        emulationGl->mGlesVulkanInteropSupported = s_egl.eglQueryVulkanInteropSupportANDROID();
    }
    if (emulationGl->mGlesVulkanInteropSupported) {
        // Intel: b/271028352 workaround
        const std::vector<const char*> disallowList = {"Intel",
#ifdef _WIN32
                                                       "AMD Radeon Pro WX 3200"
#endif
        };
        const std::string& glesRenderer = emulationGl->getGlesRenderer();
        for (const auto& disallowed : disallowList) {
            if (strstr(glesRenderer.c_str(), disallowed)) {
                emulationGl->mGlesVulkanInteropSupported = false;
                break;
            }
        }
    }

    emulationGl->mTextureDraw = std::make_unique<TextureDraw>();
    if (!emulationGl->mTextureDraw) {
        GFXSTREAM_ERROR("Failed to initialize TextureDraw.");
        return nullptr;
    }

    emulationGl->mCompositorGl = std::make_unique<CompositorGl>(emulationGl->mTextureDraw.get());

    emulationGl->mDisplayGl = std::make_unique<DisplayGl>(emulationGl->mTextureDraw.get());

    {
        auto surface1 = DisplaySurfaceGl::createPbufferSurface(emulationGl->mEglDisplay,
                                                               emulationGl->mEglConfig,
                                                               emulationGl->mEglContext,
                                                               getGlesMaxContextAttribs(),
                                                               /*width=*/1,
                                                               /*height=*/1);
        if (!surface1) {
            GFXSTREAM_ERROR("Failed to create pbuffer surface for ReadbackWorkerGl.");
            return nullptr;
        }

        auto surface2 = DisplaySurfaceGl::createPbufferSurface(emulationGl->mEglDisplay,
                                                               emulationGl->mEglConfig,
                                                               emulationGl->mEglContext,
                                                               getGlesMaxContextAttribs(),
                                                               /*width=*/1,
                                                               /*height=*/1);
        if (!surface2) {
            GFXSTREAM_ERROR("Failed to create pbuffer surface for ReadbackWorkerGl.");
            return nullptr;
        }

        emulationGl->mReadbackWorkerGl = std::make_unique<ReadbackWorkerGl>(std::move(surface1),
                                                                            std::move(surface2));
    }

    return emulationGl;
}

EmulationGl::~EmulationGl() {
    if (mPbufferSurface) {
        const auto* displaySurfaceGl =
            reinterpret_cast<const DisplaySurfaceGl*>(mPbufferSurface->getImpl());

        RecursiveScopedContextBind contextBind(displaySurfaceGl->getContextHelper());
        if (contextBind.isOk()) {
            mTextureDraw.reset();
        } else {
            GFXSTREAM_ERROR("Failed to bind context for destroying TextureDraw.");
        }
    }

    for (auto it : mPlatformEglContexts) {
        destroySharedTrivialContext(it.second.context, it.second.surface);
    }

    if (mEglDisplay != EGL_NO_DISPLAY) {
        s_egl.eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mEglContext != EGL_NO_CONTEXT) {
            s_egl.eglDestroyContext(mEglDisplay, mEglContext);
            mEglContext = EGL_NO_CONTEXT;
        }
        mEglDisplay = EGL_NO_DISPLAY;
    }
}

std::unique_ptr<DisplaySurface> EmulationGl::createFakeWindowSurface() {
    return std::make_unique<DisplaySurface>(
        mWidth, mHeight,
        DisplaySurfaceGl::createPbufferSurface(
            mEglDisplay, mEglConfig, mEglContext, getGlesMaxContextAttribs(), mWidth, mHeight));
}

/*static*/ const GLint* EmulationGl::getGlesMaxContextAttribs() {
    int glesMaj, glesMin;
    get_gfxstream_gles_version(&glesMaj, &glesMin);
    if (shouldEnableCoreProfile()) {
        if (glesMaj == 2) {
            return kGles2ContextAttribsCoreGL;
        } else {
            return kGles3ContextAttribsCoreGL;
        }
    }
    if (glesMaj == 2) {
        return kGles2ContextAttribsESOrGLCompat;
    } else {
        return kGles3ContextAttribsESOrGLCompat;
    }
}

const EGLDispatch* EmulationGl::getEglDispatch() {
    return &s_egl;
}

const GLESv2Dispatch* EmulationGl::getGles2Dispatch() {
    return &s_gles2;
}

std::string EmulationGl::getEglString(EGLenum name) {
    const char* str = s_egl.eglQueryString(mEglDisplay, name);
    if (!str) {
        return "";
    }

    std::string eglStr(str);
    if ((mGlesDispatchMaxVersion >= GLES_DISPATCH_MAX_VERSION_3_0) &&
        mFeatures.GlesDynamicVersion.enabled() &&
        eglStr.find("EGL_KHR_create_context") == std::string::npos) {
        eglStr += "EGL_KHR_create_context ";
    }

    return eglStr;
}

std::string EmulationGl::getGlString(EGLenum name) {
    std::string str;

    RenderThreadInfoGl* const tInfo = RenderThreadInfoGl::get();
    if (tInfo && tInfo->currContext.get()) {
        if (tInfo->currContext->clientVersion() > GLESApi_CM) {
            str = (const char*)s_gles2.glGetString(name);
        } else {
            str = (const char*)s_gles1.glGetString(name);
        }
    }

    // Filter extensions by name to match guest-side support
    if (name == GL_EXTENSIONS) {
        str = gl::filterExtensionsBasedOnMaxVersion(mFeatures, mGlesDispatchMaxVersion, str);
    }

    return str;
}

GLESDispatchMaxVersion EmulationGl::getGlesMaxDispatchVersion() const {
    return mGlesDispatchMaxVersion;
}

bool EmulationGl::hasEglExtension(const std::string& ext) const {
    return mEglExtensions.find(ext) != mEglExtensions.end();
}

void EmulationGl::getEglVersion(EGLint* major, EGLint* minor) const {
    if (major) {
        *major = mEglVersionMajor;
    }
    if (minor) {
        *minor = mEglVersionMinor;
    }
}

void EmulationGl::getGlesVersion(GLint* major, GLint* minor) const {
    if (major) {
        *major = mGlesVersionMajor;
    }
    if (minor) {
        *minor = mGlesVersionMinor;
    }
}

bool EmulationGl::isMesa() const { return mGlesVersion.find("Mesa") != std::string::npos; }

bool EmulationGl::isFastBlitSupported() const {
    return mFastBlitSupported;
}

void EmulationGl::disableFastBlitForTesting() {
    mFastBlitSupported = false;
}

bool EmulationGl::isAsyncReadbackSupported() const {
    return mGlesVersionMajor > 2;
}

std::unique_ptr<DisplaySurface> EmulationGl::createWindowSurface(
        uint32_t width,
        uint32_t height,
        EGLNativeWindowType window) {
    auto surfaceGl = DisplaySurfaceGl::createWindowSurface(mEglDisplay,
                                                           mEglConfig,
                                                           mEglContext,
                                                           getGlesMaxContextAttribs(),
                                                           window);
    if (!surfaceGl) {
        GFXSTREAM_ERROR("Failed to create DisplaySurfaceGl.");
        return nullptr;
    }

    return std::make_unique<DisplaySurface>(width,
                                            height,
                                            std::move(surfaceGl));
}

ContextHelper* EmulationGl::getColorBufferContextHelper() {
    if (!mPbufferSurface) {
        return nullptr;
    }

    const auto* surfaceGl = static_cast<const DisplaySurfaceGl*>(mPbufferSurface->getImpl());
    return surfaceGl->getContextHelper();
}

ContextHelper* EmulationGl::getPbufferSurfaceContextHelper() const {
    if (!mPbufferSurface) {
        GFXSTREAM_FATAL("EGL emulation pbuffer surface not available.");
    }
    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(mPbufferSurface->getImpl());

    return displaySurfaceGl->getContextHelper();
}

std::unique_ptr<BufferGl> EmulationGl::createBuffer(uint64_t size, HandleType handle) {
    return BufferGl::create(size, handle, getColorBufferContextHelper());
}

std::unique_ptr<BufferGl> EmulationGl::loadBuffer(gfxstream::Stream* stream) {
    return BufferGl::onLoad(stream, getColorBufferContextHelper());
}

bool EmulationGl::isFormatSupported(GfxstreamFormat format) {
    const std::vector<GfxstreamFormat> kUnhandledFormats = {
        GfxstreamFormat::D16_UNORM,
        GfxstreamFormat::D24_UNORM,
        GfxstreamFormat::D24_UNORM_S8_UINT,
        GfxstreamFormat::D32_FLOAT,
        GfxstreamFormat::D32_FLOAT_S8_UINT,
    };

    if (std::find(kUnhandledFormats.begin(), kUnhandledFormats.end(), format) !=
            kUnhandledFormats.end()) {
        return false;
    }
    // TODO(b/356603558): add proper GL querying, for now preserve existing assumption
    return true;
}

std::unique_ptr<ColorBufferGl> EmulationGl::createColorBuffer(uint32_t width, uint32_t height,
                                                              GfxstreamFormat format,
                                                              HandleType handle) {
    return ColorBufferGl::create(mEglDisplay, width, height, format,
                                 handle, getColorBufferContextHelper(), mTextureDraw.get(),
                                 isFastBlitSupported(), mFeatures, mPixelReadFormats);
}

std::unique_ptr<ColorBufferGl> EmulationGl::loadColorBuffer(gfxstream::Stream* stream) {
    return ColorBufferGl::onLoad(stream, mEglDisplay, getColorBufferContextHelper(),
                                 mTextureDraw.get(), isFastBlitSupported(), mFeatures,
                                 mPixelReadFormats);
}

std::unique_ptr<EmulatedEglContext> EmulationGl::createEmulatedEglContextImpl(
    uint32_t emulatedEglConfigIndex, const EmulatedEglContext* sharedContext, GLESApi api,
    HandleType handle) {
    if (!mEmulatedEglConfigs) {
        GFXSTREAM_ERROR("EmulatedEglConfigs unavailable.");
        return nullptr;
    }

    const EmulatedEglConfig* emulatedEglConfig = mEmulatedEglConfigs->get(emulatedEglConfigIndex);
    if (!emulatedEglConfig) {
        GFXSTREAM_ERROR("Failed to find emulated EGL config %d", emulatedEglConfigIndex);
        return nullptr;
    }

    EGLConfig config = emulatedEglConfig->getHostEglConfig();
    EGLContext share = sharedContext ? sharedContext->getEGLContext() : EGL_NO_CONTEXT;

    return EmulatedEglContext::create(mEglDisplay, config, share, handle, api);
}

std::unique_ptr<EmulatedEglContext> EmulationGl::loadEmulatedEglContext(
        gfxstream::Stream* stream) {
    return EmulatedEglContext::onLoad(stream, mEglDisplay);
}

uint64_t EmulationGl::createEmulatedEglFenceSync(EGLenum type, int destroyWhenSignaled) {
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

    const bool hasNativeFence = type == EGL_SYNC_NATIVE_FENCE_ANDROID;
    auto sync = EmulatedEglFenceSync::create(mEglDisplay, hasNativeFence, destroyWhenSignaled);
    return sync ? (uint64_t)(uintptr_t)sync.release() : 0;
}

HandleType EmulationGl::createEmulatedEglImage(HandleType contextHandle, EGLenum target,
                                               EGLClientBuffer buffer) {
    EGLContext eglContext = EGL_NO_CONTEXT;
    if (contextHandle) {
        auto it = mContexts.find(contextHandle);
        if (it != mContexts.end()) {
            eglContext = it->second->getEGLContext();
        } else {
            GFXSTREAM_ERROR("Failed to find EmulatedEglContext:%d", contextHandle);
            return 0;
        }
    }
    auto image = EmulatedEglImage::create(mEglDisplay, eglContext, target, buffer);
    if (!image) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglImage");
        return 0;
    }

    HandleType imageHandle = image->getHandle();
    mImages[imageHandle] = std::move(image);

    RenderThreadInfoGl* tInfo = RenderThreadInfoGl::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        mProcOwnedEmulatedEglImages[puid].insert(imageHandle);
    }
    return imageHandle;
}

bool EmulationGl::destroyEmulatedEglImage(HandleType imageHandle) {
    auto imageIt = mImages.find(imageHandle);
    if (imageIt == mImages.end()) {
        GFXSTREAM_ERROR("Failed to find EmulatedEglImage:%d", imageHandle);
        return false;
    }
    auto& image = imageIt->second;

    EGLBoolean success = image->destroy();
    mImages.erase(imageIt);

    RenderThreadInfoGl* tInfo = RenderThreadInfoGl::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        mProcOwnedEmulatedEglImages[puid].erase(imageHandle);
    }
    return (success == EGL_TRUE);
}

std::unique_ptr<EmulatedEglWindowSurface> EmulationGl::createEmulatedEglWindowSurfaceImpl(
    uint32_t emulatedConfigIndex, uint32_t width, uint32_t height, HandleType handle) {
    if (!mEmulatedEglConfigs) {
        GFXSTREAM_ERROR("EmulatedEglConfigs unavailable.");
        return nullptr;
    }

    const EmulatedEglConfig* emulatedEglConfig = mEmulatedEglConfigs->get(emulatedConfigIndex);
    if (!emulatedEglConfig) {
        GFXSTREAM_ERROR("Failed to find emulated EGL config %d", emulatedConfigIndex);
        return nullptr;
    }

    EGLConfig config = emulatedEglConfig->getHostEglConfig();

    return EmulatedEglWindowSurface::create(mEglDisplay, config, width, height, handle);
}

std::unique_ptr<EmulatedEglWindowSurface> EmulationGl::loadEmulatedEglWindowSurface(
    gfxstream::Stream* stream, const std::function<IColorBufferRef(uint32_t)>& colorBufferLookup,
    const EmulatedEglContextMap& contexts) {
    return EmulatedEglWindowSurface::onLoad(stream, mEglDisplay, colorBufferLookup, contexts);
}

HandleType EmulationGl::createEmulatedEglContext(uint32_t emulatedConfigIndex,
                                                 HandleType shareContextHandle, GLESApi api) {
    gfxstream::base::AutoWriteLock contextLock(mContextStructureLock);
    HandleType handle = mGlobalState->genHandleLocked();

    EmulatedEglContextPtr shareContext = nullptr;
    if (shareContextHandle != 0) {
        auto shareContextIt = mContexts.find(shareContextHandle);
        if (shareContextIt == mContexts.end()) {
            GFXSTREAM_ERROR("Failed to find share EmulatedEglContext:%d", shareContextHandle);
            return 0;
        }
        shareContext = shareContextIt->second;
    }

    auto context =
        createEmulatedEglContextImpl(emulatedConfigIndex, shareContext.get(), api, handle);
    if (!context) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglContext.");
        return 0;
    }

    mContexts[handle] = std::move(context);

    RenderThreadInfoGl* tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadInfoGl not available.");
    }
    uint64_t puid = tinfo->m_puid;
    if (puid) {
        mProcOwnedEmulatedEglContexts[puid].insert(handle);
    } else {
        tinfo->m_contextSet.insert(handle);
    }
    return handle;
}

void EmulationGl::destroyEmulatedEglContext(HandleType contextHandle) {
    gfxstream::base::AutoWriteLock contextLock(mContextStructureLock);
    auto it = mContexts.find(contextHandle);
    if (it == mContexts.end()) {
        GFXSTREAM_ERROR("Failed to find EmulatedEglContext:%d", contextHandle);
        return;
    }
    mContexts.erase(it);

    RenderThreadInfoGl* tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadInfoGl not available.");
    }
    uint64_t puid = tinfo->m_puid;
    if (puid) {
        auto procIte = mProcOwnedEmulatedEglContexts.find(puid);
        if (procIte != mProcOwnedEmulatedEglContexts.end()) {
            procIte->second.erase(contextHandle);
        }
    } else {
        tinfo->m_contextSet.erase(contextHandle);
    }
}

HandleType EmulationGl::createEmulatedEglWindowSurface(uint32_t emulatedConfigIndex, uint32_t width,
                                                       uint32_t height) {
    HandleType handle = mGlobalState->genHandleLocked();
    auto window = createEmulatedEglWindowSurfaceImpl(emulatedConfigIndex, width, height, handle);
    if (!window) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglWindowSurface.");
        return 0;
    }

    mWindows[handle] = {std::move(window), 0};

    RenderThreadInfoGl* info = RenderThreadInfoGl::get();
    if (!info) {
        GFXSTREAM_FATAL("RenderThreadInfoGl not available.");
    }

    uint64_t puid = info->m_puid;
    if (puid) {
        mProcOwnedEmulatedEglWindowSurfaces[puid].insert(handle);
    } else {
        info->m_windowSet.insert(handle);
    }

    return handle;
}

std::vector<HandleType> EmulationGl::destroyEmulatedEglWindowSurface(HandleType surfaceHandle) {
    std::vector<HandleType> colorBuffersToCleanUp;
    const auto w = mWindows.find(surfaceHandle);
    if (w != mWindows.end()) {
        RecursiveScopedContextBind bind(getColorBufferContextHelper());
        if (w->second.second != 0) {
            colorBuffersToCleanUp.push_back(w->second.second);
        }
        mWindows.erase(w);
        RenderThreadInfoGl* tinfo = RenderThreadInfoGl::get();
        if (!tinfo) {
            GFXSTREAM_FATAL("RenderThreadInfoGl not available.");
        }
        uint64_t puid = tinfo->m_puid;
        if (puid) {
            auto ite = mProcOwnedEmulatedEglWindowSurfaces.find(puid);
            if (ite != mProcOwnedEmulatedEglWindowSurfaces.end()) {
                ite->second.erase(surfaceHandle);
            }
        } else {
            tinfo->m_windowSet.erase(surfaceHandle);
        }
    }
    return colorBuffersToCleanUp;
}

bool EmulationGl::isHandleInUse(HandleType handle) const {
    return mContexts.find(handle) != mContexts.end() || mWindows.find(handle) != mWindows.end();
}

EmulatedEglContextPtr EmulationGl::getContext(HandleType contextHandle) {
    return gfxstream::base::findOrDefault(mContexts, contextHandle);
}

EmulatedEglWindowSurfacePtr EmulationGl::getWindowSurface(HandleType surfaceHandle) {
    return gfxstream::base::findOrDefault(mWindows, surfaceHandle).first;
}

bool EmulationGl::bindColorBufferToTexture(IColorBuffer* colorBuffer) {
    if (!colorBuffer) {
        GFXSTREAM_ERROR("Failed to bind to texture: invalid color buffer.");
        return false;
    }
    colorBuffer->touch();

    auto colorBufferGl = colorBuffer->getColorBufferGl();
    if (!colorBufferGl) {
        GFXSTREAM_ERROR("Failed to bind to texture: invalid color buffer gl.");
        return false;
    }
    return colorBufferGl->bindToTexture();
}

bool EmulationGl::bindColorBufferToTexture2(IColorBuffer* colorBuffer) {
    if (!colorBuffer) {
        GFXSTREAM_ERROR("Failed to bind to texture: invalid color buffer.");
        return false;
    }
    colorBuffer->touch();

    auto colorBufferGl = colorBuffer->getColorBufferGl();
    if (!colorBufferGl) {
        GFXSTREAM_ERROR("Failed to bind to texture: invalid color buffer gl.");
        return false;
    }
    return colorBufferGl->bindToTexture2();
}

bool EmulationGl::bindColorBufferToRenderbuffer(IColorBuffer* colorBuffer) {
    if (!colorBuffer) {
        GFXSTREAM_ERROR("Failed to bind to renderbuffer: invalid color buffer.");
        return false;
    }
    colorBuffer->touch();

    auto colorBufferGl = colorBuffer->getColorBufferGl();
    if (!colorBufferGl) {
        GFXSTREAM_ERROR("Failed to bind to renderbuffer: invalid color buffer gl.");
        return false;
    }
    return colorBufferGl->bindToRenderbuffer();
}

bool EmulationGl::bindContext(HandleType contextHandle, HandleType drawSurfaceHandle,
                              HandleType readSurfaceHandle) {
    EmulatedEglWindowSurfacePtr draw = nullptr;
    EmulatedEglWindowSurfacePtr read = nullptr;
    EmulatedEglContextPtr ctx = nullptr;

    if (contextHandle || drawSurfaceHandle || readSurfaceHandle) {
        ctx = getContext(contextHandle);
        if (!ctx) return false;

        auto drawWindowIt = mWindows.find(drawSurfaceHandle);
        if (drawWindowIt == mWindows.end()) {
            return false;
        }
        draw = drawWindowIt->second.first;

        if (readSurfaceHandle != drawSurfaceHandle) {
            auto readWindowIt = mWindows.find(readSurfaceHandle);
            if (readWindowIt == mWindows.end()) {
                return false;
            }
            read = readWindowIt->second.first;
        } else {
            read = draw;
        }
    }

    if (!s_egl.eglMakeCurrent(mEglDisplay, draw ? draw->getEGLSurface() : EGL_NO_SURFACE,
                              read ? read->getEGLSurface() : EGL_NO_SURFACE,
                              ctx ? ctx->getEGLContext() : EGL_NO_CONTEXT)) {
        GFXSTREAM_ERROR("eglMakeCurrent failed");
        return false;
    }

    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGl not available.");
    }

    EmulatedEglWindowSurfacePtr bindDraw, bindRead;
    if (draw.get() == NULL && read.get() == NULL) {
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

    tinfo->currContext = ctx;
    tinfo->currDrawSurf = draw;
    tinfo->currReadSurf = read;
    if (ctx) {
        if (ctx->clientVersion() > GLESApi_CM) {
            tinfo->m_gl2Dec.setContextData(&ctx->decoderContextData());
            tinfo->m_glDec.setContextData(nullptr);
        } else {
            tinfo->m_glDec.setContextData(&ctx->decoderContextData());
            tinfo->m_gl2Dec.setContextData(nullptr);
        }
    } else {
        tinfo->m_glDec.setContextData(nullptr);
        tinfo->m_gl2Dec.setContextData(nullptr);
    }
    return true;
}

void EmulationGl::preSave(Stream* stream, const gfxstream::ITextureSaverPtr& textureSaver) {
    if (s_egl.eglPreSaveContext && s_egl.eglSaveAllImages) {
        for (const auto& ctx : mContexts) {
            s_egl.eglPreSaveContext(mEglDisplay, ctx.second->getEGLContext(), stream);
        }
        s_egl.eglSaveAllImages(mEglDisplay, stream, &textureSaver);
    }
}

void EmulationGl::saveContexts(Stream* stream) {
    saveCollection(stream, mContexts, [](Stream* s, const EmulatedEglContextMap::value_type& pair) {
        pair.second->onSave(s);
    });
}

void EmulationGl::saveWindowSurfaces(Stream* stream) {
    saveCollection(stream, mWindows,
                   [](Stream* s, const EmulatedEglWindowSurfaceMap::value_type& pair) {
                       pair.second.first->onSave(s);
                       s->putBe32(pair.second.second);
                   });
}

void EmulationGl::saveProcOwnedWindowSurfaces(Stream* stream) {
    saveProcOwnedCollection(stream, mProcOwnedEmulatedEglWindowSurfaces);
}

void EmulationGl::saveProcOwnedContexts(Stream* stream) {
    saveProcOwnedCollection(stream, mProcOwnedEmulatedEglContexts);
}

void EmulationGl::saveProcOwnedImages(Stream* stream) {
    saveProcOwnedCollection(stream, mProcOwnedEmulatedEglImages);
}

bool EmulationGl::loadContexts(Stream* stream) {
    bool loadOk = true;
    loadCollection(stream, &mContexts, [this, &loadOk](Stream* stream) -> EmulatedEglContextMap::value_type {
        auto context = loadEmulatedEglContext(stream);
        auto contextHandle = context ? context->getHndl() : 0;
        if (!context) {
            // Do not return, keep reading the stream
            loadOk = false;
        }
        return {contextHandle, std::move(context)};
    });
    if (!loadOk) {
        GFXSTREAM_ERROR("Could not load contexts");
        return false;
    }
    return true;
}

bool EmulationGl::loadWindowSurfaces(
    Stream* stream, const std::function<IColorBufferRef(uint32_t)>& colorBufferLookup) {
    bool loadOk = true;
    loadCollection(
        stream, &mWindows,
        [this, &colorBufferLookup, &loadOk](Stream* stream) -> EmulatedEglWindowSurfaceMap::value_type {
            auto window = loadEmulatedEglWindowSurface(stream, colorBufferLookup, mContexts);
            HandleType colorBufferHandle = stream->getBe32();
            if (!window) {
                // Do not return, keep reading the stream
                loadOk = false;
            }

            HandleType handle = window ? window->getHndl() : 0;
            return {handle, {std::move(window), colorBufferHandle}};
        });
    if (!loadOk) {
        GFXSTREAM_ERROR("Could not load window surfaces");
        return false;
    }
    return true;
}

void EmulationGl::loadProcOwnedWindowSurfaces(Stream* stream) {
    loadProcOwnedCollection(stream, &mProcOwnedEmulatedEglWindowSurfaces);
}

void EmulationGl::loadProcOwnedContexts(Stream* stream) {
    loadProcOwnedCollection(stream, &mProcOwnedEmulatedEglContexts);
}

void EmulationGl::loadProcOwnedImages(Stream* stream) {
    loadProcOwnedCollection(stream, &mProcOwnedEmulatedEglImages);
}

void EmulationGl::loadAllImages(Stream* stream, const gfxstream::ITextureLoaderPtr& textureLoader) {
    if (s_egl.eglLoadAllImages) {
        s_egl.eglLoadAllImages(mEglDisplay, stream, &textureLoader);
    }
}

void EmulationGl::postLoad(Stream* stream) {
    if (s_egl.eglPostLoadAllImages) {
        s_egl.eglPostLoadAllImages(mEglDisplay, stream);
    }
}

bool EmulationGl::bindColorBufferToWindowSurface(HandleType surfaceHandle,
                                                 HandleType colorBufferHandle,
                                                 HandleType* outOldColorBufferHandle) {
    auto w = mWindows.find(surfaceHandle);
    if (w == mWindows.end()) {
        return false;
    }

    IColorBufferRef cb = nullptr;
    if (colorBufferHandle) {
        cb = mGlobalState->findColorBuffer(colorBufferHandle);
        if (!cb) {
            GFXSTREAM_ERROR("bad color buffer handle %d", colorBufferHandle);
            return false;
        }
    }

    w->second.first->setColorBuffer(cb);
    *outOldColorBufferHandle = w->second.second;
    w->second.second = colorBufferHandle;
    return true;
}

HandleType EmulationGl::getWindowSurfaceColorBufferHandle(HandleType surfaceHandle) const {
    auto it = mWindows.find(surfaceHandle);
    if (it == mWindows.end()) {
        return 0;
    }
    return it->second.second;
}

std::vector<HandleType> EmulationGl::cleanupProcGLObjects(uint64_t puid) {
    std::vector<HandleType> colorBuffersToCleanUp;
    {
        RecursiveScopedContextBind bind(getColorBufferContextHelper());

        // Clean up window surfaces
        auto procWindowsIt = mProcOwnedEmulatedEglWindowSurfaces.find(puid);
        if (procWindowsIt != mProcOwnedEmulatedEglWindowSurfaces.end()) {
            for (auto whndl : procWindowsIt->second) {
                auto w = mWindows.find(whndl);
                if (w != mWindows.end()) {
                    if (w->second.second != 0) {
                        colorBuffersToCleanUp.push_back(w->second.second);
                    }
                    mWindows.erase(w);
                }
            }
            mProcOwnedEmulatedEglWindowSurfaces.erase(procWindowsIt);
        }

        // Cleanup EGLImages
        auto procImagesIt = mProcOwnedEmulatedEglImages.find(puid);
        if (procImagesIt != mProcOwnedEmulatedEglImages.end()) {
            for (auto image : procImagesIt->second) {
                mImages.erase(image);
            }
            mProcOwnedEmulatedEglImages.erase(procImagesIt);
        }
    }

    // Unbind before cleaning up contexts
    // Cleanup render contexts
    {
        gfxstream::base::AutoWriteLock contextLock(mContextStructureLock);
        auto procContextsIt = mProcOwnedEmulatedEglContexts.find(puid);
        if (procContextsIt != mProcOwnedEmulatedEglContexts.end()) {
            for (auto ctx : procContextsIt->second) {
                mContexts.erase(ctx);
            }
            mProcOwnedEmulatedEglContexts.erase(procContextsIt);
        }
    }
    return colorBuffersToCleanUp;
}

void EmulationGl::postSave(Stream* stream) {
    if (s_egl.eglPostSaveContext) {
        for (const auto& ctx : mContexts) {
            s_egl.eglPostSaveContext(mEglDisplay, ctx.second->getEGLContext(), stream);
        }
        if (mEglContext != EGL_NO_CONTEXT) {
            s_egl.eglPostSaveContext(mEglDisplay, mEglContext, stream);
        }
    }
}

bool EmulationGl::hasContextsOrWindowSurfaces() const {
    return !mContexts.empty() || !mWindows.empty();
}

void EmulationGl::clearContextsAndWindowSurfaces() {
    mContexts.clear();
    mWindows.clear();
}

bool EmulationGl::hasProcOwnedResources() const {
    return !mProcOwnedEmulatedEglContexts.empty() || !mProcOwnedEmulatedEglWindowSurfaces.empty() ||
           !mProcOwnedEmulatedEglImages.empty();
}

std::vector<uint64_t> EmulationGl::getGLPUIDs() const {
    std::vector<uint64_t> puids;
    for (const auto& pair : mProcOwnedEmulatedEglContexts) {
        puids.push_back(pair.first);
    }
    for (const auto& pair : mProcOwnedEmulatedEglWindowSurfaces) {
        if (std::find(puids.begin(), puids.end(), pair.first) == puids.end()) {
            puids.push_back(pair.first);
        }
    }
    for (const auto& pair : mProcOwnedEmulatedEglImages) {
        if (std::find(puids.begin(), puids.end(), pair.first) == puids.end()) {
            puids.push_back(pair.first);
        }
    }
    return puids;
}

void EmulationGl::drainRenderThreadContexts() {
    gfxstream::base::AutoWriteLock contextLock(mContextStructureLock);
    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }
    for (const HandleType contextHandle : tinfo->m_contextSet) {
        mContexts.erase(contextHandle);
    }
    tinfo->m_contextSet.clear();
}

void EmulationGl::drainRenderThreadSurfaces() {
    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }
    RecursiveScopedContextBind bind(getColorBufferContextHelper());
    for (const HandleType winHandle : tinfo->m_windowSet) {
        const auto winIt = mWindows.find(winHandle);
        if (winIt != mWindows.end()) {
            if (winIt->second.second != 0) {
                mGlobalState->closeColorBufferByWindow(winIt->second.second);
            }
            mWindows.erase(winIt);
        }
    }
    tinfo->m_windowSet.clear();
}

bool EmulationGl::flushEmulatedEglWindowSurfaceColorBuffer(HandleType surfaceHandle) {
    auto it = mWindows.find(surfaceHandle);
    if (it == mWindows.end()) {
        GFXSTREAM_ERROR("flushEmulatedEglWindowSurfaceColorBuffer: window handle %#x not found",
                        surfaceHandle);
        return false;
    }
    it->second.first->flushColorBuffer();
    return true;
}

void EmulationGl::createTrivialContext(HandleType shared, HandleType* contextOut,
                                       HandleType* surfOut) {
    assert(contextOut);
    assert(surfOut);

    *contextOut = createEmulatedEglContext(0, shared, GLESApi_2);
    *surfOut = createEmulatedEglWindowSurface(0, 1, 1);
}

void EmulationGl::createSharedTrivialContext(EGLContext* contextOut, EGLSurface* surfOut) {
    assert(contextOut);
    assert(surfOut);

    if (mEglConfig == EGL_NO_CONFIG) {
        GFXSTREAM_FATAL("GL/EGL emulation has not chosen a config.");
    }

    int maj, min;
    get_gfxstream_gles_version(&maj, &min);

    const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, maj,
                                     EGL_CONTEXT_MINOR_VERSION_KHR, min, EGL_NONE};

    *contextOut = s_egl.eglCreateContext(mEglDisplay, mEglConfig, mEglContext, contextAttribs);

    const EGLint pbufAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

    *surfOut = s_egl.eglCreatePbufferSurface(mEglDisplay, mEglConfig, pbufAttribs);
}

void EmulationGl::destroySharedTrivialContext(EGLContext context, EGLSurface surface) {
    if (mEglDisplay != EGL_NO_DISPLAY) {
        s_egl.eglDestroyContext(mEglDisplay, context);
        s_egl.eglDestroySurface(mEglDisplay, surface);
    }
}

bool EmulationGl::setEmulatedEglWindowSurfaceColorBuffer(HandleType surfaceHandle,
                                                         HandleType colorBufferHandle) {
    HandleType oldColorBuffer = 0;
    if (!bindColorBufferToWindowSurface(surfaceHandle, colorBufferHandle, &oldColorBuffer)) {
        return false;
    }

    if (colorBufferHandle) {
        mGlobalState->openColorBufferByWindow(colorBufferHandle);
    }

    if (oldColorBuffer) {
        mGlobalState->closeColorBufferByWindow(oldColorBuffer);
    }

    return true;
}

void* EmulationGl::platformCreateSharedEglContext() {
    EGLContext context = 0;
    EGLSurface surface = 0;
    createSharedTrivialContext(&context, &surface);

    void* underlyingContext = s_egl.eglGetNativeContextANDROID(mEglDisplay, context);
    if (!underlyingContext) {
        GFXSTREAM_ERROR("Error: Underlying egl backend could not produce a native EGL context.");
        return nullptr;
    }

    mPlatformEglContexts[underlyingContext] = {context, surface};

#if defined(__QNX__)
    EGLDisplay currDisplay = eglGetCurrentDisplay();
    EGLSurface currRead = eglGetCurrentSurface(EGL_READ);
    EGLSurface currDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface currContext = eglGetCurrentContext();
    // Make this context current to ensure thread-state is initialized
    s_egl.eglMakeCurrent(mEglDisplay, surface, surface, context);
    // Revert back to original state
    s_egl.eglMakeCurrent(currDisplay, currRead, currDraw, currContext);
#endif

    return underlyingContext;
}

bool EmulationGl::platformDestroySharedEglContext(void* underlyingContext) {
    auto it = mPlatformEglContexts.find(underlyingContext);
    if (it == mPlatformEglContexts.end()) {
        GFXSTREAM_ERROR(
            "Error: Could not find underlying egl context %p (perhaps already destroyed?)",
            underlyingContext);
        return false;
    }

    destroySharedTrivialContext(it->second.context, it->second.surface);

    mPlatformEglContexts.erase(it);

    return true;
}

void EmulationGl::createYUVTextures(uint32_t type, uint32_t count, int width, int height,
                                    uint32_t* output) {
    auto formatOpt = GetGfxstreamFormat(mFeatures, static_cast<FrameworkFormat>(type));
    if (!formatOpt) {
        GFXSTREAM_ERROR("Unsupported framework format %d", type);
        return;
    }
    auto format = *formatOpt;

    auto contextHelper = getPbufferSurfaceContextHelper();
    if (!contextHelper) {
        // This should not be called in vulkan-only mode
        GFXSTREAM_ERROR("%s: invalid pbuffer surface context", __func__);
        return;
    }
    RecursiveScopedContextBind bind(contextHelper);
    if (!bind.isOk()) {
        GFXSTREAM_ERROR("%s: could not bind context helper", __func__);
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (format == GfxstreamFormat::NV12 || format == GfxstreamFormat::NV21) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height, format, YuvPlane::Y,
                                         &output[2 * i]);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2, format, YuvPlane::UV,
                                         &output[2 * i + 1]);
        } else if (format == GfxstreamFormat::YV12 || format == GfxstreamFormat::YV21) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height, format, YuvPlane::Y,
                                         &output[3 * i]);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2, format, YuvPlane::U,
                                         &output[3 * i + 1]);
            YUVConverter::createYUVGLTex(GL_TEXTURE2, width / 2, height / 2, format, YuvPlane::V,
                                         &output[3 * i + 2]);
        }
    }
}

void EmulationGl::destroyYUVTextures(uint32_t type, uint32_t count, uint32_t* textures) {
    auto formatOpt = GetGfxstreamFormat(mFeatures, static_cast<FrameworkFormat>(type));
    if (!formatOpt) {
        GFXSTREAM_ERROR("Unsupported framework format %d", type);
        return;
    }
    auto format = *formatOpt;

    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
    if (format == GfxstreamFormat::NV12 || format == GfxstreamFormat::NV21) {
        s_gles2.glDeleteTextures(2 * count, textures);
    } else if (format == GfxstreamFormat::YV12 || format == GfxstreamFormat::YV21) {
        s_gles2.glDeleteTextures(3 * count, textures);
    }
}

void EmulationGl::updateYUVTextures(uint32_t type, uint32_t* textures, void* privData, void* func) {
    auto formatOpt = GetGfxstreamFormat(mFeatures, static_cast<FrameworkFormat>(type));
    if (!formatOpt) {
        GFXSTREAM_ERROR("Unsupported framework format %d", type);
        return;
    }
    auto format = *formatOpt;

    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());

    yuv_updater_t updater = (yuv_updater_t)func;
    uint32_t gtextures[3] = {0, 0, 0};

    if (format == GfxstreamFormat::NV12 || format == GfxstreamFormat::NV21) {
        gtextures[0] = s_gles2.glGetGlobalTexName(textures[0]);
        gtextures[1] = s_gles2.glGetGlobalTexName(textures[1]);
    } else if (format == GfxstreamFormat::YV12 || format == GfxstreamFormat::YV21) {
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

void EmulationGl::drainRenderThreadResources() {
    bindContext(0, 0, 0);
    drainRenderThreadSurfaces();
    drainRenderThreadContexts();
    if (!s_egl.eglReleaseThread()) {
        GFXSTREAM_ERROR("Error: RenderThread failed to eglReleaseThread()");
    }
}

void EmulationGl::fillGlesUsages(android_studio::EmulatorGLESUsages* usages) {
    if (s_egl.eglFillUsages) {
        s_egl.eglFillUsages(usages);
    }
}

bool EmulationGl::getRenderOpt(gfxstream::RenderOpt* opt) const {
    if (!opt) {
        return false;
    }
    opt->display = mEglDisplay;
    opt->config = mEglConfig;

    if (!mWindowSurface) {
        opt->surface = EGL_NO_SURFACE;
    } else {
        const auto* displaySurfaceGl =
            reinterpret_cast<const DisplaySurfaceGl*>(mWindowSurface->getImpl());
        opt->surface = displaySurfaceGl->getSurface();
    }

    return (opt->display && opt->surface && opt->config);
}

EGLContext EmulationGl::getGlobalEGLContext() const {
    if (!mPbufferSurface) {
        GFXSTREAM_FATAL("FrameBuffer pbuffer surface not available.");
    }
    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(mPbufferSurface->getImpl());
    return displaySurfaceGl->getContextForShareContext();
}

void EmulationGl::swapTexturesAndUpdateColorBuffer(IColorBuffer* colorBuffer, uint32_t format,
                                                   uint32_t type, uint32_t texturesType,
                                                   uint32_t* textures) {
    auto texturesFormatOpt = GetGfxstreamFormat(mFeatures, (FrameworkFormat)texturesType);
    if (!texturesFormatOpt) {
        GFXSTREAM_ERROR("Unsupported framework format %d", texturesType);
        return;
    }
    auto texturesFormat = *texturesFormatOpt;

    ColorBufferGl* colorBufferGl = colorBuffer->getColorBufferGl();
    if (!colorBufferGl) {
        return;
    }

    colorBufferGl->swapYUVTextures(texturesFormat, textures);
    colorBufferGl->subUpdate(0, 0, colorBuffer->getWidth(), colorBuffer->getHeight(),
                             texturesFormat, nullptr);

    colorBuffer->flushFromBackend(Backend::GL);
}

}  // namespace gl
}  // namespace host
}  // namespace gfxstream
