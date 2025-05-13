// Copyright (C) 2019 The Android Open Source Project
// Copyright (C) 2019 Google Inc.
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
#include "aemu/base/threads/AndroidWorkPool.h"

#include "aemu/base/threads/AndroidFunctorThread.h"
#include "aemu/base/synchronization/AndroidLock.h"
#include "aemu/base/synchronization/AndroidConditionVariable.h"
#include "aemu/base/synchronization/AndroidMessageChannel.h"

#include <atomic>
#include <memory>
#include <unordered_map>
#include <sys/time.h>

using gfxstream::guest::AutoLock;
using gfxstream::guest::ConditionVariable;
using gfxstream::guest::FunctorThread;
using gfxstream::guest::Lock;
using gfxstream::guest::MessageChannel;

namespace gfxstream {
namespace guest {

static constexpr const uint64_t kMicrosecondsPerSecond = 1000000;
static constexpr const uint64_t kNanosecondsPerMicrosecond = 1000;

class WaitGroup { // intrusive refcounted
public:

    WaitGroup(int numTasksRemaining) :
        mNumTasksInitial(numTasksRemaining),
        mNumTasksRemaining(numTasksRemaining) { }

    ~WaitGroup() = default;

    gfxstream::guest::Lock& getLock() { return mLock; }

    void acquire() {
        if (0 == mRefCount.fetch_add(1, std::memory_order_seq_cst)) {
            abort();
        }
    }

    bool release() {
        if (0 == mRefCount) {
            abort();
        }
        if (1 == mRefCount.fetch_sub(1, std::memory_order_seq_cst)) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
            return true;
        }
        return false;
    }

    // wait on all of or any of the associated tasks to complete.
    bool waitAllLocked(WorkPool::TimeoutUs timeout) {
        return conditionalTimeoutLocked(
            [this] { return mNumTasksRemaining > 0; },
            timeout);
    }

    bool waitAnyLocked(WorkPool::TimeoutUs timeout) {
        return conditionalTimeoutLocked(
            [this] { return mNumTasksRemaining == mNumTasksInitial; },
            timeout);
    }

    // broadcasts to all waiters that there has been a new job that has completed
    bool decrementBroadcast() {
        AutoLock<Lock> lock(mLock);
        bool done =
            (1 == mNumTasksRemaining.fetch_sub(1, std::memory_order_seq_cst));
        std::atomic_thread_fence(std::memory_order_acquire);
        mCv.broadcast();
        return done;
    }

private:

    bool doWait(WorkPool::TimeoutUs timeout) {
        if (timeout == ~0ULL) {
            mCv.wait(&mLock);
            return true;
        } else {
            return mCv.timedWait(&mLock, getDeadline(timeout));
        }
    }

    struct timespec getDeadline(WorkPool::TimeoutUs relative) {
        struct timeval deadlineUs;
        struct timespec deadlineNs;
        gettimeofday(&deadlineUs, 0);

        deadlineUs.tv_sec += (relative / kMicrosecondsPerSecond);
        deadlineUs.tv_usec += (relative % kMicrosecondsPerSecond);

        if (static_cast<uint64_t>(deadlineUs.tv_usec) > kMicrosecondsPerSecond) {
            deadlineUs.tv_sec += (deadlineUs.tv_usec / kMicrosecondsPerSecond);
            deadlineUs.tv_usec = (deadlineUs.tv_usec % kMicrosecondsPerSecond);
        }

        deadlineNs.tv_sec = deadlineUs.tv_sec;
        deadlineNs.tv_nsec = deadlineUs.tv_usec * kNanosecondsPerMicrosecond;
        return deadlineNs;
    }

    uint64_t currTimeUs() {
        struct timeval tv;
        gettimeofday(&tv, 0);
        return (uint64_t)(tv.tv_sec * kMicrosecondsPerSecond + tv.tv_usec);
    }

    bool conditionalTimeoutLocked(std::function<bool()> conditionFunc, WorkPool::TimeoutUs timeout) {
        uint64_t currTime = currTimeUs();
        WorkPool::TimeoutUs currTimeout = timeout;

        while (conditionFunc()) {
            doWait(currTimeout);
            if (conditionFunc()) {
                // Decrement timeout for wakeups
                uint64_t nextTime = currTimeUs();
                WorkPool::TimeoutUs waited =
                    nextTime - currTime;
                currTime = nextTime;

                if (currTimeout > waited) {
                    currTimeout -= waited;
                } else {
                    return conditionFunc();
                }
            }
        }

        return true;
    }

    std::atomic<int> mRefCount = { 1 };
    int mNumTasksInitial;
    std::atomic<int> mNumTasksRemaining;

