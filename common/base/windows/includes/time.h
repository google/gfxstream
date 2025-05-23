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

#include_next <time.h>

#ifndef _AEMU_TIME_H_
#define _AEMU_TIME_H_

#ifndef _AEMU_SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif

__BEGIN_DECLS

#define 	CLOCK_MONOTONIC   1
typedef int clockid_t;

int clock_gettime(clockid_t clk_id, struct timespec *tp);
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);

__END_DECLS

#endif	/* Not _AEMU_TIME_H_ */
