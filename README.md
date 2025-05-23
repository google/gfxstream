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

### Linux

#### Bazel

```
bazel test ...
```

or

```
bazel test common/end2end:gfxstream_end2end_tests
```

#### Soong

From within an Android repo, run:

```
atest --host GfxstreamEnd2EndTests
```

### Windows

There are a bunch of test executables generated. They require `libEGL.dll` and
`libGLESv2.dll` and `vulkan-1.dll` to be available, possibly from your GPU
vendor or ANGLE, in the `%PATH%`.

## Notice

This is not an officially supported Google product. This project is not eligible
for the
[Google Open Source Software Vulnerability Rewards Program](https://bughunters.google.com/open-source-security).
