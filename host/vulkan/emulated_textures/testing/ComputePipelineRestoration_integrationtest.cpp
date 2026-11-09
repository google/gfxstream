// copyright (c) 2023 the android open source project
//
// licensed under the apache license, version 2.0 (the "license");
// you may not use this file except in compliance with the license.
// you may obtain a copy of the license at
//
// http://www.apache.org/licenses/license-2.0
//
// unless required by applicable law or agreed to in writing, software
// distributed under the license is distributed on an "as is" basis,
// without warranties or conditions of any kind, either express or implied.
// see the license for the specific language governing permissions and
// limitations under the license.

// This is an integration test that verifies that the compute pipeline is correctly restored after
// we perform texture decompression.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "vulkan/testing/VulkanTestHelper.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace testing {
namespace {

// Compiled code for the following shader:
//
//     #version 450
//     void main() {}
inline constexpr uint32_t emptyShader[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x00000006, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0005000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x00060010, 0x00000004, 0x00000011,
    0x00000001, 0x00000001, 0x00000001, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47,
    0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c, 0x7269645f, 0x69746365, 0x00006576,
    0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669,
    0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x000100fd, 0x00010038};

// Compiled code for the following compute shader:
//
//     #version 450
//     layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
//     layout(push_constant) uniform PushConstants {
//         uvec4 values;
//     } pc;
//     layout(set = 0, binding = 0) buffer OutputBuffer {
//         uvec4 outValues;
//     } outBuf;
//     void main() {
//         outBuf.outValues = pc.values;
//     }
inline constexpr uint32_t pushConstantReadbackShader[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000018, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0005000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x00060010, 0x00000004, 0x00000011,
    0x00000001, 0x00000001, 0x00000001, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47,
    0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c, 0x7269645f, 0x69746365, 0x00006576,
    0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669,
    0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00060005, 0x00000008, 0x7074754f, 0x75427475,
    0x72656666, 0x00000000, 0x00060006, 0x00000008, 0x00000000, 0x5674756f, 0x65756c61, 0x00000073,
    0x00040005, 0x0000000a, 0x4274756f, 0x00006675, 0x00060005, 0x0000000d, 0x68737550, 0x736e6f43,
    0x746e6174, 0x00000073, 0x00050006, 0x0000000d, 0x00000000, 0x756c6176, 0x00007365, 0x00030005,
    0x0000000f, 0x00006370, 0x00030047, 0x00000008, 0x00000003, 0x00050048, 0x00000008, 0x00000000,
    0x00000023, 0x00000000, 0x00040047, 0x0000000a, 0x00000021, 0x00000000, 0x00040047, 0x0000000a,
    0x00000022, 0x00000000, 0x00030047, 0x0000000d, 0x00000002, 0x00050048, 0x0000000d, 0x00000000,
    0x00000023, 0x00000000, 0x00040047, 0x00000017, 0x0000000b, 0x00000019, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020, 0x00000000, 0x00040017,
    0x00000007, 0x00000006, 0x00000004, 0x0003001e, 0x00000008, 0x00000007, 0x00040020, 0x00000009,
    0x00000002, 0x00000008, 0x0004003b, 0x00000009, 0x0000000a, 0x00000002, 0x00040015, 0x0000000b,
    0x00000020, 0x00000001, 0x0004002b, 0x0000000b, 0x0000000c, 0x00000000, 0x0003001e, 0x0000000d,
    0x00000007, 0x00040020, 0x0000000e, 0x00000009, 0x0000000d, 0x0004003b, 0x0000000e, 0x0000000f,
    0x00000009, 0x00040020, 0x00000010, 0x00000009, 0x00000007, 0x00040020, 0x00000013, 0x00000002,
    0x00000007, 0x00040017, 0x00000015, 0x00000006, 0x00000003, 0x0004002b, 0x00000006, 0x00000016,
    0x00000001, 0x0006002c, 0x00000015, 0x00000017, 0x00000016, 0x00000016, 0x00000016, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x00000010,
    0x00000011, 0x0000000f, 0x0000000c, 0x0004003d, 0x00000007, 0x00000012, 0x00000011, 0x00050041,
    0x00000013, 0x00000014, 0x0000000a, 0x0000000c, 0x0003003e, 0x00000014, 0x00000012, 0x000100fd,
    0x00010038};

class ComputePipelineRestorationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        vkTest = std::make_unique<VulkanTestHelper>();
        vkTest->failOnValidationErrors(false);
        vkTest->initialize(
            {.appInfo = VkApplicationInfo{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                          .pApplicationName = "pipeline_restoration_test",
                                          .pEngineName = "pipeline_restoration_test",
                                          .apiVersion = VK_API_VERSION_1_1},
             .deviceFeatures = {
                 .textureCompressionASTC_LDR = true,
             }});

