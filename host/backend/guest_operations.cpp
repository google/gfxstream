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

#include "gfxstream/host/guest_operations.h"

namespace gfxstream {
namespace {

static int sGuestAndroidApiLevel = -1;
static GrallocImplementation sGuestAndroidGralloc = MINIGBM;

}  // namespace

void set_gfxstream_guest_android_api_level(int level) {
    sGuestAndroidApiLevel = level;
}

int get_gfxstream_guest_android_api_level() {
    return sGuestAndroidApiLevel;
}

void set_gfxstream_guest_android_gralloc(GrallocImplementation gralloc) {
    sGuestAndroidGralloc = gralloc;
}

GrallocImplementation get_gfxstream_guest_android_gralloc() {
    return sGuestAndroidGralloc;
}

}  // namespace gfxstream
