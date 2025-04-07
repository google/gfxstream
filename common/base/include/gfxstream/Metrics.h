// Copyright (C) 2021 The Android Open Source Project
// Copyright (C) 2021 Google Inc.
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

#include <inttypes.h>

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>

#include "aemu/base/threads/Thread.h"

// Library to log metrics.
namespace android {
namespace base {

// Struct for hanging events
struct EventHangMetadata {
    const char* file;
    const char* function;
    const char* msg;
    const int line;
    const unsigned long threadId;
    // Field for adding custom key value annotations
    using HangAnnotations = std::unordered_map<std::string, std::string>;
    // Field for adding custom key value annotations
    std::unique_ptr<HangAnnotations> data;

    enum class HangType { kRenderThread, kSyncThread, kOther };
    HangType hangType;

    EventHangMetadata(const char* file, const char* function, const char* msg, int line,
                      HangType hangType, std::unique_ptr<HangAnnotations> data)
        : file(file),
          function(function),
          msg(msg),
          line(line),
          threadId(getCurrentThreadId()),
          data(std::move(data)),
          hangType(hangType) {}

    EventHangMetadata()
        : EventHangMetadata(nullptr, nullptr, nullptr, 0, HangType::kRenderThread, nullptr) {}

    void mergeAnnotations(std::unique_ptr<HangAnnotations> annotations) {
        if (!data) {
            data = std::make_unique<HangAnnotations>();
        }
        data->merge(*annotations);
    }
};

// Events that can be logged.
struct MetricEventBadPacketLength {
    int64_t len;
};
struct MetricEventDuplicateSequenceNum {
    int64_t opcode;
};
struct MetricEventFreeze {};
struct MetricEventUnFreeze { int64_t frozen_ms; };
struct MetricEventHang {
    uint64_t taskId; /* From HealthMonitor */
    EventHangMetadata* metadata;
    int64_t otherHungTasks;
};
struct MetricEventUnHang {
    uint64_t taskId; /* From HealthMonitor */
    EventHangMetadata* metadata;
    int64_t hung_ms;
    int64_t otherHungTasks;
};
struct GfxstreamVkAbort {
    const char* file;
    const char* function;
    const char* msg;
    int line;
    int64_t abort_reason;
};
struct MetricEventVulkanOutOfMemory {
    int64_t vkResultCode;
    std::optional<uint32_t> opCode = std::nullopt;
    const char* function = nullptr;
    std::optional<int> line = std::nullopt;
    std::optional<uint64_t> allocationSize = std::nullopt;
};

using MetricEventType =
    std::variant<std::monostate, MetricEventBadPacketLength, MetricEventDuplicateSequenceNum,
                 MetricEventFreeze, MetricEventUnFreeze, MetricEventHang, MetricEventUnHang,
                 MetricEventVulkanOutOfMemory, GfxstreamVkAbort>;

class MetricsLogger {
   public:
    // Log a MetricEventType.
    virtual void logMetricEvent(MetricEventType eventType) = 0;
    // Set a crash annotation.
    virtual void setCrashAnnotation(const char* key, const char* value) = 0;
    // Virtual destructor.
    virtual ~MetricsLogger() = default;

    // Callbacks to log events
    static void (*add_instant_event_callback)(int64_t event_code);
    static void (*add_instant_event_with_descriptor_callback)(int64_t event_code,
                                                              int64_t descriptor);
    static void (*add_instant_event_with_metric_callback)(int64_t event_code, int64_t metric_value);
    static void (*add_vulkan_out_of_memory_event)(int64_t result_code, uint32_t op_code,
                                                  const char* function, uint32_t line,
                                                  uint64_t allocation_size,
                                                  bool is_host_side_result, bool is_allocation);
    // Crashpad will copy the strings, so these need only persist for the function call
    static void (*set_crash_annotation_callback)(const char* key, const char* value);
};

std::unique_ptr<MetricsLogger> CreateMetricsLogger();

}  // namespace base
}  // namespace android