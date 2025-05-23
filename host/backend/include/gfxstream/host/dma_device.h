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

#include "render-utils/dma_device.h"

namespace gfxstream {

extern gfxstream_dma_get_host_addr_t g_gfxstream_dma_get_host_addr;
extern gfxstream_dma_unlock_t g_gfxstream_dma_unlock;

void set_gfxstream_dma_get_host_addr(gfxstream_dma_get_host_addr_t);
void set_gfxstream_dma_unlock(gfxstream_dma_unlock_t);

}  // namespace gfxstream
