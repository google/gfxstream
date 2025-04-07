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

#include <errno.h>
#ifndef dfatal
#include "aemu/base/logging/CLog.h"
#endif

namespace android {
namespace base {

// Set EINTR_WRAPPER_DEBUG to 1 to force the debug version of HANDLE_EINTR
// which will call eintrWrapperFatal() is the system call loops
// too many times, or 0 to get it to loop indefinitly.
// Mostly used for unit testing.
// If the macro is undefined, auto-detect the value based on NDEBUG.
#if !defined(EINTR_WRAPPER_DEBUG)
#ifdef NDEBUG
#define EINTR_WRAPPER_DEBUG 0
#else
#define EINTR_WRAPPER_DEBUG 1
#endif
#endif

// HANDLE_EINTR() is a macro used to handle EINTR return values when
// calling system calls like open() or read() on Posix systems.
//
// By default, this will loop indefinitly, retrying the call until
// the result is no longer -1/EINTR, except in debug mode, where a
// loop counter is actually used and to provoke a fatal error if there
// are too many loops.
//
// Usage example:
//     int ret = HANDLE_EINTR(open("/some/file/path", O_RDONLY));
//
// IMPORTANT: Do not use with the close() system call (use IGNORE_EINTR()
// instead).
//
// - On Linux, the file descriptor is always already closed when this
//   function returns -1/EINTR, and calling it again with the same
//   parameters risks closing another file descriptor open by another
//   thread in parallel!
//
// - On OS X, whether the file descriptor is closed or not is pretty
//   much random! It's better to leave the descriptor open than risk
//   closing another one by mistake :(
//
#define MAX_EINTR_LOOP_COUNT 100

#ifdef _WIN32
#define HANDLE_EINTR(x) (x)
#elif EINTR_WRAPPER_DEBUG == 0
#define HANDLE_EINTR(x)                                       \
    __extension__({                                           \
        __typeof__(x) eintr_wrapper_result;                   \
        do {                                                  \
            eintr_wrapper_result = (x);                       \
        } while (eintr_wrapper_result < 0 && errno == EINTR); \
        eintr_wrapper_result;                                 \
    })
#else  // !_WIN32 && EINTR_WRAPPER_DEBUG

#define HANDLE_EINTR(x)                                                 \
    __extension__({                                                     \
        __typeof__(x) eintr_wrapper_result;                             \
        int eintr_wrapper_loop_count = 0;                               \
        for (;;) {                                                      \
            eintr_wrapper_result = (x);                                 \
            if (eintr_wrapper_result != -1 || errno != EINTR) break;    \
            ++eintr_wrapper_loop_count;                                 \
            if (eintr_wrapper_loop_count >= MAX_EINTR_LOOP_COUNT)       \
                dfatal("Looping around EINTR too many times");          \
        };                                                              \
        eintr_wrapper_result;                                           \
    })
#endif  // !_WIN32 && EINTR_WRAPPER_DEBUG

// IGNORE_EINTR() is a macro used to perform a system call and ignore
// an EINTR result, i.e. it will return 0 instead of -1 if this occurs.
// This is mostly used with the close() system call, as described
// in the HANDLE_EINTR() documentation.
#ifdef _WIN32
#define IGNORE_EINTR(x) (x)
#else
#define IGNORE_EINTR(x)                                                             \
    __extension__({                                                                 \
        __typeof__(x) eintr_wrapper_result = (x);                                   \
        if (eintr_wrapper_result == -1 && errno == EINTR) eintr_wrapper_result = 0; \
        eintr_wrapper_result;                                                       \
    })
#endif

}  // namespace base
}  // namespace android
