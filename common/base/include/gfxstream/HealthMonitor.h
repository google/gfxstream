/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <queue>
#include <stack>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "aemu/base/synchronization/ConditionVariable.h"
#include "aemu/base/synchronization/Lock.h"
#include "aemu/base/Metrics.h"
#include "aemu/base/threads/Thread.h"
#include "host-common/GfxstreamFatalError.h"
#include "host-common/logging.h"

using android::base::EventHangMetadata;
using android::base::getCurrentThreadId;

#define WATCHDOG_BUILDER(healthMonitorPtr, msg)                                  \
    ::emugl::HealthWatchdogBuilder<std::decay_t<decltype(*(healthMonitorPtr))>>( \
        (healthMonitorPtr), __FILE__, __func__, msg, __LINE__)

namespace emugl {

using android::base::ConditionVariable;
using android::base::Lock;
using android::base::MetricsLogger;
using std::chrono::duration;
using std::chrono::steady_clock;
using std::chrono::time_point;
using HangAnnotations = EventHangMetadata::HangAnnotations;

static uint64_t kDefaultIntervalMs = 1'000;
static uint64_t kDefaultTimeoutMs = 5'000;
static std::chrono::nanoseconds kTimeEpsilon(1);

// HealthMonitor provides the ability to register arbitrary start/touch/stop events associated
// with client defined tasks. At some pre-defined interval, it will periodically consume
// all logged events to assess whether the system is hanging on any task. Via the
// MetricsLogger, it will log hang and unhang events when it detects tasks hanging/resuming.
// Design doc: http://go/gfxstream-health-monitor
template <class Clock = steady_clock>
class HealthMonitor : public android::base::Thread {
   public:
    // Alias for task id.
    using Id = uint64_t;

    // Constructor
    // `heatbeatIntervalMs` is the interval, in milleseconds, that the thread will sleep for
    // in between health checks.
    HealthMonitor(MetricsLogger& metricsLogger, uint64_t heartbeatInterval = kDefaultIntervalMs);

    // Destructor
    // Enqueues an event to end monitoring and waits on thread to process remaining queued events.
    ~HealthMonitor();

    // Start monitoring a task. Returns an id that is used for touch and stop operations.
    // `metadata` is a struct containing info on the task watchdog to be passed through to the
    // metrics logger.
    // `onHangAnnotationsCallback` is an optional containing a callable that will return key-value
    // string pairs to be recorded at the time a hang is detected, which is useful for debugging.
    // `timeout` is the duration in milliseconds a task is allowed to run before it's
    // considered "hung". Because `timeout` must be larger than the monitor's heartbeat
    // interval, as shorter timeout periods would not be detected, this method will set actual
    // timeout to the lesser of `timeout` and twice the heartbeat interval.
    // `parentId` can be the Id of another task. Events in this monitored task will update
    // the parent task recursively.
    Id startMonitoringTask(std::unique_ptr<EventHangMetadata> metadata,
                           std::optional<std::function<std::unique_ptr<HangAnnotations>()>>
                               onHangAnnotationsCallback = std::nullopt,
                           uint64_t timeout = kDefaultTimeoutMs,
                           std::optional<Id> parentId = std::nullopt);

    // Touch a monitored task. Resets the timeout countdown for that task.
    void touchMonitoredTask(Id id);

    // Stop monitoring a task.
    void stopMonitoringTask(Id id);

   private:
    using Duration = typename Clock::duration;  // duration<double>;
    using Timestamp = time_point<Clock, Duration>;

    // Allow test class access to private functions
    friend class HealthMonitorTest;

    struct MonitoredEventType {
        struct Start {
            Id id;
            std::unique_ptr<EventHangMetadata> metadata;
            Timestamp timeOccurred;
            std::optional<std::function<std::unique_ptr<HangAnnotations>()>>
                onHangAnnotationsCallback;
            Duration timeoutThreshold;
            std::optional<Id> parentId;
        };
        struct Touch {
            Id id;
            Timestamp timeOccurred;
        };
        struct Stop {
            Id id;
            Timestamp timeOccurred;
        };
        struct EndMonitoring {};
        struct Poll {
            std::promise<void> complete;
        };
    };

