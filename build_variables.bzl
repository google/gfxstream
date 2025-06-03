
"""
Common build configuration definitions.
"""
GFXSTREAM_HOST_COPTS = [
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
    "@platforms//os:macos": [
        "VK_USE_PLATFORM_METAL_EXT",
        "VK_USE_PLATFORM_MACOS_MVK",
    ],
    "@platforms//os:windows": [
        "VK_USE_PLATFORM_WIN32_KHR",
    ],
    "//conditions:default": [],
})
GFXSTREAM_HOST_DEFINES = GFXSTREAM_HOST_VK_DEFINES + [
    "BUILDING_EMUGL_COMMON_SHARED",
    "EMUGL_BUILD",
    "GFXSTREAM_ENABLE_HOST_GLES=1",
] + select({
    "@platforms//os:windows": [
        "WIN32_LEAN_AND_MEAN",
    ],
    "//conditions:default": [],
})
