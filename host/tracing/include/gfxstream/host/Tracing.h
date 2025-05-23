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

// Simple wrapper around Perfetto tracing that allows for building
// without tracing.

#pragma once

#include <stdint.h>

#define GFXSTREAM_TRACE_DEFAULT_CATEGORY "gfxstream.default"
#define GFXSTREAM_TRACE_DECODER_CATEGORY "gfxstream.decoder"
#define GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY "gfxstream.stream_renderer"
#define GFXSTREAM_TRACE_VIRTIO_GPU_TIMELINE_CATEGORY "gfxstream.virtio_gpu_timeline"

#ifdef GFXSTREAM_BUILD_WITH_TRACING

#ifdef GFXSTREAM_BUILD_WITH_PERFETTO_SDK
#include <perfetto.h>
#else
#include <perfetto/tracing.h>
#endif

PERFETTO_DEFINE_CATEGORIES(perfetto::Category(GFXSTREAM_TRACE_DEFAULT_CATEGORY)
                               .SetDescription("Default events")
                               .SetTags("default"),
                           perfetto::Category(GFXSTREAM_TRACE_DECODER_CATEGORY)
                               .SetDescription("Decoder events")
                               .SetTags("decoder"),
                           perfetto::Category(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY)
                               .SetDescription("Gfxstream frontend command events")
                               .SetTags("stream-renderer"),
                           perfetto::Category(GFXSTREAM_TRACE_VIRTIO_GPU_TIMELINE_CATEGORY)
                               .SetDescription("Virtio GPU fence timeline events")
                               .SetTags("virtio-gpu"));

#define GFXSTREAM_TRACE_EVENT(...) TRACE_EVENT(__VA_ARGS__)
#define GFXSTREAM_TRACE_EVENT_INSTANT(...) TRACE_EVENT_INSTANT(__VA_ARGS__)

#define GFXSTREAM_TRACE_FLOW(id) perfetto::Flow::ProcessScoped(id)

#define GFXSTREAM_TRACE_TRACK_FOR_CURRENT_THREAD() perfetto::ThreadTrack::Current()
#define GFXSTREAM_TRACE_TRACK(id) perfetto::Track(id)

#define GFXSTREAM_TRACE_NAME_TRACK(track, name)                           \
    do {                                                                  \
        auto trackDescriptor = track.Serialize();                         \
        trackDescriptor.set_name(name);                                   \
        perfetto::TrackEvent::SetTrackDescriptor(track, trackDescriptor); \
    } while (0)

#else

#define GFXSTREAM_TRACE_EVENT(...)
#define GFXSTREAM_TRACE_EVENT_INSTANT(...)

#define GFXSTREAM_TRACE_FLOW(id)

#define GFXSTREAM_TRACE_TRACK_FOR_CURRENT_THREAD()
#define GFXSTREAM_TRACE_TRACK(id)
#define GFXSTREAM_TRACE_NAME_TRACK(track, name)

#endif  // GFXSTREAM_BUILD_WITH_TRACING

namespace gfxstream {
namespace host {

uint64_t GetUniqueTracingId();

void InitializeTracing();

}  // namespace host
}  // namespace gfxstream
