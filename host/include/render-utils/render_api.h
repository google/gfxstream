/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#pragma once

#include "render-utils/render_api_functions.h"

// All interfaces which can fail return an int, with zero indicating failure
// and anything else indicating success.

#ifndef USING_ANDROID_BP
#ifdef __cplusplus
extern "C" {
#endif
#endif

namespace gfxstream {

#ifdef _WIN32
#define RENDER_API_EXPORT __declspec(dllexport)
#else
#define RENDER_API_EXPORT __attribute__((visibility("default")))
#endif

#define RENDER_API_DECLARE(return_type, func_name, signature, callargs) \
    typedef return_type (*func_name ## Fn) signature; \
    RENDER_API_EXPORT return_type func_name signature;

LIST_RENDER_API_FUNCTIONS(RENDER_API_DECLARE)

RENDER_API_EXPORT RenderLibPtr initLibrary();

}  // namespace gfxstream

#ifndef USING_ANDROID_BP
#ifdef __cplusplus
} // extern "C"
#endif
#endif
