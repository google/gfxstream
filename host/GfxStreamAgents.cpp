// Copyright (C) 2020 The Android Open Source Project
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
#include "GfxStreamAgents.h"

#include <stdint.h>  // for uint32_t
#include <stdio.h>   // for fprintf

#include <map>      // for map, __ma...
#include <utility>  // for pair

#include "gfxstream/host/logging.h"
#include "host-common/HostmemIdMapping.h"  // for android_e...
#include "host-common/MultiDisplay.h"      // for MultiDisp...
#include "host-common/misc.h"
#include "host-common/multi_display_agent.h"  // for QAndroidM...
#include "host-common/vm_operations.h"        // for SnapshotC...
#include "host-common/window_agent.h"         // for WindowMes...

#ifdef _DEBUG
#define DEBUG_LOG(fd, fmt, ...) fprintf(fd, fmt, __VA_ARGS__);
#else
#define DEBUG_LOG(fd, fmt, ...)
#endif

static std::map<uint32_t, android::MultiDisplayInfo> mMultiDisplay;

using namespace android;

static const QAndroidMultiDisplayAgent sMultiDisplayAgent = {
        .setMultiDisplay = [](uint32_t id,
                              int32_t x,
                              int32_t y,
                              uint32_t w,
                              uint32_t h,
                              uint32_t dpi,
                              uint32_t flag,
                              bool add) -> int {
            return 0;
        },
        .getMultiDisplay = [](uint32_t id,
                              int32_t* x,
                              int32_t* y,
                              uint32_t* w,
                              uint32_t* h,
                              uint32_t* dpi,
                              uint32_t* flag,
                              bool* enabled) -> bool {
            if (mMultiDisplay.find(id) == mMultiDisplay.end()) {
                if (enabled) {
                    *enabled = false;
                }
                return false;
            }
            if (x) {
                *x = mMultiDisplay[id].pos_x;
            }
            if (y) {
                *y = mMultiDisplay[id].pos_y;
            }
            if (w) {
                *w = mMultiDisplay[id].width;
            }
            if (h) {
                *h = mMultiDisplay[id].height;
            }
            if (dpi) {
                *dpi = mMultiDisplay[id].dpi;
            }
            if (flag) {
                *flag = mMultiDisplay[id].flag;
            }
            if (enabled) {
                *enabled = mMultiDisplay[id].enabled;
            }
            return true;
        },
        .getNextMultiDisplay = [](int32_t start_id,
                                  uint32_t* id,
                                  int32_t* x,
                                  int32_t* y,
                                  uint32_t* w,
                                  uint32_t* h,
                                  uint32_t* dpi,
                                  uint32_t* flag,
                                  uint32_t* cb) -> bool {
            uint32_t key;
            std::map<uint32_t, android::MultiDisplayInfo>::iterator i;
            if (start_id < 0) {
                key = 0;
            } else {
                key = start_id + 1;
            }
            i = mMultiDisplay.lower_bound(key);
            if (i == mMultiDisplay.end()) {
                return false;
            } else {
                if (id) {
                    *id = i->first;
                }
                if (x) {
                    *x = i->second.pos_x;
                }
                if (y) {
                    *y = i->second.pos_y;
                }
                if (w) {
                    *w = i->second.width;
                }
                if (h) {
                    *h = i->second.height;
                }
                if (dpi) {
                    *dpi = i->second.dpi;
                }
                if (flag) {
                    *flag = i->second.flag;
                }
                if (cb) {
                    *cb = i->second.cb;
                }
                return true;
            }
        },
        .isMultiDisplayEnabled = [](void) -> bool {
            return mMultiDisplay.size() > 1;
        },
        .getCombinedDisplaySize = [](uint32_t* width, uint32_t* height) {},
        .multiDisplayParamValidate = [](uint32_t id,
                                        uint32_t w,
                                        uint32_t h,
                                        uint32_t dpi,
                                        uint32_t flag) -> bool { return true; },
        .translateCoordination =
                [](uint32_t* x, uint32_t* y, uint32_t* displayId) -> bool {
            return true;
        },
        .setGpuMode = [](bool isGuestMode, uint32_t w, uint32_t h) {},
        .createDisplay = [](uint32_t* displayId) -> int {
            if (displayId == nullptr) {
                GFXSTREAM_ERROR("cannot create display, null displayId pointer");
                return -1;
            }
            if (mMultiDisplay.size() >= MultiDisplay::s_maxNumMultiDisplay) {
                GFXSTREAM_ERROR("cannot create more displays, exceeding limits %d",
                                MultiDisplay::s_maxNumMultiDisplay);
                return -1;
            }
            if (mMultiDisplay.find(*displayId) != mMultiDisplay.end()) {
                return 0;
            }
            // displays created by internal rcCommands
            if (*displayId == MultiDisplay::s_invalidIdMultiDisplay) {
                for (uint32_t i = MultiDisplay::s_displayIdInternalBegin; i < MultiDisplay::s_maxNumMultiDisplay; i++) {
                    if (mMultiDisplay.find(i) == mMultiDisplay.end()) {
                        *displayId = i;
                        break;
                    }
                }
            }
            if (*displayId == MultiDisplay::s_invalidIdMultiDisplay) {
                GFXSTREAM_ERROR(
                    "cannot create more internaldisplays, exceeding limits %d",
                    MultiDisplay::s_maxNumMultiDisplay - MultiDisplay::s_displayIdInternalBegin);
                return -1;
            }

            mMultiDisplay.emplace(*displayId, android::MultiDisplayInfo());
            return 0;
        },
        .destroyDisplay = [](uint32_t displayId) -> int {
            mMultiDisplay.erase(displayId);
            return 0;
        },
        .setDisplayPose = [](uint32_t displayId,
                             int32_t x,
                             int32_t y,
                             uint32_t w,
                             uint32_t h,
                             uint32_t dpi) -> int {
            if (mMultiDisplay.find(displayId) == mMultiDisplay.end()) {
                GFXSTREAM_ERROR("cannot find display %d", displayId);
                return -1;
            }
            mMultiDisplay[displayId].pos_x = x;
            mMultiDisplay[displayId].pos_y = y;
            mMultiDisplay[displayId].width = w;
            mMultiDisplay[displayId].height = h;
            mMultiDisplay[displayId].dpi = dpi;
            return 0;
        },
        .getDisplayPose = [](uint32_t displayId,
                             int32_t* x,
                             int32_t* y,
                             uint32_t* w,
                             uint32_t* h) -> int {
            if (mMultiDisplay.find(displayId) == mMultiDisplay.end()) {
                GFXSTREAM_ERROR("cannot find display %d", displayId);
                return -1;
            }
            if (x)
                *x = mMultiDisplay[displayId].pos_x;
            if (y)
                *y = mMultiDisplay[displayId].pos_y;
            if (w)
                *w = mMultiDisplay[displayId].width;
            if (h)
                *h = mMultiDisplay[displayId].height;
            return 0;
        },
        .getDisplayColorBuffer = [](uint32_t displayId,
                                    uint32_t* colorBuffer) -> int {
            if (mMultiDisplay.find(displayId) == mMultiDisplay.end()) {
                GFXSTREAM_ERROR("cannot find display %d", displayId);
                return -1;
            }
            *colorBuffer = mMultiDisplay[displayId].cb;
            return 0;
        },
        .getColorBufferDisplay = [](uint32_t colorBuffer,
                                    uint32_t* displayId) -> int {
            for (const auto& iter : mMultiDisplay) {
                if (iter.second.cb == colorBuffer) {
                    *displayId = iter.first;
                    return 0;
                }
            }
            return -1;
        },
        .setDisplayColorBuffer = [](uint32_t displayId,
                                    uint32_t colorBuffer) -> int {
            if (mMultiDisplay.find(displayId) == mMultiDisplay.end()) {
                GFXSTREAM_ERROR("Unable to set display color buffer for non existing display: %d",
                                displayId);
                return -1;
            }
            mMultiDisplay[displayId].cb = colorBuffer;
            return 0;
        },
       .isPixelFold = [] () {
           return false;
       },
    };

