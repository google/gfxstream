// Copyright 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <GLES2/gl2.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BorrowedImageVk.h"
#include "CompositorVk.h"
#include "DebugUtilsHelper.h"
#include "DeviceLostHelper.h"
#include "DisplayVk.h"
#include "ExternalObjectManager.h"
#include "FrameworkFormats.h"
#include "aemu/base/Optional.h"
#include "aemu/base/ThreadAnnotations.h"
#include "gfxstream/host/BackendCallbacks.h"
#include "gfxstream/host/Features.h"
#include "goldfish_vk_private_defs.h"
#include "utils/GfxApiLogger.h"
#include "utils/RenderDoc.h"

#if defined(_WIN32)
typedef void* HANDLE;
// External sync objects are HANDLE on Windows
typedef HANDLE VK_EXT_SYNC_HANDLE;
// corresponds to INVALID_HANDLE_VALUE
#define VK_EXT_SYNC_HANDLE_INVALID (VK_EXT_SYNC_HANDLE)(uintptr_t)(-1)
#else
// External sync objects are fd's on other POSIX systems
typedef int VK_EXT_SYNC_HANDLE;
#define VK_EXT_SYNC_HANDLE_INVALID (-1)
#endif

namespace gfxstream {
namespace vk {

struct VulkanDispatch;

// Returns a consistent answer for which memory type index is best for staging
// memory. This is not the simplest thing in the world because even if a memory
// type index is host visible, that doesn't mean a VkBuffer is allowed to be
// associated with it.
bool getStagingMemoryTypeIndex(VulkanDispatch* vk, VkDevice device,
                               const VkPhysicalDeviceMemoryProperties* memProps,
                               uint32_t* typeIndex);

enum class AstcEmulationMode {
    Disabled,  // No ASTC emulation (ie: ASTC not supported unless the GPU supports it natively)
    Cpu,       // Decompress ASTC textures on the CPU
    Gpu,       // Decompress ASTC textures on the GPU
};

// Global state that holds a global Vulkan instance along with globally
// exported memory allocations + images. This is in order to service things
// like AndroidHardwareBuffer/FuchsiaImagePipeHandle. Each such allocation is
// associated with a ColorBuffer handle, and depending on host-side support for
// GL_EXT_memory_object, also be able to zero-copy render into and readback
// with the traditional GL pipeline.
class VkEmulation {
   public:
    ~VkEmulation();

    static std::unique_ptr<VkEmulation> create(VulkanDispatch* vk,
                                               gfxstream::host::BackendCallbacks callbacks,
                                               gfxstream::host::FeatureSet features);

    struct Features {
        bool glInteropSupported = false;
        bool deferredCommands = false;
        bool createResourceWithRequirements = false;
        bool useVulkanComposition = false;
        bool useVulkanNativeSwapchain = false;
        std::unique_ptr<emugl::RenderDocWithMultipleVkInstances> guestRenderDoc = nullptr;
        AstcEmulationMode astcLdrEmulationMode = AstcEmulationMode::Disabled;
        bool enableEtc2Emulation = false;
        bool enableYcbcrEmulation = false;
        bool guestVulkanOnly = false;
        bool useDedicatedAllocations = false;
    };
    void initFeatures(Features features);

    bool isYcbcrEmulationEnabled() const;

    bool isEtc2EmulationEnabled() const;

    bool deferredCommandsEnabled() const;
    bool createResourcesWithRequirementsEnabled() const;

    bool supportsExternalMemoryCapabilities() const;
    bool supportsExternalSemaphoreCapabilities() const;
    bool supportsExternalFenceCapabilities() const;
    bool supportsSurfaces() const;
    bool supportsMoltenVk() const;

    bool supportsGetPhysicalDeviceProperties2() const;

    bool supportsPhysicalDeviceIDProperties() const;

    std::optional<std::array<uint8_t, VK_UUID_SIZE>> getDeviceUuid();
    std::optional<std::array<uint8_t, VK_UUID_SIZE>> getDriverUuid();

