/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "GraphicsDetectorVkPrecisionQualifiersOnYuvSamplers.h"

#include <vector>

#include "Image.h"
#include "Vulkan.h"

namespace gfxstream {
namespace {

// kBlitTextureVert
#include "shaders/blit_texture.vert.inl"
// kBlitTextureFrag
#include "shaders/blit_texture.frag.inl"
// kBlitTextureLowpFrag
#include "shaders/blit_texture_lowp.frag.inl"
// kBlitTextureMediumpFrag
#include "shaders/blit_texture_mediump.frag.inl"
// kBlitTextureHighpFrag
#include "shaders/blit_texture_highp.frag.inl"

gfxstream::expected<bool, vkhpp::Result>
CanHandlePrecisionQualifierWithYuvSampler(
        const std::vector<uint8_t>& blitVertShaderSpirv,
        const std::vector<uint8_t>& blitFragShaderSpirv) {
    auto vk = VK_EXPECT(Vk::Load(
        /*instance_extensions=*/{},
        /*instance_layers=*/{},
        /*device_extensions=*/
        {
            VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
        }));

    uint32_t textureWidth = 32;
    uint32_t textureHeight = 32;
    RGBAImage textureDataRgba = FillWithColor(textureWidth,
                                              textureHeight,
                                              /*red=*/0xFF,
                                              /*green=*/0x00,
                                              /*blue=*/0x00,
                                              /*alpha=*/0xFF);

    YUV420Image textureDataYuv = ConvertRGBA8888ToYUV420(textureDataRgba);
    #if 0
        // Debugging can be easier with a larger image with more details.
        textureDataYuv = GFXSTREAM_EXPECT(LoadYUV420FromBitmapFile("custom.bmp"));
    #endif

    Vk::YuvImageWithMemory sampledImage = VK_EXPECT(vk.CreateYuvImage(
        textureWidth,
        textureHeight,
        vkhpp::ImageUsageFlagBits::eSampled |
            vkhpp::ImageUsageFlagBits::eTransferDst |
            vkhpp::ImageUsageFlagBits::eTransferSrc,
        vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
        vkhpp::ImageLayout::eTransferDstOptimal));

    VK_EXPECT_RESULT(vk.LoadYuvImage(sampledImage.image,
                                     textureWidth,
                                     textureHeight,
                                     textureDataYuv.y,
                                     textureDataYuv.u,
                                     textureDataYuv.v,
                                     /*currentLayout=*/vkhpp::ImageLayout::eTransferDstOptimal,
                                     /*returnedLayout=*/vkhpp::ImageLayout::eShaderReadOnlyOptimal));

    Vk::FramebufferWithAttachments framebuffer =
        VK_EXPECT(vk.CreateFramebuffer(textureWidth,
                                       textureHeight,
                                       /*colorAttachmentFormat=*/vkhpp::Format::eR8G8B8A8Unorm));

    const vkhpp::Sampler descriptorSet0Binding0Sampler = *sampledImage.imageSampler;
    const std::vector<vkhpp::DescriptorSetLayoutBinding> descriptorSet0Bindings =
        {
            vkhpp::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vkhpp::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vkhpp::ShaderStageFlagBits::eFragment,
                .pImmutableSamplers = &descriptorSet0Binding0Sampler,
            },
        };
    const vkhpp::DescriptorSetLayoutCreateInfo descriptorSet0CreateInfo = {
        .bindingCount = static_cast<uint32_t>(descriptorSet0Bindings.size()),
        .pBindings = descriptorSet0Bindings.data(),
    };
    auto descriptorSet0Layout = VK_EXPECT_RV(vk.device().createDescriptorSetLayoutUnique(descriptorSet0CreateInfo));

    const std::vector<vkhpp::DescriptorPoolSize> descriptorPoolSizes = {
        vkhpp::DescriptorPoolSize{
            .type = vkhpp::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
        },
    };
    const vkhpp::DescriptorPoolCreateInfo descriptorPoolCreateInfo = {
        .flags = vkhpp::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
        .pPoolSizes = descriptorPoolSizes.data(),
    };
    auto descriptorSet0Pool = VK_EXPECT_RV(vk.device().createDescriptorPoolUnique(descriptorPoolCreateInfo));

