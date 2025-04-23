// Copyright (C) 2024 The Android Open Source Project
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

#include "gfxstream/Strings.h"

namespace gfxstream {

std::vector<std::string> Split(const std::string& s, const std::string& delimiters) {
    if (delimiters.empty()) {
        return {};
    }

    std::vector<std::string> result;

    size_t base = 0;
    size_t found;
    while (true) {
        found = s.find_first_of(delimiters, base);
        result.push_back(s.substr(base, found - base));
        if (found == s.npos) break;
        base = found + 1;
    }

    return result;
}

bool HasExtension(const std::string& haystack, const std::string& needle) {
    const std::vector<std::string> extensions = Split(haystack, " ");
    for (const std::string& extension : extensions) {
        if (extension == needle) {
            return true;
        }
    }
    return false;
}

}  // namespace gfxstream