        vk = &vkTest->vk();
        device = vkTest->device();
    }

    // Tears the test down, and make sure there are no validation errors
    void TearDown() override {
        if (image != VK_NULL_HANDLE) {
            vk->vkDestroyImage(device, image, nullptr);
        }
        if (imageMemory != VK_NULL_HANDLE) {
            vk->vkFreeMemory(device, imageMemory, nullptr);
        }

        vkTest->destroy();
        ASSERT_FALSE(vkTest->hasValidationErrors());
        vkTest.reset();
        vk = nullptr;
        device = VK_NULL_HANDLE;
    }

    // Creates an arbitrary 128x128 ASTC image
    void createImage() {
        VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
            .extent = {128, 128, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VK_CHECK(vk->vkCreateImage(device, &imageInfo, nullptr, &image));

        VkMemoryRequirements memRequirements;
        vk->vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = vkTest->findMemoryType(memRequirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        VK_CHECK(vk->vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory));
        VK_CHECK(vk->vkBindImageMemory(device, image, imageMemory, 0));
    }

    // Tries to decompress an image, which will trigger the pipeline restoration mechanism
    void decompressImage(VkCommandBuffer cmdBuf) {
        if (image == VK_NULL_HANDLE) {
            createImage();
        }

        // Transition the image to TRANSFER_DST, pretend we write some data to it
        vkTest->transitionImageLayout(cmdBuf, image, VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // Then transition to SHADER_READ, as if we were going to read from it. This will trigger
        // the decompression code
        vkTest->transitionImageLayout(cmdBuf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    std::unique_ptr<VulkanTestHelper> vkTest;
    VkDecoderTestDispatch* vk = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
};

TEST_F(ComputePipelineRestorationTest, ShouldNotRestoreDestroyedPipeline) {
    // Create the shader module
    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(emptyShader),
        .pCode = emptyShader,
    };
    VkShaderModule shader;
    VK_CHECK(vk->vkCreateShaderModule(device, &shaderInfo, nullptr, &shader));

    // Create the descriptor set layout. The shader has no bindings.
    VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    };
    VkDescriptorSetLayout descriptorSetLayout;
    VK_CHECK(vk->vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &descriptorSetLayout));
    VkDescriptorSetLayout unboxedDSLayout =
        unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(descriptorSetLayout));

    // Create the pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &unboxedDSLayout,
    };
    VkPipelineLayout pipelineLayout;
    VK_CHECK(vk->vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    // Create the compute pipeline
    VkComputePipelineCreateInfo computePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = unbox_VkShaderModule(Boxed<VkShaderModule>(shader)),
                  .pName = "main"},
        .layout = unbox_VkPipelineLayout(Boxed<VkPipelineLayout>(pipelineLayout)),
    };
    VkPipeline pipeline;
    VK_CHECK(
        vk->vkCreateComputePipelines(device, nullptr, 1, &computePipelineInfo, nullptr, &pipeline));
    vk->vkDestroyShaderModule(device, shader, nullptr);

    // Create a command buffer
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = unbox_VkCommandPool(Boxed<VkCommandPool>(vkTest->commandPool())),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmdBuf;
    vk->vkAllocateCommandBuffers(device, &allocInfo, &cmdBuf);
    auto unboxedCmdBuf = unbox_VkCommandBuffer(Boxed<VkCommandBuffer>(cmdBuf));

    // Begin command buffer and bind the pipeline to it
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk->vkBeginCommandBuffer(cmdBuf, &beginInfo);
    vk->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk->vkEndCommandBuffer(cmdBuf);

    // Submit the command buffer
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &unboxedCmdBuf,
    };
    vk->vkQueueSubmit(vkTest->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vk->vkQueueWaitIdle(vkTest->graphicsQueue());

    // Now destroy the pipeline. This is legal since the command buffer is fully submitted.
    vk->vkDestroyPipeline(device, pipeline, nullptr);
    vk->vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    // Now begin the command buffer again
    vk->vkBeginCommandBuffer(cmdBuf, &beginInfo);

    // If we do image decompression now, it should not try to restore the now-destroyed pipeline
    decompressImage(cmdBuf);

    // Submit the command buffer and destroy everything
    vk->vkEndCommandBuffer(cmdBuf);
    vk->vkQueueSubmit(vkTest->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vk->vkQueueWaitIdle(vkTest->graphicsQueue());
    vk->vkFreeCommandBuffers(device, vkTest->commandPool(), 1, &unboxedCmdBuf);
    vk->vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
}

TEST_F(ComputePipelineRestorationTest, PreservesPushConstantsAndComputeDescriptorsAcrossDecompression) {
    // Output buffer for reading back push constants from the compute shader.
    VkBuffer outBuffer = VK_NULL_HANDLE;
    VkDeviceMemory outBufferMemory = VK_NULL_HANDLE;
    vkTest->createBuffer(sizeof(uint32_t) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         outBuffer, outBufferMemory);

    // Dummy buffer bound to a graphics descriptor set to verify graphics descriptor binds do not
    // overwrite saved compute descriptor state.
    VkBuffer dummyGraphicsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dummyGraphicsMemory = VK_NULL_HANDLE;
    vkTest->createBuffer(sizeof(uint32_t) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         dummyGraphicsBuffer, dummyGraphicsMemory);

    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(pushConstantReadbackShader),
        .pCode = pushConstantReadbackShader,
    };
    VkShaderModule shader = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreateShaderModule(device, &shaderInfo, nullptr, &shader));

    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &dsLayout));
    VkDescriptorSetLayout unboxedDsLayout =
        unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(dsLayout));

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                      VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t) * 4,
    };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &unboxedDsLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    VkComputePipelineCreateInfo computePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = unbox_VkShaderModule(Boxed<VkShaderModule>(shader)),
                  .pName = "main"},
        .layout = unbox_VkPipelineLayout(Boxed<VkPipelineLayout>(pipelineLayout)),
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(
        vk->vkCreateComputePipelines(device, nullptr, 1, &computePipelineInfo, nullptr, &pipeline));
    vk->vkDestroyShaderModule(device, shader, nullptr);

    VkDescriptorPoolSize poolSize = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 2,
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    VkDescriptorPool dsPool = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreateDescriptorPool(device, &poolInfo, nullptr, &dsPool));

    VkDescriptorSetLayout unboxedLayouts[2] = {unboxedDsLayout, unboxedDsLayout};
    VkDescriptorSetAllocateInfo dsAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = unbox_VkDescriptorPool(Boxed<VkDescriptorPool>(dsPool)),
        .descriptorSetCount = 2,
        .pSetLayouts = unboxedLayouts,
    };
    VkDescriptorSet descriptorSets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VK_CHECK(vk->vkAllocateDescriptorSets(device, &dsAllocInfo, descriptorSets));

    VkDescriptorBufferInfo computeBufInfo = {
        .buffer = unbox_VkBuffer(Boxed<VkBuffer>(outBuffer)),
        .offset = 0,
        .range = sizeof(uint32_t) * 4,
    };
    VkDescriptorBufferInfo graphicsBufInfo = {
        .buffer = unbox_VkBuffer(Boxed<VkBuffer>(dummyGraphicsBuffer)),
        .offset = 0,
        .range = sizeof(uint32_t) * 4,
    };
    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = unbox_VkDescriptorSet(Boxed<VkDescriptorSet>(descriptorSets[0])),
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &computeBufInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = unbox_VkDescriptorSet(Boxed<VkDescriptorSet>(descriptorSets[1])),
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &graphicsBufInfo,
        },
    };
    vk->vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    createImage();

    const std::array<uint32_t, 4> expectedPushConstants = {
        0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u};

    VkCommandBuffer cmdBuf = vkTest->beginCommandBuffer();
    vk->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                                &descriptorSets[0], 0, nullptr);
    // Bind a graphics descriptor set afterwards; this must not overwrite compute restoration state.
    vk->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &descriptorSets[1], 0, nullptr);
    vk->vkCmdPushConstants(cmdBuf, pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                               VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(expectedPushConstants), expectedPushConstants.data());

    // Trigger GPU ASTC decompression via vkCmdPipelineBarrier. Neither push constants nor the
    // bound compute pipeline/descriptor set should be clobbered.
    decompressImage(cmdBuf);

    // Dispatch without re-binding compute state or re-uploading push constants.
    vk->vkCmdDispatch(cmdBuf, 1, 1, 1);
    vkTest->submitCommandBuffer(cmdBuf);

    void* mapped = nullptr;
    VK_CHECK(vk->vkMapMemory(device, outBufferMemory, 0, sizeof(expectedPushConstants), 0, &mapped));
    std::array<uint32_t, 4> actual = {};
    std::memcpy(actual.data(), mapped, sizeof(actual));
    vk->vkUnmapMemory(device, outBufferMemory);

    EXPECT_EQ(actual, expectedPushConstants);

    vk->vkDestroyDescriptorPool(device, dsPool, nullptr);
    vk->vkDestroyPipeline(device, pipeline, nullptr);
    vk->vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vk->vkDestroyDescriptorSetLayout(device, dsLayout, nullptr);
    vk->vkDestroyBuffer(device, dummyGraphicsBuffer, nullptr);
    vk->vkFreeMemory(device, dummyGraphicsMemory, nullptr);
    vk->vkDestroyBuffer(device, outBuffer, nullptr);
    vk->vkFreeMemory(device, outBufferMemory, nullptr);
}

