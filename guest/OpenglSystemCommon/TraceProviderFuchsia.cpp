/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "TraceProviderFuchsia.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/channel.h>

#include "gfxstream/common/logging.h"
#include "services/service_connector.h"

TraceProviderFuchsia::~TraceProviderFuchsia() {
    if (mTraceProvider) {
        async::PostTask(mLoop.dispatcher(), [this]() {
            // trace_provider_.reset() needs to run on loop_'s dispatcher or
            // else its teardown can be racy and crash.
            mTraceProvider.reset();
            // Run Quit() in the loop to ensure this task executes before
            // JoinThreads() returns and the destructor finishes.
            mLoop.Quit();
        });
    } else {
        mLoop.Quit();
    }
    mLoop.JoinThreads();
}

TraceProviderFuchsia::TraceProviderFuchsia()
    : mLoop(&kAsyncLoopConfigNeverAttachToThread) {}

bool TraceProviderFuchsia::Initialize() {
    // Connect to fuchsia.tracing.provider.Registry service.
    zx_handle_t client_channel =
        GetConnectToServiceFunction()("/svc/fuchsia.tracing.provider.Registry");
    if (client_channel == ZX_HANDLE_INVALID) {
        GFXSTREAM_ERROR("Failed to connect to tracing provider service");
        return false;
    }

    zx_status_t status = mLoop.StartThread();
    if (status != ZX_OK) {
        GFXSTREAM_ERROR("Failed to start async loop: %d", status);
        return false;
    }

    mTraceProvider = std::make_unique<trace::TraceProvider>(
        zx::channel(client_channel), mLoop.dispatcher());
    return true;
}
