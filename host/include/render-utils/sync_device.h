// Copyright 2025 The Android Open Source Project
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

#include <cstdint>

typedef uint64_t (*gfxstream_sync_create_timeline_t)();
typedef int (*gfxstream_sync_create_fence_t)(uint64_t timeline, uint32_t pt);
typedef void (*gfxstream_sync_timeline_inc_t)(uint64_t timeline, uint32_t howmuch);
typedef void (*gfxstream_sync_destroy_timeline_t)(uint64_t timeline);
typedef void (*gfxstream_sync_trigger_wait_t)(uint64_t glsync, uint64_t thread, uint64_t timeline);
typedef void (*gfxstream_sync_register_trigger_wait_t)(gfxstream_sync_trigger_wait_t trigger_fn);
typedef bool (*gfxstream_sync_device_exists_t)();
