// Copyright (C) 2017 The Android Open Source Project
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

#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "gfxstream/ThreadAnnotations.h"

//
// WorkerThread<Item> encapsulates an asynchronous processing queue for objects
// of type Item. It manages queue memory, runs processing function in a separate
// thread and allows the processing function to stop it at any moment.
//
// Expected usage of the class:
//
// - Define an object to store all data for processing:
//      struct WorkItem { int number; };
//
// - Create a WorkerThread with processing function:
//      WorkerThread<WorkItem> worker([](WorkItem&& item) {
//          std::cout << item.number;
//          return item.number
//              ? WorkerProcessingResult::Continue
//              : WorkerProcessingResult::Stop;
//      });
//
// - Start the worker and send some data for asynchronous processing
//      worker.start();
//      worker.enqueue({1});
//      worker.enqueue({2});
//      worker.enqueue({});     // <--- this item will stop processing.
//      worker.join();
//
// WorkerThread<>'s all methods are thread-safe, with an expectation that the
// work could be added from any number of threads at once.
//
// Note: destructor calls join() implicitly - it's better to send some
// end-of-work marker before trying to destroy a worker thread.
//

namespace gfxstream {
namespace base {

// Return values for a worker thread's processing function.
enum class WorkerProcessingResult { Continue, Stop };

template <class Item>
class WorkerThread {
  public:
    using Result = WorkerProcessingResult;
    // A function that's called for each enqueued item in a separate thread.
    using Processor = std::function<Result(Item&&)>;

    WorkerThread(Processor&& processor) : mProcessor(std::move(processor)) { mQueue.reserve(10); }

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    ~WorkerThread() {
        join();
    }

    // Starts the worker thread.
    bool start() {
        std::lock_guard<std::mutex> lock(mThreadMutex);
        if (!mThread) {
            mThread.emplace([this]() { ThreadLoop(); });
        }
        return true;
    }

    // Waits for all enqueue()'d items to finish or the worker stops.
    void waitQueuedItems() {
        // Enqueue an empty sync command.
        std::future<void> completeFuture = enqueueImpl(Command());
        completeFuture.wait();
    }

    // Moves the |item| into internal queue for processing. If the command is enqueued after the
    // stop command is enqueued or before start() returns, the returned future will also be ready
    // without processing the command.
    std::future<void> enqueue(Item&& item) {
        return enqueueImpl(Command(std::move(item)));
    }

    void join() {
        std::lock_guard<std::mutex> lock(mThreadMutex);
        if (mThread) {
            mThread->join();
            mThread.reset();
        }
    }

   private:
    struct Command {
        Command() : mWorkItem(std::nullopt) {}
        Command(Item&& it) : mWorkItem(std::move(it)) {}
        Command(Command&& other)
            : mCompletedPromise(std::move(other.mCompletedPromise)),
              mWorkItem(std::move(other.mWorkItem)) {}

        std::promise<void> mCompletedPromise;
        std::optional<Item> mWorkItem;
    };

    std::future<void> enqueueImpl(Command command) {
        std::lock_guard<std::mutex> lock(mMutex);

        // Do not enqueue any new items if exiting.
        if (mExiting) {
            command.mCompletedPromise.set_value();
            return command.mCompletedPromise.get_future();
        }

        std::future<void> res = command.mCompletedPromise.get_future();
        mQueue.emplace_back(std::move(command));
        mCv.notify_one();
        return res;
    }

    void ThreadLoop() {
        std::vector<Command> todo;
        todo.reserve(10);
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mMutex);
                ScopedLockAssertion lockAssertion(mMutex);
                mCv.wait(lock, [this]{
                    ScopedLockAssertion lockAssertion(mMutex);
                    return !mQueue.empty();
                });
                todo.swap(mQueue);
            }

            bool shouldStop = false;
            for (Command& item : todo) {
                if (!shouldStop && item.mWorkItem) {
                    shouldStop = mProcessor(std::move(item.mWorkItem.value())) == Result::Stop;
                }
                item.mCompletedPromise.set_value();
            }

            if (shouldStop) {
                std::lock_guard<std::mutex> lock(mMutex);

                mExiting = true;

                // Signal pending tasks as if they are completed.
                for (Command& item : mQueue) {
                    item.mCompletedPromise.set_value();
                }

                return;
            }

            todo.clear();
        }
    }

    Processor mProcessor;

    std::mutex mThreadMutex;
    std::optional<std::thread> mThread GUARDED_BY(mThreadMutex);

    std::mutex mMutex;
    std::condition_variable mCv GUARDED_BY(mMutex);
    std::vector<Command> mQueue GUARDED_BY(mMutex);
    bool mExiting GUARDED_BY(mMutex) = false;
};

}  // namespace base
}  // namespace android
