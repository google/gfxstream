/*
* Copyright (C) 2011 The Android Open Source Project
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
#include "common/gles_pointer.h"

#include <algorithm>
#include <iterator>
#include <stdlib.h>
#include <string.h>

void GLESpointer::rebindArrayData() {
    if (m_attribType == ARRAY && !m_ownData.empty()) {
        m_data = m_ownData.data();
    }
}

GLESpointer::GLESpointer(const GLESpointer& other) {
    *this = other;
}

GLESpointer& GLESpointer::operator=(const GLESpointer& other) {
    if (this != &other) {
        m_size = other.m_size;
        m_type = other.m_type;
        m_stride = other.m_stride;
        m_enabled = other.m_enabled;
        m_normalize = other.m_normalize;
        m_attribType = other.m_attribType;
        m_dataSize = other.m_dataSize;
        m_data = other.m_data;
        m_bufferObject = other.m_bufferObject;
        m_buffer = other.m_buffer;
        m_bufferName = other.m_bufferName;
        m_buffOffset = other.m_buffOffset;
        m_isInt = other.m_isInt;
        m_divisor = other.m_divisor;
        m_bindingIndex = other.m_bindingIndex;
        m_reloffset = other.m_reloffset;
        m_ownData = other.m_ownData;
        m_valueCount = other.m_valueCount;
        memcpy(m_values, other.m_values, sizeof(m_values));
        rebindArrayData();
    }
    return *this;
}

GLESpointer::GLESpointer(GLESpointer&& other) noexcept {
    *this = std::move(other);
}

GLESpointer& GLESpointer::operator=(GLESpointer&& other) noexcept {
    if (this != &other) {
        m_size = other.m_size;
        m_type = other.m_type;
        m_stride = other.m_stride;
        m_enabled = other.m_enabled;
        m_normalize = other.m_normalize;
        m_attribType = other.m_attribType;
        m_dataSize = other.m_dataSize;
        m_data = other.m_data;
        m_bufferObject = std::move(other.m_bufferObject);
        m_buffer = other.m_buffer;
        m_bufferName = other.m_bufferName;
        m_buffOffset = other.m_buffOffset;
        m_isInt = other.m_isInt;
        m_divisor = other.m_divisor;
        m_bindingIndex = other.m_bindingIndex;
        m_reloffset = other.m_reloffset;
        m_ownData = std::move(other.m_ownData);
        m_valueCount = other.m_valueCount;
        memcpy(m_values, other.m_values, sizeof(m_values));
        rebindArrayData();
        other.m_data = nullptr;
        other.m_dataSize = 0;
        other.m_buffer = nullptr;
    }
    return *this;
}

GLenum GLESpointer::getType() const {
    return m_type;
}

GLint GLESpointer::getSize() const {
    return m_size;
}

GLsizei GLESpointer::getStride() const {
    return m_stride;
}

const GLvoid* GLESpointer::getArrayData() const {
    if (m_attribType == ARRAY && !m_ownData.empty()) {
        return m_ownData.data();
    }
    return m_data;
}

GLvoid* GLESpointer::getBufferData() const {
    return m_buffer
                   ? static_cast<unsigned char*>(m_buffer->getData()) +
                             m_buffOffset
                   : nullptr;
}

const GLfloat* GLESpointer::getValues() const {
    return m_values;
}

unsigned int GLESpointer::getValueCount() const {
    return m_valueCount;
}

const GLvoid* GLESpointer::getData() const {
    switch (m_attribType) {
        case ARRAY:
            return getArrayData();
            break;
        case BUFFER:
            return getBufferData();
            break;
        case VALUE:
            return (GLvoid*)getValues();
            break;
    }
    return nullptr;
}

size_t GLESpointer::getDataCapacity() const {
    switch (m_attribType) {
        case ARRAY:
            return m_dataSize > 0 ? static_cast<size_t>(m_dataSize) : 0;
        case BUFFER:
            return (m_buffer && m_buffOffset < m_buffer->getSize())
                           ? static_cast<size_t>(m_buffer->getSize() - m_buffOffset)
                           : 0;
        case VALUE:
            return m_valueCount > 0 ? static_cast<size_t>(m_valueCount) * sizeof(GLfloat) : 0;
    }
    return 0;
}

void GLESpointer::redirectPointerData() {
    m_ownData.resize(0);
    m_data = getBufferData();
}

GLuint GLESpointer::getBufferName() const {
    return m_bufferName;
}

unsigned int GLESpointer::getBufferOffset() const {
    return m_buffOffset;
}

bool GLESpointer::isEnable() const {
    return m_enabled;
}

bool GLESpointer::isNormalize() const {
    return m_normalize;
}

GLESpointer::AttribType GLESpointer::getAttribType() const {
    return m_attribType;
}

bool GLESpointer::isIntPointer() const {
    return m_isInt;
}

void GLESpointer::enable(bool b) {
    m_enabled = b;
}

void GLESpointer::setArray(GLint size,
                           GLenum type,
                           GLsizei stride,
                           const GLvoid* data,
                           GLsizei dataSize,
                           bool normalize,
                           bool isInt) {
    if (data && dataSize > 0) {
        m_ownData.assign(reinterpret_cast<const unsigned char*>(data),
                         reinterpret_cast<const unsigned char*>(data) + dataSize);
        m_data = m_ownData.data();
        m_dataSize = dataSize;
    } else {
        m_ownData.clear();
        m_data = data;
        m_dataSize = (data != nullptr) ? dataSize : 0;
    }
    m_size = size;
    m_type = type;
    m_stride = stride;
    m_bufferObject.reset();
    m_buffer = nullptr;
    m_bufferName = 0;
    m_buffOffset = 0;
    m_normalize = normalize;
    m_attribType = ARRAY;
    m_isInt = isInt;
}

void GLESpointer::setBuffer(GLint size,
                            GLenum type,
                            GLsizei stride,
                            ObjectDataPtr bufObj,
                            GLuint bufferName,
                            int offset,
                            bool normalize,
                            bool isInt) {
    m_ownData.clear();
    m_size = size;
    m_type = type;
    m_stride = stride;
    m_dataSize = 0;
    m_data = nullptr;
    m_bufferObject = std::move(bufObj);
    m_buffer = static_cast<GLESbuffer*>(m_bufferObject.get());
    m_bufferName = bufferName;
    m_buffOffset = offset;
    m_normalize = normalize;
    m_attribType = BUFFER;
    m_isInt = isInt;
}

void GLESpointer::setValue(unsigned int count, const GLfloat* val) {
    m_ownData.clear();
    m_dataSize = 0;
    memcpy(m_values, val, sizeof(GLfloat) * count);
    m_valueCount = count;
    m_attribType = VALUE;
    m_data = nullptr;
    m_bufferObject.reset();
    m_buffer = nullptr;
}

void GLESpointer::setDivisor(GLuint divisor) {
    m_divisor = divisor;
}

void GLESpointer::setBindingIndex(GLuint index) {
    m_bindingIndex = index;
}

void GLESpointer::setFormat(GLint size, GLenum type,
                            bool normalize,
                            GLuint reloffset,
                            bool isInt) {
    m_size = size;
    m_type = type;
    m_normalize = normalize;
    m_reloffset = reloffset;
    m_isInt = isInt;
}

void GLESpointer::getBufferConversions(const RangeList& rl, RangeList& rlOut) {
    m_buffer->getConversions(rl, rlOut);
}

GLESpointer::GLESpointer(gfxstream::Stream* stream) {
    onLoad(stream);
}

void GLESpointer::restoreBufferObj(
        std::function<ObjectDataPtr(GLuint)> getBufferObj) {
    if (m_attribType != BUFFER) return;
    m_bufferObject = getBufferObj(m_bufferName);
    m_buffer = static_cast<GLESbuffer*>(m_bufferObject.get());
}

void GLESpointer::onLoad(gfxstream::Stream* stream) {
    m_size = stream->getBe32();
    m_type = stream->getBe32();
    m_stride = stream->getBe32();
    m_enabled = stream->getByte();
    m_normalize = stream->getByte();
    m_attribType = (GLESpointer::AttribType)stream->getByte();
    m_bufferName = stream->getBe32();
    if (m_attribType == ARRAY) {
        m_dataSize = stream->getBe32();
        if (m_dataSize > 0) {
            m_ownData.resize(m_dataSize);
            stream->read(m_ownData.data(), m_dataSize);
        } else {
            m_ownData.clear();
            m_dataSize = 0;
        }
        m_data = !m_ownData.empty() ? m_ownData.data() : nullptr;
    } else {
        m_ownData.clear();
        m_dataSize = 0;
        m_data = nullptr;
    }
    m_buffOffset = stream->getBe32();
    m_isInt = stream->getByte();
    m_divisor = stream->getBe32();
    m_bindingIndex = stream->getBe32();
    m_reloffset = stream->getBe32();
    uint32_t valueCount = stream->getBe32();
    constexpr uint32_t kMaxValueCount =
        static_cast<uint32_t>(sizeof(m_values) / sizeof(m_values[0]));
    uint32_t toRead = std::min(valueCount, kMaxValueCount);
    stream->read(m_values, sizeof(GLfloat) * toRead);
    if (valueCount > kMaxValueCount) {
        // Skip remaining floats to maintain stream alignment.
        uint32_t toSkip = valueCount - kMaxValueCount;
        constexpr size_t kScratchFloats = 64;
        GLfloat scratch[kScratchFloats];
        while (toSkip > 0 && !stream->hasErrors()) {
            uint32_t chunk = std::min<uint32_t>(toSkip, static_cast<uint32_t>(kScratchFloats));
            if (stream->read(scratch, chunk * sizeof(GLfloat)) <= 0) {
                break;
            }
            toSkip -= chunk;
        }
    }
    m_valueCount = static_cast<GLsizei>(toRead);
}

void GLESpointer::onSave(gfxstream::Stream* stream) const {
    stream->putBe32(m_size);
    stream->putBe32(m_type);
    stream->putBe32(m_stride);
    stream->putByte(m_enabled);
    stream->putByte(m_normalize);
    stream->putByte(m_attribType);
    stream->putBe32(m_bufferName);
    if (m_attribType == ARRAY) {
        const GLvoid* data = getArrayData();
        if (data && m_dataSize > 0) {
            stream->putBe32(m_dataSize);
            stream->write(data, m_dataSize);
        } else {
            stream->putBe32(0);
        }
    }
    stream->putBe32(m_buffOffset);
    stream->putByte(m_isInt);
    stream->putBe32(m_divisor);
    stream->putBe32(m_bindingIndex);
    stream->putBe32(m_reloffset);
    stream->putBe32(m_valueCount);
    stream->write(m_values, sizeof(GLfloat) * m_valueCount);
}
