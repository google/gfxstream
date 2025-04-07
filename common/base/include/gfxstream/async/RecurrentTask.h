// Copyright (C) 2015 The Android Open Source Project
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
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

#include "aemu/base/async/Looper.h"

namespace android {
namespace base {

/**
 * @class RecurrentTask
 * @brief A class to run a recurring task on a Looper event loop.
 *
 * The RecurrentTask allows scheduling a task that will run repeatedly
 * at a defined interval on the event loop. The task will continue running
 * until it is explicitly stopped.
 */
class RecurrentTask {
   public:
    /**
     * @typedef TaskFunction
     * @brief Defines the function type for the task to be run.
     *
     * The function returns a boolean indicating whether the task
     * should be run again (`true`) or not (`false`).
     */
    using TaskFunction = std::function<bool()>;

    /**
     * @brief Constructor to initialize a RecurrentTask.
     *
     * @param looper The Looper on which the task will be scheduled.
     * @param function The task function that returns a boolean indicating
     *                 whether the task should repeat.
     * @param taskIntervalMs The interval (in milliseconds) between task executions.
     */
    RecurrentTask(Looper* looper, TaskFunction function, Looper::Duration taskIntervalMs)
        : mLooper(looper),
          mFunction(std::move(function)),
          mTaskIntervalMs(static_cast<int>(taskIntervalMs)),
          mTimer(mLooper->createTimer(&RecurrentTask::taskCallbackStatic, this)) {}

    /**
     * @brief Constructor to initialize a RecurrentTask.
     *
     * @param looper The Looper on which the task will be scheduled.
     * @param function The task function that returns a boolean indicating
     *                 whether the task should repeat.
     * @param interval The interval (in milliseconds) between task executions.
     */
    RecurrentTask(Looper* looper, TaskFunction function, std::chrono::milliseconds interval)
        : RecurrentTask(looper, function, interval.count()) {}

    ~RecurrentTask() { stopAndWait(); }

    /**
     * @brief Starts the task, scheduling it on the looper.
     *
     * @param runImmediately If true, runs the task immediately; otherwise, it waits
     *                       for the task interval before running.
     */
    void start(bool runImmediately = false) {
        start(runImmediately ? std::chrono::milliseconds(0)
                             : std::chrono::milliseconds(mTaskIntervalMs));
    }

    /**
     * @brief Starts the task, scheduling it on the looper after an initial delay.
     *
     * @param initialDelay The delay (in milliseconds) before the first execution of the task.
     */
    void start(std::chrono::milliseconds initialDelay) {
        std::lock_guard<std::mutex> lock(mMutex);
        mInFlight = true;
        mTimer->startRelative(initialDelay.count());
    }

    /**
     * @brief Stops the task asynchronously.
     *
     * This function stops the timer and prevents any further task execution.
     * The function will not wait for a currently running task to complete.
     */
    void stopAsync() {
        std::lock_guard<std::mutex> lock(mMutex);
        mInFlight = false;
        mTimer->stop();
    }

    /**
     * @brief Stops the task and waits for any ongoing task to finish.
     *
     * Ensures that any currently running task completes before stopping the recurrent task.
     */
    void stopAndWait() {
        // To implement this properly we need Looper::Timer::stopAndWait.
        stopAsync();
    }

    /**
     * @brief Checks if the task is currently in flight (scheduled or running).
     *
     * @return true if the task is scheduled or running, false otherwise.
     */
    bool inFlight() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mInFlight;
    }

    /**
     * @brief Gets the task execution interval in milliseconds.
     *
     * @return The interval (in milliseconds) between task executions.
     */
    Looper::Duration taskIntervalMs() const { return mTaskIntervalMs; }

    /**
     * @brief Gets the task execution interval
     *
     * @return The interval (in milliseconds) between task executions.
     */
    std::chrono::milliseconds interval() const {
        return std::chrono::milliseconds(mTaskIntervalMs);
    }

   private:
    void taskCallback(Looper::Timer* /*timer*/) {
        if (inFlight()) {
            if (mFunction()) {
                std::lock_guard<std::mutex> lock(mMutex);
                if (mInFlight) {
                    mTimer->startRelative(mTaskIntervalMs);
                }
            } else {
                std::lock_guard<std::mutex> lock(mMutex);
                mInFlight = false;
            }
        }
    }

    static void taskCallbackStatic(void* that, Looper::Timer* timer) {
        static_cast<RecurrentTask*>(that)->taskCallback(timer);
    }

   protected:
    Looper* const mLooper;
    const TaskFunction mFunction;
    const int mTaskIntervalMs;
    bool mInFlight = false;
    const std::unique_ptr<Looper::Timer> mTimer;
    mutable std::mutex mMutex;
};

/**
 * @class SimpleRecurrentTask
 * @brief A simple scheduler that automatically deletes itself
 *        when the task function returns false, indicating completion.
 *
 * This class is used to repeatedly schedule tasks on the looper thread
 * and delete itself once the task is done (i.e., when the task function
 * returns false).
 */
class SimpleRecurrentTask {
   public:
    /**
     * @brief Schedules a task that runs on the given interval until the
     *        task function returns false.
     *
     * The function creates a new instance of SimpleRecurrentTask, which starts
     * after the initial delay and automatically deletes itself when the task is done.
     *
     * @param looper The event loop on which to schedule the task.
     * @param function The task function to be executed. Should return `true`
     *        to continue scheduling the task or `false` to stop and delete
     *        the task.
     * @param interval The interval between successive executions of the task.
     * @param initialDelay The delay before the first execution.
     *
     * @note The task will keep running until the provided task function
     *       returns false.
     * @note This can leak a SimpleRecurrentTask object if the looper is deleted
     *       when the task is still scheduled.
     *
     * Example usage:
     * @code
     * SimpleRecurrentTask::schedule(looper, []() {
     *     std::cout << "Task executed!" << std::endl;
     *     return someConditionMet;  // Return false to stop the task
     * }, std::chrono::milliseconds(1000));  // Run every 1 second
     * @endcode
     */
    static void schedule(Looper* looper, RecurrentTask::TaskFunction function,
                         std::chrono::milliseconds interval,
                         std::chrono::milliseconds initialDelay = std::chrono::milliseconds(0)) {
        new SimpleRecurrentTask(looper, function, interval, initialDelay);
    }

   private:
    SimpleRecurrentTask(Looper* looper, RecurrentTask::TaskFunction function,
                        std::chrono::milliseconds interval, std::chrono::milliseconds initialDelay)
        : mTimer(looper->createTimer(&SimpleRecurrentTask::taskCallback, this)),
          mTaskInterval(interval),
          mFunction(std::move(function)) {
        mTimer->startRelative(initialDelay.count());
    }

    static void taskCallback(void* opaqueThis, Looper::Timer* /*timer*/) {
        // Note that timer == mTimer
        auto self = static_cast<SimpleRecurrentTask*>(opaqueThis);
        if (self->mFunction()) {
            self->mTimer->startRelative(self->mTaskInterval.count());
        } else {
            delete self;
        }
    }

    const std::unique_ptr<Looper::Timer> mTimer;
    const std::chrono::milliseconds mTaskInterval;
    const RecurrentTask::TaskFunction mFunction;
};

}  // namespace base
}  // namespace android