    using MonitoredEvent =
        std::variant<std::monostate, typename MonitoredEventType::Start,
                     typename MonitoredEventType::Touch, typename MonitoredEventType::Stop,
                     typename MonitoredEventType::EndMonitoring, typename MonitoredEventType::Poll>;

    struct MonitoredTask {
        Id id;
        Timestamp timeoutTimestamp;
        Duration timeoutThreshold;
        std::optional<Timestamp> hungTimestamp;
        std::unique_ptr<EventHangMetadata> metadata;
        std::optional<std::function<std::unique_ptr<HangAnnotations>()>> onHangAnnotationsCallback;
        std::optional<Id> parentId;
    };

    // Thread's main loop
    intptr_t main() override;

    // Update the parent task
    void updateTaskParent(std::queue<std::unique_ptr<MonitoredEvent>>& events,
                          const MonitoredTask& task, Timestamp eventTime);

    // Explicitly wake the monitor thread. Returns a future that can be used to wait until the
    // poll event has been processed.
    std::future<void> poll();

    // Immutable. Multi-thread access is safe.
    const Duration mInterval;

    // Members accessed only on the worker thread. Not protected by mutex.
    int mHungTasks = 0;
    MetricsLogger& mLogger;
    std::unordered_map<Id, MonitoredTask> mMonitoredTasks;

    // Lock and cv control access to queue and id counter
    android::base::ConditionVariable mCv;
    Lock mLock;
    Id mNextId = 0;
    std::queue<std::unique_ptr<MonitoredEvent>> mEventQueue;
};

// This class provides an RAII mechanism for monitoring a task.
// HealthMonitorT should have the exact same interface as HealthMonitor. Note that HealthWatchdog
// can be used in performance critical path, so we use a template to dispatch a call here to
// overcome the performance cost of virtual function dispatch.
template <class HealthMonitorT = HealthMonitor<>>
class HealthWatchdog {
   public:
    HealthWatchdog(HealthMonitorT* healthMonitor, std::unique_ptr<EventHangMetadata> metadata,
                   std::optional<std::function<std::unique_ptr<HangAnnotations>()>>
                       onHangAnnotationsCallback = std::nullopt,
                   uint64_t timeout = kDefaultTimeoutMs)
        : mHealthMonitor(healthMonitor), mThreadId(getCurrentThreadId()) {
        if (!mHealthMonitor) {
            mId = std::nullopt;
            return;
        }
        auto& threadTasks = getMonitoredThreadTasks();
        auto& stack = threadTasks[mHealthMonitor];
        typename HealthMonitorT::Id id = mHealthMonitor->startMonitoringTask(
            std::move(metadata), std::move(onHangAnnotationsCallback), timeout,
            stack.empty() ? std::nullopt : std::make_optional(stack.top()));
        mId = id;
        stack.push(id);
    }

    ~HealthWatchdog() {
        if (!mId.has_value()) {
            return;
        }
        mHealthMonitor->stopMonitoringTask(*mId);
        checkedStackPop();
    }

    void touch() {
        if (!mId.has_value()) {
            return;
        }
        mHealthMonitor->touchMonitoredTask(*mId);
    }

    // Return the underlying Id, and don't issue a stop on destruction.
    std::optional<typename HealthMonitorT::Id> release() {
        if (mId.has_value()) {
            checkedStackPop();
        }
        return std::exchange(mId, std::nullopt);
    }

   private:
    using ThreadTasks =
        std::unordered_map<HealthMonitorT*, std::stack<typename HealthMonitorT::Id>>;
    std::optional<typename HealthMonitorT::Id> mId;
    HealthMonitorT* mHealthMonitor;
    const unsigned long mThreadId;

