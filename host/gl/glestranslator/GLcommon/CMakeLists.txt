# Copyright 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(
  GLcommon
  rgtc.cpp
  FramebufferData.cpp
  GLBackgroundLoader.cpp
  GLDispatch.cpp
  GLESbuffer.cpp
  GLEScontext.cpp
  GLESpointer.cpp
  GLESvalidate.cpp
  GLutils.cpp
  NamedObject.cpp
  ObjectData.cpp
  ObjectNameSpace.cpp
  PaletteTexture.cpp
  RangeManip.cpp
  SaveableTexture.cpp
  ScopedGLState.cpp
  ShareGroup.cpp
  TextureData.cpp
  TextureUtils.cpp)
target_include_directories(
    GLcommon PUBLIC
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/gfxstream_host_decoder_common
    ${GFXSTREAM_REPO_ROOT}/host/gl/glestranslator/GLcommon/include)
target_link_libraries(
    GLcommon
    PUBLIC
    gfxstream_backend_headers
    gfxstream_common_base
    gfxstream_common_logging
    gfxstream_etc_headers
    gfxstream_host_backend
    gfxstream_host_compressed_textures
    gfxstream_host_library
    gfxstream_host_snapshot.headers
    gfxstream_opengl_headers
    gfxstream_openglesdispatch
    PRIVATE
    gfxstream_etc)
if (NOT MSVC)
    target_compile_options(GLcommon PRIVATE -fvisibility=hidden)
endif()
target_compile_options(GLcommon PUBLIC -Wno-inconsistent-missing-override)

if (LINUX)
    target_link_libraries(GLcommon PRIVATE "-ldl" "-Wl,-Bsymbolic")
endif()

# android_add_test(TARGET GLcommon_unittests SRC # cmake-format: sortable
#                                                Etc2_unittest.cpp)
# target_link_libraries(GLcommon_unittests PUBLIC GLcommon gmock_main)
# target_link_libraries(GLcommon_unittests PRIVATE emugl_base)
# android_target_link_libraries(GLcommon_unittests linux-x86_64
#                               PRIVATE "-ldl" "-Wl,-Bsymbolic")
# android_target_link_libraries(GLcommon_unittests windows
#                               PRIVATE "gdi32::gdi32" "-Wl,--add-stdcall-alias")
