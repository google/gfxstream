// Copyright 2020 The Android Open Source Project
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

#include <stdbool.h>
#include <stdint.h>

#include "aemu/base/c_header.h"
#include "aemu/base/export.h"

ANDROID_BEGIN_HEADER

// Enum tracking all current available renderer backends
// for the emulator.
typedef enum SelectedRenderer {
    SELECTED_RENDERER_UNKNOWN = 0,
    SELECTED_RENDERER_HOST = 1,
    SELECTED_RENDERER_OFF = 2,
    SELECTED_RENDERER_GUEST = 3,
    SELECTED_RENDERER_MESA = 4,
    SELECTED_RENDERER_SWIFTSHADER = 5,
    SELECTED_RENDERER_ANGLE = 6, // ANGLE D3D11 with D3D9 fallback
    SELECTED_RENDERER_ANGLE9 = 7, // ANGLE forced to D3D9
    SELECTED_RENDERER_SWIFTSHADER_INDIRECT = 8,
    SELECTED_RENDERER_ANGLE_INDIRECT = 9,
    SELECTED_RENDERER_ANGLE9_INDIRECT = 10,
    SELECTED_RENDERER_ERROR = 255,
} SelectedRenderer;

enum GrallocImplementation { MINIGBM, GOLDFISH_GRALLOC };

ANDROID_END_HEADER