    bool supportsPrivateData() const;

    bool supportsExternalMemoryImport() const;

    bool supportsDmaBuf() const;

    bool supportsExternalMemoryHostProperties() const;

    std::optional<VkPhysicalDeviceRobustness2FeaturesEXT> getRobustness2Features() const;

    VkPhysicalDeviceExternalMemoryHostPropertiesEXT externalMemoryHostProperties() const;

    bool isGuestVulkanOnly() const;

    bool commandBufferCheckpointsEnabled() const;

    bool supportsSamplerYcbcrConversion() const;

    bool debugUtilsEnabled() const;

    DebugUtilsHelper& getDebugUtilsHelper();

    DeviceLostHelper& getDeviceLostHelper();

    const gfxstream::host::FeatureSet& getFeatures() const;

    const gfxstream::host::BackendCallbacks& getCallbacks() const;

    AstcEmulationMode getAstcLdrEmulationMode() const;

    emugl::RenderDocWithMultipleVkInstances* getRenderDoc();

    Compositor* getCompositor();

    DisplayVk* getDisplay();

    VkInstance getInstance();

    std::string getGpuVendor() const;
    std::string getGpuName() const;
    std::string getGpuVersionString() const;
    std::string getInstanceExtensionsString() const;
    std::string getDeviceExtensionsString() const;

    const VkPhysicalDeviceProperties getPhysicalDeviceProperties() const;

    struct RepresentativeColorBufferMemoryTypeInfo {
        // The host memory type index used for Buffer/ColorBuffer allocations.
        uint32_t hostMemoryTypeIndex;

        // The guest memory type index that will be returned to guest processes querying
        // the memory type index of host AHardwareBuffer/ColorBuffer allocations. This may
        // point to an emulated memory type so that the host can control which memory flags are
        // exposed to the guest (i.e. hide VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT from the guest).
        uint32_t guestMemoryTypeIndex;
    };
    RepresentativeColorBufferMemoryTypeInfo getRepresentativeColorBufferMemoryTypeInfo() const;

    void onVkDeviceLost();

    VkExternalMemoryHandleTypeFlagBits getDefaultExternalMemoryHandleType();

    std::unique_ptr<gfxstream::DisplaySurface> createDisplaySurface(FBNativeWindowType window,
                                                                    uint32_t width,
                                                                    uint32_t height);

    // ColorBuffer operations

    bool getColorBufferShareInfo(uint32_t colorBufferHandle, bool* glExported,
                                 bool* externalMemoryCompatible);

    bool getColorBufferAllocationInfo(uint32_t colorBufferHandle, VkDeviceSize* outSize,
                                      uint32_t* outMemoryTypeIndex, bool* outMemoryIsDedicatedAlloc,
                                      void** outMappedPtr);

    std::unique_ptr<VkImageCreateInfo> generateColorBufferVkImageCreateInfo(VkFormat format,
                                                                            uint32_t width,
                                                                            uint32_t height,
                                                                            VkImageTiling tiling);

    bool isFormatSupported(GLenum format);

    bool createVkColorBuffer(uint32_t width, uint32_t height, GLenum format,
                             FrameworkFormat frameworkFormat, uint32_t colorBufferHandle,
                             bool vulkanOnly, uint32_t memoryProperty);

    bool teardownVkColorBuffer(uint32_t colorBufferHandle);

    struct ExternalMemoryInfo {
        // Input fields
        VkDeviceSize size;
        uint32_t typeIndex;

        // Output fields
        uint32_t id = 0;
        VkDeviceMemory memory = VK_NULL_HANDLE;

        // host-mapping fields
        // host virtual address (hva).
        void* mappedPtr = nullptr;
        // host virtual address, aligned to 4KB page.
        void* pageAlignedHva = nullptr;
        // the offset of |mappedPtr| off its memory page.
        uint32_t pageOffset = 0u;
        // the offset set in |vkBindImageMemory| or |vkBindBufferMemory|.
        uint32_t bindOffset = 0u;
        // the size of all the pages the memory uses.
        size_t sizeToPage = 0u;
        // guest physical address.
        uintptr_t gpa = 0u;

