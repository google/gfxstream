// Copyright 2021 The Android Open Source Project
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
#include "VirtioGpuTimelines.h"

#include <cinttypes>
#include <cstdio>

#include "gfxstream/host/Tracing.h"
#include "gfxstream/common/logging.h"

using TaskId = VirtioGpuTimelines::TaskId;
using Ring = VirtioGpuTimelines::Ring;
using FenceId = VirtioGpuTimelines::FenceId;

std::unique_ptr<VirtioGpuTimelines> VirtioGpuTimelines::create(FenceCompletionCallback callback) {
    return std::unique_ptr<VirtioGpuTimelines>(new VirtioGpuTimelines(std::move(callback)));
}

VirtioGpuTimelines::VirtioGpuTimelines(FenceCompletionCallback callback)
    : mFenceCompletionCallback(std::move(callback)), mNextId(0) {
        gfxstream::host::InitializeTracing();
    }

TaskId VirtioGpuTimelines::enqueueTask(const Ring& ring) {
    std::lock_guard<std::mutex> lock(mTimelinesMutex);

    TaskId id = mNextId++;

    const uint64_t traceId = gfxstream::host::GetUniqueTracingId();
    GFXSTREAM_TRACE_EVENT_INSTANT(GFXSTREAM_TRACE_VIRTIO_GPU_TIMELINE_CATEGORY,
                                  "Queue timeline task", "Task ID", id,
                                  GFXSTREAM_TRACE_FLOW(traceId));

    std::shared_ptr<Task> task(new Task(id, ring, traceId), [this](Task* task) {
        mTaskIdToTask.erase(task->mId);
        delete task;
    });
    mTaskIdToTask[id] = task;

    Timeline& timeline = GetOrCreateTimelineLocked(ring);
    timeline.mQueue.emplace_back(std::move(task));
    return id;
}

void VirtioGpuTimelines::enqueueFence(const Ring& ring, FenceId fenceId) {
    std::lock_guard<std::mutex> lock(mTimelinesMutex);

    Timeline& timeline = GetOrCreateTimelineLocked(ring);
    timeline.mQueue.emplace_back(fenceId);

    poll_locked(ring);
}

void VirtioGpuTimelines::notifyTaskCompletion(TaskId taskId) {
    std::lock_guard<std::mutex> lock(mTimelinesMutex);
    auto iTask = mTaskIdToTask.find(taskId);
    if (iTask == mTaskIdToTask.end()) {
        GFXSTREAM_FATAL("Task(id = " PRIu64 " can't be found", static_cast<uint64_t>(taskId));
    }
    std::shared_ptr<Task> task = iTask->second.lock();
    if (task == nullptr) {
        GFXSTREAM_FATAL("Task(id = " PRIu64 " has been destroyed", static_cast<uint64_t>(taskId));
    }
    if (task->mId != taskId) {
        GFXSTREAM_FATAL("Task id mismatch. Expected " PRIu64 " actual " PRIu64,
                        static_cast<uint64_t>(taskId), static_cast<uint64_t>(task->mId));
    }
    if (task->mHasCompleted) {
        GFXSTREAM_FATAL("Task(id = " PRIu64 " has been set to completed", static_cast<uint64_t>(taskId));
    }

    GFXSTREAM_TRACE_EVENT_INSTANT(GFXSTREAM_TRACE_VIRTIO_GPU_TIMELINE_CATEGORY,
                                  "Notify timeline task completed",
                                  GFXSTREAM_TRACE_FLOW(task->mTraceId), "Task ID", task->mId);

    task->mHasCompleted = true;

    poll_locked(task->mRing);
}

VirtioGpuTimelines::Timeline& VirtioGpuTimelines::GetOrCreateTimelineLocked(const Ring& ring) {
    auto [it, inserted] =
        mTimelineQueues.emplace(std::piecewise_construct, std::make_tuple(ring), std::make_tuple());
    Timeline& timeline = it->second;
    if (inserted) {
        timeline.mTraceTrackId = gfxstream::host::GetUniqueTracingId();

        const std::string timelineName = "Virtio Gpu Timeline " + to_string(ring);
        GFXSTREAM_TRACE_NAME_TRACK(GFXSTREAM_TRACE_TRACK(timeline.mTraceTrackId), timelineName);

        GFXSTREAM_TRACE_EVENT_INSTANT(GFXSTREAM_TRACE_VIRTIO_GPU_TIMELINE_CATEGORY,
                                      "Create Timeline",
                                      GFXSTREAM_TRACE_TRACK(timeline.mTraceTrackId));
    }

    return timeline;
}

