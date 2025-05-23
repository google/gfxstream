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
#ifndef VIRTIO_GPU_TIMELINES_H
#define VIRTIO_GPU_TIMELINES_H

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include "VirtioGpuTimelinesSnapshot.pb.h"
#endif  // GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include "gfxstream/ThreadAnnotations.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"

typedef uint32_t VirtioGpuCtxId;
typedef uint8_t VirtioGpuRingIdx;

struct VirtioGpuRingGlobal {};
struct VirtioGpuRingContextSpecific {
    VirtioGpuCtxId mCtxId;
    VirtioGpuRingIdx mRingIdx;
};
using VirtioGpuRing = std::variant<VirtioGpuRingGlobal, VirtioGpuRingContextSpecific>;

template <>
struct std::hash<VirtioGpuRingGlobal> {
    std::size_t operator()(VirtioGpuRingGlobal const&) const noexcept { return 0; }
};

inline bool operator==(const VirtioGpuRingGlobal&, const VirtioGpuRingGlobal&) { return true; }

template <>
struct std::hash<VirtioGpuRingContextSpecific> {
    std::size_t operator()(VirtioGpuRingContextSpecific const& ringContextSpecific) const noexcept {
        std::size_t ctxHash = std::hash<VirtioGpuCtxId>{}(ringContextSpecific.mCtxId);
        std::size_t ringHash = std::hash<VirtioGpuRingIdx>{}(ringContextSpecific.mRingIdx);
        // Use the hash_combine from
        // https://www.boost.org/doc/libs/1_78_0/boost/container_hash/hash.hpp.
        std::size_t res = ctxHash;
        res ^= ringHash + 0x9e3779b9 + (res << 6) + (res >> 2);
        return res;
    }
};

inline bool operator==(const VirtioGpuRingContextSpecific& lhs,
                       const VirtioGpuRingContextSpecific& rhs) {
    return lhs.mCtxId == rhs.mCtxId && lhs.mRingIdx == rhs.mRingIdx;
}

inline std::string to_string(const VirtioGpuRing& ring) {
    struct {
        std::string operator()(const VirtioGpuRingGlobal&) { return "global"; }
        std::string operator()(const VirtioGpuRingContextSpecific& ring) {
            std::stringstream ss;
            ss << "context specific {ctx = " << ring.mCtxId << ", ring = " << (int)ring.mRingIdx
               << "}";
            return ss.str();
        }
    } visitor;
    return std::visit(visitor, ring);
}

class VirtioGpuTimelines {
   public:
    using FenceId = uint64_t;
    using Ring = VirtioGpuRing;
    using TaskId = uint64_t;
    using FenceCompletionCallback = std::function<void(const Ring&, FenceId)>;

    static std::unique_ptr<VirtioGpuTimelines> create(FenceCompletionCallback callback);

    TaskId enqueueTask(const Ring&);
    void enqueueFence(const Ring&, FenceId);
    void notifyTaskCompletion(TaskId);
    void poll();

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    std::optional<gfxstream::host::snapshot::VirtioGpuTimelinesSnapshot> Snapshot() const;

    static std::unique_ptr<VirtioGpuTimelines> Restore(
        FenceCompletionCallback callback,
        const gfxstream::host::snapshot::VirtioGpuTimelinesSnapshot& snapshot);
#endif

   private:
    VirtioGpuTimelines(FenceCompletionCallback callback);

    struct Task {
        Task(TaskId id, const Ring& ring, uint64_t traceId)
            : mId(id), mRing(ring), mTraceId(traceId), mHasCompleted(false) {}

        // LINT.IfChange(virtio_gpu_timeline_task)
        TaskId mId;
        Ring mRing;
        uint64_t mTraceId;
        std::atomic_bool mHasCompleted;
        // LINT.ThenChange(VirtioGpuTimelinesSnapshot.proto:virtio_gpu_timeline_task)
    };

    // LINT.IfChange(virtio_gpu_timeline_item)
    using TimelineItem = std::variant<FenceId, std::shared_ptr<Task>>;
    // LINT.ThenChange(VirtioGpuTimelinesSnapshot.proto:virtio_gpu_timeline_item)

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    static std::optional<gfxstream::host::snapshot::VirtioGpuTimelineItem> SnapshotTimelineItem(
        const TimelineItem& timelineItem);

    static std::optional<TimelineItem> RestoreTimelineItem(
        const gfxstream::host::snapshot::VirtioGpuTimelineItem& snapshot);
#endif

    struct Timeline {
        // LINT.IfChange(virtio_gpu_timeline)
        uint64_t mTraceTrackId;
        std::list<TimelineItem> mQueue;
        // LINT.ThenChange(VirtioGpuTimelinesSnapshot.proto:virtio_gpu_timeline)

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
        std::optional<gfxstream::host::snapshot::VirtioGpuTimeline> Snapshot() const;

        static std::optional<Timeline> Restore(
            const gfxstream::host::snapshot::VirtioGpuTimeline& snapshot);
#endif
    };

    Timeline& GetOrCreateTimelineLocked(const Ring& ring) REQUIRES(mTimelinesMutex);

    // Go over the timeline, signal any fences without pending tasks, and remove
    // timeline items that are no longer needed.
    void poll_locked(const Ring&) REQUIRES(mTimelinesMutex);

    FenceCompletionCallback mFenceCompletionCallback;

    mutable std::mutex mTimelinesMutex;
    // The mTaskIdToTask cache must be destroyed after the actual owner of Task,
    // mTimelineQueues, is destroyed, because the deleter of Task will
    // automatically remove the entry in mTaskIdToTask.
    std::unordered_map<TaskId, std::weak_ptr<Task>> mTaskIdToTask;

    // LINT.IfChange(virtio_gpu_timelines)
    std::atomic<TaskId> mNextId;
    std::unordered_map<Ring, Timeline> mTimelineQueues GUARDED_BY(mTimelinesMutex);
    // LINT.ThenChange(VirtioGpuTimelinesSnapshot.proto:virtio_gpu_timelines)
};

#endif  // VIRTIO_GPU_TIMELINES_H
