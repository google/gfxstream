#include "CompositorVk.h"

#include <string.h>

#include <cinttypes>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>

#include "gfxstream/host/Tracing.h"
#include "gfxstream/host/logging.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vk_util.h"

namespace gfxstream {
namespace vk {

using emugl::ABORT_REASON_OTHER;
using emugl::FatalError;

namespace CompositorVkShader {
#include "vulkan/CompositorFragmentShader.h"
#include "vulkan/CompositorVertexShader.h"
}  // namespace CompositorVkShader

namespace {

constexpr const VkImageLayout kSourceImageInitialLayoutUsed =
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
constexpr const VkImageLayout kSourceImageFinalLayoutUsed =
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

constexpr const VkImageLayout kTargetImageInitialLayoutUsed = VK_IMAGE_LAYOUT_UNDEFINED;
constexpr const VkImageLayout kTargetImageFinalLayoutUsed = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

const BorrowedImageInfoVk* getInfoOrAbort(const std::unique_ptr<BorrowedImageInfo>& info) {
    auto imageVk = static_cast<const BorrowedImageInfoVk*>(info.get());
    if (imageVk != nullptr) {
        return imageVk;
    }

    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
        << "CompositorVk did not find BorrowedImageInfoVk";
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
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "CompositorVk found empty image view handle when creating RenderTarget. Image: "
            << m_vkImage << " Dimensions: " << m_width << "x" << m_height;
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
    std::shared_ptr<android::base::Lock> queueLock, uint32_t queueFamilyIndex,
    uint32_t maxFramesInFlight, DebugUtilsHelper debugUtils) {
    auto res = std::unique_ptr<CompositorVk>(new CompositorVk(vk, vkDevice, vkPhysicalDevice,
                                                              vkQueue, queueLock, queueFamilyIndex,
                                                              maxFramesInFlight, debugUtils));
    res->setUpCommandPool();
    res->setUpSampler();
    res->setUpGraphicsPipeline();
    res->setUpVertexBuffers();
    res->setUpUniformBuffers();
    res->setUpDescriptorSets();
    res->setUpFences();
    res->setUpDefaultImage();
    res->setUpFrameResourceFutures();
    return res;
}

CompositorVk::CompositorVk(const VulkanDispatch& vk, VkDevice vkDevice,
                           VkPhysicalDevice vkPhysicalDevice, VkQueue vkQueue,
                           std::shared_ptr<android::base::Lock> queueLock,
                           uint32_t queueFamilyIndex, uint32_t maxFramesInFlight,
                           DebugUtilsHelper debugUtilsHelper)
    : CompositorVkBase(vk, vkDevice, vkPhysicalDevice, vkQueue, queueLock, queueFamilyIndex,
                       maxFramesInFlight, debugUtilsHelper),
      m_maxFramesInFlight(maxFramesInFlight),
      m_renderTargetCache(k_renderTargetCacheSize) {}

CompositorVk::~CompositorVk() {
    {
        android::base::AutoLock lock(*m_vkQueueLock);
        VK_CHECK(vk_util::waitForVkQueueIdleWithRetry(m_vk, m_vkQueue));
    }
    if (m_defaultImage.m_vkImageView != VK_NULL_HANDLE) {
        m_vk.vkDestroyImageView(m_vkDevice, m_defaultImage.m_vkImageView, nullptr);
    }
    if (m_defaultImage.m_vkImage != VK_NULL_HANDLE) {
        m_vk.vkDestroyImage(m_vkDevice, m_defaultImage.m_vkImage, nullptr);
    }
    if (m_defaultImage.m_vkImageMemory != VK_NULL_HANDLE) {
        m_vk.vkFreeMemory(m_vkDevice, m_defaultImage.m_vkImageMemory, nullptr);
    }
    m_vk.vkDestroyDescriptorPool(m_vkDevice, m_vkDescriptorPool, nullptr);
    if (m_uniformStorage.m_vkDeviceMemory != VK_NULL_HANDLE) {
        m_vk.vkUnmapMemory(m_vkDevice, m_uniformStorage.m_vkDeviceMemory);
    }
    m_vk.vkDestroyBuffer(m_vkDevice, m_uniformStorage.m_vkBuffer, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, m_uniformStorage.m_vkDeviceMemory, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, m_vertexVkDeviceMemory, nullptr);
    m_vk.vkDestroyBuffer(m_vkDevice, m_vertexVkBuffer, nullptr);
    m_vk.vkFreeMemory(m_vkDevice, m_indexVkDeviceMemory, nullptr);
    m_vk.vkDestroyBuffer(m_vkDevice, m_indexVkBuffer, nullptr);
    for (auto& [_, formatResources] : m_formatResources) {
        m_vk.vkDestroyPipeline(m_vkDevice, formatResources.m_graphicsVkPipeline, nullptr);
        m_vk.vkDestroyRenderPass(m_vkDevice, formatResources.m_vkRenderPass, nullptr);
    }
    m_vk.vkDestroyPipelineLayout(m_vkDevice, m_vkPipelineLayout, nullptr);
    m_vk.vkDestroySampler(m_vkDevice, m_vkSampler, nullptr);
    m_vk.vkDestroyDescriptorSetLayout(m_vkDevice, m_vkDescriptorSetLayout, nullptr);
    m_vk.vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);
    for (PerFrameResources& frameResources : m_frameResources) {
        m_vk.vkDestroyFence(m_vkDevice, frameResources.m_vkFence, nullptr);
    }
}

void CompositorVk::setUpGraphicsPipeline() {
    const std::vector<uint32_t> vertSpvBuff(CompositorVkShader::compositorVertexShader,
                                            std::end(CompositorVkShader::compositorVertexShader));
    const std::vector<uint32_t> fragSpvBuff(CompositorVkShader::compositorFragmentShader,
                                            std::end(CompositorVkShader::compositorFragmentShader));
    const auto vertShaderMod = createShaderModule(m_vk, m_vkDevice, vertSpvBuff);
    const auto fragShaderMod = createShaderModule(m_vk, m_vkDevice, fragSpvBuff);

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

    const VkDescriptorSetLayoutBinding layoutBindings[2] = {
        VkDescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = &m_vkSampler,
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
    VK_CHECK(m_vk.vkCreateDescriptorSetLayout(m_vkDevice, &descriptorSetLayoutCi, nullptr,
                                              &m_vkDescriptorSetLayout));

    const VkPipelineLayoutCreateInfo pipelineLayoutCi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_vkDescriptorSetLayout,
        .pushConstantRangeCount = 0,
    };

    VK_CHECK(
        m_vk.vkCreatePipelineLayout(m_vkDevice, &pipelineLayoutCi, nullptr, &m_vkPipelineLayout));

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
        .layout = m_vkPipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    const std::vector<VkFormat> kRenderTargetFormats = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
    };
    for (VkFormat renderTargetFormat : kRenderTargetFormats) {
        colorAttachment.format = renderTargetFormat;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VK_CHECK(m_vk.vkCreateRenderPass(m_vkDevice, &renderPassCi, nullptr, &renderPass));

        graphicsPipelineCi.renderPass = renderPass;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VK_CHECK(m_vk.vkCreateGraphicsPipelines(m_vkDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCi,
                                                nullptr, &pipeline));

        m_formatResources[renderTargetFormat] = PerFormatResources{
            .m_vkRenderPass = renderPass,
            .m_graphicsVkPipeline = pipeline,
        };
    }

