// Copyright 2019 The Android Open Source Project
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

#include "aemu/base/files/StreamSerializing.h"

namespace android {
namespace base {

void saveStream(Stream* stream, const MemStream& memStream) {
    memStream.save(stream);
}

void loadStream(Stream* stream, MemStream* memStream) {
    memStream->load(stream);
}

void saveBufferRaw(Stream* stream, char* buffer, uint32_t len) {
    stream->putBe32(len);
    stream->write(buffer, len);
}

bool loadBufferRaw(Stream* stream, char* buffer) {
    auto len = stream->getBe32();
    int ret = (int)stream->read(buffer, len);
    return ret == (int)len;
}

void saveStringArray(Stream* stream, const char* const* strings, uint32_t count) {
    stream->putBe32(count);
    for (uint32_t i = 0; i < count; ++i) {
        stream->putString(strings[i]);
    }
}

std::vector<std::string> loadStringArray(Stream* stream) {
    uint32_t count = stream->getBe32();
    std::vector<std::string> res;
    for (uint32_t i = 0; i < count; ++i) {
        res.push_back(stream->getString());
    }
    return res;
}

}  // namespace base
}  // namespace android
