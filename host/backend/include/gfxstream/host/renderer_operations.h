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

#include "render-utils/renderer_enums.h"

namespace gfxstream {

void set_gfxstream_renderer(SelectedRenderer renderer);
SelectedRenderer get_gfxstream_renderer();

void set_gfxstream_gles_version(int maj, int min);
void get_gfxstream_gles_version(int* maj, int* min);

void set_gfxstream_should_skip_draw(bool skip);
bool get_gfxstream_should_skip_draw();


}  // namespace gfxstream
