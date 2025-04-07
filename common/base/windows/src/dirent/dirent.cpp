// Copyright (C) 2025 The Android Open Source Project
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
// Implementation file (dirent.cpp)
#include "dirent.h"

#include <errno.h>
#include <windows.h>

#include <algorithm>
#include <codecvt>
#include <locale>
#include <memory>
#include <string>

namespace {

using file_index_t = uint64_t;

using DereferencedHandle = std::remove_pointer_t<HANDLE>;
struct HandleCloser {
    void operator()(HANDLE h) const {
        if (h != INVALID_HANDLE_VALUE) {
            ::CloseHandle(h);
        }
    }
};

using UniqueHandle = std::unique_ptr<DereferencedHandle, HandleCloser>;

// Translates Windows error codes to errno values
int translate_windows_error_to_errno(DWORD errorCode) {
    switch (errorCode) {
        case ERROR_SUCCESS:
            return 0;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return ENOENT;
        case ERROR_ACCESS_DENIED:
            return EACCES;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return EEXIST;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_NAME:
            return EINVAL;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return ENOMEM;
        case ERROR_WRITE_PROTECT:
            return EROFS;
        case ERROR_HANDLE_EOF:
            return EPIPE;
        case ERROR_HANDLE_DISK_FULL:
        case ERROR_DISK_FULL:
            return ENOSPC;
        case ERROR_NOT_SUPPORTED:
            return ENOTSUP;
        case ERROR_DIRECTORY:
            return ENOTDIR;
        case ERROR_DIR_NOT_EMPTY:
            return ENOTEMPTY;
        case ERROR_BAD_PATHNAME:
            return ENOENT;
        case ERROR_OPERATION_ABORTED:
            return EINTR;
        case ERROR_INVALID_HANDLE:
            return EBADF;
        case ERROR_FILENAME_EXCED_RANGE:
        case ERROR_CANT_RESOLVE_FILENAME:
            return ENAMETOOLONG;
        case ERROR_DEV_NOT_EXIST:
            return ENODEV;
        case ERROR_TOO_MANY_OPEN_FILES:
            return EMFILE;
        default:
            return EIO;
    }
}

// Get file index information
file_index_t get_file_index(const std::wstring& path) {
    UniqueHandle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));

    if (file.get() == INVALID_HANDLE_VALUE) {
        return 0;
    }

    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file.get(), &info)) {
        return 0;
    }

    return (static_cast<file_index_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
}

// Convert UTF-8 to wide string
std::wstring utf8_to_wide(const std::string& input) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(input);
}

// Convert wide string to UTF-8
std::string wide_to_utf8(const std::wstring& input) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(input);
}

// Prepare directory path for Windows API
std::wstring prepare_dir_path(const std::wstring& path) {
    // Check if path already has extended-length prefix
    if (path.rfind(L"\\\\?\\", 0) == 0) {
        return path;
    }

    // Add extended-length prefix
    return L"\\\\?\\" + path;
}

// Create search path with wildcard
std::wstring create_search_path(const std::wstring& dir_path) {
    std::wstring search_path = dir_path;
    if (!search_path.empty() && search_path.back() != L'\\') {
        search_path += L"\\";
    }
    search_path += L"*";
    return search_path;
}

}  // namespace

// Internal DIR structure (hidden from users)
struct InternalDir {
    HANDLE handle;
    WIN32_FIND_DATAW find_data;
    dirent entry;
    std::wstring path;         // Original path (wide)
    std::wstring search_path;  // Search path with pattern
    bool first;
    bool end_reached;
    long current_position;  // Current position in the directory

    // Constructor
    InternalDir()
        : handle(INVALID_HANDLE_VALUE), first(true), end_reached(false), current_position(0) {
        memset(&entry, 0, sizeof(dirent));
    }

    // Destructor
    ~InternalDir() {
        if (handle != INVALID_HANDLE_VALUE) {
            FindClose(handle);
        }
    }

   private:
    // Prevent copying and assignment to maintain unique ownership
    InternalDir(const InternalDir&) = delete;
    InternalDir& operator=(const InternalDir&) = delete;
};

// Opaque DIR type (declared in header)
struct DIR {
    std::unique_ptr<InternalDir> pImpl;  // std::unique_ptr to hold the internal structure

    DIR() : pImpl(std::make_unique<InternalDir>()) {}

   private:
    // Prevent copying and assignment to maintain unique ownership
    DIR(const DIR&) = delete;
    DIR& operator=(const DIR&) = delete;
};

