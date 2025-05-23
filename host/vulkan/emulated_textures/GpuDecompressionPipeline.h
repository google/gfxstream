// Copyright 2023 The Android Open Source Project
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
#include <memory>
#include <unordered_map>

#include "goldfish_vk_dispatch.h"
#include "vulkan/vulkan.h"

namespace gfxstream {
namespace vk {

struct ShaderData {
    const uint32_t* code;  // Pointer to shader's compiled spir-v code
    const size_t size;     // size of the code in bytes
};

struct Etc2PushConstant {
    uint32_t compFormat;
    uint32_t baseLayer;
};

struct AstcPushConstant {
    uint32_t blockSize[2];
    uint32_t baseLayer;
    uint32_t smallBlock;
};

// Holds the Vulkan resources for the GPU decompression pipeline of a given emulated texture format.
// GpuDecompressionPipeline should be reused, use GpuDecompressionPipelineManager for that.
class GpuDecompressionPipeline {
   public:
    // Factory method. Returns null if pipeline initialization failed.
    static std::unique_ptr<GpuDecompressionPipeline> create(
        VulkanDispatch* vk, VkDevice device, VkFormat compressedFormat,
        VkImageType imageType, VkDescriptorSetLayout descriptorSetLayout,
        VkPipelineLayout pipelineLayout);

    // GpuDecompressionPipeline is neither copyable nor movable.
    // This allows us to safely release Vulkan resources in the destructor
    GpuDecompressionPipeline(const GpuDecompressionPipeline&) = delete;
    GpuDecompressionPipeline& operator=(const GpuDecompressionPipeline&) = delete;

    // Destructor, will release any Vulkan resource we hold
    ~GpuDecompressionPipeline();

    // Accessors
    VkPipeline pipeline() const { return mPipeline; }
    VkPipelineLayout pipelineLayout() const { return mPipelineLayout; }
    VkDescriptorSetLayout descriptorSetLayout() const { return mDescriptorSetLayout; }

   private:
    GpuDecompressionPipeline(VulkanDispatch* vk, VkDevice device,
                             VkFormat compressedFormat, VkImageType imageType,
                             VkDescriptorSetLayout descriptorSetLayout,
                             VkPipelineLayout pipelineLayout);

    // Initializes the compute shader pipeline
    bool initialize();

    // Member variables set in the constructor
    VulkanDispatch* mVk;
    VkDevice mDevice;
    VkFormat mCompressedFormat;
    VkImageType mImageType;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkPipelineLayout mPipelineLayout;

    // Member variables set by initialize()
    VkPipeline mPipeline = VK_NULL_HANDLE;
};

enum class AstcDecoder{Old, NewRgb, NewBc3};

// Acts as a cache for GpuDecompressionPipeline objects
// Currently no eviction strategy, but the maximum number of entries is bounded by the number of
// compressed formats that we emulate.
// Thread-safety: not thread safe. Access to members of this class must be synchronized externally.
class GpuDecompressionPipelineManager {
   public:
    // TODO(gregschlom) remove this once we fully remove the old decoder
    static void setAstcDecoder(AstcDecoder value);
    static AstcDecoder astcDecoder();

    GpuDecompressionPipelineManager(VulkanDispatch* vk, VkDevice device);

    // Returns the cached pipeline for the decompression of a given image format and type.
    // If the pipeline doesn't exist yet, it will be created and initialized.
    // Returns null if initialization of the pipeline failed.
    GpuDecompressionPipeline* get(VkFormat compressedFormat, VkImageType imageType);

    // Erases everything from the cache
    void clear();

    ~GpuDecompressionPipelineManager();

    // GpuDecompressionPipelineManager is neither copyable nor movable.
    // This allows us to safely release Vulkan resources in the destructor
    GpuDecompressionPipelineManager(const GpuDecompressionPipelineManager&) = delete;
    GpuDecompressionPipelineManager& operator=(const GpuDecompressionPipelineManager&) = delete;

   private:
    VkDescriptorSetLayout getDescriptorSetLayout();
    VkPipelineLayout getPipelineLayout(VkFormat format);

    std::unordered_map<const ShaderData*, std::unique_ptr<GpuDecompressionPipeline>> mPipelines;

    VulkanDispatch* mVk = nullptr;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mAstcPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout mEtc2PipelineLayout = VK_NULL_HANDLE;
};

}  // namespace vk
}  // namespace gfxstream
