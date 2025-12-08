// Copyright 2025 The Android Open Source Project
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

#include "compositor_vk.h"

#include <string.h>

#include <cinttypes>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>

#include "gfxstream/common/logging.h"
#include "gfxstream/host/tracing.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vk_format_utils.h"
#include "vulkan/vk_utils.h"

namespace gfxstream {
namespace host {
namespace vk {

namespace CompositorVkShader {
#include "vulkan/compositor_fragment_shader.h"
#include "vulkan/compositor_vertex_shader.h"
}  // namespace CompositorVkShader

namespace {

constexpr const VkImageLayout kSourceImageInitialLayoutUsed =
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
constexpr const VkImageLayout kSourceImageFinalLayoutUsed =
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

constexpr const VkImageLayout kTargetImageInitialLayoutUsed = VK_IMAGE_LAYOUT_UNDEFINED;
constexpr const VkImageLayout kTargetImageFinalLayoutUsed = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

static const GfxstreamFormat kRenderTargetFormats[2] = {
    GfxstreamFormat::R8G8B8A8_UNORM,
    GfxstreamFormat::B8G8R8A8_UNORM,
};

const BorrowedImageInfoVk* getInfoOrAbort(const std::unique_ptr<BorrowedImageInfo>& info) {
    auto imageVk = static_cast<const BorrowedImageInfoVk*>(info.get());
    if (imageVk != nullptr) {
        return imageVk;
    }

    GFXSTREAM_FATAL("CompositorVk did not find BorrowedImageInfoVk");
    return nullptr;
}

struct Vertex {
    alignas(8) glm::vec2 pos;
    alignas(8) glm::vec2 tex;

    static VkVertexInputBindingDescription getBindingDescription() {
        return VkVertexInputBindingDescription{
            .binding = 0,
            .stride = sizeof(struct Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescription() {
        return {
            VkVertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(struct Vertex, pos),
            },
            VkVertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(struct Vertex, tex),
            },
        };
    }
};

static const std::vector<Vertex> k_vertices = {
    // clang-format off
    { .pos = {-1.0f, -1.0f}, .tex = {0.0f, 0.0f}},
    { .pos = { 1.0f, -1.0f}, .tex = {1.0f, 0.0f}},
    { .pos = { 1.0f,  1.0f}, .tex = {1.0f, 1.0f}},
    { .pos = {-1.0f,  1.0f}, .tex = {0.0f, 1.0f}},
    // clang-format on
};

static const std::vector<uint16_t> k_indices = {0, 1, 2, 2, 3, 0};

static VkShaderModule createShaderModule(const VulkanDispatch& vk, VkDevice device,
                                         const std::vector<uint32_t>& code) {
    const VkShaderModuleCreateInfo shaderModuleCi = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = static_cast<uint32_t>(code.size() * sizeof(uint32_t)),
        .pCode = code.data(),
    };
    VkShaderModule res;
    VK_CHECK(vk.vkCreateShaderModule(device, &shaderModuleCi, nullptr, &res));
    return res;
}

}  // namespace

CompositorVk::RenderTarget::RenderTarget(const VulkanDispatch& vk, VkDevice vkDevice,
                                         VkImage vkImage, VkImageView vkImageView, uint32_t width,
                                         uint32_t height, VkRenderPass vkRenderPass)
    : m_vk(vk),
      m_vkDevice(vkDevice),
      m_vkImage(vkImage),
      m_vkFramebuffer(VK_NULL_HANDLE),
      m_width(width),
      m_height(height) {
    if (vkImageView == VK_NULL_HANDLE) {
        GFXSTREAM_FATAL(
            "CompositorVk found empty image view handle when creating RenderTarget. "
            "VkImage:%p w:%" PRIu32 " h:%" PRIu32,
            m_vkImage, m_width, m_height);
    }

    const VkFramebufferCreateInfo framebufferCi = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .flags = 0,
        .renderPass = vkRenderPass,
        .attachmentCount = 1,
        .pAttachments = &vkImageView,
        .width = width,
        .height = height,
        .layers = 1,
    };
    VK_CHECK(m_vk.vkCreateFramebuffer(vkDevice, &framebufferCi, nullptr, &m_vkFramebuffer));
}

CompositorVk::RenderTarget::~RenderTarget() {
    if (m_vkFramebuffer != VK_NULL_HANDLE) {
        m_vk.vkDestroyFramebuffer(m_vkDevice, m_vkFramebuffer, nullptr);
    }
}

std::unique_ptr<CompositorVk> CompositorVk::create(
    const VulkanDispatch& vk, VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice, VkQueue vkQueue,
    std::shared_ptr<gfxstream::base::Lock> queueLock, uint32_t queueFamilyIndex,
    uint32_t maxFramesInFlight, vk_util::YcbcrSamplerPool* ycbcrSamplerPool,
    DebugUtilsHelper debugUtils) {
    auto res = std::unique_ptr<CompositorVk>(
        new CompositorVk(vk, vkDevice, vkPhysicalDevice, vkQueue, queueLock, queueFamilyIndex,
                         maxFramesInFlight, ycbcrSamplerPool, debugUtils));
    res->setUpCommandPool();
    res->setUpFormatResources();
    res->setUpRenderPasses();
    res->setUpGraphicsPipelines();
    res->setUpVertexBuffers();
    res->setUpUniformBuffers();
    res->setUpDescriptorSets();
    res->setUpFences();
    res->setUpDefaultImage();
    res->setUpFrameResourceFutures();
    GFXSTREAM_INFO("Created CompositorVk");
    return res;
}

CompositorVk::CompositorVk(const VulkanDispatch& vk, VkDevice vkDevice,
                           VkPhysicalDevice vkPhysicalDevice, VkQueue vkQueue,
                           std::shared_ptr<gfxstream::base::Lock> queueLock,
                           uint32_t queueFamilyIndex, uint32_t maxFramesInFlight,
                           vk_util::YcbcrSamplerPool* ycbcrPool, DebugUtilsHelper debugUtilsHelper)
    : CompositorVkBase(vk, vkDevice, vkPhysicalDevice, vkQueue, queueLock, queueFamilyIndex,
                       maxFramesInFlight, ycbcrPool, debugUtilsHelper),
      m_maxFramesInFlight(maxFramesInFlight),
      m_renderTargetCache(k_renderTargetCacheSize) {}

CompositorVk::~CompositorVk() {
    {
        gfxstream::base::AutoLock lock(*m_vkQueueLock);
        VK_CHECK(m_vk.vkQueueWaitIdle(m_vkQueue));
    }
    destroyImage(m_defaultImage);
    destroyImage(m_screenMaskImage);
    destroyImage(m_screenBackgroundImage);
    destroyUniformBufferStorage(m_uniformStorage);

    m_vk.vkDestroyDescriptorPool(m_vkDevice, m_vkDescriptorPool, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, m_vertexVkDeviceMemory, nullptr);
    m_vk.vkDestroyBuffer(m_vkDevice, m_vertexVkBuffer, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, m_indexVkDeviceMemory, nullptr);
    m_vk.vkDestroyBuffer(m_vkDevice, m_indexVkBuffer, nullptr);
    for (auto& iter : m_vkGraphicsVkPipelines) {
        m_vk.vkDestroyPipeline(m_vkDevice, iter.second, nullptr);
    }
    for (auto& iter : m_vkRenderPasses) {
        m_vk.vkDestroyRenderPass(m_vkDevice, iter.second, nullptr);
    }
    for (auto& iter : m_formatResources) {
        m_vk.vkDestroyPipelineLayout(m_vkDevice, iter.second.pipelineLayout, nullptr);
        m_vk.vkDestroyDescriptorSetLayout(m_vkDevice, iter.second.descriptorSetLayout, nullptr);
    }
    m_vk.vkDestroySampler(m_vkDevice, m_defaultSampler, nullptr);
    m_vk.vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);
    for (PerFrameResources& frameResources : m_frameResources) {
        m_vk.vkDestroyFence(m_vkDevice, frameResources.m_vkFence, nullptr);
    }
}

void CompositorVk::setUpGraphicsPipeline(const VkShaderModule vertShaderMod,
                                         const VkShaderModule fragShaderMod,
                                         const GfxstreamFormat sampledImageFormat) {
    auto formatResourcesIt = m_formatResources.find(sampledImageFormat);
    if (formatResourcesIt == m_formatResources.end()) {
        const std::string formatString = ToString(sampledImageFormat);
        GFXSTREAM_FATAL("Failed to find resources for format %s", formatString.c_str());
    }
    const PerFormatResources& formatResources = formatResourcesIt->second;

    const VkPipelineShaderStageCreateInfo shaderStageCis[2] = {
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShaderMod,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderMod,
            .pName = "main",
        },
    };