static bool sIsFolded = false;

static const QAndroidEmulatorWindowAgent sQAndroidEmulatorWindowAgent = {
        .getEmulatorWindow =
                [](void) {
                    DEBUG_LOG(stderr,
                            "window-agent-GfxStream-impl: "
                            ".getEmulatorWindow\n");
                    return (EmulatorWindow*)nullptr;
                },
        .rotate90Clockwise =
                [](void) {
                    DEBUG_LOG(stderr,
                            "window-agent-GfxStream-impl: "
                            ".rotate90Clockwise\n");
                    return true;
                },
        .rotate =
                [](int rotation) {
                    DEBUG_LOG(stderr,
                            "window-agent-GfxStream-impl: "
                            ".rotate90Clockwise\n");
                    return true;
                },
        .getRotation =
                [](void) {
                    DEBUG_LOG(stderr,
                            "window-agent-GfxStream-impl: .getRotation\n");
                    return (int)SKIN_ROTATION_0;
                },
        .showMessage =
                [](const char* message, WindowMessageType type, int timeoutMs) {
                    DEBUG_LOG(stderr,
                            "window-agent-GfxStream-impl: .showMessage %s\n",
                            message);
                },
        .showMessageWithDismissCallback =
                [](const char* message,
                   WindowMessageType type,
                   const char* dismissText,
                   void* context,
                   void (*func)(void*),
                   int timeoutMs) {
                    DEBUG_LOG(stderr,
                            "window-agent-GfxStream-impl: "
                            ".showMessageWithDismissCallback %s\n",
                            message);
                },
        .fold =
                [](bool is_fold) -> bool {
                    DEBUG_LOG(stderr, "window-agent-GfxStream-impl: .fold %d\n",
                            is_fold);
                    sIsFolded = is_fold;
                    return true;
                },
        .isFolded = [](void) -> bool { return sIsFolded; },
        .getFoldedArea = [](int* x, int* y, int* w, int* h) -> bool {
            DEBUG_LOG(stderr, "window-agent-GfxStream-impl: .getFoldedArea\n");
            return true;
        },
        .updateFoldablePostureIndicator = [](bool) {
            DEBUG_LOG(stderr, "window-agent-GfxStream-impl: updateFoldablePostureIndicator\n");
        },
        .setUIDisplayRegion =
                [](int x_offset, int y_offset, int w, int h, bool ignoreOrientation) {
                    DEBUG_LOG(stderr,
                            "window-agent-GfxStream-impl: .setUIDisplayRegion "
                            "%d %d %dx%d\n",
                            x_offset, y_offset, w, h);
                },
        .getMultiDisplay = 0,
        .setNoSkin = [](void) {},
        .restoreSkin = [](void) {},
        .updateUIMultiDisplayPage =
                [](uint32_t id) {
                    DEBUG_LOG(stderr, "updateMultiDisplayPage\n");
                },
        .getMonitorRect =
                [](uint32_t* w, uint32_t* h) -> bool {
                    if (w)
                        *w = 2500;
                    if (h)
                        *h = 1600;
                    return true;
                },
};

