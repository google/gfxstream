// Copyright (C) 2023 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gfxstream_end2end_tests.h"

#include <dlfcn.h>
#include <drm/drm_fourcc.h>

#include <cmath>
#include <filesystem>

#include "VirtGpu.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/common/testing/GraphicsTestEnvironment.h"
#include "gfxstream/image_utils.h"
#include "gfxstream/strings.h"
#include "gfxstream/system/System.h"
#include "test_data_utils.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace gfxstream {
namespace tests {
namespace {

using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;

}  // namespace

Image ImageFromColor(uint32_t w, uint32_t h, const PixelR8G8B8A8& pixel) {
    uint32_t rgba = 0;
    uint8_t* rgbaParts = reinterpret_cast<uint8_t*>(&rgba);
    rgbaParts[0] = pixel.r;
    rgbaParts[1] = pixel.g;
    rgbaParts[2] = pixel.b;
    rgbaParts[3] = pixel.a;

    Image ret;
    ret.width = w;
    ret.height = h;
    ret.pixels.resize(w * h, rgba);
    return ret;
}

std::string GfxstreamTransportToEnvVar(GfxstreamTransport transport) {
    switch (transport) {
        case GfxstreamTransport::kVirtioGpuAsg: {
            return "virtio-gpu-asg";
        }
        case GfxstreamTransport::kVirtioGpuPipe: {
            return "virtio-gpu-pipe";
        }
    }
}

std::string GfxstreamTransportToString(GfxstreamTransport transport) {
    switch (transport) {
        case GfxstreamTransport::kVirtioGpuAsg: {
            return "VirtioGpuAsg";
        }
        case GfxstreamTransport::kVirtioGpuPipe: {
            return "VirtioGpuPipe";
        }
    }
}

std::string TestParams::ToString() const {
    std::string ret;
    ret += (with_gl ? "With" : "Without");
    ret += "Gl";
    ret += (with_vk ? "With" : "Without");
    ret += "Vk";
    ret += "SampleCount" + std::to_string(samples);
    if (!with_features.empty()) {
        ret += "WithFeatures_";
        ret += Join(with_features, "_");
        ret += "_";
    }
    ret += "Over";
    ret += GfxstreamTransportToString(with_transport);
    return ret;
}

std::ostream& operator<<(std::ostream& os, const TestParams& params) {
    return os << params.ToString();
}

std::string GetTestName(const ::testing::TestParamInfo<TestParams>& info) {
    return info.param.ToString();
}

std::vector<TestParams> WithAndWithoutFeatures(const std::vector<TestParams>& params,
                                               const std::vector<std::string>& features) {
    std::vector<TestParams> output;
    output.reserve(params.size() * 2);

    // Copy of all of the existing test params:
    output.insert(output.end(), params.begin(), params.end());

    // Copy of all of the existing test params with the new features:
    for (TestParams copy : params) {
        copy.with_features.insert(features.begin(), features.end());
        output.push_back(copy);
    }

    return output;
}

std::unique_ptr<GuestGlDispatchTable> GfxstreamEnd2EndTest::SetupGuestGl() {
    const std::filesystem::path eglLibPath = GetTestDataPath("libEGL_emulation.so");
    const std::filesystem::path gles1LibPath = GetTestDataPath("libGLESv1_CM_emulation.so");
    const std::filesystem::path gles2LibPath = GetTestDataPath("libGLESv2_emulation.so");
    const std::string eglLibPathStr = eglLibPath.string();
    const std::string gles1LibPathStr = gles1LibPath.string();
    const std::string gles2LibPathStr = gles2LibPath.string();

    const std::string testdataDirectory = eglLibPath.parent_path().string();
    gfxstream::base::setEnvironmentVariable("GFXSTREAM_TESTDATA_PATH", testdataDirectory.c_str());

    void* eglLib = dlopen(eglLibPathStr.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!eglLib) {
        GFXSTREAM_ERROR("Failed to load Gfxstream EGL library from %s: %s.", eglLibPathStr.c_str(),
                        dlerror());
        return nullptr;
    }

    void* gles1Lib = dlopen(gles1LibPathStr.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!gles1Lib) {
        GFXSTREAM_ERROR("Failed to load Gfxstream GLES1 library from %s: %s.",
                        gles1LibPathStr.c_str(), dlerror());
        return nullptr;
    }

    void* gles2Lib = dlopen(gles2LibPathStr.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!gles2Lib) {
        GFXSTREAM_ERROR("Failed to load Gfxstream GLES2 library from %s: %s.",
                        gles2LibPathStr.c_str(), dlerror());
        return nullptr;
    }

    using GenericFnType = void*(void);
    using GetProcAddrType = GenericFnType*(const char*);

    auto eglGetAddr = reinterpret_cast<GetProcAddrType*>(dlsym(eglLib, "eglGetProcAddress"));
    if (!eglGetAddr) {
        GFXSTREAM_ERROR("Failed to load Gfxstream EGL library from %s: %s", eglLibPathStr.c_str(),
                        dlerror());
        return nullptr;
    }

    auto gl = std::make_unique<GuestGlDispatchTable>();

#define LOAD_EGL_FUNCTION(return_type, function_name, signature) \
    gl->function_name = reinterpret_cast<return_type(*) signature>(eglGetAddr(#function_name));

    LIST_RENDER_EGL_FUNCTIONS(LOAD_EGL_FUNCTION)
    LIST_RENDER_EGL_EXTENSIONS_FUNCTIONS(LOAD_EGL_FUNCTION)

#define LOAD_GLES2_FUNCTION(return_type, function_name, signature, callargs)         \
    gl->function_name =                                                              \
        reinterpret_cast<return_type(*) signature>(dlsym(gles2Lib, #function_name)); \
    if (!gl->function_name) {                                                        \
        gl->function_name =                                                          \
            reinterpret_cast<return_type(*) signature>(eglGetAddr(#function_name));  \
    }

    LIST_GLES_FUNCTIONS(LOAD_GLES2_FUNCTION, LOAD_GLES2_FUNCTION)

    return gl;
}

std::unique_ptr<GuestRenderControlDispatchTable> GfxstreamEnd2EndTest::SetupGuestRc() {
    const std::string rcLibPath = GetTestDataPath("libgfxstream_guest_rendercontrol.so").string();

    void* rcLib = dlopen(rcLibPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!rcLib) {
        GFXSTREAM_ERROR("Failed to load Gfxstream RenderControl library from %s.",
                        rcLibPath.c_str());
        return nullptr;
    }

    auto rc = std::make_unique<GuestRenderControlDispatchTable>();

#define LOAD_RENDERCONTROL_FUNCTION(name)                                   \
    rc->name = reinterpret_cast<PFN_##name>(dlsym(rcLib, #name));           \
    if (rc->name == nullptr) {                                              \
        GFXSTREAM_ERROR("Failed to load RenderControl function %s", #name); \
        return nullptr;                                                     \
    }

    LOAD_RENDERCONTROL_FUNCTION(rcCreateDevice);
    LOAD_RENDERCONTROL_FUNCTION(rcDestroyDevice);
    LOAD_RENDERCONTROL_FUNCTION(rcCompose);

    return rc;
}

std::unique_ptr<vkhpp::detail::DynamicLoader> GfxstreamEnd2EndTest::SetupGuestVk() {
    const std::string vkLibPath = GetTestDataPath("vulkan.ranchu.so").string();

    auto dl = std::make_unique<vkhpp::detail::DynamicLoader>(vkLibPath);
    if (!dl->success()) {
        GFXSTREAM_ERROR("Failed to load Vulkan from: %s", vkLibPath.c_str());
        return nullptr;
    }

    auto getInstanceProcAddr =
        dl->getProcAddress<PFN_vkGetInstanceProcAddr>("vk_icdGetInstanceProcAddr");
    if (!getInstanceProcAddr) {
        GFXSTREAM_ERROR("Failed to load Vulkan vkGetInstanceProcAddr. %s", dlerror());
        return nullptr;
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstanceProcAddr);

    return dl;
}

void GfxstreamEnd2EndTest::SetUp() {
    const TestParams params = GetParam();
    const std::string transportValue = GfxstreamTransportToEnvVar(params.with_transport);
    std::vector<std::string> featureEnables;
    for (const std::string& feature : params.with_features) {
        featureEnables.push_back(feature + ":enabled");
    }

    ASSERT_THAT(gfxstream::testing::SetupGraphicsTestEnvironment(), IsTrue())
        << "Failed to configured graphics test environment!";

    ASSERT_THAT(setenv("GFXSTREAM_TRANSPORT", transportValue.c_str(), /*overwrite=*/1), Eq(0));

    ASSERT_THAT(setenv("VIRTGPU_KUMQUAT", "1", /*overwrite=*/1), Eq(0));
    const std::string features = Join(featureEnables, ",");

    // We probably don't need to create a Kumquat Server instance for every test.  GTest provides
    // SetUpTestSuite + TearDownTestSuite for common resources that can be shared across a test
    // suite.
    mKumquatInstance = std::make_unique<KumquatInstance>();
    mKumquatInstance->SetUp(params.with_gl, params.with_vk, features);

    if (params.with_gl) {
        mGl = SetupGuestGl();
        ASSERT_THAT(mGl, NotNull());
    }
    if (params.with_vk) {
        mVk = SetupGuestVk();
        ASSERT_THAT(mVk, NotNull());
    }

    mRc = SetupGuestRc();
    ASSERT_THAT(mRc, NotNull());

    mAnwHelper.reset(createPlatformANativeWindowHelper());
    mGralloc.reset(createPlatformGralloc());
    mSync.reset(createPlatformSyncHelper());
}

void GfxstreamEnd2EndTest::TearDownGuest() {
    if (mGl) {
        EGLDisplay display = mGl->eglGetCurrentDisplay();
        if (display != EGL_NO_DISPLAY) {
            mGl->eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            mGl->eglTerminate(display);
        }
        mGl->eglReleaseThread();
        mGl.reset();
    }
    mVk.reset();
    mRc.reset();

    mAnwHelper.reset();
    mGralloc.reset();
    mSync.reset();
}

void GfxstreamEnd2EndTest::TearDown() {
    TearDownGuest();
    mKumquatInstance.reset();
    VirtGpuDevice::resetInstance();
}

void GfxstreamEnd2EndTest::SetUpEglContextAndSurface(uint32_t contextVersion, uint32_t width,
                                                     uint32_t height, EGLDisplay* outDisplay,
                                                     EGLContext* outContext,
                                                     EGLSurface* outSurface) {
    ASSERT_THAT(contextVersion, AnyOf(Eq(2), Eq(3))) << "Invalid context version requested.";

    EGLDisplay display = mGl->eglGetDisplay(EGL_DEFAULT_DISPLAY);
    ASSERT_THAT(display, Not(Eq(EGL_NO_DISPLAY)));

    int versionMajor = 0;
    int versionMinor = 0;
    ASSERT_THAT(mGl->eglInitialize(display, &versionMajor, &versionMinor), IsTrue());

    ASSERT_THAT(mGl->eglBindAPI(EGL_OPENGL_ES_API), IsTrue());

    // clang-format off
    static const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE,
    };
    // clang-format on

    int numConfigs = 0;
    ASSERT_THAT(mGl->eglChooseConfig(display, configAttributes, nullptr, 1, &numConfigs), IsTrue());
    ASSERT_THAT(numConfigs, Gt(0));

    EGLConfig config = nullptr;
    ASSERT_THAT(mGl->eglChooseConfig(display, configAttributes, &config, 1, &numConfigs), IsTrue());
    ASSERT_THAT(config, Not(Eq(nullptr)));

    // clang-format off
    static const EGLint surfaceAttributes[] = {
        EGL_WIDTH,  static_cast<EGLint>(width),
        EGL_HEIGHT, static_cast<EGLint>(height),
        EGL_NONE,
    };
    // clang-format on

    EGLSurface surface = mGl->eglCreatePbufferSurface(display, config, surfaceAttributes);
    ASSERT_THAT(surface, Not(Eq(EGL_NO_SURFACE)));

    // clang-format off
    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, static_cast<EGLint>(contextVersion),
        EGL_NONE,
    };
    // clang-format on

    EGLContext context = mGl->eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    ASSERT_THAT(context, Not(Eq(EGL_NO_CONTEXT)));

    ASSERT_THAT(mGl->eglMakeCurrent(display, surface, surface, context), IsTrue());

    *outDisplay = display;
    *outContext = context;
    *outSurface = surface;
}

void GfxstreamEnd2EndTest::TearDownEglContextAndSurface(EGLDisplay display, EGLContext context,
                                                        EGLSurface surface) {
    ASSERT_THAT(mGl->eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT),
                IsTrue());
    ASSERT_THAT(mGl->eglDestroyContext(display, context), IsTrue());
    ASSERT_THAT(mGl->eglDestroySurface(display, surface), IsTrue());
}

Result<ScopedGlShader> ScopedGlShader::MakeShader(GlDispatch& dispatch, GLenum type,
                                                  const std::string& source) {
    GLuint shader = dispatch.glCreateShader(type);
    if (!shader) {
        return gfxstream::unexpected("Failed to create shader.");
    }

    const GLchar* sourceTyped = (const GLchar*)source.c_str();
    const GLint sourceLength = source.size();
    dispatch.glShaderSource(shader, 1, &sourceTyped, &sourceLength);
    dispatch.glCompileShader(shader);

    GLint compileStatus;
    dispatch.glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);

    if (compileStatus != GL_TRUE) {
        GLint errorLogLength = 0;
        dispatch.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &errorLogLength);
        if (!errorLogLength) {
            errorLogLength = 512;
        }

        std::vector<GLchar> errorLog(errorLogLength);
        dispatch.glGetShaderInfoLog(shader, errorLogLength, &errorLogLength, errorLog.data());

        const std::string errorString = errorLogLength == 0 ? "" : errorLog.data();
        GFXSTREAM_ERROR("Shader compilation failed with: \"%s\"", errorString.c_str());

        dispatch.glDeleteShader(shader);
        return gfxstream::unexpected(errorString);
    }

    return ScopedGlShader(dispatch, shader);
}

Result<ScopedGlProgram> ScopedGlProgram::MakeProgram(GlDispatch& dispatch,
                                                     const std::string& vertSource,
                                                     const std::string& fragSource) {
    auto vertShader =
        GFXSTREAM_EXPECT(ScopedGlShader::MakeShader(dispatch, GL_VERTEX_SHADER, vertSource));
    auto fragShader =
        GFXSTREAM_EXPECT(ScopedGlShader::MakeShader(dispatch, GL_FRAGMENT_SHADER, fragSource));

    GLuint program = dispatch.glCreateProgram();
    dispatch.glAttachShader(program, vertShader);
    dispatch.glAttachShader(program, fragShader);
    dispatch.glLinkProgram(program);

    GLint linkStatus;
    dispatch.glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint errorLogLength = 0;
        dispatch.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &errorLogLength);
        if (!errorLogLength) {
            errorLogLength = 512;
        }

        std::vector<char> errorLog(errorLogLength, 0);
        dispatch.glGetProgramInfoLog(program, errorLogLength, nullptr, errorLog.data());

        const std::string errorString = errorLogLength == 0 ? "" : errorLog.data();
        GFXSTREAM_ERROR("Program link failed with: \"%s\"", errorString.c_str());

        dispatch.glDeleteProgram(program);
        return gfxstream::unexpected(errorString);
    }

    return ScopedGlProgram(dispatch, program);
}

Result<ScopedGlProgram> ScopedGlProgram::MakeProgram(
    GlDispatch& dispatch, GLenum programBinaryFormat,
    const std::vector<uint8_t>& programBinaryData) {
    GLuint program = dispatch.glCreateProgram();
    dispatch.glProgramBinary(program, programBinaryFormat, programBinaryData.data(),
                             programBinaryData.size());

    GLint linkStatus;
    dispatch.glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint errorLogLength = 0;
        dispatch.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &errorLogLength);
        if (!errorLogLength) {
            errorLogLength = 512;
        }

