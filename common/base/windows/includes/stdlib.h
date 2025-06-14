
// Copyright (C) 2023 The Android Open Source Project
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

#include_next <stdlib.h>

#ifndef _AEMU_STDLIB_H_
#define _AEMU_STDLIB_H_

#include "compat_compiler.h"
ANDROID_BEGIN_HEADER

int mkstemp(char *tmpl);

ANDROID_END_HEADER
#endif	/* Not _AEMU_STDLIB_H_ */
