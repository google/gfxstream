// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "aemu/base/memory/NoDestructor.h"
#include <string>
#include <utility>
#include <gtest/gtest.h>
#include "aemu/base/Log.h"

namespace android::base {
namespace {
struct CheckOnDestroy {
  ~CheckOnDestroy() { dfatal("Destructor was called"); }
};
TEST(NoDestructorTest, SkipsDestructors) {
  NoDestructor<CheckOnDestroy> destructor_should_not_run;
}
struct CopyOnly {
  CopyOnly() = default;
  CopyOnly(const CopyOnly&) = default;
  CopyOnly& operator=(const CopyOnly&) = default;
  CopyOnly(CopyOnly&&) = delete;
  CopyOnly& operator=(CopyOnly&&) = delete;
};
struct MoveOnly {
  MoveOnly() = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
};
struct ForwardingTestStruct {
  ForwardingTestStruct(const CopyOnly&, MoveOnly&&) {}
};
TEST(NoDestructorTest, ForwardsArguments) {
  CopyOnly copy_only;
  MoveOnly move_only;
  static NoDestructor<ForwardingTestStruct> test_forwarding(
      copy_only, std::move(move_only));
}
TEST(NoDestructorTest, Accessors) {
  static NoDestructor<std::string> awesome("awesome");
  EXPECT_EQ("awesome", *awesome);
  EXPECT_EQ(0, awesome->compare("awesome"));
  EXPECT_EQ(0, awesome.get()->compare("awesome"));
}
}  // namespace
}  // namespace base