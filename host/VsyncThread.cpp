// Copyright 2021 The Android Open Source Project
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
#include "VsyncThread.h"

#include "gfxstream/system/System.h"

namespace gfxstream {

VsyncThread::VsyncThread(uint64_t vsyncPeriodNs) :
    mPeriodNs(vsyncPeriodNs),
    mThread([this] { threadFunc(); }) {
    mThread.start();
}

VsyncThread::~VsyncThread() {
    exit();
}

void VsyncThread::schedule(VsyncTask task) {
    mChannel.send({ CommandType::Default, task });
}

void VsyncThread::setPeriod(uint64_t newPeriod) {
    mChannel.send({ CommandType::ChangePeriod, {}, newPeriod });
}

void VsyncThread::exit() {
    mChannel.send({ CommandType::Exit });
    mThread.wait();
}

void VsyncThread::threadFunc() {
    VsyncThreadCommand currentCommand;
    uint64_t lastTimeUs = ~0ULL;
    uint64_t phasedWaitTimeUs;
    uint64_t currentUs;

    while (true) {
        uint64_t periodUs = mPeriodNs / 1000ULL;
        currentUs = gfxstream::base::getHighResTimeUs();

        if (lastTimeUs == ~0ULL) {
            phasedWaitTimeUs = currentUs + periodUs;
        } else {
            phasedWaitTimeUs =
                periodUs * ((currentUs - lastTimeUs) / periodUs + 1) +
                lastTimeUs;
        }

        gfxstream::base::sleepToUs(phasedWaitTimeUs);

        lastTimeUs = phasedWaitTimeUs;

        while (mChannel.tryReceive(&currentCommand)) {
            switch (currentCommand.type) {
                case CommandType::Exit:
                    return;
                case CommandType::ChangePeriod:
                    mPeriodNs = currentCommand.newPeriod;
                    break;
                case CommandType::Default:
                default:
                    currentCommand.task(mCount);
            }
        }

        ++mCount;
    }
}

}  // namespace gfxstream