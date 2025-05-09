// Copyright 2025 The Android Open Source Project
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

#include <cstdint>
#include <string>
#include <string.h>

#ifdef _WIN32
#include <BaseTsd.h>
#ifndef ssize_t
typedef SSIZE_T ssize_t;
#endif
#else
#include <sys/types.h>
#endif

namespace gfxstream {

// Abstract interface to byte streams of all kind.
// This is mainly used to implement disk serialization.
class Stream {
  public:
    // Default constructor.
    Stream() = default;

    // Destructor.
    virtual ~Stream() = default;

    // Read up to |size| bytes and copy them to |buffer|. Return the number
    // of bytes that were actually transferred, or -errno value on error.
    virtual ssize_t read(void* buffer, size_t size) = 0;

    // Write up to |size| bytes from |buffer| into the stream. Return the
    // number of bytes that were actually transferred, or -errno value on
    // error.
    virtual ssize_t write(const void* buffer, size_t size) = 0;

    // Write a single byte |value| into the stream. Ignore errors.
    void putByte(uint8_t value) {
        write(&value, 1U);
    }

    // Write a 16-bit |value| as big-endian into the stream. Ignore errors.
    void putBe16(uint16_t value) {
        uint8_t b[2] = {
            (uint8_t)(value >> 8),
            (uint8_t) value
        };
        write(b, 2U);
    }

    // Write a 32-bit |value| as big-endian into the stream. Ignore errors.
    void putBe32(uint32_t value) {
        uint8_t b[4] = {
            (uint8_t)(value >> 24),
            (uint8_t)(value >> 16),
            (uint8_t)(value >> 8),
            (uint8_t) value,
        };
        write(b, 4U);
    }

    // Write a 64-bit |value| as big-endian into the stream. Ignore errors.
    void putBe64(uint64_t value) {
        uint8_t b[8] = {
            (uint8_t)(value >> 56),
            (uint8_t)(value >> 48),
            (uint8_t)(value >> 40),
            (uint8_t)(value >> 32),
            (uint8_t)(value >> 24),
            (uint8_t)(value >> 16),
            (uint8_t)(value >> 8),
            (uint8_t )value,
        };
        write(b, 8U);
    }

    // Read a single byte from the stream. Return 0 on error.
    uint8_t getByte() {
        uint8_t value[1] = { 0 };
        read(value, 1U);
        return value[0];
    }

    // Read a single big-endian 16-bit value from the stream.
    // Return 0 on error.
    uint16_t getBe16() {
        uint8_t b[2] = { 0, 0 };
        read(b, 2U);
        return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
    }

    // Read a single big-endian 32-bit value from the stream.
    // Return 0 on error.
    uint32_t getBe32() {
        uint8_t b[4] = { 0, 0, 0, 0 };
        read(b, 4U);
        return ((uint32_t)b[0] << 24) |
               ((uint32_t)b[1] << 16) |
               ((uint32_t)b[2] << 8) |
                (uint32_t)b[3];
    }

