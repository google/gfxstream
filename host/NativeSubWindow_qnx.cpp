/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2023 BlackBerry Limited
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
#include <pthread.h>
#include <screen/screen.h>

#include "NativeSubWindow.h"

namespace {

static pthread_once_t once_control = PTHREAD_ONCE_INIT;
static screen_context_t g_screen_ctx;

static void screen_init(void) {
    /* initialize the global screen context */
    screen_create_context(&g_screen_ctx, SCREEN_APPLICATION_CONTEXT);
}

static screen_context_t get_screen_context() {
    pthread_once(&once_control, screen_init);
    return g_screen_ctx;
}

}  // namespace

EGLNativeWindowType createSubWindow(FBNativeWindowType p_window, int x, int y, int width,
                                    int height, float dpr,
                                    SubWindowRepaintCallback repaint_callback,
                                    void* repaint_callback_param, int hideWindow) {
    screen_context_t screen_ctx;
    screen_window_t screen_window;
    int rc;

    screen_ctx = get_screen_context();
    if (screen_ctx == nullptr) {
        perror("No screen context");
        return nullptr;
    }

    int type = p_window ? SCREEN_EMBEDDED_WINDOW : SCREEN_APPLICATION_WINDOW;
    rc = screen_create_window_type(&screen_window, screen_ctx, type);
    if (rc) {
        perror("screen_create_window");
        return nullptr;
    }

    int alpha_mode = SCREEN_NON_PRE_MULTIPLIED_ALPHA;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_ALPHA_MODE, &alpha_mode);

    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_OPENGL_ES2;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_USAGE, &usage);

    int interval = 1;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SWAP_INTERVAL, &interval);

    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_FORMAT, &format);

    int transp = SCREEN_TRANSPARENCY_NONE;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_TRANSPARENCY, &transp);

    int pos[2] = {x, y};
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_POSITION, pos);

    int size[2] = {width, height};
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SIZE, size);
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_BUFFER_SIZE, size);

    rc = screen_create_window_buffers(screen_window, 2);
    if (rc) {
        perror("screen_create_window_buffers");
        screen_destroy_window(screen_window);
        return nullptr;
    }

    if (p_window) {
        char group[64] = {0};
        rc = screen_get_window_property_cv(p_window, SCREEN_PROPERTY_ID, sizeof(group), group);
        if (!rc && group[0]) {
            rc = screen_join_window_group(screen_window, group);
        }
        if (rc || !group[0]) {
            perror("group");
        }
    } else {
        // TODO: yodai create window group?
    }

    int sensitivity = SCREEN_SENSITIVITY_TEST;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SENSITIVITY, &sensitivity);

    int visible = hideWindow ? 0 : 1;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_VISIBLE, &visible);

    rc = screen_flush_context(screen_ctx, SCREEN_WAIT_IDLE);
    if (rc) {
        perror("flush");
    }

    return screen_window;
}

void destroySubWindow(EGLNativeWindowType win) { screen_destroy_window(win); }

int moveSubWindow(FBNativeWindowType p_parent_window, EGLNativeWindowType p_sub_window, int x,
                  int y, int width, int height, float dpr) {
    int pos[2] = {x, y};
    if (screen_set_window_property_iv(p_sub_window, SCREEN_PROPERTY_POSITION, pos)) {
        return 0;
    }
    int size[2] = {width, height};
    if (screen_set_window_property_iv(p_sub_window, SCREEN_PROPERTY_SIZE, size)) {
        return 0;
    }
    return screen_flush_context(get_screen_context(), 0) == EOK;
}