        std::vector<char> errorLog(errorLogLength, 0);
        dispatch.glGetProgramInfoLog(program, errorLogLength, nullptr, errorLog.data());

        const std::string errorString = errorLogLength == 0 ? "" : errorLog.data();
        GFXSTREAM_ERROR("Program link failed with: \"%s\"", errorString.c_str());

        dispatch.glDeleteProgram(program);
        return gfxstream::unexpected(errorString);
    }

    return ScopedGlProgram(dispatch, program);
}

Result<ScopedAHardwareBuffer> ScopedAHardwareBuffer::Allocate(Gralloc& gralloc, uint32_t width,
                                                              uint32_t height, uint32_t format) {
    // TODO: move into emulated gralloc:
    if (format == GFXSTREAM_AHB_FORMAT_YV12) {
        if ((width % 32) != 0) {
            return gfxstream::unexpected(
                "Failed to allocate YV12 AHB with non multiple of 32 width: " +
                std::to_string(width));
        }
    }

    AHardwareBuffer* ahb = nullptr;
    int status = gralloc.allocate(width, height, format, -1, &ahb);
    if (status != 0) {
        return gfxstream::unexpected(std::string("Failed to allocate AHB with width:") +
                                     std::to_string(width) + std::string(" height:") +
                                     std::to_string(height) + std::string(" format:") +
                                     std::to_string(format));
    }

    return ScopedAHardwareBuffer(gralloc, ahb);
}