    const auto vertexAttributeDescription = Vertex::getAttributeDescription();
    const auto vertexBindingDescription = Vertex::getBindingDescription();
    const VkPipelineVertexInputStateCreateInfo vertexInputStateCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescription.size()),
        .pVertexAttributeDescriptions = vertexAttributeDescription.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    const VkPipelineViewportStateCreateInfo viewportStateCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        // The viewport state is dynamic.
        .pViewports = nullptr,
        .scissorCount = 1,
        // The scissor state is dynamic.
        .pScissors = nullptr,
    };

    const VkPipelineRasterizationStateCreateInfo rasterizerStateCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisampleStateCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    const VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    const VkPipelineColorBlendStateCreateInfo colorBlendStateCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    const VkPipelineDynamicStateCreateInfo dynamicStateCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = std::size(dynamicStates),
        .pDynamicStates = dynamicStates,
    };

    VkGraphicsPipelineCreateInfo graphicsPipelineCi = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = static_cast<uint32_t>(std::size(shaderStageCis)),
        .pStages = shaderStageCis,
        .pVertexInputState = &vertexInputStateCi,
        .pInputAssemblyState = &inputAssemblyStateCi,
        .pViewportState = &viewportStateCi,
        .pRasterizationState = &rasterizerStateCi,
        .pMultisampleState = &multisampleStateCi,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &colorBlendStateCi,
        .pDynamicState = &dynamicStateCi,
        .layout = formatResources.pipelineLayout,
        .renderPass = VK_NULL_HANDLE,  // to be filled below
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    for (GfxstreamFormat renderTargetFormat : kRenderTargetFormats) {
        graphicsPipelineCi.renderPass = m_vkRenderPasses[renderTargetFormat];

        VkPipeline pipeline = VK_NULL_HANDLE;
        VK_CHECK(m_vk.vkCreateGraphicsPipelines(m_vkDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCi,
                                                nullptr, &pipeline));

        GraphicsPipelineKey key = {
            .renderTargetFormat = renderTargetFormat,
            .sampledImageFormat = sampledImageFormat,
        };
        m_vkGraphicsVkPipelines[key] = pipeline;
    }
}

void CompositorVk::setUpGraphicsPipelines() {
    const std::vector<uint32_t> vertSpvBuff = CompositorVkShader::compositorVertexShader;
    const std::vector<uint32_t> fragSpvBuff = CompositorVkShader::compositorFragmentShader;
    const auto vertShaderMod = createShaderModule(m_vk, m_vkDevice, vertSpvBuff);
    const auto fragShaderMod = createShaderModule(m_vk, m_vkDevice, fragSpvBuff);

    // Setup the pipeline for all non-YCbCr formats (GfxstreamFormat::UNKNOWN):
    setUpGraphicsPipeline(vertShaderMod, fragShaderMod, GfxstreamFormat::UNKNOWN);

    // Setup the pipeline for all YCbCr formats:
    //
    // TODO(b/389646068): Current descriptor management in the compositor doesn't allow on demand
    // format addition and this exploits a pre-warmed sampler pool to support ycbcr format. We need
    // to get it running on-demand to be able to support other formats
    for (GfxstreamFormat format : m_ycbcrSamplerPool->getAllFormats()) {
        setUpGraphicsPipeline(vertShaderMod, fragShaderMod, format);
    }

    m_vk.vkDestroyShaderModule(m_vkDevice, vertShaderMod, nullptr);
    m_vk.vkDestroyShaderModule(m_vkDevice, fragShaderMod, nullptr);
}

void CompositorVk::setUpRenderPasses() {
    VkAttachmentDescription colorAttachment = {
        .format = VK_FORMAT_UNDEFINED,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = kTargetImageInitialLayoutUsed,
        .finalLayout = kTargetImageFinalLayoutUsed,
    };

    const VkAttachmentReference colorAttachmentRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
    };

    // TODO: to support multiple layer composition, we could run the same render
    // pass for multiple time. In that case, we should use explicit
    // VkImageMemoryBarriers to transform the image layout instead of relying on
    // renderpass to do it.
    const VkSubpassDependency subpassDependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    const VkRenderPassCreateInfo renderPassCi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &subpassDependency,
    };

    for (GfxstreamFormat renderTargetFormat : kRenderTargetFormats) {
        colorAttachment.format = TO_VK_FORMAT_OR_DIE(renderTargetFormat);

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VK_CHECK(m_vk.vkCreateRenderPass(m_vkDevice, &renderPassCi, nullptr, &renderPass));

        m_vkRenderPasses[renderTargetFormat] = renderPass;
    }
}

void CompositorVk::setUpVertexBuffers() {
    const VkDeviceSize vertexBufferSize = sizeof(Vertex) * k_vertices.size();
    std::tie(m_vertexVkBuffer, m_vertexVkDeviceMemory) =
        createBuffer(vertexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            .value();
    auto [vertexStagingBuffer, vertexStagingBufferMemory] =
        createStagingBufferWithData(k_vertices.data(), vertexBufferSize);
    copyBuffer(vertexStagingBuffer, m_vertexVkBuffer, vertexBufferSize);
    m_vk.vkDestroyBuffer(m_vkDevice, vertexStagingBuffer, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, vertexStagingBufferMemory, nullptr);

    VkDeviceSize indexBufferSize = sizeof(k_indices[0]) * k_indices.size();
    auto [indexStagingBuffer, indexStagingBufferMemory] =
        createStagingBufferWithData(k_indices.data(), indexBufferSize);
    std::tie(m_indexVkBuffer, m_indexVkDeviceMemory) =
        createBuffer(indexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            .value();

    copyBuffer(indexStagingBuffer, m_indexVkBuffer, indexBufferSize);
    m_vk.vkDestroyBuffer(m_vkDevice, indexStagingBuffer, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, indexStagingBufferMemory, nullptr);
}

void CompositorVk::setUpDescriptorSets() {
    const uint32_t descriptorSetsPerLayer = m_formatResources.size();
    const uint32_t descriptorSetsPerFrame =
        (kMaxLayersPerFrame * descriptorSetsPerLayer + kMaxImmediateDrawsPerFrame);
    const uint32_t descriptorSetsTotal = descriptorSetsPerFrame * m_maxFramesInFlight;

    // TODO(b/389646068): *2 should not be necessary for image samplers but
    // some drivers are throwing VK_ERROR_OUT_OF_POOL_MEMORY errors.
    const VkDescriptorPoolSize descriptorPoolSizes[2] = {
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = descriptorSetsTotal * 2,
        },
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = descriptorSetsTotal,
        }};
    const VkDescriptorPoolCreateInfo descriptorPoolCi = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = 0,
        .maxSets = descriptorSetsTotal,
        .poolSizeCount = static_cast<uint32_t>(std::size(descriptorPoolSizes)),
        .pPoolSizes = descriptorPoolSizes,
    };
    VK_CHECK(
        m_vk.vkCreateDescriptorPool(m_vkDevice, &descriptorPoolCi, nullptr, &m_vkDescriptorPool));

    auto allocateFrameDescriptorSetsForLayout = [&]( UniformBufferStorage& uniformStorage,
                                                    VkDescriptorSetLayout layout,
                                                    VkDeviceSize& bufferOffset,
                                                    uint32_t numDescriptorSets) {
        const std::vector<VkDescriptorSetLayout> frameDescriptorSetLayouts(numDescriptorSets,
                                                                           layout);
        const VkDescriptorSetAllocateInfo frameDescriptorSetAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_vkDescriptorPool,
            .descriptorSetCount = numDescriptorSets,
            .pSetLayouts = frameDescriptorSetLayouts.data(),
        };
        std::vector<VkDescriptorSet> frameDescriptorSets;
        frameDescriptorSets.resize(numDescriptorSets);

        VK_CHECK(m_vk.vkAllocateDescriptorSets(m_vkDevice, &frameDescriptorSetAllocInfo,
                                               frameDescriptorSets.data()));

        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkWriteDescriptorSet> descriptorSetWrites;
        bufferInfos.resize(numDescriptorSets);
        descriptorSetWrites.resize(numDescriptorSets);
        for (uint32_t layerIndex = 0; layerIndex < numDescriptorSets; ++layerIndex) {
            bufferInfos[layerIndex] = {
                .buffer = uniformStorage.m_vkBuffer,
                .offset = bufferOffset,
                .range = sizeof(UniformBufferBinding),
            };
            descriptorSetWrites[layerIndex] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frameDescriptorSets[layerIndex],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bufferInfos[layerIndex],
            };
            bufferOffset += uniformStorage.m_stride;

            if (bufferOffset > uniformStorage.m_size) {
                // This indicates a serious error in the offset calculation logic
                GFXSTREAM_FATAL("%s: Invalid offset for uniform buffer descriptors (%llu, %llu)",
                                bufferOffset, uniformStorage.m_size);
            }
        }

        m_vk.vkUpdateDescriptorSets(m_vkDevice, descriptorSetWrites.size(),
                                    descriptorSetWrites.data(), 0, nullptr);

        return frameDescriptorSets;
    };

    VkDeviceSize uniformBufferOffset = 0;
    for (uint32_t frameIndex = 0; frameIndex < m_maxFramesInFlight; ++frameIndex) {
        PerFrameResources& frameResources = m_frameResources[frameIndex];

        for (const auto& [format, formatResources] : m_formatResources) {
            frameResources.m_layerDescriptorSets[format] = allocateFrameDescriptorSetsForLayout(
                m_uniformStorage, formatResources.descriptorSetLayout, uniformBufferOffset,
                kMaxLayersPerFrame);

            if (format == GfxstreamFormat::UNKNOWN) {
                m_immediateFrameResources[frameIndex].m_descriptorSets =
                    allocateFrameDescriptorSetsForLayout(
                        m_uniformStorage, formatResources.descriptorSetLayout, uniformBufferOffset,
                        kMaxImmediateDrawsPerFrame);
            }
        }
    }
}

