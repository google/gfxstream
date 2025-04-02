add_library(gfxstream-snapshot.headers INTERFACE)
target_include_directories(gfxstream-snapshot.headers INTERFACE include)

add_library(
    ${SNAPSHOT_LIB_NAME}
    TextureLoader.cpp
    TextureSaver.cpp)

if (BUILD_SHARED_LIBS)
    set_target_properties(
        ${SNAPSHOT_LIB_NAME}
        PROPERTIES
        VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
        SOVERSION ${VERSION_MAJOR})
endif()

target_link_libraries(
    ${SNAPSHOT_LIB_NAME}
    PRIVATE
    aemu-base.headers
    aemu-host-common.headers)
target_include_directories(
    ${SNAPSHOT_LIB_NAME}
    PUBLIC
    ${AEMU_COMMON_REPO_ROOT}/include)