Result<ScopedGlShader> GfxstreamEnd2EndTest::SetUpShader(GLenum type, const std::string& source) {
    if (!mGl) {
        return gfxstream::unexpected("Gl not enabled for this test.");
    }

    return ScopedGlShader::MakeShader(*mGl, type, source);
}

Result<ScopedGlProgram> GfxstreamEnd2EndTest::SetUpProgram(const std::string& vertSource,
                                                           const std::string& fragSource) {
    if (!mGl) {
        return gfxstream::unexpected("Gl not enabled for this test.");
    }

    return ScopedGlProgram::MakeProgram(*mGl, vertSource, fragSource);
}

Result<ScopedGlProgram> GfxstreamEnd2EndTest::SetUpProgram(
    GLenum programBinaryFormat, const std::vector<uint8_t>& programBinaryData) {
    if (!mGl) {
        return gfxstream::unexpected("Gl not enabled for this test.");
    }

    return ScopedGlProgram::MakeProgram(*mGl, programBinaryFormat, programBinaryData);
}

Result<GfxstreamEnd2EndTest::TypicalVkTestEnvironment>
GfxstreamEnd2EndTest::SetUpTypicalVkTestEnvironment(const TypicalVkTestEnvironmentOptions& opts) {
    const auto availableInstanceLayers = vkhpp::enumerateInstanceLayerProperties().value;
    GFXSTREAM_VERBOSE("Available instance layers:");
    for (const vkhpp::LayerProperties& layer : availableInstanceLayers) {
        GFXSTREAM_VERBOSE(" - %s", layer.layerName.data());
    }

    constexpr const bool kEnableValidationLayers = true;

    std::vector<const char*> requestedInstanceExtensions;
    std::vector<const char*> requestedInstanceLayers;
    if (kEnableValidationLayers) {
        requestedInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const vkhpp::ApplicationInfo applicationInfo{
        .pApplicationName = ::testing::UnitTest::GetInstance()->current_test_info()->name(),
        .applicationVersion = 1,
        .pEngineName = "Gfxstream Testing Engine",
        .engineVersion = 1,
        .apiVersion = opts.apiVersion,
    };
    const vkhpp::InstanceCreateInfo instanceCreateInfo{
        .pNext = opts.instanceCreateInfoPNext ? *opts.instanceCreateInfoPNext : nullptr,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<uint32_t>(requestedInstanceLayers.size()),
        .ppEnabledLayerNames = requestedInstanceLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requestedInstanceExtensions.size()),
        .ppEnabledExtensionNames = requestedInstanceExtensions.data(),
    };

    auto instance = GFXSTREAM_EXPECT_VKHPP_RV(vkhpp::createInstanceUnique(instanceCreateInfo));

    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    auto physicalDevices = GFXSTREAM_EXPECT_VKHPP_RV(instance->enumeratePhysicalDevices());
    GFXSTREAM_VERBOSE("Available physical devices:");
    for (const auto& physicalDevice : physicalDevices) {
        const auto physicalDeviceProps = physicalDevice.getProperties();
        GFXSTREAM_VERBOSE(" - %s", physicalDeviceProps.deviceName.data());
    }

    if (physicalDevices.empty()) {
        return gfxstream::unexpected(
            "Failed to set up typical VK env: no physical devices available.");
    }

    auto physicalDevice = std::move(physicalDevices[0]);
    {
        const auto physicalDeviceProps = physicalDevice.getProperties();
        GFXSTREAM_VERBOSE("Selected physical device: %s", physicalDeviceProps.deviceName.data());
    }
    {
        const auto exts =
            GFXSTREAM_EXPECT_VKHPP_RV(physicalDevice.enumerateDeviceExtensionProperties());
        GFXSTREAM_VERBOSE("Available physical device extensions:");
        for (const auto& ext : exts) {
            GFXSTREAM_VERBOSE(" - %s", ext.extensionName.data());
        }
    }

    uint32_t graphicsQueueFamilyIndex = -1;
    {
        const auto props = physicalDevice.getQueueFamilyProperties();
        for (uint32_t i = 0; i < props.size(); i++) {
            const auto& prop = props[i];
            if (prop.queueFlags & vkhpp::QueueFlagBits::eGraphics) {
                graphicsQueueFamilyIndex = i;
                break;
            }
        }
    }
    if (graphicsQueueFamilyIndex == -1) {
        return gfxstream::unexpected("Failed to set up typical VK env: no graphics queue.");
    }

    const float queuePriority = 1.0f;
    const vkhpp::DeviceQueueCreateInfo deviceQueueCreateInfo = {
        .queueFamilyIndex = graphicsQueueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    std::vector<const char*> deviceExtensions = {
        VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME,
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
    };
    if (opts.deviceExtensions) {
        for (const std::string& ext : *opts.deviceExtensions) {
            deviceExtensions.push_back(ext.c_str());
        }
    }
    const vkhpp::DeviceCreateInfo deviceCreateInfo = {
        .pNext = opts.deviceCreateInfoPNext ? *opts.deviceCreateInfoPNext : nullptr,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
    };
    auto device = GFXSTREAM_EXPECT_VKHPP_RV(physicalDevice.createDeviceUnique(deviceCreateInfo));

    auto queue = device->getQueue(graphicsQueueFamilyIndex, 0);

    return TypicalVkTestEnvironment{
        .instance = std::move(instance),
        .physicalDevice = std::move(physicalDevice),
        .device = std::move(device),
        .queue = std::move(queue),
        .queueFamilyIndex = graphicsQueueFamilyIndex,
    };
}

void GfxstreamEnd2EndTest::SnapshotSaveAndLoad() {
    mKumquatInstance->Snapshot();
    mKumquatInstance->Restore();
}

Result<Image> GfxstreamEnd2EndTest::LoadImage(const std::string& basename) {
    const std::string filepath = GetTestDataPath(basename);
    if (!std::filesystem::exists(filepath)) {
        return gfxstream::unexpected("File " + filepath + " does not exist.");
    }
    if (!std::filesystem::is_regular_file(filepath)) {
        return gfxstream::unexpected("File " + filepath + " is not a regular file.");
    }

    Image image;

    std::vector<uint32_t> sourcePixels;
    if (!LoadRGBAFromPng(filepath, &image.width, &image.height, &image.pixels)) {
        return gfxstream::unexpected("Failed to load " + filepath + " as RGBA PNG.");
    }

    return image;
}

Result<Image> GfxstreamEnd2EndTest::AsImage(ScopedAHardwareBuffer& ahb) {
    Image actual;
    actual.width = ahb.GetWidth();
    if (actual.width == 0) {
        return gfxstream::unexpected("Failed to query AHB width.");
    }
    actual.height = ahb.GetHeight();
    if (actual.height == 0) {
        return gfxstream::unexpected("Failed to query AHB height.");
    }
    actual.pixels.resize(actual.width * actual.height);

    const uint32_t ahbFormat = ahb.GetAHBFormat();
    if (ahbFormat != GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM &&
        ahbFormat != GFXSTREAM_AHB_FORMAT_B8G8R8A8_UNORM) {
        return gfxstream::unexpected("Unhandled AHB format " + std::to_string(ahbFormat));
    }

    {
        uint8_t* ahbPixels = GFXSTREAM_EXPECT(ahb.Lock());
        std::memcpy(actual.pixels.data(), ahbPixels, actual.pixels.size() * sizeof(uint32_t));
        ahb.Unlock();
    }

    if (ahbFormat == GFXSTREAM_AHB_FORMAT_B8G8R8A8_UNORM) {
        for (uint32_t& pixel : actual.pixels) {
            uint8_t* pixelComponents = reinterpret_cast<uint8_t*>(&pixel);
            std::swap(pixelComponents[0], pixelComponents[2]);
        }
    }

    return actual;
}

Result<Ok> GfxstreamEnd2EndTest::FillAhb(ScopedAHardwareBuffer& ahb, PixelFillColor pixelFill) {
    // Use checkerboad function with the same colors
    return FillAhbWithCheckerboard(ahb, ahb.GetWidth(), pixelFill, pixelFill);
}

Result<Ok> GfxstreamEnd2EndTest::FillAhbWithCheckerboard(ScopedAHardwareBuffer& ahb,
                                                         const uint32_t tileWidth,
                                                         PixelFillColor pixelFill1,
                                                         PixelFillColor pixelFill2) {
    if (tileWidth == 0) {
        return gfxstream::unexpected("Invalid parameter: tileWidth");
    }
    const uint32_t drmFormat = ahb.GetDrmFormat();

    const uint32_t ahbWidth = ahb.GetWidth();
    const uint32_t ahbHeight = ahb.GetHeight();

    std::vector<Gralloc::LockedPlane> planes = GFXSTREAM_EXPECT(ahb.LockPlanes());
    if (drmFormat == DRM_FORMAT_ABGR8888) {
        if (!std::holds_alternative<PixelR8G8B8A8>(pixelFill1) ||
            !std::holds_alternative<PixelR8G8B8A8>(pixelFill2)) {
            return gfxstream::unexpected(
                "FillAhb does not support filling a RGBA AHB with non RGBA data.");
        }
        const PixelR8G8B8A8& color1 = std::get<PixelR8G8B8A8>(pixelFill1);
        const PixelR8G8B8A8& color2 = std::get<PixelR8G8B8A8>(pixelFill2);

        const Gralloc::LockedPlane& plane = planes[0];

        // Add padding and copy with tileWidth-offset for odd rows
        std::vector<uint8_t> srcRow;
        srcRow.reserve((ahbWidth + tileWidth) * 4);
        for (uint32_t x = 0; x < ahbWidth + tileWidth; x++) {
            const bool odd = ((x / tileWidth) % 2);
            const PixelR8G8B8A8& color = odd ? color2 : color1;
            srcRow.push_back(color.r);
            srcRow.push_back(color.g);
            srcRow.push_back(color.b);
            srcRow.push_back(color.a);
        }

        for (uint32_t y = 0; y < ahbHeight; y++) {
            uint8_t* dstRow = plane.data + (y * plane.rowStrideBytes);
            const uint8_t* srcRowData = srcRow.data();
            const bool odd = ((y / tileWidth) % 2);
            if (odd) {
                // Offset the pointer to alternate between checkerboard colors
                srcRowData += tileWidth*4;
            }
            std::memcpy(dstRow, srcRowData, ahbWidth*4);
        }
    } else if (drmFormat == DRM_FORMAT_NV12 || drmFormat == DRM_FORMAT_YVU420) {
        if (!std::holds_alternative<PixelY8U8V8>(pixelFill1) ||
            !std::holds_alternative<PixelY8U8V8>(pixelFill2)) {
            return gfxstream::unexpected(
                "FillAhb does not support filling a YUV AHB with non YUV data.");
        }
        const PixelY8U8V8& color1 = std::get<PixelY8U8V8>(pixelFill1);
        const PixelY8U8V8& color2 = std::get<PixelY8U8V8>(pixelFill2);

        const Gralloc::LockedPlane& yPlane = planes[0];
        const Gralloc::LockedPlane& uPlane = planes[1];
        const Gralloc::LockedPlane& vPlane = planes[2];

        // Y plane, full res
        for (uint32_t y = 0; y < ahbHeight; y++) {
            for (uint32_t x = 0; x < ahbWidth; x++) {
                uint8_t* dstY =
                    yPlane.data + (y * yPlane.rowStrideBytes) + (x * yPlane.pixelStrideBytes);
                const bool odd = ((x / tileWidth) % 2) ^ ((y / tileWidth) % 2);
                *dstY = (odd) ? color2.y : color1.y;
            }
        }

        // UV planes, half res
        uint32_t tileWidthUV = std::max(1u, tileWidth / 2);
        for (uint32_t UV_y = 0; UV_y < ahbHeight / 2; UV_y++) {
            for (uint32_t UV_x = 0; UV_x < ahbWidth / 2; UV_x++) {
                uint8_t* dstU =
                    uPlane.data + (UV_y * uPlane.rowStrideBytes) + (UV_x * uPlane.pixelStrideBytes);
                uint8_t* dstV =
                    vPlane.data + (UV_y * vPlane.rowStrideBytes) + (UV_x * vPlane.pixelStrideBytes);

                const bool odd = ((UV_x / tileWidthUV) % 2) ^ ((UV_y / tileWidthUV) % 2);
                *dstU = odd ? color2.u : color1.u;
                *dstV = odd ? color2.v : color1.v;
            }
        }
    } else {
        return gfxstream::unexpected("Unhandled DRM format: " + std::to_string(drmFormat));
    }

    ahb.Unlock();

    return Ok{};
}

Result<ScopedAHardwareBuffer> GfxstreamEnd2EndTest::CreateAHBFromImage(
    const std::string& basename) {
    auto image = GFXSTREAM_EXPECT(LoadImage(basename));

    auto ahb = GFXSTREAM_EXPECT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, image.width, image.height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    {
        uint8_t* ahbPixels = GFXSTREAM_EXPECT(ahb.Lock());
        std::memcpy(ahbPixels, image.pixels.data(), image.pixels.size() * sizeof(uint32_t));
        ahb.Unlock();
    }

    return std::move(ahb);
}

Result<ScopedAHardwareBuffer> GfxstreamEnd2EndTest::CreateAHBWithColor(
    const uint32_t width, const uint32_t height, const uint32_t ahbFormat,
    const PixelFillColor& color) {
    auto ahb =
        GFXSTREAM_EXPECT(ScopedAHardwareBuffer::Allocate(*mGralloc, width, height, ahbFormat));

    GFXSTREAM_EXPECT(FillAhb(ahb, color));

    return std::move(ahb);
}

Result<ScopedAHardwareBuffer> GfxstreamEnd2EndTest::CreateAHBWithCheckerboard(
    const uint32_t width, const uint32_t height, const uint32_t tileWidth, const uint32_t ahbFormat,
    const PixelFillColor& color1, const PixelFillColor& color2) {
    auto ahb =
        GFXSTREAM_EXPECT(ScopedAHardwareBuffer::Allocate(*mGralloc, width, height, ahbFormat));

    GFXSTREAM_EXPECT(FillAhbWithCheckerboard(ahb, tileWidth, color1, color2));

    return std::move(ahb);
}

bool GfxstreamEnd2EndTest::ArePixelsSimilar(uint32_t expectedPixel, uint32_t actualPixel) {
    const uint8_t* actualRGBA = reinterpret_cast<const uint8_t*>(&actualPixel);
    const uint8_t* expectedRGBA = reinterpret_cast<const uint8_t*>(&expectedPixel);

    constexpr const uint32_t kRGBA8888Tolerance = 2;
    for (uint32_t channel = 0; channel < 4; channel++) {
        const uint8_t actualChannel = actualRGBA[channel];
        const uint8_t expectedChannel = expectedRGBA[channel];

        if ((std::max(actualChannel, expectedChannel) - std::min(actualChannel, expectedChannel)) >
            kRGBA8888Tolerance) {
            return false;
        }
    }
    return true;
}

bool GfxstreamEnd2EndTest::AreImagesSimilar(const Image& expected, const Image& actual) {
    if (actual.width != expected.width) {
        ADD_FAILURE() << "Image comparison failed: " << "expected.width " << expected.width << "vs"
                      << "actual.width " << actual.width;
        return false;
    }
    if (actual.height != expected.height) {
        ADD_FAILURE() << "Image comparison failed: " << "expected.height " << expected.height
                      << "vs" << "actual.height " << actual.height;
        return false;
    }
    const uint32_t width = actual.width;
    const uint32_t height = actual.height;
    const uint32_t* actualPixels = actual.pixels.data();
    const uint32_t* expectedPixels = expected.pixels.data();

    bool imagesSimilar = true;

    uint32_t reportedIncorrectPixels = 0;
    constexpr const uint32_t kMaxReportedIncorrectPixels = 5;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            const uint32_t actualPixel = actualPixels[y * height + x];
            const uint32_t expectedPixel = expectedPixels[y * width + x];
            if (!ArePixelsSimilar(expectedPixel, actualPixel)) {
                imagesSimilar = false;
                if (reportedIncorrectPixels < kMaxReportedIncorrectPixels) {
                    reportedIncorrectPixels++;
                    const uint8_t* actualRGBA = reinterpret_cast<const uint8_t*>(&actualPixel);
                    const uint8_t* expectedRGBA = reinterpret_cast<const uint8_t*>(&expectedPixel);
                    // clang-format off
                    ADD_FAILURE()
                        << "Pixel comparison failed at (" << x << ", " << y << ") "
                        << " with actual "
                        << " r:" << static_cast<int>(actualRGBA[0])
                        << " g:" << static_cast<int>(actualRGBA[1])
                        << " b:" << static_cast<int>(actualRGBA[2])
                        << " a:" << static_cast<int>(actualRGBA[3])
                        << " but expected "
                        << " r:" << static_cast<int>(expectedRGBA[0])
                        << " g:" << static_cast<int>(expectedRGBA[1])
                        << " b:" << static_cast<int>(expectedRGBA[2])
                        << " a:" << static_cast<int>(expectedRGBA[3]);
                    // clang-format on
                }
            }
        }
    }
    return imagesSimilar;
}