    const vkhpp::DescriptorSetLayout descriptorSet0LayoutHandle = *descriptorSet0Layout;
    const vkhpp::DescriptorSetAllocateInfo descriptorSet0AllocateInfo = {
        .descriptorPool = *descriptorSet0Pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSet0LayoutHandle,
    };
    auto descriptorSets = VK_EXPECT_RV(vk.device().allocateDescriptorSetsUnique(descriptorSet0AllocateInfo));
    auto descriptorSet0(std::move(descriptorSets[0]));

    const vkhpp::DescriptorImageInfo descriptorSet0Binding0ImageInfo = {
        .sampler = VK_NULL_HANDLE,
        .imageView = *sampledImage.imageView,
        .imageLayout = vkhpp::ImageLayout::eShaderReadOnlyOptimal,
    };
    const std::vector<vkhpp::WriteDescriptorSet> descriptorSet0Writes = {
        vkhpp::WriteDescriptorSet{
            .dstSet = *descriptorSet0,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vkhpp::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &descriptorSet0Binding0ImageInfo,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        },
    };
    vk.device().updateDescriptorSets(descriptorSet0Writes, {});

    const std::vector<vkhpp::DescriptorSetLayout> pipelineLayoutDescriptorSetLayouts = {
        *descriptorSet0Layout,
    };
    const vkhpp::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
        .setLayoutCount = static_cast<uint32_t>(pipelineLayoutDescriptorSetLayouts.size()),
        .pSetLayouts = pipelineLayoutDescriptorSetLayouts.data(),
    };
    auto pipelineLayout = VK_EXPECT_RV(vk.device().createPipelineLayoutUnique(pipelineLayoutCreateInfo));

    const vkhpp::ShaderModuleCreateInfo vertShaderCreateInfo = {
        .codeSize = static_cast<uint32_t>(blitVertShaderSpirv.size()),
        .pCode = reinterpret_cast<const uint32_t*>(blitVertShaderSpirv.data()),
    };
    auto vertShaderModule = VK_EXPECT_RV(vk.device().createShaderModuleUnique(vertShaderCreateInfo));

    const vkhpp::ShaderModuleCreateInfo fragShaderCreateInfo = {
        .codeSize = static_cast<uint32_t>(blitFragShaderSpirv.size()),
        .pCode = reinterpret_cast<const uint32_t*>(blitFragShaderSpirv.data()),
    };
    auto fragShaderModule = VK_EXPECT_RV(vk.device().createShaderModuleUnique(fragShaderCreateInfo));

