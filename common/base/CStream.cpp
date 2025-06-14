// Copyright 2015 The Android Open Source Project
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

#include "gfxstream/utils/stream.h"

#include "gfxstream/files/Stream.h"

#include <stdlib.h>
#include <string.h>

typedef gfxstream::base::Stream BaseStream;

static BaseStream* asBaseStream(Stream* stream) {
    return reinterpret_cast<BaseStream*>(stream);
}

ssize_t stream_read(Stream* stream, void* buffer, size_t len) {
    return asBaseStream(stream)->read(buffer, len);
}

ssize_t stream_write(Stream* stream, const void* buffer, size_t len) {
    return asBaseStream(stream)->write(buffer, len);
}

void stream_put_byte(Stream* stream, int v) {
    asBaseStream(stream)->putByte(static_cast<uint8_t>(v));
}

void stream_put_be16(Stream* stream, uint16_t v) {
    asBaseStream(stream)->putBe16(v);
}

void stream_put_be32(Stream* stream, uint32_t v) {
    asBaseStream(stream)->putBe32(v);
}

void stream_put_be64(Stream* stream, uint64_t v) {
    asBaseStream(stream)->putBe64(v);
}

uint8_t stream_get_byte(Stream* stream) {
    return asBaseStream(stream)->getByte();
}

uint16_t stream_get_be16(Stream* stream) {
    return asBaseStream(stream)->getBe16();
}

uint32_t stream_get_be32(Stream* stream) {
    return asBaseStream(stream)->getBe32();
}

uint64_t stream_get_be64(Stream* stream) {
    return asBaseStream(stream)->getBe64();
}

void stream_put_float(Stream* stream, float v) {
    asBaseStream(stream)->putFloat(v);
}

float stream_get_float(Stream* stream) {
    return asBaseStream(stream)->getFloat();
}

void stream_put_string(Stream* stream, const char* str) {
    asBaseStream(stream)->putString(str);
}

char* stream_get_string(Stream* stream) {
    std::string ret = asBaseStream(stream)->getString();
    if (ret.empty()) {
        return NULL;
    }
    char* result = static_cast<char*>(::malloc(ret.size() + 1U));
    ::memcpy(result, ret.c_str(), ret.size() + 1U);
    return result;
}

void stream_free(Stream* stream) {
    delete asBaseStream(stream);
}
