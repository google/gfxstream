/*
 * Copyright (C) 2021 The Android Open Source Project
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

#pragma once

void encoderLog(const char* format, ...);

// Uncomment to log function calls with arguments:
// #define ENABLE_ENCODER_DEBUG_LOGGING_FOR_ALL_APPS 1
// #define ENABLE_ENCODER_DEBUG_LOGGING_FOR_APP "com.android.systemui"

#if defined(ENABLE_ENCODER_DEBUG_LOGGING_FOR_ALL_APPS) || \
    defined(ENABLE_ENCODER_DEBUG_LOGGING_FOR_APP)
#define ENCODER_DEBUG_LOG(...) encoderLog(__VA_ARGS__)
#else
#define ENCODER_DEBUG_LOG(...) ((void)0)
#endif
