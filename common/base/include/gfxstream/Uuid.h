// Copyright 2016 The Android Open Source Project
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

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#include <stdint.h>

namespace android {
namespace base {

// This class incapsulates a Uuid - universally unique identifier, AKA Guid
// It's implemented with Uuid* functions in Windows and libuuid for Posix.
//
class Uuid {
public:
    // Create a zeroed Uuid
    Uuid();

    // Parse a string into Uuid instance. If the string isn't parsable the
    // object is zero-initialized instead
    explicit Uuid(std::string_view asString);

    // These two functions generate a new random Uuid. generateFast() may
    // be slightly faster because of the higher possibility of
    // non-global uniquiness.
    // Windows has a faster function and it is used here. Current Posix
    // implementation uses the same uuid_generate() for both
    static Uuid generate();
    static Uuid generateFast();

    // Returns a string representation of the Uuid in the following form:
    //      xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::string toString() const;

    static constexpr int byteSize = 16;

    // Returns the raw in-memory representation
    const uint8_t* bytes() const {
        return static_cast<const uint8_t*>(dataPtr());
    }

    // A string form of the zeroed Uuid
    static const char* const nullUuidStr;

    // Set of comparison operators for the Uuids
    friend bool operator<(const Uuid& a, const Uuid& b) {
        return memcmp(&a.mData, &b.mData, sizeof(Uuid::data_t)) < 0;
    }
    friend bool operator>(const Uuid& a, const Uuid& b) { return b < a; }
    friend bool operator==(const Uuid& a, const Uuid& b) {
        return memcmp(&a.mData, &b.mData, sizeof(Uuid::data_t)) == 0;
    }
    friend bool operator!=(const Uuid& a, const Uuid& b) { return !(a == b); }

private:
    // Helpers for casting to a system-specific Uuid type.
    void* dataPtr();
    const void* dataPtr() const;

    // The Uuid is 16-byte number, but underlying system types are different
    // on different OSes.
    // This type alias is able to store any of those
    using data_t = typename std::
            aligned_storage<byteSize, std::alignment_of<int32_t>::value>::type;

    data_t mData;
};

}  // namespace base
}  // namespace android