void CompositorVk::setUpCommandPool() {
    const VkCommandPoolCreateInfo commandPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = m_queueFamilyIndex,
    };

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VK_CHECK(m_vk.vkCreateCommandPool(m_vkDevice, &commandPoolCreateInfo, nullptr, &commandPool));
    m_vkCommandPool = commandPool;
    m_debugUtilsHelper.addDebugLabel(m_vkCommandPool, "CompositorVk command pool");
}

void CompositorVk::setUpFences() {
    for (uint32_t frameIndex = 0; frameIndex < m_maxFramesInFlight; ++frameIndex) {
        PerFrameResources& frameResources = m_frameResources[frameIndex];
        const VkFenceCreateInfo fenceCi = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        VK_CHECK(m_vk.vkCreateFence(m_vkDevice, &fenceCi, nullptr, &frameResources.m_vkFence));
    }
}

CompositorVkBase::Image CompositorVk::createImage(uint32_t width, uint32_t height,
                                                  const uint8_t* rgbaData,
                                                  const std::string& debugName) {
    GFXSTREAM_VERBOSE("%s: %s with size %d x %d", __func__, debugName.c_str(), width, height);
    const VkImageCreateInfo imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    VK_CHECK(m_vk.vkCreateImage(m_vkDevice, &imageCreateInfo, nullptr, &image));

    VkMemoryRequirements imageMemoryRequirements;
    m_vk.vkGetImageMemoryRequirements(m_vkDevice, image, &imageMemoryRequirements);

    auto memoryTypeIndexOpt =
        findMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memoryTypeIndexOpt) {
        GFXSTREAM_FATAL("CompositorVk failed to find memory type for default image.");
    }

    const VkMemoryAllocateInfo imageMemoryAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = imageMemoryRequirements.size,
        .memoryTypeIndex = *memoryTypeIndexOpt,
    };
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VK_CHECK_MEMALLOC(
        m_vk.vkAllocateMemory(m_vkDevice, &imageMemoryAllocInfo, nullptr, &imageMemory),
        imageMemoryAllocInfo);

    VK_CHECK(m_vk.vkBindImageMemory(m_vkDevice, image, imageMemory, 0));

    const VkImageViewCreateInfo imageViewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    VkImageView imageView = VK_NULL_HANDLE;
    VK_CHECK(m_vk.vkCreateImageView(m_vkDevice, &imageViewCreateInfo, nullptr, &imageView));

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    std::tie(stagingBuffer, stagingBufferMemory) =
        createStagingBufferWithData(rgbaData, width * height * 4);

    runSingleTimeCommands(m_vkQueue, m_vkQueueLock, [&, this](const VkCommandBuffer& cmdBuff) {
        const VkImageMemoryBarrier toTransferDstImageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        m_vk.vkCmdPipelineBarrier(cmdBuff,
                                  /*srcStageMask=*/VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                  /*dstStageMask=*/VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  /*dependencyFlags=*/0,
                                  /*memoryBarrierCount=*/0,
                                  /*pMemoryBarriers=*/nullptr,
                                  /*bufferMemoryBarrierCount=*/0,
                                  /*pBufferMemoryBarriers=*/nullptr, 1, &toTransferDstImageBarrier);

        const VkBufferImageCopy bufferToImageCopy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageOffset =
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
            .imageExtent =
                {
                    .width = width,
                    .height = height,
                    .depth = 1,
                },
        };
        m_vk.vkCmdCopyBufferToImage(cmdBuff, stagingBuffer, image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferToImageCopy);

        const VkImageMemoryBarrier toSampledImageImageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        m_vk.vkCmdPipelineBarrier(cmdBuff,
                                  /*srcStageMask=*/VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  /*dstStageMask=*/VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  /*dependencyFlags=*/0,
                                  /*memoryBarrierCount=*/0,
                                  /*pMemoryBarriers=*/nullptr,
                                  /*bufferMemoryBarrierCount=*/0,
                                  /*pBufferMemoryBarriers=*/nullptr, 1,
                                  &toSampledImageImageBarrier);
    });

    m_vk.vkDestroyBuffer(m_vkDevice, stagingBuffer, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, stagingBufferMemory, nullptr);

    m_debugUtilsHelper.addDebugLabel(image, "CompositorVk::image %s", debugName.c_str());
    m_debugUtilsHelper.addDebugLabel(imageView, "CompositorVk::imageView %s", debugName.c_str());
    m_debugUtilsHelper.addDebugLabel(imageMemory, "CompositorVk::imageMemory %s",
                                     debugName.c_str());

    // Encapsulate the created vulkan objects into an Image instance
    Image img;
    img.m_vkImage = image;
    img.m_vkImageView = imageView;
    img.m_vkImageMemory = imageMemory;
    img.m_imageFormat = GfxstreamFormat::R8G8B8A8_UNORM;
    return img;
}

void CompositorVk::setUpDefaultImage() {
    destroyImage(m_defaultImage);

    const std::array<uint8_t, 16> pixels = {
        0xFF, 0x00, 0xFF, 0xFF,  //
        0xFF, 0x00, 0xFF, 0xFF,  //
        0xFF, 0x00, 0xFF, 0xFF,  //
        0xFF, 0x00, 0xFF, 0xFF,  //
    };
    m_defaultImage = createImage(2, 2, pixels.data(), "defaultImage");
}

void CompositorVk::setUpScreenMaskImage(uint32_t width, uint32_t height, const uint8_t* rgbaData) {
    std::lock_guard<std::mutex> lock(mScreenImagesMutex);
    destroyImage(m_screenMaskImage);
    if (rgbaData) {
        m_screenMaskImage = createImage(width, height, rgbaData, "screenMask");
    }
}

void CompositorVk::setUpScreenBackgroundImage(uint32_t width, uint32_t height,
                                              const uint8_t* rgbaData) {
    std::lock_guard<std::mutex> lock(mScreenImagesMutex);
    destroyImage(m_screenBackgroundImage);
    if (rgbaData) {
        m_screenBackgroundImage = createImage(width, height, rgbaData, "screenBackground");
    }
}

void CompositorVk::setUpFrameResourceFutures() {
    for (uint32_t frameIndex = 0; frameIndex < m_maxFramesInFlight; ++frameIndex) {
        std::shared_future<PerFrameResources*> availableFrameResourceFuture =
            std::async(std::launch::deferred, [this, frameIndex] {
                return &m_frameResources[frameIndex];
            }).share();

        m_availableFrameResources.push_back(std::move(availableFrameResourceFuture));
    }
}

