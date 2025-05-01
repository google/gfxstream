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

#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include "gfxstream/host/mem_stream.h"
#include "gfxstream/synchronization/ConditionVariable.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/threads/Thread.h"
#include "render-utils/address_space_graphics_types.h"
#include "render-utils/stream.h"

namespace gfxstream {

class RenderChannelImpl;
class RendererImpl;
class ReadBuffer;
class RingStream;

// A class used to model a thread of the RenderServer. Each one of them
// handles a single guest client / protocol byte stream.
class RenderThread : public gfxstream::base::Thread {
    using MemStream = gfxstream::MemStream;

public:
    static constexpr uint32_t INVALID_CONTEXT_ID = std::numeric_limits<uint32_t>::max();
    // Create a new RenderThread instance.
    RenderThread(RenderChannelImpl* channel,
                 gfxstream::Stream* loadStream = nullptr,
                 uint32_t virtioGpuContextId = INVALID_CONTEXT_ID);

    // Create a new RenderThread instance tied to the address space device.
    RenderThread(
        struct asg_context context,
        gfxstream::Stream* loadStream,
        android::emulation::asg::ConsumerCallbacks callbacks,
        uint32_t contextId, uint32_t capsetId,
        std::optional<std::string> nameOpt);
    virtual ~RenderThread();

    // Returns true iff the thread has finished.
    bool isFinished() const { return mFinished.load(std::memory_order_relaxed); }
    void waitForFinished();

    void pausePreSnapshot();
    void resume();
    void save(gfxstream::Stream* stream);

    // RenderThreads are blocked from exiting after finished to workaround driver bugs.
    // `sendExitSignal` allows us to control when we can allow the thread to exit to synchronize
    // between exits and other RenderThreads calling vkDestroyDevice, eglMakeCurrent, etc.
    // This must be called after RenderThread has finished (use `waitForFinished`), as a deadlock
    // can occur if vulkan commands are still processing.
    void sendExitSignal();
private:
    virtual intptr_t main();
    void setFinished();
    void waitForExitSignal();

    // Snapshot support.
    enum class SnapshotState {
        Empty,
        StartSaving,
        StartLoading,
        InProgress,
        Finished,
    };

    // Whether using RenderChannel or a ring buffer.
    enum TransportMode {
        Channel,
        Ring,
    };

    struct SnapshotObjects;

    bool doSnapshotOp(const SnapshotObjects& objects, SnapshotState expectedState,
                      std::function<void()> op);

    bool loadSnapshot(const SnapshotObjects& objects);
    bool saveSnapshot(const SnapshotObjects& objects);

    void waitForSnapshotCompletion(gfxstream::base::AutoLock* lock);
    void loadImpl(gfxstream::base::AutoLock* lock, const SnapshotObjects& objects);
    void saveImpl(gfxstream::base::AutoLock* lock, const SnapshotObjects& objects);

    bool isPausedForSnapshotLocked() const;

    RenderChannelImpl* mChannel = nullptr;
    std::unique_ptr<RingStream> mRingStream;

    SnapshotState mState = SnapshotState::Empty;
    std::atomic<bool> mFinished { false };
    gfxstream::base::Lock mLock;
    gfxstream::base::ConditionVariable mSnapshotSignal;
    gfxstream::base::ConditionVariable mFinishedSignal;
    gfxstream::base::ConditionVariable mExitSignal;
    std::atomic<bool> mCanExit { false };
    std::optional<gfxstream::MemStream> mStream;

    bool mRunInLimitedMode = false;
    uint32_t mContextId = 0;
    uint32_t mCapsetId = 0;
    // If we need to reload process resources.
    // This happens in snapshot testing where we don't snapshot render threads.
    bool mNeedReloadProcessResources = false;
};

}  // namespace gfxstream
