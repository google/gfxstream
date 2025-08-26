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
import subprocess
import os

# Converts GLSL shaders to a SPIR-V C++ headers. Should be called from the base
# gfxstream path to always generate the same output with correct relative paths.

def convert_shader(shader_path, output_path, var_name):
    """Calls glsl-shader-to-spv.py to convert a GLSL shader to a SPIR-V C++ header."""

    script_path = "scripts/glsl-shader-to-spv.py"
    if not os.path.exists(script_path):
        print(f"Error: The script '{script_path}' was not found.")
        return

    command = [
        "python",
        script_path,
        shader_path,
        output_path,
        var_name
    ]

    try:
        subprocess.run(command, check=True, text=True)
        print(f"Successfully converted {shader_path} -> {output_path}")
    except subprocess.CalledProcessError as e:
        # We're not capturing stderr with capture_output=True on
        # subprocess.run, so the error should already be printed
        print(f"Error converting {shader_path}.")
        print(f"Exit code: {e.returncode}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    # Define the shaders relative to the base path
    shaders = [
        ("host/vulkan/Compositor.vert", "host/vulkan/CompositorVertexShader.h", "compositorVertexShader"),
        ("host/vulkan/Compositor.frag", "host/vulkan/CompositorFragmentShader.h", "compositorFragmentShader")
    ]

    # Convert them all
    for shader_file, output_file, variable_name in shaders:
        convert_shader(shader_file, output_file, variable_name)