        std::optional<ExternalHandleInfo> handleInfo = std::nullopt;
#ifdef __APPLE__
        // This is used as an external handle when MoltenVK is enabled
        MTLResource_id externalMetalHandle = nullptr;
#endif

        bool dedicatedAllocation = false;
    };

    bool allocExternalMemory(
        VulkanDispatch* vk, ExternalMemoryInfo* info, bool actuallyExternal = true,
        android::base::Optional<uint64_t> deviceAlignment = android::base::kNullopt,
        android::base::Optional<VkBuffer> bufferForDedicatedAllocation = android::base::kNullopt,
        android::base::Optional<VkImage> imageForDedicatedAllocation = android::base::kNullopt);

    bool importExternalMemory(VulkanDispatch* vk, VkDevice targetDevice,
                              const ExternalMemoryInfo* info,
                              VkMemoryDedicatedAllocateInfo* dedicatedAllocInfo,
                              VkDeviceMemory* out);

    enum class VulkanMode {
        // Default: ColorBuffers can still be used with the existing GL-based
        // API.  Synchronization with (if it exists) Vulkan images happens on
        // every one of the GL-based API calls:
        //
        // rcReadColorBuffer
        // rcUpdateColorBuffer
        // rcBindTexture
        // rcBindRenderbuffer
        // rcFlushWindowColorBuffer
        //
        // either through explicit CPU copies or implicit in the host driver
        // if OpenGL interop is supported.
        //
        // When images are posted (rcFBPost),
        // eglSwapBuffers is used, even if that requires a CPU readback.

        Default = 0,

        // VulkanOnly: It is assumed that the guest interacts entirely with
        // the underlying Vulkan image in the guest and does not use the
        // GL-based API.  This means we can assume those APIs are not called:
        //
        // rcReadColorBuffer
        // rcUpdateColorBuffer
        // rcBindTexture
        // rcBindRenderbuffer
        // rcFlushWindowColorBuffer
        //
        // and thus we skip a lot of GL/Vk synchronization.
        //
        // When images are posted, eglSwapBuffers is only used if OpenGL
        // interop is supported. If OpenGL interop is not supported, then we
        // use a host platform-specific Vulkan swapchain to display the
        // results.

        VulkanOnly = 1,
    };

    struct ColorBufferInfo {
        ExternalMemoryInfo memory;

        uint32_t handle;

        /* Set in create(), before initialize() */
        uint32_t width;
        uint32_t height;
        GLenum internalFormat;
        uint32_t memoryProperty;
        int frameworkFormat;
        int frameworkStride;
        bool initialized = false;

        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSamplerYcbcrConversion ycbcrConversion = VK_NULL_HANDLE;
        VkImageCreateInfo imageCreateInfoShallow = {};

        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t currentQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;

        bool glExported = false;
        bool externalMemoryCompatible = false;

        VulkanMode vulkanMode = VulkanMode::Default;
    };
    std::optional<VkEmulation::ColorBufferInfo> getColorBufferInfo(uint32_t colorBufferHandle);

    struct BufferInfo {
        ExternalMemoryInfo memory;
        uint32_t handle;

        VkDeviceSize size;
        VkBufferCreateFlags createFlags;
        VkBufferUsageFlags usageFlags;
        VkSharingMode sharingMode;

        VkBuffer buffer = VK_NULL_HANDLE;

        bool glExported = false;
        VulkanMode vulkanMode = VulkanMode::Default;
    };

    std::optional<ExternalHandleInfo> dupColorBufferExtMemoryHandle(uint32_t colorBufferHandle);
#ifdef __APPLE__
    MTLResource_id getColorBufferMetalMemoryHandle(uint32_t colorBufferHandle);
    VkImage getColorBufferVkImage(uint32_t colorBufferHandle);
#endif

