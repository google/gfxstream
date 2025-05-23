// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gfxstream/guest/RenderControl.h"

#include <vector>

#include "HostConnection.h"
#include "gfxstream/common/logging.h"

namespace {

typedef struct compose_layer {
    uint32_t cbHandle;
    hwc2_composition_t composeMode;
    hwc_rect_t displayFrame;
    hwc_frect_t crop;
    int32_t blendMode;
    float alpha;
    hwc_color_t color;
    hwc_transform_t transform;
} ComposeLayer;

typedef struct compose_device {
    uint32_t version;
    uint32_t targetHandle;
    uint32_t numLayers;
    struct compose_layer layer[0];
} ComposeDevice;

typedef struct compose_device_v2 {
    uint32_t version;
    uint32_t displayId;
    uint32_t targetHandle;
    uint32_t numLayers;
    struct compose_layer layer[0];
} ComposeDevice_v2;

class RenderControlDeviceImpl {
   public:
    RenderControlDeviceImpl() : mHostConnection(HostConnection::createUnique(kCapsetNone)) {}

    RenderControlDeviceImpl(const RenderControlDeviceImpl& rhs) = delete;
    RenderControlDeviceImpl& operator=(const RenderControlDeviceImpl& rhs) = delete;

    RenderControlDeviceImpl(RenderControlDeviceImpl&& rhs) = delete;
    RenderControlDeviceImpl& operator=(RenderControlDeviceImpl&& rhs) = delete;

    int DoCompose(std::vector<uint8_t>& bytes) {
        if (!mHostConnection) {
            GFXSTREAM_ERROR("RenderControlDevice missing HostConnection.");
            return -1;
        }

        auto* rc = mHostConnection->rcEncoder();
        if (!rc) {
            GFXSTREAM_ERROR("RenderControlDevice missing rcEncoder.");
            return -1;
        }

        mHostConnection->lock();
        rc->rcCompose(rc, bytes.size(), bytes.data());
        mHostConnection->unlock();
        return 0;
    }

   private:
    std::unique_ptr<HostConnection> mHostConnection;
};

RenderControlDevice* ToHandle(RenderControlDeviceImpl* device) {
    return reinterpret_cast<RenderControlDevice*>(device);
}

RenderControlDeviceImpl* ToImpl(RenderControlDevice* device) {
    return reinterpret_cast<RenderControlDeviceImpl*>(device);
}

}  // namespace

extern "C" __attribute__((visibility("default"))) RenderControlDevice* rcCreateDevice() {
    return ToHandle(new RenderControlDeviceImpl());
}

extern "C" __attribute__((visibility("default"))) void rcDestroyDevice(
    RenderControlDevice* pDevice) {
    if (pDevice != nullptr) {
        delete ToImpl(pDevice);
    }
}

extern "C" __attribute__((visibility("default"))) int rcCompose(
    RenderControlDevice* deviceHandle, const RenderControlComposition* pComposition,
    uint32_t compositionLayerCount, const RenderControlCompositionLayer* pCompositionLayers) {
    const size_t bytesNeeded =
        sizeof(ComposeDevice_v2) + (sizeof(ComposeLayer) * compositionLayerCount);
    std::vector<uint8_t> bytes(bytesNeeded);

    auto* composeDevice = reinterpret_cast<ComposeDevice_v2*>(bytes.data());
    auto* composeLayers = composeDevice->layer;

    composeDevice->version = 2;
    composeDevice->displayId = 0;
    composeDevice->targetHandle = pComposition->compositionResultColorBufferHandle;
    composeDevice->numLayers = compositionLayerCount;

    for (uint32_t i = 0; i < compositionLayerCount; i++) {
        auto& requestLayer = pCompositionLayers[i];
        auto& composeLayer = composeLayers[i];
        composeLayer.cbHandle = requestLayer.colorBufferHandle;
        composeLayer.composeMode = requestLayer.composeMode;
        composeLayer.displayFrame = requestLayer.displayFrame;
        composeLayer.crop = requestLayer.crop;
        composeLayer.blendMode = requestLayer.blendMode;
        composeLayer.alpha = requestLayer.alpha;
        composeLayer.color = requestLayer.color;
        composeLayer.transform = requestLayer.transform;
    }

    RenderControlDeviceImpl* device = ToImpl(deviceHandle);
    return device->DoCompose(bytes);
}
