// Copyright (C) 2019 The Android Open Source Project
// Copyright (C) 2019 Google Inc.
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
#include "aemu/base/Tracing.h"

#if defined(__ANDROID__)

#include <cutils/trace.h>
#define TRACE_TAG ATRACE_TAG_GRAPHICS

#elif defined(__Fuchsia__) && !defined(FUCHSIA_NO_TRACE)

#include <lib/trace/event.h>
#define TRACE_TAG "gfx"

#else
#endif

namespace gfxstream {
namespace guest {

bool isTracingEnabled() {
    // TODO: Fuchsia + Linux
    return false;
}

void ScopedTraceGuest::beginTraceImpl(const char* name) {
    // No-op
    (void)name;
}

void ScopedTraceGuest::endTraceImpl(const char* name) {
    // No-op
    (void)name;
}

} // namespace base
} // namespace gfxstream
