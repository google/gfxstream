# Gfxstream

Graphics Streaming Kit, colloquially known as Gfxstream and previously known as
Vulkan Cereal, is a collection of code generators and libraries for streaming
rendering APIs from one place to another.

-   From a virtual machine guest to host for virtualized graphics
-   From one process to another for IPC graphics
-   From one computer to another via network sockets

## Building

### Linux

#### Bazel

The Bazel build current supports building the host server which is typically
used for Android virtual device host tooling.

```
cd <gfxstream project>

bazel build ...

bazel test ...
```

#### CMake

The CMake build has historically been used for building the host backend for
Goldfish.

The CMake build can be used from either a standalone Gfxstream checkout or from
inside an Android repo.

Then,

```
mkdir build

cd build

cmake .. -G Ninja

ninja
```

For validating a Goldfish build,

```
cd <aosp/emu-main-dev repo>

cd external/qemu

python android/build/python/cmake.py --gfxstream
```

#### Meson

The Meson build has historically been used for building the backend for Linux
guest on Linux host use cases.

```
cd <gfxstream project>

meson setup \
    -Ddefault_library=static \
    -Dgfxstream-build=host \
    build

meson compile -C build
```

#### Soong

The Android Soong build is used for building the guest components for virtual
device (Cuttlefish, Goldfish, etc) images and was previously used for building
the host backend for virtual device host tools.

Please follow the instructions
[here](https://source.android.com/docs/setup/start) for getting started with
Android development and setting up a repo.

Then,

```
m libgfxstream_backend
```

and `libgfxstream_backend.so` can be found in `out/host`.

For validating changes, consider running

```
cd hardware/google/gfxstream

mma
```

to build everything inside of the Gfxstream directory.

### Windows

Make sure the latest CMake is installed. Make sure Visual Studio 2019 is
installed on your system along with all the Clang C++ toolchain components.
Then:

```
mkdir build

cd build

cmake . ../ -A x64 -T ClangCL
```

A solution file should be generated. Then open the solution file in Visual
studio and build the `gfxstream_backend` target.

## Codegen

### Regenerating GLES/RenderControl code

First, build `build/gfxstream-generic-apigen`. Then run:

```
scripts/generate-apigen-source.sh
```

### Regenerating Vulkan code

To re-generate both guest and Vulkan code, please run:

```
scripts/generate-gfxstream-vulkan.sh
```

## Testing

## Android Host Tests

There are Android mock tests available, runnable on Linux. To build these tests,
run:

```
m GfxstreamEnd2EndTests
```

## Windows Tests

There are a bunch of test executables generated. They require `libEGL.dll` and
`libGLESv2.dll` and `vulkan-1.dll` to be available, possibly from your GPU
vendor or ANGLE, in the `%PATH%`.

## Features

### Tracing

The host renderer has optional support for Perfetto tracing which can be enabled
by defining `GFXSTREAM_BUILD_WITH_TRACING` (enabled by default on Android
builds).

The `perfetto` and `traced` tools from Perfetto should be installed. Please see
the [Perfetto Quickstart](https://perfetto.dev/docs/quickstart/linux-tracing) or
follow these short form instructions:

```
cd <your Android repo>/external/perfetto

./tools/install-build-deps

./tools/gn gen --args='is_debug=false' out/linux

./tools/ninja -C out/linux traced perfetto
```

To capture a trace on Linux, start the Perfetto daemon:

```
./out/linux/traced
```

Then, run Gfxstream with
[Cuttlefish](https://source.android.com/docs/devices/cuttlefish):

```
cvd start --gpu_mode=gfxstream_guest_angle_host_swiftshader
```

Next, start a trace capture with:

```
./out/linux/perfetto --txt -c gfxstream_trace.cfg -o gfxstream_trace.perfetto
```

with `gfxstream_trace.cfg` containing the following or similar:

```
buffers {
  size_kb: 4096
}
data_sources {
  config {
    name: "track_event"
    track_event_config {
    }
  }
}
```

Next, end the trace capture with Ctrl + C.

Finally, open https://ui.perfetto.dev/ in your webbrowser and use "Open trace
file" to view the trace.

## Design Notes

### Guest Vulkan

gfxstream vulkan is the most actively developed component. Some key commponents
of the current design include:

*   1:1 threading model - each guest Vulkan encoder thread gets host side
    decoding thread
*   Support for both virtio-gpu, goldish and testing transports.
*   Support for Android, Fuchsia, and Linux guests.
*   Ring Buffer to stream commands, in the style of io_uring.
*   Mesa embedded to provide
    [dispatch](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/vulkan/dispatch.rst)
    and
    [objects](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/vulkan/base-objs.rst).
*   Currently, there are a set of Mesa objects and gfxstream objects. For
    example, `struct gfxstream_vk_device` and the gfxstream object
    `goldfish_device` both are internal representations of Vulkan opaque handle
    `VkDevice`. The Mesa object is used first, since Mesa provides dispatch. The
    Mesa object contains a key to the hash table to get a gfxstream internal
    object (for example, `gfxstream_vk_device::internal_object`). Eventually,
    gfxstream objects will be phased out and Mesa objects used exclusively.

## Future Project Ideas

Gfxstream is a first class open source project, and welcomes new contributors.
There are many interesting projects available, for new and experienced software
enthusiasts. Some ideas include:

1.  New OS support (Windows, Haiku, MacOS) support for gfxstream guest
2.  Rewriting the gfxstream protocol using python templates and working with
    other FOSS projects to de-duplicate
3.  Guided performance optimizations
4.  KVM or hypervisor integration to close gap between HW GPU virtualization
5.  Improving rutabaga integrations
6.  Improving display virtualization

Please reach out to your local gfxstreamist today if you are interested!

## Notice

This is not an officially supported Google product. This project is not eligible
for the
[Google Open Source Software Vulnerability Rewards Program](https://bughunters.google.com/open-source-security).
