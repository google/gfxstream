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

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace gfxstream {

class DisplaySurface;
class DisplaySurfaceUser;

// Base class used for controlling the lifetime of a particular surface
// used for a display (e.g. EGLSurface or VkSurfaceKHR).
class DisplaySurfaceImpl {
  public:
    virtual ~DisplaySurfaceImpl() {}
};

class DisplaySurface {
  public:
    DisplaySurface(uint32_t width,
                   uint32_t height,
                   std::unique_ptr<DisplaySurfaceImpl> impl);
    ~DisplaySurface();

    DisplaySurface(const DisplaySurface&) = delete;
    DisplaySurface& operator=(const DisplaySurface&) = delete;

    // Return the API specific implementation of a DisplaySurface. This
    // should only be called by API specific components such as DisplayGl
    // or DisplayVk.
    const DisplaySurfaceImpl* getImpl() const { return mImpl.get(); }

    uint32_t getWidth() const;
    uint32_t getHeight() const;

    void updateSize(uint32_t newWidth, uint32_t newHeight);

  private:
    friend class DisplaySurfaceUser;

    void registerUser(DisplaySurfaceUser* user);
    void unregisterUser(DisplaySurfaceUser* user);

    mutable std::mutex mParamsMutex;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;

    std::unique_ptr<DisplaySurfaceImpl> mImpl;
    std::unordered_set<DisplaySurfaceUser*> mBoundUsers;
};

}  // namespace gfxstream