void CompositorVk::destroyImage(Image& img) {
    if (img.m_vkImageView != VK_NULL_HANDLE) {
        m_vk.vkDestroyImageView(m_vkDevice, img.m_vkImageView, nullptr);
        img.m_vkImageView = VK_NULL_HANDLE;
    }
    if (img.m_vkImage != VK_NULL_HANDLE) {
        m_vk.vkDestroyImage(m_vkDevice, img.m_vkImage, nullptr);
        img.m_vkImage = VK_NULL_HANDLE;
    }
    if (img.m_vkImageMemory != VK_NULL_HANDLE) {
        m_vk.vkFreeMemory(m_vkDevice, img.m_vkImageMemory, nullptr);
        img.m_vkImageMemory = VK_NULL_HANDLE;
    }
}

void CompositorVk::setUpUniformBuffers() {
    const uint32_t numLayouts = m_formatResources.size();
    uint32_t numBuffersRequiredPerFrame =
        (kMaxLayersPerFrame * numLayouts) + kMaxImmediateDrawsPerFrame;
    uint32_t numBuffersRequired = m_maxFramesInFlight * numBuffersRequiredPerFrame;

    createUniformBufferStorage(m_uniformStorage, numBuffersRequired);
    uint8_t* uniformDataPtr = reinterpret_cast<uint8_t*>(m_uniformStorage.m_mappedPtr);
    const uint8_t* uniformDataPtrStart = uniformDataPtr;

    for (uint32_t frameIndex = 0; frameIndex < m_maxFramesInFlight; ++frameIndex) {
        for (const auto& [format, res] : m_formatResources) {
            auto& uboStorages = m_frameResources[frameIndex].m_layerUboStorages[format];
            uboStorages.resize(kMaxLayersPerFrame);
            for (uint32_t layerIndex = 0; layerIndex < kMaxLayersPerFrame; ++layerIndex) {
                uboStorages[layerIndex] = reinterpret_cast<UniformBufferBinding*>(uniformDataPtr);
                uniformDataPtr += m_uniformStorage.m_stride;
            }

            if (format == GfxstreamFormat::UNKNOWN) {
                auto& imStorages = m_immediateFrameResources[frameIndex].m_uboStorages;
                imStorages.resize(kMaxImmediateDrawsPerFrame);
                for (uint32_t drawIndex = 0; drawIndex < kMaxImmediateDrawsPerFrame; ++drawIndex) {
                    imStorages[drawIndex] = reinterpret_cast<UniformBufferBinding*>(uniformDataPtr);
                    uniformDataPtr += m_uniformStorage.m_stride;
                }
            }
        }
    }
    if (uniformDataPtr > (uniformDataPtrStart + m_uniformStorage.m_size)) {
        // This indicates a serious error in the offset calculation logic
        GFXSTREAM_FATAL("%s: Invalid offset for uniform buffers");
    }
}

void CompositorVk::createUniformBufferStorage(UniformBufferStorage& storage,
                                              uint32_t numBuffersRequired) {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    m_vk.vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &physicalDeviceProperties);
    const VkDeviceSize alignment = physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
    const VkDeviceSize stride = ((sizeof(UniformBufferBinding) - 1) / alignment + 1) * alignment;
    const VkDeviceSize allocSize = stride * numBuffersRequired;

    auto maybeBuffer =
        createBuffer(allocSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                         VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    auto buffer = std::make_tuple<VkBuffer, VkDeviceMemory>(VK_NULL_HANDLE, VK_NULL_HANDLE);
    if (maybeBuffer.has_value()) {
        buffer = maybeBuffer.value();
    } else {
        GFXSTREAM_VERBOSE("CompositorVk::%s: Cannot create uniform buffer with HOST_CACHED",
                          __func__);
        buffer =
            createBuffer(allocSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                .value();
    }

    std::tie(storage.m_vkBuffer, storage.m_vkDeviceMemory) = buffer;
    storage.m_stride = stride;
    storage.m_size = allocSize;
    storage.m_mappedPtr = nullptr;
    VK_CHECK(m_vk.vkMapMemory(m_vkDevice, storage.m_vkDeviceMemory, 0, VK_WHOLE_SIZE, 0,
                              &storage.m_mappedPtr));
}

void CompositorVk::destroyUniformBufferStorage(UniformBufferStorage& storage) {
    if (storage.m_vkDeviceMemory != VK_NULL_HANDLE) {
        m_vk.vkUnmapMemory(m_vkDevice, storage.m_vkDeviceMemory);
        storage.m_mappedPtr = nullptr;
    }
    m_vk.vkDestroyBuffer(m_vkDevice, storage.m_vkBuffer, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, storage.m_vkDeviceMemory, nullptr);
}

void CompositorVk::setUpFormatResources() {
    auto createFormatResources = [](const VulkanDispatch& vk, VkDevice device, VkSampler sampler) {
        PerFormatResources ret = {};
        const VkDescriptorSetLayoutBinding layoutBindings[2] = {
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = &sampler,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            },
        };

        const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCi = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<uint32_t>(std::size(layoutBindings)),
            .pBindings = layoutBindings,
        };

        VK_CHECK(vk.vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCi, nullptr,
                                                &ret.descriptorSetLayout));

        const VkPipelineLayoutCreateInfo pipelineLayoutCi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &ret.descriptorSetLayout,
            .pushConstantRangeCount = 0,
        };

        VK_CHECK(
            vk.vkCreatePipelineLayout(device, &pipelineLayoutCi, nullptr, &ret.pipelineLayout));

        return ret;
    };

    // The texture coordinate transformation matrices should output in [0-1] range
    // to ensure compatibility with YUV samplers with clamped addressing modes.
    constexpr const VkSamplerAddressMode kSamplerMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    const VkSamplerCreateInfo samplerCi = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = kSamplerMode,
        .addressModeV = kSamplerMode,
        .addressModeW = kSamplerMode,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VK_CHECK(m_vk.vkCreateSampler(m_vkDevice, &samplerCi, nullptr, &m_defaultSampler));
    m_formatResources[GfxstreamFormat::UNKNOWN] =
        createFormatResources(m_vk, m_vkDevice, m_defaultSampler);

    for (GfxstreamFormat format : m_ycbcrSamplerPool->getAllFormats()) {
        VkSampler sampler = m_ycbcrSamplerPool->getSampler(format);
        m_formatResources[format] = createFormatResources(m_vk, m_vkDevice, sampler);
    }
}

// Create a VkBuffer and a bound VkDeviceMemory. When the specified memory type
// can't be found, return std::nullopt. When Vulkan call fails, terminate the
// program.
std::optional<std::tuple<VkBuffer, VkDeviceMemory>> CompositorVk::createBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProperty) const {
    const VkBufferCreateInfo bufferCi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer resBuffer;
    VK_CHECK(m_vk.vkCreateBuffer(m_vkDevice, &bufferCi, nullptr, &resBuffer));
    VkMemoryRequirements memRequirements;
    m_vk.vkGetBufferMemoryRequirements(m_vkDevice, resBuffer, &memRequirements);
    VkPhysicalDeviceMemoryProperties physicalMemProperties;
    m_vk.vkGetPhysicalDeviceMemoryProperties(m_vkPhysicalDevice, &physicalMemProperties);
    auto maybeMemoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, memProperty);
    if (!maybeMemoryTypeIndex.has_value()) {
        GFXSTREAM_ERROR("Failed to find memory type for creating buffer.");
        m_vk.vkDestroyBuffer(m_vkDevice, resBuffer, nullptr);
        return std::nullopt;
    }
    const VkMemoryAllocateInfo memAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = maybeMemoryTypeIndex.value(),
    };
    VkDeviceMemory resMemory;
    VK_CHECK_MEMALLOC(m_vk.vkAllocateMemory(m_vkDevice, &memAllocInfo, nullptr, &resMemory),
                      memAllocInfo);
    VK_CHECK(m_vk.vkBindBufferMemory(m_vkDevice, resBuffer, resMemory, 0));
    return std::make_tuple(resBuffer, resMemory);
}

std::tuple<VkBuffer, VkDeviceMemory> CompositorVk::createStagingBufferWithData(
    const void* srcData, VkDeviceSize size) const {
    auto [stagingBuffer, stagingBufferMemory] =
        createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            .value();
    void* data;
    VK_CHECK(m_vk.vkMapMemory(m_vkDevice, stagingBufferMemory, 0, size, 0, &data));
    memcpy(data, srcData, size);
    m_vk.vkUnmapMemory(m_vkDevice, stagingBufferMemory);
    return std::make_tuple(stagingBuffer, stagingBufferMemory);
}

void CompositorVk::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
    runSingleTimeCommands(m_vkQueue, m_vkQueueLock, [&, this](const auto& cmdBuff) {
        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        m_vk.vkCmdCopyBuffer(cmdBuff, src, dst, 1, &copyRegion);
    });
}

