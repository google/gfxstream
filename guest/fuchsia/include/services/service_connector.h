// Copyright (C) 2019 The Android Open Source Project
// Copyright (C) 2019 Google Inc.
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

#ifndef FUCHSIA_INCLUDE_SERVICES_SERVICE_CONNECTOR_H_
#define FUCHSIA_INCLUDE_SERVICES_SERVICE_CONNECTOR_H_

#include <cstdint>
#include <zircon/types.h>

// Takes the name of a service (e.g. /svc/fuchsia.sysmem.Allocator) and returns
// a handle to a connection to it.
typedef zx_handle_t (*PFN_ConnectToServiceAddr)(const char *pName);

void SetConnectToServiceFunction(PFN_ConnectToServiceAddr func);
PFN_ConnectToServiceAddr GetConnectToServiceFunction();

#endif // FUCHSIA_INCLUDE_SERVICES_SERVICE_CONNECTOR_H_
