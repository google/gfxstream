// Copyright (C) 2025 The Android Open Source Project
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

#include "render-utils/sync_device.h"

namespace gfxstream {

extern gfxstream_sync_create_timeline_t gfxstream_sync_create_timeline;
extern gfxstream_sync_create_fence_t gfxstream_sync_create_fence;
extern gfxstream_sync_timeline_inc_t gfxstream_sync_timeline_inc;
extern gfxstream_sync_destroy_timeline_t gfxstream_sync_destroy_timeline;
extern gfxstream_sync_register_trigger_wait_t gfxstream_sync_register_trigger_wait;
extern gfxstream_sync_device_exists_t gfxstream_sync_device_exists;

void set_gfxstream_sync_create_timeline(gfxstream_sync_create_timeline_t);
void set_gfxstream_sync_create_fence(gfxstream_sync_create_fence_t);
void set_gfxstream_sync_timeline_inc(gfxstream_sync_timeline_inc_t);
void set_gfxstream_sync_destroy_timeline(gfxstream_sync_destroy_timeline_t);
void set_gfxstream_sync_register_trigger_wait(gfxstream_sync_register_trigger_wait_t trigger_fn);
void set_gfxstream_sync_device_exists(gfxstream_sync_device_exists_t);

}  // namespace gfxstream