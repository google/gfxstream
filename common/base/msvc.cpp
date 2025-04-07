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

#include <io.h>

#include "aemu/base/msvc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysinfoapi.h>

#define FILETIME_1970 116444736000000000ull
#define HECTONANOSEC_PER_SEC 10000000ull


int mkstemp(char* t) {
    // TODO(joshuaduong): Support unicode (b/117322783)
    int len = strlen(t) + 1;
    errno_t err = _mktemp_s(t, len);

    if (err != 0) {
        return -1;
    }

    return _sopen(t, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYRW,
                  _S_IREAD | _S_IWRITE);
}

// From https://msdn.microsoft.com/en-us/library/28d5ce15.aspx
int asprintf(char** buf, const char* format, ...) {
    va_list args;
    int len;

    if (buf == NULL) {
        return -1;
    }

    // retrieve the variable arguments
    va_start(args, format);

    len = _vscprintf(format, args)  // _vscprintf doesn't count
          + 1;                      // terminating '\0'

    if (len <= 0) {
        return len;
    }

    *buf = (char*)malloc(len * sizeof(char));

    vsprintf(*buf, format, args);  // C4996
    // Note: vsprintf is deprecated; consider using vsprintf_s instead
    return len;
}

// From https://msdn.microsoft.com/en-us/library/28d5ce15.aspx
static int vasprintf(char** buf, const char* format, va_list args) {
    int len;

    if (buf == NULL) {
        return -1;
    }

    len = _vscprintf(format, args)  // _vscprintf doesn't count
          + 1;                      // terminating '\0'

    if (len <= 0) {
        return len;
    }

    *buf = (char*)malloc(len * sizeof(char));

    vsprintf(*buf, format, args);  // C4996
    // Note: vsprintf is deprecated; consider using vsprintf_s instead
    return len;
}

// This is a poor resolution timer, but at least it
// is available on Win7 and older. System.cpp will install
// a better one.
static SystemTime getSystemTime = (SystemTime)GetSystemTimeAsFileTime;

int getntptimeofday(struct timespec*, struct timezone*);

int getntptimeofday(struct timespec* tp, struct timezone* z) {
    int res = 0;
    union {
        unsigned long long ns100; /*time since 1 Jan 1601 in 100ns units */
        FILETIME ft;
    } _now;
    TIME_ZONE_INFORMATION TimeZoneInformation;
    DWORD tzi;

    if (z != NULL) {
        if ((tzi = GetTimeZoneInformation(&TimeZoneInformation)) !=
            TIME_ZONE_ID_INVALID) {
            z->tz_minuteswest = TimeZoneInformation.Bias;
            if (tzi == TIME_ZONE_ID_DAYLIGHT)
                z->tz_dsttime = 1;
            else
                z->tz_dsttime = 0;
        } else {
            z->tz_minuteswest = 0;
            z->tz_dsttime = 0;
        }
    }

    if (tp != NULL) {
        getSystemTime((FileTime*)&_now.ft); /* 100-nanoseconds since 1-1-1601 */
        /* The actual accuracy on XP seems to be 125,000 nanoseconds = 125
         * microseconds = 0.125 milliseconds */
        _now.ns100 -= FILETIME_1970; /* 100 nano-seconds since 1-1-1970 */
        tp->tv_sec =
                _now.ns100 / HECTONANOSEC_PER_SEC; /* seconds since 1-1-1970 */
        tp->tv_nsec = (long)(_now.ns100 % HECTONANOSEC_PER_SEC) *
                      100; /* nanoseconds */
    }
    return res;
}

int gettimeofday(struct timeval* p, struct timezone* z) {
    struct timespec tp;

    if (getntptimeofday(&tp, z))
        return -1;
    p->tv_sec = tp.tv_sec;
    p->tv_usec = (tp.tv_nsec / 1000);
    return 0;
}
