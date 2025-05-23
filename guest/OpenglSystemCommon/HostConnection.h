/*
* Copyright (C) 2011 The Android Open Source Project
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
#ifndef __COMMON_HOST_CONNECTION_H
#define __COMMON_HOST_CONNECTION_H

#if defined(ANDROID)
#include "gfxstream/guest/ANativeWindow.h"
#include "gfxstream/guest/GfxStreamGralloc.h"
#endif

#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "ExtendedRenderControl.h"
#include "Sync.h"
#include "VirtGpu.h"

class GLEncoder;
struct gl_client_context_t;
class GL2Encoder;
struct gl2_client_context_t;

struct EGLThreadInfo;

enum HostConnectionType {
    HOST_CONNECTION_QEMU_PIPE = 1,
    HOST_CONNECTION_ADDRESS_SPACE = 2,
    HOST_CONNECTION_VIRTIO_GPU_PIPE = 3,
    HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE = 4,
};

class HostConnection
{
public:
    static HostConnection *get();
    static HostConnection* getOrCreate(enum VirtGpuCapset capset = kCapsetNone);
    static HostConnection* getWithThreadInfo(EGLThreadInfo* tInfo, enum VirtGpuCapset capset);
    static void exit();

    static std::unique_ptr<HostConnection> createUnique(enum VirtGpuCapset capset);
    HostConnection(const HostConnection&) = delete;

    ~HostConnection();

    GLEncoder *glEncoder();
    GL2Encoder *gl2Encoder();
    ExtendedRCEncoderContext *rcEncoder();

#if defined(ANDROID)
    gfxstream::ANativeWindowHelper* anwHelper() { return m_anwHelper.get(); }
    gfxstream::Gralloc* grallocHelper() { return m_grallocHelper.get(); }
#endif
    gfxstream::SyncHelper* syncHelper() { return m_syncHelper.get(); }

    void flush() {
        if (m_stream) {
            m_stream->flush();
        }
    }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
#endif
    void lock() const { m_lock.lock(); }
    void unlock() const { m_lock.unlock(); }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    void setVulkanFeatureInfo(void* info);

   private:
    // If the connection failed, |conn| is deleted.
    // Returns NULL if connection failed.
 static std::unique_ptr<HostConnection> connect(enum VirtGpuCapset capset);

 HostConnection();
 static gl_client_context_t* s_getGLContext();
 static gl2_client_context_t* s_getGL2Context();

private:
 HostConnectionType m_connectionType;

 // intrusively refcounted
 gfxstream::guest::IOStream* m_stream = nullptr;

 std::unique_ptr<GLEncoder> m_glEnc;
 std::unique_ptr<GL2Encoder> m_gl2Enc;

 // intrusively refcounted
 std::unique_ptr<ExtendedRCEncoderContext> m_rcEnc;

 gfxstream::guest::ChecksumCalculator m_checksumHelper;
#if defined(ANDROID)
 std::unique_ptr<gfxstream::ANativeWindowHelper> m_anwHelper;
 std::unique_ptr<gfxstream::Gralloc> m_grallocHelper;
#endif
 std::unique_ptr<gfxstream::SyncHelper> m_syncHelper;
 bool m_noHostError;
 mutable std::mutex m_lock;
 int m_rendernodeFd;
};

#endif
