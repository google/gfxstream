// Copyright 2014 The Android Open Source Project
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
#include <stdint.h>

namespace android {
namespace base {

// Return true iff |text| corresponds to |textLen| bytes of valid UTF-8
// encoded text.
bool utf8IsValid(const char* text, size_t textLen);

// Decode a single UTF-8 encoded input value. Reads at most |textLen| bytes
// from |text| and tries to interpret the data as the start of an UTF-8
// encoded Unicode value. On success, return the length in bytes of the
// encoding and sets |*codepoint| to the corresponding codepoint.
// On error (e.g. invalid encoding, or input too short), return -1.
int utf8Decode(const uint8_t* text, size_t textLen, uint32_t* codepoint);

// Encode Unicode |codepoint| into its UTF-8 representation into |buffer|
// which must be at least |buffer_len| bytes. Returns encoding length, or
// -1 in case of error. Note that |buffer| can be NULL, in which case
// |buffer_len| is ignored and the full encoding length is returned.
int utf8Encode(uint32_t codepoint, uint8_t* buffer, size_t buffer_len);

}  // namespace base
}  // namespace android