// TODO: move this to another common CRTP helper class in vk_util.h.
VkFormatFeatureFlags CompositorVk::getFormatFeatures(VkFormat format, VkImageTiling tiling) {
    auto i = m_vkFormatProperties.find(format);
    if (i == m_vkFormatProperties.end()) {
        VkFormatProperties formatProperties;
        m_vk.vkGetPhysicalDeviceFormatProperties(m_vkPhysicalDevice, format, &formatProperties);
        i = m_vkFormatProperties.emplace(format, formatProperties).first;
    }
    const VkFormatProperties& formatProperties = i->second;
    VkFormatFeatureFlags formatFeatures = 0;
    if (tiling == VK_IMAGE_TILING_LINEAR) {
        formatFeatures = formatProperties.linearTilingFeatures;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL) {
        formatFeatures = formatProperties.optimalTilingFeatures;
    } else {
        GFXSTREAM_ERROR("Unknown tiling:%#" PRIx64 ".", static_cast<uint64_t>(tiling));
    }
    return formatFeatures;
}

CompositorVk::RenderTarget* CompositorVk::getOrCreateRenderTargetInfo(
    const BorrowedImageInfoVk& imageInfo) {
    auto* renderTargetPtr = m_renderTargetCache.get(imageInfo.id);
    if (renderTargetPtr != nullptr) {
        return renderTargetPtr->get();
    }

    auto renderPassIt = m_vkRenderPasses.find(imageInfo.imageFormat);
    if (renderPassIt == m_vkRenderPasses.end()) {
        const std::string formatString = ToString(imageInfo.imageFormat);
        GFXSTREAM_WARNING("Failed to find VkRenderPass for format %s.", formatString.c_str());
        return nullptr;
    }
    VkRenderPass renderPass = renderPassIt->second;

    auto* renderTarget = new RenderTarget(m_vk, m_vkDevice, imageInfo.image, imageInfo.imageView,
                                          imageInfo.imageCreateInfo.extent.width,
                                          imageInfo.imageCreateInfo.extent.height, renderPass);

    m_renderTargetCache.set(imageInfo.id, std::unique_ptr<RenderTarget>(renderTarget));

    return renderTarget;
}

