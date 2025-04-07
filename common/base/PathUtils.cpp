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

#include "aemu/base/files/PathUtils.h"

#include <string.h>                      // for size_t, strncmp
#include <iterator>                      // for reverse_iterator, operator!=
#include <numeric>                       // for accumulate
#include <type_traits>                   // for enable_if<>::type

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#include "aemu/base/system/Win32UnicodeString.h"
#endif

static inline bool sIsEmpty(const char* str) {
    return !str || str[0] == '\0';
}

namespace android {
namespace base {

const char* const PathUtils::kExeNameSuffixes[kHostTypeCount] = {"", ".exe"};

const char* const PathUtils::kExeNameSuffix =
        PathUtils::kExeNameSuffixes[PathUtils::HOST_TYPE];

std::string PathUtils::toExecutableName(const char* baseName,
                                        HostType hostType) {
    return static_cast<std::string>(baseName).append(
            kExeNameSuffixes[hostType]);
}

// static
bool PathUtils::isDirSeparator(int ch, HostType hostType) {
    return (ch == '/') || (hostType == HOST_WIN32 && ch == '\\');
}

// static
bool PathUtils::isPathSeparator(int ch, HostType hostType) {
    return (hostType == HOST_POSIX && ch == ':') ||
           (hostType == HOST_WIN32 && ch == ';');
}

// static
size_t PathUtils::rootPrefixSize(const std::string& path, HostType hostType) {
    if (path.empty()) return 0;

    if (hostType != HOST_WIN32)
        return (path[0] == '/') ? 1U : 0U;

    size_t result = 0;
    if (path[1] == ':') {
        int ch = path[0];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
            result = 2U;
    } else if (!strncmp(path.c_str(), "\\\\.\\", 4) ||
               !strncmp(path.c_str(), "\\\\?\\", 4)) {
        // UNC prefixes.
        return 4U;
    } else if (isDirSeparator(path[0], hostType)) {
        result = 1;
        if (isDirSeparator(path[1], hostType)) {
            result = 2;
            while (path[result] && !isDirSeparator(path[result], HOST_WIN32))
                result++;
        }
    }
    if (result && path[result] && isDirSeparator(path[result], HOST_WIN32))
        result++;

    return result;
}

// static
bool PathUtils::isAbsolute(const char* path, HostType hostType) {
    size_t prefixSize = rootPrefixSize(path, hostType);
    if (!prefixSize) {
        return false;
    }
    if (hostType != HOST_WIN32) {
        return true;
    }
    return isDirSeparator(path[prefixSize - 1], HOST_WIN32);
}

// static
std::string_view PathUtils::extension(const std::string& path,
                                      HostType hostType) {
    std::string_view tmp = path;
    using riter = std::reverse_iterator<std::string_view::const_iterator>;

    for (auto it = riter(tmp.end()), itEnd = riter(tmp.begin()); it != itEnd;
         ++it) {
        if (*it == '.') {
            // reverse iterator stores a base+1, so decrement it when returning
            // MSVC doesn't have string_view constructor with iterators
            return std::string_view(&*std::prev(it.base()), (it - riter(tmp.end()) + 1));
        }
        if (isDirSeparator(*it, hostType)) {
            // no extension here - we've found the end of file name already
            break;
        }
    }

    // either there's no dot in the whole path, or we found directory separator
    // first - anyway, there's no extension in this name
    return "";
}
// static
std::string PathUtils::removeTrailingDirSeparator(const char* path,
                                                 HostType hostType) {
    size_t pathLen = strlen(path);
    // NOTE: Don't remove initial path separator for absolute paths.
    while (pathLen > 1U && isDirSeparator(path[pathLen - 1U], hostType)) {
        pathLen--;
    }
    return std::string(path, pathLen);
}

// static
std::string PathUtils::addTrailingDirSeparator(const char* path,
                                               HostType hostType) {
    std::string result = path;
    if (result.size() > 0 && !isDirSeparator(result[result.size() - 1U])) {
        result += getDirSeparator(hostType);
    }
    return result;
}

// static
bool PathUtils::split(const char* path,
                      HostType hostType,
                      std::string* dirName,
                      std::string* baseName) {
    if (sIsEmpty(path)) {
        return false;
    }

    // If there is a trailing directory separator, return an error.
    size_t end = strlen(path);
    if (isDirSeparator(path[end - 1U], hostType)) {
        return false;
    }

    // Find last separator.
    size_t prefixLen = rootPrefixSize(path, hostType);
    size_t pos = end;
    while (pos > prefixLen && !isDirSeparator(path[pos - 1U], hostType)) {
        pos--;
    }

    // Handle common case.
    if (pos > prefixLen) {
        if (dirName) {
            *dirName = std::string(path, pos);
        }
        if (baseName) {
            *baseName = path + pos;
        }
        return true;
    }

    // If there is no directory separator, the path is a single file name.
    if (dirName) {
        if (!prefixLen) {
            *dirName = ".";
        } else {
            *dirName = std::string(path, prefixLen);
        }
    }
    if (baseName) {
        *baseName = path + prefixLen;
    }
    return true;
}

// static
std::string PathUtils::join(const std::string& path1,
                            const std::string& path2,
                            HostType hostType) {
    if (path1.empty()) {
        return path2;
    }
    if (path2.empty()) {
        return path1;
    }
    if (isAbsolute(path2.c_str(), hostType)) {
        return path2;
    }
    size_t prefixLen = rootPrefixSize(path1, hostType);
    std::string result(path1);
    size_t end = result.size();
    if (end > prefixLen && !isDirSeparator(result[end - 1U], hostType)) {
        result += getDirSeparator(hostType);
    }
    result += path2;
    return result;
}

template <class String>
static std::vector<String> decomposeImpl(const String& path, PathUtils::HostType hostType) {
    std::vector<String> result;
    if (path.empty())
        return result;

    size_t prefixLen = PathUtils::rootPrefixSize(path, hostType);
    auto it = path.begin();
    if (prefixLen) {
        result.emplace_back(it, it + prefixLen);
        it += prefixLen;
    }
    for (;;) {
        auto p = it;
        while (p != path.end() && !PathUtils::isDirSeparator(*p, hostType))
            p++;
        if (p > it) {
            result.emplace_back(it, p);
        }
        if (p == path.end()) {
            break;
        }
        it = p + 1;
    }
    return result;
}

std::vector<std::string> PathUtils::decompose(std::string&& path,
                                              HostType hostType) {
    return decomposeImpl<std::string>(path, hostType);
}

std::vector<std::string> PathUtils::decompose(const std::string& path,
                                              HostType hostType) {
    return decomposeImpl<std::string>(path, hostType);
}

template <class String>
std::string PathUtils::recompose(const std::vector<String>& components,
                                 HostType hostType) {
    if (components.empty()) {
        return {};
    }

    const char dirSeparator = getDirSeparator(hostType);
    std::string result;
    // To reduce memory allocations, compute capacity before doing the
    // real append.
    const size_t capacity =
            components.size() - 1 +
            std::accumulate(components.begin(), components.end(), size_t(0),
                            [](size_t val, const String& next) {
                                return val + next.size();
                            });

    result.reserve(capacity);
    bool addSeparator = false;
    for (size_t n = 0; n < components.size(); ++n) {
        const auto& component = components[n];
        if (addSeparator)
            result += dirSeparator;
        addSeparator = true;
        if (n == 0) {
            size_t prefixLen = rootPrefixSize(component, hostType);
            if (prefixLen == component.size()) {
                addSeparator = false;
            }
        }
        result += component;
    }
    return result;
}

// static
std::string PathUtils::recompose(const std::vector<std::string>& components,
                                 HostType hostType) {
    return recompose<std::string>(components, hostType);
}

// static
template <class String>
void PathUtils::simplifyComponents(std::vector<String>* components) {
    std::vector<String> stack;
    for (auto& component : *components) {
        if (component == ".") {
            // Ignore any instance of '.' from the list.
            continue;
        }
        if (component == "..") {
            // Handling of '..' is specific: if there is a item on the
            // stack that is not '..', then remove it, otherwise push
            // the '..'.
            if (!stack.empty() && stack.back() != "..") {
                stack.pop_back();
            } else {
                stack.push_back(std::move(component));
            }
            continue;
        }
        // If not a '..', just push on the stack.
        stack.push_back(std::move(component));
    }
    if (stack.empty()) {
        stack.push_back(".");
    }
    components->swap(stack);
}

void PathUtils::simplifyComponents(std::vector<std::string>* components) {
    simplifyComponents<std::string>(components);
}

// static
std::string PathUtils::relativeTo(const std::string& base,
                                  const std::string& path,
                                  HostType hostType) {
    auto baseDecomposed = decompose(base, hostType);
    auto pathDecomposed = decompose(path, hostType);

    if (baseDecomposed.size() > pathDecomposed.size())
        return path;

    for (size_t i = 0; i < baseDecomposed.size(); i++) {
        if (baseDecomposed[i] != pathDecomposed[i])
            return path;
    }

    std::string result =
            recompose(std::vector<std::string>(
                              pathDecomposed.begin() + baseDecomposed.size(),
                              pathDecomposed.end()),
                      hostType);

    return result;
}

bool PathUtils::move(const std::string& from, const std::string& to) {
    // std::rename returns 0 on success.
    if (std::rename(from.data(), to.data())) {
#ifdef _SUPPORT_FILESYSTEM
        // Rename can fail if files are on different disks
        if (std::filesystem::copy_file(from.data(), to.data())) {
            std::filesystem::remove(from.data());
            return true;
        } else {
            return false;
        }
#else   // _SUPPORT_FILESYSTEM
        return false;
#endif  // _SUPPORT_FILESYSTEM
    }
    return true;
}

#ifdef _WIN32

// Return |path| as a Unicode string, while discarding trailing separators.
Win32UnicodeString win32Path(const char* path) {
    Win32UnicodeString wpath(path);
    // Get rid of trailing directory separators, Windows doesn't like them.
    size_t size = wpath.size();
    while (size > 0U &&
           (wpath[size - 1U] == L'\\' || wpath[size - 1U] == L'/')) {
        size--;
    }
    if (size < wpath.size()) {
        wpath.resize(size);
    }
    return wpath;
}

/* access function */
#define	F_OK		0	/* test for existence of file */
#define	X_OK		0x01	/* test for execute or search permission */
#define	W_OK		0x02	/* test for write permission */
#define	R_OK		0x04	/* test for read permission */

static int GetWin32Mode(int mode) {
    // Convert |mode| to win32 permission bits.
    int win32mode = 0x0;

    if ((mode & R_OK) || (mode & X_OK)) {
        win32mode |= 0x4;
    }
    if (mode & W_OK) {
        win32mode |= 0x2;
    }

    return win32mode;
}

#endif

bool pathExists(const char* path) {
#ifdef _WIN32
    return _waccess(win32Path(path).c_str(), GetWin32Mode(F_OK));
#else
    return 0 == access(path, F_OK);
#endif
}

std::string pj(const std::string& path1, const std::string& path2) {
    return PathUtils::join(path1, path2);
}

std::string pj(const std::vector<std::string>& paths) {
    std::string res;

    if (paths.size() == 0)
        return "";

    if (paths.size() == 1)
        return paths[0];

    res = paths[0];

    for (size_t i = 1; i < paths.size(); i++) {
        res = PathUtils::join(res, paths[i]);
    }

    return res;
}

std::string PathUtils::addTrailingDirSeparator(const std::string& path,
                                               HostType hostType) {
    std::string result = path;
    if (result.size() > 0 && !isDirSeparator(result[result.size() - 1U])) {
        result += getDirSeparator(hostType);
    }
    return result;
}

}  // namespace base
}  // namespace android