    Lock mLock;
    ConditionVariable mCv;
};

class WorkPoolThread {
public:
    // State diagram for each work pool thread
    //
    // Unacquired: (Start state) When no one else has claimed the thread.
    // Acquired: When the thread has been claimed for work,
    // but work has not been issued to it yet.
    // Scheduled: When the thread is running tasks from the acquirer.
    // Exiting: cleanup
    //
    // Messages:
    //
    // Acquire
    // Run
    // Exit
    //
    // Transitions:
    //
    // Note: While task is being run, messages will come back with a failure value.
    //
    // Unacquired:
    //     message Acquire -> Acquired. effect: return success value
    //     message Run -> Unacquired. effect: return failure value
    //     message Exit -> Exiting. effect: return success value
    //
    // Acquired:
    //     message Acquire -> Acquired. effect: return failure value
    //     message Run -> Scheduled. effect: run the task, return success
    //     message Exit -> Exiting. effect: return success value
    //
    // Scheduled:
    //     implicit effect: after task is run, transition back to Unacquired.
    //     message Acquire -> Scheduled. effect: return failure value
    //     message Run -> Scheduled. effect: return failure value
    //     message Exit -> queue up exit message, then transition to Exiting after that is done.
    //         effect: return success value
    //
    enum State {
        Unacquired = 0,
        Acquired = 1,
        Scheduled = 2,
        Exiting = 3,
    };

    WorkPoolThread() : mThread([this] { threadFunc(); }) {
        mThread.start();
    }

    ~WorkPoolThread() {
        exit();
        mThread.wait();
    }

    bool acquire() {
        AutoLock<Lock> lock(mLock);
        switch (mState) {
            case State::Unacquired:
                mState = State::Acquired;
                return true;
            case State::Acquired:
            case State::Scheduled:
            case State::Exiting:
                return false;
            default:
                return false;
        }
    }

    bool run(WorkPool::WaitGroupHandle waitGroupHandle, WaitGroup* waitGroup, WorkPool::Task task) {
        AutoLock<Lock> lock(mLock);
        switch (mState) {
            case State::Unacquired:
                return false;
            case State::Acquired: {
                mState = State::Scheduled;
                mToCleanupWaitGroupHandle = waitGroupHandle;
                waitGroup->acquire();
                mToCleanupWaitGroup = waitGroup;
                mShouldCleanupWaitGroup = false;
                TaskInfo msg = {
                    Command::Run,
                    waitGroup, task,
                };
                mRunMessages.send(msg);
                return true;
            }
            case State::Scheduled:
            case State::Exiting:
                return false;
            default:
                return false;
        }
    }

    bool shouldCleanupWaitGroup(WorkPool::WaitGroupHandle* waitGroupHandle, WaitGroup** waitGroup) {
        AutoLock<Lock> lock(mLock);
        bool res = mShouldCleanupWaitGroup;
        *waitGroupHandle = mToCleanupWaitGroupHandle;
        *waitGroup = mToCleanupWaitGroup;
        mShouldCleanupWaitGroup = false;
        return res;
    }

private:
    enum Command {
        Run = 0,
        Exit = 1,
    };

    struct TaskInfo {
        Command cmd;
        WaitGroup* waitGroup = nullptr;
        WorkPool::Task task = {};
    };

    bool exit() {
        AutoLock<Lock> lock(mLock);
        TaskInfo msg { Command::Exit, };
        mRunMessages.send(msg);
        return true;
    }

    void threadFunc() {
        TaskInfo taskInfo;
        bool done = false;

        while (!done) {
            mRunMessages.receive(&taskInfo);
            switch (taskInfo.cmd) {
                case Command::Run:
                    doRun(taskInfo);
                    break;
                case Command::Exit: {
                    AutoLock<Lock> lock(mLock);
                    mState = State::Exiting;
                    break;
                }
            }
            AutoLock<Lock> lock(mLock);
            done = mState == State::Exiting;
        }
    }

    // Assumption: the wait group refcount is >= 1 when entering
    // this function (before decrement)..
    // at least it doesn't get to 0
    void doRun(TaskInfo& msg) {
        WaitGroup* waitGroup = msg.waitGroup;

        if (msg.task) msg.task();

        bool lastTask =
            waitGroup->decrementBroadcast();

        AutoLock<Lock> lock(mLock);
        mState = State::Unacquired;

        if (lastTask) {
            mShouldCleanupWaitGroup = true;
        }

        waitGroup->release();
    }

    FunctorThread mThread;
    Lock mLock;
    State mState = State::Unacquired;
    MessageChannel<TaskInfo, 4> mRunMessages;
    WorkPool::WaitGroupHandle mToCleanupWaitGroupHandle = 0;
    WaitGroup* mToCleanupWaitGroup = nullptr;
    bool mShouldCleanupWaitGroup = false;
};

class WorkPool::Impl {
public:
    Impl(int numInitialThreads) : mThreads(numInitialThreads) {
        for (size_t i = 0; i < mThreads.size(); ++i) {
            mThreads[i].reset(new WorkPoolThread);
        }
    }

