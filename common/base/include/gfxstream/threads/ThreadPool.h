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

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "aemu/base/Compiler.h"
#include "aemu/base/Optional.h"
#include "aemu/base/system/System.h"
#include "aemu/base/threads/WorkerThread.h"

//
// ThreadPool<Item> - a simple collection of worker threads to process enqueued
// items on multiple cores.
//
// To create a thread pool supply a processing function and an optional number
// of threads to use (default is number of CPU cores).
// Thread pool distributes the work in simple round robin manner over all its
// workers - this means individual items should be simple and take similar time
// to process.
//
// Usage is very similar to one of WorkerThread, with difference being in the
// number of worker threads used and in existence of explicit done() method:
//
//      struct WorkItem { int number; };
//
//      ThreadPool<WorkItem> tp([](WorkItem&& item) { std::cout << item.num; });
//      CHECK(tp.start()) << "Failed to start the thread pool";
//      tp.enqueue({1});
//      tp.enqueue({2});
//      tp.enqueue({3});
//      tp.enqueue({4});
//      tp.enqueue({5});
//      tp.done();
//      tp.join();
//
// Make sure that the processing function won't block worker threads - thread
// pool has no way of detecting it and may potentially get all workers to block,
// resulting in a hanging application.
//

namespace android {
namespace base {

using ThreadPoolWorkerId = uint32_t;

template <class ItemT>
class ThreadPool {
    DISALLOW_COPY_AND_ASSIGN(ThreadPool);

public:
    using Item = ItemT;
    using WorkerId = ThreadPoolWorkerId;
    using Processor = std::function<void(Item&&, WorkerId)>;

   private:
    struct Command {
        Item mItem;
        WorkerId mWorkerId;

        Command(Item&& item, WorkerId workerId) : mItem(std::move(item)), mWorkerId(workerId) {}
        DISALLOW_COPY_AND_ASSIGN(Command);
        Command(Command&&) = default;
    };
    using Worker = WorkerThread<Optional<Command>>;

   public:
    // Fn is the type of the processor, it can either have 2 parameters: 1 for the Item, 1 for the
    // WorkerId, or have only 1 Item parameter.
    template <class Fn, typename = std::enable_if_t<std::is_invocable_v<Fn, Item, WorkerId> ||
                                                    std::is_invocable_v<Fn, Item>>>
    ThreadPool(int threads, Fn&& processor) : mProcessor() {
        if constexpr (std::is_invocable_v<Fn, Item, WorkerId>) {
            mProcessor = std::move(processor);
        } else if constexpr (std::is_invocable_v<Fn, Item>) {
            using namespace std::placeholders;
            mProcessor = std::bind(std::move(processor), _1);
        }
        if (threads < 1) {
            threads = android::base::getCpuCoreCount();
        }
        mWorkers = std::vector<Optional<Worker>>(threads);
        for (auto& workerPtr : mWorkers) {
            workerPtr.emplace([this](Optional<Command>&& commandOpt) {
                if (!commandOpt) {
                    return Worker::Result::Stop;
                }
                Command command = std::move(commandOpt.value());
                mProcessor(std::move(command.mItem), command.mWorkerId);
                return Worker::Result::Continue;
            });
        }
    }
    explicit ThreadPool(Processor&& processor)
        : ThreadPool(0, std::move(processor)) {}
    ~ThreadPool() {
        done();
        join();
    }

    bool start() {
        for (auto& workerPtr : mWorkers) {
            if (workerPtr->start()) {
                ++mValidWorkersCount;
            } else {
                workerPtr.clear();
            }
        }
        return mValidWorkersCount > 0;
    }

    void done() {
        for (auto& workerPtr : mWorkers) {
            if (workerPtr) {
                workerPtr->enqueue(kNullopt);
            }
        }
    }

    void join() {
        for (auto& workerPtr : mWorkers) {
            if (workerPtr) {
                workerPtr->join();
            }
        }
        mWorkers.clear();
        mValidWorkersCount = 0;
    }

    void enqueue(Item&& item) {
        for (;;) {
            int currentIndex =
                    mNextWorkerIndex.fetch_add(1, std::memory_order_relaxed);
            int workerIndex = currentIndex % mWorkers.size();
            auto& workerPtr = mWorkers[workerIndex];
            if (workerPtr) {
                Command command(std::forward<Item>(item), workerIndex);
                workerPtr->enqueue(std::move(command));
                break;
            }
        }
    }

    // The itemFactory will be called multiple times to generate one item for each worker thread.
    template <class Fn, typename = std::enable_if_t<std::is_invocable_r_v<Item, Fn>>>
    void broadcast(Fn&& itemFactory) {
        int i = 0;
        for (auto& workerOpt : mWorkers) {
            if (!workerOpt) continue;
            Command command(std::move(itemFactory()), i);
            workerOpt->enqueue(std::move(command));
            ++i;
        }
    }

    void waitAllItems() {
        if (0 == mValidWorkersCount) return;
        for (auto& workerOpt : mWorkers) {
            if (!workerOpt) continue;
            workerOpt->waitQueuedItems();
        }
    }

    int numWorkers() const { return mValidWorkersCount; }

private:
    Processor mProcessor;
    std::vector<Optional<Worker>> mWorkers;
    std::atomic<int> mNextWorkerIndex{0};
    int mValidWorkersCount{0};
};

}  // namespace base
}  // namespace android
