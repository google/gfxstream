// Copyright (C) 2016 The Android Open Source Project
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
#include "RenderLibImpl.h"

#include "FrameBuffer.h"
#include "RendererImpl.h"
#include "aemu/base/files/Stream.h"
#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/dma_device.h"
#include "gfxstream/host/guest_operations.h"
#include "gfxstream/host/logging.h"
#include "gfxstream/host/sync_device.h"
#include "gfxstream/host/vm_operations.h"
#include "gfxstream/host/window_operations.h"
#include "host-common/address_space_device_control_ops.h"
#include "host-common/misc.h"
#include "host-common/opengl/misc.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#endif

namespace gfxstream {

void RenderLibImpl::setRenderer(SelectedRenderer renderer) {
    emugl::setRenderer(renderer);
}

void RenderLibImpl::setGuestAndroidApiLevel(int api) {
    set_gfxstream_guest_android_api_level(api);
}

void RenderLibImpl::getGlesVersion(int* maj, int* min) {
    emugl::getGlesVersion(maj, min);
}

void RenderLibImpl::setLogger(gfxstream_log_callback_t callback) {
    gfxstream::host::SetGfxstreamLogCallback(
        [callback](gfxstream::host::LogLevel level, const char* file, int line, const char* function, const char* message) {
            callback(static_cast<gfxstream_logging_level>(level), file, line, function, message);
        });
}

void RenderLibImpl::setSyncDevice
    (gfxstream_sync_create_timeline_t create_timeline,
     gfxstream_sync_create_fence_t create_fence,
     gfxstream_sync_timeline_inc_t timeline_inc,
     gfxstream_sync_destroy_timeline_t destroy_timeline,
     gfxstream_sync_register_trigger_wait_t register_trigger_wait,
     gfxstream_sync_device_exists_t device_exists) {
    set_gfxstream_sync_create_timeline(create_timeline);
    set_gfxstream_sync_create_fence(create_fence);
    set_gfxstream_sync_timeline_inc(timeline_inc);
    set_gfxstream_sync_destroy_timeline(destroy_timeline);
    set_gfxstream_sync_register_trigger_wait(register_trigger_wait);
    set_gfxstream_sync_device_exists(device_exists);
}

void RenderLibImpl::setDmaOps(gfxstream_dma_ops ops) {
    set_gfxstream_dma_get_host_addr(ops.get_host_addr);
    set_gfxstream_dma_unlock(ops.unlock);
}

void RenderLibImpl::setVmOps(const gfxstream_vm_ops& ops) {
    set_gfxstream_vm_operations(ops);

    // TODO: remove in next change:
    static const QAndroidVmOperations sAndroidOps = {
        .mapUserBackedRam = ops.map_user_memory,
        .unmapUserBackedRam = ops.unmap_user_memory,
        .physicalMemoryGetAddr = ops.lookup_user_memory,
    };
    address_space_set_vm_operations(&sAndroidOps);
}

void RenderLibImpl::setAddressSpaceDeviceControlOps(struct address_space_device_control_ops* ops) {
    set_emugl_address_space_device_control_ops(ops);
}

void RenderLibImpl::setWindowOps(const gfxstream_window_ops& window_operations) {
    set_gfxstream_window_operations(window_operations);
}

void RenderLibImpl::setDisplayOps(const gfxstream_multi_display_ops& display_operations) {
    set_gfxstream_multi_display_operations(display_operations);
}

void RenderLibImpl::setGrallocImplementation(GrallocImplementation gralloc) {
    emugl::setGrallocImplementation(gralloc);
}

bool RenderLibImpl::getOpt(RenderOpt* opt) {
    FrameBuffer* fb  = FrameBuffer::getFB();
    if (fb == nullptr) {
        return false;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    opt->display = fb->getDisplay();
    opt->surface = fb->getWindowSurface();
    opt->config = fb->getConfig();
#endif
    return (opt->display && opt->surface  && opt->config);
}

RendererPtr RenderLibImpl::initRenderer(int width, int height,
                                        const gfxstream::host::FeatureSet& features,
                                        bool useSubWindow, bool egl2egl) {
    if (!mRenderer.expired()) {
        return nullptr;
    }

    const auto res = std::make_shared<RendererImpl>();
    if (!res->initialize(width, height, features, useSubWindow, egl2egl)) {
        return nullptr;
    }
    mRenderer = res;
    return res;
}

OnLastColorBufferRef RenderLibImpl::getOnLastColorBufferRef() {
    return [](uint32_t handle) {
        FrameBuffer* fb = FrameBuffer::getFB();
        if (fb) {
            fb->onLastColorBufferRef(handle);
        }
    };
}

}  // namespace gfxstream