    // Read a single big-endian 64-bit value from the stream.
    // Return 0 on error.
    uint64_t getBe64() {
        uint8_t b[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        read(b, 8U);
        return ((uint64_t)b[0] << 56) |
               ((uint64_t)b[1] << 48) |
               ((uint64_t)b[2] << 40) |
               ((uint64_t)b[3] << 32) |
               ((uint64_t)b[4] << 24) |
               ((uint64_t)b[5] << 16) |
               ((uint64_t)b[6] << 8) |
                (uint64_t)b[7];
    }

    // Write a 32-bit float |value| to the stream.
    void putFloat(float v) {
        union {
            float f;
            uint8_t bytes[sizeof(float)];
        } u;
        u.f = v;
        this->write(u.bytes, sizeof(u.bytes));
    }

    // Read a single 32-bit float value from the stream.
    float getFloat() {
        union {
            float f;
            uint8_t bytes[sizeof(float)];
        } u;
        this->read(u.bytes, sizeof(u.bytes));
        return u.f;
    }

    // Write a string |str| of |strlen| bytes into the stream.
    // Ignore errors.
    void putString(const char* str, size_t len) {
        this->putBe32(len);
        this->write(str, len);
    }

    // Write a 0-terminated C string |str| into the stream. Ignore error.
    void putString(const char* str) {
        putString(str, strlen(str));
    }

    void putString(const std::string& str) {
        putString(str.c_str());
    }

    // Read a string from the stream. Return a new string instance,
    // which will be empty on error. Note that this can only be used
    // to read strings that were written with putString().
    std::string getString() {
        std::string result;
        size_t len = this->getBe32();
        if (len > 0) {
            result.resize(len);
            if (this->read(&result[0], len) != static_cast<ssize_t>(len)) {
                result.clear();
            }
        }
#ifdef _WIN32
        else {
            // std::string in GCC's STL still uses copy on write implementation
            // with a single shared buffer for an empty string. Its dtor has
            // a check for that shared buffer, and it deallocates memory only if
            // the current string's instance address != shared empty string address
            // Unfortunately, in Windows DLLs each DLL has its own copy of this
            // empty string (that's just the way Windows DLLs work), so if this
            // code creates an empty string and passes it over into another module,
            // that module's std::string::~string() will compare address with its
            // empty string object, find that they are different and will try to
            // free() a static object.
            // To mitigate it we make sure the string allocates something, so it
            // isn't empty internally and dtor is OK to delete the storage.
            result.reserve(1);
        }
#endif
        return result;
    }

    // Static big-endian conversions
    static void toByte(uint8_t*) {}

    static void toBe16(uint8_t* v) {
        uint16_t value;
        memcpy(&value, v, sizeof(uint16_t));
        uint8_t b[2] = {
            (uint8_t)(value >> 8),
            (uint8_t) value,
        };
        memcpy(v, b, sizeof(uint16_t));
    }

    static void toBe32(uint8_t* v) {
        uint32_t value;
        memcpy(&value, v, sizeof(uint32_t));
        uint8_t b[4] = {
                (uint8_t)(value >> 24),
                (uint8_t)(value >> 16),
                (uint8_t)(value >> 8),
                (uint8_t) value,
        };
        memcpy(v, b, sizeof(uint32_t));
    }

    static void toBe64(uint8_t* v) {
        uint64_t value;
        memcpy(&value, v, sizeof(uint64_t));
        uint8_t b[8] = {
                (uint8_t)(value >> 56),
                (uint8_t)(value >> 48),
                (uint8_t)(value >> 40),
                (uint8_t)(value >> 32),
                (uint8_t)(value >> 24),
                (uint8_t)(value >> 16),
                (uint8_t)(value >> 8),
                (uint8_t) value,
        };
        memcpy(v, b, sizeof(uint64_t));
    }

    static void fromByte(uint8_t* v) {

    }

    static void fromBe16(uint8_t* v) {
        uint8_t b[2];
        memcpy(b, v, sizeof(uint16_t));
        uint16_t value =
            ((uint16_t)b[0] << 8) |
             (uint16_t)b[1];
        memcpy(v, &value, sizeof(uint16_t));
    }

    static void fromBe32(uint8_t* v) {
        uint8_t b[4];
        memcpy(b, v, sizeof(uint32_t));
        uint32_t value =
            ((uint32_t)b[0] << 24) |
            ((uint32_t)b[1] << 16) |
            ((uint32_t)b[2] << 8)  |
             (uint32_t)b[3];
        memcpy(v, &value, sizeof(uint32_t));
    }

    static void fromBe64(uint8_t* v) {
        uint8_t b[8];
        memcpy(b, v, sizeof(uint64_t));
        uint64_t value =
            ((uint64_t)b[0] << 56) |
            ((uint64_t)b[1] << 48) |
            ((uint64_t)b[2] << 40) |
            ((uint64_t)b[3] << 32) |
            ((uint64_t)b[4] << 24) |
            ((uint64_t)b[5] << 16) |
            ((uint64_t)b[6] << 8)  |
             (uint64_t)b[7];
        memcpy(v, &value, sizeof(uint64_t));
    }
};

}  // namespace gfxstream
