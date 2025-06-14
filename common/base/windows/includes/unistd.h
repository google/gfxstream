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
#if !defined(_AEMU_UNISTD_H_) && !defined(_MSVC_UNISTD_H)
#define _AEMU_UNISTD_H_
#define _MSVC_UNISTD_H

#include "compat_compiler.h"
#include <process.h>

ANDROID_BEGIN_HEADER

#include <direct.h>
#include <inttypes.h>
#include <io.h>
#include <stdio.h>
#include <sys/stat.h>

typedef long long ssize_t;
typedef unsigned long long size_t;
typedef long off_t;
typedef int64_t off64_t;
typedef int mode_t;

#undef fstat
#define fstat _fstat64

#define lseek(a, b, c) _lseek(a, b, c)
#define lseek64 _lseeki64

/* File type and permission flags for stat(), general mask */
#if !defined(S_IFMT)
#define S_IFMT _S_IFMT
#endif

/* Directory bit */
#if !defined(S_IFDIR)
#define S_IFDIR _S_IFDIR
#endif

/* Character device bit */
#if !defined(S_IFCHR)
#define S_IFCHR _S_IFCHR
#endif

/* Pipe bit */
#if !defined(S_IFFIFO)
#define S_IFFIFO _S_IFFIFO
#endif

/* Regular file bit */
#if !defined(S_IFREG)
#define S_IFREG _S_IFREG
#endif

/* Read permission */
#if !defined(S_IREAD)
#define S_IREAD _S_IREAD
#endif

/* Write permission */
#if !defined(S_IWRITE)
#define S_IWRITE _S_IWRITE
#endif

/* Execute permission */
#if !defined(S_IEXEC)
#define S_IEXEC _S_IEXEC
#endif

/* Pipe */
#if !defined(S_IFIFO)
#define S_IFIFO _S_IFIFO
#endif

/* Block device */
#if !defined(S_IFBLK)
#define S_IFBLK 0
#endif

/* Link */
#if !defined(S_IFLNK)
#define S_IFLNK 0
#endif

/* Socket */
#if !defined(S_IFSOCK)
#define S_IFSOCK 0
#endif

/* Read user permission */
#if !defined(S_IRUSR)
#define S_IRUSR S_IREAD
#endif

/* Write user permission */
#if !defined(S_IWUSR)
#define S_IWUSR S_IWRITE
#endif

/* Execute user permission */
#if !defined(S_IXUSR)
#define S_IXUSR 0
#endif

/* Read group permission */
#if !defined(S_IRGRP)
#define S_IRGRP 0
#endif

/* Write group permission */
#if !defined(S_IWGRP)
#define S_IWGRP 0
#endif

/* Execute group permission */
#if !defined(S_IXGRP)
#define S_IXGRP 0
#endif

/* Read others permission */
#if !defined(S_IROTH)
#define S_IROTH 0
#endif

/* Write others permission */
#if !defined(S_IWOTH)
#define S_IWOTH 0
#endif

/* Execute others permission */
#if !defined(S_IXOTH)
#define S_IXOTH 0
#endif

/* Maximum length of file name */
#if !defined(PATH_MAX)
#define PATH_MAX MAX_PATH
#endif
#if !defined(FILENAME_MAX)
#define FILENAME_MAX MAX_PATH
#endif
#if !defined(NAME_MAX)
#define NAME_MAX FILENAME_MAX
#endif

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
ssize_t pread(int fd, void *buf, size_t count, off_t offset);

int usleep(long usec);
unsigned int sleep(unsigned int seconds);

// Qemu will redefine this if it can.
int _ftruncate(int fd, off_t length);
#define ftruncate _ftruncate


#define __try1(x) __try
#define __except1 __except (EXCEPTION_EXECUTE_HANDLER)

ANDROID_END_HEADER
#endif	/* Not _AEMU_UNISTD_H_ */