bool CompositorVk::canCompositeFrom(const VkImageCreateInfo& imageCi) {
    VkFormatFeatureFlags formatFeatures = getFormatFeatures(imageCi.format, imageCi.tiling);
    if (!(formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        GFXSTREAM_ERROR(
            "The format, %s, with tiling, %s, doesn't support the "
            "VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT feature. All supported features are %s.",
            string_VkFormat(imageCi.format), string_VkImageTiling(imageCi.tiling),
            string_VkFormatFeatureFlags(formatFeatures).c_str());
        return false;
    }
    return true;
}

void CompositorVk::buildCompositionVk(const CompositionRequest& compositionRequest,
                                      CompositionVk* compositionVk) {
    if (compositionRequest.target.get() == nullptr) {
        GFXSTREAM_ERROR("invalid target!");
        return;
    }
    const BorrowedImageInfoVk* targetImage = getInfoOrAbort(compositionRequest.target);

    auto renderPassIt = m_vkRenderPasses.find(targetImage->imageFormat);
    if (renderPassIt == m_vkRenderPasses.end()) {
        const std::string formatString = ToString(targetImage->imageFormat);
        GFXSTREAM_FATAL("Failed to find VkRenderPass for format %s.", formatString.c_str());
        return;
    }

    RenderTarget* targetImageRenderTarget = getOrCreateRenderTargetInfo(*targetImage);

    const uint32_t targetWidth = targetImage->width;
    const uint32_t targetHeight = targetImage->height;

    compositionVk->targetImage = targetImage;
    compositionVk->targetRenderPass = renderPassIt->second;
    compositionVk->targetFramebuffer = targetImageRenderTarget->m_vkFramebuffer;

    for (const CompositionRequestLayer& layer : compositionRequest.layers) {
        uint32_t sourceImageWidth = 0;
        uint32_t sourceImageHeight = 0;
        const BorrowedImageInfoVk* sourceImage = nullptr;

        if (layer.props.composeMode == HWC2_COMPOSITION_SOLID_COLOR) {
            sourceImageWidth = targetWidth;
            sourceImageHeight = targetHeight;
        } else if (layer.source) {
            sourceImage = getInfoOrAbort(layer.source);
            if (!canCompositeFrom(sourceImage->imageCreateInfo)) {
                continue;
            }

            sourceImageWidth = sourceImage->width;
            sourceImageHeight = sourceImage->height;
        } else {
            GFXSTREAM_ERROR("%s: Invalid layer with compose mode %d", __func__,
                            layer.props.composeMode);
            continue;
        }

        // Calculate the posTransform and the texcoordTransform needed in the
        // uniform of the Compositor.vert shader. The posTransform should transform
        // the square(top = -1, bottom = 1, left = -1, right = 1) to the position
        // where the layer should be drawn in NDC space given the layer.
        // texcoordTransform should transform the unit square(top = 0, bottom = 1,
        // left = 0, right = 1) to where we should sample the layer in the
        // normalized uv space given the composeLayer.
        const hwc_rect_t& posRect = layer.props.displayFrame;
        const hwc_frect_t& texcoordRect = layer.props.crop;

        const int posWidth = posRect.right - posRect.left;
        const int posHeight = posRect.bottom - posRect.top;

        const float posScaleX = float(posWidth) / targetWidth;
        const float posScaleY = float(posHeight) / targetHeight;

        const float posTranslateX = -1.0f + posScaleX + 2.0f * float(posRect.left) / targetWidth;
        const float posTranslateY = -1.0f + posScaleY + 2.0f * float(posRect.top) / targetHeight;

        float texCoordScaleX = (texcoordRect.right - texcoordRect.left) / float(sourceImageWidth);
        float texCoordScaleY = (texcoordRect.bottom - texcoordRect.top) / float(sourceImageHeight);

        float texCoordTranslateX = texcoordRect.left / float(sourceImageWidth);
        float texCoordTranslateY = texcoordRect.top / float(sourceImageHeight);

        float texcoordRotation = 0.0f;

        const float pi = glm::pi<float>();

        switch (layer.props.transform) {
            // Set texCoordTranslate values to keep texcoord outputs in [0-1] range
            // for compatibility with the YUV samplers with CLAMP_TO_EDGE addressing modes.
            // Rotations will be applied around the origin, clockwise. E.g. rotating 90 degrees
            // will map (1,1) to (0,-1), hence texCoordTranslateY+=1 is added.
            //       ^ y
            //       |
            //       +------+ (1,1)
            //       |      |
            //       |      |
            // -(0,0)+------+---> x
            //       |
            case HWC_TRANSFORM_NONE:
                break;
            case HWC_TRANSFORM_ROT_90:
                texcoordRotation = pi * 0.5f;
                texCoordTranslateY += 1.0f;
                break;
            case HWC_TRANSFORM_ROT_180:
                texcoordRotation = pi;
                texCoordTranslateX += 1.0f;
                texCoordTranslateY += 1.0f;
                break;
            case HWC_TRANSFORM_ROT_270:
                texcoordRotation = pi * 1.5f;
                texCoordTranslateX += 1.0f;
                break;
            case HWC_TRANSFORM_FLIP_H:
                texCoordScaleX *= -1.0f;
                texCoordTranslateX += 1.0f;
                break;
            case HWC_TRANSFORM_FLIP_V:
                texCoordScaleY *= -1.0f;
                texCoordTranslateY += 1.0f;
                break;
            case HWC_TRANSFORM_FLIP_H_ROT_90:
                texcoordRotation = pi * 0.5f;
                texCoordScaleX *= -1.0f;
                texCoordTranslateX += 1.0f;
                texCoordTranslateY += 1.0f;
                break;
            case HWC_TRANSFORM_FLIP_V_ROT_90:
                texcoordRotation = pi * 0.5f;
                texCoordScaleY *= -1.0f;
                break;
            default:
                GFXSTREAM_ERROR("Unknown transform:%d", static_cast<int>(layer.props.transform));
                break;
        }

        DescriptorSetContents descriptorSetContents =
            {
                .binding1 =
                    {
                        .positionTransform =
                            glm::translate(glm::mat4(1.0f),
                                           glm::vec3(posTranslateX, posTranslateY, 0.0f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(posScaleX, posScaleY, 1.0f)),
                        .texCoordTransform =
                            glm::translate(glm::mat4(1.0f), glm::vec3(texCoordTranslateX,
                                                                      texCoordTranslateY, 0.0f)) *
                            glm::scale(glm::mat4(1.0f),
                                       glm::vec3(texCoordScaleX, texCoordScaleY, 1.0f)) *
                            glm::rotate(glm::mat4(1.0f), texcoordRotation,
                                        glm::vec3(0.0f, 0.0f, -1.0f)), // rotate clockwise
                        // TODO(b/420586022): Support color transformation on host composition
                        .colorTransform = glm::mat4(1.0f),
                        .mode = glm::uvec4(static_cast<uint32_t>(layer.props.composeMode), 0, 0, 0),
                        .alpha =
                            glm::vec4(layer.props.alpha, layer.props.alpha, layer.props.alpha,
                                      layer.props.alpha),
                    },
            };

        GraphicsPipelineKey graphicsPipelineKey = {
            .renderTargetFormat = targetImage->imageFormat,
            // Undefined means non non-ycbcr.
            .sampledImageFormat = GfxstreamFormat::UNKNOWN,
        };

        if (layer.props.composeMode == HWC2_COMPOSITION_SOLID_COLOR) {
            descriptorSetContents.binding0.sampledImageId = 0;
            descriptorSetContents.binding0.sampledImageView = m_defaultImage.m_vkImageView;
            descriptorSetContents.binding1.color =
                glm::vec4(static_cast<float>(layer.props.color.r) / 255.0f,
                          static_cast<float>(layer.props.color.g) / 255.0f,
                          static_cast<float>(layer.props.color.b) / 255.0f,
                          static_cast<float>(layer.props.color.a) / 255.0f);

        } else {
            if (sourceImage == nullptr) {
                GFXSTREAM_FATAL("CompositorVk failed to find sourceImage.");
            }

            graphicsPipelineKey.sampledImageFormat = sourceImage->imageFormat;

            descriptorSetContents.binding0.sampledImageId = sourceImage->id;
            descriptorSetContents.binding0.sampledImageView = sourceImage->imageView;
            descriptorSetContents.binding0.sampledImageFormat = sourceImage->imageFormat;
            compositionVk->layersSourceImages.emplace_back(sourceImage);
        }

        auto pipelineIt = m_vkGraphicsVkPipelines.find(graphicsPipelineKey);
        if (pipelineIt == m_vkGraphicsVkPipelines.end()) {
            const std::string renderTargetFormatString =
                ToString(graphicsPipelineKey.renderTargetFormat);
            const std::string sampledImageFormatString =
                ToString(graphicsPipelineKey.sampledImageFormat.underlying);
            GFXSTREAM_FATAL("Failed to find pipeline for target:%s sampled:%s",
                            renderTargetFormatString.c_str(), sampledImageFormatString.c_str());
        }

        compositionVk->layersDescriptorSets.descriptorSets.emplace_back(descriptorSetContents);
        compositionVk->layersPipelines.emplace_back(pipelineIt->second);
        compositionVk->layerSourceSamplerFormats.emplace_back(
            graphicsPipelineKey.sampledImageFormat);
    }
}

CompositorVk::CompositionFinishedWaitable CompositorVk::compose(
    const CompositionRequest& compositionRequest) {
    static uint32_t sCompositionNumber = 0;
    const uint32_t thisCompositionNumber = sCompositionNumber++;

    const uint64_t traceId = gfxstream::host::GetUniqueTracingId();
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "CompositorVk::compose()",
                          GFXSTREAM_TRACE_FLOW(traceId), "Composition Number",
                          thisCompositionNumber);

    CompositionVk compositionVk;
    buildCompositionVk(compositionRequest, &compositionVk);

    // Grab and wait for the next available resources.
    if (m_availableFrameResources.empty()) {
        GFXSTREAM_FATAL("CompositorVk failed to get PerFrameResources.");
    }
    auto frameResourceFuture = std::move(m_availableFrameResources.front());
    m_availableFrameResources.pop_front();
    PerFrameResources* frameResources = frameResourceFuture.get();

    updateDescriptorSetsIfChanged(compositionVk.layersDescriptorSets, frameResources);

    std::vector<VkImageMemoryBarrier> preCompositionQueueTransferBarriers;
    std::vector<VkImageMemoryBarrier> preCompositionLayoutTransitionBarriers;
    std::vector<VkImageMemoryBarrier> postCompositionLayoutTransitionBarriers;
    std::vector<VkImageMemoryBarrier> postCompositionQueueTransferBarriers;
    addNeededBarriersToUseBorrowedImage(
        *compositionVk.targetImage, m_queueFamilyIndex, kTargetImageInitialLayoutUsed,
        kTargetImageFinalLayoutUsed, VK_ACCESS_MEMORY_WRITE_BIT,
        &preCompositionQueueTransferBarriers, &preCompositionLayoutTransitionBarriers,
        &postCompositionLayoutTransitionBarriers, &postCompositionQueueTransferBarriers);
    for (const BorrowedImageInfoVk* sourceImage : compositionVk.layersSourceImages) {
        addNeededBarriersToUseBorrowedImage(
            *sourceImage, m_queueFamilyIndex, kSourceImageInitialLayoutUsed,
            kSourceImageFinalLayoutUsed, VK_ACCESS_SHADER_READ_BIT,
            &preCompositionQueueTransferBarriers, &preCompositionLayoutTransitionBarriers,
            &postCompositionLayoutTransitionBarriers, &postCompositionQueueTransferBarriers);
    }

    VkCommandBuffer& commandBuffer = frameResources->m_vkCommandBuffer;
    if (commandBuffer != VK_NULL_HANDLE) {
        m_vk.vkFreeCommandBuffers(m_vkDevice, m_vkCommandPool, 1, &commandBuffer);
    }

    const VkCommandBufferAllocateInfo commandBufferAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_vkCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(m_vk.vkAllocateCommandBuffers(m_vkDevice, &commandBufferAllocInfo, &commandBuffer));

    m_debugUtilsHelper.addDebugLabel(commandBuffer, "CompositorVk composition:%d command buffer",
                                     thisCompositionNumber);

    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(m_vk.vkBeginCommandBuffer(commandBuffer, &beginInfo));

    m_debugUtilsHelper.cmdBeginDebugLabel(commandBuffer,
                                          "CompositorVk composition:%d into ColorBuffer:%d",
                                          thisCompositionNumber, compositionVk.targetImage->id);

    if (!preCompositionQueueTransferBarriers.empty()) {
        m_vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                                  static_cast<uint32_t>(preCompositionQueueTransferBarriers.size()),
                                  preCompositionQueueTransferBarriers.data());
    }
    if (!preCompositionLayoutTransitionBarriers.empty()) {
        m_vk.vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr,
            static_cast<uint32_t>(preCompositionLayoutTransitionBarriers.size()),
            preCompositionLayoutTransitionBarriers.data());
    }

    const VkClearValue renderTargetClearColor = {
        .color =
            {
                .float32 = {0.0f, 0.0f, 0.0f, 1.0f},
            },
    };
    const VkRenderPassBeginInfo renderPassBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = compositionVk.targetRenderPass,
        .framebuffer = compositionVk.targetFramebuffer,
        .renderArea =
            {
                .offset =
                    {
                        .x = 0,
                        .y = 0,
                    },
                .extent =
                    {
                        .width = compositionVk.targetImage->imageCreateInfo.extent.width,
                        .height = compositionVk.targetImage->imageCreateInfo.extent.height,
                    },
            },
        .clearValueCount = 1,
        .pClearValues = &renderTargetClearColor,
    };
    m_vk.vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    const VkDeviceSize offsets[] = {0};
    m_vk.vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexVkBuffer, offsets);

    m_vk.vkCmdBindIndexBuffer(commandBuffer, m_indexVkBuffer, 0, VK_INDEX_TYPE_UINT16);

    const VkRect2D scissor = {
        .offset =
            {
                .x = 0,
                .y = 0,
            },
        .extent =
            {
                .width = compositionVk.targetImage->imageCreateInfo.extent.width,
                .height = compositionVk.targetImage->imageCreateInfo.extent.height,
            },
    };
    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(compositionVk.targetImage->imageCreateInfo.extent.width),
        .height = static_cast<float>(compositionVk.targetImage->imageCreateInfo.extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    const uint32_t numLayers = compositionVk.layersDescriptorSets.descriptorSets.size();
    VkPipeline lastPipeline = VK_NULL_HANDLE;
    for (uint32_t layerIndex = 0; layerIndex < numLayers; ++layerIndex) {
        m_debugUtilsHelper.cmdBeginDebugLabel(commandBuffer, "CompositorVk compose layer:%d",
                                              layerIndex);

        VkPipeline pipelineToUse = compositionVk.layersPipelines[layerIndex];
        if (lastPipeline != pipelineToUse) {
            m_vk.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineToUse);
            lastPipeline = pipelineToUse;

            m_vk.vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            m_vk.vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        }

        YuvOrDefaultGfxstreamFormat sampledImageFormat =
            compositionVk.layerSourceSamplerFormats[layerIndex];

        VkPipelineLayout layerPipelineLayout = m_formatResources[sampledImageFormat].pipelineLayout;
        VkDescriptorSet layerDescriptorSet =
            frameResources->m_layerDescriptorSets[sampledImageFormat][layerIndex];

        m_vk.vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     layerPipelineLayout,
                                     /*firstSet=*/0,
                                     /*descriptorSetCount=*/1, &layerDescriptorSet,
                                     /*dynamicOffsetCount=*/0,
                                     /*pDynamicOffsets=*/nullptr);

        m_vk.vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(k_indices.size()), 1, 0, 0, 0);

        m_debugUtilsHelper.cmdEndDebugLabel(commandBuffer);
    }

    m_vk.vkCmdEndRenderPass(commandBuffer);

    // Insert a VkImageMemoryBarrier so that the vkCmdBlitImage in post will wait for the rendering
    // to the render target to complete.
    const VkImageMemoryBarrier renderTargetBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = compositionVk.targetImage->image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    m_vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              /*dependencyFlags=*/0,
                              /*memoryBarrierCount=*/0,
                              /*pMemoryBarriers=*/nullptr,
                              /*bufferMemoryBarrierCount=*/0,
                              /*pBufferMemoryBarriers=*/nullptr, 1, &renderTargetBarrier);

    if (!postCompositionLayoutTransitionBarriers.empty()) {
        m_vk.vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr,
            static_cast<uint32_t>(postCompositionLayoutTransitionBarriers.size()),
            postCompositionLayoutTransitionBarriers.data());
    }
    if (!postCompositionQueueTransferBarriers.empty()) {
        m_vk.vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr,
            static_cast<uint32_t>(postCompositionQueueTransferBarriers.size()),
            postCompositionQueueTransferBarriers.data());
    }

    m_debugUtilsHelper.cmdEndDebugLabel(commandBuffer);

    VK_CHECK(m_vk.vkEndCommandBuffer(commandBuffer));

    VkFence composeCompleteFence = frameResources->m_vkFence;
    m_debugUtilsHelper.addDebugLabel(
        composeCompleteFence, "CompositorVk composition:%d complete fence", thisCompositionNumber);

    VK_CHECK(m_vk.vkResetFences(m_vkDevice, 1, &composeCompleteFence));

    const VkPipelineStageFlags submitWaitStages[] = {
        VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = submitWaitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    {
        gfxstream::base::AutoLock lock(*m_vkQueueLock);
        VK_CHECK(m_vk.vkQueueSubmit(m_vkQueue, 1, &submitInfo, composeCompleteFence));
    }

    // Create a future that will return the PerFrameResources to the next
    // iteration of CompostiorVk::compose() once this current composition
    // completes.
    std::shared_future<PerFrameResources*> composeCompleteFutureForResources =
        std::async(std::launch::deferred, [composeCompleteFence, frameResources, traceId,
                                           this]() mutable {
            (void)traceId;
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "Wait for compose fence",
                                  GFXSTREAM_TRACE_FLOW(traceId));

            VkResult res = m_vk.vkWaitForFences(m_vkDevice, 1, &composeCompleteFence, VK_TRUE,
                                                kVkWaitForFencesTimeoutNsecs);
            if (res == VK_TIMEOUT) {
                // Retry. If device lost, hopefully this returns immediately.
                res = m_vk.vkWaitForFences(m_vkDevice, 1, &composeCompleteFence, VK_TRUE,
                                           kVkWaitForFencesTimeoutNsecs);
            }
            VK_CHECK(res);
            return frameResources;
        }).share();
    m_availableFrameResources.push_back(composeCompleteFutureForResources);

    // Create a future that will return once this current composition
    // completes that can be shared outside of CompositorVk.
    std::shared_future<void> composeCompleteFuture =
        std::async(std::launch::deferred, [composeCompleteFutureForResources]() {
            composeCompleteFutureForResources.get();
        }).share();

    return composeCompleteFuture;
}

