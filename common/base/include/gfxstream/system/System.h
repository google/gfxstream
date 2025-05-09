// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gfxstream/CpuTime.h"

#include <string>

namespace gfxstream {
namespace base {

std::string getEnvironmentVariable(const std::string& key);
void setEnvironmentVariable(const std::string& key, const std::string& value);

uint64_t getUnixTimeUs();
uint64_t getHighResTimeUs();

uint64_t getUptimeMs();

std::string getProgramDirectory();
std::string getLauncherDirectory();

bool getFileSize(int fd, uint64_t* size);

void sleepMs(uint64_t ms);
void sleepUs(uint64_t us);
// Sleep to the specified time in microseconds from getHighResTimeUs().
void sleepToUs(uint64_t us);

CpuTime cpuTime();

bool queryFileVersionInfo(const char* filename, int* major, int* minor, int* build1, int* build2);

int getCpuCoreCount();

} // namespace base
} // namespace gfxstream
