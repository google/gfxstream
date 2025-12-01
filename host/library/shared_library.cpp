// Copyright (C) 2014 The Android Open Source Project
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

#include "gfxstream/shared_library.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <dlfcn.h>
#include <stdlib.h>
#endif

#include <functional>
#include <vector>

#include "gfxstream/common/logging.h"
#include "gfxstream/files/PathUtils.h"
#include "gfxstream/strings.h"
#include "gfxstream/system/System.h"

using gfxstream::base::PathUtils;

namespace gfxstream {
namespace host {

static SharedLibrary::LibraryMap s_libraryMap;

// static
SharedLibrary* SharedLibrary::open(const char* libraryName) {
    GFXSTREAM_INFO("SharedLibrary::open for [%s]", libraryName);
    char error[1];
    return open(libraryName, error, sizeof(error));
}

SharedLibrary* SharedLibrary::open(const char* libraryName,
                                   char* error,
                                   size_t errorSize) {
    auto lib = s_libraryMap.find(libraryName);

    if (lib == s_libraryMap.end()) {
        GFXSTREAM_INFO("SharedLibrary::open for [%s]: not found in map, open for the first time",
             libraryName);
        SharedLibrary* load = do_open(libraryName, error, errorSize);
        if (load != nullptr) {
            s_libraryMap[libraryName] =
                std::unique_ptr<SharedLibrary, SharedLibrary::Deleter>(load);
        }
        return load;
    }

    return lib->second.get();
}

#ifdef _WIN32

// static
SharedLibrary* SharedLibrary::do_open(const char* libraryName,
                                      char* error,
                                      size_t errorSize) {
    GFXSTREAM_INFO("SharedLibrary::open for [%s] (win32): call LoadLibrary", libraryName);
    HMODULE lib = LoadLibraryA(libraryName);

    if (lib) {
        constexpr size_t kMaxPathLength = 2048;
        char fullPath[kMaxPathLength];
        GetModuleFileNameA(lib, fullPath, kMaxPathLength);
        GFXSTREAM_INFO("SharedLibrary::open succeeded for [%s]. File name: [%s]", libraryName, fullPath);
        return new SharedLibrary(lib);
    }

    if (errorSize == 0) {
        GFXSTREAM_INFO("SharedLibrary::open for [%s] failed, but no error", libraryName);
        return NULL;
    }

    // Convert error into human-readable message.
    DWORD errorCode = ::GetLastError();
    LPSTR message = NULL;
    size_t messageLen = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR) &message,
            0,
            NULL);

    int ret = snprintf(error, errorSize, "%.*s", (int)messageLen, message);
    if (ret < 0 || ret == static_cast<int>(errorSize)) {
        // snprintf() on Windows doesn't behave as expected by C99,
        // this path is to ensure that the result is always properly
        // zero-terminated.
        ret = static_cast<int>(errorSize - 1);
        error[ret] = '\0';
    }
    // Remove any trailing \r\n added by FormatMessage
    if (ret > 0 && error[ret - 1] == '\n') {
        error[--ret] = '\0';
    }
    if (ret > 0 && error[ret - 1] == '\r') {
        error[--ret] = '\0';
    }
    GFXSTREAM_INFO("Failed to load [%s]. Error string: [%s]", libraryName, error);

    return NULL;
}

SharedLibrary::SharedLibrary(HandleType lib) : mLib(lib) {}

SharedLibrary::~SharedLibrary() {
    if (mLib) {
        // BUG: 66013149
        // In windows it sometimes hang on exit when destroying s_libraryMap.
        // Let's skip freeing the library, since pretty much the only situation
        // we need to do it, is on exit.
        //FreeLibrary(mLib);
    }
}

SharedLibrary::FunctionPtr SharedLibrary::findSymbol(
        const char* symbolName) const {
    if (!mLib || !symbolName) {
        return NULL;
    }
    return reinterpret_cast<FunctionPtr>(
                GetProcAddress(mLib, symbolName));
}

#else // !_WIN32

// static
SharedLibrary* SharedLibrary::do_open(const char* libraryName,
                                      char* error,
                                      size_t errorSize) {
    GFXSTREAM_INFO("SharedLibrary::open for [%s] (posix): begin", libraryName);

    const char* libPath = libraryName;
    char* path = NULL;

    const char* libBaseName = strrchr(libraryName, '/');
    if (!libBaseName) {
        libBaseName = libraryName;
    }

    if (!strchr(libBaseName, '.')) {
        // There is no extension in this library name, so append one.
#ifdef __APPLE__
        static const char kDllExtension[] = ".dylib";
#else
        static const char kDllExtension[] = ".so";
#endif
        size_t pathLen = strlen(libraryName) + sizeof(kDllExtension);
        path = static_cast<char*>(malloc(pathLen));
        snprintf(path, pathLen, "%s%s", libraryName, kDllExtension);
        libPath = path;
    }

    dlerror();  // clear error.

#ifdef __APPLE__
    // On OSX, some libraries don't include an extension (notably OpenGL)
    // On OSX we try to open |libraryName| first.  If that doesn't exist,
    // we try |libraryName|.dylib
    GFXSTREAM_INFO("SharedLibrary::open for [%s] (posix,darwin): call dlopen", libraryName);
    void* lib = dlopen(libraryName, RTLD_NOW);
    if (lib == NULL) {
        GFXSTREAM_INFO(
            "SharedLibrary::open for [%s] (posix,darwin): failed, "
            "try again with [%s]",
            libraryName, libPath);
        lib = dlopen(libPath, RTLD_NOW);
    }
#else
    GFXSTREAM_INFO("SharedLibrary::open for [%s] (posix,linux): call dlopen on [%s]", libraryName, libPath);
    void* lib = nullptr;
    const std::vector<std::string> ldLibraryPaths =
        gfxstream::Split(gfxstream::base::getEnvironmentVariable("LD_LIBRARY_PATH"), ":");
    for (const std::string& ldLibraryPath : ldLibraryPaths) {
        if (ldLibraryPath.empty()) {
            continue;
        }

        const std::string fullpath = PathUtils::join(ldLibraryPath, libPath);
        GFXSTREAM_VERBOSE("Calling dlopen on %s.", fullpath.c_str());

        lib = dlopen(fullpath.c_str(), RTLD_NOW);
        if (lib != nullptr) {
            break;
        }
    }
    if (lib == nullptr) {
        lib = dlopen(libPath, RTLD_NOW);
    }
#endif

    if (path) {
        free(path);
    }

    if (lib) {
        GFXSTREAM_INFO("SharedLibrary::open succeeded for [%s].", libraryName);
        return new SharedLibrary(lib);
    }

    snprintf(error, errorSize, "%s", dlerror());
    GFXSTREAM_INFO("SharedLibrary::open for [%s] failed (posix). dlerror: [%s]", libraryName, error);
    return nullptr;
}

SharedLibrary::SharedLibrary(HandleType lib) : mLib(lib) {}

SharedLibrary::~SharedLibrary() {
    if (mLib) {
        dlclose(mLib);
    }
}

SharedLibrary::FunctionPtr SharedLibrary::findSymbol(
        const char* symbolName) const {
    if (!mLib || !symbolName) {
        return nullptr;
    }
    return reinterpret_cast<FunctionPtr>(dlsym(mLib, symbolName));
}

#endif  // !_WIN32

}  // namespace host
}  // namespace gfxstream