void CompositorVk::setScreenMask(int width, int height, const uint8_t* rgbaData) {
    setUpScreenMaskImage(uint32_t(width), uint32_t(height), rgbaData);
}

void CompositorVk::setScreenBackground(int width, int height, const uint8_t* rgbaData) {
    setUpScreenBackgroundImage(uint32_t(width), uint32_t(height), rgbaData);
}

void CompositorVk::onImageDestroyed(uint32_t imageId) { m_renderTargetCache.remove(imageId); }

bool operator==(const CompositorVkBase::DescriptorSetContents& lhs,
                const CompositorVkBase::DescriptorSetContents& rhs) {
    return std::tie(lhs.binding0.sampledImageId,      //
                    lhs.binding0.sampledImageView,    //
                    lhs.binding0.sampledImageFormat,  //
                    lhs.binding1.mode,                //
                    lhs.binding1.alpha,               //
                    lhs.binding1.color,               //
                    lhs.binding1.positionTransform,   //
                    lhs.binding1.texCoordTransform,   //
                    lhs.binding1.colorTransform)      //
           ==                                         //
           std::tie(rhs.binding0.sampledImageId,      //
                    rhs.binding0.sampledImageView,    //
                    rhs.binding0.sampledImageFormat,  //
                    rhs.binding1.mode,                //
                    rhs.binding1.alpha,               //
                    rhs.binding1.color,               //
                    rhs.binding1.positionTransform,   //
                    rhs.binding1.texCoordTransform,   //
                    rhs.binding1.colorTransform);
}

bool operator==(const CompositorVkBase::FrameDescriptorSetsContents& lhs,
                const CompositorVkBase::FrameDescriptorSetsContents& rhs) {
    return lhs.descriptorSets == rhs.descriptorSets;
}

void CompositorVk::updateDescriptorSetsIfChanged(
    const FrameDescriptorSetsContents& descriptorSetsContents, PerFrameResources* frameResources) {
    if (frameResources->m_vkDescriptorSetsContents == descriptorSetsContents) {
        return;
    }

    const uint32_t numRequestedLayers =
        static_cast<uint32_t>(descriptorSetsContents.descriptorSets.size());
    if (numRequestedLayers > kMaxLayersPerFrame) {
        GFXSTREAM_FATAL("CompositorVk can't compose more than %" PRIu32
                        " layers. layers asked: %" PRIu32,
                        kMaxLayersPerFrame, numRequestedLayers);
        return;
    }

    std::vector<VkDescriptorImageInfo> descriptorImageInfos(numRequestedLayers);
    std::vector<VkWriteDescriptorSet> descriptorWrites(numRequestedLayers);
    for (uint32_t layerIndex = 0; layerIndex < numRequestedLayers; ++layerIndex) {
        const DescriptorSetContents& layerDescriptorSetContents =
            descriptorSetsContents.descriptorSets[layerIndex];
        const YuvOrDefaultGfxstreamFormat sampledImageFormat =
            layerDescriptorSetContents.binding0.sampledImageFormat;

        descriptorImageInfos[layerIndex] = VkDescriptorImageInfo{
            // Empty as we only use immutable samplers.
            .sampler = VK_NULL_HANDLE,
            .imageView = layerDescriptorSetContents.binding0.sampledImageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        descriptorWrites[layerIndex] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frameResources->m_layerDescriptorSets[sampledImageFormat][layerIndex],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &descriptorImageInfos[layerIndex],
        };

        UniformBufferBinding* layerUboStorage =
            frameResources->m_layerUboStorages[sampledImageFormat][layerIndex];
        *layerUboStorage = layerDescriptorSetContents.binding1;
    }

    m_vk.vkUpdateDescriptorSets(m_vkDevice, descriptorWrites.size(), descriptorWrites.data(), 0,
                                nullptr);

    frameResources->m_vkDescriptorSetsContents = descriptorSetsContents;
}

