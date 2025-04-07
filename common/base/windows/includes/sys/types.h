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

#ifndef _AEMU_SYS_TYPES_H_
#define _AEMU_SYS_TYPES_H_


#include_next <sys/types.h>
#include <inttypes.h>
#include <stddef.h>
#include <BaseTsd.h>

typedef unsigned int pid_t;

#ifndef ssize_t
typedef SSIZE_T ssize_t;
#endif
#endif	/* Not _AEMU_SYS_TYPES_H_ */