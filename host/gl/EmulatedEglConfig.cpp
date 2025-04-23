// Copyright (C) 2015 The Android Open Source Project
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

#include "EmulatedEglConfig.h"

#include <stdio.h>
#include <string.h>

#include "OpenGLESDispatch/EGLDispatch.h"
#include "gfxstream/host/Features.h"
#include "gfxstream/host/guest_operations.h"
#include "gfxstream/host/logging.h"
#include "gfxstream/host/renderer_operations.h"

namespace gfxstream {
namespace gl {
namespace {

#ifndef EGL_PRESERVED_RESOURCES
#define EGL_PRESERVED_RESOURCES 0x3030
#endif

const GLuint kConfigAttributes[] = {
    EGL_DEPTH_SIZE,     // must be first - see getDepthSize()
    EGL_STENCIL_SIZE,   // must be second - see getStencilSize()
    EGL_RENDERABLE_TYPE,// must be third - see getRenderableType()
    EGL_SURFACE_TYPE,   // must be fourth - see getSurfaceType()
    EGL_CONFIG_ID,      // must be fifth  - see chooseConfig()
    EGL_BUFFER_SIZE,
    EGL_ALPHA_SIZE,
    EGL_BLUE_SIZE,
    EGL_GREEN_SIZE,
    EGL_RED_SIZE,
    EGL_CONFIG_CAVEAT,
    EGL_LEVEL,
    EGL_MAX_PBUFFER_HEIGHT,
    EGL_MAX_PBUFFER_PIXELS,
    EGL_MAX_PBUFFER_WIDTH,
    EGL_NATIVE_RENDERABLE,
    EGL_NATIVE_VISUAL_ID,
    EGL_NATIVE_VISUAL_TYPE,
    EGL_PRESERVED_RESOURCES,
    EGL_SAMPLES,
    EGL_SAMPLE_BUFFERS,
    EGL_TRANSPARENT_TYPE,
    EGL_TRANSPARENT_BLUE_VALUE,
    EGL_TRANSPARENT_GREEN_VALUE,
    EGL_TRANSPARENT_RED_VALUE,
    EGL_BIND_TO_TEXTURE_RGB,
    EGL_BIND_TO_TEXTURE_RGBA,
    EGL_MIN_SWAP_INTERVAL,
    EGL_MAX_SWAP_INTERVAL,
    EGL_LUMINANCE_SIZE,
    EGL_ALPHA_MASK_SIZE,
    EGL_COLOR_BUFFER_TYPE,
    //EGL_MATCH_NATIVE_PIXMAP,
    EGL_RECORDABLE_ANDROID,
    EGL_CONFORMANT
};

const size_t kConfigAttributesLen =
        sizeof(kConfigAttributes) / sizeof(kConfigAttributes[0]);

bool isCompatibleHostConfig(EGLConfig config, EGLDisplay display) {
    // Filter out configs which do not support pbuffers, since they
    // are used to implement window surfaces.
    EGLint surfaceType;
    s_egl.eglGetConfigAttrib(
            display, config, EGL_SURFACE_TYPE, &surfaceType);
    if (!(surfaceType & EGL_PBUFFER_BIT)) {
        GFXSTREAM_VERBOSE("%s:%d surfaceType=%d is not compatible", __func__, __LINE__,
                          surfaceType);
        return false;
    }

    // Filter out configs that do not support RGB pixel values.
    EGLint redSize = 0, greenSize = 0, blueSize = 0;
    s_egl.eglGetConfigAttrib(
            display, config,EGL_RED_SIZE, &redSize);
    s_egl.eglGetConfigAttrib(
            display, config, EGL_GREEN_SIZE, &greenSize);
    s_egl.eglGetConfigAttrib(
            display, config, EGL_BLUE_SIZE, &blueSize);
    if (!redSize || !greenSize || !blueSize) {
        GFXSTREAM_VERBOSE(
            "%s:%d surfaceType=%d is not compatible, redSize=%d greenSize=%d blueSize=%d", __func__,
            __LINE__, surfaceType, redSize, greenSize, blueSize);
        return false;
    }

    return true;
}

}  // namespace

EmulatedEglConfig::EmulatedEglConfig(EGLint guestConfig,
                                     EGLConfig hostConfig,
                                     EGLDisplay hostDisplay,
                                     bool glesDynamicVersion)
        : mGuestConfig(guestConfig),
          mHostConfig(hostConfig),
          mAttribValues(kConfigAttributesLen),
          mGlesDynamicVersion(glesDynamicVersion) {
    for (size_t i = 0; i < kConfigAttributesLen; ++i) {
        mAttribValues[i] = 0;
        s_egl.eglGetConfigAttrib(hostDisplay,
                                 hostConfig,
                                 kConfigAttributes[i],
                                 &mAttribValues[i]);

        // This implementation supports guest window surfaces by wrapping
        // them around host Pbuffers, so always report it to the guest.
        if (kConfigAttributes[i] == EGL_SURFACE_TYPE) {
            mAttribValues[i] |= EGL_WINDOW_BIT;
        }

        // Don't report ES3 renderable type if we don't support it.
        if (kConfigAttributes[i] == EGL_RENDERABLE_TYPE) {
            if (!mGlesDynamicVersion && mAttribValues[i] & EGL_OPENGL_ES3_BIT) {
                mAttribValues[i] &= ~EGL_OPENGL_ES3_BIT;
            }
        }
    }
}

EmulatedEglConfigList::EmulatedEglConfigList(EGLDisplay display,
                                             GLESDispatchMaxVersion version,
                                             const gfxstream::host::FeatureSet& features)
        : mDisplay(display),
          mGlesDispatchMaxVersion(version),
          mGlesDynamicVersion(features.GlesDynamicVersion.enabled) {
    if (display == EGL_NO_DISPLAY) {
        GFXSTREAM_ERROR("Invalid display value %p (EGL_NO_DISPLAY).", (void*)display);
        return;
    }

    EGLint numHostConfigs = 0;
    if (!s_egl.eglGetConfigs(display, NULL, 0, &numHostConfigs)) {
        GFXSTREAM_ERROR("Failed to get number of host EGL configs.");
        return;
    }
    std::vector<EGLConfig> hostConfigs(numHostConfigs);
    s_egl.eglGetConfigs(display, hostConfigs.data(), numHostConfigs, &numHostConfigs);

    for (EGLConfig hostConfig : hostConfigs) {
        // Filter out configs that are not compatible with our implementation.
        if (!isCompatibleHostConfig(hostConfig, display)) {
            continue;
        }

        const EGLint guestConfig = static_cast<EGLint>(mConfigs.size());
        mConfigs.push_back(EmulatedEglConfig(guestConfig, hostConfig, display, mGlesDynamicVersion));
    }
}

const EmulatedEglConfig* EmulatedEglConfigList::get(int guestId) const {
    if (guestId >= 0 && guestId < (int)mConfigs.size()) {
        return &mConfigs[guestId];
    } else {
        GFXSTREAM_INFO("Requested invalid EGL config id: %d (list size: %d)", guestId,
                       (int)mConfigs.size());
        return NULL;
    }
}

int EmulatedEglConfigList::chooseConfig(const EGLint* attribs,
                                        EGLint* configs,
                                        EGLint configsSize) const {
    EGLint numHostConfigs = 0;
    if (!s_egl.eglGetConfigs(mDisplay, NULL, 0, &numHostConfigs)) {
        GFXSTREAM_ERROR("Failed to get number of host EGL configs.");
        return 0;
    }

    // If EGL_SURFACE_TYPE appears in |attribs|, the value passed to
    // eglChooseConfig should be forced to EGL_PBUFFER_BIT because that's
    // what it used by the current implementation, exclusively. This forces
    // the rewrite of |attribs| into a new array.
    bool hasSurfaceType = false;
    bool wantSwapPreserved = false;
    int surfaceTypeIdx = 0;
    int numAttribs = 0;
    std::vector<EGLint> newAttribs;
    while (attribs[numAttribs] != EGL_NONE) {
        if (attribs[numAttribs] == EGL_SURFACE_TYPE) {
            hasSurfaceType = true;
            surfaceTypeIdx = numAttribs;
            if (attribs[numAttribs+1] & EGL_SWAP_BEHAVIOR_PRESERVED_BIT) {
                wantSwapPreserved = true;
            }
        }

        // Reject config if guest asked for ES3 and we don't have it.
        if (attribs[numAttribs] == EGL_RENDERABLE_TYPE) {
            if (attribs[numAttribs + 1] != EGL_DONT_CARE &&
                attribs[numAttribs + 1] & EGL_OPENGL_ES3_BIT_KHR &&
                (!mGlesDynamicVersion || mGlesDispatchMaxVersion < GLES_DISPATCH_MAX_VERSION_3_0)) {
                return 0;
            }
        }
        numAttribs += 2;
    }

    if (numAttribs) {
        newAttribs.resize(numAttribs);
        memcpy(&newAttribs[0], attribs, numAttribs * sizeof(EGLint));
    }

    const int apiLevel = get_gfxstream_guest_android_api_level();
    if (!hasSurfaceType) {
        newAttribs.push_back(EGL_SURFACE_TYPE);
        newAttribs.push_back(0);
    } else if (wantSwapPreserved && apiLevel <= 19) {
        newAttribs[surfaceTypeIdx + 1] &= ~(EGLint)EGL_SWAP_BEHAVIOR_PRESERVED_BIT;
    }
    if (get_gfxstream_renderer() == SELECTED_RENDERER_SWIFTSHADER ||
        get_gfxstream_renderer() == SELECTED_RENDERER_SWIFTSHADER_INDIRECT ||
        get_gfxstream_renderer() == SELECTED_RENDERER_ANGLE ||
        get_gfxstream_renderer() == SELECTED_RENDERER_ANGLE_INDIRECT) {
        newAttribs.push_back(EGL_CONFIG_CAVEAT);
        newAttribs.push_back(EGL_DONT_CARE);
    }

    newAttribs.push_back(EGL_NONE);


    std::vector<EGLConfig> matchedConfigs(numHostConfigs);
    if (s_egl.eglChooseConfig(mDisplay,
                              &newAttribs[0],
                              matchedConfigs.data(),
                              numHostConfigs,
                              &numHostConfigs) == EGL_FALSE) {
        return -s_egl.eglGetError();
    }

    int result = 0;
    for (int n = 0; n < numHostConfigs; ++n) {
        // Don't count or write more than |configsSize| items if |configs|
        // is not NULL.
        if (configs && configsSize > 0 && result >= configsSize) {
            break;
        }
        // Skip incompatible host configs.
        if (!isCompatibleHostConfig(matchedConfigs[n], mDisplay)) {
            continue;
        }
        // Find the EmulatedEglConfig with the same EGL_CONFIG_ID
        EGLint hostConfigId;
        s_egl.eglGetConfigAttrib(
                mDisplay, matchedConfigs[n], EGL_CONFIG_ID, &hostConfigId);
        for (const EmulatedEglConfig& config : mConfigs) {
            if (config.getConfigId() == hostConfigId) {
                // There is a match. Write it to |configs| if it is not NULL.
                if (configs && result < configsSize) {
                    configs[result] = config.getGuestEglConfig();
                }
                result++;
                break;
            }
        }
    }

    return result;
}


void EmulatedEglConfigList::getPackInfo(EGLint* numConfigs,
                               EGLint* numAttributes) const {
    if (numConfigs) {
        *numConfigs = mConfigs.size();
    }
    if (numAttributes) {
        *numAttributes = static_cast<EGLint>(kConfigAttributesLen);
    }
}

EGLint EmulatedEglConfigList::packConfigs(GLuint bufferByteSize, GLuint* buffer) const {
    GLuint numAttribs = static_cast<GLuint>(kConfigAttributesLen);
    GLuint kGLuintSize = static_cast<GLuint>(sizeof(GLuint));
    GLuint neededByteSize = (mConfigs.size() + 1) * numAttribs * kGLuintSize;
    if (!buffer || bufferByteSize < neededByteSize) {
        return -neededByteSize;
    }
    // Write to the buffer the config attribute ids, followed for each one
    // of the configs, their values.
    memcpy(buffer, kConfigAttributes, kConfigAttributesLen * kGLuintSize);

    for (int i = 0; i < (int)mConfigs.size(); ++i) {
        memcpy(buffer + (i + 1) * kConfigAttributesLen,
               mConfigs[i].mAttribValues.data(),
               kConfigAttributesLen * kGLuintSize);
    }
    return mConfigs.size();
}

}  // namespace gl
}  // namespace gfxstream