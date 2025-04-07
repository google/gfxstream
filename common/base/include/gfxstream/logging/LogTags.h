// Copyright (C) 2021 The Android Open Source Project
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
#ifdef __cplusplus
extern "C" {
#endif

#ifndef VERBOSE_TAG_LIST
#error "_VERBOSE_TAG(xx, yy) List not defined."
#endif

#define _VERBOSE_TAG(x, y) VERBOSE_##x,
typedef enum {
    VERBOSE_TAG_LIST VERBOSE_MAX /* do not remove */
} VerboseTag;
#undef _VERBOSE_TAG

#define VERBOSE_ENABLE_IMPL(tag) verbose_enable((int64_t)tag)
#define VERBOSE_ENABLE(tag) VERBOSE_ENABLE_IMPL(VERBOSE_##tag)
#define VERBOSE_DISABLE_IMPL(tag) verbose_disable((int64_t)tag)
#define VERBOSE_DISABLE(tag) VERBOSE_DISABLE_IMPL(VERBOSE_##tag)
#define VERBOSE_CHECK_IMPL(tag) verbose_check((int64_t)tag)
#define VERBOSE_CHECK(tag) VERBOSE_CHECK_IMPL(VERBOSE_##tag)
#define VERBOSE_CHECK_ANY() verbose_check_any();

#define VERBOSE_PRINT_IMPL(tag, ...) \
    if (VERBOSE_CHECK_IMPL(tag)) {   \
        dprint(__VA_ARGS__);  \
    }

#define VERBOSE_PRINT(tag, ...) VERBOSE_PRINT_IMPL(VERBOSE_##tag, __VA_ARGS__)

#define VERBOSE_INFO_IMPL(tag, ...) \
    if (VERBOSE_CHECK_IMPL(tag)) {  \
        dinfo(__VA_ARGS__);  \
    }

#define VERBOSE_INFO(tag, ...) VERBOSE_INFO_IMPL(VERBOSE_##tag, __VA_ARGS__)

#define VERBOSE_DPRINT(tag, ...) VERBOSE_PRINT_IMPL(VERBOSE_##tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