    const std::vector<vkhpp::PipelineShaderStageCreateInfo> pipelineStages = {
        vkhpp::PipelineShaderStageCreateInfo{
            .stage = vkhpp::ShaderStageFlagBits::eVertex,
            .module = *vertShaderModule,
            .pName = "main",
        },
        vkhpp::PipelineShaderStageCreateInfo{
            .stage = vkhpp::ShaderStageFlagBits::eFragment,
            .module = *fragShaderModule,
            .pName = "main",
        },
    };
    const vkhpp::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo = {};
    const vkhpp::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo = {
        .topology = vkhpp::PrimitiveTopology::eTriangleStrip,
    };
    const vkhpp::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo = {
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };
    const vkhpp::PipelineRasterizationStateCreateInfo pipelineRasterStateCreateInfo = {
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = vkhpp::PolygonMode::eFill,
        .cullMode = {},
        .frontFace = vkhpp::FrontFace::eCounterClockwise,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };
    const vkhpp::SampleMask pipelineSampleMask = 65535;
    const vkhpp::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo = {
        .rasterizationSamples = vkhpp::SampleCountFlagBits::e1,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = &pipelineSampleMask,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    const vkhpp::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo = {
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = vkhpp::CompareOp::eLess,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {
            .failOp = vkhpp::StencilOp::eKeep,
            .passOp = vkhpp::StencilOp::eKeep,
            .depthFailOp = vkhpp::StencilOp::eKeep,
            .compareOp = vkhpp::CompareOp::eAlways,
            .compareMask = 0,
            .writeMask = 0,
            .reference = 0,
        },
        .back = {
            .failOp = vkhpp::StencilOp::eKeep,
            .passOp = vkhpp::StencilOp::eKeep,
            .depthFailOp = vkhpp::StencilOp::eKeep,
            .compareOp = vkhpp::CompareOp::eAlways,
            .compareMask = 0,
            .writeMask = 0,
            .reference = 0,
        },
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 0.0f,
    };
    const std::vector<vkhpp::PipelineColorBlendAttachmentState> pipelineColorBlendAttachments = {
        vkhpp::PipelineColorBlendAttachmentState{
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = vkhpp::BlendFactor::eOne,
            .dstColorBlendFactor = vkhpp::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp = vkhpp::BlendOp::eAdd,
            .srcAlphaBlendFactor = vkhpp::BlendFactor::eOne,
            .dstAlphaBlendFactor = vkhpp::BlendFactor::eOneMinusSrcAlpha,
            .alphaBlendOp = vkhpp::BlendOp::eAdd,
            .colorWriteMask = vkhpp::ColorComponentFlagBits::eR |
                              vkhpp::ColorComponentFlagBits::eG |
                              vkhpp::ColorComponentFlagBits::eB |
                              vkhpp::ColorComponentFlagBits::eA,
        },
    };
    const vkhpp::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo = {
        .logicOpEnable = VK_FALSE,
        .logicOp = vkhpp::LogicOp::eCopy,
        .attachmentCount = static_cast<uint32_t>(pipelineColorBlendAttachments.size()),
        .pAttachments = pipelineColorBlendAttachments.data(),
        .blendConstants = {{
            0.0f,
            0.0f,
            0.0f,
            0.0f,
        }},
    };
    const std::vector<vkhpp::DynamicState> pipelineDynamicStates = {
        vkhpp::DynamicState::eViewport,
        vkhpp::DynamicState::eScissor,
    };
    const vkhpp::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {
        .dynamicStateCount = static_cast<uint32_t>(pipelineDynamicStates.size()),
        .pDynamicStates = pipelineDynamicStates.data(),
    };
    const vkhpp::GraphicsPipelineCreateInfo pipelineCreateInfo = {
        .stageCount = static_cast<uint32_t>(pipelineStages.size()),
        .pStages = pipelineStages.data(),
        .pVertexInputState = &pipelineVertexInputStateCreateInfo,
        .pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo,
        .pTessellationState = nullptr,
        .pViewportState = &pipelineViewportStateCreateInfo,
        .pRasterizationState = &pipelineRasterStateCreateInfo,
        .pMultisampleState = &pipelineMultisampleStateCreateInfo,
        .pDepthStencilState = &pipelineDepthStencilStateCreateInfo,
        .pColorBlendState = &pipelineColorBlendStateCreateInfo,
        .pDynamicState = &pipelineDynamicStateCreateInfo,
        .layout = *pipelineLayout,
        .renderPass = *framebuffer.renderpass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };
    auto pipeline = VK_EXPECT_RV(vk.device().createGraphicsPipelineUnique({}, pipelineCreateInfo));

    VK_EXPECT_RESULT(vk.DoCommandsImmediate(
        [&](vkhpp::UniqueCommandBuffer& cmd) {
            const std::vector<vkhpp::ClearValue> renderPassBeginClearValues = {
                vkhpp::ClearValue{
                    .color = {
                        .float32 = {{
                            1.0f,
                            0.0f,
                            0.0f,
                            1.0f,
                        }},
                    },
                },
            };
            const vkhpp::RenderPassBeginInfo renderPassBeginInfo = {
                .renderPass = *framebuffer.renderpass,
                .framebuffer = *framebuffer.framebuffer,
                .renderArea = {
                    .offset = {
                        .x = 0,
                        .y = 0,
                    },
                    .extent = {
                        .width = textureWidth,
                        .height = textureHeight,
                    },
                },
                .clearValueCount = static_cast<uint32_t>(renderPassBeginClearValues.size()),
                .pClearValues = renderPassBeginClearValues.data(),
            };
            cmd->beginRenderPass(renderPassBeginInfo, vkhpp::SubpassContents::eInline);

            cmd->bindPipeline(vkhpp::PipelineBindPoint::eGraphics, *pipeline);

            cmd->bindDescriptorSets(vkhpp::PipelineBindPoint::eGraphics,
                                    *pipelineLayout,
                                    /*firstSet=*/0, {*descriptorSet0},
                                    /*dynamicOffsets=*/{});

            const vkhpp::Viewport viewport = {
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(textureWidth),
                .height = static_cast<float>(textureHeight),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            };
            cmd->setViewport(0, {viewport});

            const vkhpp::Rect2D scissor = {
                .offset = {
                    .x = 0,
                    .y = 0,
                },
                .extent = {
                    .width = textureWidth,
                    .height = textureHeight,
                },
            };
            cmd->setScissor(0, {scissor});

            cmd->draw(4, 1, 0, 0);

            cmd->endRenderPass();
            return vkhpp::Result::eSuccess;
        }));

    const std::vector<uint8_t> renderedPixels = VK_EXPECT(vk.DownloadImage(
        textureWidth,
        textureHeight,
        framebuffer.colorAttachment->image,
        vkhpp::ImageLayout::eColorAttachmentOptimal,
        vkhpp::ImageLayout::eColorAttachmentOptimal));

    #if 0
        SaveRGBAToBitmapFile(textureWidth,
                             textureHeight,
                             renderedPixels.data(),
                             "rendered.bmp");
    #endif

    const RGBAImage actual = {
        .width = textureWidth,
        .height = textureHeight,
        .pixels = std::move(renderedPixels),
    };

    auto result = CompareImages(textureDataRgba, actual);
    return result.ok();
}

}  // namespace

