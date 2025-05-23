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

#include <hardware/hwcomposer2.h>

struct RenderControlDevice;

typedef RenderControlDevice* (*PFN_rcCreateDevice)();

typedef void (*PFN_rcDestroyDevice)(RenderControlDevice*);

struct RenderControlCompositionLayer {
    uint32_t colorBufferHandle = 0;
    hwc2_composition_t composeMode;
    hwc_rect_t displayFrame;
    hwc_frect_t crop;
    int32_t blendMode;
    float alpha;
    hwc_color_t color;
    hwc_transform_t transform;
};

struct RenderControlComposition {
    uint32_t displayId = 0;
    uint32_t compositionResultColorBufferHandle = 0;
};

typedef int (*PFN_rcCompose)(RenderControlDevice* device,
                             const RenderControlComposition* composition, uint32_t layerCount,
                             const RenderControlCompositionLayer* pLayers);
