// Copyright 2020 The Android Open Source Project
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
#include "aemu/base/GLObjectCounter.h"

#include <array>
#include <atomic>
#include <sstream>

namespace android {
namespace base {

enum class NamedObjectType : int {
    NULLTYPE,
    VERTEXBUFFER,
    TEXTURE,
    RENDERBUFFER,
    FRAMEBUFFER,
    SHADER_OR_PROGRAM,
    SAMPLER,
    QUERY,
    VERTEX_ARRAY_OBJECT,
    TRANSFORM_FEEDBACK,
    NUM_OBJECT_TYPES  // Must be last
};

static constexpr size_t toIndex(NamedObjectType type) {
    return static_cast<size_t>(type);
}

class GLObjectCounter::Impl {
public:
    void incCount(size_t type) {
        if (type > toIndex(NamedObjectType::NULLTYPE) &&
            type < toIndex(NamedObjectType::NUM_OBJECT_TYPES)) {
            mCounter[type] += 1;
        }
    }

    void decCount(size_t type) {
        if (type > toIndex(NamedObjectType::NULLTYPE) &&
            type < toIndex(NamedObjectType::NUM_OBJECT_TYPES)) {
            mCounter[type] -= 1;
        }
    }

    std::vector<size_t> getCounts() {
        std::vector<size_t> v;
        for (auto& it : mCounter) {
            v.push_back(it.load());
        }
        return v;
    }

    std::string printUsage() {
        std::stringstream ss;
        ss << "VertexBuffer: "
           << mCounter[toIndex(NamedObjectType::VERTEXBUFFER)].load();
        ss << " Texture: "
           << mCounter[toIndex(NamedObjectType::TEXTURE)].load();
        ss << " RenderBuffer: "
           << mCounter[toIndex(NamedObjectType::RENDERBUFFER)].load();
        ss << " FrameBuffer: "
           << mCounter[toIndex(NamedObjectType::FRAMEBUFFER)].load();
        ss << " ShaderOrProgram: "
           << mCounter[toIndex(NamedObjectType::SHADER_OR_PROGRAM)].load();
        ss << " Sampler: "
           << mCounter[toIndex(NamedObjectType::SAMPLER)].load();
        ss << " Query: " << mCounter[toIndex(NamedObjectType::QUERY)].load();
        ss << " VertexArrayObject: "
           << mCounter[toIndex(NamedObjectType::VERTEX_ARRAY_OBJECT)].load();
        ss << " TransformFeedback: "
           << mCounter[toIndex(NamedObjectType::TRANSFORM_FEEDBACK)].load()
           << "\n";
        return ss.str();
    }

private:
    std::array<std::atomic<size_t>, toIndex(NamedObjectType::NUM_OBJECT_TYPES)>
            mCounter = {};
};

static GLObjectCounter* sGlobal() {
    static GLObjectCounter* g = new GLObjectCounter;
    return g;
}

GLObjectCounter::GLObjectCounter() : mImpl(new GLObjectCounter::Impl()) {}

void GLObjectCounter::incCount(size_t type) {
    mImpl->incCount(type);
}

void GLObjectCounter::decCount(size_t type) {
    mImpl->decCount(type);
}

std::vector<size_t> GLObjectCounter::getCounts() {
    return mImpl->getCounts();
}

std::string GLObjectCounter::printUsage() {
    return mImpl->printUsage();
}

GLObjectCounter* GLObjectCounter::get() {
    return sGlobal();
}

}  // namespace base
}  // namespace android
