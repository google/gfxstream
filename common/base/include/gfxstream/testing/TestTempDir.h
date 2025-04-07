// Copyright (C) 2015 The Android Open Source Project
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

#include "aemu/base/Compiler.h"
#include "aemu/base/StringFormat.h"
#include "aemu/base/files/PathUtils.h"

#include "file_io.h"

#ifdef _WIN32
#include <windows.h>
#include <random>
#include <sstream>
#include <filesystem>
#undef ERROR
#include <errno.h>
#include <stdio.h>
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _MSC_VER
#include <dirent.h>
#include <unistd.h>
#endif

#if defined(_MSC_VER) || defined(_WIN32)
#include <rpc.h>
#endif

namespace android {
namespace base {

// A class used to model a temporary directory used during testing.
// Usage is simple:
//
//      {
//        TestTempDir myDir("my_test");   // creates new temp directory.
//        ASSERT_TRUE(myDir.path());      // NULL if error during creation.
//        ... write files into directory path myDir->path()
//        ... do your test
//      }   // destructor removes temp directory and all files under it.

#ifdef _MSC_VER
namespace uuid {
    static inline std::string generate_uuid_v4() {
        UUID uuid{};
        UuidCreate(&uuid);
        RPC_CSTR str{};
        UuidToStringA(&uuid, &str);
        std::string ret = reinterpret_cast<char*>(str);
        RpcStringFreeA(&str);
        return ret;
    }
}
#endif

class TestTempDir {
public:
    // Create new instance. This also tries to create a new temporary
    // directory. |debugPrefix| is an optional name prefix and can be NULL.
    TestTempDir(const std::string& debugName) {
        std::string temp_dir = getTempPath();
        if (!debugName.empty()) {
            temp_dir += debugName;
            temp_dir += ".";
        }

#if defined(_MSC_VER) || defined(_WIN32)
        temp_dir += uuid::generate_uuid_v4();
        if (android_mkdir(temp_dir.c_str(), 0755) != 0) {
            fprintf(stderr, "Unable to create %s, falling back to tmp dir",
                    temp_dir.c_str());
            temp_dir = getTempPath();
        }
        auto parts = PathUtils::decompose(temp_dir);
        mPath = PathUtils::recompose(parts);
#else
        temp_dir += "XXXXXX";
        if (mkdtemp(&temp_dir[0])) {
            // Fix any Win32/Linux naming issues
            auto parts = PathUtils::decompose(temp_dir);
            mPath = PathUtils::recompose(parts);
        } else {
            fprintf(stderr, "%s: mkdtemp failed!\n", __func__);
        }
#endif
    }

    // Return the path to the temporary directory, or NULL if it could not
    // be created for some reason.
    const char* path() const { return mPath.size() ? mPath.c_str() : NULL; }

    // Return the path as a string. It will be empty if the directory could
    // not be created for some reason.
    const std::string& pathString() const { return mPath; }

    // Destroy instance, and removes the temporary directory and all files
    // inside it.
    ~TestTempDir() {
        if (mPath.size()) {
            DeleteRecursive(mPath);
        }
    }

    // Create the path of a directory entry under the temporary directory.
    std::string makeSubPath(const std::string& subpath) {
        return StringFormat("%s/%s", mPath.c_str(), subpath.c_str());
    }

    // Create an empty directory under the temporary directory.
    bool makeSubDir(const std::string& subdir) {
        std::string path = makeSubPath(subdir);
        if (android_mkdir(path.c_str(), 0755) < 0) {
            // PLOG(ERROR) << "Can't create " << path.c_str() << ": ";
            return false;
        }
        return true;
    }

    // Create an empty file under the temporary directory.
    bool makeSubFile(const std::string& file) {
        std::string path = makeSubPath(file);
        int fd = ::android_open(path.c_str(), O_WRONLY | O_CREAT, 0744);
        if (fd < 0) {
            // PLOG(ERROR) << "Can't create " << path.c_str() << ": ";
            return false;
        }
        ::close(fd);
        return true;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(TestTempDir);

    void DeleteRecursive(const std::string& path) {
#ifdef _WIN32
        std::filesystem::remove_all(path);
#else
        // First remove any files in the dir
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            return;
        }

        dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }
            std::string entry_path = StringFormat("%s/%s", path, entry->d_name);
#ifdef _WIN32
            struct _stati64 stats;
            android_lstat(entry_path.c_str(),
                          reinterpret_cast<struct stat*>(&stats));
#else
            struct stat stats;
            android_lstat(entry_path.c_str(), &stats);
#endif

            if (S_ISDIR(stats.st_mode)) {
                DeleteRecursive(entry_path);
            } else {
                android_unlink(entry_path.c_str());
            }
        }
        closedir(dir);
        android_rmdir(path.c_str());
#endif
    }

#ifdef _WIN32
    std::string getTempPath() {
        return std::filesystem::temp_directory_path().string();
    }

    char* mkdtemp(char* path) {
        char tmpnamBuf[2048];
        if (!std::tmpnam(tmpnamBuf)) return nullptr;

        char* path_end = tmpnamBuf + ::strlen(path);

        // Loop. On each iteration, replace the XXXXXX suffix with a random
        // number.
        const size_t kSuffixLen = 6U;
        for (int tries = 128; tries > 0; tries--) {
            int random = rand() % 1000000;

            snprintf(path_end - kSuffixLen, kSuffixLen + 1, "%0d", random);
            if (android_mkdir(path, 0755) == 0) {
                return path;  // Success
            }
            if (errno != EEXIST) {
                return NULL;
            }
        }
        return NULL;
    }
#else  // !_WIN32
    std::string getTempPath() {
        std::string result;
        // Only check TMPDIR if we're not root.
        if (getuid() != 0 && getgid() != 0) {
            const char* tmpdir = ::getenv("TMPDIR");
            if (tmpdir && tmpdir[0]) {
                result = tmpdir;
            }
        }
        // Otherwise use P_tmpdir, which defaults to /tmp
        if (result.empty()) {
#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif
            result = P_tmpdir;
        }
        // Check that it exists and is a directory.
        struct stat st;
        int ret = android_stat(result.c_str(), &st);
        if (ret < 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "%s: Can't find temp path %s\n", __func__,
                    result.c_str());
            abort();
            // LOG(FATAL) << "Can't find temporary path: [" << result.c_str()
            //            << "]";
        }
        // Ensure there is a trailing directory separator.
        if (result.size() && result[result.size() - 1] != '/') {
            result += '/';
        }
        return result;
    }
#endif  // !_WIN32

    std::string mPath;
};

}  // namespace base
}  // namespace android
