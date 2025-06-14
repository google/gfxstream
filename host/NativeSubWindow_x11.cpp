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

#include <stdio.h>

#include "gfxstream/host/X11Support.h"

namespace {

static Bool WaitForMapNotify(Display *d, XEvent *e, char *arg) {
    if (e->type == MapNotify && e->xmap.window == (Window)arg) {
        return 1;
    }
    return 0;
}

static Bool WaitForConfigureNotify(Display *d, XEvent *e, char *arg) {
    if (e->type == ConfigureNotify && e->xmap.window == (Window)arg) {
        return 1;
    }
    return 0;
}

static Display *s_display = NULL;

}  // namespace

EGLNativeWindowType createSubWindow(FBNativeWindowType p_window,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    float dpr,
                                    SubWindowRepaintCallback repaint_callback,
                                    void* repaint_callback_param,
                                    int hideWindow) {
    auto x11 = getX11Api();
   // The call to this function is protected by a lock
   // in FrameBuffer so it is safe to check and initialize s_display here
   if (!s_display) {
       s_display = x11->XOpenDisplay(NULL);
   }

    XSetWindowAttributes wa;
    wa.event_mask = StructureNotifyMask;
    wa.override_redirect = True;
    Window win = x11->XCreateWindow(s_display,
            p_window,
            x * dpr,
            y * dpr,
            width * dpr,
            height * dpr,
            0,
            CopyFromParent,
            CopyFromParent,
            CopyFromParent,
            CWEventMask,
            &wa);
    if (!hideWindow) {
        x11->XMapWindow(s_display,win);
        x11->XSetWindowBackground(s_display, win, 0);
        XEvent e;
        x11->XIfEvent(s_display, &e, WaitForMapNotify, (char *)win);
    }
    return win;
}

void destroySubWindow(EGLNativeWindowType win) {
    if (!s_display) {
        return;
    }

    getX11Api()->XDestroyWindow(s_display, win);
}

int moveSubWindow(FBNativeWindowType p_parent_window,
                  EGLNativeWindowType p_sub_window,
                  int x,
                  int y,
                  int width,
                  int height,
                  float dpr) {
    // This value is set during create, so if it is still null, simply
    // return because the global state is corrupted
    if (!s_display) {
        return false;
    }

    x *= dpr;
    y *= dpr;
    width *= dpr;
    height *= dpr;

    auto x11 = getX11Api();

    // Make sure something has changed, otherwise XIfEvent will block and
    // freeze the emulator.
    XWindowAttributes attrs;
    if (!x11->XGetWindowAttributes(s_display, p_sub_window, &attrs)) {
        return false;
    }
    if (x == attrs.x && y == attrs.y &&
        width == attrs.width && height == attrs.height) {
        // Technically, resizing was a success because it was unneeded.
        return true;
    }

    // This prevents flicker on resize.
    x11->XSetWindowBackgroundPixmap(s_display, p_sub_window, None);

    int ret = x11->XMoveResizeWindow(
            s_display,
            p_sub_window,
            x,
            y,
            width,
            height);

    XEvent e;
    x11->XIfEvent(s_display, &e, WaitForConfigureNotify, (char *)p_sub_window);

    return ret;
}
