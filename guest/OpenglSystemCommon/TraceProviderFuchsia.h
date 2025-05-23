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

#ifndef __COMMON_TRACE_PROVIDER_FUCHSIA_H
#define __COMMON_TRACE_PROVIDER_FUCHSIA_H

#ifndef __Fuchsia__
#error "TraceProviderFuchsia is only supported on Fuchsia!"
#endif  // __Fuchsia__

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/trace-provider/provider.h>

class TraceProviderFuchsia {
public:
    TraceProviderFuchsia();
    ~TraceProviderFuchsia();

    bool Initialize();

private:
    async::Loop mLoop;
    std::unique_ptr<trace::TraceProvider> mTraceProvider;
};

#endif  // __COMMON_TRACE_PROVIDER_FUCHSIA_H