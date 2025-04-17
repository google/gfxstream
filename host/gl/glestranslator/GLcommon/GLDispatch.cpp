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

#include "GLcommon/GLDispatch.h"

#include "GLcommon/GLLibrary.h"
#include "aemu/base/SharedLibrary.h"
#include "aemu/base/synchronization/Lock.h"
#include "gfxstream/host/logging.h"

#ifdef __linux__
#include <GL/glx.h>
#elif defined(WIN32)
#include <windows.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unordered_map>

typedef GlLibrary::GlFunctionPointer GL_FUNC_PTR;

static GL_FUNC_PTR getGLFuncAddress(const char *funcName, GlLibrary* glLib) {
    return glLib->findSymbol(funcName);
}

static const std::unordered_map<std::string, std::string> sAliasExtra = {
    {"glDepthRange", "glDepthRangef"},
    {"glDepthRangef", "glDepthRange"},
    {"glClearDepth", "glClearDepthf"},
    {"glClearDepthf", "glClearDepth"},
};

#define LOAD_GL_FUNC(return_type, func_name, signature, args)                             \
    do {                                                                                  \
        if (!func_name) {                                                                 \
            void* address = (void*)getGLFuncAddress(#func_name, glLib);                   \
            /*Check alias*/                                                               \
            if (!address) {                                                               \
                address = (void*)getGLFuncAddress(#func_name "OES", glLib);               \
                if (address) {                                                            \
                    GFXSTREAM_DEBUG("%s not found, using %sOES", #func_name, #func_name); \
                }                                                                         \
            }                                                                             \
            if (!address) {                                                               \
                address = (void*)getGLFuncAddress(#func_name "EXT", glLib);               \
                if (address) {                                                            \
                    GFXSTREAM_DEBUG("%s not found, using %sEXT", #func_name, #func_name); \
                }                                                                         \
            }                                                                             \
            if (!address) {                                                               \
                address = (void*)getGLFuncAddress(#func_name "ARB", glLib);               \
                if (address) {                                                            \
                    GFXSTREAM_DEBUG("%s not found, using %sARB", #func_name, #func_name); \
                }                                                                         \
            }                                                                             \
            if (!address) {                                                               \
                const auto& it = sAliasExtra.find(#func_name);                            \
                if (it != sAliasExtra.end()) {                                            \
                    address = (void*)getGLFuncAddress(it->second.c_str(), glLib);         \
                }                                                                         \
            }                                                                             \
            if (address) {                                                                \
                func_name = (__typeof__(func_name))(address);                             \
            } else {                                                                      \
                GFXSTREAM_VERBOSE("%s not found", #func_name);                            \
                func_name = nullptr;                                                      \
            }                                                                             \
        }                                                                                 \
    } while (0);

#define LOAD_GLEXT_FUNC(return_type, func_name, signature, args) do { \
        if (!func_name) { \
            void* address = (void *)getGLFuncAddress(#func_name, glLib); \
            if (address) { \
                func_name = (__typeof__(func_name))(address); \
            } else { \
                func_name = (__typeof__(func_name))((void*)eglGPA(#func_name)); \
            } \
        } \
    } while (0);

// Define dummy functions, only for non-extensions.

#define RETURN_void return
#define RETURN_GLboolean return GL_FALSE
#define RETURN_GLint return 0
#define RETURN_GLuint return 0U
#define RETURN_GLenum return 0
#define RETURN_int return 0
#define RETURN_GLconstubyteptr return NULL

#define RETURN_(x)  RETURN_ ## x

#define DEFINE_DUMMY_FUNCTION(return_type, func_name, signature, args) \
static return_type dummy_##func_name signature { \
    assert(0); \
    RETURN_(return_type); \
}

#define DEFINE_DUMMY_EXTENSION_FUNCTION(return_type, func_name, signature, args) \
  // nothing here

// Initializing static GLDispatch members*/

android::base::Lock GLDispatch::s_lock;

#define GL_DISPATCH_DEFINE_POINTER(return_type, function_name, signature, args) \
    return_type (*GLDispatch::function_name) signature = NULL;

LIST_GLES_FUNCTIONS(GL_DISPATCH_DEFINE_POINTER, GL_DISPATCH_DEFINE_POINTER)

#if defined(ENABLE_DISPATCH_LOG)

// With dispatch debug logging enabled, the original loaded function pointers
// are moved to the "_underlying" suffixed function pointers and then the non
// suffixed functions pointers are updated to be the "_dispatchDebugLogWrapper"
// suffixed functions from the ".impl" files. For example,
//
// void glEnable_dispatchDebugLogWrapper(GLenum cap) {
//   DISPATCH_DEBUG_LOG("glEnable(cap:%d)", cap);
//   GLDispatch::glEnable_underlying(cap);
// }
//
// GLDispatch::glEnable_underlying = dlsym(lib, "glEnable");
// GLDispatch::glEnable = glEnable_dispatchLoggingWrapper;

#include "OpenGLESDispatch/gles_common_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles_extensions_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles1_only_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles1_extensions_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles2_only_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles2_extensions_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles3_only_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles3_extensions_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles31_only_dispatch_logging_wrappers.impl"
#include "OpenGLESDispatch/gles32_only_dispatch_logging_wrappers.impl"

#define LOAD_GL_FUNC_DEBUG_LOG_WRAPPER(return_type, func_name, signature, args) do { \
        func_name##_underlying = func_name; \
        func_name = func_name##_dispatchLoggingWrapper; \
    } while(0);

#define LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER(return_type, func_name, signature, args) do { \
        func_name##_underlying = func_name; \
        func_name = func_name##_dispatchLoggingWrapper; \
    } while (0);

#define GL_DISPATCH_DEFINE_UNDERLYING_POINTER(return_type, function_name, signature, args) \
    return_type (*GLDispatch::function_name##_underlying) signature = NULL;

LIST_GLES_FUNCTIONS(GL_DISPATCH_DEFINE_UNDERLYING_POINTER, GL_DISPATCH_DEFINE_UNDERLYING_POINTER)

#else

#define LOAD_GL_FUNC_DEBUG_LOG_WRAPPER(return_type, func_name, signature, args)
#define LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER(return_type, func_name, signature, args)

#endif

// Constructor.
GLDispatch::GLDispatch() : m_isLoaded(false) {}

bool GLDispatch::isInitialized() const {
    return m_isLoaded;
}

GLESVersion GLDispatch::getGLESVersion() const {
    return m_version;
}

void GLDispatch::dispatchFuncs(GLESVersion version, GlLibrary* glLib, EGLGetProcAddressFunc eglGPA) {
    android::base::AutoLock mutex(s_lock);
    if(m_isLoaded)
        return;

    /* Loading OpenGL functions which are needed for implementing BOTH GLES 1.1 & GLES 2.0*/
    LIST_GLES_COMMON_FUNCTIONS(LOAD_GL_FUNC)
    LIST_GLES_COMMON_FUNCTIONS(LOAD_GL_FUNC_DEBUG_LOG_WRAPPER)

    LIST_GLES_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC)
    LIST_GLES_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER)

    /* Load both GLES1 and GLES2. On core profile, GLES 1 implementation will
     * require GLES 3 function supports and set version to GLES_3_0. Thus
     * we cannot really tell if the dispatcher is used for GLES1 or GLES2, so
     * let's just load both of them.
     */
    LIST_GLES1_ONLY_FUNCTIONS(LOAD_GL_FUNC)
    LIST_GLES1_ONLY_FUNCTIONS(LOAD_GL_FUNC_DEBUG_LOG_WRAPPER)

    LIST_GLES1_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC)
    LIST_GLES1_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER)

    LIST_GLES2_ONLY_FUNCTIONS(LOAD_GL_FUNC)
    LIST_GLES2_ONLY_FUNCTIONS(LOAD_GL_FUNC_DEBUG_LOG_WRAPPER)

    LIST_GLES2_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC)
    LIST_GLES2_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER)

    /* Load OpenGL ES 3.x functions through 3.1. Not all are supported;
     * leave it up to EGL to determine support level. */

    if (version >= GLES_3_0) {
        LIST_GLES3_ONLY_FUNCTIONS(LOAD_GLEXT_FUNC)
        LIST_GLES3_ONLY_FUNCTIONS(LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER)

        LIST_GLES3_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC)
        LIST_GLES3_EXTENSIONS_FUNCTIONS(LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER)
    }

    if (version >= GLES_3_1) {
        LIST_GLES31_ONLY_FUNCTIONS(LOAD_GLEXT_FUNC)
        LIST_GLES31_ONLY_FUNCTIONS(LOAD_GLEXT_FUNC_DEBUG_LOG_WRAPPER)
    }

    const char* kAngleName = "ANGLE";
    const char* glString = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (glString && 0 == strncmp(glString, kAngleName, strlen(kAngleName))) {
        // ANGLE loads a bad glGetTexImage. (No it is not the dummy.)
        // Overwrite it.
        void* _glGetTexImageANGLE =
                (void*)getGLFuncAddress("glGetTexImageANGLE", glLib);
        if (_glGetTexImageANGLE) {
            glGetTexImage = (__typeof__(
                    glGetTexImage))_glGetTexImageANGLE;
        }
    }

    m_isLoaded = true;
    m_version = version;
}
