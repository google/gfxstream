if(TARGET msvc-posix-compat)
  return()
endif()

if(INCLUDE_ANDROID_CMAKE)
  # This is a posix wrapper for windows-msvc build.
  android_nasm_compile(TARGET setjmp_asm_lib LICENSE Apache-2.0 SRC
                       src/setjmp.asm)
  android_add_library(TARGET msvc-posix-compat LICENSE Apache-2.0 SRC "")
  target_link_libraries(msvc-posix-compat PRIVATE setjmp_asm_lib)
else()
  add_library(msvc-posix-compat)
endif()

target_sources(
  msvc-posix-compat
  PRIVATE src/asprintf.c
          src/files.cpp
          src/getopt.c
          src/msvc-posix.c
          src/pread.cpp
          src/time.cpp)
target_link_libraries(msvc-posix-compat PUBLIC aemu-base.headers)

# Msvc redefines macro's to inject compatibility.
target_compile_options(
  msvc-posix-compat
  PUBLIC "-Wno-macro-redefined" "-Wno-deprecated-declarations" # A lot of the
                                                               # POSIX names are
                                                               # deprecated..
)
target_include_directories(msvc-posix-compat PUBLIC includes)

if (ANDROID_EMULATOR_BUILD)
  add_subdirectory(tests)
endif()