static const QAndroidVmOperations sQAndroidVmOperations =
    {
        .vmStop = []() -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: vm stop\n");
            return true;
        },
        .vmStart = []() -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: vm start\n");
            return true;
        },
        .vmReset = []() { DEBUG_LOG(stderr, "goldfish-opengl vm ops: vm reset\n"); },
        .vmShutdown = []() { DEBUG_LOG(stderr, "goldfish-opengl vm ops: vm reset\n"); },
        .vmPause = []() -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: vm pause\n");
            return true;
        },
        .vmResume = []() -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: vm resume\n");
            return true;
        },
        .vmIsRunning = []() -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: vm is running\n");
            return true;
        },
        .snapshotList = [](void*, LineConsumerCallback, LineConsumerCallback) -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: snapshot list\n");
            return true;
        },
        .snapshotSave = [](const char* name, void* opaque, LineConsumerCallback) -> bool {
            DEBUG_LOG(stderr, "gfxstream vm ops: snapshot save\n");
            return true;
        },
        .snapshotLoad = [](const char* name, void* opaque, LineConsumerCallback) -> bool {
            DEBUG_LOG(stderr, "gfxstream vm ops: snapshot load\n");
            return true;
        },
        .snapshotDelete = [](const char* name, void* opaque,
                             LineConsumerCallback errConsumer) -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: snapshot delete\n");
            return true;
        },
        .snapshotRemap = [](bool shared, void* opaque, LineConsumerCallback errConsumer) -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: snapshot remap\n");
            return true;
        },
        .snapshotExport = [](const char* snapshot, const char* dest, void* opaque,
                             LineConsumerCallback errConsumer) -> bool {
            DEBUG_LOG(stderr, "goldfish-opengl vm ops: snapshot export image\n");
            return true;
        },
        .setSnapshotCallbacks =
            [](void* opaque, const SnapshotCallbacks* callbacks) {
                DEBUG_LOG(stderr, "goldfish-opengl vm ops: set snapshot callbacks\n");
            },
        .mapUserBackedRam =
            [](uint64_t gpa, void* hva, uint64_t size) {
                DEBUG_LOG(stderr, "%s: map user backed ram\n", __func__);
            },
        .unmapUserBackedRam =
            [](uint64_t gpa, uint64_t size) {
                DEBUG_LOG(stderr, "%s: unmap user backed ram\n", __func__);
            },
        .getVmConfiguration =
            [](VmConfiguration* out) {
                DEBUG_LOG(stderr, "goldfish-opengl vm ops: get vm configuration\n");
            },
        .setFailureReason =
            [](const char* name, int failureReason) {
                DEBUG_LOG(stderr, "goldfish-opengl vm ops: set failure reason\n");
            },
        .setExiting = []() { DEBUG_LOG(stderr, "goldfish-opengl vm ops: set exiting\n"); },
        .allowRealAudio =
            [](bool allow) { DEBUG_LOG(stderr, "goldfish-opengl vm ops: allow real audio\n"); },
        .physicalMemoryGetAddr =
            [](uint64_t gpa) {
                DEBUG_LOG(stderr, "%s: physmemGetAddr\n", __func__);
                return (void*)nullptr;
            },
        .isRealAudioAllowed =
            [](void) {
                DEBUG_LOG(stderr, "goldfish-opengl vm ops: is real audiop allowed\n");
                return true;
            },
        .setSkipSnapshotSave =
            [](bool used) {
                DEBUG_LOG(stderr, "goldfish-opengl vm ops: set skip snapshot save\n");
            },
        .isSnapshotSaveSkipped =
            []() {
                DEBUG_LOG(stderr,
                          "goldfish-opengl vm ops: is snapshot save "
                          "skipped\n");
                return false;
            },
        .hostmemRegister = android_emulation_hostmem_register,
        .hostmemUnregister = android_emulation_hostmem_unregister,
        .hostmemGetInfo = android_emulation_hostmem_get_info,
