// Copyright 2025 The Android Open Source Project
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

#include <fcntl.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

#include "gfxstream/memory/UdmabufCreator.h"

namespace gfxstream {
namespace base {

bool IsAndroidKernel6_6() {
    int ret;
    int major = 0;
    int minor = 0;
    struct utsname uname_data;

    ret = uname(&uname_data);
    if (ret < 0) {
        return false;
    }

    // Expects a format like "X.Y.Z-something" or "X.Y-something"
    std::string kernelVersion = uname_data.release;

    size_t firstDot = kernelVersion.find('.');
    if (firstDot == std::string::npos) {
        return false;
    }

    size_t secondDot = kernelVersion.find('.', firstDot + 1);
    major = std::stoi(kernelVersion.substr(0, firstDot));
    if (secondDot != std::string::npos) {
        minor = std::stoi(kernelVersion.substr(firstDot + 1, secondDot - (firstDot + 1)));
    } else {
        // If no second dot, it might be X.Y-something
        // We need to find the end of the minor version number, which is
        // usually before a hyphen or a space.
        size_t endOfMinor = kernelVersion.find_first_not_of("0123456789", firstDot + 1);
        if (endOfMinor == std::string::npos) {
            minor = std::stoi(kernelVersion.substr(firstDot + 1));
        } else {
            minor = std::stoi(kernelVersion.substr(firstDot + 1, endOfMinor - (firstDot + 1)));
        }
    }

    // udmabuf only needed on certain platforms
#if !defined(__ANDROID__)
    return false;
#endif

    // udmabuf only needed on 6.6 kernel
    if (major != 6 || minor != 6) {
        return false;
    }

    return true;
}

bool HasUdmabufDevice() { return access("/dev/udmabuf", F_OK) == 0; }

UdmabufCreator::UdmabufCreator() {}

UdmabufCreator::~UdmabufCreator() {}

bool UdmabufCreator::init() {
    auto rawDescriptor = open("/dev/udmabuf", O_RDWR);
    if (rawDescriptor < 0) {
        return false;
    }

    mOsHandleCreator = ManagedDescriptor(rawDescriptor);
    return true;
}

std::optional<DescriptorType> UdmabufCreator::handleFromSharedMemory(SharedMemory& memory) {
    // An udmabuf-created fd and a shared memory fd have separate lifetimes, but alias the same set
    // of kernel pages.
    const struct udmabuf_create create = {
        .memfd = (uint32_t)memory.getFd(),
        .flags = UDMABUF_FLAGS_CLOEXEC,
        .size = (uint64_t)memory.size(),
    };

    DescriptorType udmabuf = ioctl(*mOsHandleCreator.get(), UDMABUF_CREATE, &create);
    if (udmabuf < 0) {
        return std::nullopt;
    }

    std::optional<DescriptorType> descriptorOpt = std::make_optional(udmabuf);
    return descriptorOpt;
}

}  // namespace base
}  // namespace gfxstream
