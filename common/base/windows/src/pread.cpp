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

#include <io.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <windows.h>

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }
    auto handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    DWORD cRead;
    OVERLAPPED overlapped = {.OffsetHigh = (DWORD)((offset & 0xFFFFFFFF00000000LL) >> 32),
                             .Offset = (DWORD)(offset & 0xFFFFFFFFLL)};
    bool rd = ReadFile(handle, buf, count, &cRead, &overlapped);
    if (!rd) {
        auto err = GetLastError();
        switch (err) {
            case ERROR_IO_PENDING:
                errno = EAGAIN;
                break;
            case ERROR_HANDLE_EOF:
                cRead = 0;
                errno = 0;
                return 0;
            default:
                // Oh oh
                errno = EINVAL;
        }
        return -1;
    }

    return cRead;
}