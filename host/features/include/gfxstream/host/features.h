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

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gfxstream {
namespace host {

class FeatureInfoBase;

using FeatureMap = std::map<std::string, FeatureInfoBase*>;
// The potential types for the "value" of a given feature
using StringFeatureValue = std::optional<std::string>;
using FeatureValue = std::variant<bool, StringFeatureValue>;

class FeatureInfoBase {
   public:
    void setReason(std::string reasonStr) { reason = reasonStr; }
    const std::string& getReason() const { return reason; }
    const std::string& getName() const { return name; }

    virtual bool parseValue(std::string_view strValue) = 0;
    virtual std::string getValueReadable() const = 0;

   protected:
    FeatureInfoBase(const FeatureInfoBase& rhs) = default;

    FeatureInfoBase(std::string_view name, std::string_view description, FeatureMap* map,
                    FeatureValue initialValue)
        : name(name), description(description), reason("Default reason"), value(initialValue) {
        if (map) {
            (*map)[std::string(name)] = this;
        }
    }

    virtual ~FeatureInfoBase() = default;

    std::string name;
    std::string description;
    std::string reason;
    FeatureValue value;
};

class BoolFeatureInfo : public FeatureInfoBase {
public:
 BoolFeatureInfo(std::string_view name, std::string_view description, FeatureMap* map)
     : FeatureInfoBase(name, description, map, false) {}

 void setEnabled(bool enabled) { value = enabled; }
 bool enabled() const { return std::get<bool>(value); }

 bool parseValue(std::string_view strValue) {
     if (strValue != "enabled" && strValue != "disabled") {
         return false;
     }

     setEnabled(strValue == "enabled");
     return true;
 }

 std::string getValueReadable() const {
     if (std::get<bool>(value)) {
         return "enabled";
     } else {
         return "disabled";
     }
 }
};

class StringFeatureInfo : public FeatureInfoBase {
public:
 StringFeatureInfo(std::string_view name, std::string_view description, FeatureMap* map)
     : FeatureInfoBase(name, description, map, std::nullopt) {}

 StringFeatureValue getValue() const { return std::get<StringFeatureValue>(value); }

 bool parseValue(std::string_view strValue) {
     value = StringFeatureValue(strValue);
     return true;
 }

 std::string getValueReadable() const {
     auto strValueOpt = std::get<StringFeatureValue>(value);
     if (strValueOpt) {
         return *strValueOpt;
     } else {
         return "(Unset)";
     }
 }
};

struct FeatureSet {
    FeatureSet() = default;

    FeatureSet(const FeatureSet& rhs);
    FeatureSet& operator=(const FeatureSet& rhs);

    bool processFeatureString(std::string featureStr, std::string featureReason);

    FeatureMap map;

    // This represents the maximum vulkan api version that should be reported to the guest and is
    // not related to the host vulkan level available or used.
    std::optional<uint32_t> guestVulkanMaxApiVersion;

    BoolFeatureInfo AsyncComposeSupport = {
        "AsyncComposeSupport",
        "If enabled, allows the guest to use asynchronous render control commands "
        "to compose and post frame buffers.",
        &map,
    };
    BoolFeatureInfo EglOnEgl = {
        "EglOnEgl",
        "If enabled, the GLES translator will layer on the host's EGL.",
        &map,
    };
    BoolFeatureInfo ExternalBlob = {
        "ExternalBlob",
        "If enabled, virtio gpu blob resources will be allocated with external "
        "memory and will be exportable via file descriptors.",
        &map,
    };
    BoolFeatureInfo VulkanExternalSync = {
        "VulkanExternalSync",
        "If enabled, Vulkan fences/semaphores will be allocated with external "
        "create info and will be exportable via fence handles.",
        &map,
    };
    BoolFeatureInfo SystemBlob = {
        "SystemBlob",
        "If enabled, virtio gpu blob resources will be allocated with shmem and "
        "will be exportable via file descriptors.",
        &map,
    };
    BoolFeatureInfo GlAsyncSwap = {
        "GlAsyncSwap",
        "If enabled, uses the host GL driver's fence commands and fence file "
        "descriptors in the guest to have explicit signals of buffer swap "
        "completion.",
        &map,
    };
    BoolFeatureInfo GlDirectMem = {
        "GlDirectMem",
        "If enabled, allows mapping the host address from glMapBufferRange() into "
        "the guest.",
        &map,
    };
    BoolFeatureInfo GlDma = {
        "GlDma",
        "Default description: consider contributing a description if you see this!",
        &map,
    };
    BoolFeatureInfo GlDma2 = {
        "GlDma2",
        "Default description: consider contributing a description if you see this!",
        &map,
    };
    BoolFeatureInfo GlProgramBinaryLinkStatus = {
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
    BoolFeatureInfo GlPipeChecksum = {
        "GlPipeChecksum",
        "If enabled, the guest and host will use checksums to ensure consistency "
        "for GL calls between the guest and host.",
        &map,
    };
    BoolFeatureInfo GlesDynamicVersion = {
        "GlesDynamicVersion",
        "If enabled, attempts to detect and use the maximum supported GLES version "
        "from the host.",
        &map,
    };
    BoolFeatureInfo GrallocSync = {
        "GrallocSync",
        "If enabled, adds additional synchronization on the host for cases where "
        "a guest app may directly writing to gralloc buffers and posting.",
        &map,
    };
    BoolFeatureInfo GuestVulkanOnly = {
        "GuestVulkanOnly",
        "If enabled, indicates that the guest only requires Vulkan translation. "
        " The guest will not use GL and the host will not enable the GL backend. "
        " This is the case when the guest uses libraries such as Angle or Zink for "
        " GL to Vulkan translation.",
        &map,
    };
    BoolFeatureInfo HasSharedSlotsHostMemoryAllocator = {
        "HasSharedSlotsHostMemoryAllocator",
        "If enabled, the host supports "
        "AddressSpaceSharedSlotsHostMemoryAllocatorContext.",
        &map,
    };
    BoolFeatureInfo HostComposition = {
        "HostComposition",
        "If enabled, the host supports composition via render control commands.",
        &map,
    };
    BoolFeatureInfo HwcMultiConfigs = {
        "HwcMultiConfigs",
        "If enabled, the host supports multiple HWComposer configs per display.",
        &map,
    };
    BoolFeatureInfo Minigbm = {
        "Minigbm",
        "If enabled, the guest is known to be using Minigbm as its Gralloc "
        "implementation.",
        &map,
    };
    BoolFeatureInfo Surfaceless = {
        "Surfaceless",
        "If enabled, Gfxstream will run in surfaceless mode and will not depend "
        "on VK_KHR_swapchain or other surface-related extensions.",
        &map,
    };
    BoolFeatureInfo MinimalLogging = {
        "MinimalLogging",
        "If enabled, Gfxstream will log less info. Useful for preventing logspam "
        "CI which frequently starts and stops Gfxstream.",
        &map,
    };
    BoolFeatureInfo NativeTextureDecompression = {
        "NativeTextureDecompression",
        "If enabled, allows the host to use ASTC and ETC2 formats when supported by "
        " the host GL driver.",
        &map,
    };
    BoolFeatureInfo NoDelayCloseColorBuffer = {
        "NoDelayCloseColorBuffer",
        "If enabled, indicates that the guest properly associates resources with "
        "guest OS handles and that the host resources can be immediately cleaned "
        "upon receiving resource clean up commands.",
        &map,
    };
    BoolFeatureInfo RefCountPipe = {
        "RefCountPipe",
        "If enabled, resources are referenced counted via a specific pipe "
        "implementation.",
        &map,
    };
    BoolFeatureInfo VirtioGpuFenceContexts = {
        "VirtioGpuFenceContexts",
        "If enabled, the host will support multiple virtio gpu fence timelines.",
        &map,
    };
    BoolFeatureInfo VirtioGpuNativeSync = {
        "VirtioGpuNativeSync",
        "If enabled, use virtio gpu instead of goldfish sync for sync fd support.",
        &map,
    };
    BoolFeatureInfo VirtioGpuNext = {
        "VirtioGpuNext",
        "If enabled, virtio gpu supports blob resources (this was historically "
        "called on a virtio-gpu-next branch in upstream kernel?).",
        &map,
    };
    BoolFeatureInfo BypassVulkanDeviceFeatureOverrides = {
        "BypassVulkanDeviceFeatureOverrides",
        "We are force disabling (overriding) some vulkan features (private data, uniform inline "
        "block etc) which the device may naturally support."
        "If toggled ON, this flag will cause the host side to not force disable anything and let "
        "the device fully advertise supported features.",
        &map,
    };
    BoolFeatureInfo VulkanAllocateDeviceMemoryOnly = {
        "VulkanAllocateDeviceMemoryOnly",
        "If enabled, prevents the guest from allocating Vulkan memory that does "
        "not have VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT.",
        &map,
    };
    BoolFeatureInfo VulkanAllocateHostMemory = {
        "VulkanAllocateHostMemory",
        "If enabled, allocates host private memory and uses "
        "VK_EXT_external_memory_host to handle Vulkan "
        "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT allocations.",
        &map,
    };
    BoolFeatureInfo VulkanBatchedDescriptorSetUpdate = {
        "VulkanBatchedDescriptorSetUpdate",
        "If enabled, Vulkan descriptor set updates via vkUpdateDescriptorSets() are "
        "not immediately sent to the host and are instead deferred until needed "
        "in vkQueueSubmit() commands.",
        &map,
    };
    BoolFeatureInfo VulkanIgnoredHandles = {
        "VulkanIgnoredHandles",
        "If enabled, the guest to host Vulkan protocol will ignore handles in some "
        "cases such as VkWriteDescriptorSet().",
        &map,
    };
    BoolFeatureInfo VulkanNativeSwapchain = {
        "VulkanNativeSwapchain",
        "If enabled, the host display implementation uses a native Vulkan swapchain.",
        &map,
    };
    BoolFeatureInfo VulkanNullOptionalStrings = {
        "VulkanNullOptionalStrings",
        "If enabled, the guest to host Vulkan protocol will encode null optional "
        "strings as actual null values instead of as empty strings.",
        &map,
    };
    BoolFeatureInfo VulkanQueueSubmitWithCommands = {
        "VulkanQueueSubmitWithCommands",
        "If enabled, uses deferred command submission with global sequence number "
        "synchronization for Vulkan queue submits.",
        &map,
    };
    BoolFeatureInfo VulkanShaderFloat16Int8 = {
        "VulkanShaderFloat16Int8",
        "If enabled, enables the VK_KHR_shader_float16_int8 extension.",
        &map,
    };
    BoolFeatureInfo VulkanSnapshots = {
        "VulkanSnapshots",
        "If enabled, supports snapshotting the guest and host Vulkan state.",
        &map,
    };
    BoolFeatureInfo VulkanUseDedicatedAhbMemoryType = {
        "VulkanUseDedicatedAhbMemoryType",
        "If enabled, emulates an additional memory type for AHardwareBuffer allocations "
        "that only has VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT for the purposes of preventing "
        "the guest from trying to map AHardwareBuffer memory.",
        &map,
    };
    BoolFeatureInfo Vulkan = {
        "Vulkan",
        "If enabled, allows the guest to use Vulkan and enables the Vulkan backend "
        "on the host.",
        &map,
    };
    BoolFeatureInfo Yuv420888ToNv21 = {
        "Yuv420888ToNv21",
        "If enabled, Androids HAL_PIXEL_FORMAT_YCbCr_420_888 format is treated as "
        "NV21.",
        &map,
    };
    BoolFeatureInfo YuvCache = {
        "YuvCache",
        "If enabled, the host will cache YUV frames.",
        &map,
    };
    BoolFeatureInfo VulkanDebugUtils = {
        "VulkanDebugUtils",
        "If enabled, the host will enable VK_EXT_debug_utils extension when available to use "
        "labels on Vulkan resources and operation",
        &map,
    };
    BoolFeatureInfo VulkanCommandBufferCheckpoints = {
        "VulkanCommandBufferCheckpoints",
        "If enabled, the host will enable the VK_NV_device_diagnostic_checkpoints extension "
        "when available, track command buffers with markers, and report unfinished command "
        "buffers on device lost. (TODO: VK_AMD_buffer_marker)",
        &map,
    };
    BoolFeatureInfo VulkanVirtualQueue = {
        "VulkanVirtualQueue",
        "(Experimental) If enabled, a virtual graphics queue will be added into physical Vulkan "
        "device properties for the guest queries.",
        &map,
    };
    BoolFeatureInfo VulkanRobustness = {
        "VulkanRobustness",
        "If enabled, robustness extensions with all supported features will be enabled on "
        "all created devices. (e.g. VK_EXT_robustness2)",
        &map,
    };
    BoolFeatureInfo VulkanDisableCoherentMemoryAndEmulate = {
        "VulkanDisableCoherentMemoryAndEmulate",
        "If enabled, cached memory is reported as coherent memory to the guest and the host "
        "performs additional `vkFlushMappedMemoryRanges()` calls during queue submits to emulate.",
        &map,
    };
    BoolFeatureInfo VulkanAllocateHostVisibleAsUdmabuf = {
        "VulkanAllocateHostVisibleAsUdmabuf",
        "If enabled, backs blob memory via udmabuf that can be used with vkImportMemory",
        &map,
    };
    BoolFeatureInfo VulkanEnsureCachedCoherentMemoryAvailable = {
        "VulkanEnsureCachedCoherentMemoryAvailable",
        "If enabled, ensures that the at least one memory type that is both cached and coherent is "
        "advertised to the guest. In the absence of any cached-coherent memory reported by host "
        "driver, the first available "
        "coherent memoryType will also be marked as cached before, being advertised to the guest. "
        "This provides some "
        "app-compatiblity for common graphics layers in the guest.",
        &map,
    };
    StringFeatureInfo VulkanExternalMemoryMode = {
        "VulkanExternalMemoryMode",
        "A string specifying the ExternalMemoryMode for VkEmulation to use, "
        "which overrides what would otherwise be determined automatically based on the platform "
        "and the available Vulkan driver extensions.",
        &map,
    };
    BoolFeatureInfo VulkanProtectedMemoryEmulation = {
        "VulkanProtectedMemoryEmulation",
        "If enabled, enables protected memory emulation for the guest.",
        &map,
    };
};

#define GFXSTREAM_SET_BOOL_FEATURE_ON_CONDITION(set, feature, condition) \
    do {                                                                 \
        {                                                                \
            (set)->feature.setEnabled(condition);                        \
            (set)->feature.setReason(#condition);                        \
        }                                                                \
    } while (0)

}  // namespace host
}  // namespace gfxstream
