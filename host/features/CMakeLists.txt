if (NOT TARGET gfxstream_features)
    add_library(
        gfxstream_features.headers
        INTERFACE)
    target_include_directories(
        gfxstream_features.headers
        INTERFACE
        include)

    add_library(
        gfxstream_features
        Features.cpp)
    target_link_libraries(
        gfxstream_features
        PUBLIC
        gfxstream_features.headers)
endif()