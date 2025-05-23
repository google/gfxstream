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
#ifndef _EGL_CONTEXT_H
#define _EGL_CONTEXT_H

#include "gfxstream/guest/GLClientState.h"
#include "gfxstream/guest/GLSharedGroup.h"

#include <string>
#include <vector>

struct EGLContext_t {

    enum {
        IS_CURRENT      =   0x00010000,
        NEVER_CURRENT   =   0x00020000
    };

    EGLContext_t(EGLDisplay dpy, EGLConfig config, EGLContext_t* shareCtx, int maj, int min);
    ~EGLContext_t();
    uint32_t            flags;
    EGLDisplay          dpy;
    EGLConfig           config;
    EGLSurface          read;
    EGLSurface          draw;
    EGLSurface          dummy_surface;
    EGLContext_t    *   shareCtx;
    uint32_t            rcContext;
    const char*         versionString;
    EGLint              majorVersion;
    EGLint              minorVersion;
    EGLint              deviceMajorVersion;
    EGLint              deviceMinorVersion;
    const char*         vendorString;
    const char*         rendererString;
    const char*         shaderVersionString;
    const char*         extensionString;
    std::vector<std::string> extensionStringArray;
    EGLint              deletePending;
    gfxstream::guest::GLClientState * getClientState(){ return clientState; }
    gfxstream::guest::GLSharedGroupPtr getSharedGroup(){ return sharedGroup; }
    int getGoldfishSyncFd();
private:
    gfxstream::guest::GLClientState    *    clientState;
    gfxstream::guest::GLSharedGroupPtr      sharedGroup;
    int goldfishSyncFd;
};

#endif
