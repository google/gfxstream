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

#ifndef _WIN32
#error "Only include this header when building for Windows."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

// Default FD_SETSIZE is 64 which is not enough for us.
// Note: previously set to 1024, which was not enough, see:
//     https://code.google.com/p/android/issues/detail?id=102361
//
// The value 32768 is what a typical Winsock initialization returns
// as the maximum number of sockets, see #12 on the link above. Just assume
// that this translates to the max file descriptor number for our uses.
// Of course that also means each fd_set will be 4 KiB.
#define FD_SETSIZE 32768

// Order of inclusion of winsock2.h and windows.h depends on the version
// of Mingw32 being used.
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#undef ERROR  // necessary to compile LOG(ERROR) statements.
