// Copyright 2021 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#ifndef _AEMU_BITS_SOCKET_H_
#define _AEMU_BITS_SOCKET_H_

#ifndef __linux__
#ifndef __QNX__
// Make sure these are defined and don't change anything if used.
enum {
    SOCK_CLOEXEC = 0,
#ifndef __APPLE__
    O_CLOEXEC = 0
#endif
};
#endif  // !__QNX__
#endif  // !__linux__

#endif	/* Not _AEMU_BITS_SOCKET_H_ */