    struct VkColorBufferMemoryExport {
        ExternalHandleInfo handleInfo;
        uint64_t size = 0;
        bool linearTiling = false;
        bool dedicatedAllocation = false;
    };
    std::optional<VkColorBufferMemoryExport> exportColorBufferMemory(uint32_t colorBufferHandle);

    bool setColorBufferVulkanMode(uint32_t colorBufferHandle, uint32_t vulkanMode);
    int32_t mapGpaToBufferHandle(uint32_t bufferHandle, uint64_t gpa, uint64_t size = 0);

    bool colorBufferNeedsUpdateBetweenGlAndVk(uint32_t colorBufferHandle);

    bool readColorBufferToBytes(uint32_t colorBufferHandle, std::vector<uint8_t>* bytes);
    bool readColorBufferToBytes(uint32_t colorBufferHandle, uint32_t x, uint32_t y, uint32_t w,
                                uint32_t h, void* outPixels, uint64_t outPixelsSize);

    bool updateColorBufferFromBytes(uint32_t colorBufferHandle, const std::vector<uint8_t>& bytes);
    bool updateColorBufferFromBytes(uint32_t colorBufferHandle, uint32_t x, uint32_t y, uint32_t w,
                                    uint32_t h, const void* pixels);

    // Data buffer operations
    bool getBufferAllocationInfo(uint32_t bufferHandle, VkDeviceSize* outSize,
                                 uint32_t* outMemoryTypeIndex, bool* outMemoryIsDedicatedAlloc);

    bool setupVkBuffer(uint64_t size, uint32_t bufferHandle, bool vulkanOnly = false,
                       uint32_t memoryProperty = 0);
    bool teardownVkBuffer(uint32_t bufferHandle);

    std::optional<GenericDescriptorInfo> exportMemoryHandle(VkDevice device, VkDeviceMemory memory);
    std::optional<ExternalHandleInfo> dupBufferExtMemoryHandle(uint32_t bufferHandle);
#ifdef __APPLE__
    MTLResource_id getBufferMetalMemoryHandle(uint32_t bufferHandle);
    MTLResource_id getMtlResourceFromVkDeviceMemory(VulkanDispatch* vk, VkDeviceMemory memory);
#endif

    bool readBufferToBytes(uint32_t bufferHandle, uint64_t offset, uint64_t size, void* outBytes);
    bool updateBufferFromBytes(uint32_t bufferHandle, uint64_t offset, uint64_t size,
                               const void* bytes);

    VkExternalMemoryHandleTypeFlags transformExternalMemoryHandleTypeFlags_tohost(
        VkExternalMemoryHandleTypeFlags bits);

    VkExternalMemoryHandleTypeFlags transformExternalMemoryHandleTypeFlags_fromhost(
        VkExternalMemoryHandleTypeFlags hostBits,
        VkExternalMemoryHandleTypeFlags wantedGuestHandleType);

    VkExternalMemoryProperties transformExternalMemoryProperties_tohost(
        VkExternalMemoryProperties props);

    VkExternalMemoryProperties transformExternalMemoryProperties_fromhost(
        VkExternalMemoryProperties props, VkExternalMemoryHandleTypeFlags wantedGuestHandleType);

    void setColorBufferCurrentLayout(uint32_t colorBufferHandle, VkImageLayout);

    VkImageLayout getColorBufferCurrentLayout(uint32_t colorBufferHandle);

    void releaseColorBufferForGuestUse(uint32_t colorBufferHandle);

    std::unique_ptr<BorrowedImageInfoVk> borrowColorBufferForComposition(uint32_t colorBufferHandle,
                                                                         bool colorBufferIsTarget);
    std::unique_ptr<BorrowedImageInfoVk> borrowColorBufferForDisplay(uint32_t colorBufferHandle);

   private:
    VkEmulation() = default;

    std::optional<RepresentativeColorBufferMemoryTypeInfo>
    findRepresentativeColorBufferMemoryTypeIndexLocked() REQUIRES(mMutex);

    struct ImageSupportInfo {
        // Input parameters
        VkFormat format;
        VkImageType type;
        VkImageTiling tiling;
        VkImageUsageFlags usageFlags;
        VkImageCreateFlags createFlags;

