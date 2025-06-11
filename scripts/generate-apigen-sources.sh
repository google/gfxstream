# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -x

bazel build codegen/generic-apigen:gfxstream_generic_apigen

# The encoders use the prefix GL while the decoders use the prefix GLES
cp -f codegen/gles1/gles1.attrib  codegen/gles1/gl.attrib
cp -f codegen/gles1/gles1.in      codegen/gles1/gl.in
cp -f codegen/gles1/gles1.types   codegen/gles1/gl.types
./bazel-bin/codegen/generic-apigen/gfxstream_generic_apigen -i ./codegen/gles1 -D ./host/gl/gles1_dec -B gles1
./bazel-bin/codegen/generic-apigen/gfxstream_generic_apigen -i ./codegen/gles1 -E ./guest/GLESv1_enc -B gl
rm codegen/gles1/gl.attrib
rm codegen/gles1/gl.in
rm codegen/gles1/gl.types

cp -f codegen/gles2/gles2.attrib  codegen/gles2/gl2.attrib
cp -f codegen/gles2/gles2.in      codegen/gles2/gl2.in
cp -f codegen/gles2/gles2.types   codegen/gles2/gl2.types
./bazel-bin/codegen/generic-apigen/gfxstream_generic_apigen -i ./codegen/gles2 -D ./host/gl/gles2_dec -B gles2
./bazel-bin/codegen/generic-apigen/gfxstream_generic_apigen -i ./codegen/gles2 -E ./guest/GLESv2_enc -B gl2
rm codegen/gles2/gl2.attrib
rm codegen/gles2/gl2.in
rm codegen/gles2/gl2.types

./bazel-bin/codegen/generic-apigen/gfxstream_generic_apigen -i ./codegen/renderControl -D ./host/renderControl_dec -B renderControl
./bazel-bin/codegen/generic-apigen/gfxstream_generic_apigen -i ./codegen/renderControl -E ./guest/renderControl_enc -B renderControl
