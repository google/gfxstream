// Copyright 2023 The Android Open Source Project
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

#ifndef _AEMU_BITS_SOCKET_H_
#ifndef __linux__
#ifndef __QNX__
// Make sure these are defined and don't change anything if used.
enum {
    SOCK_CLOEXEC = 0,
#ifndef __APPLE__
    O_CLOEXEC = 0
#endif
};
#define _AEMU_BITS_SOCKET_H_
#endif  // !__QNX__
#endif  // !__linux__
#endif

#ifdef _MSC_VER

#include <windows.h>

#include <io.h>
#include <stdint.h>

#include "sys/cdefs.h"

__BEGIN_DECLS

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/cdefs.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef fseeko
#define fseeko _fseeki64
#endif

#ifndef ftello
#define ftello _ftelli64
#endif

extern int asprintf(char** buf, const char* format, ...);
extern int vasprintf(char** buf, const char* format, va_list args);

__END_DECLS

#endif