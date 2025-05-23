# Copyright (C) 2022 The Android Open Source Project
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

import os

# This script compiles the image decompression shaders.
# glslc must be in the path for this script to work

# The list of shaders to compile, without the .comp extension
shaders = [
    "Astc",
    "AstcToRgb",
    "AstcToBc3",
    "EacR11Snorm",
    "EacR11Unorm",
    "EacRG11Snorm",
    "EacRG11Unorm",
    "Etc2RGB8",
    "Etc2RGBA8",
]

# Template for the compilation command
command = "glslc -DDIM={dim} --target-env=vulkan1.1 -mfmt=num {input} -o {output}"

output_dir = "compiled"


# Compiles a single shader
# shader: shader name, without the .comp extension
# dim: image dimension. Must be 1, 2 or 3 - for 1D, 2D and 3D respectively
def compile(shader: str, dim: int):
    input = "{}.comp".format(shader)
    output = os.path.join(output_dir, "{}_{}D.inl".format(shader, dim))
    cmd = command.format(dim=dim, input=input, output=output)
    print("Executing: {}".format(cmd))
    ret = os.system(cmd)
    if ret != 0:
        print("Compiling {} failed.".format(input))
        exit(ret)


def main():
    # Set the current working directory where the script is located
    os.chdir(os.path.dirname(os.path.realpath(__file__)))
    print("Compiling shaders in", os.getcwd())

    # Make sure the shaders are all properly formatted
    ret = os.system("clang-format -i *.comp *.glsl")
    if ret != 0:
        print("Running clang-format failed. Make sure it is available in your PATH.")
        exit(ret)

    os.makedirs(output_dir, exist_ok=True)

    for shader in shaders:
        for dim in range(1, 4):
            compile(shader, dim)


if __name__ == "__main__":
    main()
