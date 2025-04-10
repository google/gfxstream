// Copyright 2023 The Android Open Source Project
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

#include "gfxstream/virtio-gpu-gfxstream-renderer-goldfish.h"

#include "gfxstream/host/logging.h"
#ifdef CONFIG_AEMU
#include "host-common/opengles.h"
#endif
#include "snapshot/common.h"

VG_EXPORT int stream_renderer_snapshot_presave_pause() {
#ifdef CONFIG_AEMU
    android_getOpenglesRenderer()->pauseAllPreSave();
    return 0;
#else
    GFXSTREAM_FATAL("Unexpected call to Goldfish specific function in non-Goldfish build.");
    return -1;
#endif
}

VG_EXPORT int stream_renderer_snapshot_postsave_resume() {
#ifdef CONFIG_AEMU
    android_getOpenglesRenderer()->resumeAll();
    return 0;
#else
    GFXSTREAM_FATAL("Unexpected call to Goldfish specific function in non-Goldfish build.");
    return -1;
#endif
}

VG_EXPORT int stream_renderer_snapshot_save(void* saverStream) {
#ifdef CONFIG_AEMU
    auto* saver = static_cast<android::snapshot::SnapshotSaveStream*>(saverStream);
    android_getOpenglesRenderer()->save(saver->stream, saver->textureSaver);
    return 0;
#else
    GFXSTREAM_FATAL("Unexpected call to Goldfish specific function in non-Goldfish build.");
    return -1;
#endif
}

VG_EXPORT int stream_renderer_snapshot_load(void* loaderStream) {
#ifdef CONFIG_AEMU
    auto* loader = static_cast<android::snapshot::SnapshotLoadStream*>(loaderStream);
    android_getOpenglesRenderer()->load(loader->stream, loader->textureLoader);
    return 0;
#else
    GFXSTREAM_FATAL("Unexpected call to Goldfish specific function in non-Goldfish build.");
    return -1;
#endif
}
