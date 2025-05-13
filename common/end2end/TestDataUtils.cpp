// Copyright (C) 2025 The Android Open Source Project
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

#include "TestDataUtils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef BAZEL_CURRENT_REPOSITORY
#include "rules_cc/cc/runfiles/runfiles.h"
#else
#include "gfxstream/system/System.h"
#endif

namespace gfxstream {
namespace tests {

std::filesystem::path GetTestDataPath(const std::string& basename) {
#ifdef BAZEL_CURRENT_REPOSITORY
    // https://github.com/bazelbuild/rules_cc/blob/main/cc/runfiles/runfiles.h
    using rules_cc::cc::runfiles::Runfiles;
    static Runfiles* sRunfiles = []() -> Runfiles* {
        std::string error;
        auto* runfiles =
            Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error);
        if (runfiles == nullptr) {
            ADD_FAILURE() << "Failed to load runfiles: " << error;
            return nullptr;
        }
        return runfiles;
    }();
    if (sRunfiles == nullptr) {
        ADD_FAILURE() << "Testdata runfiles not available.";
        return "";
    }
    const std::vector<std::string> possiblePaths = {
        std::string("_main/common/end2end/") + basename,
        std::string("_main/common/end2end/testdata/") + basename,
        std::string("_main/guest/GLESv1/") + basename,
        std::string("_main/guest/GLESv2/") + basename,
        std::string("_main/guest/egl/") + basename,
        std::string("_main/guest/rendercontrol/") + basename,
        std::string("+_repo_rules2+mesa/") + basename,
        std::string("+_repo_rules2+rutabaga/") + basename,
    };
    for (const std::string& possiblePath : possiblePaths) {
        const std::string path = sRunfiles->Rlocation(possiblePath);
        if (!path.empty() && std::filesystem::exists(path)) {
            return path;
        }
    }
    ADD_FAILURE() << "Failed to find " << basename << " testdata file.";
    return "";

#else
    const std::filesystem::path currentPath = gfxstream::base::getProgramDirectory();
    const std::vector<std::filesystem::path> possiblePaths = {
        currentPath / basename,
        currentPath / "testdata" / basename,
    };
    for (const std::string& possiblePath : possiblePaths) {
        if (std::filesystem::exists(possiblePath)) {
            return possiblePath;
        }
    }
    ADD_FAILURE() << "Failed to find " << basename << " testdata file.";
    return "";
#endif
}

}  // namespace tests
}  // namespace gfxstream
