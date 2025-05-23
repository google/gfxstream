// Copyright 2024 The Android Open Source Project
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

#pragma once

#include <string>
#include <map>
#include <vector>

namespace gfxstream {
namespace host {

struct FeatureInfo;

using FeatureMap = std::map<std::string, FeatureInfo*>;
//type used for returning success or a string with the concatenated errors (missing features)
using FeatureResult = std::pair<bool, std::string>;

struct FeatureInfo {
    FeatureInfo(const FeatureInfo& rhs) = default;

    FeatureInfo(const char* name,
                const char* description,
                FeatureMap* map) :
            name(name),
            description(description),
            enabled(false),
            reason("Default value") {
        if (map) {
            (*map)[std::string(name)] = this;
        }
    }

    ~FeatureInfo() = default;

    std::string name;
    std::string description;
    bool enabled;
    std::string reason;
};

struct FeatureSet {
    FeatureSet() = default;

    FeatureSet(const FeatureSet& rhs);
    FeatureSet& operator=(const FeatureSet& rhs);

    FeatureMap map;

    FeatureInfo AsyncComposeSupport = {
        "AsyncComposeSupport",
        "If enabled, allows the guest to use asynchronous render control commands "
        "to compose and post frame buffers.",
        &map,
    };
    FeatureInfo ExternalBlob = {
        "ExternalBlob",
        "If enabled, virtio gpu blob resources will be allocated with external "
        "memory and will be exportable via file descriptors.",
        &map,
    };
    FeatureInfo VulkanExternalSync = {
        "VulkanExternalSync",
        "If enabled, Vulkan fences/semaphores will be allocated with external "
        "create info and will be exportable via fence handles.",
        &map,
    };
    FeatureInfo SystemBlob = {
        "SystemBlob",
        "If enabled, virtio gpu blob resources will be allocated with shmem and "
        "will be exportable via file descriptors.",
        &map,
    };
    FeatureInfo GlAsyncSwap = {
        "GlAsyncSwap",
        "If enabled, uses the host GL driver's fence commands and fence file "
        "descriptors in the guest to have explicit signals of buffer swap "
        "completion.",
        &map,
    };
    FeatureInfo GlDirectMem = {
        "GlDirectMem",
        "If enabled, allows mapping the host address from glMapBufferRange() into "
        "the guest.",
        &map,
    };
    FeatureInfo GlDma = {
        "GlDma",
        "Default description: consider contributing a description if you see this!",
        &map,
    };
    FeatureInfo GlDma2 = {
        "GlDma2",
        "Default description: consider contributing a description if you see this!",
        &map,
    };
    FeatureInfo GlProgramBinaryLinkStatus = {
        "GlProgramBinaryLinkStatus",
        "If enabled, the host will track and report the correct link status of programs "
        "created with glProgramBinary(). If not enabled, the host will effectively "
        "return false for all glGetProgramiv(... GL_LINK_STATUS ...) calls."
        ""
        "Prior to aosp/3151743, the host GLES translator was not tracking the link "
        "status of programs created by glProgramBinary() and would always return "
        "false for glGetProgramiv(... GL_LINK_STATUS ...) calls."
        ""
        "Also, prior to aosp/3151743, the guest GL encoder was losing information about "
        "`samplerExternalOES` between glGetProgramBinary() and glProgramBinary() calls "
        "which would cause incorrect handling of sampling from a binding with "
        "GL_TEXTURE_EXTERNAL_OES."
        ""
        "Guest applications seem to typically fallback to fully recreating programs "
        "with shaders (glCreateShader() + glShaderSource() + glAttachShader()) when "
        "linking fails with glProgramBinary(). This lead to backwards compatibility "
        "problems when an old guest (which does not have the above guest GL encoder "
        "fix) runs with a newer host (which does have the above host GLES translator "
        "fix) as the fallback path would be disabled but the guest would have "
        "incorrect GL_TEXTURE_EXTERNAL_OES handling. As such, the corrected host "
        "behavior is hidden behind this feature.",
        &map,
    };
    FeatureInfo GlPipeChecksum = {
        "GlPipeChecksum",
        "If enabled, the guest and host will use checksums to ensure consistency "
        "for GL calls between the guest and host.",
        &map,
    };
    FeatureInfo GlesDynamicVersion = {
        "GlesDynamicVersion",
        "If enabled, attempts to detect and use the maximum supported GLES version "
        "from the host.",
        &map,
    };
    FeatureInfo GrallocSync = {
        "GrallocSync",
        "If enabled, adds additional synchronization on the host for cases where "
        "a guest app may directly writing to gralloc buffers and posting.",
        &map,
    };
    FeatureInfo GuestVulkanOnly = {
        "GuestVulkanOnly",
        "If enabled, indicates that the guest only requires Vulkan translation. "
        " The guest will not use GL and the host will not enable the GL backend. "
        " This is the case when the guest uses libraries such as Angle or Zink for "
        " GL to Vulkan translation.",
        &map,
    };
    FeatureInfo HasSharedSlotsHostMemoryAllocator = {
        "HasSharedSlotsHostMemoryAllocator",
        "If enabled, the host supports "
        "AddressSpaceSharedSlotsHostMemoryAllocatorContext.",
        &map,
    };
    FeatureInfo HostComposition = {
        "HostComposition",
        "If enabled, the host supports composition via render control commands.",
        &map,
    };
    FeatureInfo HwcMultiConfigs = {
        "HwcMultiConfigs",
        "If enabled, the host supports multiple HWComposer configs per display.",
        &map,
    };
    FeatureInfo Minigbm = {
        "Minigbm",
        "If enabled, the guest is known to be using Minigbm as its Gralloc "
        "implementation.",
        &map,
    };
    FeatureInfo NativeTextureDecompression = {
        "NativeTextureDecompression",
        "If enabled, allows the host to use ASTC and ETC2 formats when supported by "
        " the host GL driver.",
        &map,
    };
    FeatureInfo NoDelayCloseColorBuffer = {
        "NoDelayCloseColorBuffer",
        "If enabled, indicates that the guest properly associates resources with "
        "guest OS handles and that the host resources can be immediately cleaned "
        "upon receiving resource clean up commands.",
        &map,
    };
    FeatureInfo PlayStoreImage = {
        "PlayStoreImage",
        "If enabled, the guest image is using the play store image which has "
        "additional requirements.",
        &map,
    };
    FeatureInfo RefCountPipe = {
        "RefCountPipe",
        "If enabled, resources are referenced counted via a specific pipe "
        "implementation.",
        &map,
    };
    FeatureInfo VirtioGpuFenceContexts = {
        "VirtioGpuFenceContexts",
        "If enabled, the host will support multiple virtio gpu fence timelines.",
        &map,
    };
    FeatureInfo VirtioGpuNativeSync = {
        "VirtioGpuNativeSync",
        "If enabled, use virtio gpu instead of goldfish sync for sync fd support.",
        &map,
    };
    FeatureInfo VirtioGpuNext = {
        "VirtioGpuNext",
        "If enabled, virtio gpu supports blob resources (this was historically "
        "called on a virtio-gpu-next branch in upstream kernel?).",
        &map,
    };
    FeatureInfo BypassVulkanDeviceFeatureOverrides = {
        "BypassVulkanDeviceFeatureOverrides",
        "We are force disabling (overriding) some vulkan features (private data, uniform inline "
        "block etc) which the device may naturally support."
        "If toggled ON, this flag will cause the host side to not force disable anything and let "
        "the device fully advertise supported features.",
        &map,
    };
    FeatureInfo VulkanAllocateDeviceMemoryOnly = {
        "VulkanAllocateDeviceMemoryOnly",
        "If enabled, prevents the guest from allocating Vulkan memory that does "
        "not have VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT.",
        &map,
    };
    FeatureInfo VulkanAllocateHostMemory = {
        "VulkanAllocateHostMemory",
        "If enabled, allocates host private memory and uses "
        "VK_EXT_external_memory_host to handle Vulkan "
        "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT allocations.",
        &map,
    };
    FeatureInfo VulkanBatchedDescriptorSetUpdate = {
        "VulkanBatchedDescriptorSetUpdate",
        "If enabled, Vulkan descriptor set updates via vkUpdateDescriptorSets() are "
        "not immediately sent to the host and are instead deferred until needed "
        "in vkQueueSubmit() commands.",
        &map,
    };
    FeatureInfo VulkanIgnoredHandles = {
        "VulkanIgnoredHandles",
        "If enabled, the guest to host Vulkan protocol will ignore handles in some "
        "cases such as VkWriteDescriptorSet().",
        &map,
    };
    FeatureInfo VulkanNativeSwapchain = {
        "VulkanNativeSwapchain",
        "If enabled, the host display implementation uses a native Vulkan swapchain.",
        &map,
    };
    FeatureInfo VulkanNullOptionalStrings = {
        "VulkanNullOptionalStrings",
        "If enabled, the guest to host Vulkan protocol will encode null optional "
        "strings as actual null values instead of as empty strings.",
        &map,
    };
    FeatureInfo VulkanQueueSubmitWithCommands = {
        "VulkanQueueSubmitWithCommands",
        "If enabled, uses deferred command submission with global sequence number "
        "synchronization for Vulkan queue submits.",
        &map,
    };
    FeatureInfo VulkanShaderFloat16Int8 = {
        "VulkanShaderFloat16Int8",
        "If enabled, enables the VK_KHR_shader_float16_int8 extension.",
        &map,
    };
    FeatureInfo VulkanSnapshots = {
        "VulkanSnapshots",
        "If enabled, supports snapshotting the guest and host Vulkan state.",
        &map,
    };
    FeatureInfo VulkanUseDedicatedAhbMemoryType = {
        "VulkanUseDedicatedAhbMemoryType",
        "If enabled, emulates an additional memory type for AHardwareBuffer allocations "
        "that only has VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT for the purposes of preventing "
        "the guest from trying to map AHardwareBuffer memory.",
        &map,
    };
    FeatureInfo Vulkan = {
        "Vulkan",
        "If enabled, allows the guest to use Vulkan and enables the Vulkan backend "
        "on the host.",
        &map,
    };
    FeatureInfo Yuv420888ToNv21 = {
        "Yuv420888ToNv21",
        "If enabled, Androids HAL_PIXEL_FORMAT_YCbCr_420_888 format is treated as "
        "NV21.",
        &map,
    };
    FeatureInfo YuvCache = {
        "YuvCache",
        "If enabled, the host will cache YUV frames.",
        &map,
    };
    FeatureInfo VulkanDebugUtils = {
        "VulkanDebugUtils",
        "If enabled, the host will enable VK_EXT_debug_utils extension when available to use "
        "labels on Vulkan resources and operation",
        &map,
    };
    FeatureInfo VulkanCommandBufferCheckpoints = {
        "VulkanCommandBufferCheckpoints",
        "If enabled, the host will enable the VK_NV_device_diagnostic_checkpoints extension "
        "when available, track command buffers with markers, and report unfinished command "
        "buffers on device lost. (TODO: VK_AMD_buffer_marker)",
        &map,
    };
    FeatureInfo VulkanVirtualQueue = {
        "VulkanVirtualQueue",
        "(Experimental) If enabled, a virtual graphics queue will be added into physical Vulkan "
        "device properties for the guest queries.",
        &map,
    };
    FeatureInfo VulkanRobustness = {
        "VulkanRobustness",
        "If enabled, robustness extensions with all supported features will be enabled on "
        "all created devices. (e.g. VK_EXT_robustness2)",
        &map,
    };
};

#define GFXSTREAM_SET_FEATURE_ON_CONDITION(set, feature, condition) \
    do                                                              \
    {                                                               \
        {                                                           \
            (set)->feature.enabled = condition;                     \
            (set)->feature.reason = #condition;                     \
        }                                                           \
    } while (0)

}  // namespace host
}  // namespace gfxstream
