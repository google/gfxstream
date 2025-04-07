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

#ifdef _MSC_VER
  #include "aemu/base/msvc.h"
#endif

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

FILE* android_fopen(const char* path, const char* mode);
FILE* android_popen(const char* path, const char* mode);

// This thing uses macro varargs to select which open function to use.
// Because __VA_ARGS__ consumes a different amount of arguments the call_name
// will have a different name depending on how many arguments are passed
#define DETERMINE_OPEN_CALL(first, second, third, call_name, ...) call_name
#define android_open(...) DETERMINE_OPEN_CALL(__VA_ARGS__, \
                                              android_open_with_mode, \
                                              android_open_without_mode) \
                                                  (__VA_ARGS__)

// WARNING: Do not use these, just use the android_open macro above the way
// you would use open. The version to use gets picked automatically
int android_open_without_mode(const char* path, int flags);
int android_open_with_mode(const char* path, int flags, mode_t mode);

#ifdef _WIN32
  #define android_lstat(path, buf) android_stat((path), (buf))
  int android_stat(const char* path, struct _stati64* buf);
#else
int android_stat(const char* path, struct stat* buf);
int android_lstat(const char* path, struct stat* buf);
#endif

int android_access(const char* path, int mode);
int android_mkdir(const char* path, mode_t mode);

int android_creat(const char* path, mode_t mode);

int android_unlink(const char* path);
int android_chmod(const char* path, mode_t mode);

int android_rmdir(const char* path);
