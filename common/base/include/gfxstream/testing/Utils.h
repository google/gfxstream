// Copyright 2016 The Android Open Source Project
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

#include "aemu/base/memory/ScopedPtr.h"
#include "aemu/base/system/System.h"

#include <gtest/gtest.h>

#include <locale.h>

#define SKIP_TEST_ON_WINE()                        \
    do {                                           \
        if (System::get()->isRunningUnderWine()) { \
            return;                                \
        }                                          \
    } while (0)

inline auto setScopedCommaLocale() ->
    android::base::ScopedCustomPtr<char, void(*)(const char*)> {
#ifdef _WIN32
    static constexpr char commaLocaleName[] = "French";
#else
    static constexpr char commaLocaleName[] = "fr_FR.UTF-8";
#endif

    // Set up a locale with comma as a decimal mark.
    auto oldLocale = setlocale(LC_ALL, nullptr);
    EXPECT_NE(nullptr, oldLocale);
    if (!oldLocale) {
        return {};
    }
    // setlocale() will overwrite the |oldLocale| pointer's data, so copy it.
    auto oldLocaleCopy = android::base::ScopedCPtr<char>(strdup(oldLocale));
    auto newLocale = setlocale(LC_ALL, commaLocaleName);
    EXPECT_NE(nullptr, newLocale);
    if (!newLocale) {
        return {};
    }

    EXPECT_STREQ(",", localeconv()->decimal_point);

    // Restore the locale after the test.
    return android::base::makeCustomScopedPtr(
            oldLocaleCopy.release(), [](const char* name) {
                auto nameDeleter = android::base::ScopedCPtr<const char>(name);
                EXPECT_NE(nullptr, setlocale(LC_ALL, name));
            });
}
