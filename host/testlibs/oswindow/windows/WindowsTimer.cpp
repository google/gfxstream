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

// WindowsTimer.cpp: Implementation of a high precision timer class on Windows

#include "windows/WindowsTimer.h"

WindowsTimer::WindowsTimer() : mRunning(false), mStartTime(0), mStopTime(0)
{
}

void WindowsTimer::start()
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    mFrequency = frequency.QuadPart;

    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    mStartTime = curTime.QuadPart;

    mRunning = true;
}

void WindowsTimer::stop()
{
    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    mStopTime = curTime.QuadPart;

    mRunning = false;
}

double WindowsTimer::getElapsedTime() const
{
    LONGLONG endTime;
    if (mRunning)
    {
        LARGE_INTEGER curTime;
        QueryPerformanceCounter(&curTime);
        endTime = curTime.QuadPart;
    }
    else
    {
        endTime = mStopTime;
    }

    return static_cast<double>(endTime - mStartTime) / mFrequency;
}

Timer *CreateTimer()
{
    return new WindowsTimer();
}
