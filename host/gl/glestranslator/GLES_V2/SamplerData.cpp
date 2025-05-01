/*
* Copyright (C) 2017 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "SamplerData.h"

#include "gfxstream/host/stream_utils.h"
#include "GLcommon/GLEScontext.h"

SamplerData::SamplerData(gfxstream::Stream* stream)
    : ObjectData(stream) {
    if (stream) {
        gfxstream::loadCollection(stream, &mParamis,
                [](gfxstream::Stream* stream) {
                    GLuint idx = stream->getBe32();
                    GLuint val = stream->getBe32();
                    return std::make_pair(idx, val);
                });
        gfxstream::loadCollection(stream, &mParamfs,
                [](gfxstream::Stream* stream) {
                    GLuint idx = stream->getBe32();
                    GLfloat val = stream->getFloat();
                    return std::make_pair(idx, val);
                });
    }
}

void SamplerData::onSave(gfxstream::Stream* stream, unsigned int globalName) const {
    ObjectData::onSave(stream, globalName);
    gfxstream::saveCollection(stream, mParamis,
            [](gfxstream::Stream* stream,
                const std::pair<const GLenum, GLuint>& item) {
                stream->putBe32(item.first);
                stream->putBe32(item.second);
            });
    gfxstream::saveCollection(stream, mParamfs,
            [](gfxstream::Stream* stream,
                const std::pair<const GLenum, GLfloat>& item) {
                stream->putBe32(item.first);
                stream->putFloat(item.second);
            });
}

void SamplerData::restore(ObjectLocalName localName,
        const getGlobalName_t& getGlobalName) {
    ObjectData::restore(localName, getGlobalName);
    int globalName = getGlobalName(NamedObjectType::SAMPLER,
        localName);
    GLDispatch& dispatcher = GLEScontext::dispatcher();
    for (auto& param : mParamis) {
        dispatcher.glSamplerParameteri(globalName, param.first, param.second);
    }
    for (auto& param : mParamfs) {
        dispatcher.glSamplerParameterf(globalName, param.first, param.second);
    }
}

void SamplerData::setParami(GLenum pname, GLint param) {
    mParamis[pname] = param;
}

void SamplerData::setParamf(GLenum pname, GLfloat param) {
    mParamfs[pname] = param;
}
