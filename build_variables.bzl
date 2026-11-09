"""
Common build configuration definitions.
"""

GFXSTREAM_COMMON_COPTS = [
    "-Wall",
    "-Wextra",
    "-Wformat",
    "-Wshadow",
    "-Wunused-result",
    "-Wno-missing-field-initializers",
    "-Wno-return-type-c-linkage",
    "-Wno-unused-function",
    "-Wno-unused-parameter",
    "-Wno-unused-private-field",
    "-Wno-unused-variable",
    "-Wno-thread-safety-analysis",
    "-Wno-thread-safety-attributes",
    "-Wno-c++98-compat-pedantic",
    "-Wno-old-style-cast",
    "-Wno-pre-c++17-compat",
    "-Wno-pre-c++20-compat-pedantic",
    "-Wno-unsafe-buffer-usage",
] + select({
    "@platforms//os:android": [
        # In gfxstream, ANDROID means an AOSP platform build. NDK builds still
        # get __ANDROID__.
        "-UANDROID",
    ],
    "@platforms//os:linux": [
        "-Werror",
    ],
    "//conditions:default": [],
})

GFXSTREAM_HOST_COPTS = GFXSTREAM_COMMON_COPTS + [
] + select({
    "@platforms//os:windows": [
        "/EHs-c-",
    ],
    "//conditions:default": [
        "-fno-exceptions",
    ],
})

GFXSTREAM_HOST_VK_DEFINES = [
    "VK_GFXSTREAM_STRUCTURE_TYPE_EXT",
    "VK_GOOGLE_gfxstream",
] + select({
    "@platforms//os:android": [
        "VK_USE_PLATFORM_ANDROID_KHR",
    ],
    "@platforms//os:macos": [
        "VK_USE_PLATFORM_METAL_EXT",
        "VK_USE_PLATFORM_MACOS_MVK",
    ],
    "@platforms//os:windows": [
        "VK_USE_PLATFORM_WIN32_KHR",
    ],
    "@platforms//os:linux": [
        "VK_USE_PLATFORM_XCB_KHR",
    ],
    "//conditions:default": [],
})

GFXSTREAM_HOST_DEFINES = GFXSTREAM_HOST_VK_DEFINES + [
    "BUILDING_EMUGL_COMMON_SHARED",
    "EMUGL_BUILD",
    "GFXSTREAM_ENABLE_HOST_GLES=1",
    "GFXSTREAM_BUILD_WITH_SNAPSHOT_SUPPORT=1",
    "VK_BASE_VERSION_1_0",
    "VK_BASE_VERSION_1_1",
    "VK_BASE_VERSION_1_2",
    "VK_BASE_VERSION_1_3",
    "VK_BASE_VERSION_1_4",
    "VK_COMPUTE_VERSION_1_0",
    "VK_COMPUTE_VERSION_1_1",
    "VK_COMPUTE_VERSION_1_2",
    "VK_COMPUTE_VERSION_1_3",
    "VK_COMPUTE_VERSION_1_4",
    "VK_GRAPHICS_VERSION_1_0",
    "VK_GRAPHICS_VERSION_1_1",
    "VK_GRAPHICS_VERSION_1_2",
    "VK_GRAPHICS_VERSION_1_3",
    "VK_GRAPHICS_VERSION_1_4",
] + select({
    "@platforms//os:android": [
        # No GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT: the snapshot
        # frontend needs the backend protos, which Android builds do not link.
        "GFXSTREAM_UNSTABLE_VULKAN_BLOB_COLOR_BUFFER=1",
        "GFXSTREAM_UNSTABLE_VULKAN_EXTERNAL_SYNC=1",
    ],
    "@platforms//os:windows": [
        "GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT=1",
        "WIN32_LEAN_AND_MEAN",
    ],
    "//conditions:default": [
        "GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT=1",
    ],
})

GFXSTREAM_HOST_LINKOPTS = select({
    "@platforms//os:android": [
        "-llog",
        "-lnativewindow",
    ],
    "@platforms//os:linux": [
        "-Wl,--warn-backrefs",
        "-Wl,--warn-backrefs-exclude=*llvm*",
        "-Wl,--fatal-warnings",
    ],
    "//conditions:default": [],
})
