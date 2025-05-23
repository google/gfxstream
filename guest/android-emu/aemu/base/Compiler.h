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

// Use this inside a class declaration to ensure that the corresponding objects
// cannot be copy-constructed or assigned. For example:
//
//   class Foo {
//       ....
//       DISALLOW_COPY_AND_ASSIGN(Foo)
//       ....
//   };
//
// Note: this macro is sometimes defined in 3rd-party libs, so let's check first
#ifndef DISALLOW_COPY_AND_ASSIGN

#define DISALLOW_COPY_AND_ASSIGN(T) \
    T(const T& other) = delete; \
    T& operator=(const T& other) = delete

#endif

#ifndef DISALLOW_COPY_ASSIGN_AND_MOVE

#define DISALLOW_COPY_ASSIGN_AND_MOVE(T) \
    DISALLOW_COPY_AND_ASSIGN(T); \
    T(T&&) = delete; \
    T& operator=(T&&) = delete

#endif
