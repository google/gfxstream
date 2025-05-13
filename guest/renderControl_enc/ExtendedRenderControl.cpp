// Copyright (C) 2024 The Android Open Source Project
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
#include "ExtendedRenderControl.h"

#include "gfxstream/common/logging.h"

using gfxstream::guest::ChecksumCalculator;

const std::string& ExtendedRCEncoderContext::queryHostExtensions() {
    if (!m_hostExtensions.empty()) {
        return m_hostExtensions;
    }

    // Extensions strings are usually quite long, preallocate enough here.
    std::string extensionsBuffer(1023, '\0');

    // Returns the required size including the 0-terminator, so
    // account it when passing/using the sizes.
    int extensionSize =
        this->rcGetHostExtensionsString(this, extensionsBuffer.size() + 1, &extensionsBuffer[0]);
    if (extensionSize < 0) {
        extensionsBuffer.resize(-extensionSize);
        extensionSize =
            this->rcGetHostExtensionsString(this, -extensionSize + 1, &extensionsBuffer[0]);
    }

    if (extensionSize > 0) {
        extensionsBuffer.resize(extensionSize - 1);
        m_hostExtensions.swap(extensionsBuffer);
    }

    return m_hostExtensions;
}

uint64_t ExtendedRCEncoderContext::lockAndWriteDma(void* data, uint32_t size) {
        if (m_dmaPtr && m_dmaPhysAddr) {
            if (data != m_dmaPtr) {
                memcpy(m_dmaPtr, data, size);
            }
            return m_dmaPhysAddr;
        } else {
            GFXSTREAM_ERROR("No DMA context bound!");
            return 0;
        }
    }

void ExtendedRCEncoderContext::queryAndSetHostCompositionImpl() {
    const std::string& hostExtensions = queryHostExtensions();
    // make sure V2 is checked first before V1, as host may declare supporting both
    if (hostExtensions.find(kHostCompositionV2) != std::string::npos) {
        this->setHostComposition(HOST_COMPOSITION_V2);
    } else if (hostExtensions.find(kHostCompositionV1) != std::string::npos) {
        this->setHostComposition(HOST_COMPOSITION_V1);
    } else {
        this->setHostComposition(HOST_COMPOSITION_NONE);
    }
}

void ExtendedRCEncoderContext::setChecksumHelper(ChecksumCalculator* calculator) {
    const std::string& hostExtensions = queryHostExtensions();
    // check the host supported version
    uint32_t checksumVersion = 0;
    const char* checksumPrefix = ChecksumCalculator::getMaxVersionStrPrefix();
    const char* glProtocolStr = strstr(hostExtensions.c_str(), checksumPrefix);
    if (glProtocolStr) {
        uint32_t maxVersion = ChecksumCalculator::getMaxVersion();
        sscanf(glProtocolStr + strlen(checksumPrefix), "%d", &checksumVersion);
        if (maxVersion < checksumVersion) {
            checksumVersion = maxVersion;
        }
        // The ordering of the following two commands matters!
        // Must tell the host first before setting it in the guest
        this->rcSelectChecksumHelper(this, checksumVersion, 0);
        calculator->setVersion(checksumVersion);
    }
}

void ExtendedRCEncoderContext::queryAndSetSyncImpl() {
    const std::string& hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kRCNativeSyncV4) != std::string::npos) {
        this->setSyncImpl(SYNC_IMPL_NATIVE_SYNC_V4);
    } else if (hostExtensions.find(kRCNativeSyncV3) != std::string::npos) {
        this->setSyncImpl(SYNC_IMPL_NATIVE_SYNC_V3);
    } else if (hostExtensions.find(kRCNativeSyncV2) != std::string::npos) {
        this->setSyncImpl(SYNC_IMPL_NATIVE_SYNC_V2);
    } else {
        this->setSyncImpl(SYNC_IMPL_NONE);
    }
}

void ExtendedRCEncoderContext::queryAndSetDmaImpl() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kDmaExtStr_v1) != std::string::npos) {
        this->setDmaImpl(DMA_IMPL_v1);
    } else {
        this->setDmaImpl(DMA_IMPL_NONE);
    }
}

void ExtendedRCEncoderContext::queryAndSetGLESMaxVersion() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kGLESMaxVersion_2) != std::string::npos) {
        this->setGLESMaxVersion(GLES_MAX_VERSION_2);
    } else if (hostExtensions.find(kGLESMaxVersion_3_0) != std::string::npos) {
        this->setGLESMaxVersion(GLES_MAX_VERSION_3_0);
    } else if (hostExtensions.find(kGLESMaxVersion_3_1) != std::string::npos) {
        this->setGLESMaxVersion(GLES_MAX_VERSION_3_1);
    } else if (hostExtensions.find(kGLESMaxVersion_3_2) != std::string::npos) {
        this->setGLESMaxVersion(GLES_MAX_VERSION_3_2);
    } else {
        GFXSTREAM_WARNING("Unrecognized GLES max version string in extensions: %s", hostExtensions.c_str());
        this->setGLESMaxVersion(GLES_MAX_VERSION_2);
    }
}

void ExtendedRCEncoderContext::queryAndSetNoErrorState(bool& hostError) {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kGLESUseHostError) != std::string::npos) {
        hostError = false;
    }
}

