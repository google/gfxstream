// Copyright 2020 The Android Open Source Project
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

#include "aemu/base/Optional.h"

#include <string>

namespace android {

// Reads from |fd| into |file_contents|
// Returns false if something went wrong
bool readFileIntoString(int fd, std::string* file_contents);

// Reads |name| file
base::Optional<std::string> readFileIntoString(const std::string& filename);

// Writes |file_contents| to |fd|
// Returns false if something went wrong
bool writeStringToFile(int fd, const std::string& file_contents);

// Sets the file size to |size|, could either extend or truncate it.
bool setFileSize(int fd, int64_t size);

}  // namespace android
