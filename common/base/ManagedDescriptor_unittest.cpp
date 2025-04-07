// Copyright 2022 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "aemu/base/ManagedDescriptor.hpp"

namespace android {
namespace base {
namespace {

using ::testing::_;
using ::testing::AtLeast;

struct PlatformTraitTest {
    using DescriptorType = int;
    MOCK_METHOD(void, closeDescriptor, (DescriptorType));
};

using ManagedDescriptorForTesting = ManagedDescriptorBase<PlatformTraitTest>;

TEST(ManagedDescriptorTest, ShouldCloseAnInitializedDescriptorOutOfScope) {
    PlatformTraitTest::DescriptorType rawDescriptor = 42;
    PlatformTraitTest platformTrait;
    {
        EXPECT_CALL(platformTrait, closeDescriptor(rawDescriptor)).Times(1);
        { ManagedDescriptorForTesting descriptor(rawDescriptor, platformTrait); }
    }
    {
        EXPECT_CALL(platformTrait, closeDescriptor(rawDescriptor)).Times(1);
        {
            ManagedDescriptorForTesting descriptor(rawDescriptor, platformTrait);
            descriptor = ManagedDescriptorForTesting(platformTrait);
        }
    }
}

TEST(ManagedDescriptorTest, ShouldNotCloseForAnUninitializedDescriptorOutOfScope) {
    PlatformTraitTest platformTrait;
    EXPECT_CALL(platformTrait, closeDescriptor(_)).Times(0);
    { ManagedDescriptorForTesting descriptor(platformTrait); }
    {
        ManagedDescriptorForTesting descriptor(42, platformTrait);
        descriptor.release();
    }
}

TEST(ManagedDescriptorTest, ShouldGetTheCorrectRawDescriptor) {
    PlatformTraitTest platformTrait;
    PlatformTraitTest::DescriptorType rawDescriptor = 42;
    EXPECT_CALL(platformTrait, closeDescriptor(_)).Times(AtLeast(1));
    {
        ManagedDescriptorForTesting descriptor(rawDescriptor, platformTrait);
        EXPECT_EQ(descriptor.release(), rawDescriptor);
    }
    {
        ManagedDescriptorForTesting descriptor(rawDescriptor, platformTrait);
        EXPECT_EQ(descriptor.get(), rawDescriptor);
    }
    {
        ManagedDescriptorForTesting descriptor(platformTrait);
        EXPECT_EQ(descriptor.release(), std::nullopt);
    }
    {
        ManagedDescriptorForTesting descriptor(platformTrait);
        EXPECT_EQ(descriptor.get(), std::nullopt);
    }
}

TEST(ManagedDescriptorTest, ShouldCloseOnceIfMoved) {
    PlatformTraitTest platformTrait;
    PlatformTraitTest::DescriptorType rawDescriptor = 42;
    {
        EXPECT_CALL(platformTrait, closeDescriptor(rawDescriptor)).Times(1);
        {
            ManagedDescriptorForTesting descriptor1(rawDescriptor, platformTrait),
                descriptor2(platformTrait);
            descriptor2 = std::move(descriptor1);
        }
    }
    {
        EXPECT_CALL(platformTrait, closeDescriptor(rawDescriptor)).Times(1);
        {
            ManagedDescriptorForTesting descriptor1(rawDescriptor, platformTrait);
            ManagedDescriptorForTesting descriptor2(std::move(descriptor1));
        }
    }
    {
        EXPECT_CALL(platformTrait, closeDescriptor(rawDescriptor)).Times(1);
        {
            ManagedDescriptorForTesting descriptor(rawDescriptor, platformTrait);
            descriptor = std::move(descriptor);
        }
    }
    {
        PlatformTraitTest::DescriptorType anotherRawDescriptor = 2871;
        EXPECT_CALL(platformTrait, closeDescriptor(rawDescriptor)).Times(1);
        EXPECT_CALL(platformTrait, closeDescriptor(anotherRawDescriptor)).Times(1);
        {
            ManagedDescriptorForTesting descriptor1(rawDescriptor, platformTrait);
            ManagedDescriptorForTesting descriptor2(anotherRawDescriptor, platformTrait);
            descriptor1 = std::move(descriptor2);
        }
    }
}

}  // namespace
}  // namespace base
}  // namespace android