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

#include "aemu/base/GLObjectCounter.h"
#include "render-utils/gralloc_enums.h"
#include "render-utils/renderer_enums.h"

#ifdef _MSC_VER
# ifdef BUILDING_EMUGL_COMMON_SHARED
#  define EMUGL_COMMON_API __declspec(dllexport)
# else
#  define EMUGL_COMMON_API __declspec(dllimport)
#endif
#else
# define EMUGL_COMMON_API
#endif

namespace android {

namespace base {

class GLObjectCounter;

} // namespace base
} // namespace android

namespace emugl {
    // Set/get GLES major/minor version.
    EMUGL_COMMON_API void setGlesVersion(int maj, int min);
    EMUGL_COMMON_API void getGlesVersion(int* maj, int* min);

    // Set/get renderer
    EMUGL_COMMON_API void setRenderer(SelectedRenderer renderer);
    EMUGL_COMMON_API SelectedRenderer getRenderer();

    // Extension string query
    EMUGL_COMMON_API bool hasExtension(const char* extensionsStr,
                      const char* wantedExtension);

    // GL object counter get/set
    EMUGL_COMMON_API void setGLObjectCounter(
            android::base::GLObjectCounter* counter);
    EMUGL_COMMON_API android::base::GLObjectCounter* getGLObjectCounter();

    // Gralloc implementation get/set
    EMUGL_COMMON_API void setGrallocImplementation(
            GrallocImplementation gralloc);
    EMUGL_COMMON_API GrallocImplementation getGrallocImplementation();
}
