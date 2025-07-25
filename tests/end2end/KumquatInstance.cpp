// Copyright (C) 2024 The Android Open Source Project
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

#include "KumquatInstance.h"

#include <unistd.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "TestDataUtils.h"

using testing::Eq;

namespace gfxstream {
namespace tests {

KumquatInstance::KumquatInstance() {}

void KumquatInstance::SetUp(bool withGl, bool withVk, std::string features) {
    const std::string kumquatCommand = GetTestDataPath("kumquat").string();
    const std::string renderer_features = "--renderer-features=" + features;

    std::string capset_names = "--capset-names=";
    if (withGl) {
        capset_names.append("gfxstream-gles:");
    }
    if (withVk) {
        capset_names.append("gfxstream-vulkan:");
    }

    int fds[2];
    ASSERT_THAT(pipe(fds), Eq(0));

    // VirtGpuKumquatDevice.cpp by default connects to "/tmp/kumquat-gpu-0".  If this changes,
    // the correct socket path must be plumbed through to VirtGpuKumquatDevice.cpp
    const std::string gpu_socket_path = "/tmp/kumquat-gpu-" + std::to_string(0);

    const std::string gpu_socket_cmd = "--gpu-socket-path=" + gpu_socket_path;

    const std::string pipe_descriptor = "--pipe-descriptor=" + std::to_string(fds[1]);

    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        execl(kumquatCommand.c_str(), kumquatCommand.c_str(), gpu_socket_cmd.c_str(),
              capset_names.c_str(), renderer_features.c_str(), pipe_descriptor.c_str(), nullptr);
        exit(0);
    } else {
        close(fds[1]);
        uint64_t count = 0;
        ssize_t bytes_read = read(fds[0], &count, sizeof(count));
        // Kumquat writes [uint64_t](1) to the write end of the pipe..
        ASSERT_THAT(bytes_read, Eq(8));
        close(fds[0]);
        ASSERT_THAT(virtgpu_kumquat_init(&mVirtGpu, gpu_socket_path.c_str()), Eq(0));
        mKumquatPid = pid;
    }
}

KumquatInstance::~KumquatInstance() {
    virtgpu_kumquat_finish(&mVirtGpu);
    kill(mKumquatPid, SIGKILL);
    int status = 0;
    pid_t pid = waitpid(mKumquatPid, &status, WNOHANG);
    while (!pid) {
        pid = waitpid(mKumquatPid, &status, WNOHANG);
    }
}

void KumquatInstance::Snapshot() { ASSERT_THAT(virtgpu_kumquat_snapshot_save(mVirtGpu), Eq(0)); }

void KumquatInstance::Restore() { ASSERT_THAT(virtgpu_kumquat_snapshot_restore(mVirtGpu), Eq(0)); }

}  // namespace tests
}  // namespace gfxstream