    m_vk.vkDestroyShaderModule(m_vkDevice, vertShaderMod, nullptr);
    m_vk.vkDestroyShaderModule(m_vkDevice, fragShaderMod, nullptr);
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
    const uint32_t descriptorSetsPerFrame = kMaxLayersPerFrame;
    const uint32_t descriptorSetsTotal = descriptorSetsPerFrame * m_maxFramesInFlight;

    const uint32_t descriptorsOfEachTypePerSet = 1;
    const uint32_t descriptorsOfEachTypePerFrame =
        descriptorSetsPerFrame * descriptorsOfEachTypePerSet;
    const uint32_t descriptorsOfEachTypeTotal = descriptorsOfEachTypePerFrame * m_maxFramesInFlight;

    const VkDescriptorPoolSize descriptorPoolSizes[2] = {
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = descriptorsOfEachTypeTotal,
        },
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = descriptorsOfEachTypeTotal,
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

    const std::vector<VkDescriptorSetLayout> frameDescriptorSetLayouts(descriptorSetsPerFrame,
                                                                       m_vkDescriptorSetLayout);
    const VkDescriptorSetAllocateInfo frameDescriptorSetAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_vkDescriptorPool,
        .descriptorSetCount = descriptorSetsPerFrame,
        .pSetLayouts = frameDescriptorSetLayouts.data(),
    };

