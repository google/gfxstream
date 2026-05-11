// Copyright 2024 The Android Open Source Project
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

#include "gfxstream/host/features.h"

#include <sstream>
#include <vector>

#include "gfxstream/common/logging.h"
#include "gfxstream/strings.h"
namespace gfxstream {
namespace host {

FeatureSet::FeatureSet(const FeatureSet& rhs) : FeatureSet() {
    *this = rhs;
}

FeatureSet& FeatureSet::operator=(const FeatureSet& rhs) {
    guestVulkanMaxApiVersion = rhs.guestVulkanMaxApiVersion;
    for (const auto& [featureName, featureInfo] : rhs.map) {
        *map[featureName] = *featureInfo;
    }
    return *this;
}

bool FeatureSet::processFeatureString(std::string featureStr, std::string featureReason) {
    const std::vector<std::string>& parts = gfxstream::Split(featureStr, ":");
    if (parts.size() != 2) {
        GFXSTREAM_ERROR("Error: invalid feature string: %s", featureStr.c_str());
        return false;
    }

    const std::string& feature_name = parts[0];
    const std::string& feature_value = parts[1];

    auto feature_it = map.find(feature_name);
    if (feature_it == map.end()) {
        GFXSTREAM_ERROR("Error: invalid feature name: '%s' (from feature string: %s)",
                        feature_name.c_str(), featureStr.c_str());
        return false;
    }
    auto& feature_info = feature_it->second;

    if (!feature_info->parseValue(feature_value)) {
        GFXSTREAM_ERROR("Error: the feature value string: %s is invalid for feature name: %s",
                        feature_value.c_str(), feature_name.c_str());
        return false;
    }

    feature_info->setReason(featureReason);

    GFXSTREAM_INFO("Gfxstream feature %s %s", feature_name.c_str(), feature_value.c_str());

    return true;
}

}  // host
}  // gfxstream
