// Copyright 2014 The Android Open Source Project
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

// Define HAVE_MALLOC_USABLE_SIZE to 1 to indicate that the current
// system has malloc_usable_size(). Which takes the address of a malloc-ed
// pointer, and return the size of the underlying storage block.
// This is useful to optimize heap memory usage.

// Including at least one C library header is required to define symbols
// like __GLIBC__. Choose carefully because some headers like <stddef.h>
// are actually provided by the compiler, not the C library and do not
// define the macros we need.
#include <stdint.h>

#if defined(__GLIBC__)
#  include <malloc.h>
#  define USE_MALLOC_USABLE_SIZE  1
#elif defined(__APPLE__) || defined(__FreeBSD__)
#  include <malloc/malloc.h>
#  define malloc_usable_size  malloc_size
#  define USE_MALLOC_USABLE_SIZE  1
#else
#  define USE_MALLOC_USABLE_SIZE  0
#endif
