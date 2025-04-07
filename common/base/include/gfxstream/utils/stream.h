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

#pragma once

#include "aemu/base/c_header.h"
#include "aemu/base/msvc.h"

#include <inttypes.h>
#include <sys/types.h>

ANDROID_BEGIN_HEADER

// Opaque declaration for an object modelling a stream of bytes.
// This really is backed by an android::base::Stream implementation.
typedef struct Stream Stream;

ssize_t stream_read(Stream* stream, void* buffer, size_t size);
ssize_t stream_write(Stream* stream, const void* buffer, size_t size);

void stream_put_byte(Stream* stream, int v);
void stream_put_be16(Stream* stream, uint16_t v);
void stream_put_be32(Stream* stream, uint32_t v);
void stream_put_be64(Stream* stream, uint64_t v);

uint8_t stream_get_byte(Stream* stream);
uint16_t stream_get_be16(Stream* stream);
uint32_t stream_get_be32(Stream* stream);
uint64_t stream_get_be64(Stream* stream);

void stream_put_float(Stream* stream, float v);
float stream_get_float(Stream* stream);

void stream_put_string(Stream* stream, const char* str);

// Return a heap-allocated string, or NULL if empty string or error.
// Caller must free() the resturned value.
char* stream_get_string(Stream* stream);

void stream_free(Stream* stream);

ANDROID_END_HEADER
