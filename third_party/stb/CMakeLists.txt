if (NOT TARGET gfxstream_stb)
    add_library(gfxstream_stb
                src/stb_image.cpp
                src/stb_image_write.cpp)

    target_include_directories(gfxstream_stb
                               PUBLIC
                               include)
endif()
