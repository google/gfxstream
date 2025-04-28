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
#pragma once

#include <functional>
#include <memory>

#include "aemu/base/files/Stream.h"
#include "gfxstream/host/Features.h"
#include "render-utils/Renderer.h"
#include "render-utils/display_operations.h"
#include "render-utils/dma_device.h"
#include "render-utils/gralloc_enums.h"
#include "render-utils/logging_operations.h"
#include "render-utils/render_api_types.h"
#include "render-utils/renderer_enums.h"
#include "render-utils/sync_device.h"
#include "render-utils/vm_operations.h"
#include "render-utils/window_operations.h"

extern "C" {

struct address_space_device_control_ops;

} // extern "C"

namespace gfxstream {

struct RenderOpt {
    void* display;
    void* surface;
    void* config;
};

using OnLastColorBufferRef = std::function<void(uint32_t)>;

// RenderLib - root interface for the GPU emulation library
//  Use it to set the library-wide parameters (logging, crash reporting) and
//  create indivudual renderers that take care of drawing windows.
class RenderLib {
public:
    virtual ~RenderLib() = default;

    // Get the selected renderer
    virtual void setRenderer(SelectedRenderer renderer) = 0;

    virtual void setGuestAndroidApiLevel(int api) = 0;

    // Get the GLES major/minor version determined.
    virtual void getGlesVersion(int* maj, int* min) = 0;

    virtual void setLogger(gfxstream_log_callback_t callback) = 0;

    // TODO: delete after goldfish fully migrates to virtio gpu.
    virtual void setSyncDevice(gfxstream_sync_create_timeline_t,
                               gfxstream_sync_create_fence_t,
                               gfxstream_sync_timeline_inc_t,
                               gfxstream_sync_destroy_timeline_t,
                               gfxstream_sync_register_trigger_wait_t,
                               gfxstream_sync_device_exists_t) = 0;

    // Sets the function use to read from the guest
    // physically contiguous DMA region at particular offsets.
    virtual void setDmaOps(gfxstream_dma_ops) = 0;

    virtual void setVmOps(const gfxstream_vm_ops& vm_operations) = 0;
    virtual void setAddressSpaceDeviceControlOps(struct address_space_device_control_ops* ops) = 0;

    virtual void setWindowOps(const gfxstream_window_ops& window_operations) = 0;

    virtual void setDisplayOps(const gfxstream_multi_display_ops& display_ops) = 0;

    virtual void setGrallocImplementation(GrallocImplementation gralloc) = 0;

    virtual bool getOpt(RenderOpt* opt) = 0;

    // initRenderer - initialize the OpenGL renderer object.
    //
    // |width| and |height| are the framebuffer dimensions that will be reported
    // to the guest display driver.
    //
    // |features| host side feature flags.
    //
    // |useSubWindow| is true to indicate that renderer has to support an
    // OpenGL subwindow. If false, it only supports setPostCallback().
    // See Renderer.h for more info.
    //
    // There might be only one renderer.
    virtual RendererPtr initRenderer(int width, int height,
                                     const gfxstream::host::FeatureSet& features,
                                     bool useSubWindow, bool egl2egl) = 0;

    virtual OnLastColorBufferRef getOnLastColorBufferRef() = 0;
};

using RenderLibPtr = std::unique_ptr<RenderLib>;

}  // namespace gfxstream
