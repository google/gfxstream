/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "HostConnection.h"

#include "GoldfishAddressSpaceStream.h"
#include "VirtioGpuAddressSpaceStream.h"
#include "aemu/base/threads/AndroidThread.h"
#if defined(__ANDROID__)
#include "android-base/properties.h"
#endif
#include "gfxstream/common/logging.h"
#include "renderControl_types.h"

using gfxstream::guest::ChecksumCalculator;
using gfxstream::guest::IOStream;

#ifdef GOLDFISH_NO_GL
struct gl_client_context_t {
    int placeholder;
};
class GLEncoder : public gl_client_context_t {
public:
    GLEncoder(IOStream*, ChecksumCalculator*) { }
    void setContextAccessor(gl_client_context_t *()) { }
};
struct gl2_client_context_t {
    int placeholder;
};
class GL2Encoder : public gl2_client_context_t {
public:
    GL2Encoder(IOStream*, ChecksumCalculator*) { }
    void setContextAccessor(gl2_client_context_t *()) { }
    void setNoHostError(bool) { }
    void setDrawCallFlushInterval(uint32_t) { }
    void setHasAsyncUnmapBuffer(int) { }
    void setHasSyncBufferData(int) { }
};
#else
#include "GLEncoder.h"
#include "GL2Encoder.h"
#endif

#include "AddressSpaceStream.h"
#include <unistd.h>

#include "ProcessPipe.h"
#include "QemuPipeStream.h"
#include "ThreadInfo.h"

#include "VirtGpu.h"
#include "VirtioGpuPipeStream.h"

#if defined(__linux__) || defined(__ANDROID__)
#include <fstream>
#include <string>
#include <unistd.h>
#endif

#define STREAM_BUFFER_SIZE  (4*1024*1024)

constexpr const auto kEglProp = "ro.hardware.egl";

static HostConnectionType getConnectionTypeFromProperty(enum VirtGpuCapset capset) {
#if defined(__Fuchsia__) || defined(LINUX_GUEST_BUILD)
    return HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE;
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
        return HOST_CONNECTION_QEMU_PIPE;
#else
        return HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE;
#endif
    }

    if (transport == "asg") {
        return HOST_CONNECTION_ADDRESS_SPACE;
    }
    if (transport == "pipe") {
        return HOST_CONNECTION_QEMU_PIPE;
    }

    if (transport == "virtio-gpu-asg" || transport == "virtio-gpu-pipe") {
        std::string egl;
#if defined(__ANDROID__)
        egl = android::base::GetProperty(kEglProp, "");
#endif
        // ANGLE doesn't work well without ASG, particularly if HostComposer uses a pipe
        // transport and VK uses ASG.
        if (capset == kCapsetGfxStreamVulkan || egl == "angle") {
            return HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE;
        } else {
            return HOST_CONNECTION_VIRTIO_GPU_PIPE;
        }
    }

    return HOST_CONNECTION_QEMU_PIPE;
#endif
}

static uint32_t getDrawCallFlushIntervalFromProperty() {
    constexpr uint32_t kDefaultValue = 800;
    uint32_t value = kDefaultValue;

#if defined(__ANDROID__)
    value = android::base::GetUintProperty("ro.boot.qemu.gltransport.drawFlushInterval",
                                           kDefaultValue);
#endif
    return value;
}

HostConnection::HostConnection() : m_checksumHelper(), m_noHostError(true), m_rendernodeFd(-1) {}

HostConnection::~HostConnection()
{
    // round-trip to ensure that queued commands have been processed
    // before process pipe closure is detected.
    if (m_rcEnc) {
        (void)m_rcEnc->rcGetRendererVersion(m_rcEnc.get());
    }

    if (m_stream) {
        m_stream->decRef();
    }
}

