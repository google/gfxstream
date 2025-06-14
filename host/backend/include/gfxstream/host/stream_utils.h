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

#pragma once

#include <string>
#include <vector>

#include "gfxstream/TypeTraits.h"
#include "render-utils/stream.h"

namespace gfxstream {

//
// Save/load operations for different types.
//

void saveBufferRaw(Stream* stream, char* buffer, uint32_t len);
bool loadBufferRaw(Stream* stream, char* buffer);

template <class T, class = gfxstream::base::enable_if<std::is_standard_layout<T>>>
void saveBuffer(Stream* stream, const std::vector<T>& buffer) {
    stream->putBe32(buffer.size());
    stream->write(buffer.data(), sizeof(T) * buffer.size());
}

template <class T, class = gfxstream::base::enable_if<std::is_standard_layout<T>>>
bool loadBuffer(Stream* stream, std::vector<T>* buffer) {
    auto len = stream->getBe32();
    buffer->resize(len);
    int ret = (int)stream->read(buffer->data(), len * sizeof(T));
    return ret == len * sizeof(T);
}

template <class Container,
          class = gfxstream::base::enable_if<std::is_standard_layout<typename Container::value_type>>>
void saveBuffer(Stream* stream, const Container& buffer) {
    stream->putBe32(buffer.size());
    stream->write(buffer.data(), sizeof(typename Container::value_type) * buffer.size());
}

template <class Container,
          class = gfxstream::base::enable_if<std::is_standard_layout<typename Container::value_type>>>
bool loadBuffer(Stream* stream, Container* buffer) {
    auto len = stream->getBe32();
    buffer->clear();
    buffer->resize_noinit(len);
    int ret = (int)stream->read(buffer->data(), len * sizeof(typename Container::value_type));
    return ret == len * sizeof(typename Container::value_type);
}

template <class T, class SaveFunc>
void saveBuffer(Stream* stream, const std::vector<T>& buffer, SaveFunc&& saver) {
    stream->putBe32(buffer.size());
    for (const auto& val : buffer) {
        saver(stream, val);
    }
}

template <class T>
void saveBuffer(Stream* stream, const T* buffer, size_t numElts) {
    stream->putBe32(numElts);
    stream->write(buffer, sizeof(T) * numElts);
}

template <class T>
void loadBufferPtr(Stream* stream, T* out) {
    auto len = stream->getBe32();
    stream->read(out, len * sizeof(T));
}

template <class T, class LoadFunc>
void loadBuffer(Stream* stream, std::vector<T>* buffer, LoadFunc&& loader) {
    auto len = stream->getBe32();
    buffer->clear();
    buffer->reserve(len);
    for (uint32_t i = 0; i < len; i++) {
        buffer->emplace_back(loader(stream));
    }
}

template <class Collection, class SaveFunc>
void saveCollection(Stream* stream, const Collection& c, SaveFunc&& saver) {
    stream->putBe32(c.size());
    for (const auto& val : c) {
        saver(stream, val);
    }
}

template <class Collection, class LoadFunc>
void loadCollection(Stream* stream, Collection* c, LoadFunc&& loader) {
    const int size = stream->getBe32();
    for (int i = 0; i < size; ++i) {
        c->emplace(loader(stream));
    }
}

void saveStringArray(Stream* stream, const char* const* strings, uint32_t count);
std::vector<std::string> loadStringArray(Stream* stream);

}  // namespace gfxstream
