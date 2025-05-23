/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <future>

namespace gfxstream {

enum class CancelableFutureStatus {
    kUnknown,
    kSuccess,
    kCanceled,
};

using CancelableFuture = std::shared_future<CancelableFutureStatus>;

class AutoCancelingPromise {
   public:
    AutoCancelingPromise() = default;

    AutoCancelingPromise(AutoCancelingPromise& rhs) = delete;
    AutoCancelingPromise& operator=(AutoCancelingPromise& rhs) = delete;

    AutoCancelingPromise(AutoCancelingPromise&& rhs) = default;
    AutoCancelingPromise& operator=(AutoCancelingPromise&& rhs) = default;

    ~AutoCancelingPromise() {
        if (!mValueWasSet) {
            mPromise.set_value(CancelableFutureStatus::kCanceled);
        }
    }

    CancelableFuture GetFuture() { return mPromise.get_future().share(); }

    void MarkComplete() {
        mValueWasSet = true;
        mPromise.set_value(CancelableFutureStatus::kSuccess);
    }

   private:
    bool mValueWasSet = false;
    std::promise<CancelableFutureStatus> mPromise;
};

}  // namespace gfxstream