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
#include "aemu/base/files/Stream.h"


#include <stdio.h>

namespace android {
namespace base {

// An implementation of android::base::Stream interface on top of an
// stdio FILE* instance.
class StdioStream : public Stream {
public:
    enum Ownership { kNotOwner, kOwner };

    StdioStream(FILE* file = nullptr, Ownership ownership = kNotOwner);
    StdioStream(StdioStream&& other);
    StdioStream& operator=(StdioStream&& other);

    virtual ~StdioStream();
    virtual ssize_t read(void* buffer, size_t size) override;
    virtual ssize_t write(const void* buffer, size_t size) override;

    FILE* get() const { return mFile; }
    void close();

private:

    DISALLOW_COPY_AND_ASSIGN(StdioStream);

    FILE* mFile;
    Ownership mOwnership;
};

}  // namespace base
}  // namespace android