Result<Ok> GfxstreamEnd2EndTest::CompareAHBWithGolden(ScopedAHardwareBuffer& ahb,
                                                      const std::string& goldenBasename) {
    Image actual = GFXSTREAM_EXPECT(AsImage(ahb));
    Result<Image> expected = LoadImage(goldenBasename);

    bool imagesAreSimilar = false;
    if (expected.ok()) {
        imagesAreSimilar = AreImagesSimilar(*expected, actual);
    } else {
        imagesAreSimilar = false;
    }

    if (!imagesAreSimilar && kSaveImagesIfComparisonFailed) {
        static uint32_t sImageNumber{1};
        const std::string outputBasename = std::to_string(sImageNumber++) + "_" + goldenBasename;
        const std::string output =
            (std::filesystem::temp_directory_path() / outputBasename).string();
        SaveRGBAToPng(actual.width, actual.height, actual.pixels.data(), output);
        ADD_FAILURE() << "Saved image comparison actual image to " << output;
    }

    if (!imagesAreSimilar) {
        return gfxstream::unexpected(
            "Image comparison failed (consider setting kSaveImagesIfComparisonFailed to true to "
            "see the actual image generated).");
    }

    return {};
}

Result<Ok> GfxstreamEnd2EndTest::AhbIsEntirely(ScopedAHardwareBuffer& ahb,
                                               const PixelR8G8B8A8& color) {
    Image actualImage = GFXSTREAM_EXPECT(AsImage(ahb));
    Image expectedImage = ImageFromColor(actualImage.width, actualImage.height, color);

    if (!AreImagesSimilar(expectedImage, actualImage)) {
        return gfxstream::unexpected("Image is not entirely " + color.ToString());
    }
    return {};
}

