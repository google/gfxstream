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

#pragma once

#include <stddef.h>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>

#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "gfxstream/host/Features.h"

#include "host-common/logging.h"

namespace gfxstream {
namespace gl {

// A class used to model an EGL config that is exposed to the guest.
//
// This really wraps a host EGLConfig handle, and provides a few cached
// attributes that can be retrieved through direct accessors, like
// getDepthSize().
//
// Each EmulatedEglConfig is identified by a unique id which is its index in
// an EmulatedEglConfigList instance (as declared below). It is not related to
// the host EGLConfig value or its EGL_CONFIG_ID.
//
// One doesn't create an EmulatedEglConfig instance. Instead, create and initialize
// an EmulatedEglConfigList from the host EGLDisplay, and use its size() and get()
// methods to access it.
class EmulatedEglConfig {
  public:
    EmulatedEglConfig(EmulatedEglConfig&&) = default;
    EmulatedEglConfig& operator=(EmulatedEglConfig&&) = default;

    // Retrieve host EGLConfig.
    EGLConfig getHostEglConfig() const { return mHostConfig; }

    EGLint getGuestEglConfig() const { return mGuestConfig; }

    // Get depth size in bits.
    GLuint getDepthSize() const { return mAttribValues[0]; }

    // Get stencil size in bits.
    GLuint getStencilSize() const { return mAttribValues[1]; }

    // Get renderable type mask.
    GLuint getRenderableType() const { return mAttribValues[2]; }

    // Get surface type mask.
    GLuint getSurfaceType() const { return mAttribValues[3]; }

    // Get the EGL_CONFIG_ID value. This is the same as the one of the
    // underlying host EGLConfig handle.
    GLint getConfigId() const { return (GLint)mAttribValues[4]; }

  private:
    friend class EmulatedEglConfigList;

    explicit EmulatedEglConfig(EGLint guestConfig,
                               EGLConfig hostConfig,
                               EGLDisplay hostDisplay,
                               bool glesDynamicVersion);

    EGLint mGuestConfig;
    EGLConfig mHostConfig;
    std::vector<GLint> mAttribValues;
    bool mGlesDynamicVersion = false;
};

// A class to model the list of EmulatedEglConfig for a given EGLDisplay, this is
// built from the list of host EGLConfig handles, filtered to only accept
// configs that are useful by the rendering library (e.g. they must support
// PBuffers and RGB pixel values).
//
// Usage is the following:
//
// 1) Create new instance by passing host EGLDisplay value.
//
// 2) Call empty() to check that the list is not empty, which would indicate
//    an error during creation.
//
// 3) EmulatedEglConfig instances are identified by numbers in 0..(N-1) range, where
//    N is the result of the size() method.
//
// 4) Convert an EmulatedEglConfig id into an EmulatedEglConfig instance with get().
//
// 5) Use getPackInfo() and packConfigs() to retrieve information about
//    available configs to the guest.
class EmulatedEglConfigList {
  public:
    // Create a new list of EmulatedEglConfig instance, by querying all compatible
    // host configs from |display|. A compatible config is one that supports
    // Pbuffers and RGB pixel values.
    //
    // After construction, call empty() to check if there are items.
    // An empty list means there was an error during construction.
    explicit EmulatedEglConfigList(EGLDisplay display,
                                   GLESDispatchMaxVersion dispatchMaxVersion,
                                   const gfxstream::host::FeatureSet& features);

    // Return true iff the list is empty. true means there was an error
    // during construction.
    bool empty() const { return mConfigs.empty(); }

    // Return the number of EmulatedEglConfig instances in the list.
    // Each instance is identified by a number from 0 to N-1,
    // where N is the result of this function.
    size_t size() const { return mConfigs.size(); }

    // Retrieve the EmulatedEglConfig instance associated with |guestId|,
    // which must be an integer between 0 and |size() - 1|. Returns
    // NULL in case of failure.
    const EmulatedEglConfig* get(int guestId) const {
        if (guestId >= 0 && guestId < (int)mConfigs.size()) {
            return &mConfigs[guestId];
        } else {
            INFO("Requested invalid EGL config id: %d (list size: %d)", guestId,
                 (int)mConfigs.size());
            return NULL;
        }
    }

    std::vector<EmulatedEglConfig>::const_iterator begin() const { return mConfigs.begin(); }
    std::vector<EmulatedEglConfig>::const_iterator end() const { return mConfigs.end(); }

    // Use |attribs| a list of EGL attribute name/values terminated by
    // EGL_NONE, to select a set of matching EmulatedEglConfig instances.
    //
    // On success, returns the number of matching instances.
    // If |configs| is not NULL, it will be populated with the guest IDs
    // of the matched EmulatedEglConfig instances.
    //
    // |configsSize| is the number of entries in the |configs| array. The
    // function will never write more than |configsSize| entries into
    // |configsSize|.
    EGLint chooseConfig(const EGLint* attribs,
                        EGLint* configs,
                        EGLint configsSize) const;

    // Retrieve information that can be sent to the guest before packed
    // config list information. If |numConfigs| is NULL, then |*numConfigs|
    // will be set on return to the number of config instances.
    // If |numAttribs| is not NULL, then |*numAttribs| will be set on return
    // to the number of attribute values cached by each EmulatedEglConfig instance.
    void getPackInfo(EGLint* mumConfigs, EGLint* numAttribs) const;

    // Write the full list information into an array of EGLuint items.
    // |buffer| is the output buffer that will receive the data.
    // |bufferByteSize| is the buffer size in bytes.
    // On success, this returns
    EGLint packConfigs(GLuint bufferByteSize, GLuint* buffer) const;

  private:
    EmulatedEglConfigList(const EmulatedEglConfigList& other) = delete;

    std::vector<EmulatedEglConfig> mConfigs;
    EGLDisplay mDisplay = 0;
    GLESDispatchMaxVersion mGlesDispatchMaxVersion;
    bool mGlesDynamicVersion = false;
};

}  // namespace gl
}  // namespace gfxstream
