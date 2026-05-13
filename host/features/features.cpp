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


#ifndef VK_VERSION_1_0
// Define here to avoid importing vulkan headers
#define VK_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))
#define VK_API_VERSION_VARIANT(version) ((uint32_t)(version) >> 29U)
#define VK_API_VERSION_MAJOR(version) (((uint32_t)(version) >> 22U) & 0x7FU)
#define VK_API_VERSION_MINOR(version) (((uint32_t)(version) >> 12U) & 0x3FFU)
#define VK_API_VERSION_PATCH(version) ((uint32_t)(version) & 0xFFFU)
#endif

namespace gfxstream {
namespace host {

bool BoolFeatureInfo::parseValue(std::string_view strValue) {
    if (strValue != "enabled" && strValue != "disabled") {
        return false;
    }

    setEnabled(strValue == "enabled");
    return true;
}

std::string BoolFeatureInfo::getValueReadable() const {
    if (std::get<bool>(value)) {
        return "enabled";
    } else {
        return "disabled";
    }
}

bool StringFeatureInfo::parseValue(std::string_view strValue) {
    value = StringFeatureValue(strValue);
    return true;
}

std::string StringFeatureInfo::getValueReadable() const {
    auto strValueOpt = std::get<StringFeatureValue>(value);
    if (strValueOpt) {
        return *strValueOpt;
    } else {
        return "(Unset)";
    }
}

bool U32FeatureInfo::parseValue(std::string_view strValue) {
    if (strValue.empty()) {
        value = U32FeatureValue(std::nullopt);
        return true;
    }
    int valueStart = 0;
    int valueBase = 10;
    // Check if the value is given as a hexadecimal value
    if (strValue.size() > 2 && strValue[0] == '0' &&
        (strValue[1] == 'x' || strValue[1] == 'X')) {
        valueStart = 2;
        valueBase = 16;
    }
    uint32_t val;
    auto res = std::from_chars(strValue.data() + valueStart, strValue.data() + strValue.size(),
                               val, valueBase);
    if (res.ec == std::errc()) {
        value = U32FeatureValue(val);
        return true;
    }
    GFXSTREAM_ERROR("Cannot parse '%.*s' for feature '%s'", (int)strValue.size(), strValue.data(), name.c_str());
    return false;
}

std::string U32FeatureInfo::getValueReadable() const {
    auto u32ValueOpt = std::get<U32FeatureValue>(value);
    if (!u32ValueOpt) {
        return "(Unset)";
    }

    // E.g. '123 (0x7B)'
    const uint32_t val = u32ValueOpt.value();
    std::ostringstream oss;
    oss << val << " (0x" << std::hex << val << ")";
    return oss.str();
}

bool VulkanVersionFeatureInfo::parseValue(std::string_view strValue) {
    // Parse vulkan version info, can be in 1.2, 1.2.3 or integer format
    if (strValue.find('.') != std::string_view::npos) {
        uint32_t parts[3] = {0, 0, 0};
        size_t pos = 0, idx = 0;

        while (idx < 3 && pos < strValue.size()) {
            auto [ptr, ec] = std::from_chars(strValue.data() + pos,
                                             strValue.data() + strValue.size(), parts[idx++]);
            pos = (ptr - strValue.data()) + 1;  // Skip the dot
        }

        value = VK_MAKE_API_VERSION(0, parts[0], parts[1], parts[2]);
        return true;
    }

    // Try parsing as regular unsigned integer
    return U32FeatureInfo::parseValue(strValue);
}

std::string VulkanVersionFeatureInfo::getValueReadable() const {
    auto u32ValueOpt = std::get<U32FeatureValue>(value);
    if (!u32ValueOpt) {
        return "(Unset)";
    }

    const uint32_t versionVal = u32ValueOpt.value();
    const uint32_t major = VK_API_VERSION_MAJOR(versionVal);
    const uint32_t minor = VK_API_VERSION_MINOR(versionVal);
    const uint32_t patch = VK_API_VERSION_PATCH(versionVal);

    // E.g. '1.2.3'
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

FeatureSet::FeatureSet(const FeatureSet& rhs) : FeatureSet() {
    *this = rhs;
}

FeatureSet& FeatureSet::operator=(const FeatureSet& rhs) {
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
