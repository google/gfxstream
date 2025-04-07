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

#include <assert.h>
#include <comdef.h>

#include <sstream>

#include "aemu/base/threads/Thread.h"
#include "aemu/base/system/Win32UnicodeString.h"
#include "host-common/logging.h"

namespace android {
namespace base {

Thread::Thread(ThreadFlags flags, int stackSize, std::optional<std::string> nameOpt)
    : mStackSize(stackSize), mFlags(flags), mNameOpt(std::move(nameOpt)) {}

Thread::~Thread() {
    if (mThread) {
        assert(!mStarted || mFinished);
        CloseHandle(mThread);
    }
}

bool Thread::start() {
    if (mStarted) {
        return false;
    }

    bool ret = true;
    mStarted = true;
    DWORD threadId = 0;
    mThread = CreateThread(NULL, mStackSize, &Thread::thread_main, this, 0,
                           &threadId);
    if (!mThread) {
        // don't reset mStarted: we're artifically limiting the user's
        // ability to retry the failed starts here.
        ret = false;
        mFinished = true;
    }

    if (mNameOpt.has_value()) {
        Win32UnicodeString name(mNameOpt->c_str());
        if (!mNameOpt->empty() && name.size() == 0) {
            ERR("Invalid thread name.");
            return ret;
        }
        HRESULT res = SetThreadDescription(mThread, name.c_str());
        if (FAILED(res)) {
            std::stringstream ss;
            ss << "Failed to set the thread name " << name.toString();
            DWORD threadId = GetThreadId(mThread);
            if (threadId != 0) {
                ss << " for thread " << threadId;
            }
            ss << ": ";
            _com_error e(res);
            Win32UnicodeString errorMessage(e.ErrorMessage());
            if (errorMessage.size() != 0) {
                ss << errorMessage.toString().c_str() << "(" << res << ")";
            } else {
                ss << res;
            }
            ss << ".";
            ERR("%s", ss.str().c_str());
        }
    }
    return ret;
}

bool Thread::wait(intptr_t* exitStatus) {
    if (!mStarted || (mFlags & ThreadFlags::Detach) != ThreadFlags::NoFlags) {
        return false;
    }

    // NOTE: Do not hold lock during wait to allow thread_main to
    // properly update mIsRunning and mFinished on thread exit.
    if (WaitForSingleObject(mThread, INFINITE) == WAIT_FAILED) {
        return false;
    }

    if (exitStatus) {
        *exitStatus = mExitStatus;
    }
    return true;
}

bool Thread::tryWait(intptr_t* exitStatus) {
    if (!mStarted || (mFlags & ThreadFlags::Detach) != ThreadFlags::NoFlags) {
        return false;
    }

    AutoLock locker(mLock);
    if (!mFinished || WaitForSingleObject(mThread, 0) != WAIT_OBJECT_0) {
        return false;
    }

    if (exitStatus) {
        *exitStatus = mExitStatus;
    }
    return true;
}

// static
DWORD WINAPI Thread::thread_main(void* arg) {
    {
        // no need to call maskAllSignals() here: we know
        // that on Windows it's a noop
        Thread* self = reinterpret_cast<Thread*>(arg);
        auto ret = self->main();

        {
            AutoLock lock(self->mLock);
            self->mFinished = true;
            self->mExitStatus = ret;
        }

        self->onExit();
        // |self| is not valid beyond this point
    }

    // This return value is ignored.
    return 0;
}

// static
void Thread::maskAllSignals() {
    // no such thing as signal in Windows
}

// static
void Thread::sleepMs(unsigned n) {
    ::Sleep(n);
}

// static
void Thread::sleepUs(unsigned n) {
    // Hehe
    ::Sleep(n / 1000);
}

// static
void Thread::yield() {
    if (!::SwitchToThread()) {
        ::Sleep(0);
    }
}

unsigned long getCurrentThreadId() {
    return static_cast<unsigned long>(GetCurrentThreadId());
}

static unsigned long sUiThreadId = 0;
void setUiThreadId(unsigned long id) {
    sUiThreadId = id;

}

bool isRunningInUiThread() {
    if (!sUiThreadId) return false;
    return sUiThreadId == getCurrentThreadId();
}

}  // namespace base
}  // namespace android
