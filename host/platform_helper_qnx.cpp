/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2026 BlackBerry Limited
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

#include "platform_helper_qnx.h"

#include <pthread.h>

#include "gfxstream/common/logging.h"

static pthread_once_t once_control = PTHREAD_ONCE_INIT;
static screen_context_t g_screen_ctx;

static void screen_init(void) {
    /* initialize the global screen context */
    screen_create_context(&g_screen_ctx, SCREEN_APPLICATION_CONTEXT);
}

static inline int getScreenFormat(GfxstreamFormat format) {
    switch (format) {
        case GfxstreamFormat::B8G8R8A8_UNORM:
            return SCREEN_FORMAT_RGBA8888;
        case GfxstreamFormat::R8G8B8A8_UNORM:
            return SCREEN_FORMAT_BGRA8888;
        case GfxstreamFormat::R8G8B8X8_UNORM:
            return SCREEN_FORMAT_BGRX8888;
        case GfxstreamFormat::R8_UNORM:
            return SCREEN_FORMAT_BYTE;
        default:
            return -1;
    }
}

namespace gfxstream {
namespace qnx {

screen_context_t getScreenContext() {
    pthread_once(&once_control, screen_init);
    return g_screen_ctx;
}

#define ASSERT_SCREEN_FUNC(func)                                                \
    do {                                                                        \
        int rc = (func);                                                        \
        if (rc != EOK) {                                                        \
            GFXSTREAM_ERROR("Failed QNX Screen API call: %s", strerror(errno)); \
            return std::nullopt;                                                \
        }                                                                       \
    } while (0)

std::optional<std::pair<screen_stream_t, screen_buffer_t>> createScreenStreamBuffer(
    int width, int height, GfxstreamFormat format, std::string bufferName) {
    screen_stream_t screen_stream = NULL;
    ASSERT_SCREEN_FUNC(screen_create_stream(&screen_stream, getScreenContext()));
    if (!screen_stream) {
        GFXSTREAM_ERROR("Could not create screen_stream_t");
        return std::nullopt;
    }

    const int screenUsage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_OPENGL_ES2 |
                            SCREEN_USAGE_OPENGL_ES3 | SCREEN_USAGE_VULKAN;
    ASSERT_SCREEN_FUNC(
        screen_set_stream_property_iv(screen_stream, SCREEN_PROPERTY_USAGE, &screenUsage));

    int screenFormat = getScreenFormat(format);
    if (screenFormat <= 0) {
        GFXSTREAM_ERROR("Could not create screen_stream_t");
        return std::nullopt;
    }
    ASSERT_SCREEN_FUNC(
        screen_set_stream_property_iv(screen_stream, SCREEN_PROPERTY_FORMAT, &screenFormat));

    int size[] = {width, height};
    ASSERT_SCREEN_FUNC(
        screen_set_stream_property_iv(screen_stream, SCREEN_PROPERTY_BUFFER_SIZE, size));

    ASSERT_SCREEN_FUNC(screen_set_stream_property_cv(screen_stream, SCREEN_PROPERTY_ID_STRING,
                                                     bufferName.length(), bufferName.c_str()));

    int rc = screen_create_stream_buffers(screen_stream, 1);
    if (rc) {
        GFXSTREAM_ERROR(
            "Could not create buffer for screen_stream_t (usage=0x%x, id_str=%s, width=%d, "
            "height=%d, format=%d)\n",
            screenUsage, bufferName.c_str(), width, height, format);
        return std::nullopt;
    }

    screen_buffer_t stream_buffer;
    ASSERT_SCREEN_FUNC(screen_get_stream_property_pv(screen_stream, SCREEN_PROPERTY_BUFFERS,
                                                     (void**)&stream_buffer));

    return std::make_optional(std::make_pair(screen_stream, stream_buffer));
}

}  // namespace qnx
}  // namespace gfxstream
