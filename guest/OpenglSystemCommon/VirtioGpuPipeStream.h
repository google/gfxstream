/*
 * Copyright (C) 2018 The Android Open Source Project
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

#pragma once

#include "HostConnection.h"
#include "VirtGpu.h"
#include "gfxstream/guest/IOStream.h"

#include <stdlib.h>

/* This file implements an IOStream that uses VIRTGPU TRANSFER* ioctls on a
 * virtio-gpu DRM rendernode device to communicate with a goldfish-pipe
 * service on the host side.
 */

class VirtioGpuPipeStream : public gfxstream::guest::IOStream {
   public:
    explicit VirtioGpuPipeStream(size_t bufsize, int32_t descriptor);
    ~VirtioGpuPipeStream();

    virtual int connect(const char* serviceName = nullptr);
    virtual uint64_t processPipeInit();

    virtual void *allocBuffer(size_t minSize);
    virtual int commitBuffer(size_t size);
    virtual const unsigned char *readFully( void *buf, size_t len);
    virtual const unsigned char *commitBufferAndReadFully(
        size_t size, void *buf, size_t len);
    virtual const unsigned char *read( void *buf, size_t *inout_len);

    bool valid();
    int getRendernodeFd();
    int recv(void *buf, size_t len);

    virtual int writeFully(const void *buf, size_t len);

private:
    // sync. Also resets the write position.
    void wait();

    // transfer to/from host ops
    ssize_t transferToHost(const void* buffer, size_t len);
    ssize_t transferFromHost(void* buffer, size_t len);

    int32_t m_fd = -1;
    std::unique_ptr<VirtGpuDevice> m_device;
    VirtGpuResourcePtr m_resource;
    VirtGpuResourceMappingPtr m_resourceMapping;
    unsigned char* m_virtio_mapped; // user mapping of bo

    // intermediate buffer
    size_t m_bufsize;
    unsigned char *m_buf;

    size_t m_writtenPos;
};
