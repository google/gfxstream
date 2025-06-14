// Copyright 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stddef.h>

namespace gfxstream {
namespace base {

// Return true iff the content of |line|, or |lineLen| characters,
// matches an HTTP request definition. See section 5.1 or RFC 2616.
bool httpIsRequestLine(const char* line, size_t lineLen);

}  // namespace base
}  // namespace android
