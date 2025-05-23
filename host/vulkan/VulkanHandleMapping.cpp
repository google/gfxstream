// Copyright (C) 2018 The Android Open Source Project
// Copyright (C) 2018 Google Inc.
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
#include "VulkanHandleMapping.h"

#include <vulkan/vulkan.h>

#include "VkDecoderGlobalState.h"
#include "VulkanBoxedHandles.h"

namespace gfxstream {
namespace vk {

#define DEFAULT_HANDLE_MAP_DEFINE(type)                                                            \
    void DefaultHandleMapping::mapHandles_##type(type*, size_t) { return; }                        \
    void DefaultHandleMapping::mapHandles_##type##_u64(const type* handles, uint64_t* handle_u64s, \
                                                       size_t count) {                             \
        for (size_t i = 0; i < count; ++i) {                                                       \
            handle_u64s[i] = (uint64_t)(uintptr_t)handles[i];                                      \
        }                                                                                          \
    }                                                                                              \
    void DefaultHandleMapping::mapHandles_u64_##type(const uint64_t* handle_u64s, type* handles,   \
                                                     size_t count) {                               \
        for (size_t i = 0; i < count; ++i) {                                                       \
            handles[i] = (type)(uintptr_t)handle_u64s[i];                                          \
        }                                                                                          \
    }

GOLDFISH_VK_LIST_HANDLE_TYPES(DEFAULT_HANDLE_MAP_DEFINE)


#define MAKE_HANDLE_MAPPING_FOREACH(class_name, type_name, map_impl, map_to_u64_impl, map_from_u64_impl)       \
    void class_name::mapHandles_##type_name(type_name* handles, size_t count)  {                               \
        for (size_t i = 0; i < count; ++i) {                                                                   \
            map_impl                                                                                           \
        }                                                                                                      \
    }                                                                                                          \
    void class_name::mapHandles_##type_name##_u64(const type_name* handles, uint64_t* handle_u64s, size_t count)  { \
        for (size_t i = 0; i < count; ++i) {                                                                        \
            map_to_u64_impl                                                                                         \
        }                                                                                                           \
    }                                                                                                               \
    void class_name::mapHandles_u64_##type_name(const uint64_t* handle_u64s, type_name* handles, size_t count) {    \
        for (size_t i = 0; i < count; ++i) {                                                                        \
            map_from_u64_impl                                                                                       \
        }                                                                                                           \
    }

#define BOXED_DISPATCHABLE_UNWRAP_IMPL(type_name)                                                  \
    MAKE_HANDLE_MAPPING_FOREACH(                                                                   \
        BoxedHandleUnwrapMapping,                                                                  \
        type_name,                                                                                 \
        if (handles[i]) {                                                                          \
            handles[i] = unbox_##type_name(handles[i]);                                            \
        } else {                                                                                   \
            handles[i] = (type_name) nullptr;                                                      \
        }                                                                                          \
        ,                                                                                          \
        if (handles[i]) {                                                                          \
            handle_u64s[i] = (uint64_t)unbox_##type_name(handles[i]);                              \
        } else {                                                                                   \
            handle_u64s[i] = 0;                                                                    \
        },                                                                                         \
        if (handle_u64s[i]) {                                                                      \
            handles[i] = unbox_##type_name((type_name)(uintptr_t)handle_u64s[i]);                  \
        } else {                                                                                   \
            handles[i] = (type_name) nullptr;                                                      \
        })

#define BOXED_NON_DISPATCHABLE_UNWRAP_IMPL(type_name)                                              \
    MAKE_HANDLE_MAPPING_FOREACH(                                                                   \
        BoxedHandleUnwrapMapping,                                                                  \
        type_name,                                                                                 \
        if (handles[i]) {                                                                          \
            handles[i] = unbox_##type_name(handles[i]);                                            \
        } else {                                                                                   \
            handles[i] = (type_name) nullptr;                                                      \
        }                                                                                          \
        ,                                                                                          \
        if (handles[i]) {                                                                          \
            handle_u64s[i] = (uint64_t)unbox_##type_name(handles[i]);                              \
        } else {                                                                                   \
            handle_u64s[i] = 0;                                                                    \
        },                                                                                         \
        if (handle_u64s[i]) {                                                                      \
            handles[i] = unbox_##type_name((type_name)(uintptr_t)handle_u64s[i]);                  \
        } else {                                                                                   \
            handles[i] = (type_name) nullptr;                                                      \
        })

GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(BOXED_DISPATCHABLE_UNWRAP_IMPL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(BOXED_NON_DISPATCHABLE_UNWRAP_IMPL)

// Not used, so we do not define.
#define BOXED_DISPATCHABLE_CREATE_IMPL(type_name)                                  \
    MAKE_HANDLE_MAPPING_FOREACH(                                                   \
        BoxedHandleCreateMapping,                                                  \
        type_name,                                                                 \
        (void)handles[i]; ,                                                        \
        (void)handle_u64s[i]; ,                                                    \
        (void)handles[i];                                                          \
        )

// We only use the create/destroy mappings for non dispatchable handles.
#define BOXED_NON_DISPATCHABLE_CREATE_IMPL(type_name)                                    \
    MAKE_HANDLE_MAPPING_FOREACH(                                                         \
        BoxedHandleCreateMapping,                                                        \
        type_name,                                                                       \
        handles[i] = new_boxed_non_dispatchable_##type_name(handles[i]); ,               \
        handle_u64s[i] = (uint64_t)new_boxed_non_dispatchable_##type_name(handles[i]); , \
        handles[i] = (type_name)new_boxed_non_dispatchable_##type_name(                  \
            (type_name)(uintptr_t)handle_u64s[i]);                                       \
        )

#define BOXED_NON_DISPATCHABLE_UNWRAP_AND_DELETE_IMPL(type_name)                           \
    MAKE_HANDLE_MAPPING_FOREACH(                                                           \
        BoxedHandleCreateMapping,                                                          \
        type_name,                                                                         \
        if (handles[i]) {                                                                  \
            auto boxed = handles[i];                                                       \
            handles[i] = unbox_##type_name(handles[i]);                                    \
            delete_##type_name(boxed);                                                     \
        } else {                                                                           \
            handles[i] = (type_name) nullptr;                                              \
        }                                                                                  \
        ,                                                                                  \
        if (handles[i]) {                                                                  \
            auto boxed = handles[i];                                                       \
            handle_u64s[i] = (uint64_t)unbox_##type_name(handles[i]);                      \
            delete_##type_name(boxed);                                                     \
        } else {                                                                           \
            handle_u64s[i] = 0;                                                            \
        },                                                                                 \
        if (handle_u64s[i]) {                                                              \
            auto boxed = (type_name)(uintptr_t)handle_u64s[i];                             \
            handles[i] = unbox_##type_name((type_name)(uintptr_t)handle_u64s[i]);          \
            delete_##type_name(boxed);                                                     \
        } else {                                                                           \
            handles[i] = (type_name) nullptr;                                              \
        })

GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(BOXED_DISPATCHABLE_CREATE_IMPL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(BOXED_NON_DISPATCHABLE_CREATE_IMPL)

}  // namespace vk
}  // namespace gfxstream
