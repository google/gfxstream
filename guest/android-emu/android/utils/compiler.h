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

#ifdef __cplusplus
#define ANDROID_BEGIN_HEADER extern "C" {
#define ANDROID_END_HEADER   }
#else
#define ANDROID_BEGIN_HEADER /* nothing */
#define ANDROID_END_HEADER  /* nothing */
#endif

// ANDROID_GCC_PREREQ(<major>,<minor>) will evaluate to true
// iff the current version of GCC is <major>.<minor> or higher.
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
# define ANDROID_GCC_PREREQ(maj, min) \
         ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define ANDROID_GCC_PREREQ(maj, min) 0
#endif