void VirtioGpuTimelines::poll() {
    std::lock_guard<std::mutex> lock(mTimelinesMutex);
    for (const auto& [ring, timeline] : mTimelineQueues) {
        poll_locked(ring);
    }
}
void VirtioGpuTimelines::poll_locked(const Ring& ring) {
    auto timelineIt = mTimelineQueues.find(ring);
    if (timelineIt == mTimelineQueues.end()) {
        const std::string ringString = to_string(ring);
        GFXSTREAM_FATAL("Ring(%s) doesn't exist.", ringString.c_str());
    }
    Timeline& timeline = timelineIt->second;

    auto& timelineQueue = timeline.mQueue;
    auto i = timelineQueue.begin();
    for (; i != timelineQueue.end(); i++) {
        bool shouldStop = std::visit(
            [&](auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, FenceId>) {
                    auto& fenceId = arg;

                    GFXSTREAM_TRACE_EVENT_INSTANT(
                        GFXSTREAM_TRACE_VIRTIO_GPU_TIMELINE_CATEGORY, "Signal Virtio Gpu Fence",
                        GFXSTREAM_TRACE_TRACK(timeline.mTraceTrackId), "Fence", fenceId);

                    mFenceCompletionCallback(ring, fenceId);

                    return false;
                } else if constexpr (std::is_same_v<T, std::shared_ptr<Task>>) {
                    auto& task = arg;

                    const bool completed = task->mHasCompleted;
                    if (completed) {
                        GFXSTREAM_TRACE_EVENT_INSTANT(
                            GFXSTREAM_TRACE_VIRTIO_GPU_TIMELINE_CATEGORY, "Process Task Complete",
                            GFXSTREAM_TRACE_TRACK(timeline.mTraceTrackId),
                            GFXSTREAM_TRACE_FLOW(task->mTraceId), "Task", task->mId);
                    }
                    return !completed;
                }
            },
            *i);

        if (shouldStop) {
            break;
        }
    }
    timelineQueue.erase(timelineQueue.begin(), i);
}

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

std::optional<gfxstream::host::snapshot::VirtioGpuRing> SnapshotRing(const VirtioGpuRing& ring) {
    gfxstream::host::snapshot::VirtioGpuRing snapshot;

    if (std::holds_alternative<VirtioGpuRingGlobal>(ring)) {
        snapshot.mutable_global();
    } else if (std::holds_alternative<VirtioGpuRingContextSpecific>(ring)) {
        auto& specific = std::get<VirtioGpuRingContextSpecific>(ring);

        auto* specificSnapshot = snapshot.mutable_context_specific();
        specificSnapshot->set_context_id(specific.mCtxId);
        specificSnapshot->set_ring_id(specific.mRingIdx);
    } else {
        return std::nullopt;
    }

    return snapshot;
}

std::optional<VirtioGpuRing> RestoreRing(const gfxstream::host::snapshot::VirtioGpuRing& snapshot) {
    if (snapshot.has_global()) {
        return VirtioGpuRingGlobal{};
    } else if (snapshot.has_context_specific()) {
        const auto& specific = snapshot.context_specific();
        return VirtioGpuRingContextSpecific{
            .mCtxId = specific.context_id(),
            .mRingIdx = static_cast<VirtioGpuRingIdx>(specific.ring_id()),
        };
    } else {
        return std::nullopt;
    }
}

/*static*/
std::optional<gfxstream::host::snapshot::VirtioGpuTimelineItem>
VirtioGpuTimelines::SnapshotTimelineItem(const TimelineItem& timelineItem) {
    gfxstream::host::snapshot::VirtioGpuTimelineItem snapshot;

    if (std::holds_alternative<FenceId>(timelineItem)) {
        auto fenceId = std::get<FenceId>(timelineItem);

        auto* fenceSnapshot = snapshot.mutable_fence();
        fenceSnapshot->set_id(fenceId);
    } else if (std::holds_alternative<std::shared_ptr<Task>>(timelineItem)) {
        const auto& task = std::get<std::shared_ptr<Task>>(timelineItem);

        auto ringOpt = SnapshotRing(task->mRing);
        if (!ringOpt) {
            GFXSTREAM_ERROR("Failed to snapshot timeline item: failed to snapshot ring.");
            return std::nullopt;
        }

        auto* taskSnapshot = snapshot.mutable_task();
        taskSnapshot->set_id(task->mId);
        taskSnapshot->mutable_ring()->Swap(&*ringOpt);
        taskSnapshot->set_trace_id(task->mTraceId);
        taskSnapshot->set_completed(task->mHasCompleted);
    } else {
        GFXSTREAM_ERROR("Failed to snapshot timeline item: unhandled.");
        return std::nullopt;
    }

    return snapshot;
}

/*static*/
std::optional<VirtioGpuTimelines::TimelineItem> VirtioGpuTimelines::RestoreTimelineItem(
    const gfxstream::host::snapshot::VirtioGpuTimelineItem& snapshot) {
    if (snapshot.has_fence()) {
        return snapshot.fence().id();
    } else if (snapshot.has_task()) {
        const auto& taskSnapshot = snapshot.task();

        auto ringOpt = RestoreRing(taskSnapshot.ring());
        if (!ringOpt) {
            GFXSTREAM_ERROR("Failed to restore timeline item: failed to restore ring.");
            return std::nullopt;
        }

        auto task = std::make_shared<Task>(taskSnapshot.id(), *ringOpt, taskSnapshot.trace_id());
        task->mHasCompleted = taskSnapshot.completed();
        return task;
    }

    GFXSTREAM_ERROR("Failed to restore timeline item: unhandled.");
    return std::nullopt;
}

