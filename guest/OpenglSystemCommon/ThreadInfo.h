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
#ifndef _THREAD_INFO_H
#define _THREAD_INFO_H

#include "HostConnection.h"

#include <inttypes.h>

struct EGLContext_t;

typedef bool (*tlsDtorCallback)(void*);

struct EGLThreadInfo
{
    EGLThreadInfo() : currentContext(NULL), eglError(EGL_SUCCESS), dtor(0) {}
    ~EGLThreadInfo() { if (dtor) dtor(this); }

    EGLContext_t *currentContext;
    std::unique_ptr<HostConnection> hostConn;
    int           eglError;
    tlsDtorCallback dtor;
};

void setTlsDestructor(tlsDtorCallback);

extern "C" __attribute__((visibility("default"))) EGLThreadInfo *goldfish_get_egl_tls();

EGLThreadInfo* getEGLThreadInfo();

#endif // of _THREAD_INFO_H
