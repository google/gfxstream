/*
 * Copyright (C) 2024 The Android Open Source Project
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
#ifndef _PROCESS_RESOURCES_H
#define _PROCESS_RESOURCES_H

#include <atomic>
#include <memory>
#include "gfxstream/Compiler.h"

namespace gfxstream {

class ProcessResources {
   public:
    // We only allow ProcessResources to be created on the heap, because the pointer to
    // mSequenceNumber shouldn't change until ProcessResources is destroyed.
    static std::unique_ptr<ProcessResources> create() {
        return std::unique_ptr<ProcessResources>(new ProcessResources());
    }
    DISALLOW_COPY_ASSIGN_AND_MOVE(ProcessResources);

    ~ProcessResources() = default;
    std::atomic<uint32_t>* getSequenceNumberPtr() const { return &mSequenceNumber; }

   private:
    ProcessResources() : mSequenceNumber(0) {}
    mutable std::atomic<uint32_t> mSequenceNumber;
};

}  // namespace gfxstream
#endif
