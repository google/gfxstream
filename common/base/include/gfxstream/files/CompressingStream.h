// Copyright 2020 The Android Open Source Project
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

#include "aemu/base/Compiler.h"
#include "aemu/base/containers/SmallVector.h"
#include "aemu/base/files/Stream.h"

namespace android {
namespace base {

class CompressingStream : public Stream {
    DISALLOW_COPY_AND_ASSIGN(CompressingStream);

public:
    CompressingStream(Stream& output);
    ~CompressingStream();

    ssize_t read(void* buffer, size_t size) override;
    ssize_t write(const void* buffer, size_t size) override;

private:
    Stream& mOutput;
    void* mLzStream;
    SmallFixedVector<char, 512> mBuffer;
};

}  // namespace base
}  // namespace android
