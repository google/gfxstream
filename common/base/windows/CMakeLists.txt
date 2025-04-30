if(TARGET gfxstream_common_base_windows_compat)
  return()
endif()

if(INCLUDE_ANDROID_CMAKE)
  # This is a posix wrapper for windows-msvc build.
  android_nasm_compile(TARGET
                       setjmp_asm_li
                       LICENSE
                       Apache-2.0
                       SRC
                       src/setjmp.asm)

  android_add_library(TARGET
                      gfxstream_common_base_windows_compat
                      LICENSE
                      Apache-2.0
                      SRC
                      "")
  target_link_libraries(gfxstream_common_base_windows_compat
                        PRIVATE
                        setjmp_asm_lib)
else()
  add_library(gfxstream_common_base_windows_compat)
endif()

target_sources(
    gfxstream_common_base_windows_compat
    PRIVATE
    src/asprintf.c
    src/files.cpp
    src/getopt.c
    src/msvc-posix.c
    src/pread.cpp
    src/time.cpp)

# Msvc redefines macro's to inject compatibility.
target_compile_options(
    gfxstream_common_base_windows_compat
    PUBLIC
    "-Wno-macro-redefined"
    "-Wno-deprecated-declarations" # A lot of the POSIX names are deprecated...
    )

target_include_directories(
    gfxstream_common_base_windows_compat
    PUBLIC
    includes)
