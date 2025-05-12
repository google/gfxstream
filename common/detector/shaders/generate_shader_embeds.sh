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
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

for file in *.{frag,vert}; do
    [ -f "${file}" ] || break

    SHADER_GLSL="${file}"
    echo "Found ${SHADER_GLSL}"

    SHADER_SPV="${file}.spv"
    SHADER_EMBED="${file}.inl"
    SHADER_BASENAME="${file}"
    SHADER_EMBED_VARNAME=$(sed -r 's/\./_/g' <<< $SHADER_BASENAME)
    SHADER_EMBED_VARNAME=$(sed -r 's/(^|_)([a-z])/\U\2/g' <<< $SHADER_EMBED_VARNAME)
    SHADER_EMBED_VARNAME="k${SHADER_EMBED_VARNAME}"

    glslc \
        "${SHADER_GLSL}" \
        -o "${SHADER_SPV}"

    generate_shader_embed \
        "${SHADER_GLSL}" \
        "${SHADER_SPV}" \
        "${SHADER_EMBED_VARNAME}" \
        "${SHADER_EMBED}"

    echo "Generated ${SHADER_EMBED}"
done
