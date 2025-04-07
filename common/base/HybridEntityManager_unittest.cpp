// Copyright (C) 2024 The Android Open Source Project
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
#include "aemu/base/containers/HybridEntityManager.h"

#include <gtest/gtest.h>

namespace android {
namespace base {

static constexpr uint32_t kTestMaxIndex = 16;
using TestHCM = HybridEntityManager<kTestMaxIndex, uint64_t, int>;

TEST(HybridEntityManager, UpdateIndex) {
    TestHCM m;
    // Occupy all linear entries.
    for (uint32_t i = 0; i < kTestMaxIndex; i++) {
        m.add(i, 1);
    }
    int indices[4];
    indices[0] = m.addFixed(kTestMaxIndex, 0, 1);
    indices[1] = m.add(100, 1);
    indices[2] = m.add(2, 1);
    m.remove(indices[1]);
    m.addFixed(indices[1], 1, 1);
    // Verify it doesn't overwrite old entries.
    indices[3] = m.add(3, 1);
    EXPECT_EQ(0, *m.get_const(indices[0]));
    EXPECT_EQ(1, *m.get_const(indices[1]));
    EXPECT_EQ(2, *m.get_const(indices[2]));
    EXPECT_EQ(3, *m.get_const(indices[3]));
}

}
}