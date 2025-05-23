/*
* Copyright (C) 2023 The Android Open Source Project
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

#include <EGL/egl.h>

#include "gfxstream/host/X11Support.h"
#include "gfxstream/synchronization/Lock.h"

class X11ErrorHandler {
  public:
    X11ErrorHandler(EGLNativeDisplayType dpy);
    ~X11ErrorHandler();
    int getLastError() const { return s_lastErrorCode; }

  private:
    static int s_lastErrorCode;
    EGLNativeDisplayType m_dpy;
    int (*m_oldErrorHandler)(Display *, XErrorEvent *) = nullptr;
    static gfxstream::base::Lock s_lock;
    static int errorHandlerProc(Display* dpy, XErrorEvent* event);
};