namespace {

static constexpr uint8_t ClampToU8(int x) {
    if (x < 0) return 0;
    if (x > 255) return 255;
    return static_cast<uint8_t>(x);
}

static constexpr int SaturateToInt(float x) {
    constexpr int kMaxS32FitsInFloat = 2147483520;
    constexpr int kMinS32FitsInFloat = -kMaxS32FitsInFloat;
    x = x < kMaxS32FitsInFloat ? x : kMaxS32FitsInFloat;
    x = x > kMinS32FitsInFloat ? x : kMinS32FitsInFloat;
    return (int)x;
}

static constexpr float Round(float x) { return (float)((double)x); }

}  // namespace

std::vector<uint8_t> Fill(uint32_t w, uint32_t h, const PixelR8G8B8A8& pixel) {
    std::vector<uint8_t> ret;
    ret.reserve(w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            ret.push_back(pixel.r);
            ret.push_back(pixel.g);
            ret.push_back(pixel.b);
            ret.push_back(pixel.a);
        }
    }
    return ret;
}

void RGBToYUV(uint8_t r, uint8_t g, uint8_t b, uint8_t* outY, uint8_t* outU, uint8_t* outV) {
    static const float kRGBToYUVBT601NarrowRange[] = {
        // clang-format off
         0.256788f,  0.504129f,  0.097906f,  0.000000f,  0.062745f,
        -0.148223f, -0.290993f,  0.439216f,  0.000000f,  0.501961f,
         0.439216f, -0.367788f, -0.071427f,  0.000000f,  0.501961f,
         0.000000f,  0.000000f,  0.000000f,  1.000000f,  0.000000f,
        // clang-format on
    };

    *outY = ClampToU8(SaturateToInt(
        Round((kRGBToYUVBT601NarrowRange[0] * r) + (kRGBToYUVBT601NarrowRange[1] * g) +
              (kRGBToYUVBT601NarrowRange[2] * b) + (kRGBToYUVBT601NarrowRange[4] * 255))));
    *outU = ClampToU8(SaturateToInt(
        Round((kRGBToYUVBT601NarrowRange[5] * r) + (kRGBToYUVBT601NarrowRange[6] * g) +
              (kRGBToYUVBT601NarrowRange[7] * b) + (kRGBToYUVBT601NarrowRange[9] * 255))));
    *outV = ClampToU8(SaturateToInt(
        Round((kRGBToYUVBT601NarrowRange[10] * r) + (kRGBToYUVBT601NarrowRange[11] * g) +
              (kRGBToYUVBT601NarrowRange[12] * b) + (kRGBToYUVBT601NarrowRange[14] * 255))));
}

}  // namespace tests
}  // namespace gfxstream
