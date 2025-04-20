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

#include "gfxstream/host/dma_device.h"

namespace gfxstream {
namespace {

static void* DefaultGfxstreamGetHostAddr(uint64_t) {
    return nullptr;
}

static void DefaultGfxstreamDmaUnlock(uint64_t) { }

}  // namespace

gfxstream_dma_get_host_addr_t g_gfxstream_dma_get_host_addr = DefaultGfxstreamGetHostAddr;
gfxstream_dma_unlock_t g_gfxstream_dma_unlock = DefaultGfxstreamDmaUnlock;

void set_gfxstream_dma_get_host_addr(gfxstream_dma_get_host_addr_t f) {
    g_gfxstream_dma_get_host_addr = f;
}

void set_gfxstream_dma_unlock(gfxstream_dma_unlock_t f) {
    g_gfxstream_dma_unlock = f;
}

}  // namespace gfxstream
