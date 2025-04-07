if(WINDOWS_MSVC_X86_64)
  android_nasm_compile(
    TARGET hello_world_asm_lib NODISTRIBUTE SRC # cmake-format: sortable
                                                yasm/hello_world.asm)
  android_add_test(
    TARGET hello_world_asm_test SRC # cmake-format: sortable
                                    yasm/hello_world_unittest.cpp)
  target_link_libraries(hello_world_asm_test PRIVATE hello_world_asm_lib
                                                     gmock_main)
endif()

add_subdirectory(compiler)
