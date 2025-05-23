// Copyright (C) 2024 The Android Open Source Project
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

#include "gfxstream/host/Tracing.h"

#include <atomic>
#include <mutex>

#if defined(GFXSTREAM_BUILD_WITH_TRACING)

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

#endif  // defined(GFXSTREAM_BUILD_WITH_TRACING)

namespace gfxstream {
namespace host {

void InitializeTracing() {
#if defined(GFXSTREAM_BUILD_WITH_TRACING)
    [[clang::no_destroy]] static std::once_flag sOnceFlag;
    std::call_once(sOnceFlag, [](){
        perfetto::TracingInitArgs args;
        args.backends |= perfetto::kSystemBackend;
        perfetto::Tracing::Initialize(args);
        perfetto::TrackEvent::Register();
    });
#endif  // defined(GFXSTREAM_BUILD_WITH_TRACING)
}

uint64_t GetUniqueTracingId() {
    static std::atomic<uint64_t> sNextId{4194304};
    return sNextId++;
}

}  // namespace host
}  // namespace gfxstream