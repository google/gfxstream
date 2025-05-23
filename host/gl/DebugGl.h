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
#pragma once

#include <string>

#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace gl {

std::string formatString(const char* format, ...);

class ScopedDebugGroup {
  public:
    ScopedDebugGroup(const std::string& message);
    ~ScopedDebugGroup();
};

#ifdef ENABLE_GL_LOG
#define GL_SCOPED_DEBUG_GROUP(...) ScopedDebugGroup sdg_ ## __LINE__(formatString(__VA_ARGS__))
#else
#define GL_SCOPED_DEBUG_GROUP(...) (void(0))
#endif

}  // namespace gl
}  // namespace gfxstream
