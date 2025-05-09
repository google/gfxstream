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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _AEMU_SYS_TIME_H_
#define _AEMU_SYS_TIME_H_

#include <stdint.h>
#include <time.h>
#include <WinSock2.h>
struct timezone {
    int tz_minuteswest; /* of Greenwich */
    int tz_dsttime;     /* type of dst correction to apply */
};


typedef struct FileTime {
  uint32_t dwLowDateTime;
  uint32_t dwHighDateTime;
} FileTime;

typedef  void (*SystemTime)(FileTime*);


extern int gettimeofday(struct timeval* tp, struct timezone* tz);
#endif	/* Not _AEMU_SYS_TIME_H_ */