    VkDeviceSize uniformBufferOffset = 0;
    for (uint32_t frameIndex = 0; frameIndex < m_maxFramesInFlight; ++frameIndex) {
        PerFrameResources& frameResources = m_frameResources[frameIndex];
        frameResources.m_layerDescriptorSets.resize(descriptorSetsPerFrame);

        VK_CHECK(m_vk.vkAllocateDescriptorSets(m_vkDevice, &frameDescriptorSetAllocInfo,
                                               frameResources.m_layerDescriptorSets.data()));

        for (uint32_t layerIndex = 0; layerIndex < kMaxLayersPerFrame; ++layerIndex) {
            const VkDescriptorBufferInfo bufferInfo = {
                .buffer = m_uniformStorage.m_vkBuffer,
                .offset = uniformBufferOffset,
                .range = sizeof(UniformBufferBinding),
            };
            const VkWriteDescriptorSet descriptorSetWrite = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frameResources.m_layerDescriptorSets[layerIndex],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bufferInfo,
            };
            m_vk.vkUpdateDescriptorSets(m_vkDevice, 1, &descriptorSetWrite, 0, nullptr);

            uniformBufferOffset += m_uniformStorage.m_stride;
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

        VkFence fence;
        VK_CHECK(m_vk.vkCreateFence(m_vkDevice, &fenceCi, nullptr, &fence));

        frameResources.m_vkFence = fence;
    }
}

