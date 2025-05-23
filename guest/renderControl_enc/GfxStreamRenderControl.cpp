/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GfxStreamRenderControl.h"

#include <mutex>

#include "GfxStreamRenderControlConnection.h"

#if defined(__ANDROID__)
#include "android-base/file.h"
#include "android-base/properties.h"
#endif
#include "aemu/base/Process.h"

constexpr const auto kEglProp = "ro.hardware.egl";

static uint64_t sProcUID = 0;
static std::mutex sNeedInitMutex;
static bool sNeedInit = true;
static gfxstream::guest::IOStream* sProcessStream = nullptr;

GfxStreamTransportType renderControlGetTransport() {
#if defined(__Fuchsia__) || defined(LINUX_GUEST_BUILD)
    return GFXSTREAM_TRANSPORT_VIRTIO_GPU_ADDRESS_SPACE;
#else
    std::string transport;

#if defined(__ANDROID__)
    transport = android::base::GetProperty("ro.boot.hardware.gltransport", "");
#else
    const char* transport_envvar = getenv("GFXSTREAM_TRANSPORT");
    if (transport_envvar != nullptr) {
        transport = std::string(transport_envvar);
    }
#endif

    if (transport.empty()) {
#if defined(__ANDROID__)
        return GFXSTREAM_TRANSPORT_QEMU_PIPE;
#else
        return GFXSTREAM_TRANSPORT_VIRTIO_GPU_ADDRESS_SPACE;
#endif
    }

    if (transport == "asg") {
        return GFXSTREAM_TRANSPORT_ADDRESS_SPACE;
    }
    if (transport == "pipe") {
        return GFXSTREAM_TRANSPORT_QEMU_PIPE;
    }

    if (transport == "virtio-gpu-asg" || transport == "virtio-gpu-pipe") {
        std::string egl;
#if defined(__ANDROID__)
        egl = android::base::GetProperty(kEglProp, "");
#endif
        return GFXSTREAM_TRANSPORT_VIRTIO_GPU_ADDRESS_SPACE;
    }

    return GFXSTREAM_TRANSPORT_QEMU_PIPE;
#endif
}

static uint64_t getPuid(GfxStreamConnectionManager* mgr) {
    std::lock_guard<std::mutex> lock(sNeedInitMutex);

    if (sNeedInit) {
        GfxStreamTransportType transport = renderControlGetTransport();
        sProcessStream = mgr->processPipeStream(transport);
        sProcUID = sProcessStream->processPipeInit();
        sNeedInit = false;
    }

    return sProcUID;
}

int32_t renderControlInit(GfxStreamConnectionManager* mgr, void* vkInfo) {
    ExtendedRCEncoderContext* rcEnc =
        (ExtendedRCEncoderContext*)mgr->getEncoder(GFXSTREAM_CONNECTION_RENDER_CONTROL);

    if (!rcEnc) {
        uint64_t puid = getPuid(mgr);
        auto stream = mgr->getStream();

        auto rcConnection = std::make_unique<GfxStreamRenderControlConnection>(stream);
        rcEnc = (ExtendedRCEncoderContext*)rcConnection->getEncoder();
        gfxstream::guest::ChecksumCalculator* calc = rcConnection->getCheckSumHelper();

        rcEnc->setChecksumHelper(calc);
        rcEnc->queryAndSetSyncImpl();
        rcEnc->queryAndSetDmaImpl();
        rcEnc->queryAndSetGLESMaxVersion();
        rcEnc->queryAndSetHostCompositionImpl();
        rcEnc->queryAndSetDirectMemSupport();
        rcEnc->queryAndSetVulkanSupport();
        rcEnc->queryAndSetDeferredVulkanCommandsSupport();
        rcEnc->queryAndSetVulkanNullOptionalStringsSupport();
        rcEnc->queryAndSetVulkanCreateResourcesWithRequirementsSupport();
        rcEnc->queryAndSetVulkanIgnoredHandles();
        rcEnc->queryAndSetYUVCache();
        rcEnc->queryAndSetAsyncUnmapBuffer();
        rcEnc->queryAndSetVirtioGpuNext();
        rcEnc->queryHasSharedSlotsHostMemoryAllocator();
        rcEnc->queryAndSetVulkanFreeMemorySync();
        rcEnc->queryAndSetVirtioGpuNativeSync();
        rcEnc->queryAndSetVulkanShaderFloat16Int8Support();
        rcEnc->queryAndSetVulkanAsyncQueueSubmitSupport();
        rcEnc->queryAndSetHostSideTracingSupport();
        rcEnc->queryAndSetAsyncFrameCommands();
        rcEnc->queryAndSetVulkanQueueSubmitWithCommandsSupport();
        rcEnc->queryAndSetVulkanBatchedDescriptorSetUpdateSupport();
        rcEnc->queryAndSetSyncBufferData();
        rcEnc->queryAndSetVulkanAsyncQsri();
        rcEnc->queryAndSetReadColorBufferDma();
        rcEnc->queryAndSetHWCMultiConfigs();
        rcEnc->queryAndSetVulkanAuxCommandBufferMemory();
        rcEnc->queryVersion();

        rcEnc->rcSetPuid(rcEnc, puid);

        static constexpr const char kRcMetadataKeyProcessName[] = "process_name";
        const std::string process_name = gfxstream::guest::getProcessName();
        rcEnc->rcSetProcessMetadata(rcEnc, const_cast<char*>(kRcMetadataKeyProcessName),
                                    const_cast<RenderControlByte*>(process_name.c_str()),
                                    process_name.length() + 1);

        if (vkInfo) {
            rcEnc->setVulkanFeatureInfo(vkInfo);
        }

        int32_t ret =
            mgr->addConnection(GFXSTREAM_CONNECTION_RENDER_CONTROL, std::move(rcConnection));
        if (ret) {
            return ret;
        }
    }

    return 0;
}