        // Output parameters
        bool supported = false;
        bool supportsExternalMemory = false;
        bool requiresDedicatedAllocation = false;

        // Keep the raw output around.
        VkFormatProperties2 formatProps2;
        VkImageFormatProperties2 imageFormatProps2;
        VkExternalImageFormatProperties extFormatProps;
    };

    static std::vector<VkEmulation::ImageSupportInfo> getBasicImageSupportList();

    // For a given ImageSupportInfo, populates usageWithExternalHandles and
    // requiresDedicatedAllocation. memoryTypeBits are populated later once the
    // device is created, because that needs a test image to be created.
    // If we don't support external memory, it's assumed dedicated allocations are
    // not needed.
    // Precondition: sVkEmulation instance has been created and ext memory caps known.
    // Returns false if the query failed.
    bool populateImageFormatExternalMemorySupportInfo(VulkanDispatch* vk, VkPhysicalDevice physdev,
                                                      ImageSupportInfo* info);

    struct DeviceSupportInfo {
        bool hasGraphicsQueueFamily = false;
        bool hasComputeQueueFamily = false;
        bool supportsExternalMemoryImport = false;
        bool supportsExternalMemoryExport = false;
        bool supportsDmaBuf = false;
        bool supportsDriverProperties = false;
        bool supportsExternalMemoryHostProps = false;
        bool hasSamplerYcbcrConversionExtension = false;
        bool supportsSamplerYcbcrConversion = false;
        bool glInteropSupported = false;
        bool hasNvidiaDeviceDiagnosticCheckpointsExtension = false;
        bool supportsNvidiaDeviceDiagnosticCheckpoints = false;
        bool supportsPrivateData = false;

        std::vector<VkExtensionProperties> extensions;

        std::vector<uint32_t> graphicsQueueFamilyIndices;
        std::vector<uint32_t> computeQueueFamilyIndices;

        VkPhysicalDeviceProperties physdevProps;
        VkPhysicalDeviceMemoryProperties memProps;
        VkPhysicalDeviceIDPropertiesKHR idProps;
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT externalMemoryHostProps;

        std::string driverVendor;
        std::string driverVersion;

        PFN_vkGetImageMemoryRequirements2KHR getImageMemoryRequirements2Func = nullptr;
        PFN_vkGetBufferMemoryRequirements2KHR getBufferMemoryRequirements2Func = nullptr;

#ifdef _WIN32
        PFN_vkGetMemoryWin32HandleKHR getMemoryHandleFunc = nullptr;
#else
        PFN_vkGetMemoryFdKHR getMemoryHandleFunc = nullptr;
#endif

        // Set only if requested and supported
        std::optional<VkPhysicalDeviceRobustness2FeaturesEXT> robustness2Features;
    };

    uint32_t getValidMemoryTypeIndex(uint32_t requiredMemoryTypeBits,
                                     VkMemoryPropertyFlags memoryProperty = 0);

    int getSelectedGpuIndex(const std::vector<DeviceSupportInfo>& deviceInfos);

    bool isFormatVulkanCompatible(GLenum internalFormat);

    bool getColorBufferAllocationInfoLocked(uint32_t colorBufferHandle, VkDeviceSize* outSize,
                                            uint32_t* outMemoryTypeIndex,
                                            bool* outMemoryIsDedicatedAlloc, void** outMappedPtr) REQUIRES(mMutex);

    std::unique_ptr<VkImageCreateInfo> generateColorBufferVkImageCreateInfoLocked(
        VkFormat format, uint32_t width, uint32_t height, VkImageTiling tiling) REQUIRES(mMutex);

    bool createVkColorBufferLocked(uint32_t width, uint32_t height, GLenum internalFormat,
                                   FrameworkFormat frameworkFormat, uint32_t colorBufferHandle,
                                   bool vulkanOnly, uint32_t memoryProperty) REQUIRES(mMutex);

    bool teardownVkColorBufferLocked(uint32_t colorBufferHandle) REQUIRES(mMutex);

