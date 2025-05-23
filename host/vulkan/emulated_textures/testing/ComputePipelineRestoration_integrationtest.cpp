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

#include <cstdint>
#include <vector>

#include "stream-servers/vulkan/testing/VulkanTestHelper.h"

namespace goldfish_vk::testing {
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

class ComputePipelineRestorationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        vkTest = std::make_unique<VulkanTestHelper>();
        vkTest->failOnValidationErrors(false);
        vkTest->initialize(
            {.astcLdrEmulationMode = AstcEmulationMode::Gpu,
             .appInfo = VkApplicationInfo{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
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
        vk->vkDestroyImage(device, image, nullptr);
        vk->vkFreeMemory(device, imageMemory, nullptr);

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
    VkDescriptorSetLayout unboxedDSLayout = unbox_VkDescriptorSetLayout(descriptorSetLayout);

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
                  .module = unbox_VkShaderModule(shader),
                  .pName = "main"},
        .layout = unbox_VkPipelineLayout(pipelineLayout),
    };
    VkPipeline pipeline;
    VK_CHECK(
        vk->vkCreateComputePipelines(device, nullptr, 1, &computePipelineInfo, nullptr, &pipeline));
    vk->vkDestroyShaderModule(device, shader, nullptr);

    // Create a command buffer
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = unbox_VkCommandPool(vkTest->commandPool()),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmdBuf;
    vk->vkAllocateCommandBuffers(device, &allocInfo, &cmdBuf);
    auto unboxedCmdBuf = unbox_VkCommandBuffer(cmdBuf);

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

}  // namespace
}  // namespace goldfish_vk::testing