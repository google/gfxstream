/*
 * Copyright (C) 2025 The Android Open Source Project
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

// This impl modified from external/perfetto/src/trace_processor/util/status_macros.h

#ifndef AEMU_UTIL_STATUS_MACROS_H_
#define AEMU_UTIL_STATUS_MACROS_H_

#include "absl/status/status.h"

// Evaluates |expr|, which should return a base::Status. If the status is an
// error status, returns the status from the current function.
#define RETURN_IF_ERROR(expr)                           \
  do {                                                  \
    absl::Status status_macro_internal_status = (expr); \
    if (!status_macro_internal_status.ok())             \
      return status_macro_internal_status;              \
  } while (0)

#define AEMU_INTERNAL_CONCAT_IMPL(x, y) x##y
#define AEMU_INTERNAL_MACRO_CONCAT(x, y) AEMU_INTERNAL_CONCAT_IMPL(x, y)

// Evalues |rhs| which should return a base::StatusOr<T> and assigns this
// to |lhs|. If the status is an error status, returns the status from the
// current function.
#define ASSIGN_OR_RETURN(lhs, rhs)                                   \
  AEMU_INTERNAL_MACRO_CONCAT(auto status_or, __LINE__) = rhs;    \
  RETURN_IF_ERROR(                                                   \
      AEMU_INTERNAL_MACRO_CONCAT(status_or, __LINE__).status()); \
  lhs = std::move(AEMU_INTERNAL_MACRO_CONCAT(status_or, __LINE__).value())

#endif  // AEMU_UTIL_STATUS_MACROS_H_
