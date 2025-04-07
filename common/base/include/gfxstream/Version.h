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

#include <string>
#include <string_view>
#include <tuple>

namespace android {
namespace base {

// Class |Version| is a class for software version manipulations
// it is able to parse, store, compare and convert version back to string
// Expected string format is "major.minor.micro[.build]", where all
// components are unsigned numbers (and, hopefully, reasonably small)
class Version {
public:
    enum Component { kMajor, kMinor, kMicro, kBuild };

    using ComponentType = unsigned int;

    // This is the value for an invalid/missing version component.
    static constexpr ComponentType kNone = static_cast<ComponentType>(-1);

    explicit Version(const char* ver);
    explicit Version(const std::string& ver) : Version(ver.data()) {}

    constexpr Version();

    constexpr Version(ComponentType major,
                      ComponentType minor,
                      ComponentType micro,
                      ComponentType build = 0);

    constexpr bool isValid() const;

    constexpr bool operator<(const Version& other) const;
    constexpr bool operator>(const Version& other) const;
    constexpr bool operator==(const Version& other) const;
    constexpr bool operator!=(const Version& other) const;

    static constexpr Version invalid();

    std::string toString() const;

    template <Component C>
    constexpr ComponentType component() const {
        return std::get<static_cast<size_t>(C)>(mData);
    }

private:
    ComponentType& component(Component c);

private:
    static const int kComponentCount = kBuild + 1;

    std::tuple<ComponentType, ComponentType, ComponentType, ComponentType>
            mData;
};

// all constexpr functions have to be defined in the header, just like templates

constexpr Version::Version() : Version(kNone, kNone, kNone) {}

constexpr Version::Version(ComponentType major,
                           ComponentType minor,
                           ComponentType micro,
                           ComponentType build)
    : mData(major, minor, micro, build) {}

constexpr bool Version::isValid() const {
    return *this != invalid();
}

constexpr bool Version::operator<(const Version& other) const {
    return mData < other.mData;
}

constexpr bool Version::operator>(const Version& other) const {
    return mData > other.mData;
}

constexpr bool Version::operator==(const Version& other) const {
    return mData == other.mData;
}

constexpr bool Version::operator!=(const Version& other) const {
    return !(*this == other);
}

constexpr Version Version::invalid() {
    return Version(kNone, kNone, kNone);
}

}  // namespace android
}  // namespace base
