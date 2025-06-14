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

#include <stdint.h>

#include "KHR/khrplatform.h"
#include "OpenGLESDispatch/gldefs.h"
#include "OpenGLESDispatch/gles2_extensions_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles2_only_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles31_only_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles32_only_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles3_extensions_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles3_only_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles_common_for_gles2_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles_extensions_for_gles2_static_translator_namespaced_header.h"
#include "OpenGLESDispatch/gles_functions.h"
#include "gfxstream/host/gl_enums.h"

namespace gfxstream {
namespace gl {

// Define function pointer types.
#define GLES2_DISPATCH_DEFINE_TYPE(return_type,func_name,signature,callargs) \
    typedef return_type (KHRONOS_APIENTRY * func_name ## _t) signature;

LIST_GLES2_FUNCTIONS(GLES2_DISPATCH_DEFINE_TYPE,GLES2_DISPATCH_DEFINE_TYPE)

struct GLESv2Dispatch {
#define GLES2_DISPATCH_DECLARE_POINTER(return_type,func_name,signature,callargs) \
        func_name ## _t func_name;
    LIST_GLES2_FUNCTIONS(GLES2_DISPATCH_DECLARE_POINTER,
                         GLES2_DISPATCH_DECLARE_POINTER)

    bool initialized = false;
};

#undef GLES2_DISPATCH_DECLARE_POINTER
#undef GLES2_DISPATCH_DEFINE_TYPE

bool gles2_dispatch_init(GLESv2Dispatch* dispatch_table);

// Used to initialize the decoder.
void* gles2_dispatch_get_proc_func(const char* name, void* userData);
// Used to check for unimplemented.
void gles2_unimplemented();

GLESDispatchMaxVersion gles2_dispatch_get_max_version();

}  // namespace gl
}  // namespace gfxstream