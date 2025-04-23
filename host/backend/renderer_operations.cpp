// Copyright (C) 2025 The Android Open Source Project
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

#include "gfxstream/host/renderer_operations.h"

namespace gfxstream {
namespace {

static SelectedRenderer sRenderer = SELECTED_RENDERER_HOST;

static int sGlesMajorVersion = 2;
static int sGlesMinorVersion = 0;

static bool sShouldSkipDraw = false;

}  // namespace

void set_gfxstream_renderer(SelectedRenderer renderer) {
    sRenderer = renderer;
}

SelectedRenderer get_gfxstream_renderer() {
    return sRenderer;
}

void set_gfxstream_gles_version(int maj, int min) {
    sGlesMajorVersion = maj;
    sGlesMinorVersion = min;
}

void get_gfxstream_gles_version(int* maj, int* min) {
    if (maj) {
        *maj = sGlesMajorVersion;
    }
    if (min) {
        *min = sGlesMinorVersion;
    }
}

void set_gfxstream_should_skip_draw(bool skip) {
    sShouldSkipDraw = skip;
}

bool get_gfxstream_should_skip_draw() {
    return sShouldSkipDraw;
}

}  // namespace gfxstream
