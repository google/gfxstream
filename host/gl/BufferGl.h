// Copyright 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES3/gl3.h>

#include <memory>

#include "ContextHelper.h"
#include "Handle.h"
#include "render-utils/stream.h"

namespace gfxstream {
namespace gl {

class BufferGl {
   public:
    static std::unique_ptr<BufferGl> create(uint64_t sizeBytes, HandleType hndl,
                                            ContextHelper* helper);

    ~BufferGl() = default;

    HandleType getHndl() const { return mHandle; }
    uint64_t getSize() const { return mSize; }

    void read(uint64_t offset, uint64_t size, void* bytes);

    void subUpdate(uint64_t offset, uint64_t size, const void* bytes);

    void onSave(gfxstream::Stream* stream);

    static std::unique_ptr<BufferGl> onLoad(gfxstream::Stream* stream, ContextHelper* helper);

   protected:
    BufferGl(uint64_t size, HandleType hndl, ContextHelper* helper);

   private:
    /*
    // TODO: GL_EXT_external_buffer
    GLuint m_buffer = 0;
    */
    const uint64_t mSize;
    const HandleType mHandle;
    ContextHelper* mContextHelper = nullptr;
};

}  // namespace gl
}  // namespace gfxstream
