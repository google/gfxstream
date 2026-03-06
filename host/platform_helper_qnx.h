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
#ifndef PLATFORM_HELPER_QNX_H
#define PLATFORM_HELPER_QNX_H

#include <screen/screen.h>

#include <optional>

#include "gfxstream/host/external_object_manager.h"
#include "gfxstream/host/gfxstream_format.h"

#ifdef __cplusplus
extern "C" {
#endif

using namespace gfxstream::host;

namespace gfxstream {
namespace qnx {

screen_context_t getScreenContext();
std::optional<std::pair<screen_stream_t, screen_buffer_t>> createScreenStreamBuffer(
    int width, int height, GfxstreamFormat format, std::string bufferName);

}  // namespace qnx
}  // namespace gfxstream

#ifdef __cplusplus
}
#endif

#endif
