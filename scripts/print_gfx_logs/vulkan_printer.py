# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#####################################################################################################
# Pretty-printer functions for Vulkan data structures
# THIS FILE IS AUTO-GENERATED - DO NOT EDIT
#
# To re-generate this file, run generate-vulkan-sources.sh
#####################################################################################################

def OP_vkAcquireImageANDROID(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    nativeFenceFd = printer.write_int("nativeFenceFd", 4, indent, signed=True, big_endian=False)
    semaphore = printer.write_int("semaphore", 8, indent, signed=False, big_endian=False)
    fence = printer.write_int("fence", 8, indent, signed=False, big_endian=False)
    return

def OP_vkAllocateMemory(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocateInfo", struct_VkMemoryAllocateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pMemory = printer.write_int("pMemory", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkBeginCommandBufferAsyncGOOGLE(printer, indent: int):
    printer.write_struct("pBeginInfo", struct_VkCommandBufferBeginInfo, False, None, indent)
    return

def OP_vkBindBufferMemory(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    buffer = printer.write_int("buffer", 8, indent, signed=False, big_endian=False)
    memory = printer.write_int("memory", 8, indent, signed=False, big_endian=False)
    memoryOffset = printer.write_int("memoryOffset", 8, indent, signed=False, big_endian=False)
    return

def OP_vkBindImageMemory(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    memory = printer.write_int("memory", 8, indent, signed=False, big_endian=False)
    memoryOffset = printer.write_int("memoryOffset", 8, indent, signed=False, big_endian=False)
    return

def OP_vkCmdBeginRenderPass(printer, indent: int):
    printer.write_struct("pRenderPassBegin", struct_VkRenderPassBeginInfo, False, None, indent)
    printer.write_enum("contents", VkSubpassContents, indent)
    return

def OP_vkCmdBindDescriptorSets(printer, indent: int):
    printer.write_enum("pipelineBindPoint", VkPipelineBindPoint, indent)
    layout = printer.write_int("layout", 8, indent, signed=False, big_endian=False)
    firstSet = printer.write_int("firstSet", 4, indent, signed=False, big_endian=False)
    descriptorSetCount = printer.write_int("descriptorSetCount", 4, indent, signed=False, big_endian=False)
    pDescriptorSets = printer.write_int("pDescriptorSets", 8, indent, optional=False, count=descriptorSetCount, big_endian=False)
    dynamicOffsetCount = printer.write_int("dynamicOffsetCount", 4, indent, signed=False, big_endian=False)
    pDynamicOffsets = printer.write_int("pDynamicOffsets", 4, indent, optional=False, count=dynamicOffsetCount, big_endian=False)
    return

def OP_vkCmdBindIndexBuffer(printer, indent: int):
    buffer = printer.write_int("buffer", 8, indent, signed=False, big_endian=False)
    offset = printer.write_int("offset", 8, indent, signed=False, big_endian=False)
    printer.write_enum("indexType", VkIndexType, indent)
    return

def OP_vkCmdBindPipeline(printer, indent: int):
    printer.write_enum("pipelineBindPoint", VkPipelineBindPoint, indent)
    pipeline = printer.write_int("pipeline", 8, indent, signed=False, big_endian=False)
    return

def OP_vkCmdBindVertexBuffers(printer, indent: int):
    firstBinding = printer.write_int("firstBinding", 4, indent, signed=False, big_endian=False)
    bindingCount = printer.write_int("bindingCount", 4, indent, signed=False, big_endian=False)
    pBuffers = printer.write_int("pBuffers", 8, indent, optional=False, count=bindingCount, big_endian=False)
    pOffsets = printer.write_int("pOffsets", 8, indent, optional=False, count=bindingCount, big_endian=False)
    return

def OP_vkCmdClearAttachments(printer, indent: int):
    attachmentCount = printer.write_int("attachmentCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pAttachments", struct_VkClearAttachment, False, attachmentCount, indent)
    rectCount = printer.write_int("rectCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pRects", struct_VkClearRect, False, rectCount, indent)
    return

def OP_vkCmdClearColorImage(printer, indent: int):
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    printer.write_enum("imageLayout", VkImageLayout, indent)
    printer.write_struct("pColor", struct_VkClearColorValue, False, None, indent)
    rangeCount = printer.write_int("rangeCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pRanges", struct_VkImageSubresourceRange, False, rangeCount, indent)
    return

def OP_vkCmdCopyBufferToImage(printer, indent: int):
    srcBuffer = printer.write_int("srcBuffer", 8, indent, signed=False, big_endian=False)
    dstImage = printer.write_int("dstImage", 8, indent, signed=False, big_endian=False)
    printer.write_enum("dstImageLayout", VkImageLayout, indent)
    regionCount = printer.write_int("regionCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pRegions", struct_VkBufferImageCopy, False, regionCount, indent)
    return

def OP_vkCmdCopyImageToBuffer(printer, indent: int):
    srcImage = printer.write_int("srcImage", 8, indent, signed=False, big_endian=False)
    printer.write_enum("srcImageLayout", VkImageLayout, indent)
    dstBuffer = printer.write_int("dstBuffer", 8, indent, signed=False, big_endian=False)
    regionCount = printer.write_int("regionCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pRegions", struct_VkBufferImageCopy, False, regionCount, indent)
    return

def OP_vkCmdDraw(printer, indent: int):
    vertexCount = printer.write_int("vertexCount", 4, indent, signed=False, big_endian=False)
    instanceCount = printer.write_int("instanceCount", 4, indent, signed=False, big_endian=False)
    firstVertex = printer.write_int("firstVertex", 4, indent, signed=False, big_endian=False)
    firstInstance = printer.write_int("firstInstance", 4, indent, signed=False, big_endian=False)
    return

def OP_vkCmdDrawIndexed(printer, indent: int):
    indexCount = printer.write_int("indexCount", 4, indent, signed=False, big_endian=False)
    instanceCount = printer.write_int("instanceCount", 4, indent, signed=False, big_endian=False)
    firstIndex = printer.write_int("firstIndex", 4, indent, signed=False, big_endian=False)
    vertexOffset = printer.write_int("vertexOffset", 4, indent, signed=True, big_endian=False)
    firstInstance = printer.write_int("firstInstance", 4, indent, signed=False, big_endian=False)
    return

def OP_vkCmdEndRenderPass(printer, indent: int):
    return

def OP_vkCmdPipelineBarrier(printer, indent: int):
    srcStageMask = printer.write_int("srcStageMask", 4, indent, signed=False, big_endian=False)
    dstStageMask = printer.write_int("dstStageMask", 4, indent, signed=False, big_endian=False)
    dependencyFlags = printer.write_int("dependencyFlags", 4, indent, signed=False, big_endian=False)
    memoryBarrierCount = printer.write_int("memoryBarrierCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pMemoryBarriers", struct_VkMemoryBarrier, False, memoryBarrierCount, indent)
    bufferMemoryBarrierCount = printer.write_int("bufferMemoryBarrierCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pBufferMemoryBarriers", struct_VkBufferMemoryBarrier, False, bufferMemoryBarrierCount, indent)
    imageMemoryBarrierCount = printer.write_int("imageMemoryBarrierCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pImageMemoryBarriers", struct_VkImageMemoryBarrier, False, imageMemoryBarrierCount, indent)
    return

def OP_vkCmdPipelineBarrier2(printer, indent: int):
    printer.write_struct("pDependencyInfo", struct_VkDependencyInfo, False, None, indent)
    return

def OP_vkCmdSetScissor(printer, indent: int):
    firstScissor = printer.write_int("firstScissor", 4, indent, signed=False, big_endian=False)
    scissorCount = printer.write_int("scissorCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pScissors", struct_VkRect2D, False, scissorCount, indent)
    return

def OP_vkCmdSetViewport(printer, indent: int):
    firstViewport = printer.write_int("firstViewport", 4, indent, signed=False, big_endian=False)
    viewportCount = printer.write_int("viewportCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pViewports", struct_VkViewport, False, viewportCount, indent)
    return

def OP_vkCollectDescriptorPoolIdsGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    descriptorPool = printer.write_int("descriptorPool", 8, indent, signed=False, big_endian=False)
    pPoolIdCount = printer.write_int("pPoolIdCount", 4, indent, optional=False, count=None, big_endian=False)
    pPoolIds = printer.write_int("pPoolIds", 8, indent, optional=True, count=pPoolIdCount, big_endian=False)
    return

def OP_vkCreateBufferWithRequirementsGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkBufferCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pBuffer = printer.write_int("pBuffer", 8, indent, optional=False, count=None, big_endian=False)
    printer.write_struct("pMemoryRequirements", struct_VkMemoryRequirements, False, None, indent)
    return

def OP_vkCreateDescriptorPool(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkDescriptorPoolCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pDescriptorPool = printer.write_int("pDescriptorPool", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateDescriptorSetLayout(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkDescriptorSetLayoutCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pSetLayout = printer.write_int("pSetLayout", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateFence(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkFenceCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pFence = printer.write_int("pFence", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateFramebuffer(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkFramebufferCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pFramebuffer = printer.write_int("pFramebuffer", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateGraphicsPipelines(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    pipelineCache = printer.write_int("pipelineCache", 8, indent, signed=False, big_endian=False)
    createInfoCount = printer.write_int("createInfoCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfos", struct_VkGraphicsPipelineCreateInfo, False, createInfoCount, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pPipelines = printer.write_int("pPipelines", 8, indent, optional=False, count=createInfoCount, big_endian=False)
    return

def OP_vkCreateImageView(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkImageViewCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pView = printer.write_int("pView", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateImageWithRequirementsGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkImageCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pImage = printer.write_int("pImage", 8, indent, optional=False, count=None, big_endian=False)
    printer.write_struct("pMemoryRequirements", struct_VkMemoryRequirements, False, None, indent)
    return

def OP_vkCreatePipelineCache(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkPipelineCacheCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pPipelineCache = printer.write_int("pPipelineCache", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreatePipelineLayout(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkPipelineLayoutCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pPipelineLayout = printer.write_int("pPipelineLayout", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateRenderPass(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkRenderPassCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pRenderPass = printer.write_int("pRenderPass", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateSampler(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkSamplerCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pSampler = printer.write_int("pSampler", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateSemaphore(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkSemaphoreCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pSemaphore = printer.write_int("pSemaphore", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkCreateShaderModule(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pCreateInfo", struct_VkShaderModuleCreateInfo, False, None, indent)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    pShaderModule = printer.write_int("pShaderModule", 8, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkDestroyBuffer(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    buffer = printer.write_int("buffer", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyCommandPool(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    commandPool = printer.write_int("commandPool", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyDescriptorPool(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    descriptorPool = printer.write_int("descriptorPool", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyDescriptorSetLayout(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    descriptorSetLayout = printer.write_int("descriptorSetLayout", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyDevice(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyFence(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    fence = printer.write_int("fence", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyFramebuffer(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    framebuffer = printer.write_int("framebuffer", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyImage(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyImageView(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    imageView = printer.write_int("imageView", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyInstance(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    instance = printer.write_int("instance", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyPipeline(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    pipeline = printer.write_int("pipeline", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyPipelineCache(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    pipelineCache = printer.write_int("pipelineCache", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyPipelineLayout(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    pipelineLayout = printer.write_int("pipelineLayout", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyRenderPass(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    renderPass = printer.write_int("renderPass", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroySemaphore(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    semaphore = printer.write_int("semaphore", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkDestroyShaderModule(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    shaderModule = printer.write_int("shaderModule", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkEndCommandBufferAsyncGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    return

def OP_vkFreeCommandBuffers(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    commandPool = printer.write_int("commandPool", 8, indent, signed=False, big_endian=False)
    commandBufferCount = printer.write_int("commandBufferCount", 4, indent, signed=False, big_endian=False)
    pCommandBuffers = printer.write_int("pCommandBuffers", 8, indent, optional=True, count=commandBufferCount, big_endian=False)
    return

def OP_vkFreeMemory(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    memory = printer.write_int("memory", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkFreeMemorySyncGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    memory = printer.write_int("memory", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pAllocator", struct_VkAllocationCallbacks, True, None, indent)
    return

def OP_vkGetBlobGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    memory = printer.write_int("memory", 8, indent, signed=False, big_endian=False)
    return

def OP_vkGetFenceStatus(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    fence = printer.write_int("fence", 8, indent, signed=False, big_endian=False)
    return

def OP_vkGetMemoryHostAddressInfoGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    memory = printer.write_int("memory", 8, indent, signed=False, big_endian=False)
    pAddress = printer.write_int("pAddress", 8, indent, optional=True, count=None, big_endian=False)
    pSize = printer.write_int("pSize", 8, indent, optional=True, count=None, big_endian=False)
    pHostmemId = printer.write_int("pHostmemId", 8, indent, optional=True, count=None, big_endian=False)
    return

def OP_vkGetPhysicalDeviceFormatProperties(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    physicalDevice = printer.write_int("physicalDevice", 8, indent, signed=False, big_endian=False)
    printer.write_enum("format", VkFormat, indent)
    printer.write_struct("pFormatProperties", struct_VkFormatProperties, False, None, indent)
    return

def OP_vkGetPhysicalDeviceProperties2KHR(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    physicalDevice = printer.write_int("physicalDevice", 8, indent, signed=False, big_endian=False)
    printer.write_struct("pProperties", struct_VkPhysicalDeviceProperties2, False, None, indent)
    return

def OP_vkGetPipelineCacheData(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    pipelineCache = printer.write_int("pipelineCache", 8, indent, signed=False, big_endian=False)
    pDataSize = printer.write_int("pDataSize", 8, indent, optional=True, count=None, big_endian=True)
    pData = printer.write_int("pData", 8, indent, optional=True, count=pDataSize, big_endian=False)
    return

def OP_vkGetSemaphoreGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    semaphore = printer.write_int("semaphore", 8, indent, signed=False, big_endian=False)
    syncId = printer.write_int("syncId", 8, indent, signed=False, big_endian=False)
    return

def OP_vkGetSwapchainGrallocUsageANDROID(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    printer.write_enum("format", VkFormat, indent)
    imageUsage = printer.write_int("imageUsage", 4, indent, signed=False, big_endian=False)
    grallocUsage = printer.write_int("grallocUsage", 4, indent, optional=False, count=None, big_endian=False)
    return

def OP_vkQueueCommitDescriptorSetUpdatesGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    queue = printer.write_int("queue", 8, indent, signed=False, big_endian=False)
    descriptorPoolCount = printer.write_int("descriptorPoolCount", 4, indent, signed=False, big_endian=False)
    pDescriptorPools = printer.write_int("pDescriptorPools", 8, indent, optional=False, count=descriptorPoolCount, big_endian=False)
    descriptorSetCount = printer.write_int("descriptorSetCount", 4, indent, signed=False, big_endian=False)
    pSetLayouts = printer.write_int("pSetLayouts", 8, indent, optional=False, count=descriptorSetCount, big_endian=False)
    pDescriptorSetPoolIds = printer.write_int("pDescriptorSetPoolIds", 8, indent, optional=False, count=descriptorSetCount, big_endian=False)
    pDescriptorSetWhichPool = printer.write_int("pDescriptorSetWhichPool", 4, indent, optional=False, count=descriptorSetCount, big_endian=False)
    pDescriptorSetPendingAllocation = printer.write_int("pDescriptorSetPendingAllocation", 4, indent, optional=False, count=descriptorSetCount, big_endian=False)
    pDescriptorWriteStartingIndices = printer.write_int("pDescriptorWriteStartingIndices", 4, indent, optional=False, count=descriptorSetCount, big_endian=False)
    pendingDescriptorWriteCount = printer.write_int("pendingDescriptorWriteCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pPendingDescriptorWrites", struct_VkWriteDescriptorSet, False, pendingDescriptorWriteCount, indent)
    return

def OP_vkQueueFlushCommandsGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    queue = printer.write_int("queue", 8, indent, signed=False, big_endian=False)
    commandBuffer = printer.write_int("commandBuffer", 8, indent, signed=False, big_endian=False)
    dataSize = printer.write_int("dataSize", 8, indent, signed=False, big_endian=False)
    return

def OP_vkQueueSignalReleaseImageANDROIDAsyncGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    queue = printer.write_int("queue", 8, indent, signed=False, big_endian=False)
    waitSemaphoreCount = printer.write_int("waitSemaphoreCount", 4, indent, signed=False, big_endian=False)
    pWaitSemaphores = printer.write_int("pWaitSemaphores", 8, indent, optional=True, count=waitSemaphoreCount, big_endian=False)
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    return

def OP_vkQueueSubmitAsyncGOOGLE(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    queue = printer.write_int("queue", 8, indent, signed=False, big_endian=False)
    submitCount = printer.write_int("submitCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pSubmits", struct_VkSubmitInfo, False, submitCount, indent)
    fence = printer.write_int("fence", 8, indent, signed=False, big_endian=False)
    return

def OP_vkQueueWaitIdle(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    queue = printer.write_int("queue", 8, indent, signed=False, big_endian=False)
    return

def OP_vkResetFences(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    fenceCount = printer.write_int("fenceCount", 4, indent, signed=False, big_endian=False)
    pFences = printer.write_int("pFences", 8, indent, optional=False, count=fenceCount, big_endian=False)
    return

def OP_vkWaitForFences(printer, indent: int):
    printer.write_int("seqno: ", 4, indent)
    device = printer.write_int("device", 8, indent, signed=False, big_endian=False)
    fenceCount = printer.write_int("fenceCount", 4, indent, signed=False, big_endian=False)
    pFences = printer.write_int("pFences", 8, indent, optional=False, count=fenceCount, big_endian=False)
    waitAll = printer.write_int("waitAll", 4, indent, signed=False, big_endian=False)
    timeout = printer.write_int("timeout", 8, indent, signed=False, big_endian=False)
    return

def struct_VkAllocationCallbacks(printer, indent: int):
    pUserData = printer.write_int("pUserData", 8, indent, optional=True, count=None, big_endian=False)
    pfnAllocation = printer.write_int("pfnAllocation", 8, indent, signed=False, big_endian=False)
    pfnReallocation = printer.write_int("pfnReallocation", 8, indent, signed=False, big_endian=False)
    pfnFree = printer.write_int("pfnFree", 8, indent, signed=False, big_endian=False)
    pfnInternalAllocation = printer.write_int("pfnInternalAllocation", 8, indent, signed=False, big_endian=False)
    pfnInternalFree = printer.write_int("pfnInternalFree", 8, indent, signed=False, big_endian=False)

def struct_VkAttachmentDescription(printer, indent: int):
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_enum("format", VkFormat, indent)
    printer.write_enum("samples", VkSampleCountFlagBits, indent)
    printer.write_enum("loadOp", VkAttachmentLoadOp, indent)
    printer.write_enum("storeOp", VkAttachmentStoreOp, indent)
    printer.write_enum("stencilLoadOp", VkAttachmentLoadOp, indent)
    printer.write_enum("stencilStoreOp", VkAttachmentStoreOp, indent)
    printer.write_enum("initialLayout", VkImageLayout, indent)
    printer.write_enum("finalLayout", VkImageLayout, indent)

def struct_VkAttachmentReference(printer, indent: int):
    attachment = printer.write_int("attachment", 4, indent, signed=False, big_endian=False)
    printer.write_enum("layout", VkImageLayout, indent)

def struct_VkBufferCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    size = printer.write_int("size", 8, indent, signed=False, big_endian=False)
    usage = printer.write_int("usage", 4, indent, signed=False, big_endian=False)
    printer.write_enum("sharingMode", VkSharingMode, indent)
    queueFamilyIndexCount = printer.write_int("queueFamilyIndexCount", 4, indent, signed=False, big_endian=False)
    pQueueFamilyIndices = printer.write_int("pQueueFamilyIndices", 4, indent, optional=True, count=queueFamilyIndexCount, big_endian=False)

def struct_VkBufferImageCopy(printer, indent: int):
    bufferOffset = printer.write_int("bufferOffset", 8, indent, signed=False, big_endian=False)
    bufferRowLength = printer.write_int("bufferRowLength", 4, indent, signed=False, big_endian=False)
    bufferImageHeight = printer.write_int("bufferImageHeight", 4, indent, signed=False, big_endian=False)
    printer.write_struct("imageSubresource", struct_VkImageSubresourceLayers, False, None, indent)
    printer.write_struct("imageOffset", struct_VkOffset3D, False, None, indent)
    printer.write_struct("imageExtent", struct_VkExtent3D, False, None, indent)

def struct_VkBufferMemoryBarrier(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER", indent)
    srcAccessMask = printer.write_int("srcAccessMask", 4, indent, signed=False, big_endian=False)
    dstAccessMask = printer.write_int("dstAccessMask", 4, indent, signed=False, big_endian=False)
    srcQueueFamilyIndex = printer.write_int("srcQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    dstQueueFamilyIndex = printer.write_int("dstQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    buffer = printer.write_int("buffer", 8, indent, signed=False, big_endian=False)
    offset = printer.write_int("offset", 8, indent, signed=False, big_endian=False)
    size = printer.write_int("size", 8, indent, signed=False, big_endian=False)

def struct_VkBufferMemoryBarrier2(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2", indent)
    srcStageMask = printer.write_int("srcStageMask", 4, indent, signed=False, big_endian=False)
    srcAccessMask = printer.write_int("srcAccessMask", 4, indent, signed=False, big_endian=False)
    dstStageMask = printer.write_int("dstStageMask", 4, indent, signed=False, big_endian=False)
    dstAccessMask = printer.write_int("dstAccessMask", 4, indent, signed=False, big_endian=False)
    srcQueueFamilyIndex = printer.write_int("srcQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    dstQueueFamilyIndex = printer.write_int("dstQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    buffer = printer.write_int("buffer", 8, indent, signed=False, big_endian=False)
    offset = printer.write_int("offset", 8, indent, signed=False, big_endian=False)
    size = printer.write_int("size", 8, indent, signed=False, big_endian=False)

def struct_VkClearAttachment(printer, indent: int):
    aspectMask = printer.write_int("aspectMask", 4, indent, signed=False, big_endian=False)
    colorAttachment = printer.write_int("colorAttachment", 4, indent, signed=False, big_endian=False)
    printer.write_struct("clearValue", struct_VkClearValue, False, None, indent)

def struct_VkClearColorValue(printer, indent: int):
    printer.write_float("float32", indent, count=4)

def struct_VkClearRect(printer, indent: int):
    printer.write_struct("rect", struct_VkRect2D, False, None, indent)
    baseArrayLayer = printer.write_int("baseArrayLayer", 4, indent, signed=False, big_endian=False)
    layerCount = printer.write_int("layerCount", 4, indent, signed=False, big_endian=False)

def struct_VkClearValue(printer, indent: int):
    printer.write_struct("color", struct_VkClearColorValue, False, None, indent)

def struct_VkCommandBufferBeginInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pInheritanceInfo", struct_VkCommandBufferInheritanceInfo, True, None, indent)

def struct_VkCommandBufferInheritanceInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO", indent)
    renderPass = printer.write_int("renderPass", 8, indent, signed=False, big_endian=False)
    subpass = printer.write_int("subpass", 4, indent, signed=False, big_endian=False)
    framebuffer = printer.write_int("framebuffer", 8, indent, signed=False, big_endian=False)
    occlusionQueryEnable = printer.write_int("occlusionQueryEnable", 4, indent, signed=False, big_endian=False)
    queryFlags = printer.write_int("queryFlags", 4, indent, signed=False, big_endian=False)
    pipelineStatistics = printer.write_int("pipelineStatistics", 4, indent, signed=False, big_endian=False)

def struct_VkComponentMapping(printer, indent: int):
    printer.write_enum("r", VkComponentSwizzle, indent)
    printer.write_enum("g", VkComponentSwizzle, indent)
    printer.write_enum("b", VkComponentSwizzle, indent)
    printer.write_enum("a", VkComponentSwizzle, indent)

def struct_VkDependencyInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_DEPENDENCY_INFO", indent)
    dependencyFlags = printer.write_int("dependencyFlags", 4, indent, signed=False, big_endian=False)
    memoryBarrierCount = printer.write_int("memoryBarrierCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pMemoryBarriers", struct_VkMemoryBarrier2, False, memoryBarrierCount, indent)
    bufferMemoryBarrierCount = printer.write_int("bufferMemoryBarrierCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pBufferMemoryBarriers", struct_VkBufferMemoryBarrier2, False, bufferMemoryBarrierCount, indent)
    imageMemoryBarrierCount = printer.write_int("imageMemoryBarrierCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pImageMemoryBarriers", struct_VkImageMemoryBarrier2, False, imageMemoryBarrierCount, indent)

def struct_VkDescriptorBufferInfo(printer, indent: int):
    buffer = printer.write_int("buffer", 8, indent, signed=False, big_endian=False)
    offset = printer.write_int("offset", 8, indent, signed=False, big_endian=False)
    range = printer.write_int("range", 8, indent, signed=False, big_endian=False)

def struct_VkDescriptorImageInfo(printer, indent: int):
    sampler = printer.write_int("sampler", 8, indent, signed=False, big_endian=False)
    imageView = printer.write_int("imageView", 8, indent, signed=False, big_endian=False)
    printer.write_enum("imageLayout", VkImageLayout, indent)

def struct_VkDescriptorPoolCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    maxSets = printer.write_int("maxSets", 4, indent, signed=False, big_endian=False)
    poolSizeCount = printer.write_int("poolSizeCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pPoolSizes", struct_VkDescriptorPoolSize, False, poolSizeCount, indent)

def struct_VkDescriptorPoolSize(printer, indent: int):
    printer.write_enum("type", VkDescriptorType, indent)
    descriptorCount = printer.write_int("descriptorCount", 4, indent, signed=False, big_endian=False)

def struct_VkDescriptorSetLayoutBinding(printer, indent: int):
    binding = printer.write_int("binding", 4, indent, signed=False, big_endian=False)
    printer.write_enum("descriptorType", VkDescriptorType, indent)
    descriptorCount = printer.write_int("descriptorCount", 4, indent, signed=False, big_endian=False)
    stageFlags = printer.write_int("stageFlags", 4, indent, signed=False, big_endian=False)
    pImmutableSamplers = printer.write_int("pImmutableSamplers", 8, indent, optional=True, count=descriptorCount, big_endian=False)

def struct_VkDescriptorSetLayoutCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    bindingCount = printer.write_int("bindingCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pBindings", struct_VkDescriptorSetLayoutBinding, False, bindingCount, indent)

def struct_VkExtent2D(printer, indent: int):
    width = printer.write_int("width", 4, indent, signed=False, big_endian=False)
    height = printer.write_int("height", 4, indent, signed=False, big_endian=False)

def struct_VkExtent3D(printer, indent: int):
    width = printer.write_int("width", 4, indent, signed=False, big_endian=False)
    height = printer.write_int("height", 4, indent, signed=False, big_endian=False)
    depth = printer.write_int("depth", 4, indent, signed=False, big_endian=False)

def struct_VkFenceCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_FENCE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)

def struct_VkFormatProperties(printer, indent: int):
    linearTilingFeatures = printer.write_int("linearTilingFeatures", 4, indent, signed=False, big_endian=False)
    optimalTilingFeatures = printer.write_int("optimalTilingFeatures", 4, indent, signed=False, big_endian=False)
    bufferFeatures = printer.write_int("bufferFeatures", 4, indent, signed=False, big_endian=False)

def struct_VkFramebufferCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    renderPass = printer.write_int("renderPass", 8, indent, signed=False, big_endian=False)
    attachmentCount = printer.write_int("attachmentCount", 4, indent, signed=False, big_endian=False)
    pAttachments = printer.write_int("pAttachments", 8, indent, optional=False, count=attachmentCount, big_endian=False)
    width = printer.write_int("width", 4, indent, signed=False, big_endian=False)
    height = printer.write_int("height", 4, indent, signed=False, big_endian=False)
    layers = printer.write_int("layers", 4, indent, signed=False, big_endian=False)

def struct_VkGraphicsPipelineCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    stageCount = printer.write_int("stageCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pStages", struct_VkPipelineShaderStageCreateInfo, False, stageCount, indent)
    printer.write_struct("pVertexInputState", struct_VkPipelineVertexInputStateCreateInfo, True, None, indent)
    printer.write_struct("pInputAssemblyState", struct_VkPipelineInputAssemblyStateCreateInfo, True, None, indent)
    printer.write_struct("pTessellationState", struct_VkPipelineTessellationStateCreateInfo, True, None, indent)
    printer.write_struct("pViewportState", struct_VkPipelineViewportStateCreateInfo, True, None, indent)
    printer.write_struct("pRasterizationState", struct_VkPipelineRasterizationStateCreateInfo, True, None, indent)
    printer.write_struct("pMultisampleState", struct_VkPipelineMultisampleStateCreateInfo, True, None, indent)
    printer.write_struct("pDepthStencilState", struct_VkPipelineDepthStencilStateCreateInfo, True, None, indent)
    printer.write_struct("pColorBlendState", struct_VkPipelineColorBlendStateCreateInfo, True, None, indent)
    printer.write_struct("pDynamicState", struct_VkPipelineDynamicStateCreateInfo, True, None, indent)
    layout = printer.write_int("layout", 8, indent, signed=False, big_endian=False)
    renderPass = printer.write_int("renderPass", 8, indent, signed=False, big_endian=False)
    subpass = printer.write_int("subpass", 4, indent, signed=False, big_endian=False)
    basePipelineHandle = printer.write_int("basePipelineHandle", 8, indent, signed=False, big_endian=False)
    basePipelineIndex = printer.write_int("basePipelineIndex", 4, indent, signed=True, big_endian=False)

def struct_VkImageCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_enum("imageType", VkImageType, indent)
    printer.write_enum("format", VkFormat, indent)
    printer.write_struct("extent", struct_VkExtent3D, False, None, indent)
    mipLevels = printer.write_int("mipLevels", 4, indent, signed=False, big_endian=False)
    arrayLayers = printer.write_int("arrayLayers", 4, indent, signed=False, big_endian=False)
    printer.write_enum("samples", VkSampleCountFlagBits, indent)
    printer.write_enum("tiling", VkImageTiling, indent)
    usage = printer.write_int("usage", 4, indent, signed=False, big_endian=False)
    printer.write_enum("sharingMode", VkSharingMode, indent)
    queueFamilyIndexCount = printer.write_int("queueFamilyIndexCount", 4, indent, signed=False, big_endian=False)
    pQueueFamilyIndices = printer.write_int("pQueueFamilyIndices", 4, indent, optional=True, count=queueFamilyIndexCount, big_endian=False)
    printer.write_enum("initialLayout", VkImageLayout, indent)

def struct_VkImageMemoryBarrier(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER", indent)
    srcAccessMask = printer.write_int("srcAccessMask", 4, indent, signed=False, big_endian=False)
    dstAccessMask = printer.write_int("dstAccessMask", 4, indent, signed=False, big_endian=False)
    printer.write_enum("oldLayout", VkImageLayout, indent)
    printer.write_enum("newLayout", VkImageLayout, indent)
    srcQueueFamilyIndex = printer.write_int("srcQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    dstQueueFamilyIndex = printer.write_int("dstQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    printer.write_struct("subresourceRange", struct_VkImageSubresourceRange, False, None, indent)

def struct_VkImageMemoryBarrier2(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2", indent)
    srcStageMask = printer.write_int("srcStageMask", 4, indent, signed=False, big_endian=False)
    srcAccessMask = printer.write_int("srcAccessMask", 4, indent, signed=False, big_endian=False)
    dstStageMask = printer.write_int("dstStageMask", 4, indent, signed=False, big_endian=False)
    dstAccessMask = printer.write_int("dstAccessMask", 4, indent, signed=False, big_endian=False)
    printer.write_enum("oldLayout", VkImageLayout, indent)
    printer.write_enum("newLayout", VkImageLayout, indent)
    srcQueueFamilyIndex = printer.write_int("srcQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    dstQueueFamilyIndex = printer.write_int("dstQueueFamilyIndex", 4, indent, signed=False, big_endian=False)
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    printer.write_struct("subresourceRange", struct_VkImageSubresourceRange, False, None, indent)

def struct_VkImageSubresourceLayers(printer, indent: int):
    aspectMask = printer.write_int("aspectMask", 4, indent, signed=False, big_endian=False)
    mipLevel = printer.write_int("mipLevel", 4, indent, signed=False, big_endian=False)
    baseArrayLayer = printer.write_int("baseArrayLayer", 4, indent, signed=False, big_endian=False)
    layerCount = printer.write_int("layerCount", 4, indent, signed=False, big_endian=False)

def struct_VkImageSubresourceRange(printer, indent: int):
    aspectMask = printer.write_int("aspectMask", 4, indent, signed=False, big_endian=False)
    baseMipLevel = printer.write_int("baseMipLevel", 4, indent, signed=False, big_endian=False)
    levelCount = printer.write_int("levelCount", 4, indent, signed=False, big_endian=False)
    baseArrayLayer = printer.write_int("baseArrayLayer", 4, indent, signed=False, big_endian=False)
    layerCount = printer.write_int("layerCount", 4, indent, signed=False, big_endian=False)

def struct_VkImageViewCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    image = printer.write_int("image", 8, indent, signed=False, big_endian=False)
    printer.write_enum("viewType", VkImageViewType, indent)
    printer.write_enum("format", VkFormat, indent)
    printer.write_struct("components", struct_VkComponentMapping, False, None, indent)
    printer.write_struct("subresourceRange", struct_VkImageSubresourceRange, False, None, indent)

def struct_VkMemoryAllocateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO", indent)
    allocationSize = printer.write_int("allocationSize", 8, indent, signed=False, big_endian=False)
    memoryTypeIndex = printer.write_int("memoryTypeIndex", 4, indent, signed=False, big_endian=False)

def struct_VkMemoryBarrier(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_MEMORY_BARRIER", indent)
    srcAccessMask = printer.write_int("srcAccessMask", 4, indent, signed=False, big_endian=False)
    dstAccessMask = printer.write_int("dstAccessMask", 4, indent, signed=False, big_endian=False)

def struct_VkMemoryBarrier2(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_MEMORY_BARRIER_2", indent)
    srcStageMask = printer.write_int("srcStageMask", 4, indent, signed=False, big_endian=False)
    srcAccessMask = printer.write_int("srcAccessMask", 4, indent, signed=False, big_endian=False)
    dstStageMask = printer.write_int("dstStageMask", 4, indent, signed=False, big_endian=False)
    dstAccessMask = printer.write_int("dstAccessMask", 4, indent, signed=False, big_endian=False)

def struct_VkMemoryRequirements(printer, indent: int):
    size = printer.write_int("size", 8, indent, signed=False, big_endian=False)
    alignment = printer.write_int("alignment", 8, indent, signed=False, big_endian=False)
    memoryTypeBits = printer.write_int("memoryTypeBits", 4, indent, signed=False, big_endian=False)

def struct_VkOffset2D(printer, indent: int):
    x = printer.write_int("x", 4, indent, signed=True, big_endian=False)
    y = printer.write_int("y", 4, indent, signed=True, big_endian=False)

def struct_VkOffset3D(printer, indent: int):
    x = printer.write_int("x", 4, indent, signed=True, big_endian=False)
    y = printer.write_int("y", 4, indent, signed=True, big_endian=False)
    z = printer.write_int("z", 4, indent, signed=True, big_endian=False)

def struct_VkPhysicalDeviceLimits(printer, indent: int):
    maxImageDimension1D = printer.write_int("maxImageDimension1D", 4, indent, signed=False, big_endian=False)
    maxImageDimension2D = printer.write_int("maxImageDimension2D", 4, indent, signed=False, big_endian=False)
    maxImageDimension3D = printer.write_int("maxImageDimension3D", 4, indent, signed=False, big_endian=False)
    maxImageDimensionCube = printer.write_int("maxImageDimensionCube", 4, indent, signed=False, big_endian=False)
    maxImageArrayLayers = printer.write_int("maxImageArrayLayers", 4, indent, signed=False, big_endian=False)
    maxTexelBufferElements = printer.write_int("maxTexelBufferElements", 4, indent, signed=False, big_endian=False)
    maxUniformBufferRange = printer.write_int("maxUniformBufferRange", 4, indent, signed=False, big_endian=False)
    maxStorageBufferRange = printer.write_int("maxStorageBufferRange", 4, indent, signed=False, big_endian=False)
    maxPushConstantsSize = printer.write_int("maxPushConstantsSize", 4, indent, signed=False, big_endian=False)
    maxMemoryAllocationCount = printer.write_int("maxMemoryAllocationCount", 4, indent, signed=False, big_endian=False)
    maxSamplerAllocationCount = printer.write_int("maxSamplerAllocationCount", 4, indent, signed=False, big_endian=False)
    bufferImageGranularity = printer.write_int("bufferImageGranularity", 8, indent, signed=False, big_endian=False)
    sparseAddressSpaceSize = printer.write_int("sparseAddressSpaceSize", 8, indent, signed=False, big_endian=False)
    maxBoundDescriptorSets = printer.write_int("maxBoundDescriptorSets", 4, indent, signed=False, big_endian=False)
    maxPerStageDescriptorSamplers = printer.write_int("maxPerStageDescriptorSamplers", 4, indent, signed=False, big_endian=False)
    maxPerStageDescriptorUniformBuffers = printer.write_int("maxPerStageDescriptorUniformBuffers", 4, indent, signed=False, big_endian=False)
    maxPerStageDescriptorStorageBuffers = printer.write_int("maxPerStageDescriptorStorageBuffers", 4, indent, signed=False, big_endian=False)
    maxPerStageDescriptorSampledImages = printer.write_int("maxPerStageDescriptorSampledImages", 4, indent, signed=False, big_endian=False)
    maxPerStageDescriptorStorageImages = printer.write_int("maxPerStageDescriptorStorageImages", 4, indent, signed=False, big_endian=False)
    maxPerStageDescriptorInputAttachments = printer.write_int("maxPerStageDescriptorInputAttachments", 4, indent, signed=False, big_endian=False)
    maxPerStageResources = printer.write_int("maxPerStageResources", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetSamplers = printer.write_int("maxDescriptorSetSamplers", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetUniformBuffers = printer.write_int("maxDescriptorSetUniformBuffers", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetUniformBuffersDynamic = printer.write_int("maxDescriptorSetUniformBuffersDynamic", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetStorageBuffers = printer.write_int("maxDescriptorSetStorageBuffers", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetStorageBuffersDynamic = printer.write_int("maxDescriptorSetStorageBuffersDynamic", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetSampledImages = printer.write_int("maxDescriptorSetSampledImages", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetStorageImages = printer.write_int("maxDescriptorSetStorageImages", 4, indent, signed=False, big_endian=False)
    maxDescriptorSetInputAttachments = printer.write_int("maxDescriptorSetInputAttachments", 4, indent, signed=False, big_endian=False)
    maxVertexInputAttributes = printer.write_int("maxVertexInputAttributes", 4, indent, signed=False, big_endian=False)
    maxVertexInputBindings = printer.write_int("maxVertexInputBindings", 4, indent, signed=False, big_endian=False)
    maxVertexInputAttributeOffset = printer.write_int("maxVertexInputAttributeOffset", 4, indent, signed=False, big_endian=False)
    maxVertexInputBindingStride = printer.write_int("maxVertexInputBindingStride", 4, indent, signed=False, big_endian=False)
    maxVertexOutputComponents = printer.write_int("maxVertexOutputComponents", 4, indent, signed=False, big_endian=False)
    maxTessellationGenerationLevel = printer.write_int("maxTessellationGenerationLevel", 4, indent, signed=False, big_endian=False)
    maxTessellationPatchSize = printer.write_int("maxTessellationPatchSize", 4, indent, signed=False, big_endian=False)
    maxTessellationControlPerVertexInputComponents = printer.write_int("maxTessellationControlPerVertexInputComponents", 4, indent, signed=False, big_endian=False)
    maxTessellationControlPerVertexOutputComponents = printer.write_int("maxTessellationControlPerVertexOutputComponents", 4, indent, signed=False, big_endian=False)
    maxTessellationControlPerPatchOutputComponents = printer.write_int("maxTessellationControlPerPatchOutputComponents", 4, indent, signed=False, big_endian=False)
    maxTessellationControlTotalOutputComponents = printer.write_int("maxTessellationControlTotalOutputComponents", 4, indent, signed=False, big_endian=False)
    maxTessellationEvaluationInputComponents = printer.write_int("maxTessellationEvaluationInputComponents", 4, indent, signed=False, big_endian=False)
    maxTessellationEvaluationOutputComponents = printer.write_int("maxTessellationEvaluationOutputComponents", 4, indent, signed=False, big_endian=False)
    maxGeometryShaderInvocations = printer.write_int("maxGeometryShaderInvocations", 4, indent, signed=False, big_endian=False)
    maxGeometryInputComponents = printer.write_int("maxGeometryInputComponents", 4, indent, signed=False, big_endian=False)
    maxGeometryOutputComponents = printer.write_int("maxGeometryOutputComponents", 4, indent, signed=False, big_endian=False)
    maxGeometryOutputVertices = printer.write_int("maxGeometryOutputVertices", 4, indent, signed=False, big_endian=False)
    maxGeometryTotalOutputComponents = printer.write_int("maxGeometryTotalOutputComponents", 4, indent, signed=False, big_endian=False)
    maxFragmentInputComponents = printer.write_int("maxFragmentInputComponents", 4, indent, signed=False, big_endian=False)
    maxFragmentOutputAttachments = printer.write_int("maxFragmentOutputAttachments", 4, indent, signed=False, big_endian=False)
    maxFragmentDualSrcAttachments = printer.write_int("maxFragmentDualSrcAttachments", 4, indent, signed=False, big_endian=False)
    maxFragmentCombinedOutputResources = printer.write_int("maxFragmentCombinedOutputResources", 4, indent, signed=False, big_endian=False)
    maxComputeSharedMemorySize = printer.write_int("maxComputeSharedMemorySize", 4, indent, signed=False, big_endian=False)
    printer.write_int("maxComputeWorkGroupCount", 4, indent, signed=False, count=3)
    maxComputeWorkGroupInvocations = printer.write_int("maxComputeWorkGroupInvocations", 4, indent, signed=False, big_endian=False)
    printer.write_int("maxComputeWorkGroupSize", 4, indent, signed=False, count=3)
    subPixelPrecisionBits = printer.write_int("subPixelPrecisionBits", 4, indent, signed=False, big_endian=False)
    subTexelPrecisionBits = printer.write_int("subTexelPrecisionBits", 4, indent, signed=False, big_endian=False)
    mipmapPrecisionBits = printer.write_int("mipmapPrecisionBits", 4, indent, signed=False, big_endian=False)
    maxDrawIndexedIndexValue = printer.write_int("maxDrawIndexedIndexValue", 4, indent, signed=False, big_endian=False)
    maxDrawIndirectCount = printer.write_int("maxDrawIndirectCount", 4, indent, signed=False, big_endian=False)
    printer.write_float("maxSamplerLodBias", indent)
    printer.write_float("maxSamplerAnisotropy", indent)
    maxViewports = printer.write_int("maxViewports", 4, indent, signed=False, big_endian=False)
    printer.write_int("maxViewportDimensions", 4, indent, signed=False, count=2)
    printer.write_float("viewportBoundsRange", indent, count=2)
    viewportSubPixelBits = printer.write_int("viewportSubPixelBits", 4, indent, signed=False, big_endian=False)
    minMemoryMapAlignment = printer.write_int("minMemoryMapAlignment", 8, indent, signed=False, big_endian=True)
    minTexelBufferOffsetAlignment = printer.write_int("minTexelBufferOffsetAlignment", 8, indent, signed=False, big_endian=False)
    minUniformBufferOffsetAlignment = printer.write_int("minUniformBufferOffsetAlignment", 8, indent, signed=False, big_endian=False)
    minStorageBufferOffsetAlignment = printer.write_int("minStorageBufferOffsetAlignment", 8, indent, signed=False, big_endian=False)
    minTexelOffset = printer.write_int("minTexelOffset", 4, indent, signed=True, big_endian=False)
    maxTexelOffset = printer.write_int("maxTexelOffset", 4, indent, signed=False, big_endian=False)
    minTexelGatherOffset = printer.write_int("minTexelGatherOffset", 4, indent, signed=True, big_endian=False)
    maxTexelGatherOffset = printer.write_int("maxTexelGatherOffset", 4, indent, signed=False, big_endian=False)
    printer.write_float("minInterpolationOffset", indent)
    printer.write_float("maxInterpolationOffset", indent)
    subPixelInterpolationOffsetBits = printer.write_int("subPixelInterpolationOffsetBits", 4, indent, signed=False, big_endian=False)
    maxFramebufferWidth = printer.write_int("maxFramebufferWidth", 4, indent, signed=False, big_endian=False)
    maxFramebufferHeight = printer.write_int("maxFramebufferHeight", 4, indent, signed=False, big_endian=False)
    maxFramebufferLayers = printer.write_int("maxFramebufferLayers", 4, indent, signed=False, big_endian=False)
    framebufferColorSampleCounts = printer.write_int("framebufferColorSampleCounts", 4, indent, signed=False, big_endian=False)
    framebufferDepthSampleCounts = printer.write_int("framebufferDepthSampleCounts", 4, indent, signed=False, big_endian=False)
    framebufferStencilSampleCounts = printer.write_int("framebufferStencilSampleCounts", 4, indent, signed=False, big_endian=False)
    framebufferNoAttachmentsSampleCounts = printer.write_int("framebufferNoAttachmentsSampleCounts", 4, indent, signed=False, big_endian=False)
    maxColorAttachments = printer.write_int("maxColorAttachments", 4, indent, signed=False, big_endian=False)
    sampledImageColorSampleCounts = printer.write_int("sampledImageColorSampleCounts", 4, indent, signed=False, big_endian=False)
    sampledImageIntegerSampleCounts = printer.write_int("sampledImageIntegerSampleCounts", 4, indent, signed=False, big_endian=False)
    sampledImageDepthSampleCounts = printer.write_int("sampledImageDepthSampleCounts", 4, indent, signed=False, big_endian=False)
    sampledImageStencilSampleCounts = printer.write_int("sampledImageStencilSampleCounts", 4, indent, signed=False, big_endian=False)
    storageImageSampleCounts = printer.write_int("storageImageSampleCounts", 4, indent, signed=False, big_endian=False)
    maxSampleMaskWords = printer.write_int("maxSampleMaskWords", 4, indent, signed=False, big_endian=False)
    timestampComputeAndGraphics = printer.write_int("timestampComputeAndGraphics", 4, indent, signed=False, big_endian=False)
    printer.write_float("timestampPeriod", indent)
    maxClipDistances = printer.write_int("maxClipDistances", 4, indent, signed=False, big_endian=False)
    maxCullDistances = printer.write_int("maxCullDistances", 4, indent, signed=False, big_endian=False)
    maxCombinedClipAndCullDistances = printer.write_int("maxCombinedClipAndCullDistances", 4, indent, signed=False, big_endian=False)
    discreteQueuePriorities = printer.write_int("discreteQueuePriorities", 4, indent, signed=False, big_endian=False)
    printer.write_float("pointSizeRange", indent, count=2)
    printer.write_float("lineWidthRange", indent, count=2)
    printer.write_float("pointSizeGranularity", indent)
    printer.write_float("lineWidthGranularity", indent)
    strictLines = printer.write_int("strictLines", 4, indent, signed=False, big_endian=False)
    standardSampleLocations = printer.write_int("standardSampleLocations", 4, indent, signed=False, big_endian=False)
    optimalBufferCopyOffsetAlignment = printer.write_int("optimalBufferCopyOffsetAlignment", 8, indent, signed=False, big_endian=False)
    optimalBufferCopyRowPitchAlignment = printer.write_int("optimalBufferCopyRowPitchAlignment", 8, indent, signed=False, big_endian=False)
    nonCoherentAtomSize = printer.write_int("nonCoherentAtomSize", 8, indent, signed=False, big_endian=False)

def struct_VkPhysicalDeviceProperties(printer, indent: int):
    apiVersion = printer.write_int("apiVersion", 4, indent, signed=False, big_endian=False)
    driverVersion = printer.write_int("driverVersion", 4, indent, signed=False, big_endian=False)
    vendorID = printer.write_int("vendorID", 4, indent, signed=False, big_endian=False)
    deviceID = printer.write_int("deviceID", 4, indent, signed=False, big_endian=False)
    printer.write_enum("deviceType", VkPhysicalDeviceType, indent)
    printer.write_string("deviceName", 256, indent)
    printer.write_int("pipelineCacheUUID", 1, indent, signed=False, count=16)
    printer.write_struct("limits", struct_VkPhysicalDeviceLimits, False, None, indent)
    printer.write_struct("sparseProperties", struct_VkPhysicalDeviceSparseProperties, False, None, indent)

def struct_VkPhysicalDeviceProperties2(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2", indent)
    printer.write_struct("properties", struct_VkPhysicalDeviceProperties, False, None, indent)

def struct_VkPhysicalDeviceSparseProperties(printer, indent: int):
    residencyStandard2DBlockShape = printer.write_int("residencyStandard2DBlockShape", 4, indent, signed=False, big_endian=False)
    residencyStandard2DMultisampleBlockShape = printer.write_int("residencyStandard2DMultisampleBlockShape", 4, indent, signed=False, big_endian=False)
    residencyStandard3DBlockShape = printer.write_int("residencyStandard3DBlockShape", 4, indent, signed=False, big_endian=False)
    residencyAlignedMipSize = printer.write_int("residencyAlignedMipSize", 4, indent, signed=False, big_endian=False)
    residencyNonResidentStrict = printer.write_int("residencyNonResidentStrict", 4, indent, signed=False, big_endian=False)

def struct_VkPipelineCacheCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    initialDataSize = printer.write_int("initialDataSize", 8, indent, signed=False, big_endian=True)
    pInitialData = printer.write_int("pInitialData", 8, indent, optional=False, count=initialDataSize, big_endian=False)

def struct_VkPipelineColorBlendAttachmentState(printer, indent: int):
    blendEnable = printer.write_int("blendEnable", 4, indent, signed=False, big_endian=False)
    printer.write_enum("srcColorBlendFactor", VkBlendFactor, indent)
    printer.write_enum("dstColorBlendFactor", VkBlendFactor, indent)
    printer.write_enum("colorBlendOp", VkBlendOp, indent)
    printer.write_enum("srcAlphaBlendFactor", VkBlendFactor, indent)
    printer.write_enum("dstAlphaBlendFactor", VkBlendFactor, indent)
    printer.write_enum("alphaBlendOp", VkBlendOp, indent)
    colorWriteMask = printer.write_int("colorWriteMask", 4, indent, signed=False, big_endian=False)

def struct_VkPipelineColorBlendStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    logicOpEnable = printer.write_int("logicOpEnable", 4, indent, signed=False, big_endian=False)
    printer.write_enum("logicOp", VkLogicOp, indent)
    attachmentCount = printer.write_int("attachmentCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pAttachments", struct_VkPipelineColorBlendAttachmentState, False, attachmentCount, indent)
    printer.write_float("blendConstants", indent, count=4)

def struct_VkPipelineDepthStencilStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    depthTestEnable = printer.write_int("depthTestEnable", 4, indent, signed=False, big_endian=False)
    depthWriteEnable = printer.write_int("depthWriteEnable", 4, indent, signed=False, big_endian=False)
    printer.write_enum("depthCompareOp", VkCompareOp, indent)
    depthBoundsTestEnable = printer.write_int("depthBoundsTestEnable", 4, indent, signed=False, big_endian=False)
    stencilTestEnable = printer.write_int("stencilTestEnable", 4, indent, signed=False, big_endian=False)
    printer.write_struct("front", struct_VkStencilOpState, False, None, indent)
    printer.write_struct("back", struct_VkStencilOpState, False, None, indent)
    printer.write_float("minDepthBounds", indent)
    printer.write_float("maxDepthBounds", indent)

def struct_VkPipelineDynamicStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    dynamicStateCount = printer.write_int("dynamicStateCount", 4, indent, signed=False, big_endian=False)
    printer.write_enum("pDynamicStates", VkDynamicState, indent)

def struct_VkPipelineInputAssemblyStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_enum("topology", VkPrimitiveTopology, indent)
    primitiveRestartEnable = printer.write_int("primitiveRestartEnable", 4, indent, signed=False, big_endian=False)

def struct_VkPipelineLayoutCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    setLayoutCount = printer.write_int("setLayoutCount", 4, indent, signed=False, big_endian=False)
    pSetLayouts = printer.write_int("pSetLayouts", 8, indent, optional=False, count=setLayoutCount, big_endian=False)
    pushConstantRangeCount = printer.write_int("pushConstantRangeCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pPushConstantRanges", struct_VkPushConstantRange, False, pushConstantRangeCount, indent)

def struct_VkPipelineMultisampleStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_enum("rasterizationSamples", VkSampleCountFlagBits, indent)
    sampleShadingEnable = printer.write_int("sampleShadingEnable", 4, indent, signed=False, big_endian=False)
    printer.write_float("minSampleShading", indent)
    pSampleMask = printer.write_int("pSampleMask", 8, indent, optional=True, count=int(rasterizationSamples / 32), big_endian=False)
    alphaToCoverageEnable = printer.write_int("alphaToCoverageEnable", 4, indent, signed=False, big_endian=False)
    alphaToOneEnable = printer.write_int("alphaToOneEnable", 4, indent, signed=False, big_endian=False)

def struct_VkPipelineRasterizationStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    depthClampEnable = printer.write_int("depthClampEnable", 4, indent, signed=False, big_endian=False)
    rasterizerDiscardEnable = printer.write_int("rasterizerDiscardEnable", 4, indent, signed=False, big_endian=False)
    printer.write_enum("polygonMode", VkPolygonMode, indent)
    cullMode = printer.write_int("cullMode", 4, indent, signed=False, big_endian=False)
    printer.write_enum("frontFace", VkFrontFace, indent)
    depthBiasEnable = printer.write_int("depthBiasEnable", 4, indent, signed=False, big_endian=False)
    printer.write_float("depthBiasConstantFactor", indent)
    printer.write_float("depthBiasClamp", indent)
    printer.write_float("depthBiasSlopeFactor", indent)
    printer.write_float("lineWidth", indent)

def struct_VkPipelineShaderStageCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_enum("stage", VkShaderStageFlagBits, indent)
    module = printer.write_int("module", 8, indent, signed=False, big_endian=False)
    printer.write_string("pName", None, indent)
    printer.write_struct("pSpecializationInfo", struct_VkSpecializationInfo, True, None, indent)

def struct_VkPipelineTessellationStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    patchControlPoints = printer.write_int("patchControlPoints", 4, indent, signed=False, big_endian=False)

def struct_VkPipelineVertexInputStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    vertexBindingDescriptionCount = printer.write_int("vertexBindingDescriptionCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pVertexBindingDescriptions", struct_VkVertexInputBindingDescription, False, vertexBindingDescriptionCount, indent)
    vertexAttributeDescriptionCount = printer.write_int("vertexAttributeDescriptionCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pVertexAttributeDescriptions", struct_VkVertexInputAttributeDescription, False, vertexAttributeDescriptionCount, indent)

def struct_VkPipelineViewportStateCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    viewportCount = printer.write_int("viewportCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pViewports", struct_VkViewport, True, viewportCount, indent)
    scissorCount = printer.write_int("scissorCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pScissors", struct_VkRect2D, True, scissorCount, indent)

def struct_VkPushConstantRange(printer, indent: int):
    stageFlags = printer.write_int("stageFlags", 4, indent, signed=False, big_endian=False)
    offset = printer.write_int("offset", 4, indent, signed=False, big_endian=False)
    size = printer.write_int("size", 4, indent, signed=False, big_endian=False)

def struct_VkRect2D(printer, indent: int):
    printer.write_struct("offset", struct_VkOffset2D, False, None, indent)
    printer.write_struct("extent", struct_VkExtent2D, False, None, indent)

def struct_VkRenderPassBeginInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO", indent)
    renderPass = printer.write_int("renderPass", 8, indent, signed=False, big_endian=False)
    framebuffer = printer.write_int("framebuffer", 8, indent, signed=False, big_endian=False)
    printer.write_struct("renderArea", struct_VkRect2D, False, None, indent)
    clearValueCount = printer.write_int("clearValueCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pClearValues", struct_VkClearValue, True, clearValueCount, indent)

def struct_VkRenderPassCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    attachmentCount = printer.write_int("attachmentCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pAttachments", struct_VkAttachmentDescription, False, attachmentCount, indent)
    subpassCount = printer.write_int("subpassCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pSubpasses", struct_VkSubpassDescription, False, subpassCount, indent)
    dependencyCount = printer.write_int("dependencyCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pDependencies", struct_VkSubpassDependency, False, dependencyCount, indent)

def struct_VkSamplerCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_enum("magFilter", VkFilter, indent)
    printer.write_enum("minFilter", VkFilter, indent)
    printer.write_enum("mipmapMode", VkSamplerMipmapMode, indent)
    printer.write_enum("addressModeU", VkSamplerAddressMode, indent)
    printer.write_enum("addressModeV", VkSamplerAddressMode, indent)
    printer.write_enum("addressModeW", VkSamplerAddressMode, indent)
    printer.write_float("mipLodBias", indent)
    anisotropyEnable = printer.write_int("anisotropyEnable", 4, indent, signed=False, big_endian=False)
    printer.write_float("maxAnisotropy", indent)
    compareEnable = printer.write_int("compareEnable", 4, indent, signed=False, big_endian=False)
    printer.write_enum("compareOp", VkCompareOp, indent)
    printer.write_float("minLod", indent)
    printer.write_float("maxLod", indent)
    printer.write_enum("borderColor", VkBorderColor, indent)
    unnormalizedCoordinates = printer.write_int("unnormalizedCoordinates", 4, indent, signed=False, big_endian=False)

def struct_VkSemaphoreCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)

def struct_VkShaderModuleCreateInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO", indent)
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    codeSize = printer.write_int("codeSize", 8, indent, signed=False, big_endian=True)
    pCode = printer.write_int("pCode", 4, indent, optional=False, count=int(codeSize / 4), big_endian=False)

def struct_VkSpecializationInfo(printer, indent: int):
    mapEntryCount = printer.write_int("mapEntryCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pMapEntries", struct_VkSpecializationMapEntry, False, mapEntryCount, indent)
    dataSize = printer.write_int("dataSize", 8, indent, signed=False, big_endian=True)
    pData = printer.write_int("pData", 8, indent, optional=False, count=dataSize, big_endian=False)

def struct_VkSpecializationMapEntry(printer, indent: int):
    constantID = printer.write_int("constantID", 4, indent, signed=False, big_endian=False)
    offset = printer.write_int("offset", 4, indent, signed=False, big_endian=False)
    size = printer.write_int("size", 8, indent, signed=False, big_endian=True)

def struct_VkStencilOpState(printer, indent: int):
    printer.write_enum("failOp", VkStencilOp, indent)
    printer.write_enum("passOp", VkStencilOp, indent)
    printer.write_enum("depthFailOp", VkStencilOp, indent)
    printer.write_enum("compareOp", VkCompareOp, indent)
    compareMask = printer.write_int("compareMask", 4, indent, signed=False, big_endian=False)
    writeMask = printer.write_int("writeMask", 4, indent, signed=False, big_endian=False)
    reference = printer.write_int("reference", 4, indent, signed=False, big_endian=False)

def struct_VkSubmitInfo(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_SUBMIT_INFO", indent)
    waitSemaphoreCount = printer.write_int("waitSemaphoreCount", 4, indent, signed=False, big_endian=False)
    pWaitSemaphores = printer.write_int("pWaitSemaphores", 8, indent, optional=False, count=waitSemaphoreCount, big_endian=False)
    pWaitDstStageMask = printer.write_int("pWaitDstStageMask", 4, indent, optional=False, count=waitSemaphoreCount, big_endian=False)
    commandBufferCount = printer.write_int("commandBufferCount", 4, indent, signed=False, big_endian=False)
    pCommandBuffers = printer.write_int("pCommandBuffers", 8, indent, optional=False, count=commandBufferCount, big_endian=False)
    signalSemaphoreCount = printer.write_int("signalSemaphoreCount", 4, indent, signed=False, big_endian=False)
    pSignalSemaphores = printer.write_int("pSignalSemaphores", 8, indent, optional=False, count=signalSemaphoreCount, big_endian=False)

def struct_VkSubpassDependency(printer, indent: int):
    srcSubpass = printer.write_int("srcSubpass", 4, indent, signed=False, big_endian=False)
    dstSubpass = printer.write_int("dstSubpass", 4, indent, signed=False, big_endian=False)
    srcStageMask = printer.write_int("srcStageMask", 4, indent, signed=False, big_endian=False)
    dstStageMask = printer.write_int("dstStageMask", 4, indent, signed=False, big_endian=False)
    srcAccessMask = printer.write_int("srcAccessMask", 4, indent, signed=False, big_endian=False)
    dstAccessMask = printer.write_int("dstAccessMask", 4, indent, signed=False, big_endian=False)
    dependencyFlags = printer.write_int("dependencyFlags", 4, indent, signed=False, big_endian=False)

def struct_VkSubpassDescription(printer, indent: int):
    flags = printer.write_int("flags", 4, indent, signed=False, big_endian=False)
    printer.write_enum("pipelineBindPoint", VkPipelineBindPoint, indent)
    inputAttachmentCount = printer.write_int("inputAttachmentCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pInputAttachments", struct_VkAttachmentReference, False, inputAttachmentCount, indent)
    colorAttachmentCount = printer.write_int("colorAttachmentCount", 4, indent, signed=False, big_endian=False)
    printer.write_struct("pColorAttachments", struct_VkAttachmentReference, False, colorAttachmentCount, indent)
    printer.write_struct("pResolveAttachments", struct_VkAttachmentReference, True, colorAttachmentCount, indent)
    printer.write_struct("pDepthStencilAttachment", struct_VkAttachmentReference, True, None, indent)
    preserveAttachmentCount = printer.write_int("preserveAttachmentCount", 4, indent, signed=False, big_endian=False)
    pPreserveAttachments = printer.write_int("pPreserveAttachments", 4, indent, optional=False, count=preserveAttachmentCount, big_endian=False)

def struct_VkVertexInputAttributeDescription(printer, indent: int):
    location = printer.write_int("location", 4, indent, signed=False, big_endian=False)
    binding = printer.write_int("binding", 4, indent, signed=False, big_endian=False)
    printer.write_enum("format", VkFormat, indent)
    offset = printer.write_int("offset", 4, indent, signed=False, big_endian=False)

def struct_VkVertexInputBindingDescription(printer, indent: int):
    binding = printer.write_int("binding", 4, indent, signed=False, big_endian=False)
    stride = printer.write_int("stride", 4, indent, signed=False, big_endian=False)
    printer.write_enum("inputRate", VkVertexInputRate, indent)

def struct_VkViewport(printer, indent: int):
    printer.write_float("x", indent)
    printer.write_float("y", indent)
    printer.write_float("width", indent)
    printer.write_float("height", indent)
    printer.write_float("minDepth", indent)
    printer.write_float("maxDepth", indent)

def struct_VkWriteDescriptorSet(printer, indent: int):
    printer.write_stype_and_pnext("VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET", indent)
    dstSet = printer.write_int("dstSet", 8, indent, signed=False, big_endian=False)
    dstBinding = printer.write_int("dstBinding", 4, indent, signed=False, big_endian=False)
    dstArrayElement = printer.write_int("dstArrayElement", 4, indent, signed=False, big_endian=False)
    descriptorCount = printer.write_int("descriptorCount", 4, indent, signed=False, big_endian=False)
    printer.write_enum("descriptorType", VkDescriptorType, indent)
    printer.write_struct("pImageInfo", struct_VkDescriptorImageInfo, True, descriptorCount, indent)
    printer.write_struct("pBufferInfo", struct_VkDescriptorBufferInfo, True, descriptorCount, indent)
    pTexelBufferView = printer.write_int("pTexelBufferView", 8, indent, optional=True, count=descriptorCount, big_endian=False)

VkAttachmentLoadOp = {
    0: "VK_ATTACHMENT_LOAD_OP_LOAD",
    1: "VK_ATTACHMENT_LOAD_OP_CLEAR",
    2: "VK_ATTACHMENT_LOAD_OP_DONT_CARE",
    1000400000: "VK_ATTACHMENT_LOAD_OP_NONE_EXT",
}

VkAttachmentStoreOp = {
    0: "VK_ATTACHMENT_STORE_OP_STORE",
    1: "VK_ATTACHMENT_STORE_OP_DONT_CARE",
    1000301000: "VK_ATTACHMENT_STORE_OP_NONE",
}

VkBlendFactor = {
    0: "VK_BLEND_FACTOR_ZERO",
    1: "VK_BLEND_FACTOR_ONE",
    2: "VK_BLEND_FACTOR_SRC_COLOR",
    3: "VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR",
    4: "VK_BLEND_FACTOR_DST_COLOR",
    5: "VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR",
    6: "VK_BLEND_FACTOR_SRC_ALPHA",
    7: "VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA",
    8: "VK_BLEND_FACTOR_DST_ALPHA",
    9: "VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA",
    10: "VK_BLEND_FACTOR_CONSTANT_COLOR",
    11: "VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR",
    12: "VK_BLEND_FACTOR_CONSTANT_ALPHA",
    13: "VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA",
    14: "VK_BLEND_FACTOR_SRC_ALPHA_SATURATE",
    15: "VK_BLEND_FACTOR_SRC1_COLOR",
    16: "VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR",
    17: "VK_BLEND_FACTOR_SRC1_ALPHA",
    18: "VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA",
}

VkBlendOp = {
    0: "VK_BLEND_OP_ADD",
    1: "VK_BLEND_OP_SUBTRACT",
    2: "VK_BLEND_OP_REVERSE_SUBTRACT",
    3: "VK_BLEND_OP_MIN",
    4: "VK_BLEND_OP_MAX",
    1000148000: "VK_BLEND_OP_ZERO_EXT",
    1000148001: "VK_BLEND_OP_SRC_EXT",
    1000148002: "VK_BLEND_OP_DST_EXT",
    1000148003: "VK_BLEND_OP_SRC_OVER_EXT",
    1000148004: "VK_BLEND_OP_DST_OVER_EXT",
    1000148005: "VK_BLEND_OP_SRC_IN_EXT",
    1000148006: "VK_BLEND_OP_DST_IN_EXT",
    1000148007: "VK_BLEND_OP_SRC_OUT_EXT",
    1000148008: "VK_BLEND_OP_DST_OUT_EXT",
    1000148009: "VK_BLEND_OP_SRC_ATOP_EXT",
    1000148010: "VK_BLEND_OP_DST_ATOP_EXT",
    1000148011: "VK_BLEND_OP_XOR_EXT",
    1000148012: "VK_BLEND_OP_MULTIPLY_EXT",
    1000148013: "VK_BLEND_OP_SCREEN_EXT",
    1000148014: "VK_BLEND_OP_OVERLAY_EXT",
    1000148015: "VK_BLEND_OP_DARKEN_EXT",
    1000148016: "VK_BLEND_OP_LIGHTEN_EXT",
    1000148017: "VK_BLEND_OP_COLORDODGE_EXT",
    1000148018: "VK_BLEND_OP_COLORBURN_EXT",
    1000148019: "VK_BLEND_OP_HARDLIGHT_EXT",
    1000148020: "VK_BLEND_OP_SOFTLIGHT_EXT",
    1000148021: "VK_BLEND_OP_DIFFERENCE_EXT",
    1000148022: "VK_BLEND_OP_EXCLUSION_EXT",
    1000148023: "VK_BLEND_OP_INVERT_EXT",
    1000148024: "VK_BLEND_OP_INVERT_RGB_EXT",
    1000148025: "VK_BLEND_OP_LINEARDODGE_EXT",
    1000148026: "VK_BLEND_OP_LINEARBURN_EXT",
    1000148027: "VK_BLEND_OP_VIVIDLIGHT_EXT",
    1000148028: "VK_BLEND_OP_LINEARLIGHT_EXT",
    1000148029: "VK_BLEND_OP_PINLIGHT_EXT",
    1000148030: "VK_BLEND_OP_HARDMIX_EXT",
    1000148031: "VK_BLEND_OP_HSL_HUE_EXT",
    1000148032: "VK_BLEND_OP_HSL_SATURATION_EXT",
    1000148033: "VK_BLEND_OP_HSL_COLOR_EXT",
    1000148034: "VK_BLEND_OP_HSL_LUMINOSITY_EXT",
    1000148035: "VK_BLEND_OP_PLUS_EXT",
    1000148036: "VK_BLEND_OP_PLUS_CLAMPED_EXT",
    1000148037: "VK_BLEND_OP_PLUS_CLAMPED_ALPHA_EXT",
    1000148038: "VK_BLEND_OP_PLUS_DARKER_EXT",
    1000148039: "VK_BLEND_OP_MINUS_EXT",
    1000148040: "VK_BLEND_OP_MINUS_CLAMPED_EXT",
    1000148041: "VK_BLEND_OP_CONTRAST_EXT",
    1000148042: "VK_BLEND_OP_INVERT_OVG_EXT",
    1000148043: "VK_BLEND_OP_RED_EXT",
    1000148044: "VK_BLEND_OP_GREEN_EXT",
    1000148045: "VK_BLEND_OP_BLUE_EXT",
}

VkBorderColor = {
    0: "VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK",
    1: "VK_BORDER_COLOR_INT_TRANSPARENT_BLACK",
    2: "VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK",
    3: "VK_BORDER_COLOR_INT_OPAQUE_BLACK",
    4: "VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE",
    5: "VK_BORDER_COLOR_INT_OPAQUE_WHITE",
    1000287003: "VK_BORDER_COLOR_FLOAT_CUSTOM_EXT",
    1000287004: "VK_BORDER_COLOR_INT_CUSTOM_EXT",
}

VkCompareOp = {
    0: "VK_COMPARE_OP_NEVER",
    1: "VK_COMPARE_OP_LESS",
    2: "VK_COMPARE_OP_EQUAL",
    3: "VK_COMPARE_OP_LESS_OR_EQUAL",
    4: "VK_COMPARE_OP_GREATER",
    5: "VK_COMPARE_OP_NOT_EQUAL",
    6: "VK_COMPARE_OP_GREATER_OR_EQUAL",
    7: "VK_COMPARE_OP_ALWAYS",
}

VkComponentSwizzle = {
    0: "VK_COMPONENT_SWIZZLE_IDENTITY",
    1: "VK_COMPONENT_SWIZZLE_ZERO",
    2: "VK_COMPONENT_SWIZZLE_ONE",
    3: "VK_COMPONENT_SWIZZLE_R",
    4: "VK_COMPONENT_SWIZZLE_G",
    5: "VK_COMPONENT_SWIZZLE_B",
    6: "VK_COMPONENT_SWIZZLE_A",
}

VkDescriptorType = {
    0: "VK_DESCRIPTOR_TYPE_SAMPLER",
    1: "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER",
    2: "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE",
    3: "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
    4: "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER",
    5: "VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER",
    6: "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER",
    7: "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER",
    8: "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC",
    9: "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC",
    10: "VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT",
    1000138000: "VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK",
    1000150000: "VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR",
    1000165000: "VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV",
    1000440000: "VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM",
    1000440001: "VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM",
    1000351000: "VK_DESCRIPTOR_TYPE_MUTABLE_EXT",
}

VkDynamicState = {
    0: "VK_DYNAMIC_STATE_VIEWPORT",
    1: "VK_DYNAMIC_STATE_SCISSOR",
    2: "VK_DYNAMIC_STATE_LINE_WIDTH",
    3: "VK_DYNAMIC_STATE_DEPTH_BIAS",
    4: "VK_DYNAMIC_STATE_BLEND_CONSTANTS",
    5: "VK_DYNAMIC_STATE_DEPTH_BOUNDS",
    6: "VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK",
    7: "VK_DYNAMIC_STATE_STENCIL_WRITE_MASK",
    8: "VK_DYNAMIC_STATE_STENCIL_REFERENCE",
    1000267000: "VK_DYNAMIC_STATE_CULL_MODE",
    1000267001: "VK_DYNAMIC_STATE_FRONT_FACE",
    1000267002: "VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY",
    1000267003: "VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT",
    1000267004: "VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT",
    1000267005: "VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE",
    1000267006: "VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE",
    1000267007: "VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE",
    1000267008: "VK_DYNAMIC_STATE_DEPTH_COMPARE_OP",
    1000267009: "VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE",
    1000267010: "VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE",
    1000267011: "VK_DYNAMIC_STATE_STENCIL_OP",
    1000377001: "VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE",
    1000377002: "VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE",
    1000377004: "VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE",
    1000087000: "VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_NV",
    1000099000: "VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT",
    1000099001: "VK_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT",
    1000099002: "VK_DYNAMIC_STATE_DISCARD_RECTANGLE_MODE_EXT",
    1000143000: "VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT",
    1000347000: "VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR",
    1000164004: "VK_DYNAMIC_STATE_VIEWPORT_SHADING_RATE_PALETTE_NV",
    1000164006: "VK_DYNAMIC_STATE_VIEWPORT_COARSE_SAMPLE_ORDER_NV",
    1000205000: "VK_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_ENABLE_NV",
    1000205001: "VK_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_NV",
    1000226000: "VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR",
    1000352000: "VK_DYNAMIC_STATE_VERTEX_INPUT_EXT",
    1000377000: "VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT",
    1000377003: "VK_DYNAMIC_STATE_LOGIC_OP_EXT",
    1000381000: "VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT",
    1000455003: "VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT",
    1000455004: "VK_DYNAMIC_STATE_POLYGON_MODE_EXT",
    1000455005: "VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT",
    1000455006: "VK_DYNAMIC_STATE_SAMPLE_MASK_EXT",
    1000455007: "VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT",
    1000455008: "VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT",
    1000455009: "VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT",
    1000455010: "VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT",
    1000455011: "VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT",
    1000455012: "VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT",
    1000455002: "VK_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT",
    1000455013: "VK_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT",
    1000455014: "VK_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT",
    1000455015: "VK_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT",
    1000455016: "VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT",
    1000455017: "VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT",
    1000455018: "VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT",
    1000455019: "VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT",
    1000455020: "VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT",
    1000455021: "VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT",
    1000455022: "VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT",
    1000455023: "VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV",
    1000455024: "VK_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV",
    1000455025: "VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV",
    1000455026: "VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_LOCATION_NV",
    1000455027: "VK_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV",
    1000455028: "VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV",
    1000455029: "VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_NV",
    1000455030: "VK_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV",
    1000455031: "VK_DYNAMIC_STATE_REPRESENTATIVE_FRAGMENT_TEST_ENABLE_NV",
    1000455032: "VK_DYNAMIC_STATE_COVERAGE_REDUCTION_MODE_NV",
    1000524000: "VK_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT",
    1000259000: "VK_DYNAMIC_STATE_LINE_STIPPLE_KHR",
}

VkFilter = {
    0: "VK_FILTER_NEAREST",
    1: "VK_FILTER_LINEAR",
    1000015000: "VK_FILTER_CUBIC_EXT",
}

VkFormat = {
    0: "VK_FORMAT_UNDEFINED",
    1: "VK_FORMAT_R4G4_UNORM_PACK8",
    2: "VK_FORMAT_R4G4B4A4_UNORM_PACK16",
    3: "VK_FORMAT_B4G4R4A4_UNORM_PACK16",
    4: "VK_FORMAT_R5G6B5_UNORM_PACK16",
    5: "VK_FORMAT_B5G6R5_UNORM_PACK16",
    6: "VK_FORMAT_R5G5B5A1_UNORM_PACK16",
    7: "VK_FORMAT_B5G5R5A1_UNORM_PACK16",
    8: "VK_FORMAT_A1R5G5B5_UNORM_PACK16",
    9: "VK_FORMAT_R8_UNORM",
    10: "VK_FORMAT_R8_SNORM",
    11: "VK_FORMAT_R8_USCALED",
    12: "VK_FORMAT_R8_SSCALED",
    13: "VK_FORMAT_R8_UINT",
    14: "VK_FORMAT_R8_SINT",
    15: "VK_FORMAT_R8_SRGB",
    16: "VK_FORMAT_R8G8_UNORM",
    17: "VK_FORMAT_R8G8_SNORM",
    18: "VK_FORMAT_R8G8_USCALED",
    19: "VK_FORMAT_R8G8_SSCALED",
    20: "VK_FORMAT_R8G8_UINT",
    21: "VK_FORMAT_R8G8_SINT",
    22: "VK_FORMAT_R8G8_SRGB",
    23: "VK_FORMAT_R8G8B8_UNORM",
    24: "VK_FORMAT_R8G8B8_SNORM",
    25: "VK_FORMAT_R8G8B8_USCALED",
    26: "VK_FORMAT_R8G8B8_SSCALED",
    27: "VK_FORMAT_R8G8B8_UINT",
    28: "VK_FORMAT_R8G8B8_SINT",
    29: "VK_FORMAT_R8G8B8_SRGB",
    30: "VK_FORMAT_B8G8R8_UNORM",
    31: "VK_FORMAT_B8G8R8_SNORM",
    32: "VK_FORMAT_B8G8R8_USCALED",
    33: "VK_FORMAT_B8G8R8_SSCALED",
    34: "VK_FORMAT_B8G8R8_UINT",
    35: "VK_FORMAT_B8G8R8_SINT",
    36: "VK_FORMAT_B8G8R8_SRGB",
    37: "VK_FORMAT_R8G8B8A8_UNORM",
    38: "VK_FORMAT_R8G8B8A8_SNORM",
    39: "VK_FORMAT_R8G8B8A8_USCALED",
    40: "VK_FORMAT_R8G8B8A8_SSCALED",
    41: "VK_FORMAT_R8G8B8A8_UINT",
    42: "VK_FORMAT_R8G8B8A8_SINT",
    43: "VK_FORMAT_R8G8B8A8_SRGB",
    44: "VK_FORMAT_B8G8R8A8_UNORM",
    45: "VK_FORMAT_B8G8R8A8_SNORM",
    46: "VK_FORMAT_B8G8R8A8_USCALED",
    47: "VK_FORMAT_B8G8R8A8_SSCALED",
    48: "VK_FORMAT_B8G8R8A8_UINT",
    49: "VK_FORMAT_B8G8R8A8_SINT",
    50: "VK_FORMAT_B8G8R8A8_SRGB",
    51: "VK_FORMAT_A8B8G8R8_UNORM_PACK32",
    52: "VK_FORMAT_A8B8G8R8_SNORM_PACK32",
    53: "VK_FORMAT_A8B8G8R8_USCALED_PACK32",
    54: "VK_FORMAT_A8B8G8R8_SSCALED_PACK32",
    55: "VK_FORMAT_A8B8G8R8_UINT_PACK32",
    56: "VK_FORMAT_A8B8G8R8_SINT_PACK32",
    57: "VK_FORMAT_A8B8G8R8_SRGB_PACK32",
    58: "VK_FORMAT_A2R10G10B10_UNORM_PACK32",
    59: "VK_FORMAT_A2R10G10B10_SNORM_PACK32",
    60: "VK_FORMAT_A2R10G10B10_USCALED_PACK32",
    61: "VK_FORMAT_A2R10G10B10_SSCALED_PACK32",
    62: "VK_FORMAT_A2R10G10B10_UINT_PACK32",
    63: "VK_FORMAT_A2R10G10B10_SINT_PACK32",
    64: "VK_FORMAT_A2B10G10R10_UNORM_PACK32",
    65: "VK_FORMAT_A2B10G10R10_SNORM_PACK32",
    66: "VK_FORMAT_A2B10G10R10_USCALED_PACK32",
    67: "VK_FORMAT_A2B10G10R10_SSCALED_PACK32",
    68: "VK_FORMAT_A2B10G10R10_UINT_PACK32",
    69: "VK_FORMAT_A2B10G10R10_SINT_PACK32",
    70: "VK_FORMAT_R16_UNORM",
    71: "VK_FORMAT_R16_SNORM",
    72: "VK_FORMAT_R16_USCALED",
    73: "VK_FORMAT_R16_SSCALED",
    74: "VK_FORMAT_R16_UINT",
    75: "VK_FORMAT_R16_SINT",
    76: "VK_FORMAT_R16_SFLOAT",
    77: "VK_FORMAT_R16G16_UNORM",
    78: "VK_FORMAT_R16G16_SNORM",
    79: "VK_FORMAT_R16G16_USCALED",
    80: "VK_FORMAT_R16G16_SSCALED",
    81: "VK_FORMAT_R16G16_UINT",
    82: "VK_FORMAT_R16G16_SINT",
    83: "VK_FORMAT_R16G16_SFLOAT",
    84: "VK_FORMAT_R16G16B16_UNORM",
    85: "VK_FORMAT_R16G16B16_SNORM",
    86: "VK_FORMAT_R16G16B16_USCALED",
    87: "VK_FORMAT_R16G16B16_SSCALED",
    88: "VK_FORMAT_R16G16B16_UINT",
    89: "VK_FORMAT_R16G16B16_SINT",
    90: "VK_FORMAT_R16G16B16_SFLOAT",
    91: "VK_FORMAT_R16G16B16A16_UNORM",
    92: "VK_FORMAT_R16G16B16A16_SNORM",
    93: "VK_FORMAT_R16G16B16A16_USCALED",
    94: "VK_FORMAT_R16G16B16A16_SSCALED",
    95: "VK_FORMAT_R16G16B16A16_UINT",
    96: "VK_FORMAT_R16G16B16A16_SINT",
    97: "VK_FORMAT_R16G16B16A16_SFLOAT",
    98: "VK_FORMAT_R32_UINT",
    99: "VK_FORMAT_R32_SINT",
    100: "VK_FORMAT_R32_SFLOAT",
    101: "VK_FORMAT_R32G32_UINT",
    102: "VK_FORMAT_R32G32_SINT",
    103: "VK_FORMAT_R32G32_SFLOAT",
    104: "VK_FORMAT_R32G32B32_UINT",
    105: "VK_FORMAT_R32G32B32_SINT",
    106: "VK_FORMAT_R32G32B32_SFLOAT",
    107: "VK_FORMAT_R32G32B32A32_UINT",
    108: "VK_FORMAT_R32G32B32A32_SINT",
    109: "VK_FORMAT_R32G32B32A32_SFLOAT",
    110: "VK_FORMAT_R64_UINT",
    111: "VK_FORMAT_R64_SINT",
    112: "VK_FORMAT_R64_SFLOAT",
    113: "VK_FORMAT_R64G64_UINT",
    114: "VK_FORMAT_R64G64_SINT",
    115: "VK_FORMAT_R64G64_SFLOAT",
    116: "VK_FORMAT_R64G64B64_UINT",
    117: "VK_FORMAT_R64G64B64_SINT",
    118: "VK_FORMAT_R64G64B64_SFLOAT",
    119: "VK_FORMAT_R64G64B64A64_UINT",
    120: "VK_FORMAT_R64G64B64A64_SINT",
    121: "VK_FORMAT_R64G64B64A64_SFLOAT",
    122: "VK_FORMAT_B10G11R11_UFLOAT_PACK32",
    123: "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32",
    124: "VK_FORMAT_D16_UNORM",
    125: "VK_FORMAT_X8_D24_UNORM_PACK32",
    126: "VK_FORMAT_D32_SFLOAT",
    127: "VK_FORMAT_S8_UINT",
    128: "VK_FORMAT_D16_UNORM_S8_UINT",
    129: "VK_FORMAT_D24_UNORM_S8_UINT",
    130: "VK_FORMAT_D32_SFLOAT_S8_UINT",
    131: "VK_FORMAT_BC1_RGB_UNORM_BLOCK",
    132: "VK_FORMAT_BC1_RGB_SRGB_BLOCK",
    133: "VK_FORMAT_BC1_RGBA_UNORM_BLOCK",
    134: "VK_FORMAT_BC1_RGBA_SRGB_BLOCK",
    135: "VK_FORMAT_BC2_UNORM_BLOCK",
    136: "VK_FORMAT_BC2_SRGB_BLOCK",
    137: "VK_FORMAT_BC3_UNORM_BLOCK",
    138: "VK_FORMAT_BC3_SRGB_BLOCK",
    139: "VK_FORMAT_BC4_UNORM_BLOCK",
    140: "VK_FORMAT_BC4_SNORM_BLOCK",
    141: "VK_FORMAT_BC5_UNORM_BLOCK",
    142: "VK_FORMAT_BC5_SNORM_BLOCK",
    143: "VK_FORMAT_BC6H_UFLOAT_BLOCK",
    144: "VK_FORMAT_BC6H_SFLOAT_BLOCK",
    145: "VK_FORMAT_BC7_UNORM_BLOCK",
    146: "VK_FORMAT_BC7_SRGB_BLOCK",
    147: "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK",
    148: "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK",
    149: "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK",
    150: "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK",
    151: "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK",
    152: "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK",
    153: "VK_FORMAT_EAC_R11_UNORM_BLOCK",
    154: "VK_FORMAT_EAC_R11_SNORM_BLOCK",
    155: "VK_FORMAT_EAC_R11G11_UNORM_BLOCK",
    156: "VK_FORMAT_EAC_R11G11_SNORM_BLOCK",
    157: "VK_FORMAT_ASTC_4x4_UNORM_BLOCK",
    158: "VK_FORMAT_ASTC_4x4_SRGB_BLOCK",
    159: "VK_FORMAT_ASTC_5x4_UNORM_BLOCK",
    160: "VK_FORMAT_ASTC_5x4_SRGB_BLOCK",
    161: "VK_FORMAT_ASTC_5x5_UNORM_BLOCK",
    162: "VK_FORMAT_ASTC_5x5_SRGB_BLOCK",
    163: "VK_FORMAT_ASTC_6x5_UNORM_BLOCK",
    164: "VK_FORMAT_ASTC_6x5_SRGB_BLOCK",
    165: "VK_FORMAT_ASTC_6x6_UNORM_BLOCK",
    166: "VK_FORMAT_ASTC_6x6_SRGB_BLOCK",
    167: "VK_FORMAT_ASTC_8x5_UNORM_BLOCK",
    168: "VK_FORMAT_ASTC_8x5_SRGB_BLOCK",
    169: "VK_FORMAT_ASTC_8x6_UNORM_BLOCK",
    170: "VK_FORMAT_ASTC_8x6_SRGB_BLOCK",
    171: "VK_FORMAT_ASTC_8x8_UNORM_BLOCK",
    172: "VK_FORMAT_ASTC_8x8_SRGB_BLOCK",
    173: "VK_FORMAT_ASTC_10x5_UNORM_BLOCK",
    174: "VK_FORMAT_ASTC_10x5_SRGB_BLOCK",
    175: "VK_FORMAT_ASTC_10x6_UNORM_BLOCK",
    176: "VK_FORMAT_ASTC_10x6_SRGB_BLOCK",
    177: "VK_FORMAT_ASTC_10x8_UNORM_BLOCK",
    178: "VK_FORMAT_ASTC_10x8_SRGB_BLOCK",
    179: "VK_FORMAT_ASTC_10x10_UNORM_BLOCK",
    180: "VK_FORMAT_ASTC_10x10_SRGB_BLOCK",
    181: "VK_FORMAT_ASTC_12x10_UNORM_BLOCK",
    182: "VK_FORMAT_ASTC_12x10_SRGB_BLOCK",
    183: "VK_FORMAT_ASTC_12x12_UNORM_BLOCK",
    184: "VK_FORMAT_ASTC_12x12_SRGB_BLOCK",
    1000156000: "VK_FORMAT_G8B8G8R8_422_UNORM",
    1000156001: "VK_FORMAT_B8G8R8G8_422_UNORM",
    1000156002: "VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM",
    1000156003: "VK_FORMAT_G8_B8R8_2PLANE_420_UNORM",
    1000156004: "VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM",
    1000156005: "VK_FORMAT_G8_B8R8_2PLANE_422_UNORM",
    1000156006: "VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM",
    1000156007: "VK_FORMAT_R10X6_UNORM_PACK16",
    1000156008: "VK_FORMAT_R10X6G10X6_UNORM_2PACK16",
    1000156009: "VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16",
    1000156010: "VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16",
    1000156011: "VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16",
    1000156012: "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16",
    1000156013: "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16",
    1000156014: "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16",
    1000156015: "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16",
    1000156016: "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16",
    1000156017: "VK_FORMAT_R12X4_UNORM_PACK16",
    1000156018: "VK_FORMAT_R12X4G12X4_UNORM_2PACK16",
    1000156019: "VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16",
    1000156020: "VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16",
    1000156021: "VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16",
    1000156022: "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16",
    1000156023: "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16",
    1000156024: "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16",
    1000156025: "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16",
    1000156026: "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16",
    1000156027: "VK_FORMAT_G16B16G16R16_422_UNORM",
    1000156028: "VK_FORMAT_B16G16R16G16_422_UNORM",
    1000156029: "VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM",
    1000156030: "VK_FORMAT_G16_B16R16_2PLANE_420_UNORM",
    1000156031: "VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM",
    1000156032: "VK_FORMAT_G16_B16R16_2PLANE_422_UNORM",
    1000156033: "VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM",
    1000330000: "VK_FORMAT_G8_B8R8_2PLANE_444_UNORM",
    1000330001: "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16",
    1000330002: "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16",
    1000330003: "VK_FORMAT_G16_B16R16_2PLANE_444_UNORM",
    1000340000: "VK_FORMAT_A4R4G4B4_UNORM_PACK16",
    1000340001: "VK_FORMAT_A4B4G4R4_UNORM_PACK16",
    1000066000: "VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK",
    1000066001: "VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK",
    1000066002: "VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK",
    1000066003: "VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK",
    1000066004: "VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK",
    1000066005: "VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK",
    1000066006: "VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK",
    1000066007: "VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK",
    1000066008: "VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK",
    1000066009: "VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK",
    1000066010: "VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK",
    1000066011: "VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK",
    1000066012: "VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK",
    1000066013: "VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK",
    1000054000: "VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG",
    1000054001: "VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG",
    1000054002: "VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG",
    1000054003: "VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG",
    1000054004: "VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG",
    1000054005: "VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG",
    1000054006: "VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG",
    1000054007: "VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG",
    1000288000: "VK_FORMAT_ASTC_3x3x3_UNORM_BLOCK_EXT",
    1000288001: "VK_FORMAT_ASTC_3x3x3_SRGB_BLOCK_EXT",
    1000288002: "VK_FORMAT_ASTC_3x3x3_SFLOAT_BLOCK_EXT",
    1000288003: "VK_FORMAT_ASTC_4x3x3_UNORM_BLOCK_EXT",
    1000288004: "VK_FORMAT_ASTC_4x3x3_SRGB_BLOCK_EXT",
    1000288005: "VK_FORMAT_ASTC_4x3x3_SFLOAT_BLOCK_EXT",
    1000288006: "VK_FORMAT_ASTC_4x4x3_UNORM_BLOCK_EXT",
    1000288007: "VK_FORMAT_ASTC_4x4x3_SRGB_BLOCK_EXT",
    1000288008: "VK_FORMAT_ASTC_4x4x3_SFLOAT_BLOCK_EXT",
    1000288009: "VK_FORMAT_ASTC_4x4x4_UNORM_BLOCK_EXT",
    1000288010: "VK_FORMAT_ASTC_4x4x4_SRGB_BLOCK_EXT",
    1000288011: "VK_FORMAT_ASTC_4x4x4_SFLOAT_BLOCK_EXT",
    1000288012: "VK_FORMAT_ASTC_5x4x4_UNORM_BLOCK_EXT",
    1000288013: "VK_FORMAT_ASTC_5x4x4_SRGB_BLOCK_EXT",
    1000288014: "VK_FORMAT_ASTC_5x4x4_SFLOAT_BLOCK_EXT",
    1000288015: "VK_FORMAT_ASTC_5x5x4_UNORM_BLOCK_EXT",
    1000288016: "VK_FORMAT_ASTC_5x5x4_SRGB_BLOCK_EXT",
    1000288017: "VK_FORMAT_ASTC_5x5x4_SFLOAT_BLOCK_EXT",
    1000288018: "VK_FORMAT_ASTC_5x5x5_UNORM_BLOCK_EXT",
    1000288019: "VK_FORMAT_ASTC_5x5x5_SRGB_BLOCK_EXT",
    1000288020: "VK_FORMAT_ASTC_5x5x5_SFLOAT_BLOCK_EXT",
    1000288021: "VK_FORMAT_ASTC_6x5x5_UNORM_BLOCK_EXT",
    1000288022: "VK_FORMAT_ASTC_6x5x5_SRGB_BLOCK_EXT",
    1000288023: "VK_FORMAT_ASTC_6x5x5_SFLOAT_BLOCK_EXT",
    1000288024: "VK_FORMAT_ASTC_6x6x5_UNORM_BLOCK_EXT",
    1000288025: "VK_FORMAT_ASTC_6x6x5_SRGB_BLOCK_EXT",
    1000288026: "VK_FORMAT_ASTC_6x6x5_SFLOAT_BLOCK_EXT",
    1000288027: "VK_FORMAT_ASTC_6x6x6_UNORM_BLOCK_EXT",
    1000288028: "VK_FORMAT_ASTC_6x6x6_SRGB_BLOCK_EXT",
    1000288029: "VK_FORMAT_ASTC_6x6x6_SFLOAT_BLOCK_EXT",
    1000464000: "VK_FORMAT_R16G16_S10_5_NV",
    1000470000: "VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR",
    1000470001: "VK_FORMAT_A8_UNORM_KHR",
}

VkFrontFace = {
    0: "VK_FRONT_FACE_COUNTER_CLOCKWISE",
    1: "VK_FRONT_FACE_CLOCKWISE",
}

VkImageLayout = {
    0: "VK_IMAGE_LAYOUT_UNDEFINED",
    1: "VK_IMAGE_LAYOUT_GENERAL",
    2: "VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL",
    3: "VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL",
    4: "VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL",
    5: "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL",
    6: "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
    7: "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL",
    8: "VK_IMAGE_LAYOUT_PREINITIALIZED",
    1000117000: "VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL",
    1000117001: "VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL",
    1000241000: "VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL",
    1000241001: "VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL",
    1000241002: "VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL",
    1000241003: "VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL",
    1000314000: "VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL",
    1000314001: "VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL",
    1000001002: "VK_IMAGE_LAYOUT_PRESENT_SRC_KHR",
    1000024000: "VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR",
    1000024001: "VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR",
    1000024002: "VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR",
    1000111000: "VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR",
    1000218000: "VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT",
    1000164003: "VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR",
    1000299000: "VK_IMAGE_LAYOUT_VIDEO_ENCODE_DST_KHR",
    1000299001: "VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR",
    1000299002: "VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR",
    1000339000: "VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT",
}

VkImageTiling = {
    0: "VK_IMAGE_TILING_OPTIMAL",
    1: "VK_IMAGE_TILING_LINEAR",
    1000158000: "VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT",
}

VkImageType = {
    0: "VK_IMAGE_TYPE_1D",
    1: "VK_IMAGE_TYPE_2D",
    2: "VK_IMAGE_TYPE_3D",
}

VkImageViewType = {
    0: "VK_IMAGE_VIEW_TYPE_1D",
    1: "VK_IMAGE_VIEW_TYPE_2D",
    2: "VK_IMAGE_VIEW_TYPE_3D",
    3: "VK_IMAGE_VIEW_TYPE_CUBE",
    4: "VK_IMAGE_VIEW_TYPE_1D_ARRAY",
    5: "VK_IMAGE_VIEW_TYPE_2D_ARRAY",
    6: "VK_IMAGE_VIEW_TYPE_CUBE_ARRAY",
}

VkIndexType = {
    0: "VK_INDEX_TYPE_UINT16",
    1: "VK_INDEX_TYPE_UINT32",
    1000165000: "VK_INDEX_TYPE_NONE_KHR",
    1000265000: "VK_INDEX_TYPE_UINT8_EXT",
}

VkLogicOp = {
    0: "VK_LOGIC_OP_CLEAR",
    1: "VK_LOGIC_OP_AND",
    2: "VK_LOGIC_OP_AND_REVERSE",
    3: "VK_LOGIC_OP_COPY",
    4: "VK_LOGIC_OP_AND_INVERTED",
    5: "VK_LOGIC_OP_NO_OP",
    6: "VK_LOGIC_OP_XOR",
    7: "VK_LOGIC_OP_OR",
    8: "VK_LOGIC_OP_NOR",
    9: "VK_LOGIC_OP_EQUIVALENT",
    10: "VK_LOGIC_OP_INVERT",
    11: "VK_LOGIC_OP_OR_REVERSE",
    12: "VK_LOGIC_OP_COPY_INVERTED",
    13: "VK_LOGIC_OP_OR_INVERTED",
    14: "VK_LOGIC_OP_NAND",
    15: "VK_LOGIC_OP_SET",
}

VkPhysicalDeviceType = {
    0: "VK_PHYSICAL_DEVICE_TYPE_OTHER",
    1: "VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU",
    2: "VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU",
    3: "VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU",
    4: "VK_PHYSICAL_DEVICE_TYPE_CPU",
}

VkPipelineBindPoint = {
    0: "VK_PIPELINE_BIND_POINT_GRAPHICS",
    1: "VK_PIPELINE_BIND_POINT_COMPUTE",
    1000134000: "VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX",
    1000165000: "VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR",
    1000369003: "VK_PIPELINE_BIND_POINT_SUBPASS_SHADING_HUAWEI",
}

VkPolygonMode = {
    0: "VK_POLYGON_MODE_FILL",
    1: "VK_POLYGON_MODE_LINE",
    2: "VK_POLYGON_MODE_POINT",
    1000153000: "VK_POLYGON_MODE_FILL_RECTANGLE_NV",
}

VkPrimitiveTopology = {
    0: "VK_PRIMITIVE_TOPOLOGY_POINT_LIST",
    1: "VK_PRIMITIVE_TOPOLOGY_LINE_LIST",
    2: "VK_PRIMITIVE_TOPOLOGY_LINE_STRIP",
    3: "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST",
    4: "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP",
    5: "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN",
    6: "VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY",
    7: "VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY",
    8: "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY",
    9: "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY",
    10: "VK_PRIMITIVE_TOPOLOGY_PATCH_LIST",
}

VkSampleCountFlagBits = {
    1: "VK_SAMPLE_COUNT_1_BIT",
    2: "VK_SAMPLE_COUNT_2_BIT",
    4: "VK_SAMPLE_COUNT_4_BIT",
    8: "VK_SAMPLE_COUNT_8_BIT",
    16: "VK_SAMPLE_COUNT_16_BIT",
    32: "VK_SAMPLE_COUNT_32_BIT",
    64: "VK_SAMPLE_COUNT_64_BIT",
}

VkSamplerAddressMode = {
    0: "VK_SAMPLER_ADDRESS_MODE_REPEAT",
    1: "VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT",
    2: "VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE",
    3: "VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER",
    4: "VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE",
    4: "VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE",
}

VkSamplerMipmapMode = {
    0: "VK_SAMPLER_MIPMAP_MODE_NEAREST",
    1: "VK_SAMPLER_MIPMAP_MODE_LINEAR",
}

VkShaderStageFlagBits = {
    1: "VK_SHADER_STAGE_VERTEX_BIT",
    2: "VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT",
    4: "VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT",
    8: "VK_SHADER_STAGE_GEOMETRY_BIT",
    16: "VK_SHADER_STAGE_FRAGMENT_BIT",
    32: "VK_SHADER_STAGE_COMPUTE_BIT",
    31: "VK_SHADER_STAGE_ALL_GRAPHICS",
    2147483647: "VK_SHADER_STAGE_ALL",
    256: "VK_SHADER_STAGE_RAYGEN_BIT_KHR",
    512: "VK_SHADER_STAGE_ANY_HIT_BIT_KHR",
    1024: "VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR",
    2048: "VK_SHADER_STAGE_MISS_BIT_KHR",
    4096: "VK_SHADER_STAGE_INTERSECTION_BIT_KHR",
    8192: "VK_SHADER_STAGE_CALLABLE_BIT_KHR",
    64: "VK_SHADER_STAGE_TASK_BIT_EXT",
    128: "VK_SHADER_STAGE_MESH_BIT_EXT",
    16384: "VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI",
    524288: "VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI",
}

VkSharingMode = {
    0: "VK_SHARING_MODE_EXCLUSIVE",
    1: "VK_SHARING_MODE_CONCURRENT",
}

VkStencilOp = {
    0: "VK_STENCIL_OP_KEEP",
    1: "VK_STENCIL_OP_ZERO",
    2: "VK_STENCIL_OP_REPLACE",
    3: "VK_STENCIL_OP_INCREMENT_AND_CLAMP",
    4: "VK_STENCIL_OP_DECREMENT_AND_CLAMP",
    5: "VK_STENCIL_OP_INVERT",
    6: "VK_STENCIL_OP_INCREMENT_AND_WRAP",
    7: "VK_STENCIL_OP_DECREMENT_AND_WRAP",
}

VkStructureType = {
    0: "VK_STRUCTURE_TYPE_APPLICATION_INFO",
    1: "VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO",
    2: "VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO",
    3: "VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO",
    4: "VK_STRUCTURE_TYPE_SUBMIT_INFO",
    5: "VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO",
    6: "VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE",
    7: "VK_STRUCTURE_TYPE_BIND_SPARSE_INFO",
    8: "VK_STRUCTURE_TYPE_FENCE_CREATE_INFO",
    9: "VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO",
    10: "VK_STRUCTURE_TYPE_EVENT_CREATE_INFO",
    11: "VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO",
    12: "VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO",
    13: "VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO",
    14: "VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO",
    15: "VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO",
    16: "VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO",
    17: "VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO",
    18: "VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO",
    19: "VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO",
    20: "VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO",
    21: "VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO",
    22: "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO",
    23: "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO",
    24: "VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO",
    25: "VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO",
    26: "VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO",
    27: "VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO",
    28: "VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO",
    29: "VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO",
    30: "VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO",
    31: "VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO",
    32: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO",
    33: "VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO",
    34: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO",
    35: "VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET",
    36: "VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET",
    37: "VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO",
    38: "VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO",
    39: "VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO",
    40: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO",
    41: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO",
    42: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO",
    43: "VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO",
    44: "VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER",
    45: "VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER",
    46: "VK_STRUCTURE_TYPE_MEMORY_BARRIER",
    47: "VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO",
    48: "VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO",
    1000094000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES",
    1000157000: "VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO",
    1000157001: "VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO",
    1000083000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES",
    1000127000: "VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS",
    1000127001: "VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO",
    1000060000: "VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO",
    1000060003: "VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO",
    1000060004: "VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO",
    1000060005: "VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO",
    1000060006: "VK_STRUCTURE_TYPE_DEVICE_GROUP_BIND_SPARSE_INFO",
    1000060013: "VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO",
    1000060014: "VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO",
    1000070000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES",
    1000070001: "VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO",
    1000146000: "VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2",
    1000146001: "VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2",
    1000146002: "VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2",
    1000146003: "VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2",
    1000146004: "VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2",
    1000059000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2",
    1000059001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2",
    1000059002: "VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2",
    1000059003: "VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2",
    1000059004: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2",
    1000059005: "VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2",
    1000059006: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2",
    1000059007: "VK_STRUCTURE_TYPE_SPARSE_IMAGE_FORMAT_PROPERTIES_2",
    1000059008: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_INFO_2",
    1000117000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES",
    1000117001: "VK_STRUCTURE_TYPE_RENDER_PASS_INPUT_ATTACHMENT_ASPECT_CREATE_INFO",
    1000117002: "VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO",
    1000117003: "VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO",
    1000053000: "VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO",
    1000053001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES",
    1000053002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES",
    1000120000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES",
    1000145000: "VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO",
    1000145001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES",
    1000145002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES",
    1000145003: "VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2",
    1000156000: "VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO",
    1000156001: "VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO",
    1000156002: "VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO",
    1000156003: "VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO",
    1000156004: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES",
    1000156005: "VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES",
    1000085000: "VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO",
    1000071000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO",
    1000071001: "VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES",
    1000071002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO",
    1000071003: "VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES",
    1000071004: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES",
    1000072000: "VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO",
    1000072001: "VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO",
    1000072002: "VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO",
    1000112000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO",
    1000112001: "VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES",
    1000113000: "VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO",
    1000077000: "VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO",
    1000076000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO",
    1000076001: "VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES",
    1000168000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES",
    1000168001: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT",
    1000063000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES",
    49: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES",
    50: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES",
    51: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES",
    52: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES",
    1000147000: "VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO",
    1000109000: "VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2",
    1000109001: "VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2",
    1000109002: "VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2",
    1000109003: "VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2",
    1000109004: "VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2",
    1000109005: "VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO",
    1000109006: "VK_STRUCTURE_TYPE_SUBPASS_END_INFO",
    1000177000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES",
    1000196000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES",
    1000180000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES",
    1000082000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES",
    1000197000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES",
    1000161000: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO",
    1000161001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES",
    1000161002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES",
    1000161003: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO",
    1000161004: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT",
    1000199000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES",
    1000199001: "VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE",
    1000221000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES",
    1000246000: "VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO",
    1000130000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES",
    1000130001: "VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO",
    1000211000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES",
    1000108000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES",
    1000108001: "VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO",
    1000108002: "VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO",
    1000108003: "VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO",
    1000253000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES",
    1000175000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES",
    1000241000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES",
    1000241001: "VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_STENCIL_LAYOUT",
    1000241002: "VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_STENCIL_LAYOUT",
    1000261000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES",
    1000207000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES",
    1000207001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES",
    1000207002: "VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO",
    1000207003: "VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO",
    1000207004: "VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO",
    1000207005: "VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO",
    1000257000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES",
    1000244001: "VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO",
    1000257002: "VK_STRUCTURE_TYPE_BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO",
    1000257003: "VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO",
    1000257004: "VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO",
    53: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES",
    54: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES",
    1000192000: "VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO",
    1000215000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES",
    1000245000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES",
    1000276000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES",
    1000295000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES",
    1000295001: "VK_STRUCTURE_TYPE_DEVICE_PRIVATE_DATA_CREATE_INFO",
    1000295002: "VK_STRUCTURE_TYPE_PRIVATE_DATA_SLOT_CREATE_INFO",
    1000297000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES",
    1000314000: "VK_STRUCTURE_TYPE_MEMORY_BARRIER_2",
    1000314001: "VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2",
    1000314002: "VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2",
    1000314003: "VK_STRUCTURE_TYPE_DEPENDENCY_INFO",
    1000314004: "VK_STRUCTURE_TYPE_SUBMIT_INFO_2",
    1000314005: "VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO",
    1000314006: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO",
    1000314007: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES",
    1000325000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES",
    1000335000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES",
    1000337000: "VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2",
    1000337001: "VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2",
    1000337002: "VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2",
    1000337003: "VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2",
    1000337004: "VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2",
    1000337005: "VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2",
    1000337006: "VK_STRUCTURE_TYPE_BUFFER_COPY_2",
    1000337007: "VK_STRUCTURE_TYPE_IMAGE_COPY_2",
    1000337008: "VK_STRUCTURE_TYPE_IMAGE_BLIT_2",
    1000337009: "VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2",
    1000337010: "VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2",
    1000225000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES",
    1000225001: "VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO",
    1000225002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES",
    1000138000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES",
    1000138001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES",
    1000138002: "VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK",
    1000138003: "VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO",
    1000066000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES",
    1000044000: "VK_STRUCTURE_TYPE_RENDERING_INFO",
    1000044001: "VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO",
    1000044002: "VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO",
    1000044003: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES",
    1000044004: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO",
    1000280000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES",
    1000280001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES",
    1000281001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES",
    1000360000: "VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3",
    1000413000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES",
    1000413001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES",
    1000413002: "VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS",
    1000413003: "VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS",
    1000298000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_SC_1_0_FEATURES",
    1000298001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_SC_1_0_PROPERTIES",
    1000298002: "VK_STRUCTURE_TYPE_DEVICE_OBJECT_RESERVATION_CREATE_INFO",
    1000298003: "VK_STRUCTURE_TYPE_COMMAND_POOL_MEMORY_RESERVATION_CREATE_INFO",
    1000298004: "VK_STRUCTURE_TYPE_COMMAND_POOL_MEMORY_CONSUMPTION",
    1000298005: "VK_STRUCTURE_TYPE_PIPELINE_POOL_SIZE",
    1000298007: "VK_STRUCTURE_TYPE_FAULT_DATA",
    1000298008: "VK_STRUCTURE_TYPE_FAULT_CALLBACK_INFO",
    1000298010: "VK_STRUCTURE_TYPE_PIPELINE_OFFLINE_CREATE_INFO",
    1000001000: "VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR",
    1000001001: "VK_STRUCTURE_TYPE_PRESENT_INFO_KHR",
    1000060007: "VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_CAPABILITIES_KHR",
    1000060008: "VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR",
    1000060009: "VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR",
    1000060010: "VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR",
    1000060011: "VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR",
    1000060012: "VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR",
    1000002000: "VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR",
    1000002001: "VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR",
    1000003000: "VK_STRUCTURE_TYPE_DISPLAY_PRESENT_INFO_KHR",
    1000004000: "VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR",
    1000005000: "VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR",
    1000006000: "VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR",
    1000008000: "VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR",
    1000009000: "VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR",
    1000010000: "VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID",
    1000010001: "VK_STRUCTURE_TYPE_SWAPCHAIN_IMAGE_CREATE_INFO_ANDROID",
    1000010002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENTATION_PROPERTIES_ANDROID",
    1000011000: "VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT",
    1000018000: "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD",
    1000022000: "VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT",
    1000022001: "VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT",
    1000022002: "VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT",
    1000023000: "VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR",
    1000023001: "VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR",
    1000023002: "VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR",
    1000023003: "VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR",
    1000023004: "VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR",
    1000023005: "VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR",
    1000023006: "VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR",
    1000023007: "VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_UPDATE_INFO_KHR",
    1000023008: "VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR",
    1000023009: "VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR",
    1000023010: "VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR",
    1000023011: "VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR",
    1000023012: "VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR",
    1000023013: "VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR",
    1000023014: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR",
    1000023015: "VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR",
    1000023016: "VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR",
    1000024000: "VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR",
    1000024001: "VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR",
    1000024002: "VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR",
    1000026000: "VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_IMAGE_CREATE_INFO_NV",
    1000026001: "VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_BUFFER_CREATE_INFO_NV",
    1000026002: "VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_MEMORY_ALLOCATE_INFO_NV",
    1000028000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT",
    1000028001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT",
    1000028002: "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT",
    1000029000: "VK_STRUCTURE_TYPE_CU_MODULE_CREATE_INFO_NVX",
    1000029001: "VK_STRUCTURE_TYPE_CU_FUNCTION_CREATE_INFO_NVX",
    1000029002: "VK_STRUCTURE_TYPE_CU_LAUNCH_INFO_NVX",
    1000030000: "VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX",
    1000030001: "VK_STRUCTURE_TYPE_IMAGE_VIEW_ADDRESS_PROPERTIES_NVX",
    1000038000: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_EXT",
    1000038001: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_EXT",
    1000038002: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_EXT",
    1000038003: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_EXT",
    1000038004: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_DPB_SLOT_INFO_EXT",
    1000038005: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_EXT",
    1000038006: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_GOP_REMAINING_FRAME_INFO_EXT",
    1000038007: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_EXT",
    1000038008: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_EXT",
    1000038009: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_LAYER_INFO_EXT",
    1000038010: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_CREATE_INFO_EXT",
    1000038011: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_QUALITY_LEVEL_PROPERTIES_EXT",
    1000038012: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_EXT",
    1000038013: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_FEEDBACK_INFO_EXT",
    1000039000: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_CAPABILITIES_EXT",
    1000039001: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_EXT",
    1000039002: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_ADD_INFO_EXT",
    1000039003: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_EXT",
    1000039004: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_EXT",
    1000039005: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_EXT",
    1000039006: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_GOP_REMAINING_FRAME_INFO_EXT",
    1000039007: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_EXT",
    1000039009: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_INFO_EXT",
    1000039010: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_LAYER_INFO_EXT",
    1000039011: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_CREATE_INFO_EXT",
    1000039012: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_EXT",
    1000039013: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_EXT",
    1000039014: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_FEEDBACK_INFO_EXT",
    1000040000: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR",
    1000040001: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR",
    1000040003: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR",
    1000040004: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR",
    1000040005: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR",
    1000040006: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR",
    1000041000: "VK_STRUCTURE_TYPE_TEXTURE_LOD_GATHER_FORMAT_PROPERTIES_AMD",
    1000044006: "VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR",
    1000044007: "VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_DENSITY_MAP_ATTACHMENT_INFO_EXT",
    1000044008: "VK_STRUCTURE_TYPE_ATTACHMENT_SAMPLE_COUNT_INFO_AMD",
    1000044009: "VK_STRUCTURE_TYPE_MULTIVIEW_PER_VIEW_ATTRIBUTES_INFO_NVX",
    1000049000: "VK_STRUCTURE_TYPE_STREAM_DESCRIPTOR_SURFACE_CREATE_INFO_GGP",
    1000050000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CORNER_SAMPLED_IMAGE_FEATURES_NV",
    1000051000: "VK_STRUCTURE_TYPE_PRIVATE_VENDOR_INFO_PLACEHOLDER_OFFSET_0_NV",
    1000056000: "VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_NV",
    1000056001: "VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_NV",
    1000057000: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_NV",
    1000057001: "VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_NV",
    1000058000: "VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_NV",
    1000060007: "VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_CAPABILITIES_KHR",
    1000060008: "VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR",
    1000060009: "VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR",
    1000060010: "VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR",
    1000060011: "VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR",
    1000060012: "VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR",
    1000061000: "VK_STRUCTURE_TYPE_VALIDATION_FLAGS_EXT",
    1000062000: "VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN",
    1000067000: "VK_STRUCTURE_TYPE_IMAGE_VIEW_ASTC_DECODE_MODE_EXT",
    1000067001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT",
    1000068000: "VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO_EXT",
    1000068001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT",
    1000068002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_PROPERTIES_EXT",
    1000073000: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR",
    1000073001: "VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR",
    1000073002: "VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR",
    1000073003: "VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR",
    1000074000: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR",
    1000074001: "VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR",
    1000074002: "VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR",
    1000075000: "VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR",
    1000078000: "VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR",
    1000078001: "VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR",
    1000078002: "VK_STRUCTURE_TYPE_D3D12_FENCE_SUBMIT_INFO_KHR",
    1000078003: "VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR",
    1000079000: "VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR",
    1000079001: "VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR",
    1000080000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR",
    1000081000: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_CONDITIONAL_RENDERING_INFO_EXT",
    1000081001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT",
    1000081002: "VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT",
    1000084000: "VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR",
    1000087000: "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_W_SCALING_STATE_CREATE_INFO_NV",
    1000090000: "VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_EXT",
    1000091000: "VK_STRUCTURE_TYPE_DISPLAY_POWER_INFO_EXT",
    1000091001: "VK_STRUCTURE_TYPE_DEVICE_EVENT_INFO_EXT",
    1000091002: "VK_STRUCTURE_TYPE_DISPLAY_EVENT_INFO_EXT",
    1000091003: "VK_STRUCTURE_TYPE_SWAPCHAIN_COUNTER_CREATE_INFO_EXT",
    1000092000: "VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE",
    1000097000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_ATTRIBUTES_PROPERTIES_NVX",
    1000098000: "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SWIZZLE_STATE_CREATE_INFO_NV",
    1000099000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISCARD_RECTANGLE_PROPERTIES_EXT",
    1000099001: "VK_STRUCTURE_TYPE_PIPELINE_DISCARD_RECTANGLE_STATE_CREATE_INFO_EXT",
    1000101000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT",
    1000101001: "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT",
    1000102000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT",
    1000102001: "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT",
    1000105000: "VK_STRUCTURE_TYPE_HDR_METADATA_EXT",
    1000111000: "VK_STRUCTURE_TYPE_SHARED_PRESENT_SURFACE_CAPABILITIES_KHR",
    1000114000: "VK_STRUCTURE_TYPE_IMPORT_FENCE_WIN32_HANDLE_INFO_KHR",
    1000114001: "VK_STRUCTURE_TYPE_EXPORT_FENCE_WIN32_HANDLE_INFO_KHR",
    1000114002: "VK_STRUCTURE_TYPE_FENCE_GET_WIN32_HANDLE_INFO_KHR",
    1000115000: "VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR",
    1000115001: "VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR",
    1000116000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR",
    1000116001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR",
    1000116002: "VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR",
    1000116003: "VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_SUBMIT_INFO_KHR",
    1000116004: "VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR",
    1000116005: "VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR",
    1000116006: "VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR",
    1000116007: "VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_RESERVATION_INFO_KHR",
    1000119000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR",
    1000119001: "VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR",
    1000119002: "VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR",
    1000121000: "VK_STRUCTURE_TYPE_DISPLAY_PROPERTIES_2_KHR",
    1000121001: "VK_STRUCTURE_TYPE_DISPLAY_PLANE_PROPERTIES_2_KHR",
    1000121002: "VK_STRUCTURE_TYPE_DISPLAY_MODE_PROPERTIES_2_KHR",
    1000121003: "VK_STRUCTURE_TYPE_DISPLAY_PLANE_INFO_2_KHR",
    1000121004: "VK_STRUCTURE_TYPE_DISPLAY_PLANE_CAPABILITIES_2_KHR",
    1000122000: "VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK",
    1000123000: "VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK",
    1000128000: "VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT",
    1000128001: "VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_TAG_INFO_EXT",
    1000128002: "VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT",
    1000128003: "VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT",
    1000128004: "VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT",
    1000129000: "VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID",
    1000129001: "VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID",
    1000129002: "VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID",
    1000129003: "VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID",
    1000129004: "VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID",
    1000129005: "VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID",
    1000129006: "VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID",
    1000134000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ENQUEUE_FEATURES_AMDX",
    1000134001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ENQUEUE_PROPERTIES_AMDX",
    1000134002: "VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_SCRATCH_SIZE_AMDX",
    1000134003: "VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_CREATE_INFO_AMDX",
    1000134004: "VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NODE_CREATE_INFO_AMDX",
    1000143000: "VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT",
    1000143001: "VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT",
    1000143002: "VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT",
    1000143003: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT",
    1000143004: "VK_STRUCTURE_TYPE_MULTISAMPLE_PROPERTIES_EXT",
    1000148000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT",
    1000148001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_PROPERTIES_EXT",
    1000148002: "VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_ADVANCED_STATE_CREATE_INFO_EXT",
    1000149000: "VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_TO_COLOR_STATE_CREATE_INFO_NV",
    1000150007: "VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR",
    1000150000: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR",
    1000150002: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR",
    1000150003: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR",
    1000150004: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR",
    1000150005: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR",
    1000150006: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR",
    1000150009: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_VERSION_INFO_KHR",
    1000150010: "VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR",
    1000150011: "VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_TO_MEMORY_INFO_KHR",
    1000150012: "VK_STRUCTURE_TYPE_COPY_MEMORY_TO_ACCELERATION_STRUCTURE_INFO_KHR",
    1000150013: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR",
    1000150014: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR",
    1000150017: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR",
    1000150020: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR",
    1000347000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR",
    1000347001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR",
    1000150015: "VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR",
    1000150016: "VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR",
    1000150018: "VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR",
    1000348013: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR",
    1000152000: "VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_MODULATION_STATE_CREATE_INFO_NV",
    1000154000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_FEATURES_NV",
    1000154001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV",
    1000158000: "VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT",
    1000158002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT",
    1000158003: "VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT",
    1000158004: "VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT",
    1000158005: "VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT",
    1000158006: "VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT",
    1000160000: "VK_STRUCTURE_TYPE_VALIDATION_CACHE_CREATE_INFO_EXT",
    1000160001: "VK_STRUCTURE_TYPE_SHADER_MODULE_VALIDATION_CACHE_CREATE_INFO_EXT",
    1000163000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR",
    1000163001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_PROPERTIES_KHR",
    1000164000: "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SHADING_RATE_IMAGE_STATE_CREATE_INFO_NV",
    1000164001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV",
    1000164002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV",
    1000164005: "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_COARSE_SAMPLE_ORDER_STATE_CREATE_INFO_NV",
    1000165000: "VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV",
    1000165001: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV",
    1000165003: "VK_STRUCTURE_TYPE_GEOMETRY_NV",
    1000165004: "VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV",
    1000165005: "VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV",
    1000165006: "VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV",
    1000165007: "VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV",
    1000165008: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV",
    1000165009: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV",
    1000165011: "VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV",
    1000165012: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV",
    1000166000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_REPRESENTATIVE_FRAGMENT_TEST_FEATURES_NV",
    1000166001: "VK_STRUCTURE_TYPE_PIPELINE_REPRESENTATIVE_FRAGMENT_TEST_STATE_CREATE_INFO_NV",
    1000170000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT",
    1000170001: "VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT",
    1000178000: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT",
    1000178001: "VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT",
    1000178002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT",
    1000181000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR",
    1000183000: "VK_STRUCTURE_TYPE_PIPELINE_COMPILER_CONTROL_CREATE_INFO_AMD",
    1000184000: "VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT",
    1000185000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD",
    1000187000: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_CAPABILITIES_KHR",
    1000187001: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR",
    1000187002: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR",
    1000187003: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR",
    1000187004: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PICTURE_INFO_KHR",
    1000187005: "VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR",
    1000174000: "VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR",
    1000388000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES_KHR",
    1000388001: "VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR",
    1000189000: "VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD",
    1000190000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT",
    1000190001: "VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT",
    1000190002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT",
    1000191000: "VK_STRUCTURE_TYPE_PRESENT_FRAME_TOKEN_GGP",
    1000201000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV",
    1000202000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV",
    1000202001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_NV",
    1000204000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_FOOTPRINT_FEATURES_NV",
    1000205000: "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_EXCLUSIVE_SCISSOR_STATE_CREATE_INFO_NV",
    1000205002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXCLUSIVE_SCISSOR_FEATURES_NV",
    1000206000: "VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV",
    1000206001: "VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_NV",
    1000209000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_FUNCTIONS_2_FEATURES_INTEL",
    1000210000: "VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_QUERY_CREATE_INFO_INTEL",
    1000210001: "VK_STRUCTURE_TYPE_INITIALIZE_PERFORMANCE_API_INFO_INTEL",
    1000210002: "VK_STRUCTURE_TYPE_PERFORMANCE_MARKER_INFO_INTEL",
    1000210003: "VK_STRUCTURE_TYPE_PERFORMANCE_STREAM_MARKER_INFO_INTEL",
    1000210004: "VK_STRUCTURE_TYPE_PERFORMANCE_OVERRIDE_INFO_INTEL",
    1000210005: "VK_STRUCTURE_TYPE_PERFORMANCE_CONFIGURATION_ACQUIRE_INFO_INTEL",
    1000212000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT",
    1000213000: "VK_STRUCTURE_TYPE_DISPLAY_NATIVE_HDR_SURFACE_CAPABILITIES_AMD",
    1000213001: "VK_STRUCTURE_TYPE_SWAPCHAIN_DISPLAY_NATIVE_HDR_CREATE_INFO_AMD",
    1000214000: "VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA",
    1000217000: "VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT",
    1000218000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT",
    1000218001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT",
    1000218002: "VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT",
    1000226000: "VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR",
    1000226001: "VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR",
    1000226002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR",
    1000226003: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR",
    1000226004: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR",
    1000227000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD",
    1000229000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD",
    1000234000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT",
    1000237000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT",
    1000238000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT",
    1000238001: "VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT",
    1000239000: "VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR",
    1000240000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEDICATED_ALLOCATION_IMAGE_ALIASING_FEATURES_NV",
    1000244000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT",
    1000244002: "VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_CREATE_INFO_EXT",
    1000247000: "VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT",
    1000248000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR",
    1000249000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_NV",
    1000249001: "VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_NV",
    1000249002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_NV",
    1000250000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COVERAGE_REDUCTION_MODE_FEATURES_NV",
    1000250001: "VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_REDUCTION_STATE_CREATE_INFO_NV",
    1000250002: "VK_STRUCTURE_TYPE_FRAMEBUFFER_MIXED_SAMPLES_COMBINATION_NV",
    1000251000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT",
    1000252000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT",
    1000254000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT",
    1000254001: "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT",
    1000254002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT",
    1000255000: "VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT",
    1000255002: "VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT",
    1000255001: "VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT",
    1000256000: "VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT",
    1000260000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT",
    1000265000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT",
    1000267000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT",
    1000269000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR",
    1000269001: "VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR",
    1000269002: "VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR",
    1000269003: "VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR",
    1000269004: "VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR",
    1000269005: "VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR",
    1000270000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT",
    1000270001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES_EXT",
    1000270002: "VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT",
    1000270003: "VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY_EXT",
    1000270004: "VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_EXT",
    1000270005: "VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT",
    1000270006: "VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT",
    1000270007: "VK_STRUCTURE_TYPE_COPY_IMAGE_TO_IMAGE_INFO_EXT",
    1000270008: "VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE_EXT",
    1000270009: "VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT",
    1000271000: "VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR",
    1000271001: "VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR",
    1000273000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT",
    1000274000: "VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT",
    1000274001: "VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT",
    1000274002: "VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT",
    1000275000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT",
    1000275001: "VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT",
    1000275002: "VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT",
    1000275003: "VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT",
    1000275004: "VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT",
    1000275005: "VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT",
    1000277000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_NV",
    1000277001: "VK_STRUCTURE_TYPE_GRAPHICS_SHADER_GROUP_CREATE_INFO_NV",
    1000277002: "VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_SHADER_GROUPS_CREATE_INFO_NV",
    1000277003: "VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV",
    1000277004: "VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NV",
    1000277005: "VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_NV",
    1000277006: "VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_NV",
    1000277007: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV",
    1000278000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INHERITED_VIEWPORT_SCISSOR_FEATURES_NV",
    1000278001: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_VIEWPORT_SCISSOR_INFO_NV",
    1000281000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT",
    1000282000: "VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDER_PASS_TRANSFORM_INFO_QCOM",
    1000282001: "VK_STRUCTURE_TYPE_RENDER_PASS_TRANSFORM_BEGIN_INFO_QCOM",
    1000283000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT",
    1000283001: "VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT",
    1000283002: "VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT",
    1000284000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT",
    1000284001: "VK_STRUCTURE_TYPE_DEVICE_DEVICE_MEMORY_REPORT_CREATE_INFO_EXT",
    1000284002: "VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT",
    1000286000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT",
    1000286001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT",
    1000287000: "VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT",
    1000287001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT",
    1000287002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT",
    1000290000: "VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR",
    1000292000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_BARRIER_FEATURES_NV",
    1000292001: "VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_BARRIER_NV",
    1000292002: "VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_BARRIER_CREATE_INFO_NV",
    1000294000: "VK_STRUCTURE_TYPE_PRESENT_ID_KHR",
    1000294001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR",
    1000299000: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR",
    1000299001: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_INFO_KHR",
    1000299002: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_LAYER_INFO_KHR",
    1000299003: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR",
    1000299004: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR",
    1000299005: "VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR",
    1000299006: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR",
    1000299007: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_PROPERTIES_KHR",
    1000299008: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR",
    1000299009: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR",
    1000299010: "VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR",
    1000300000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV",
    1000300001: "VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV",
    1000307000: "VK_STRUCTURE_TYPE_CUDA_MODULE_CREATE_INFO_NV",
    1000307001: "VK_STRUCTURE_TYPE_CUDA_FUNCTION_CREATE_INFO_NV",
    1000307002: "VK_STRUCTURE_TYPE_CUDA_LAUNCH_INFO_NV",
    1000307003: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUDA_KERNEL_LAUNCH_FEATURES_NV",
    1000307004: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUDA_KERNEL_LAUNCH_PROPERTIES_NV",
    1000308000: "VK_STRUCTURE_TYPE_REFRESH_OBJECT_LIST_KHR",
    1000310000: "VK_STRUCTURE_TYPE_QUERY_LOW_LATENCY_SUPPORT_NV",
    1000311000: "VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT",
    1000311001: "VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT",
    1000311002: "VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT",
    1000311003: "VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT",
    1000311004: "VK_STRUCTURE_TYPE_EXPORT_METAL_BUFFER_INFO_EXT",
    1000311005: "VK_STRUCTURE_TYPE_IMPORT_METAL_BUFFER_INFO_EXT",
    1000311006: "VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT",
    1000311007: "VK_STRUCTURE_TYPE_IMPORT_METAL_TEXTURE_INFO_EXT",
    1000311008: "VK_STRUCTURE_TYPE_EXPORT_METAL_IO_SURFACE_INFO_EXT",
    1000311009: "VK_STRUCTURE_TYPE_IMPORT_METAL_IO_SURFACE_INFO_EXT",
    1000311010: "VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT",
    1000311011: "VK_STRUCTURE_TYPE_IMPORT_METAL_SHARED_EVENT_INFO_EXT",
    1000314008: "VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_2_NV",
    1000314009: "VK_STRUCTURE_TYPE_CHECKPOINT_DATA_2_NV",
    1000316000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT",
    1000316001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_DENSITY_MAP_PROPERTIES_EXT",
    1000316002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT",
    1000316003: "VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT",
    1000316004: "VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT",
    1000316005: "VK_STRUCTURE_TYPE_BUFFER_CAPTURE_DESCRIPTOR_DATA_INFO_EXT",
    1000316006: "VK_STRUCTURE_TYPE_IMAGE_CAPTURE_DESCRIPTOR_DATA_INFO_EXT",
    1000316007: "VK_STRUCTURE_TYPE_IMAGE_VIEW_CAPTURE_DESCRIPTOR_DATA_INFO_EXT",
    1000316008: "VK_STRUCTURE_TYPE_SAMPLER_CAPTURE_DESCRIPTOR_DATA_INFO_EXT",
    1000316010: "VK_STRUCTURE_TYPE_OPAQUE_CAPTURE_DESCRIPTOR_DATA_CREATE_INFO_EXT",
    1000316011: "VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT",
    1000316012: "VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_PUSH_DESCRIPTOR_BUFFER_HANDLE_EXT",
    1000316009: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CAPTURE_DESCRIPTOR_DATA_INFO_EXT",
    1000320000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT",
    1000320001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT",
    1000320002: "VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT",
    1000321000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EARLY_AND_LATE_FRAGMENT_TESTS_FEATURES_AMD",
    1000203000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR",
    1000322000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_PROPERTIES_KHR",
    1000323000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR",
    1000326000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_ENUMS_PROPERTIES_NV",
    1000326001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_ENUMS_FEATURES_NV",
    1000326002: "VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_ENUM_STATE_CREATE_INFO_NV",
    1000327000: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_MOTION_TRIANGLES_DATA_NV",
    1000327001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MOTION_BLUR_FEATURES_NV",
    1000327002: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MOTION_INFO_NV",
    1000328000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT",
    1000328001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT",
    1000330000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_2_PLANE_444_FORMATS_FEATURES_EXT",
    1000332000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT",
    1000332001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_PROPERTIES_EXT",
    1000333000: "VK_STRUCTURE_TYPE_COPY_COMMAND_TRANSFORM_INFO_QCOM",
    1000336000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR",
    1000338000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT",
    1000338001: "VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT",
    1000338004: "VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT",
    1000339000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT",
    1000340000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT",
    1000341000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT",
    1000341001: "VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT",
    1000341002: "VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT",
    1000344000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RGBA10X6_FORMATS_FEATURES_EXT",
    1000346000: "VK_STRUCTURE_TYPE_DIRECTFB_SURFACE_CREATE_INFO_EXT",
    1000352000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT",
    1000352001: "VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT",
    1000352002: "VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT",
    1000353000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT",
    1000354000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT",
    1000354001: "VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT",
    1000355000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT",
    1000355001: "VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT",
    1000356000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT",
    1000364000: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_ZIRCON_HANDLE_INFO_FUCHSIA",
    1000364001: "VK_STRUCTURE_TYPE_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA",
    1000364002: "VK_STRUCTURE_TYPE_MEMORY_GET_ZIRCON_HANDLE_INFO_FUCHSIA",
    1000365000: "VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA",
    1000365001: "VK_STRUCTURE_TYPE_SEMAPHORE_GET_ZIRCON_HANDLE_INFO_FUCHSIA",
    1000366000: "VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA",
    1000366001: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA",
    1000366002: "VK_STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA",
    1000366003: "VK_STRUCTURE_TYPE_BUFFER_COLLECTION_PROPERTIES_FUCHSIA",
    1000366004: "VK_STRUCTURE_TYPE_BUFFER_CONSTRAINTS_INFO_FUCHSIA",
    1000366005: "VK_STRUCTURE_TYPE_BUFFER_COLLECTION_BUFFER_CREATE_INFO_FUCHSIA",
    1000366006: "VK_STRUCTURE_TYPE_IMAGE_CONSTRAINTS_INFO_FUCHSIA",
    1000366007: "VK_STRUCTURE_TYPE_IMAGE_FORMAT_CONSTRAINTS_INFO_FUCHSIA",
    1000366008: "VK_STRUCTURE_TYPE_SYSMEM_COLOR_SPACE_FUCHSIA",
    1000366009: "VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CONSTRAINTS_INFO_FUCHSIA",
    1000369000: "VK_STRUCTURE_TYPE_SUBPASS_SHADING_PIPELINE_CREATE_INFO_HUAWEI",
    1000369001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_SHADING_FEATURES_HUAWEI",
    1000369002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_SHADING_PROPERTIES_HUAWEI",
    1000370000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INVOCATION_MASK_FEATURES_HUAWEI",
    1000371000: "VK_STRUCTURE_TYPE_MEMORY_GET_REMOTE_ADDRESS_INFO_NV",
    1000371001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_RDMA_FEATURES_NV",
    1000372000: "VK_STRUCTURE_TYPE_PIPELINE_PROPERTIES_IDENTIFIER_EXT",
    1000372001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROPERTIES_FEATURES_EXT",
    1000373000: "VK_STRUCTURE_TYPE_IMPORT_FENCE_SCI_SYNC_INFO_NV",
    1000373001: "VK_STRUCTURE_TYPE_EXPORT_FENCE_SCI_SYNC_INFO_NV",
    1000373002: "VK_STRUCTURE_TYPE_FENCE_GET_SCI_SYNC_INFO_NV",
    1000373003: "VK_STRUCTURE_TYPE_SCI_SYNC_ATTRIBUTES_INFO_NV",
    1000373004: "VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_SCI_SYNC_INFO_NV",
    1000373005: "VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_SCI_SYNC_INFO_NV",
    1000373006: "VK_STRUCTURE_TYPE_SEMAPHORE_GET_SCI_SYNC_INFO_NV",
    1000373007: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SCI_SYNC_FEATURES_NV",
    1000374000: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_SCI_BUF_INFO_NV",
    1000374001: "VK_STRUCTURE_TYPE_EXPORT_MEMORY_SCI_BUF_INFO_NV",
    1000374002: "VK_STRUCTURE_TYPE_MEMORY_GET_SCI_BUF_INFO_NV",
    1000374003: "VK_STRUCTURE_TYPE_MEMORY_SCI_BUF_PROPERTIES_NV",
    1000374004: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_SCI_BUF_FEATURES_NV",
    1000375000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT",
    1000375001: "VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT",
    1000376000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT",
    1000376001: "VK_STRUCTURE_TYPE_SUBPASS_RESOLVE_PERFORMANCE_QUERY_EXT",
    1000376002: "VK_STRUCTURE_TYPE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_INFO_EXT",
    1000377000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT",
    1000378000: "VK_STRUCTURE_TYPE_SCREEN_SURFACE_CREATE_INFO_QNX",
    1000381000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT",
    1000381001: "VK_STRUCTURE_TYPE_PIPELINE_COLOR_WRITE_CREATE_INFO_EXT",
    1000382000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT",
    1000386000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR",
    1000391000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT",
    1000391001: "VK_STRUCTURE_TYPE_IMAGE_VIEW_MIN_LOD_CREATE_INFO_EXT",
    1000392000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT",
    1000392001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT",
    1000393000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT",
    1000395000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TILE_IMAGE_FEATURES_EXT",
    1000395001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TILE_IMAGE_PROPERTIES_EXT",
    1000396000: "VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT",
    1000396001: "VK_STRUCTURE_TYPE_MICROMAP_VERSION_INFO_EXT",
    1000396002: "VK_STRUCTURE_TYPE_COPY_MICROMAP_INFO_EXT",
    1000396003: "VK_STRUCTURE_TYPE_COPY_MICROMAP_TO_MEMORY_INFO_EXT",
    1000396004: "VK_STRUCTURE_TYPE_COPY_MEMORY_TO_MICROMAP_INFO_EXT",
    1000396005: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT",
    1000396006: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT",
    1000396007: "VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT",
    1000396008: "VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT",
    1000396009: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT",
    1000397000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISPLACEMENT_MICROMAP_FEATURES_NV",
    1000397001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISPLACEMENT_MICROMAP_PROPERTIES_NV",
    1000397002: "VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_DISPLACEMENT_MICROMAP_NV",
    1000404000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_FEATURES_HUAWEI",
    1000404001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_PROPERTIES_HUAWEI",
    1000411000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT",
    1000411001: "VK_STRUCTURE_TYPE_SAMPLER_BORDER_COLOR_COMPONENT_MAPPING_CREATE_INFO_EXT",
    1000412000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT",
    1000415000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_ARM",
    1000417000: "VK_STRUCTURE_TYPE_DEVICE_QUEUE_SHADER_CORE_CONTROL_CREATE_INFO_ARM",
    1000417001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_FEATURES_ARM",
    1000417002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_PROPERTIES_ARM",
    1000418000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT",
    1000418001: "VK_STRUCTURE_TYPE_IMAGE_VIEW_SLICED_CREATE_INFO_EXT",
    1000420000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE",
    1000420001: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_BINDING_REFERENCE_VALVE",
    1000420002: "VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_HOST_MAPPING_INFO_VALVE",
    1000421000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_ZERO_ONE_FEATURES_EXT",
    1000422000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT",
    1000425000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_QCOM",
    1000425001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_PROPERTIES_QCOM",
    1000425002: "VK_STRUCTURE_TYPE_SUBPASS_FRAGMENT_DENSITY_MAP_OFFSET_END_INFO_QCOM",
    1000426000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_FEATURES_NV",
    1000426001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_PROPERTIES_NV",
    1000427000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_FEATURES_NV",
    1000427001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_PROPERTIES_NV",
    1000428000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_COMPUTE_FEATURES_NV",
    1000428001: "VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_INDIRECT_BUFFER_INFO_NV",
    1000428002: "VK_STRUCTURE_TYPE_PIPELINE_INDIRECT_DEVICE_ADDRESS_INFO_NV",
    1000430000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINEAR_COLOR_ATTACHMENT_FEATURES_NV",
    1000435000: "VK_STRUCTURE_TYPE_APPLICATION_PARAMETERS_EXT",
    1000437000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT",
    1000440000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_FEATURES_QCOM",
    1000440001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_PROPERTIES_QCOM",
    1000440002: "VK_STRUCTURE_TYPE_IMAGE_VIEW_SAMPLE_WEIGHT_CREATE_INFO_QCOM",
    1000451000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT",
    1000451001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_PROPERTIES_EXT",
    1000453000: "VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_ACQUIRE_UNMODIFIED_EXT",
    1000455000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT",
    1000455001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT",
    1000458000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_MERGE_FEEDBACK_FEATURES_EXT",
    1000458001: "VK_STRUCTURE_TYPE_RENDER_PASS_CREATION_CONTROL_EXT",
    1000458002: "VK_STRUCTURE_TYPE_RENDER_PASS_CREATION_FEEDBACK_CREATE_INFO_EXT",
    1000458003: "VK_STRUCTURE_TYPE_RENDER_PASS_SUBPASS_FEEDBACK_CREATE_INFO_EXT",
    1000459000: "VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_INFO_LUNARG",
    1000459001: "VK_STRUCTURE_TYPE_DIRECT_DRIVER_LOADING_LIST_LUNARG",
    1000462000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT",
    1000462001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT",
    1000462002: "VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT",
    1000462003: "VK_STRUCTURE_TYPE_SHADER_MODULE_IDENTIFIER_EXT",
    1000342000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT",
    1000464000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV",
    1000464001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_PROPERTIES_NV",
    1000464002: "VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_INFO_NV",
    1000464003: "VK_STRUCTURE_TYPE_OPTICAL_FLOW_IMAGE_FORMAT_PROPERTIES_NV",
    1000464004: "VK_STRUCTURE_TYPE_OPTICAL_FLOW_SESSION_CREATE_INFO_NV",
    1000464005: "VK_STRUCTURE_TYPE_OPTICAL_FLOW_EXECUTE_INFO_NV",
    1000464010: "VK_STRUCTURE_TYPE_OPTICAL_FLOW_SESSION_CREATE_PRIVATE_DATA_INFO_NV",
    1000465000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_DITHERING_FEATURES_EXT",
    1000466000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROTECTED_ACCESS_FEATURES_EXT",
    1000468000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FORMAT_RESOLVE_FEATURES_ANDROID",
    1000468001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FORMAT_RESOLVE_PROPERTIES_ANDROID",
    1000468002: "VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_RESOLVE_PROPERTIES_ANDROID",
    1000470000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR",
    1000470001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES_KHR",
    1000470003: "VK_STRUCTURE_TYPE_RENDERING_AREA_INFO_KHR",
    1000470004: "VK_STRUCTURE_TYPE_DEVICE_IMAGE_SUBRESOURCE_INFO_KHR",
    1000338002: "VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_KHR",
    1000338003: "VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_KHR",
    1000470005: "VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR",
    1000470006: "VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR",
    1000481000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR",
    1000482000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT",
    1000482001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT",
    1000482002: "VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT",
    1000352001: "VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT",
    1000352002: "VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT",
    1000484000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TILE_PROPERTIES_FEATURES_QCOM",
    1000484001: "VK_STRUCTURE_TYPE_TILE_PROPERTIES_QCOM",
    1000485000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_AMIGO_PROFILING_FEATURES_SEC",
    1000485001: "VK_STRUCTURE_TYPE_AMIGO_PROFILING_SUBMIT_INFO_SEC",
    1000488000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_VIEWPORTS_FEATURES_QCOM",
    1000489000: "VK_STRUCTURE_TYPE_SEMAPHORE_SCI_SYNC_POOL_CREATE_INFO_NV",
    1000489001: "VK_STRUCTURE_TYPE_SEMAPHORE_SCI_SYNC_CREATE_INFO_NV",
    1000489002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SCI_SYNC_2_FEATURES_NV",
    1000373000: "VK_STRUCTURE_TYPE_IMPORT_FENCE_SCI_SYNC_INFO_NV",
    1000373001: "VK_STRUCTURE_TYPE_EXPORT_FENCE_SCI_SYNC_INFO_NV",
    1000373002: "VK_STRUCTURE_TYPE_FENCE_GET_SCI_SYNC_INFO_NV",
    1000373003: "VK_STRUCTURE_TYPE_SCI_SYNC_ATTRIBUTES_INFO_NV",
    1000489003: "VK_STRUCTURE_TYPE_DEVICE_SEMAPHORE_SCI_SYNC_POOL_RESERVATION_CREATE_INFO_NV",
    1000490000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV",
    1000490001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_NV",
    1000492000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_SPARSE_ADDRESS_SPACE_FEATURES_NV",
    1000492001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_SPARSE_ADDRESS_SPACE_PROPERTIES_NV",
    1000351000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT",
    1000351002: "VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT",
    1000497000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_FEATURES_ARM",
    1000497001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_PROPERTIES_ARM",
    1000498000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_EXT",
    1000499000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT",
    1000505000: "VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV",
    1000505001: "VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV",
    1000505002: "VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV",
    1000505003: "VK_STRUCTURE_TYPE_GET_LATENCY_MARKER_INFO_NV",
    1000505004: "VK_STRUCTURE_TYPE_LATENCY_TIMINGS_FRAME_REPORT_NV",
    1000505005: "VK_STRUCTURE_TYPE_LATENCY_SUBMISSION_PRESENT_ID_NV",
    1000505006: "VK_STRUCTURE_TYPE_OUT_OF_BAND_QUEUE_TYPE_INFO_NV",
    1000505007: "VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV",
    1000505008: "VK_STRUCTURE_TYPE_LATENCY_SURFACE_CAPABILITIES_NV",
    1000506000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR",
    1000506001: "VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR",
    1000506002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR",
    1000510000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_RENDER_AREAS_FEATURES_QCOM",
    1000510001: "VK_STRUCTURE_TYPE_MULTIVIEW_PER_VIEW_RENDER_AREAS_RENDER_PASS_BEGIN_INFO_QCOM",
    1000518000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_2_FEATURES_QCOM",
    1000518001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_2_PROPERTIES_QCOM",
    1000518002: "VK_STRUCTURE_TYPE_SAMPLER_BLOCK_MATCH_WINDOW_CREATE_INFO_QCOM",
    1000519000: "VK_STRUCTURE_TYPE_SAMPLER_CUBIC_WEIGHTS_CREATE_INFO_QCOM",
    1000519001: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_WEIGHTS_FEATURES_QCOM",
    1000519002: "VK_STRUCTURE_TYPE_BLIT_IMAGE_CUBIC_WEIGHTS_INFO_QCOM",
    1000520000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_DEGAMMA_FEATURES_QCOM",
    1000520001: "VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_YCBCR_DEGAMMA_CREATE_INFO_QCOM",
    1000521000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_CLAMP_FEATURES_QCOM",
    1000524000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT",
    1000529000: "VK_STRUCTURE_TYPE_SCREEN_BUFFER_PROPERTIES_QNX",
    1000529001: "VK_STRUCTURE_TYPE_SCREEN_BUFFER_FORMAT_PROPERTIES_QNX",
    1000529002: "VK_STRUCTURE_TYPE_IMPORT_SCREEN_BUFFER_INFO_QNX",
    1000529003: "VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_QNX",
    1000529004: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_SCREEN_BUFFER_FEATURES_QNX",
    1000530000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_DRIVER_PROPERTIES_MSFT",
    1000259000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_KHR",
    1000259001: "VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_KHR",
    1000259002: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_KHR",
    1000546000: "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_POOL_OVERALLOCATION_FEATURES_NV",
    1000601000: "VK_STRUCTURE_TYPE_IMPORT_MEMORY_METAL_HANDLE_INFO_EXT",
    1000601001: "VK_STRUCTURE_TYPE_MEMORY_METAL_HANDLE_PROPERTIES_EXT",
    1000601002: "VK_STRUCTURE_TYPE_MEMORY_GET_METAL_HANDLE_INFO_EXT",
    1000385000: "VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE",
    1000385001: "VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE",
    1000385002: "VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE",
}

VkSubpassContents = {
    0: "VK_SUBPASS_CONTENTS_INLINE",
    1: "VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS",
    1000451000: "VK_SUBPASS_CONTENTS_INLINE_AND_SECONDARY_COMMAND_BUFFERS_EXT",
}

VkVertexInputRate = {
    0: "VK_VERTEX_INPUT_RATE_VERTEX",
    1: "VK_VERTEX_INPUT_RATE_INSTANCE",
}