    ~Impl() = default;

    WorkPool::WaitGroupHandle schedule(const std::vector<WorkPool::Task>& tasks) {

        if (tasks.empty()) abort();

        AutoLock<Lock> lock(mLock);

        // Sweep old wait groups
        for (size_t i = 0; i < mThreads.size(); ++i) {
            WaitGroupHandle handle;
            WaitGroup* waitGroup;
            bool cleanup = mThreads[i]->shouldCleanupWaitGroup(&handle, &waitGroup);
            if (cleanup) {
                mWaitGroups.erase(handle);
                waitGroup->release();
            }
        }

        WorkPool::WaitGroupHandle resHandle = genWaitGroupHandleLocked();
        WaitGroup* waitGroup =
            new WaitGroup(tasks.size());

        mWaitGroups[resHandle] = waitGroup;

        std::vector<size_t> threadIndices;

        while (threadIndices.size() < tasks.size()) {
            for (size_t i = 0; i < mThreads.size(); ++i) {
                if (!mThreads[i]->acquire()) continue;
                threadIndices.push_back(i);
                if (threadIndices.size() == tasks.size()) break;
            }
            if (threadIndices.size() < tasks.size()) {
                mThreads.resize(mThreads.size() + 1);
                mThreads[mThreads.size() - 1].reset(new WorkPoolThread);
            }
        }

        // every thread here is acquired
        for (size_t i = 0; i < threadIndices.size(); ++i) {
            mThreads[threadIndices[i]]->run(resHandle, waitGroup, tasks[i]);
        }

        return resHandle;
    }

    bool waitAny(WorkPool::WaitGroupHandle waitGroupHandle, WorkPool::TimeoutUs timeout) {
        AutoLock<Lock> lock(mLock);
        auto it = mWaitGroups.find(waitGroupHandle);
        if (it == mWaitGroups.end()) return true;

        auto waitGroup = it->second;
        waitGroup->acquire();
        lock.unlock();

        bool waitRes = false;

        {
            AutoLock<Lock> waitGroupLock(waitGroup->getLock());
            waitRes = waitGroup->waitAnyLocked(timeout);
        }

        waitGroup->release();

        return waitRes;
    }

    bool waitAll(WorkPool::WaitGroupHandle waitGroupHandle, WorkPool::TimeoutUs timeout) {
        auto waitGroup = acquireWaitGroupFromHandle(waitGroupHandle);
        if (!waitGroup) return true;

        bool waitRes = false;

        {
            AutoLock<Lock> waitGroupLock(waitGroup->getLock());
            waitRes = waitGroup->waitAllLocked(timeout);
        }

        waitGroup->release();

        return waitRes;
    }

private:
    // Increments wait group refcount by 1.
    WaitGroup* acquireWaitGroupFromHandle(WorkPool::WaitGroupHandle waitGroupHandle) {
        AutoLock<Lock> lock(mLock);
        auto it = mWaitGroups.find(waitGroupHandle);
        if (it == mWaitGroups.end()) return nullptr;

        auto waitGroup = it->second;
        waitGroup->acquire();

        return waitGroup;
    }

    using WaitGroupStore = std::unordered_map<WorkPool::WaitGroupHandle, WaitGroup*>;

    WorkPool::WaitGroupHandle genWaitGroupHandleLocked() {
        WorkPool::WaitGroupHandle res = mNextWaitGroupHandle;
        ++mNextWaitGroupHandle;
        return res;
    }

    Lock mLock;
    uint64_t mNextWaitGroupHandle = 0;
    WaitGroupStore mWaitGroups;
    std::vector<std::unique_ptr<WorkPoolThread>> mThreads;
};

WorkPool::WorkPool(int numInitialThreads) : mImpl(new WorkPool::Impl(numInitialThreads)) { }
WorkPool::~WorkPool() = default;

WorkPool::WaitGroupHandle WorkPool::schedule(const std::vector<WorkPool::Task>& tasks) {
    return mImpl->schedule(tasks);
}

bool WorkPool::waitAny(WorkPool::WaitGroupHandle waitGroup, WorkPool::TimeoutUs timeout) {
    return mImpl->waitAny(waitGroup, timeout);
}

bool WorkPool::waitAll(WorkPool::WaitGroupHandle waitGroup, WorkPool::TimeoutUs timeout) {
    return mImpl->waitAll(waitGroup, timeout);
}

} // namespace guest
} // namespace gfxstream