void CompositorVk::setUpDefaultImage() {
    const VkImageCreateInfo imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent =
            {
                .width = 2,
                .height = 2,
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
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "CompositorVk failed to find memory type for default image.";
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

    const std::vector<uint8_t> pixels = {
        0xFF, 0x00, 0xFF, 0xFF,  //
        0xFF, 0x00, 0xFF, 0xFF,  //
        0xFF, 0x00, 0xFF, 0xFF,  //
        0xFF, 0x00, 0xFF, 0xFF,  //
    };
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    std::tie(stagingBuffer, stagingBufferMemory) =
        createStagingBufferWithData(pixels.data(), pixels.size());

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
                    .width = 2,
                    .height = 2,
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

    m_defaultImage.m_vkImage = image;
    m_defaultImage.m_vkImageView = imageView;
    m_defaultImage.m_vkImageMemory = imageMemory;
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

void CompositorVk::setUpUniformBuffers() {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    m_vk.vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &physicalDeviceProperties);
    const VkDeviceSize alignment = physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
    m_uniformStorage.m_stride = ((sizeof(UniformBufferBinding) - 1) / alignment + 1) * alignment;

    VkDeviceSize size = m_uniformStorage.m_stride * m_maxFramesInFlight * kMaxLayersPerFrame;
    auto maybeBuffer =
        createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                         VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    auto buffer = std::make_tuple<VkBuffer, VkDeviceMemory>(VK_NULL_HANDLE, VK_NULL_HANDLE);
    if (maybeBuffer.has_value()) {
        buffer = maybeBuffer.value();
    } else {
        buffer =
            createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                .value();
    }
    std::tie(m_uniformStorage.m_vkBuffer, m_uniformStorage.m_vkDeviceMemory) = buffer;

    void* mapped = nullptr;
    VK_CHECK(m_vk.vkMapMemory(m_vkDevice, m_uniformStorage.m_vkDeviceMemory, 0, size, 0, &mapped));

    uint8_t* data = reinterpret_cast<uint8_t*>(mapped);
    for (uint32_t frameIndex = 0; frameIndex < m_maxFramesInFlight; ++frameIndex) {
        PerFrameResources& frameResources = m_frameResources[frameIndex];
        for (uint32_t layerIndex = 0; layerIndex < kMaxLayersPerFrame; ++layerIndex) {
            auto* layerUboStorage = reinterpret_cast<UniformBufferBinding*>(data);
            frameResources.m_layerUboStorages.push_back(layerUboStorage);
            data += m_uniformStorage.m_stride;
        }
    }
}

void CompositorVk::setUpSampler() {
    // The texture coordinate transformation matrices for flip/rotate/etc
    // currently depends on this being repeat.
    constexpr const VkSamplerAddressMode kSamplerMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;

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
    VK_CHECK(m_vk.vkCreateSampler(m_vkDevice, &samplerCi, nullptr, &m_vkSampler));
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

    auto formatResourcesIt = m_formatResources.find(imageInfo.imageCreateInfo.format);
    if (formatResourcesIt == m_formatResources.end()) {
        return nullptr;
    }
    auto& formatResources = formatResourcesIt->second;

    auto* renderTarget =
        new RenderTarget(m_vk, m_vkDevice, imageInfo.image, imageInfo.imageView,
                         imageInfo.imageCreateInfo.extent.width,
                         imageInfo.imageCreateInfo.extent.height, formatResources.m_vkRenderPass);

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

bool CompositorVk::canCompositeTo(const VkImageCreateInfo& imageCi) {
    VkFormatFeatureFlags formatFeatures = getFormatFeatures(imageCi.format, imageCi.tiling);
    if (!(formatFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
        GFXSTREAM_ERROR(
            "The format, %s, with tiling, %s, doesn't support the "
            "VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT feature. All supported features are %s.",
            string_VkFormat(imageCi.format), string_VkImageTiling(imageCi.tiling),
            string_VkFormatFeatureFlags(formatFeatures).c_str());
        return false;
    }
    if (!(imageCi.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        GFXSTREAM_ERROR(
            "The VkImage is not created with the VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT usage flag. "
            "The usage flags are %s.",
            string_VkImageUsageFlags(imageCi.usage).c_str());
        return false;
    }

    if (m_formatResources.find(imageCi.format) == m_formatResources.end()) {
        GFXSTREAM_ERROR(
            "The format of the image, %s, is not supported by the CompositorVk as the render "
            "target.",
            string_VkFormat(imageCi.format));
        return false;
    }
    return true;
}

void CompositorVk::buildCompositionVk(const CompositionRequest& compositionRequest,
                                      CompositionVk* compositionVk) {
    const BorrowedImageInfoVk* targetImage = getInfoOrAbort(compositionRequest.target);

    auto formatResourcesIt = m_formatResources.find(targetImage->imageCreateInfo.format);
    if (formatResourcesIt == m_formatResources.end()) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "CompositorVk did not find format resource for format "
            << targetImage->imageCreateInfo.format;
    }
    const auto& formatResources = formatResourcesIt->second;

    RenderTarget* targetImageRenderTarget = getOrCreateRenderTargetInfo(*targetImage);

    const uint32_t targetWidth = targetImage->width;
    const uint32_t targetHeight = targetImage->height;

    compositionVk->targetImage = targetImage;
    compositionVk->targetRenderPass = formatResources.m_vkRenderPass;
    compositionVk->targetFramebuffer = targetImageRenderTarget->m_vkFramebuffer;
    compositionVk->pipeline = formatResources.m_graphicsVkPipeline;

    for (const CompositionRequestLayer& layer : compositionRequest.layers) {
        uint32_t sourceImageWidth = 0;
        uint32_t sourceImageHeight = 0;
        const BorrowedImageInfoVk* sourceImage = nullptr;

        if (layer.props.composeMode == HWC2_COMPOSITION_SOLID_COLOR) {
            sourceImageWidth = targetWidth;
            sourceImageHeight = targetHeight;
        } else {
            sourceImage = getInfoOrAbort(layer.source);
            if (!canCompositeFrom(sourceImage->imageCreateInfo)) {
                continue;
            }

            sourceImageWidth = sourceImage->width;
            sourceImageHeight = sourceImage->height;
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

        const float texCoordTranslateX = texcoordRect.left / float(sourceImageWidth);
        const float texCoordTranslateY = texcoordRect.top / float(sourceImageHeight);

        float texcoordRotation = 0.0f;

        const float pi = glm::pi<float>();

        switch (layer.props.transform) {
            case HWC_TRANSFORM_NONE:
                break;
            case HWC_TRANSFORM_ROT_90:
                texcoordRotation = pi * 0.5f;
                break;
            case HWC_TRANSFORM_ROT_180:
                texcoordRotation = pi;
                break;
            case HWC_TRANSFORM_ROT_270:
                texcoordRotation = pi * 1.5f;
                break;
            case HWC_TRANSFORM_FLIP_H:
                texCoordScaleX *= -1.0f;
                break;
            case HWC_TRANSFORM_FLIP_V:
                texCoordScaleY *= -1.0f;
                break;
            case HWC_TRANSFORM_FLIP_H_ROT_90:
                texcoordRotation = pi * 0.5f;
                texCoordScaleX *= -1.0f;
                break;
            case HWC_TRANSFORM_FLIP_V_ROT_90:
                texcoordRotation = pi * 0.5f;
                texCoordScaleY *= -1.0f;
                break;
            default:
                GFXSTREAM_ERROR("Unknown transform:%d", static_cast<int>(layer.props.transform));
                break;
        }

        DescriptorSetContents descriptorSetContents = {
            .binding1 =
                {
                    .positionTransform =
                        glm::translate(glm::mat4(1.0f),
                                       glm::vec3(posTranslateX, posTranslateY, 0.0f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(posScaleX, posScaleY, 1.0f)),
                    .texCoordTransform =
                        glm::translate(glm::mat4(1.0f),
                                       glm::vec3(texCoordTranslateX, texCoordTranslateY, 0.0f)) *
                        glm::scale(glm::mat4(1.0f),
                                   glm::vec3(texCoordScaleX, texCoordScaleY, 1.0f)) *
                        glm::rotate(glm::mat4(1.0f), texcoordRotation, glm::vec3(0.0f, 0.0f, 1.0f)),
                    .mode = glm::uvec4(static_cast<uint32_t>(layer.props.composeMode), 0, 0, 0),
                    .alpha =
                        glm::vec4(layer.props.alpha, layer.props.alpha, layer.props.alpha,
                                  layer.props.alpha),
                },
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
                GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                    << "CompositorVk failed to find sourceImage.";
            }
            descriptorSetContents.binding0.sampledImageId = sourceImage->id;
            descriptorSetContents.binding0.sampledImageView = sourceImage->imageView;
            compositionVk->layersSourceImages.emplace_back(sourceImage);
        }

        compositionVk->layersDescriptorSets.descriptorSets.emplace_back(descriptorSetContents);
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
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "CompositorVk failed to get PerFrameResources.";
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

    m_vk.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionVk.pipeline);

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
    m_vk.vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(compositionVk.targetImage->imageCreateInfo.extent.width),
        .height = static_cast<float>(compositionVk.targetImage->imageCreateInfo.extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    m_vk.vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    const VkDeviceSize offsets[] = {0};
    m_vk.vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexVkBuffer, offsets);

    m_vk.vkCmdBindIndexBuffer(commandBuffer, m_indexVkBuffer, 0, VK_INDEX_TYPE_UINT16);

    const uint32_t numLayers = compositionVk.layersDescriptorSets.descriptorSets.size();
    for (uint32_t layerIndex = 0; layerIndex < numLayers; ++layerIndex) {
        m_debugUtilsHelper.cmdBeginDebugLabel(commandBuffer, "CompositorVk compose layer:%d",
                                              layerIndex);

        VkDescriptorSet layerDescriptorSet = frameResources->m_layerDescriptorSets[layerIndex];

        m_vk.vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     m_vkPipelineLayout,
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
        android::base::AutoLock lock(*m_vkQueueLock);
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
            if (res == VK_SUCCESS) {
                return frameResources;
            }
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

void CompositorVk::onImageDestroyed(uint32_t imageId) { m_renderTargetCache.remove(imageId); }

bool operator==(const CompositorVkBase::DescriptorSetContents& lhs,
                const CompositorVkBase::DescriptorSetContents& rhs) {
    return std::tie(lhs.binding0.sampledImageId,     //
                    lhs.binding0.sampledImageView,   //
                    lhs.binding1.mode,               //
                    lhs.binding1.alpha,              //
                    lhs.binding1.color,              //
                    lhs.binding1.positionTransform,  //
                    lhs.binding1.texCoordTransform)  //
                     ==                              //
           std::tie(rhs.binding0.sampledImageId,     //
                    rhs.binding0.sampledImageView,   //
                    rhs.binding1.mode,               //
                    rhs.binding1.alpha,              //
                    rhs.binding1.color,              //
                    rhs.binding1.positionTransform,  //
                    rhs.binding1.texCoordTransform);
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
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "CompositorVk can't compose more than " << kMaxLayersPerFrame
            << " layers. layers asked: " << numRequestedLayers;
        return;
    }

    std::vector<VkDescriptorImageInfo> descriptorImageInfos(numRequestedLayers);
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    for (uint32_t layerIndex = 0; layerIndex < numRequestedLayers; ++layerIndex) {
        const DescriptorSetContents& layerDescriptorSetContents =
            descriptorSetsContents.descriptorSets[layerIndex];

        descriptorImageInfos[layerIndex] = VkDescriptorImageInfo{
            // Empty as we only use immutable samplers.
            .sampler = VK_NULL_HANDLE,
            .imageView = layerDescriptorSetContents.binding0.sampledImageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        descriptorWrites.emplace_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frameResources->m_layerDescriptorSets[layerIndex],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &descriptorImageInfos[layerIndex],
        });

        UniformBufferBinding* layerUboStorage = frameResources->m_layerUboStorages[layerIndex];
        *layerUboStorage = layerDescriptorSetContents.binding1;
    }

    m_vk.vkUpdateDescriptorSets(m_vkDevice, descriptorWrites.size(), descriptorWrites.data(), 0,
                                nullptr);

    frameResources->m_vkDescriptorSetsContents = descriptorSetsContents;
}

}  // namespace vk
}  // namespace gfxstream