    // Thread local stack of task Ids enables better reentrant behavior.
    // Multiple health monitors are not expected or advised, but as an injected dependency,
    // it is possible.
    ThreadTasks& getMonitoredThreadTasks() {
        static thread_local ThreadTasks threadTasks;
        return threadTasks;
    }

    // Pop the stack for the current thread, but with validation. Must be called with a non-empty
    // WatchDog.
    void checkedStackPop() {
        typename HealthMonitorT::Id id = *mId;
        auto& threadTasks = getMonitoredThreadTasks();
        auto& stack = threadTasks[mHealthMonitor];
        if (getCurrentThreadId() != mThreadId) {
            GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                << "HealthWatchdog destructor thread does not match origin. Destructor must be "
                   "called on the same thread.";
        }
        if (stack.empty()) {
            GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                << "HealthWatchdog thread local stack is empty!";
        }
        if (stack.top() != id) {
            GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                << "HealthWatchdog id " << id << " does not match top of stack: " << stack.top();
        }
        stack.pop();
    }
};

// HealthMonitorT should have the exact same interface as HealthMonitor. This template parameter is
// used for injecting a different type for testing.
template <class HealthMonitorT>
class HealthWatchdogBuilder {
   public:
    HealthWatchdogBuilder(HealthMonitorT* healthMonitor, const char* fileName,
                          const char* functionName, const char* message, uint32_t line)
        : mHealthMonitor(healthMonitor),
          mMetadata(std::make_unique<EventHangMetadata>(
              fileName, functionName, message, line, EventHangMetadata::HangType::kOther, nullptr)),
          mTimeoutMs(kDefaultTimeoutMs),
          mOnHangCallback(std::nullopt) {}

    DISALLOW_COPY_ASSIGN_AND_MOVE(HealthWatchdogBuilder);

    HealthWatchdogBuilder& setHangType(EventHangMetadata::HangType hangType) {
        if (mHealthMonitor) mMetadata->hangType = hangType;
        return *this;
    }
    HealthWatchdogBuilder& setTimeoutMs(uint32_t timeoutMs) {
        if (mHealthMonitor) mTimeoutMs = timeoutMs;
        return *this;
    }
    // F should be a callable that returns a std::unique_ptr<EventHangMetadata::HangAnnotations>. We
    // use template instead of std::function here to avoid extra copy.
    template <class F>
    HealthWatchdogBuilder& setOnHangCallback(F&& callback) {
        if (mHealthMonitor) {
            mOnHangCallback =
                std::function<std::unique_ptr<HangAnnotations>()>(std::forward<F>(callback));
        }
        return *this;
    }

    HealthWatchdogBuilder& setAnnotations(std::unique_ptr<HangAnnotations> annotations) {
        if (mHealthMonitor) mMetadata->data = std::move(annotations);
        return *this;
    }

    std::unique_ptr<HealthWatchdog<HealthMonitorT>> build() {
        // We are allocating on the heap, so there is a performance hit. However we also allocate
        // EventHangMetadata on the heap, so this should be Ok. If we see performance issues with
        // these allocations, for HealthWatchdog, we can always use placement new + noop deleter to
        // avoid heap allocation for HealthWatchdog.
        return std::make_unique<HealthWatchdog<HealthMonitorT>>(
            mHealthMonitor, std::move(mMetadata), std::move(mOnHangCallback), mTimeoutMs);
    }

   private:
    HealthMonitorT* mHealthMonitor;
    std::unique_ptr<EventHangMetadata> mMetadata;
    uint32_t mTimeoutMs;
    std::optional<std::function<std::unique_ptr<HangAnnotations>()>> mOnHangCallback;
};

std::unique_ptr<HealthMonitor<>> CreateHealthMonitor(
    MetricsLogger& metricsLogger, uint64_t heartbeatInterval = kDefaultIntervalMs);

}  // namespace emugl
