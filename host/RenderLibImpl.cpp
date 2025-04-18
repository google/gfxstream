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
#include "gfxstream/host/logging.h"
#include "host-common/address_space_device_control_ops.h"
#include "host-common/crash_reporter.h"
#include "host-common/dma_device.h"
#include "host-common/emugl_vm_operations.h"
#include "host-common/feature_control.h"
#include "host-common/opengl/misc.h"
#include "host-common/sync_device.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#endif

namespace gfxstream {

void RenderLibImpl::setRenderer(SelectedRenderer renderer) {
    emugl::setRenderer(renderer);
}

void RenderLibImpl::setAvdInfo(bool phone, int api) {
    emugl::setAvdInfo(phone, api);
}

void RenderLibImpl::getGlesVersion(int* maj, int* min) {
    emugl::getGlesVersion(maj, min);
}

void RenderLibImpl::setLogger(emugl_logger_struct logger) {
#ifdef CONFIG_AEMU
    set_gfxstream_logger(logger);

    // TODO: move this into emu init.
    gfxstream::host::SetGfxstreamLogCallback(
        [](gfxstream::host::LogLevel level, const char* file, int line, const char* function, const char* message) {
            char severity;
            switch (level) {
                case gfxstream::host::LogLevel::kFatal:
                    severity = 'F';
                    break;
                case gfxstream::host::LogLevel::kError:
                    severity = 'E';
                    break;
                case gfxstream::host::LogLevel::kWarning:
                    severity = 'W';
                    break;
                case gfxstream::host::LogLevel::kInfo:
                    severity = 'I';
                    break;
                case gfxstream::host::LogLevel::kDebug:
                    severity = 'D';
                    break;
                case gfxstream::host::LogLevel::kVerbose:
                    severity = 'V';
                    break;
            }
            OutputLog(stderr, severity, file, line, /*timestamp_us=*/0, "%s", message);
        });
#endif
}

void RenderLibImpl::setFeatureController(emugl_feature_is_enabled_t featureController) {
    android::featurecontrol::setFeatureEnabledCallback(featureController);
}

void RenderLibImpl::setSyncDevice
    (emugl_sync_create_timeline_t create_timeline,
     emugl_sync_create_fence_t create_fence,
     emugl_sync_timeline_inc_t timeline_inc,
     emugl_sync_destroy_timeline_t destroy_timeline,
     emugl_sync_register_trigger_wait_t register_trigger_wait,
     emugl_sync_device_exists_t device_exists) {
    emugl::set_emugl_sync_create_timeline(create_timeline);
    emugl::set_emugl_sync_create_fence(create_fence);
    emugl::set_emugl_sync_timeline_inc(timeline_inc);
    emugl::set_emugl_sync_destroy_timeline(destroy_timeline);
    emugl::set_emugl_sync_register_trigger_wait(register_trigger_wait);
    emugl::set_emugl_sync_device_exists(device_exists);
}

void RenderLibImpl::setDmaOps(emugl_dma_ops ops) {
    emugl::set_emugl_dma_get_host_addr(ops.get_host_addr);
    emugl::set_emugl_dma_unlock(ops.unlock);
}

void RenderLibImpl::setVmOps(const QAndroidVmOperations &vm_operations) {
    set_emugl_vm_operations(vm_operations);
    address_space_set_vm_operations(&get_emugl_vm_operations());
}

void RenderLibImpl::setAddressSpaceDeviceControlOps(struct address_space_device_control_ops* ops) {
    set_emugl_address_space_device_control_ops(ops);
}

void RenderLibImpl::setWindowOps(const QAndroidEmulatorWindowAgent &window_operations,
                                 const QAndroidMultiDisplayAgent &multi_display_operations) {
    emugl::set_emugl_window_operations(window_operations);
    emugl::set_emugl_multi_display_operations(multi_display_operations);
}

void RenderLibImpl::setUsageTracker(android::base::CpuUsage* cpuUsage,
                                    android::base::MemoryTracker* memUsage) {
    emugl::setCpuUsage(cpuUsage);
    emugl::setMemoryTracker(memUsage);
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

static void impl_onLastCbRef(uint32_t handle) {
    FrameBuffer* fb = FrameBuffer::getFB();
    if (fb)
        fb->onLastColorBufferRef(handle);
}

OnLastColorBufferRef RenderLibImpl::getOnLastColorBufferRef() {
    return (OnLastColorBufferRef)impl_onLastCbRef;
}

}  // namespace gfxstream
