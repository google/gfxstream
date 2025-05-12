
if("$ENV{QNX_HOST}" STREQUAL "")
    message(FATAL_ERROR "QNX_HOST environment variable not found. Please set path to your QNX SDP installation")
endif()

if("$ENV{QNX_TARGET}" STREQUAL "")
    message(FATAL_ERROR "QNX_TARGET environment variable not found. Please set QNX target path")
endif()

if("$ENV{PROCESSOR}" STREQUAL "")
    message(FATAL_ERROR "PROCESSOR variable not found. This variable should be set in the Makefile at CPU level (ie: nto/aarch64/Makefile)")
endif()

if("$ENV{VARIANT}" STREQUAL "")
    message(FATAL_ERROR "VARIANT variable not found. This variable should be set in the Makefile at VARIANT level (ie: nto/aarch64/le/Makefile)")
endif()

message(STATUS "using QNX_HOST $ENV{QNX_HOST}")
message(STATUS "using QNX_TARGET $ENV{QNX_TARGET}")
message(STATUS "cpu=$ENV{PROCESSOR}")
message(STATUS "variant=$ENV{VARIANT}")

set(QNX true)
set(CMAKE_SYSTEM_NAME QNX)
add_definitions("-D_QNX_SOURCE")

set(GENERATE_PINFO_FILES $ENV{GENERATE_PINFO_FILES})

set(target gcc_nto$ENV{VARIANT})
set(ntoarch $ENV{VARIANT})
set(processor $ENV{PROCESSOR})
set(CMAKE_C_COMPILER qcc)
set(CMAKE_C_COMPILER_TARGET ${target})
set(CMAKE_CXX_COMPILER qcc -lang-c++)
set(CMAKE_CXX_COMPILER_TARGET ${target})
set(CMAKE_ASM_COMPILER qcc -V${target})
set(CMAKE_RANLIB $ENV{QNX_HOST}/usr/bin/nto${processor}-ranlib CACHE PATH "QNX ranlib Program" FORCE)
set(CMAKE_AR $ENV{QNX_HOST}/usr/bin/nto${processor}-ar CACHE PATH "QNX ar Program" FORCE)
set(CMAKE_SYSROOT $ENV{QNX_TARGET})
set(CMAKE_CXX_STANDARD 20)
