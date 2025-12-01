// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gfxstream/host/X11Support.h"

#include "gfxstream/shared_library.h"
#include "gfxstream/common/logging.h"

#define DEFINE_DUMMY_IMPL(rettype, funcname, args) \
    static rettype dummy_##funcname args { \
        return (rettype)0; \
    } \

LIST_XLIB_FUNCTYPES(DEFINE_DUMMY_IMPL)
LIST_GLX_FUNCTYPES(DEFINE_DUMMY_IMPL)

// This combines x11 and x11-xcb libraries
class X11FunctionGetter {
    public:
        X11FunctionGetter() :
            mX11Lib(gfxstream::host::SharedLibrary::open("libX11")) {
            if (!mX11Lib) {
                GFXSTREAM_WARNING("Could not open libX11.so, try libX11.so.6");
                mX11Lib = (gfxstream::host::SharedLibrary::open("libX11.so.6"));
                if (!mX11Lib) {
                    GFXSTREAM_ERROR("Could not open libX11.so.6, give up");
                    return;
                }
            }

#define X11_ASSIGN_DUMMY_IMPL(funcname) mApi.funcname = dummy_##funcname;

                LIST_XLIB_FUNCS(X11_ASSIGN_DUMMY_IMPL)


            mX11XCBLib = gfxstream::host::SharedLibrary::open("libX11-xcb");
            if (!mX11XCBLib) {
                GFXSTREAM_WARNING("Could not open libX11-xcb.so, try libX11-xcb.so.1");
                mX11XCBLib = (gfxstream::host::SharedLibrary::open("libX11-xcb.so.1"));
                if (!mX11XCBLib) {
                    GFXSTREAM_ERROR("Could not open libX11-xcb.so.1, give up");
                    // Do not return, only disable usage of XCB instead
                }
            }

    #define X11_GET_FUNC(funcname)                                                               \
        {                                                                                        \
            auto f = mX11Lib->findSymbol(#funcname);                                             \
            if (f) {                                                                             \
                mApi.funcname = (funcname##_t)f;                                                 \
            } else {                                                                             \
                auto fxcb = mX11XCBLib->findSymbol(#funcname);                                   \
                if (fxcb) {                                                                      \
                    mApi.funcname = (funcname##_t)fxcb;                                          \
                } else {                                                                         \
                    GFXSTREAM_WARNING("Could not find function symbol '%s'  X11", #funcname);    \
                }                                                                                \
            }                                                                                    \
        } \

                LIST_XLIB_FUNCS(X11_GET_FUNC);

            }

        X11Api* getApi() { return &mApi; }
    private:
        gfxstream::host::SharedLibrary* mX11Lib;
        gfxstream::host::SharedLibrary* mX11XCBLib;

        X11Api mApi;
};

class GlxFunctionGetter {
    public:
        // Important: Use libGL.so.1 explicitly, because it will always link to
        // the vendor-specific version of the library. libGL.so might in some
        // cases, depending on bad ldconfig configurations, link to the wrapper
        // lib that doesn't behave the same.
        GlxFunctionGetter() :
            mGlxLib(gfxstream::host::SharedLibrary::open("libGL.so.1")) {

#define GLX_ASSIGN_DUMMY_IMPL(funcname) mApi.funcname = dummy_##funcname;

                LIST_GLX_FUNCS(GLX_ASSIGN_DUMMY_IMPL)

            if (!mGlxLib) {
                GFXSTREAM_WARNING("libGL library couild be loaded!");
                return;
            }

#define GLX_GET_FUNC(funcname) \
                { \
                auto f = mGlxLib->findSymbol(#funcname);                                             \
                if (f) {                                                                             \
                    mApi.funcname = (funcname##_t)f;                                                 \
                } else {                                                                             \
                    GFXSTREAM_WARNING("Could not find function symbol '%s' in libGL", #funcname);    \
                }                                                                                    \
                } \

                LIST_GLX_FUNCS(GLX_GET_FUNC);

            }

        GlxApi* getApi() { return &mApi; }

    private:
        gfxstream::host::SharedLibrary* mGlxLib;

        GlxApi mApi;
};

AEMU_EXPORT struct X11Api* getX11Api() {
    static X11FunctionGetter* g = new X11FunctionGetter;
    return g->getApi();
}

AEMU_EXPORT struct GlxApi* getGlxApi() {
    static GlxFunctionGetter* g = new GlxFunctionGetter;
    return g->getApi();
}