// static
std::unique_ptr<HostConnection> HostConnection::connect(enum VirtGpuCapset capset) {
    const enum HostConnectionType connType = getConnectionTypeFromProperty(capset);
    uint32_t noRenderControlEnc = 0;

    // Use "new" to access a non-public constructor.
    auto con = std::unique_ptr<HostConnection>(new HostConnection);
    con->m_connectionType = connType;

    switch (connType) {
        case HOST_CONNECTION_ADDRESS_SPACE: {
#if defined(__ANDROID__)
            auto stream = createGoldfishAddressSpaceStream(STREAM_BUFFER_SIZE);
            if (!stream) {
                GFXSTREAM_ERROR("Failed to create AddressSpaceStream for host connection.");
                return nullptr;
            }
            con->m_stream = stream;
#else
            GFXSTREAM_FATAL("HOST_CONNECTION_ADDRESS_SPACE not supported on this host.");
            abort();
#endif

            break;
        }
#if !defined(__Fuchsia__)
        case HOST_CONNECTION_QEMU_PIPE: {
            auto stream = new QemuPipeStream(STREAM_BUFFER_SIZE);
            if (!stream) {
                GFXSTREAM_ERROR("Failed to create QemuPipeStream for host connection.");
                return nullptr;
            }
            if (stream->connect() < 0) {
                GFXSTREAM_ERROR("Failed to connect to host (QemuPipeStream).");
                return nullptr;
            }
            con->m_stream = stream;
            break;
        }
#endif
        case HOST_CONNECTION_VIRTIO_GPU_PIPE: {
            auto stream = new VirtioGpuPipeStream(STREAM_BUFFER_SIZE, INVALID_DESCRIPTOR);
            if (!stream) {
                GFXSTREAM_ERROR("Failed to create VirtioGpu for host connection.");
                return nullptr;
            }
            if (stream->connect() < 0) {
                GFXSTREAM_ERROR("Failed to connect to host (VirtioGpu).");
                return nullptr;
            }

            auto rendernodeFd = stream->getRendernodeFd();
            auto device = VirtGpuDevice::getInstance(capset);
            con->m_stream = stream;
            con->m_rendernodeFd = rendernodeFd;
            break;
        }
        case HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE: {
            // Use kCapsetGfxStreamVulkan for now, Ranchu HWC needs to be modified to pass in
            // right capset.
            auto device = VirtGpuDevice::getInstance(kCapsetGfxStreamVulkan);
            auto deviceHandle = device->getDeviceHandle();
            auto stream = createVirtioGpuAddressSpaceStream(kCapsetGfxStreamVulkan);
            if (!stream) {
                GFXSTREAM_ERROR("Failed to create virtgpu AddressSpaceStream.");
                return nullptr;
            }
            con->m_stream = stream;
            con->m_rendernodeFd = deviceHandle;
            break;
        }
        default:
            break;
    }

#if defined(ANDROID)
    con->m_grallocHelper.reset(gfxstream::createPlatformGralloc(con->m_rendernodeFd));
    if (!con->m_grallocHelper) {
        GFXSTREAM_FATAL("Failed to create platform Gralloc!");
        abort();
    }

    con->m_anwHelper.reset(gfxstream::createPlatformANativeWindowHelper());
    if (!con->m_anwHelper) {
        GFXSTREAM_FATAL("Failed to create platform ANativeWindowHelper!");
        abort();
    }
#endif

    con->m_syncHelper.reset(gfxstream::createPlatformSyncHelper());

    // send zero 'clientFlags' to the host.
    unsigned int *pClientFlags =
            (unsigned int *)con->m_stream->allocBuffer(sizeof(unsigned int));
    *pClientFlags = 0;
    con->m_stream->commitBuffer(sizeof(unsigned int));

    if (capset == kCapsetGfxStreamMagma) {
        noRenderControlEnc = 1;
    } else if (capset == kCapsetGfxStreamVulkan) {
        VirtGpuDevice* instance = VirtGpuDevice::getInstance(kCapsetGfxStreamVulkan);
        auto caps = instance->getCaps();
        noRenderControlEnc = caps.vulkanCapset.noRenderControlEnc;
    }

    auto handle = (connType == HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE) ? con->m_rendernodeFd : -1;
    processPipeInit(handle, connType, noRenderControlEnc);
    if (!noRenderControlEnc && capset == kCapsetGfxStreamVulkan) {
        con->rcEncoder();
    }

    return con;
}

