// Copyright 2022 The Android Open Source Project
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
class AsyncResult {
   public:
    // Describes the status of functions that may asynchronously schedule
    // callbacks for execution.
    enum ResultValue {
        OK_AND_CALLBACK_SCHEDULED,
        OK_AND_CALLBACK_NOT_SCHEDULED,
        OK_AND_CALLBACK_FIRED,
        FAIL_AND_CALLBACK_NOT_SCHEDULED,
    };

    AsyncResult() = default;
    constexpr AsyncResult(ResultValue val) : value(val) {}
    constexpr bool Succeeded() { return value != ResultValue::FAIL_AND_CALLBACK_NOT_SCHEDULED; }
    constexpr bool CallbackScheduledOrFired() {
        return value == OK_AND_CALLBACK_SCHEDULED || value == OK_AND_CALLBACK_FIRED;
    }
  private:
    ResultValue value;

};
