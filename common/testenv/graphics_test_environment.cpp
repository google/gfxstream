// Copyright (C) 2025 The Android Open Source Project
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

#include "gfxstream/common/testing/graphics_test_environment.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#ifdef BAZEL_CURRENT_REPOSITORY
#include <rules_cc/cc/runfiles/runfiles.h>
#endif

#include "gfxstream/common/logging.h"
#include "gfxstream/system/System.h"

namespace gfxstream {
namespace testing {
namespace {

#if defined(GFXSTREAM_TESTING_USE_GLES_ANGLE) || \
    defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE) || \
    defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
std::optional<std::filesystem::path> GetGraphicsDriverPath(const std::string& basename) {
#if defined(BAZEL_CURRENT_REPOSITORY)
    // https://github.com/bazelbuild/rules_cc/blob/main/cc/runfiles/runfiles.h
    using rules_cc::cc::runfiles::Runfiles;
    static Runfiles* sRunfiles = []() -> Runfiles* {
        std::string error;
        auto* runfiles = Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error);
        if (runfiles == nullptr) {
            GFXSTREAM_ERROR("Failed to load runfiles: %s.", error.c_str());
            return nullptr;
        }
        return runfiles;
    }();
    if (sRunfiles == nullptr) {
        GFXSTREAM_ERROR("Testdata runfiles not available?");
        return std::nullopt;
    }
    const std::vector<std::string> possiblePaths = {
        std::string("gfxstream/common/testenv/graphics_test_environment_drivers/") + basename,
        std::string("_main/common/testenv/graphics_test_environment_drivers/") + basename,
    };
    for (const std::string& possiblePath : possiblePaths) {
        const std::string path = sRunfiles->Rlocation(possiblePath);
        if (!path.empty() && std::filesystem::exists(path)) {
            return path;
        }
    }
    GFXSTREAM_ERROR("Failed to find %s in graphics test environment data files.", basename.c_str());
    return std::nullopt;
#else
    GFXSTREAM_ERROR("Library built without BAZEL_CURRENT_REPOSITORY?");
    return std::nullopt;
#endif  // defined(BAZEL_CURRENT_REPOSITORY)
}
#endif

}  // namespace

