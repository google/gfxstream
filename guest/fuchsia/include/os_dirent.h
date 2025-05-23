/*
 * Copyright 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OS_DIRENT_H
#define OS_DIRENT_H

#include <limits.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct os_dirent {
    ino_t d_ino;
    char d_name[NAME_MAX + 1];
};

#define OS_INO_UNKNOWN (0xFFFFFFFFFFFFFFFFul)

typedef struct os_dir os_dir_t;

os_dir_t* os_opendir(const char* path);

// Not threadsafe.
struct os_dirent* os_readdir(os_dir_t* dir);

int os_closedir(os_dir_t* dir);

#ifdef __cplusplus
}
#endif

#endif /* OS_DIRENT_H */
