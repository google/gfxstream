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

#include "gfxstream/host/sync_device.h"

namespace gfxstream {
namespace {

static uint64_t DefaultGfxstreamSyncCreateTimeline() {
    return 0;
}

static int DefaultGfxstreamSyncCreateFence(uint64_t, uint32_t) {
    return -1;
}

static void DefaultGfxstreamSyncIncrementTimeline(uint64_t, uint32_t) { }

static void DefaultGfxstreamSyncDestroyTimeline(uint64_t) { }

static void DefaultGfxstreamSyncRegisterTriggerWait(gfxstream_sync_trigger_wait_t) { }

static bool DefaultGfxstreamSyncDeviceExists() {
    return false;
}

}  // namespace

gfxstream_sync_create_timeline_t gfxstream_sync_create_timeline = DefaultGfxstreamSyncCreateTimeline;
gfxstream_sync_create_fence_t gfxstream_sync_create_fence = DefaultGfxstreamSyncCreateFence;
gfxstream_sync_timeline_inc_t gfxstream_sync_timeline_inc = DefaultGfxstreamSyncIncrementTimeline;
gfxstream_sync_destroy_timeline_t gfxstream_sync_destroy_timeline = DefaultGfxstreamSyncDestroyTimeline;
gfxstream_sync_register_trigger_wait_t gfxstream_sync_register_trigger_wait = DefaultGfxstreamSyncRegisterTriggerWait;
gfxstream_sync_device_exists_t gfxstream_sync_device_exists = DefaultGfxstreamSyncDeviceExists;

void set_gfxstream_sync_create_timeline(gfxstream_sync_create_timeline_t f) {
    gfxstream_sync_create_timeline = f;
}

void set_gfxstream_sync_create_fence(gfxstream_sync_create_fence_t f) {
    gfxstream_sync_create_fence = f;
}

void set_gfxstream_sync_timeline_inc(gfxstream_sync_timeline_inc_t f) {
    gfxstream_sync_timeline_inc = f;
}

void set_gfxstream_sync_destroy_timeline(gfxstream_sync_destroy_timeline_t f) {
    gfxstream_sync_destroy_timeline = f;
}

void set_gfxstream_sync_register_trigger_wait(gfxstream_sync_register_trigger_wait_t f) {
    gfxstream_sync_register_trigger_wait = f;
}

void set_gfxstream_sync_device_exists(gfxstream_sync_device_exists_t f) {
    gfxstream_sync_device_exists = f;
}

}  // namespace gfxstream
