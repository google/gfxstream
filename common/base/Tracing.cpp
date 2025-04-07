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

#ifdef USE_PERFETTO_TRACING
#include "perfetto-tracing-only.h"
#endif

#include <string>
#include <vector>

#include <fcntl.h>

namespace android {
namespace base {

const bool* tracingDisabledPtr = nullptr;

void initializeTracing() {
#ifdef USE_PERFETTO_TRACING
    virtualdeviceperfetto::initialize(&tracingDisabledPtr);
#endif
}

void enableTracing() {
#ifdef USE_PERFETTO_TRACING
    if (virtualdeviceperfetto::queryTraceConfig().tracingDisabled) {
        virtualdeviceperfetto::enableTracing();
    }
#endif
}

void disableTracing() {
#ifdef USE_PERFETTO_TRACING
    if (!virtualdeviceperfetto::queryTraceConfig().tracingDisabled) {
        virtualdeviceperfetto::disableTracing();
    }
#endif
}

bool shouldEnableTracing() {
#ifdef USE_PERFETTO_TRACING
    return !(virtualdeviceperfetto::queryTraceConfig().tracingDisabled);
#else
    return false;
#endif
}

#ifdef __cplusplus
#   define CC_LIKELY( exp )    (__builtin_expect( !!(exp), true ))
#   define CC_UNLIKELY( exp )  (__builtin_expect( !!(exp), false ))
#else
#   define CC_LIKELY( exp )    (__builtin_expect( !!(exp), 1 ))
#   define CC_UNLIKELY( exp )  (__builtin_expect( !!(exp), 0 ))
#endif

__attribute__((always_inline)) void beginTrace(const char* name) {
    if (CC_LIKELY(!tracingDisabledPtr)) return;
#ifdef USE_PERFETTO_TRACING
    virtualdeviceperfetto::beginTrace(name);
#endif
}

__attribute__((always_inline)) void endTrace() {
    if (CC_LIKELY(!tracingDisabledPtr)) return;
#ifdef USE_PERFETTO_TRACING
    virtualdeviceperfetto::endTrace();
#endif
}

__attribute__((always_inline)) void traceCounter(const char* name, int64_t value) {
    if (CC_LIKELY(!tracingDisabledPtr)) return;
#ifdef USE_PERFETTO_TRACING
    virtualdeviceperfetto::traceCounter(name, value);
#endif
}

ScopedTrace::ScopedTrace(const char* name) {
    if (CC_LIKELY(!tracingDisabledPtr)) return;
#ifdef USE_PERFETTO_TRACING
    virtualdeviceperfetto::beginTrace(name);
#endif
}

ScopedTrace::~ScopedTrace() {
    if (CC_LIKELY(!tracingDisabledPtr)) return;
#ifdef USE_PERFETTO_TRACING
    virtualdeviceperfetto::endTrace();
#endif
}

void setGuestTime(uint64_t t) {
#ifdef USE_PERFETTO_TRACING
    virtualdeviceperfetto::setGuestTime(t);
#endif
}

} // namespace base
} // namespace android
