# Build: QNX

Requires QNX SDP to build, with `QNX_TARGET` and `QNX_HOST` environment variables set. Make sure the latest meson is installed.

Build scripts are made available to show how to cross-compile gfxstream and its aemu dependencies. See `./qnx/nto/build-aemu.sh` and `./qnx/nto/build-gfxstream.sh`

    cd ./qnx
    make

Make will navigate to `./qnx/nto/aarch64le` and run the build scripts from there.

The aemu dependency is downloaded to, and built in `./qnx/nto/aarch64le/aemu`.

Gfxstream's build directory is `./qnx/nto/aarch64le/gfxstream`.

    cd ./qnx
    make install

The install target requires the `INSTALL_ROOT_nto` environment variable, which sets the qnx staging/install path.

pinfo.mk writes build info into the binary:

    # use -i ./nto/aarch64le/gfxstream/host/libgfxstream_backend.so
    QNX_BUILDID=(GNU)5dc1327b962ed2a7e992c1f6f35c0df3
    NAME=libgfxstream_backend.so
    DESCRIPTION=virtio-gpu backend renderer
    DATE=2023/09/18-10:58:49-EDT
    COMMIT=7b8c19e51845aee014684c43e6aa8409e919af6a

A "recursive make" directory layout is used to allow multiplatform builds. For more info, see: [Conventions for Recursive Makefiles and Directories](https://www.qnx.com/developers/docs/7.1/#com.qnx.doc.neutrino.prog/topic/make_convent.html)