std::optional<gfxstream::host::snapshot::VirtioGpuTimeline> VirtioGpuTimelines::Timeline::Snapshot()
    const {
    gfxstream::host::snapshot::VirtioGpuTimeline timeline;
    timeline.set_trace_id(mTraceTrackId);
    for (const auto& timelineItem : mQueue) {
        auto timelineItemSnapshotOpt = SnapshotTimelineItem(timelineItem);
        if (!timelineItemSnapshotOpt) {
            GFXSTREAM_ERROR("Failed to snapshot timeline item.");
            return std::nullopt;
        }
        timeline.mutable_items()->Add(std::move(*timelineItemSnapshotOpt));
    }
    return timeline;
}

/*static*/
std::optional<VirtioGpuTimelines::Timeline> VirtioGpuTimelines::Timeline::Restore(
    const gfxstream::host::snapshot::VirtioGpuTimeline& snapshot) {
    VirtioGpuTimelines::Timeline timeline;
    timeline.mTraceTrackId = snapshot.trace_id();
    for (const auto& timelineItemSnapshot : snapshot.items()) {
        auto timelineItemOpt = RestoreTimelineItem(timelineItemSnapshot);
        if (!timelineItemOpt) {
            GFXSTREAM_ERROR("Failed to snapshot timeline item.");
            return std::nullopt;
        }
        timeline.mQueue.emplace_back(std::move(*timelineItemOpt));
    }
    return timeline;
}

std::optional<gfxstream::host::snapshot::VirtioGpuTimelinesSnapshot> VirtioGpuTimelines::Snapshot()
    const {
    std::lock_guard<std::mutex> lock(mTimelinesMutex);

    gfxstream::host::snapshot::VirtioGpuTimelinesSnapshot snapshot;
    snapshot.set_next_id(mNextId);

    for (const auto& [ring, timeline] : mTimelineQueues) {
        auto ringSnapshotOpt = SnapshotRing(ring);
        if (!ringSnapshotOpt) {
            GFXSTREAM_ERROR("Failed to snapshot timelines: failed to snapshot ring.");
            return std::nullopt;
        }

        auto timelineSnapshotOpt = timeline.Snapshot();
        if (!timelineSnapshotOpt) {
            GFXSTREAM_ERROR("Failed to snapshot timelines: failed to snapshot timeline.");
            return std::nullopt;
        }

        auto* kv = snapshot.add_timelines();
        kv->mutable_ring()->Swap(&*ringSnapshotOpt);
        kv->mutable_timeline()->Swap(&*timelineSnapshotOpt);
    }

    return snapshot;
}

/*static*/
std::unique_ptr<VirtioGpuTimelines> VirtioGpuTimelines::Restore(
    FenceCompletionCallback callback,
    const gfxstream::host::snapshot::VirtioGpuTimelinesSnapshot& snapshot) {
    std::unique_ptr<VirtioGpuTimelines> timelines(new VirtioGpuTimelines(std::move(callback)));

    std::lock_guard<std::mutex> lock(timelines->mTimelinesMutex);

    timelines->mNextId.store(snapshot.next_id());

    for (const auto& timelineSnapshot : snapshot.timelines()) {
        if (!timelineSnapshot.has_ring()) {
            GFXSTREAM_ERROR("Failed to restore timelines: missing ring.");
            return nullptr;
        }
        auto ringOpt = RestoreRing(timelineSnapshot.ring());
        if (!ringOpt) {
            GFXSTREAM_ERROR("Failed to restore timelines: failed to restore ring.");
            return nullptr;
        }

        if (!timelineSnapshot.has_timeline()) {
            GFXSTREAM_ERROR("Failed to restore timelines: missing timeline.");
            return nullptr;
        }
        auto timelineOpt = Timeline::Restore(timelineSnapshot.timeline());
        if (!timelineOpt) {
            GFXSTREAM_ERROR("Failed to restore timelines: failed to restore timeline.");
            return nullptr;
        }

        timelines->mTimelineQueues[std::move(*ringOpt)] = std::move(*timelineOpt);
    }

    // Rebuild task index:
    for (const auto& [_, timeline] : timelines->mTimelineQueues) {
        for (const auto& timelineItem : timeline.mQueue) {
            if (std::holds_alternative<std::shared_ptr<Task>>(timelineItem)) {
                auto& task = std::get<std::shared_ptr<Task>>(timelineItem);
                timelines->mTaskIdToTask[task->mId] = task;
            }
        }
    }

    return timelines;
}

#endif  // GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
