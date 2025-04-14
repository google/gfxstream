// Copyright (C) 2022 The Android Open Source Project
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

#include <memory>

#include "gfxstream/host/display.h"
#include "gfxstream/host/display_surface.h"
#include "gfxstream/host/logging.h"

namespace gfxstream {

DisplaySurfaceUser::~DisplaySurfaceUser() {
    if (mBoundSurface != nullptr) {
        GFXSTREAM_FATAL("Failed to unbind a DisplaySurface before DisplaySurfaceUser destruction.");
    }
}

void DisplaySurfaceUser::bindToSurface(DisplaySurface* surface) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mBoundSurface != nullptr) {
        GFXSTREAM_FATAL("Attempting to bind a DisplaySurface while another is already bound.");
    }

    this->bindToSurfaceImpl(surface);
    surface->registerUser(this);
    mBoundSurface = surface;
}

void DisplaySurfaceUser::unbindFromSurface() {
    std::lock_guard<std::mutex> lock(mMutex);
    this->unbindFromSurfaceImpl();
    if (mBoundSurface != nullptr) {
        mBoundSurface->unregisterUser(this);
        mBoundSurface = nullptr;
    }
}

}  // namespace gfxstream
