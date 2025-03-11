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

#include "GpuDecompressionPipeline.h"

#include "gfxstream/host/logging.h"
#include "vulkan/VkFormatUtils.h"
#include "vulkan/emulated_textures/shaders/DecompressionShaders.h"
#include "vulkan/vk_enum_string_helper.h"

namespace gfxstream {
namespace vk {

namespace {

// Which GPU decoder we use for ASTC textures.
// Note: we currently only enable ASTC decompression for native vulkan apps, and we try CPU
// decompression first, before falling back to GPU decompression.
static AstcDecoder activeAstcDecoder = AstcDecoder::NewRgb;

struct ShaderGroup {
    ShaderData shader1D;
    ShaderData shader2D;
    ShaderData shader3D;
};

// Helper macro to declare the shader goups
#define DECLARE_SHADER_GROUP(Format)                                      \
    constexpr ShaderGroup kShader##Format {                               \
        .shader1D = {.code = decompression_shaders::Format##_1D,          \
                     .size = sizeof(decompression_shaders::Format##_1D)}, \
        .shader2D = {.code = decompression_shaders::Format##_2D,          \
                     .size = sizeof(decompression_shaders::Format##_2D)}, \
        .shader3D = {.code = decompression_shaders::Format##_3D,          \
                     .size = sizeof(decompression_shaders::Format##_3D)}, \
    }

DECLARE_SHADER_GROUP(Astc);
DECLARE_SHADER_GROUP(AstcToRgb);
DECLARE_SHADER_GROUP(AstcToBc3);
DECLARE_SHADER_GROUP(EacR11Snorm);
DECLARE_SHADER_GROUP(EacR11Unorm);
DECLARE_SHADER_GROUP(EacRG11Snorm);
DECLARE_SHADER_GROUP(EacRG11Unorm);
DECLARE_SHADER_GROUP(Etc2RGB8);
DECLARE_SHADER_GROUP(Etc2RGBA8);

#undef DECLARE_SHADER_GROUP

// Returns the group of shaders that can decompress a given format, or null if none is found.
const ShaderGroup* getShaderGroup(VkFormat format) {
    switch (format) {
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
            switch (activeAstcDecoder) {
                case AstcDecoder::Old:
                    return &kShaderAstc;
                case AstcDecoder::NewRgb:
                    return &kShaderAstcToRgb;
                case AstcDecoder::NewBc3:
                    return &kShaderAstcToBc3;
            }
        case VK_FORMAT_EAC_R11_SNORM_BLOCK:
            return &kShaderEacR11Snorm;

        case VK_FORMAT_EAC_R11_UNORM_BLOCK:
            return &kShaderEacR11Unorm;

        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
            return &kShaderEacRG11Snorm;

        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
            return &kShaderEacRG11Unorm;

        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
            return &kShaderEtc2RGB8;

        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
            return &kShaderEtc2RGBA8;

        default:
            return nullptr;
    }
}

// Returns the shader that can decompress a given image format and type
const ShaderData* getDecompressionShader(VkFormat format, VkImageType imageType) {
    const ShaderGroup* group = getShaderGroup(format);
    if (!group) return nullptr;

    switch (imageType) {
        case VK_IMAGE_TYPE_1D:
            return &group->shader1D;
        case VK_IMAGE_TYPE_2D:
            return &group->shader2D;
        case VK_IMAGE_TYPE_3D:
            return &group->shader3D;
        default:
            return nullptr;
    }
}

const char* string_AstcDecoder(AstcDecoder decoder) {
    switch (decoder) {
        case AstcDecoder::Old:
            return "Old";
        case AstcDecoder::NewRgb:
            return "NewRgb";
        case AstcDecoder::NewBc3:
            return "NewBc3";
    }
    return "Unknown";
}

}  // namespace

// static
std::unique_ptr<GpuDecompressionPipeline> GpuDecompressionPipeline::create(
    VulkanDispatch* vk, VkDevice device, VkFormat compressedFormat,
    VkImageType imageType, VkDescriptorSetLayout descriptorSetLayout,
    VkPipelineLayout pipelineLayout) {
    auto pipeline = std::unique_ptr<GpuDecompressionPipeline>(new GpuDecompressionPipeline(
        vk, device, compressedFormat, imageType, descriptorSetLayout, pipelineLayout));
    if (!pipeline->initialize()) {
        return nullptr;
    }
    return pipeline;
}

GpuDecompressionPipeline::GpuDecompressionPipeline(VulkanDispatch* vk, VkDevice device,
                                                   VkFormat compressedFormat, VkImageType imageType,
                                                   VkDescriptorSetLayout descriptorSetLayout,
                                                   VkPipelineLayout pipelineLayout)
    : mVk(vk),
      mDevice(device),
      mCompressedFormat(compressedFormat),
      mImageType(imageType),
      mDescriptorSetLayout(descriptorSetLayout),
      mPipelineLayout(pipelineLayout) {
    GFXSTREAM_INFO("Created GPU decompression pipeline for format %s %s. ASTC decoder: %s",
                   string_VkFormat(mCompressedFormat), string_VkImageType(imageType),
                   string_AstcDecoder(activeAstcDecoder));
}

bool GpuDecompressionPipeline::initialize() {
    const ShaderData* shaderData = getDecompressionShader(mCompressedFormat, mImageType);
    if (!shaderData) {
        GFXSTREAM_WARNING("No decompression shader found for format %s and img type %s",
                          string_VkFormat(mCompressedFormat), string_VkImageType(mImageType));
        return false;
    }

    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderData->size,
        .pCode = shaderData->code,
    };
    VkShaderModule shader;
    VkResult result = mVk->vkCreateShaderModule(mDevice, &shaderInfo, nullptr, &shader);
    if (result != VK_SUCCESS) {
        GFXSTREAM_WARNING("GPU decompression: error calling vkCreateShaderModule: %d", result);
        return false;
    }

    VkComputePipelineCreateInfo computePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = shader,
                  .pName = "main"},
        .layout = mPipelineLayout,
    };
    result = mVk->vkCreateComputePipelines(mDevice, nullptr, 1, &computePipelineInfo, nullptr,
                                           &mPipeline);
    mVk->vkDestroyShaderModule(mDevice, shader, nullptr);
    if (result != VK_SUCCESS) {
        GFXSTREAM_WARNING("GPU decompression: error calling vkCreateComputePipelines: %d", result);
        return false;
    }
    return true;
}

GpuDecompressionPipeline::~GpuDecompressionPipeline() {
    if (!mVk || !mDevice) return;
    mVk->vkDestroyPipeline(mDevice, mPipeline, nullptr);
}

// static
void GpuDecompressionPipelineManager::setAstcDecoder(AstcDecoder value) {
    activeAstcDecoder = value;
}
AstcDecoder GpuDecompressionPipelineManager::astcDecoder() { return activeAstcDecoder; }

GpuDecompressionPipelineManager::GpuDecompressionPipelineManager(VulkanDispatch* vk,
                                                                 VkDevice device)
    : mVk(vk), mDevice(device) {}

GpuDecompressionPipeline* GpuDecompressionPipelineManager::get(VkFormat compressedFormat,
                                                               VkImageType imageType) {
    auto& pipeline = mPipelines[getDecompressionShader(compressedFormat, imageType)];
    if (!pipeline) {
        VkDescriptorSetLayout descriptorSetLayout = getDescriptorSetLayout();
        if (descriptorSetLayout == VK_NULL_HANDLE) return nullptr;

        VkPipelineLayout pipelineLayout = getPipelineLayout(compressedFormat);
        if (pipelineLayout == VK_NULL_HANDLE) return nullptr;

        pipeline = GpuDecompressionPipeline::create(mVk, mDevice, compressedFormat, imageType,
                                                    descriptorSetLayout, pipelineLayout);
    }
    return pipeline.get();
}

VkDescriptorSetLayout GpuDecompressionPipelineManager::getDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding dsLayoutBindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = std::size(dsLayoutBindings),
        .pBindings = dsLayoutBindings,
    };
    VkResult result =
        mVk->vkCreateDescriptorSetLayout(mDevice, &dsLayoutInfo, nullptr, &mDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        GFXSTREAM_WARNING("GPU decompression: error calling vkCreateDescriptorSetLayout: %d",
                          result);
        return VK_NULL_HANDLE;
    }
    return mDescriptorSetLayout;
}

VkPipelineLayout GpuDecompressionPipelineManager::getPipelineLayout(VkFormat format) {
    VkPipelineLayout* pipelineLayout;
    VkPushConstantRange pushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};

