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

#include "render-utils/RenderLib.h"

#include <memory>

namespace gfxstream {

class RenderLibImpl final : public RenderLib {
  public:
    RenderLibImpl() = default;

    RenderLibImpl(const RenderLibImpl&) = delete;
    RenderLibImpl& operator=(const RenderLibImpl&) = delete;

    virtual void setRenderer(SelectedRenderer renderer) override;
    virtual void setGuestAndroidApiLevel(int api) override;
    virtual void getGlesVersion(int* maj, int* min) override;
    virtual void setLogger(gfxstream_log_callback_t callback) override;

    virtual void setSyncDevice(gfxstream_sync_create_timeline_t,
                               gfxstream_sync_create_fence_t,
                               gfxstream_sync_timeline_inc_t,
                               gfxstream_sync_destroy_timeline_t,
                               gfxstream_sync_register_trigger_wait_t,
                               gfxstream_sync_device_exists_t) override;

    virtual void setDmaOps(gfxstream_dma_ops) override;

    virtual void setVmOps(const gfxstream_vm_ops& vm_operations) override;

    virtual void setAddressSpaceDeviceControlOps(struct address_space_device_control_ops* ops) override;

    virtual void setWindowOps(const gfxstream_window_ops& window_operations) override;

    virtual void setDisplayOps(const gfxstream_multi_display_ops& display_operations) override;

    virtual void setGrallocImplementation(GrallocImplementation gralloc) override;

    virtual bool getOpt(RenderOpt* opt) override;

    virtual RendererPtr initRenderer(int width,
                                     int height,
                                     const gfxstream::host::FeatureSet& features,
                                     bool useSubWindow,
                                     bool egl2egl) override;

    OnLastColorBufferRef getOnLastColorBufferRef() override;

  private:
    std::weak_ptr<Renderer> mRenderer;
};

}  // namespace gfxstream