bool SetupGraphicsTestEnvironment() {
#if defined(GFXSTREAM_TESTING_USE_GLES_ANGLE)
    GFXSTREAM_INFO("GraphicsTestEnvironment: configuring ANGLE as EGL/GLES driver.");

    // TODO: Update ANGLE build to support running with GLVND. See
    // https://github.com/NVIDIA/libglvnd/blob/master/include/glvnd/libeglabi.h.
    // Then uncomment:
    //
    //     const auto driverGlesOpt = GetGraphicsDriverPath("libGLESv2_angle.so.2");
    //     if (!driverGlesOpt) {
    //         GFXSTREAM_ERROR("Failed to find libGLESv2_angle.so.2");
    //         return false;
    //     }
    //     const auto driverEglOpt = GetGraphicsDriverPath("libEGL_angle.so.1");
    //     if (!driverEglOpt) {
    //         GFXSTREAM_ERROR("Failed to find libEGL_angle.so.1");
    //         return false;
    //     }
    //     const auto driverEglIcdOpt = GetGraphicsDriverPath("libEGL_angle_vendor_icd.json");
    //     if (!driverEglOpt) {
    //         GFXSTREAM_ERROR("Failed to find libEGL_angle_vendor_icd.json");
    //         return false;
    //     }
    //     const std::string driverEglIcd = driverEglIcdOpt->string();
    //     gfxstream::base::setEnvironmentVariable("__EGL_VENDOR_LIBRARY_FILENAMES", driverEglIcd);
    //
    // For now, assume the ANGLE libs are directly used:
    const auto driverGlesOpt = GetGraphicsDriverPath("libGLESv2.so");
    if (!driverGlesOpt) {
        GFXSTREAM_ERROR("Failed to find libGLESv2.so.");
        return false;
    }
    const auto driverEglOpt = GetGraphicsDriverPath("libEGL.so");
    if (!driverEglOpt) {
        GFXSTREAM_ERROR("Failed to find libEGL.so");
        return false;
    }
    const std::filesystem::path driverEgl = *driverEglOpt;
    const std::filesystem::path driverDirectory = driverEgl.parent_path();

    const std::string currentLdLibraryPath = gfxstream::base::getEnvironmentVariable("LD_LIBRARY_PATH");
    const std::string updatedLdLibraryPath = driverDirectory.string() + ":" + currentLdLibraryPath;
    gfxstream::base::setEnvironmentVariable("LD_LIBRARY_PATH", updatedLdLibraryPath);
    gfxstream::base::setEnvironmentVariable("ANDROID_EMU_HEADLESS", "1");
#else
    GFXSTREAM_INFO("GraphicsTestEnvironment: not changing host EGL/GLES driver configuration.");
#endif  // defined(GFXSTREAM_TESTING_USE_GLES_ANGLE)

#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
    GFXSTREAM_INFO("GraphicsTestEnvironment: configuring Lavapipe as Vulkan driver.");
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
    GFXSTREAM_INFO("GraphicsTestEnvironment: configuring Swiftshader as Vulkan driver.");
#else
    GFXSTREAM_INFO("GraphicsTestEnvironment: not changing Vulkan driver configuration.");
#endif

#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE) || defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
    const std::string driverBasename =
#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
        "libvulkan_lvp.so";
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
        "libvk_swiftshader.so";
#else
#error "Unhandled driver."
#endif

    const std::string driverIcdBasename =
#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
        "vk_lavapipe_icd.json";
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
        "vk_swiftshader_icd.json";
#else
#error "Unhandled driver."
#endif

    const auto driverOpt = GetGraphicsDriverPath(driverBasename);
    if (!driverOpt) {
        GFXSTREAM_ERROR("Failed to find %s", driverBasename.c_str());
        return false;
    }
    auto driverIcdOpt = GetGraphicsDriverPath(driverIcdBasename);
#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
    if (!driverIcdOpt) {
        driverIcdOpt = GetGraphicsDriverPath("lvp_icd.json");
    }
#endif
    if (!driverIcdOpt) {
        GFXSTREAM_ERROR("Failed to find %s", driverIcdBasename.c_str());
        return false;
    }

    const std::filesystem::path driverDir = driverOpt->parent_path();
    const std::filesystem::path bundledLoader = driverDir / "libvulkan.so";
    if (std::filesystem::exists(bundledLoader)) {
        gfxstream::base::setEnvironmentVariable("ANDROID_EMU_VK_LOADER_PATH",
                                                bundledLoader.string());
    }

#if defined(__linux__) || defined(__APPLE__)
    const std::filesystem::path llvmLib = driverDir / "libLLVM.so";
    if (std::filesystem::exists(llvmLib)) {
        if (void* handle = ::dlopen(llvmLib.c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE)) {
            ::dlclose(handle);
        }
    }
    // Pin the software Vulkan ICD in the process address space with RTLD_NODELETE so
    // it remains mapped across vkDestroyInstance calls and until process exit.
    if (void* handle = ::dlopen(driverOpt->c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE)) {
        ::dlclose(handle);
    }
#endif

    const std::string driverIcd = driverIcdOpt->string();
    gfxstream::base::setEnvironmentVariable("VK_DRIVER_FILES", driverIcd);
    gfxstream::base::setEnvironmentVariable("VK_ICD_FILENAMES", driverIcd);
#endif  // defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE) || defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)

    return true;
}

bool IsGraphicsTestEnvironmentProvidingGlesDriver() {
#if defined(GFXSTREAM_TESTING_USE_GLES_ANGLE)
    return true;
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE) || defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
    return false;
#else
    return true;
#endif
}

bool IsGraphicsTestEnvironmentProvidingVulkanDriver() {
#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
    return true;
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
    return true;
#else
    return false;
#endif
}

}  // namespace testing
}  // namespace gfxstream