void CompositorVk::drawScreenMask(VkCommandBuffer commandBuffer, VkFormat targetFormat,
                                  uint32_t targetWidth, uint32_t targetHeight,
                                  VkRenderPass targetRenderPass, VkFramebuffer targetFramebuffer,
                                  ImmediateModeResources* frameResources, float rotationDegrees) {
    std::lock_guard<std::mutex> lock(mScreenImagesMutex);
    if (!hasScreenMask()) {
        return;
    }

    drawImage(commandBuffer, targetFormat, targetWidth, targetHeight, targetRenderPass,
              targetFramebuffer, frameResources, m_screenMaskImage.m_vkImageView, rotationDegrees);
}

void CompositorVk::drawScreenBackground(VkCommandBuffer commandBuffer, VkFormat targetFormat,
                                        uint32_t targetWidth, uint32_t targetHeight,
                                        VkRenderPass targetRenderPass,
                                        VkFramebuffer targetFramebuffer,
                                        ImmediateModeResources* frameResources,
                                        float rotationDegrees) {
    std::lock_guard<std::mutex> lock(mScreenImagesMutex);
    if (!hasScreenBackground()) {
        return;
    }

    drawImage(commandBuffer, targetFormat, targetWidth, targetHeight, targetRenderPass,
              targetFramebuffer, frameResources, m_screenBackgroundImage.m_vkImageView,
              rotationDegrees);
}

void CompositorVk::drawImage(VkCommandBuffer commandBuffer, VkFormat targetFormatVk,
                             uint32_t targetWidth, uint32_t targetHeight,
                             VkRenderPass targetRenderPass, VkFramebuffer targetFramebuffer,
                             ImmediateModeResources* frameResources, VkImageView imageView,
                             float rotationDegrees,
                             const std::optional<std::array<float, 16>>& colorTransform) {
    if (frameResources->m_curDataIndex >= kMaxImmediateDrawsPerFrame) {
        GFXSTREAM_ERROR("CompositorVk::%s Requested too many immediate mode draws", __func__);
        return;
    }

    // If a screen mask is set, add another layer to draw the mask image
    const YuvOrDefaultGfxstreamFormat sampledImageFormat;

    auto targetFormatOpt = ToGfxstreamFormat(targetFormatVk);
    if (!targetFormatOpt) {
        GFXSTREAM_FATAL("Failed to convert format %s.", string_VkFormat(targetFormatVk));
        return;
    }
    const GfxstreamFormat targetFormat = *targetFormatOpt;

    const GraphicsPipelineKey graphicsPipelineKey = {
        .renderTargetFormat = targetFormat,
        .sampledImageFormat = sampledImageFormat,
    };
    auto pipelineIt = m_vkGraphicsVkPipelines.find(graphicsPipelineKey);
    if (pipelineIt == m_vkGraphicsVkPipelines.end()) {
        const std::string renderTargetFormatString =
            ToString(graphicsPipelineKey.renderTargetFormat);
        const std::string sampledImageFormatString =
            ToString(graphicsPipelineKey.sampledImageFormat.underlying);

        GFXSTREAM_FATAL("Failed to find pipeline resources for render-target:%s sampled-image:%s",
                        renderTargetFormatString.c_str(), sampledImageFormatString.c_str());
        return;
    }

    VkDescriptorSet descriptorSet =
        frameResources->m_descriptorSets[frameResources->m_curDataIndex];
    UniformBufferBinding* uboStorage =
        frameResources->m_uboStorages[frameResources->m_curDataIndex];
    frameResources->m_curDataIndex++;

    // Determine the texture coordinate translation to ensure clamped texture addressing will work
    float texCoordTranslateX = 0;
    float texCoordTranslateY = 0;
    const float epsilon = 1e-5;
    if (fabsf(rotationDegrees) <= epsilon) {
        // HWC_TRANSFORM_NONE
    } else if (fabsf(rotationDegrees - 90.0f) <= epsilon) {
        // HWC_TRANSFORM_ROT_90
        texCoordTranslateY = 1.0f;
    } else if (fabsf(rotationDegrees - 180.0f) <= epsilon) {
        // HWC_TRANSFORM_ROT_180
        texCoordTranslateX = 1.0f;
        texCoordTranslateY = 1.0f;
    } else if (fabsf(rotationDegrees - 270.0f) <= epsilon) {
        // HWC_TRANSFORM_ROT_270
        texCoordTranslateX = 1.0f;
    } else {
        GFXSTREAM_WARNING("Unsupported rotation value: %.3f", rotationDegrees);
    }

    const float pi = glm::pi<float>();
    UniformBufferBinding uboContents = {
        .positionTransform = glm::mat4(1.0f),
        .texCoordTransform = glm::translate(glm::mat4(1.0f), glm::vec3(texCoordTranslateX,
                                                                       texCoordTranslateY, 0.0f)) *
                             glm::rotate(glm::mat4(1.0f), (rotationDegrees * pi) / 180.0f,
                                         glm::vec3(0.0f, 0.0f, -1.0f)),
        .colorTransform = glm::mat4(1.0f),
        .mode = glm::uvec4(static_cast<uint32_t>(HWC2_COMPOSITION_DEVICE), 1.0f, 0, 0),
        .alpha = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
    };
    if (colorTransform.has_value()) {
        const std::array<float, 16>& matrix = colorTransform.value();
        uboContents.colorTransform = glm::mat4(matrix[0], matrix[1], matrix[2], matrix[3],    //
                                               matrix[4], matrix[5], matrix[6], matrix[7],    //
                                               matrix[8], matrix[9], matrix[10], matrix[11],  //
                                               matrix[12], matrix[13], matrix[14], matrix[15]);
    }

    {
        memcpy(uboStorage, &uboContents, sizeof(UniformBufferBinding));

        VkDescriptorImageInfo descriptorImageInfo = VkDescriptorImageInfo{
            .sampler = VK_NULL_HANDLE,  // using immutable samplers in layout
            .imageView = imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet descriptorWrites[2];
        descriptorWrites[0] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &descriptorImageInfo,
        };

        VkDeviceSize bufferOffset = reinterpret_cast<VkDeviceSize>(uboStorage) -
                                    reinterpret_cast<VkDeviceSize>(m_uniformStorage.m_mappedPtr);
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = m_uniformStorage.m_vkBuffer,
            .offset = bufferOffset,
            .range = sizeof(UniformBufferBinding),
        };
        descriptorWrites[1] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfo,
        };

        m_vk.vkUpdateDescriptorSets(m_vkDevice, 2, descriptorWrites, 0, nullptr);
    }

    const VkRenderPassBeginInfo renderPassBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = targetRenderPass,
        .framebuffer = targetFramebuffer,
        .renderArea =
            {
                .offset =
                    {
                        .x = 0,
                        .y = 0,
                    },
                .extent =
                    {
                        .width = targetWidth,
                        .height = targetHeight,
                    },
            },
        .clearValueCount = 0,
        .pClearValues = nullptr,
    };

    const VkRect2D scissor = {
        .offset =
            {
                .x = 0,
                .y = 0,
            },
        .extent =
            {
                .width = targetWidth,
                .height = targetHeight,
            },
    };
    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(targetWidth),
        .height = static_cast<float>(targetHeight),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    m_vk.vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    const VkDeviceSize offsets[] = {0};
    m_vk.vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexVkBuffer, offsets);

    m_vk.vkCmdBindIndexBuffer(commandBuffer, m_indexVkBuffer, 0, VK_INDEX_TYPE_UINT16);

    m_vk.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineIt->second);
    m_vk.vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    m_vk.vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    m_vk.vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 m_formatResources[sampledImageFormat].pipelineLayout, 0, 1,
                                 &descriptorSet, 0, nullptr);

    m_vk.vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(k_indices.size()), 1, 0, 0, 0);

    m_vk.vkCmdEndRenderPass(commandBuffer);
}

CompositorVk::ImmediateModeResources* CompositorVk::acquireImmediateModeResources() {
    // Grab and wait for the next available resources.
    for (auto& res : m_immediateFrameResources) {
        std::lock_guard<std::mutex> lock(res.m_isFreeMutex);
        if (res.m_isFree) {
            res.init();
            return &res;
        }
    }

    // TODO: make sure immediate mode resources are always released and waited properly
    GFXSTREAM_ERROR("CompositorVk::%s failed to get resources.", __func__);
    return nullptr;
}

void CompositorVk::releaseImmediateModeResources(ImmediateModeResources* imResources) {
    if (imResources) {
        std::lock_guard<std::mutex> lock(imResources->m_isFreeMutex);
        imResources->reset();
    }
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
