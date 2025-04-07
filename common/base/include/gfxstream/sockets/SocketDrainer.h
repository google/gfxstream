// Copyright 2014 The Android Open Source Project
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

#include "aemu/base/Compiler.h"
#include "aemu/base/async/Looper.h"

// The following functions are used to manage graceful close of sockets
// as described by "The ultimate SO_LINGER page" [1], which requires
// several steps to do properly.
//
// To use it, simply create a socketDrainer instance after creating a
// Looper, as in:
//
//      SocketDrainer  socketDrainer(looper);
//
// When a socket needs to be gracefully closed, call the following:
//
//       socketDrainer.drainAndClose(socketFd);
//
// When the drainer is destroyed (e.g. failing out of scope, or through
// delete), all remaining sockets will be shut down and closed without
// further draining.
//
// [1] http://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable

namespace android {
namespace base {

class SocketDrainerImpl;
class SocketDrainer {
public:
    SocketDrainer(Looper* looper);

    ~SocketDrainer();

    // Add a socket to be drained and closed
    void drainAndClose(int socketFd);

    static void drainAndCloseBlocking(int socketFd);

private:
    SocketDrainerImpl*  mSocketDrainerImpl;
};

} // namespace base
} // namespace android
