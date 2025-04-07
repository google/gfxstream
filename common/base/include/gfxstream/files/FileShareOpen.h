// Copyright 2018 The Android Open Source Project
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

#include <cstdio>

namespace android {
namespace base {

enum class FileShare {
    Read,
    Write,
};

FILE* fsopen(const char* filename, const char* mode, FileShare fileshare);
FILE* fsopenWithTimeout(const char* filename, const char* mode,
        FileShare fileshare, int timeoutMs);

bool updateFileShare(FILE* file, FileShare fileshare);

// Create a file. This operation is safe for multi-processing if other process
// tries to fsopen the file during the creation.
void createFileForShare(const char* filename);

bool updateFileShare(FILE* file, FileShare fileshare);

}  // namespace base
}  // namespace android