HostConnection* HostConnection::get() { return getWithThreadInfo(getEGLThreadInfo(), kCapsetNone); }

HostConnection* HostConnection::getOrCreate(enum VirtGpuCapset capset) {
    return getWithThreadInfo(getEGLThreadInfo(), capset);
}

HostConnection* HostConnection::getWithThreadInfo(EGLThreadInfo* tinfo, enum VirtGpuCapset capset) {
    // Get thread info
    if (!tinfo) {
        return NULL;
    }

    if (tinfo->hostConn == NULL) {
        tinfo->hostConn = HostConnection::createUnique(capset);
    }

    return tinfo->hostConn.get();
}

void HostConnection::exit() {
    EGLThreadInfo *tinfo = getEGLThreadInfo();
    if (!tinfo) {
        return;
    }

#if defined(ANDROID)
    if (tinfo->hostConn) {
        tinfo->hostConn->m_grallocHelper = nullptr;
    }
#endif

    tinfo->hostConn.reset();
}

// static
std::unique_ptr<HostConnection> HostConnection::createUnique(enum VirtGpuCapset capset) {
    return connect(capset);
}

GLEncoder *HostConnection::glEncoder()
{
    if (!m_glEnc) {
        m_glEnc = std::make_unique<GLEncoder>(m_stream, &m_checksumHelper);
        m_glEnc->setContextAccessor(s_getGLContext);
    }
    return m_glEnc.get();
}

GL2Encoder *HostConnection::gl2Encoder()
{
    if (!m_gl2Enc) {
        m_gl2Enc = std::make_unique<GL2Encoder>(m_stream, &m_checksumHelper);
        m_gl2Enc->setContextAccessor(s_getGL2Context);
        m_gl2Enc->setNoHostError(m_noHostError);
        m_gl2Enc->setDrawCallFlushInterval(
            getDrawCallFlushIntervalFromProperty());
        m_gl2Enc->setHasAsyncUnmapBuffer(m_rcEnc->hasAsyncUnmapBuffer());
        m_gl2Enc->setHasSyncBufferData(m_rcEnc->hasSyncBufferData());
    }
    return m_gl2Enc.get();
}

ExtendedRCEncoderContext *HostConnection::rcEncoder()
{
    if (!m_rcEnc) {
        m_rcEnc = std::make_unique<ExtendedRCEncoderContext>(m_stream, &m_checksumHelper);

        ExtendedRCEncoderContext* rcEnc = m_rcEnc.get();
        rcEnc->setChecksumHelper(&m_checksumHelper);
        rcEnc->queryAndSetSyncImpl();
        rcEnc->queryAndSetDmaImpl();
        rcEnc->queryAndSetGLESMaxVersion();
        rcEnc->queryAndSetNoErrorState(m_noHostError);
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

        rcEnc->rcSetPuid(rcEnc, getPuid());
    }
    return m_rcEnc.get();
}

gl_client_context_t *HostConnection::s_getGLContext()
{
    EGLThreadInfo *ti = getEGLThreadInfo();
    if (ti->hostConn) {
        return ti->hostConn->m_glEnc.get();
    }
    return NULL;
}

gl2_client_context_t *HostConnection::s_getGL2Context()
{
    EGLThreadInfo *ti = getEGLThreadInfo();
    if (ti->hostConn) {
        return ti->hostConn->m_gl2Enc.get();
    }
    return NULL;
}

void HostConnection::setVulkanFeatureInfo(void* info) {
    ExtendedRCEncoderContext* rcEnc = m_rcEnc.get();
    rcEnc->setVulkanFeatureInfo(info);
}
