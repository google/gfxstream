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
#include "NativeSubWindow.h"
#include <android/native_window.h>

#include <stdio.h>

EGLNativeWindowType createSubWindow(FBNativeWindowType p_window,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    float dpr,
                                    SubWindowRepaintCallback repaint_callback,
                                    void* repaint_callback_param,
                                    int hideWindow) {
    ANativeWindow_acquire(p_window);
    ANativeWindow_setBuffersGeometry(p_window, width, height, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    return p_window;
}

void destroySubWindow(EGLNativeWindowType win) {
    ANativeWindow_release(win);
}

int moveSubWindow(FBNativeWindowType p_parent_window,
                  EGLNativeWindowType p_sub_window,
                  int x,
                  int y,
                  int width,
                  int height,
                  float dpr) {
    // moving windows not supported in Android; we can't create an actual sub window
    return true;
}
