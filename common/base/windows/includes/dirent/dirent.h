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
#ifndef _AEMU_DIRENT_H_
#define _AEMU_DIRENT_H_

#include <sys/types.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file dirent.h
 * @brief A POSIX-like dirent API implementation for Windows using the Windows API.
 *
 * This header provides a subset of the POSIX dirent API for Windows, allowing C and C++
 * code to use familiar functions like opendir(), readdir(), closedir(), etc. to
 * iterate through directory entries.
 *
 * @warning **Limitations:**
 *   - **`telldir()` and `seekdir()` are minimally implemented.** `seekdir()` only supports
 *     seeking to the beginning (loc = 0), the end (loc = -1), or forward to a specific entry
 *     by its index (loc > 0). Seeking to arbitrary positions is implemented by iterating
 *     through the entries, making it an **O(N)** operation in the worst case, where N is
 *     the desired position. `telldir()` returns the index of the last entry read by `readdir()`.
 *   - **`d_ino` is implemented using Windows file index.** It does not represent a
 *     true POSIX inode number but can be used to identify files uniquely.
 *   - **`d_reclen` is not supported.** The field is not present in this implementation.
 *   - **Thread safety:** This implementation is not inherently thread-safe. Using the
 *     same `DIR` pointer from multiple threads simultaneously can lead to undefined
 *     behavior.
 *
 * @note **Windows-Specific Behavior:**
 *   - Filenames are stored in `d_name` as **UTF-8** encoded strings.
 *   - Extended-length paths (longer than `MAX_PATH`) are supported using the `\\?\` prefix.
 *   - The implementation uses the Windows API (`FindFirstFileW`, `FindNextFileW`, etc.)
 *     internally.
 *   - The `DIR` type is an opaque pointer to an internal structure.
 */

/**
 * @brief The maximum length of a file name, including the null terminator.
 *
 * This is set to `MAX_PATH` (260) for compatibility but internally the implementation
 * supports extended-length paths using the `\\?\` prefix.
 */
#define FILENAME_MAX MAX_PATH

/**
 * @brief Represents a directory entry.
 */
struct dirent {
    /**
     * @brief File ID (from the Windows file index).
     *
     * This is not a true POSIX inode number but can be used as a unique file
     * identifier on Windows. It is obtained using `GetFileInformationByHandle`
     * and represents a file's unique ID within a volume.
     * @warning This field might not be fully unique across different volumes or over time.
     */
    uint64_t d_ino;

    /**
     * @brief Null-terminated file name in UTF-8 encoding.
     *
     * @warning The maximum length of the filename (excluding the null terminator)
     * that can be stored in this field is `FILENAME_MAX`. If a filename exceeds this
     * limit, `readdir` will skip the entry and set `errno` to `ENAMETOOLONG`.
     */
    char d_name[FILENAME_MAX];
};

/**
 * @brief An opaque type representing a directory stream.
 */
typedef struct DIR DIR;


/**
 * @brief Opens a directory stream for reading.
 *
 * @param name The path to the directory to open. This should be a UTF-8 encoded string.
 *
 * @return A pointer to a `DIR` structure representing the opened directory stream,
 *         or `nullptr` if an error occurred. If `nullptr` is returned, `errno` is set
 *         to indicate the error.
 *
 * @retval EACCES       Search permission is denied for the directory.
 * @retval EMFILE       The maximum number of file descriptors are already open.
 * @retval ENFILE       The maximum number of files are already open in the system.
 * @retval ENOENT       The named directory does not exist or is an empty string.
 * @retval ENOMEM       Insufficient memory is available.
 * @retval ENOTDIR      A component of the path is not a directory.
 * @retval EINVAL       The `name` argument is invalid (e.g., contains invalid characters).
 */
DIR* opendir(const char* name);

/**
 * @brief Reads the next directory entry from a directory stream.
 *
 * @param dirp A pointer to a `DIR` structure returned by `opendir()`.
 *
 * @return A pointer to a `dirent` structure representing the next directory entry,
 *         or `nullptr` if the end of the directory stream is reached or an error
 *         occurred. If `nullptr` is returned and `errno` is not 0, an error occurred.
 *
 * @retval EBADF      The `dirp` argument does not refer to an open directory stream.
 * @retval ENOMEM     Insufficient memory is available.
 * @retval ENOENT     No more directory entries.
 * @retval EIO        An I/O error occurred.
 * @retval ENAMETOOLONG A filename exceeded `FILENAME_MAX`.
 */
struct dirent* readdir(DIR* dirp);

/**
 * @brief Closes a directory stream.
 *
 * @param dirp A pointer to a `DIR` structure returned by `opendir()`.
 *
 * @return 0 on success, -1 on failure. If -1 is returned, `errno` is set to
 *         indicate the error.
 *
 * @retval EBADF      The `dirp` argument does not refer to an open directory stream.
 */
int closedir(DIR* dirp);

/**
 * @brief Resets the position of a directory stream to the beginning.
 *
 * @param dirp A pointer to a `DIR` structure returned by `opendir()`.
 *
 * @retval EBADF      The `dirp` argument does not refer to an open directory stream.
 * @retval EIO        An I/O error occurred.
 */
void rewinddir(DIR* dirp);
/**
 * @brief Gets the current position of a directory stream.
 *
 * @param dirp A pointer to a `DIR` structure returned by `opendir()`.
 *
 * @return The current position of the directory stream. This is the index of the last
 *         entry read by `readdir()`. Returns -1 if at the end of the directory stream.
 *         If -1 is returned and `errno` is not 0, an error occurred.
 *
 * @retval EBADF The `dirp` argument does not refer to an open directory stream.
 *
 * @note   The position returned by `telldir()` is an opaque value that should only be
 *         used in conjunction with `seekdir()`.
 */
long telldir(DIR* dirp);

/**
 * @brief Sets the position of a directory stream.
 *
 * @param dirp A pointer to a `DIR` structure returned by `opendir()`.
 * @param loc  The new position of the directory stream. The following values are supported:
 *             - **0:** Seek to the beginning of the stream (equivalent to `rewinddir()`).
 *             - **-1:** Seek to the end of the stream.
 *             - **\>0:** Seek to a specific entry by its index (the value returned by `telldir()`).
 *
 * @retval EBADF      The `dirp` argument does not refer to an open directory stream.
 * @retval EINVAL     The `loc` argument is invalid (e.g., negative value other than -1, or a
 *                     value that is greater than the number of entries in the directory).
 *
 * @note   Seeking to arbitrary positions (other than the beginning or end) is implemented
 *         by rewinding the directory stream and then calling `readdir()` repeatedly until
 *         the desired position is reached.
 * @note   **Time Complexity:**
 *         - O(1) for `loc = 0` (rewind) and `loc = -1` (seek to end).
 *         - O(N) for `loc > 0`, where N is the position being sought to. In the worst case,
 *           seeking to the end of a large directory can be a slow operation.
 */
void seekdir(DIR* dirp, long loc);

#ifdef __cplusplus
}
#endif

#endif	/* Not _AEMU_DIRENT_H_ */