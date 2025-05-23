/*
 * Copyright (C) 2020 The Android Open Source Project
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

#pragma once
#include <qemu_pipe_types_bp.h>

#ifdef __cplusplus
extern "C" {
#endif

QEMU_PIPE_HANDLE qemu_pipe_open_ns(const char* ns, const char* pipeName, int flags);
QEMU_PIPE_HANDLE qemu_pipe_open(const char* pipeName);
void qemu_pipe_close(QEMU_PIPE_HANDLE pipe);

int qemu_pipe_read_fully(QEMU_PIPE_HANDLE pipe, void* buffer, int size);
int qemu_pipe_write_fully(QEMU_PIPE_HANDLE pipe, const void* buffer, int size);
int qemu_pipe_read(QEMU_PIPE_HANDLE pipe, void* buffer, int size);
int qemu_pipe_write(QEMU_PIPE_HANDLE pipe, const void* buffer, int size);

int qemu_pipe_try_again(int ret);
void qemu_pipe_print_error(QEMU_PIPE_HANDLE pipe);

#ifdef __cplusplus
}  // extern "C"
#endif

#define QEMU_PIPE_RETRY(exp) ({ \
    __typeof__(exp) _rc; \
    do { \
        _rc = (exp); \
    } while (qemu_pipe_try_again(_rc)); \
    _rc; })
