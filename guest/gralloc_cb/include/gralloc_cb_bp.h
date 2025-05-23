/*
* Copyright 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef __GRALLOC_CB_H__
#define __GRALLOC_CB_H__

#include <cinttypes>
#include <cutils/native_handle.h>

static_assert(sizeof(int) == sizeof(int32_t));
static_assert(sizeof(uint64_t) >= sizeof(uintptr_t));

struct cb_handle_t : public native_handle_t {
    static constexpr uint32_t kCbHandleMagic = 0xABFABF05;

    cb_handle_t(int p_bufferFd,
                int p_hostHandleRefCountFd,
                uint32_t p_hostHandle,
                uint64_t p_usage,
                uint32_t p_format, uint32_t p_drmformat,
                uint32_t p_stride, uint32_t p_bufSize,
                void* p_bufPtr, uint32_t p_mmapedSize, uint64_t p_mmapedOffset,
                uint32_t p_externalMetadataOffset)
        : bufferFd(p_bufferFd),
          hostHandleRefcountFd(p_hostHandleRefCountFd),
          usage(p_usage),
          mmapedOffset(p_mmapedOffset),
          bufferPtr64(reinterpret_cast<uintptr_t>(p_bufPtr)),
          magic(kCbHandleMagic),
          hostHandle(p_hostHandle),
          format(p_format),
          drmformat(p_drmformat),
          mmapedSize(p_mmapedSize),
          bufferSize(p_bufSize),
          externalMetadataOffset(p_externalMetadataOffset),
          stride(p_stride) {
        version = sizeof(native_handle);
        numFds = (p_hostHandleRefCountFd >= 0) ? 2 : 1;
        numInts = (sizeof(*this) - sizeof(native_handle_t) - numFds * sizeof(int)) / sizeof(int);
    }

    uint64_t getMmapedOffset() const {
        return mmapedOffset;
    }

    uint32_t allocatedSize() const {
        return bufferSize;
    }

    char* getBufferPtr() const {
        return reinterpret_cast<char*>(static_cast<uintptr_t>(bufferPtr64));
    }

    void setBufferPtr(void* ptr) {
        bufferPtr64 = reinterpret_cast<uintptr_t>(ptr);
    }

    bool isValid() const {
        return (version == sizeof(native_handle_t)) &&
               (sizeof(*this) == ((numFds + numInts) * sizeof(int) + sizeof(native_handle_t))) &&
               (magic == kCbHandleMagic);
    }

    static cb_handle_t* from(void* p) {
        if (!p) { return NULL; }
        cb_handle_t* cb = static_cast<cb_handle_t*>(p);
        return cb->isValid() ? cb : NULL;
    }

    static const cb_handle_t* from(const void* p) {
        return from(const_cast<void*>(p));
    }

    static cb_handle_t* from_unconst(const void* p) {
        return from(const_cast<void*>(p));
    }

    const int32_t bufferFd;       // always allocated
    const int32_t hostHandleRefcountFd;  // optional

    // ints
    const uint64_t usage;         // allocation usage
    const uint64_t mmapedOffset;
    uint64_t       bufferPtr64;
    const uint32_t magic;         // magic number in order to validate a pointer
    const uint32_t hostHandle;    // the host reference to this buffer
    const uint32_t format;        // real internal pixel format format
    const uint32_t drmformat;     // drm format
    const uint32_t mmapedSize;    // real allocation side
    const uint32_t bufferSize;
    const uint32_t externalMetadataOffset; // relative to bufferPtr
    const uint32_t stride;
    uint8_t        lockedUsage;
    uint8_t        unused[3];
};

#endif //__GRALLOC_CB_H__