#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_SUPPORT
        .vulkanInstanceRegister =
            [](uint64_t id, const char* name) {
                DEBUG_LOG(stderr,
                          "goldfish-opengl vm ops: register Vulkan instance "
                          "skipped\n");
            },
        .vulkanInstanceUnregister =
            [](uint64_t id) {
                DEBUG_LOG(stderr,
                          "goldfish-opengl vm ops: unregister Vulkan instance "
                          "skipped\n");
            },
        .vulkanInstanceEnumerate =
            [](uint32_t* pCount, uint64_t* pIds, char** pNames) {
                DEBUG_LOG(stderr,
                          "goldfish-opengl vm ops: enumerate Vulkan instance "
                          "skipped\n");
            },
        .setSkipSnapshotSaveReason =
            [](SnapshotSkipReason reason) {
                DEBUG_LOG(stderr,
                        "goldfish-opengl vm ops: set skip snapshot save reason"
                        "skipped\n");
            },
        .getSkipSnapshotSaveReason =
            []() {
                DEBUG_LOG(stderr,
                        "goldfish-opengl vm ops: get skip snapshot save "
                        "reason\n");
                return SNAPSHOT_SKIP_UNKNOWN;
            },
        .setStatSnapshotUseVulkan =
            []() {
                DEBUG_LOG(stderr,
                        "goldfish-opengl vm ops: set stat snapshot use Vulkan"
                        "skipped\n");
            },
        .snapshotUseVulkan =
            []() {
                DEBUG_LOG(stderr,
                        "goldfish-opengl vm ops: get stat snapshot use Vulkan"
                        "skipped\n");
                return false;
            },
#endif
};

namespace android {
namespace emulation {

#ifndef GFXSTREAM_MESON_BUILD
const QAndroidVmOperations*
GfxStreamGraphicsAgentFactory::android_get_QAndroidVmOperations() const {
    return &sQAndroidVmOperations;
}

const QAndroidMultiDisplayAgent*
GfxStreamGraphicsAgentFactory::android_get_QAndroidMultiDisplayAgent() const {
    return &sMultiDisplayAgent;
}

const QAndroidEmulatorWindowAgent*
GfxStreamGraphicsAgentFactory::android_get_QAndroidEmulatorWindowAgent()
        const {
    return &sQAndroidEmulatorWindowAgent;
}
#endif

}  // namespace emulation
}  // namespace android