    if (isAstc(format)) {
        pipelineLayout = &mAstcPipelineLayout;
        pushConstant.size = sizeof(AstcPushConstant);
    } else {
        pipelineLayout = &mEtc2PipelineLayout;
        pushConstant.size = sizeof(Etc2PushConstant);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &mDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstant,
    };
    VkResult result =
        mVk->vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, pipelineLayout);
    if (result != VK_SUCCESS) {
        GFXSTREAM_WARNING(
            "GPU decompression: error calling vkCreatePipelineLayout for format %s: %d",
            string_VkFormat(format), result);
        return VK_NULL_HANDLE;
    }
    return *pipelineLayout;
}

void GpuDecompressionPipelineManager::clear() {
    mPipelines.clear();
    if (mVk && mDevice) {
        mVk->vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);
        mVk->vkDestroyPipelineLayout(mDevice, mAstcPipelineLayout, nullptr);
        mVk->vkDestroyPipelineLayout(mDevice, mEtc2PipelineLayout, nullptr);

        mDescriptorSetLayout = VK_NULL_HANDLE;
        mAstcPipelineLayout = VK_NULL_HANDLE;
        mEtc2PipelineLayout = VK_NULL_HANDLE;
    }
}

GpuDecompressionPipelineManager::~GpuDecompressionPipelineManager() {
    if (!mPipelines.empty() || mDescriptorSetLayout != VK_NULL_HANDLE ||
        mAstcPipelineLayout != VK_NULL_HANDLE || mEtc2PipelineLayout != VK_NULL_HANDLE) {
        GFXSTREAM_WARNING(
            "Resource leak: GpuDecompressionPipelineManager is being destroyed but clear() wasn't"
            " called first");
    }
}

}  // namespace vk
}  // namespace gfxstream