DIR* opendir(const char* name) {
    if (!name) {
        errno = EINVAL;
        return nullptr;
    }

    // Convert to wide string
    std::wstring wide_path = utf8_to_wide(name);
    if (wide_path.empty() && !std::string(name).empty()) {
        errno = EINVAL;
        return nullptr;
    }

    // Check if path exists and is a directory
    DWORD attrs = GetFileAttributesW(wide_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        errno = translate_windows_error_to_errno(GetLastError());
        return nullptr;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        errno = ENOTDIR;
        return nullptr;
    }

    // Prepare directory path
    std::wstring dir_path = prepare_dir_path(wide_path);

    // Create search path
    std::wstring search_path = create_search_path(dir_path);

    // Allocate and initialize DIR structure using unique_ptr
    std::unique_ptr<DIR> dir = std::make_unique<DIR>();
    if (!dir) {
        errno = ENOMEM;
        return nullptr;
    }

    // Initialize InternalDir structure
    dir->pImpl->handle = FindFirstFileW(search_path.c_str(), &dir->pImpl->find_data);
    if (dir->pImpl->handle == INVALID_HANDLE_VALUE) {
        errno = translate_windows_error_to_errno(GetLastError());
        return nullptr;
    }

    dir->pImpl->path = dir_path;
    dir->pImpl->search_path = search_path;
    dir->pImpl->first = true;
    dir->pImpl->end_reached = false;

    return dir.release();  // Release ownership to the caller
}

struct dirent* readdir(DIR* dirp) {
    if (!dirp) {
        errno = EBADF;
        return nullptr;
    }

    if (dirp->pImpl->end_reached) {
        return nullptr;
    }

    while (true) {
        if (!dirp->pImpl->first && !FindNextFileW(dirp->pImpl->handle, &dirp->pImpl->find_data)) {
            DWORD lastError = GetLastError();
            if (lastError == ERROR_NO_MORE_FILES) {
                dirp->pImpl->end_reached = true;
                return nullptr;
            } else {
                errno = translate_windows_error_to_errno(lastError);
                return nullptr;
            }
        }
        dirp->pImpl->first = false;

        // Skip "." and ".." entries
        if (wcscmp(dirp->pImpl->find_data.cFileName, L".") == 0 ||
            wcscmp(dirp->pImpl->find_data.cFileName, L"..") == 0) {
            continue;
        }

        // Convert filename to UTF-8
        std::string utf8_filename = wide_to_utf8(dirp->pImpl->find_data.cFileName);
        if (utf8_filename.empty() && !std::wstring(dirp->pImpl->find_data.cFileName).empty()) {
            errno = ENAMETOOLONG;
            return nullptr;
        }

        // Copy filename to dirent structure, with bounds checking
        if (utf8_filename.length() >= sizeof(dirp->pImpl->entry.d_name)) {
            errno = ENAMETOOLONG;
            return nullptr;
        }
        strcpy(dirp->pImpl->entry.d_name, utf8_filename.c_str());

        // Get full path for the current file
        std::wstring fullPath = dirp->pImpl->path + L"\\" + dirp->pImpl->find_data.cFileName;

        // Get file index information
        dirp->pImpl->entry.d_ino = get_file_index(fullPath);

        // Increment position after successfully reading an entry
        dirp->pImpl->current_position++;

        return &dirp->pImpl->entry;
    }
}

int closedir(DIR* dirp) {
    if (!dirp) {
        errno = EBADF;
        return -1;
    }

    // Destructor of unique_ptr<InternalDir> will be called automatically,
    // releasing resources held by InternalDir.

    delete dirp;  // Release memory held by DIR
    return 0;
}

void rewinddir(DIR* dirp) {
    if (!dirp) {
        errno = EBADF;
        return;
    }

    if (dirp->pImpl->handle != INVALID_HANDLE_VALUE) {
        FindClose(dirp->pImpl->handle);
    }

    dirp->pImpl->handle = FindFirstFileW(dirp->pImpl->search_path.c_str(), &dirp->pImpl->find_data);
    if (dirp->pImpl->handle == INVALID_HANDLE_VALUE) {
        errno = translate_windows_error_to_errno(GetLastError());
        return;
    }
    dirp->pImpl->first = true;
    dirp->pImpl->end_reached = false;
    dirp->pImpl->current_position = 0;  // Reset position
}

long telldir(DIR* dirp) {
    if (!dirp) {
        errno = EBADF;
        return -1;
    }
    return dirp->pImpl->end_reached ? -1 : dirp->pImpl->current_position;
}

void seekdir(DIR* dirp, long loc) {
    if (!dirp) {
        errno = EBADF;
        return;
    }

    if (loc == 0) {
        rewinddir(dirp);
    } else if (loc == -1) {
        // Seeking to the end is equivalent to reading until the end
        while (readdir(dirp) != nullptr);
    } else if (loc > 0) {
        // Seek forward to a specific position
        rewinddir(dirp);  // Start from the beginning
        for (long i = 0; i < loc; ++i) {
            if (readdir(dirp) == nullptr) {
                // Reached the end before the desired position
                errno = EINVAL;
                return;
            }
        }
    } else {
        errno = EINVAL;  // Negative positions other than -1 are not supported
        return;
    }
}