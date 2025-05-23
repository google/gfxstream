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

#ifndef MAGMA_VIRTIO_GPU_DEFS_H
#define MAGMA_VIRTIO_GPU_DEFS_H

#include <lib/magma/magma_common_defs.h>

#define MAGMA_VENDOR_VERSION_VIRTIO 1

enum MagmaVirtioGpuQuery {
  // Bits 32..47 indicate the capset id (from virtio spec), bits 48..63 indicate the version.
  // Returns buffer result.
  kMagmaVirtioGpuQueryCapset = MAGMA_QUERY_VENDOR_PARAM_0 + 10000,
};

#endif  // MAGMA_VIRTIO_GPU_DEFS_H
