/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "DebugGl.h"

#include <cstdarg>

#include "OpenGLESDispatch/DispatchTables.h"

namespace gfxstream {
namespace gl {

std::string formatString(const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, 1024, format, args);
    std::string ret(buf);
    va_end(args);
    return ret;
}

ScopedDebugGroup::ScopedDebugGroup(const std::string& message) {
    s_gles2.glGetError();

    bool groupPushed = false;
    if (s_gles2.glPushDebugGroupKHR) {
        s_gles2.glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 0, message.size(),
                                    message.c_str());
        groupPushed = s_gles2.glGetError() == GL_NO_ERROR;
    }
    if (s_gles2.glPushDebugGroup && !groupPushed) {
        s_gles2.glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, message.size(), message.c_str());
        groupPushed = s_gles2.glGetError() == GL_NO_ERROR;
    }
}

ScopedDebugGroup::~ScopedDebugGroup() {
    s_gles2.glGetError();

    bool groupPopped = false;
    if (s_gles2.glPopDebugGroupKHR) {
        s_gles2.glPopDebugGroupKHR();
        groupPopped = s_gles2.glGetError() == GL_NO_ERROR;
    }
    if (s_gles2.glPopDebugGroup && !groupPopped) {
        s_gles2.glPopDebugGroup();
        groupPopped = s_gles2.glGetError() == GL_NO_ERROR;
    }
}

}  // namespace gl
}  // namespace gfxstream