gfxstream::expected<Ok, std::string>
PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirk(
        ::gfxstream::proto::GraphicsAvailability* availability) {
    struct ShaderCombo {
        std::string name;
        const std::vector<uint8_t>& vert;
        const std::vector<uint8_t>& frag;
    };
    const std::vector<ShaderCombo> combos = {
        ShaderCombo{
            .name = "sampler2D has no precision qualifier",
            .vert = kBlitTextureVert,
            .frag = kBlitTextureFrag,
        },
        ShaderCombo{
            .name = "sampler2D has a 'lowp' precision qualifier",
            .vert = kBlitTextureVert,
            .frag = kBlitTextureLowpFrag,
        },
        ShaderCombo{
            .name = "sampler2D has a 'mediump' precision qualifier",
            .vert = kBlitTextureVert,
            .frag = kBlitTextureMediumpFrag,
        },
        ShaderCombo{
            .name = "sampler2D has a 'highp' precision qualifier",
            .vert = kBlitTextureVert,
            .frag = kBlitTextureHighpFrag,
        },
    };

    bool anyTestFailed = false;
    for (const auto& combo : combos) {
        auto result = CanHandlePrecisionQualifierWithYuvSampler(combo.vert, combo.frag);
        if (!result.ok()) {
            // Failed to run to completion.
            return gfxstream::unexpected(vkhpp::to_string(result.error()));
        }
        const bool passedTest = result.value();
        if (!passedTest) {
            // Ran to completion but had bad value.
            anyTestFailed = true;
            break;
        }
    }

    // TODO: Run this test per device.
    availability->mutable_vulkan()
                ->mutable_physical_devices(0)
                ->mutable_quirks()
                ->set_has_issue_with_precision_qualifiers_on_yuv_samplers(anyTestFailed);
    return Ok{};
}

}  // namespace gfxstream