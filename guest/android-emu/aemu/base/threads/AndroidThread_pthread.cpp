// Copyright (C) 2014 The Android Open Source Project
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

#include "aemu/base/threads/AndroidThread.h"

#include "aemu/base/threads/AndroidThreadStore.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/syscall.h>
#include <sys/types.h>
#endif

#ifdef __Fuchsia__
#include <zircon/process.h>
#endif

namespace gfxstream {
namespace guest {

Thread::Thread(ThreadFlags flags, int stackSize)
    : mThread((pthread_t)NULL), mStackSize(stackSize), mFlags(flags) {}

Thread::~Thread() {
    assert(!mStarted || mFinished);
    if ((mFlags & ThreadFlags::Detach) == ThreadFlags::NoFlags && mStarted &&
        !mJoined) {
        // Make sure we reclaim the OS resources.
        pthread_join(mThread, nullptr);
    }
}

bool Thread::start() {
    if (mStarted) {
        return false;
    }

    bool ret = true;
    mStarted = true;

    const auto useAttributes = mStackSize != 0;

    pthread_attr_t attr;
    if (useAttributes) {
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, mStackSize);
    }

    if (pthread_create(&mThread, mStackSize ? &attr : nullptr, thread_main,
                       this)) {
        ret = false;
        // We _do not_ need to guard this access to |mFinished| because we're
        // sure that the launched thread failed, so there can't be parallel
        // access.
        mFinished = true;
        mExitStatus = -errno;
        // Nothing to join, so technically it's joined.
        mJoined = true;
    }

    if (useAttributes) {
        pthread_attr_destroy(&attr);
    }

    return ret;
}

bool Thread::wait(intptr_t* exitStatus) {
    if (!mStarted || (mFlags & ThreadFlags::Detach) != ThreadFlags::NoFlags) {
        return false;
    }

    // NOTE: Do not hold the lock when waiting for the thread to ensure
    // it can update mFinished and mExitStatus properly in thread_main
    // without blocking.
    if (!mJoined && pthread_join(mThread, NULL)) {
        return false;
    }
    mJoined = true;

    if (exitStatus) {
        *exitStatus = mExitStatus;
    }
    return true;
}

bool Thread::tryWait(intptr_t* exitStatus) {
    if (!mStarted || (mFlags & ThreadFlags::Detach) != ThreadFlags::NoFlags) {
        return false;
    }

    {
        AutoLock<Lock> locker(mLock);
        if (!mFinished) {
            return false;
        }
    }

    if (!mJoined) {
        pthread_join(mThread, NULL);
        mJoined = true;
    }

    if (exitStatus) {
        *exitStatus = mExitStatus;
    }
    return true;
}

// static
void* Thread::thread_main(void* arg) {
    intptr_t ret;

    {
        Thread* self = reinterpret_cast<Thread*>(arg);
        if ((self->mFlags & ThreadFlags::MaskSignals) != ThreadFlags::NoFlags) {
            Thread::maskAllSignals();
        }

        if ((self->mFlags & ThreadFlags::Detach) != ThreadFlags::NoFlags) {
            pthread_detach(pthread_self());
        }

        ret = self->main();

        {
            AutoLock<Lock> lock(self->mLock);
            self->mFinished = true;
            self->mExitStatus = ret;
        }

        self->onExit();
        // |self| is not valid beyond this point
    }

    gfxstream::guest::ThreadStoreBase::OnThreadExit();

    // This return value is ignored.
    return NULL;
}

// static
void Thread::maskAllSignals() {
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, nullptr);
}

// static
void Thread::sleepMs(unsigned n) {
    usleep(n * 1000);
}

// static
void Thread::sleepUs(unsigned n) {
    usleep(n);
}

// static
void Thread::yield() {
    sched_yield();
}

unsigned long getCurrentThreadId() {
#ifdef __ANDROID__
    // bionic has an efficient implementation for gettid.
    pid_t tid = gettid();
#elif defined(__linux__)
    // Linux doesn't always include an implementation of gettid, so we use syscall.
    thread_local pid_t tid = -1;
    if (tid == -1) {
        tid = syscall(__NR_gettid);
    }
#elif defined(__Fuchsia__)
    zx_handle_t tid = zx_thread_self();
#else
    pthread_t thread = pthread_self();
    // POSIX doesn't require pthread_t to be a numeric type.
    // Instead, just pick up the first sizeof(long) bytes as the "id".
    static_assert(sizeof(thread) >= sizeof(long),
                  "Expected pthread_t to be at least sizeof(long) wide");
    unsigned long tid = *reinterpret_cast<unsigned long*>(&tid);
#endif
    static_assert(sizeof(tid) <= sizeof(long),
                  "Expected thread handle to be at most sizeof(long) wide");
    return static_cast<unsigned long>(tid);
}

} // namespace guest
} // namespace gfxstream
