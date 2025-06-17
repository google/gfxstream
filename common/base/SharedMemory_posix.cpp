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
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gfxstream/EintrWrapper.h"
#include "gfxstream/Macros.h"
#include "gfxstream/files/PathUtils.h"
#include "gfxstream/memory/SharedMemory.h"
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifndef __NR_memfd_create
#if __aarch64__
#define __NR_memfd_create 279
#elif __arm__
#define __NR_memfd_create 279
#elif __powerpc64__
#define __NR_memfd_create 360
#elif __i386__
#define         __NR_memfd_create 356
#elif __x86_64__
#define __NR_memfd_create 319
#endif
#endif

namespace gfxstream {
namespace base {

SharedMemory::SharedMemory(const std::string& name, size_t size) {
    mSize = ALIGN(size, getpagesize());
    const std::string& kFileUri = "file://";
    if (name.find(kFileUri, 0) == 0) {
        mShareType = ShareType::FILE_BACKED;
        auto path = name.substr(kFileUri.size());
        mName = PathUtils::recompose(PathUtils::decompose(std::move(path)));
    } else {
        mShareType = ShareType::SHARED_MEMORY;
        mName = name;
    }
}

int SharedMemory::create(mode_t mode) {
    return openInternal(O_CREAT | O_RDWR, mode);
}

int SharedMemory::createNoMapping(mode_t mode) {
    return openInternal(O_CREAT | O_RDWR, mode, false /* no mapping */);
}

int SharedMemory::open(AccessMode access) {
    int oflag = O_RDONLY;
    int mode = 0400;
    if (access == AccessMode::READ_WRITE) {
        oflag = O_RDWR;
        mode = 0600;
    }
    return openInternal(oflag, mode);
}

void SharedMemory::close(bool forceDestroy) {
    if (mAddr != unmappedMemory()) {
        munmap(mAddr, mSize);
        mAddr = unmappedMemory();
    }
    if (mFd) {
        ::close(mFd);
        mFd = invalidHandle();
    }

    assert(!isOpen());
    if (forceDestroy || mCreate) {
        if (mShareType == ShareType::FILE_BACKED) {
            remove(mName.c_str());
        } else {
#if !defined(__BIONIC__)
            shm_unlink(mName.c_str());
#endif
        }
    }
}

bool SharedMemory::isOpen() const {
    return mFd != invalidHandle();
}

int SharedMemory::openInternal(int oflag, int mode, bool doMapping) {
    if (isOpen()) {
        return EEXIST;
    }

    int err = 0;
    struct stat sb;
    if (mShareType == ShareType::SHARED_MEMORY) {
#if defined(HAVE_MEMFD_CREATE)
        mFd = memfd_create(mName.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
#else
        mFd = syscall(__NR_memfd_create, mName.c_str(), FD_CLOEXEC);
#endif
    } else {
        mFd = ::open(mName.c_str(), oflag, mode);
        // Make sure the file can hold at least mSize bytes..
        struct stat stat;
        if (!fstat(mFd, &stat) && stat.st_size < mSize) {
            err = ftruncate(mFd, mSize);
        }
    }
    if (mFd == -1 || err) {
        err = -errno;
        close();
        return err;
    }

    if (oflag & O_CREAT) {
        if (HANDLE_EINTR(fstat(mFd, &sb)) == -1) {
            err = -errno;
            close();
            return err;
        }

        // Only increase size, as we don't want to yank away memory
        // from another process.
        if (mSize > sb.st_size && HANDLE_EINTR(ftruncate(mFd, mSize)) == -1) {
            err = -errno;
            close();
            return err;
        }

#if defined(HAVE_MEMFD_CREATE)
        if (fcntl(mFd, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW) == -1) {
            err = -errno;
            close();
            return err;
        }
#endif
    }

    if (doMapping) {
        int mapFlags = PROT_READ;
        if (oflag & O_RDWR || oflag & O_CREAT) {
            mapFlags |= PROT_WRITE;
        }

        mAddr = mmap(nullptr, mSize, mapFlags, MAP_SHARED, mFd, 0);
        if (mAddr == unmappedMemory()) {
            err = -errno;
            close();
            return err;
        }
    }

    mCreate |= (oflag & O_CREAT);
    assert(isOpen());
    return 0;
}
}  // namespace base
}  // namespace android
