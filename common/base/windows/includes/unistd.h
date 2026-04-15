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

// A minimal set of functions found in unistd.h
#ifndef _AEMU_UNISTD_H_  /* use the same guard as in aemu to prevent conflicts */
#define _AEMU_UNISTD_H_

#include "compat_compiler.h"
#include <process.h>

ANDROID_BEGIN_HEADER

#include <direct.h>
#include <inttypes.h>
#include <io.h>
#include <stdio.h>
#include <sys/stat.h>
#include <limits.h>

typedef long long ssize_t;
typedef unsigned long long size_t;
typedef long off_t;
typedef int64_t off64_t;
typedef int mode_t;


#define lseek(a, b, c) _lseek(a, b, c)
#define lseek64 _lseeki64

// Define for convenience only in mingw. This is
// convenient for the _access function in Windows.
#if !defined(F_OK)
#define F_OK 0 /* Check for file existence */
#endif
#if !defined(X_OK)
#define X_OK 1 /* Check for execute permission (not supported in Windows) */
#endif
#if !defined(W_OK)
#define W_OK 2 /* Check for write permission */
#endif
#if !defined(R_OK)
#define R_OK 4 /* Check for read permission */
#endif

#define STDIN_FILENO _fileno(stdin)
#define STDOUT_FILENO _fileno(stdout)
#define STDERR_FILENO _fileno(stderr)

int usleep(long usec);
unsigned int sleep(unsigned int seconds);

// Qemu will redefine this if it can.
int _ftruncate(int fd, off_t length);
#define ftruncate _ftruncate


#define __try1(x) __try
#define __except1 __except (EXCEPTION_EXECUTE_HANDLER)

ANDROID_END_HEADER

#endif  /* _AEMU_UNISTD_H_ */