    bool colorBufferNeedsUpdateBetweenGlAndVk(const VkEmulation::ColorBufferInfo& colorBufferInfo);

    bool readColorBufferToBytesLocked(uint32_t colorBufferHandle, uint32_t x, uint32_t y,
                                      uint32_t w, uint32_t h, void* outPixels,
                                      uint64_t outPixelsSize) REQUIRES(mMutex);

    bool updateColorBufferFromBytesLocked(uint32_t colorBufferHandle, uint32_t x, uint32_t y,
                                          uint32_t w, uint32_t h, const void* pixels,
                                          size_t inputPixelsSize) REQUIRES(mMutex);

    bool updateMemReqsForExtMem(std::optional<ExternalHandleInfo> extMemHandleInfo,
                                VkMemoryRequirements* pMemReqs);

    std::tuple<VkCommandBuffer, VkFence> allocateQueueTransferCommandBufferLocked() REQUIRES(mMutex);

    void freeExternalMemoryLocked(VulkanDispatch* vk, ExternalMemoryInfo* info) REQUIRES(mMutex);

    std::mutex mMutex;

    gfxstream::host::BackendCallbacks mCallbacks;

    gfxstream::host::FeatureSet mFeatures;

    RepresentativeColorBufferMemoryTypeInfo mRepresentativeColorBufferMemoryTypeInfo;

    // Whether to use deferred command submission.
    bool mUseDeferredCommands = false;

    // Whether to fuse memory requirements getting with resource creation.
    bool mUseCreateResourcesWithRequirements = false;

    // RenderDoc integration for guest VkInstances.
    std::unique_ptr<emugl::RenderDocWithMultipleVkInstances> mGuestRenderDoc;

    // Whether to use ASTC emulation. Our current ASTC decoder implementation may lead to device
    // lost on certain device on Windows.
    AstcEmulationMode mAstcLdrEmulationMode = AstcEmulationMode::Disabled;

    // Whether to use ETC2 emulation.
    bool mEnableEtc2Emulation = false;

    // Whether to use Ycbcr emulation. If this feature is turned on, Ycbcr request will always use
    // the emulation path regardless of whether the host Vulkan driver actually supports Ycbcr
    // conversion or not.
    bool mEnableYcbcrEmulation = false;

    bool mGuestVulkanOnly = false;

    bool mUseDedicatedAllocations = false;

    // Instance and device for creating the system-wide shareable objects.
    VkInstance mInstance = VK_NULL_HANDLE;
    uint32_t mVulkanInstanceVersion = 0;
    std::vector<VkExtensionProperties> mInstanceExtensions;

    uint32_t mPhysicalDeviceIndex = 0;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;

    VkDevice mDevice = VK_NULL_HANDLE;

    // Global, instance and device dispatch tables.
    VulkanDispatch* mGvk = nullptr;
    VulkanDispatch* mIvk = nullptr;
    VulkanDispatch* mDvk = nullptr;

    bool mInstanceSupportsPhysicalDeviceIDProperties = false;
    bool mInstanceSupportsGetPhysicalDeviceProperties2 = false;
    bool mInstanceSupportsExternalMemoryCapabilities = false;
    bool mInstanceSupportsExternalSemaphoreCapabilities = false;
    bool mInstanceSupportsExternalFenceCapabilities = false;
    bool mInstanceSupportsSurface = false;
#if defined(__APPLE__)
    bool mInstanceSupportsMoltenVK = false;
#else
    static const bool mInstanceSupportsMoltenVK = false;
#endif

    PFN_vkGetPhysicalDeviceImageFormatProperties2KHR mGetImageFormatProperties2Func = nullptr;
    PFN_vkGetPhysicalDeviceProperties2KHR mGetPhysicalDeviceProperties2Func = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 mGetPhysicalDeviceFeatures2Func = nullptr;

    bool mDebugUtilsAvailableAndRequested = false;
    DebugUtilsHelper mDebugUtilsHelper = DebugUtilsHelper::withUtilsDisabled();

