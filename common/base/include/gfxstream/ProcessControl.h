// Copyright 2018 The Android Open Source Project
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

// Library to:
// save argv,
// kill and wait a process ID,
// launch process with given cwd and args.
#pragma once
#include <string>                     // for string
#include <string_view>
#include <vector>                     // for vector

#include "aemu/base/FunctionView.h"  // for FunctionView

namespace android {
namespace base {

struct ProcessLaunchParameters {
    bool operator==(const ProcessLaunchParameters& other)  const;
    std::string str() const;

    std::string workingDirectory;
    std::string programPath;
    std::vector<std::string> argv;
    // TODO: save env variables, which is good for the case
    // where we are launching not from a child process
};
static constexpr char kLaunchParamsFileName[] = "emu-launch-params.txt";

std::vector<std::string> makeArgvStrings(int argc, const char** argv);

ProcessLaunchParameters createLaunchParametersForCurrentProcess(int argc, const char** argv);
std::string createEscapedLaunchString(int argc, const char* const* argv);
std::vector<std::string> parseEscapedLaunchString(std::string launch);

void saveLaunchParameters(const ProcessLaunchParameters& launchParams,
                          std::string_view filename);

void finalizeEmulatorRestartParameters(const char* dir);

ProcessLaunchParameters loadLaunchParameters(std::string_view filename);
void launchProcessFromParameters(const ProcessLaunchParameters& launchParams, bool useArgv0 = false);


// Restart mechanism
void disableRestart(); // Disables the restart mechanism.
bool isRestartDisabled();
void initializeEmulatorRestartParameters(int argc, char** argv, const char* paramFolder);
void setEmulatorRestartOnExit();

void registerEmulatorQuitCallback(FunctionView<void()> func);
void restartEmulator();

} // namespace base
} // namespace android