TEST_F(ComputePipelineRestorationTest, DecompressesAndPreservesPushConstantsOnVkCmdWaitEvents) {
    VkBuffer outBuffer = VK_NULL_HANDLE;
    VkDeviceMemory outBufferMemory = VK_NULL_HANDLE;
    vkTest->createBuffer(sizeof(uint32_t) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         outBuffer, outBufferMemory);

    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(pushConstantReadbackShader),
        .pCode = pushConstantReadbackShader,
    };
    VkShaderModule shader = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreateShaderModule(device, &shaderInfo, nullptr, &shader));

    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &dsLayout));
    VkDescriptorSetLayout unboxedDsLayout =
        unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout>(dsLayout));

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t) * 4,
    };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &unboxedDsLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    VkComputePipelineCreateInfo computePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = unbox_VkShaderModule(Boxed<VkShaderModule>(shader)),
                  .pName = "main"},
        .layout = unbox_VkPipelineLayout(Boxed<VkPipelineLayout>(pipelineLayout)),
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(
        vk->vkCreateComputePipelines(device, nullptr, 1, &computePipelineInfo, nullptr, &pipeline));
    vk->vkDestroyShaderModule(device, shader, nullptr);

    VkDescriptorPoolSize poolSize = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    VkDescriptorPool dsPool = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreateDescriptorPool(device, &poolInfo, nullptr, &dsPool));

    VkDescriptorSetAllocateInfo dsAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = unbox_VkDescriptorPool(Boxed<VkDescriptorPool>(dsPool)),
        .descriptorSetCount = 1,
        .pSetLayouts = &unboxedDsLayout,
    };
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VK_CHECK(vk->vkAllocateDescriptorSets(device, &dsAllocInfo, &descriptorSet));

    VkDescriptorBufferInfo computeBufInfo = {
        .buffer = unbox_VkBuffer(Boxed<VkBuffer>(outBuffer)),
        .offset = 0,
        .range = sizeof(uint32_t) * 4,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = unbox_VkDescriptorSet(Boxed<VkDescriptorSet>(descriptorSet)),
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &computeBufInfo,
    };
    vk->vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkEventCreateInfo eventInfo = {
        .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
    };
    VkEvent event = VK_NULL_HANDLE;
    VK_CHECK(vk->vkCreateEvent(device, &eventInfo, nullptr, &event));

    createImage();

    const std::array<uint32_t, 4> expectedPushConstants = {
        0xCAFEBABEu, 0xDEADBEEFu, 0x01020304u, 0x89ABCDEFu};

    VkCommandBuffer cmdBuf = vkTest->beginCommandBuffer();
    vk->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                                &descriptorSet, 0, nullptr);
    vk->vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(expectedPushConstants), expectedPushConstants.data());

    vkTest->transitionImageLayout(cmdBuf, image, VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk->vkCmdSetEvent(cmdBuf, event, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageMemoryBarrier waitBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = unbox_VkImage(Boxed<VkImage>(image)),
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    vk->vkCmdWaitEvents(cmdBuf, 1, &event, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, nullptr, 0, nullptr, 1,
                        &waitBarrier);

    vk->vkCmdDispatch(cmdBuf, 1, 1, 1);
    vkTest->submitCommandBuffer(cmdBuf);

    void* mapped = nullptr;
    VK_CHECK(vk->vkMapMemory(device, outBufferMemory, 0, sizeof(expectedPushConstants), 0, &mapped));
    std::array<uint32_t, 4> actual = {};
    std::memcpy(actual.data(), mapped, sizeof(actual));
    vk->vkUnmapMemory(device, outBufferMemory);

    EXPECT_EQ(actual, expectedPushConstants);

    vk->vkDestroyEvent(device, event, nullptr);
    vk->vkDestroyDescriptorPool(device, dsPool, nullptr);
    vk->vkDestroyPipeline(device, pipeline, nullptr);
    vk->vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vk->vkDestroyDescriptorSetLayout(device, dsLayout, nullptr);
    vk->vkDestroyBuffer(device, outBuffer, nullptr);
    vk->vkFreeMemory(device, outBufferMemory, nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace vk
}  // namespace host
}  // namespace gfxstream