    bool mCommandBufferCheckpointsSupportedAndRequested = false;
    DeviceLostHelper mDeviceLostHelper{};

    // Queue, command pool, and command buffer
    // for running commands to sync stuff system-wide.
    // TODO(b/197362803): Encapsulate host side VkQueue and the lock.
    VkQueue mQueue = VK_NULL_HANDLE;
    std::shared_ptr<android::base::Lock> mQueueLock = nullptr;
    uint32_t mQueueFamilyIndex = 0;

    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
    VkFence mCommandBufferFence = VK_NULL_HANDLE;

    std::vector<ImageSupportInfo> mImageSupportInfo;

    // 128 mb staging buffer (really, just a few 4K frames or one 4k HDR frame)
    // ought to be big enough for anybody!
    static constexpr VkDeviceSize kDefaultStagingBufferSize = 128ULL * 1048576ULL;

    struct StagingBuffer {
        VkDeviceMemory mMemory = VK_NULL_HANDLE;
        VkBuffer mBuffer = VK_NULL_HANDLE;
        VkDeviceSize mAllocationSize;
        void* mMappedPtr = nullptr;
        bool mIsHostCoherent = false;

        bool create(VulkanDispatch* vk, VkDevice device,
            const VkPhysicalDeviceMemoryProperties* memProps,
            const DebugUtilsHelper& debugUtilsHelper,
            const VkDeviceSize size);
        void destroy(VulkanDispatch* dvk, VkDevice device);
    };

    // Track what is supported on whatever device was selected.
    DeviceSupportInfo mDeviceInfo;

    // Staging buffer to perform reads and updates on color buffers.
    StagingBuffer mStaging GUARDED_BY(mMutex);

    // ColorBuffers are intended to back the guest's shareable images.
    // For example:
    // Android: gralloc
    // Fuchsia: ImagePipeHandle
    // Linux: dmabuf
    std::unordered_map<uint32_t, ColorBufferInfo> mColorBuffers GUARDED_BY(mMutex);

    // Buffers are intended to back the guest's shareable Vulkan buffers.
    std::unordered_map<uint32_t, BufferInfo> mBuffers GUARDED_BY(mMutex);

    // In order to support VK_KHR_external_memory_(fd|win32) we need also to
    // support the concept of plain external memories that are just memory and
    // not necessarily images. These are then intended to pass through to the
    // guest in some way, with 1:1 mapping between guest and host external
    // memory handles.
    std::unordered_map<uint32_t, ExternalMemoryInfo> mExternalMemories GUARDED_BY(mMutex);

    // The host keeps a set of occupied guest memory addresses to avoid a
    // host memory address mapped to guest twice.
    std::unordered_set<uint64_t> mOccupiedGpas GUARDED_BY(mMutex);

    // We can also consider using a single external memory object to back all
    // host visible allocations in the guest. This would save memory, but we
    // would also need to automatically add
    // VkExternalMemory(Image|Buffer)CreateInfo, or if it is already there, OR
    // it with the handle types on the host.
    // A rough sketch: Some memories/images/buffers in the guest
    // are backed by host visible memory:
    // There is already a virtual memory type for those things in the current
    // implementation. The guest doesn't know whether the pointer or the
    // VkDeviceMemory object is backed by host external or non external.
    // TODO: are all possible buffer / image usages compatible with
    // external backing?
    // TODO: try switching to this
    ExternalMemoryInfo mVirtualHostVisibleHeap;

    // Every command buffer in the pool is associated with a VkFence which is
    // signaled only if the command buffer completes.
    std::vector<std::tuple<VkCommandBuffer, VkFence>> mTransferQueueCommandBufferPool GUARDED_BY(mMutex);

    std::unique_ptr<CompositorVk> mCompositorVk;

    // The implementation for Vulkan native swapchain. Only initialized in initVkEmulationFeatures
    // if useVulkanNativeSwapchain is set.
    std::unique_ptr<DisplayVk> mDisplayVk;
};

}  // namespace vk
}  // namespace gfxstream
