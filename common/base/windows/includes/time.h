// Copyright 2023 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.


#ifndef _AEMU_TIME_H_
#define _AEMU_TIME_H_

#include_next <time.h>

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
