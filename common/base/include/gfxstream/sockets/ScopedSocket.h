// Copyright 2014-2017 The Android Open Source Project
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

#include "aemu/base/sockets/SocketUtils.h"

#include <utility>

namespace android {
namespace base {

class ScopedSocket {
public:
    constexpr ScopedSocket() = default;
    constexpr ScopedSocket(int socket) : mSocket(socket) {}
    ScopedSocket(ScopedSocket&& other) : mSocket(other.release()) {}
    ~ScopedSocket() { close(); }

    ScopedSocket& operator=(ScopedSocket&& other) {
        reset(other.release());
        return *this;
    }

    bool valid() const { return mSocket >= 0; }

    int get() const { return mSocket; }

    void close() {
        if (valid()) {
            socketClose(release());
        }
    }

    int release() {
        int result = mSocket;
        mSocket = -1;
        return result;
    }

    void reset(int socket) {
        close();
        mSocket = socket;
    }

    void swap(ScopedSocket* other) {
        std::swap(mSocket, other->mSocket);
    }

private:
    int mSocket = -1;
};

inline void swap(ScopedSocket& one, ScopedSocket& two) {
    one.swap(&two);
}

}  // namespace base
}  // namespace android
