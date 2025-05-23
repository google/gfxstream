// Copyright 2018 The Android Open Source Project
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

#include <qemu_pipe_bp.h>

int qemu_pipe_read_fully(QEMU_PIPE_HANDLE pipe, void* buffer, int size) {
    char* p = (char*)buffer;

    while (size > 0) {
      int n = QEMU_PIPE_RETRY(qemu_pipe_read(pipe, p, size));
      if (n < 0) return n;

      p += n;
      size -= n;
    }

    return 0;
}

int qemu_pipe_write_fully(QEMU_PIPE_HANDLE pipe, const void* buffer, int size) {
    const char* p = (const char*)buffer;

    while (size > 0) {
      int n = QEMU_PIPE_RETRY(qemu_pipe_write(pipe, p, size));
      if (n < 0) return n;

      p += n;
      size -= n;
    }

    return 0;
}