void ExtendedRCEncoderContext::queryAndSetDirectMemSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kGLDirectMem) != std::string::npos) {
        this->featureInfo()->hasDirectMem = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkan) != std::string::npos) {
        this->featureInfo()->hasVulkan = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetDeferredVulkanCommandsSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kDeferredVulkanCommands) != std::string::npos) {
        this->featureInfo()->hasDeferredVulkanCommands = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanNullOptionalStringsSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanNullOptionalStrings) != std::string::npos) {
        this->featureInfo()->hasVulkanNullOptionalStrings = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanCreateResourcesWithRequirementsSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanCreateResourcesWithRequirements) != std::string::npos) {
        this->featureInfo()->hasVulkanCreateResourcesWithRequirements = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanIgnoredHandles() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanIgnoredHandles) != std::string::npos) {
        this->featureInfo()->hasVulkanIgnoredHandles = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetYUVCache() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kYUVCache) != std::string::npos) {
        this->featureInfo()->hasYUVCache = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetAsyncUnmapBuffer() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kAsyncUnmapBuffer) != std::string::npos) {
        this->featureInfo()->hasAsyncUnmapBuffer = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVirtioGpuNext() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVirtioGpuNext) != std::string::npos) {
        this->featureInfo()->hasVirtioGpuNext = true;
    }
}

void ExtendedRCEncoderContext::queryHasSharedSlotsHostMemoryAllocator() {
    const std::string& hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kHasSharedSlotsHostMemoryAllocator) != std::string::npos) {
        this->featureInfo()->hasSharedSlotsHostMemoryAllocator = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanFreeMemorySync() {
    const std::string& hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanFreeMemorySync) != std::string::npos) {
        this->featureInfo()->hasVulkanFreeMemorySync = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVirtioGpuNativeSync() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVirtioGpuNativeSync) != std::string::npos) {
        this->featureInfo()->hasVirtioGpuNativeSync = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanShaderFloat16Int8Support() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanShaderFloat16Int8) != std::string::npos) {
        this->featureInfo()->hasVulkanShaderFloat16Int8 = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanAsyncQueueSubmitSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanAsyncQueueSubmit) != std::string::npos) {
        this->featureInfo()->hasVulkanAsyncQueueSubmit = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetHostSideTracingSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kHostSideTracing) != std::string::npos) {
        this->featureInfo()->hasHostSideTracing = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetAsyncFrameCommands() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kAsyncFrameCommands) != std::string::npos) {
        this->featureInfo()->hasAsyncFrameCommands = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanQueueSubmitWithCommandsSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanQueueSubmitWithCommands) != std::string::npos) {
        this->featureInfo()->hasVulkanQueueSubmitWithCommands = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanBatchedDescriptorSetUpdateSupport() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanBatchedDescriptorSetUpdate) != std::string::npos) {
        this->featureInfo()->hasVulkanBatchedDescriptorSetUpdate = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetSyncBufferData() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kSyncBufferData) != std::string::npos) {
        this->featureInfo()->hasSyncBufferData = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanAsyncQsri() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kVulkanAsyncQsri) != std::string::npos) {
        this->featureInfo()->hasVulkanAsyncQsri = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetReadColorBufferDma() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kReadColorBufferDma) != std::string::npos) {
        this->featureInfo()->hasReadColorBufferDma = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetHWCMultiConfigs() {
    std::string hostExtensions = queryHostExtensions();
    if (hostExtensions.find(kHWCMultiConfigs) != std::string::npos) {
        this->featureInfo()->hasHWCMultiConfigs = true;
    }
}

void ExtendedRCEncoderContext::queryAndSetVulkanAuxCommandBufferMemory() {
    std::string hostExtensions = queryHostExtensions();
    this->featureInfo()->hasVulkanAuxCommandMemory =
        hostExtensions.find(kVulkanAuxCommandMemory) != std::string::npos;
}

GLint ExtendedRCEncoderContext::queryVersion() {
    GLint version = this->rcGetRendererVersion(this);
    return version;
}

void ExtendedRCEncoderContext::setVulkanFeatureInfo(void* info) {
    struct EmulatorGfxStreamVkFeatureInfo* featureInfo =
        (struct EmulatorGfxStreamVkFeatureInfo*)info;

    featureInfo->hasDirectMem = this->featureInfo()->hasDirectMem;
    featureInfo->hasVulkan = this->featureInfo()->hasVulkan;
    featureInfo->hasDeferredVulkanCommands = this->featureInfo()->hasDeferredVulkanCommands;
    featureInfo->hasVulkanNullOptionalStrings = this->featureInfo()->hasVulkanNullOptionalStrings;
    featureInfo->hasVulkanCreateResourcesWithRequirements =
        this->featureInfo()->hasVulkanCreateResourcesWithRequirements;
    featureInfo->hasVulkanIgnoredHandles = this->featureInfo()->hasVulkanIgnoredHandles;
    featureInfo->hasVirtioGpuNext = this->featureInfo()->hasVirtioGpuNext;
    featureInfo->hasVulkanFreeMemorySync = this->featureInfo()->hasVulkanFreeMemorySync;
    featureInfo->hasVirtioGpuNativeSync = this->featureInfo()->hasVirtioGpuNativeSync;
    featureInfo->hasVulkanShaderFloat16Int8 = this->featureInfo()->hasVulkanShaderFloat16Int8;
    featureInfo->hasVulkanAsyncQueueSubmit = this->featureInfo()->hasVulkanAsyncQueueSubmit;
    featureInfo->hasVulkanQueueSubmitWithCommands =
        this->featureInfo()->hasVulkanQueueSubmitWithCommands;
    featureInfo->hasVulkanBatchedDescriptorSetUpdate =
        this->featureInfo()->hasVulkanBatchedDescriptorSetUpdate;
    featureInfo->hasVulkanAsyncQsri = this->featureInfo()->hasVulkanAsyncQsri;
    featureInfo->hasVulkanAuxCommandMemory = this->featureInfo()->hasVulkanAuxCommandMemory;
}
