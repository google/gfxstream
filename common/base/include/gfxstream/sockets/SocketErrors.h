// Copyright 2014 The Android Open Source Project
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

#pragma once

// Older Mingw32 headers didn't define all error macros appropriately.
// Include this header to work-around this.
#include <errno.h>

#ifdef _WIN32
#  include "aemu/base/sockets/Winsock.h"

#  ifndef EINTR
#    define EINTR        10004
#  endif
#  ifndef EAGAIN
#    define EAGAIN       10035
#  endif
#  ifndef EWOULDBLOCK
#    define EWOULDBLOCK  EAGAIN
#  endif
#  ifndef EINPROGRESS
#    define EINPROGRESS  10036
#  endif
#  ifndef EALREADY
#    define EALREADY     10037
#  endif
#  ifndef EDESTADDRREQ
#    define EDESTADDRREQ 10039
#  endif
#  ifndef EMSGSIZE
#    define EMSGSIZE     10040
#  endif
#  ifndef EPROTOTYPE
#    define EPROTOTYPE   10041
#  endif
#  ifndef ENOPROTOOPT
#    define ENOPROTOOPT  10042
#  endif
#  ifndef EAFNOSUPPORT
#    define EAFNOSUPPORT 10047
#  endif
#  ifndef EADDRINUSE
#    define EADDRINUSE   10048
#  endif
#  ifndef EADDRNOTAVAIL
#    define EADDRNOTAVAIL 10049
#  endif
#  ifndef ENETDOWN
#    define ENETDOWN     10050
#  endif
#  ifndef ENETUNREACH
#    define ENETUNREACH  10051
#  endif
#  ifndef ENETRESET
#    define ENETRESET    10052
#  endif
#  ifndef ECONNABORTED
#    define ECONNABORTED 10053
#  endif
#  ifndef ECONNRESET
#    define ECONNRESET   10054
#  endif
#  ifndef ENOBUFS
#    define ENOBUFS      10055
#  endif
#  ifndef EISCONN
#    define EISCONN      10056
#  endif
#  ifndef ENOTCONN
#    define ENOTCONN     10057
#  endif
#  ifndef ESHUTDOWN
#    define ESHUTDOWN     10058
#  endif
#  ifndef ETOOMANYREFS
#    define ETOOMANYREFS  10059
#  endif
#  ifndef ETIMEDOUT
#    define ETIMEDOUT     10060
#  endif
#  ifndef ECONNREFUSED
#    define ECONNREFUSED  10061
#  endif
#  ifndef ELOOP
#    define ELOOP         10062
#  endif
#  ifndef EHOSTDOWN
#    define EHOSTDOWN     10064
#  endif
#  ifndef EHOSTUNREACH
#    define EHOSTUNREACH  10065
#  endif
#endif /* _WIN32 */
