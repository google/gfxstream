#!/bin/bash

# Copyright 2023 The Android Open Source Project
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

set -o errexit

WORKING_DIRECTORY="/tmp/gfxstream-nested-vulkan-loader"
if [ ! -z "$1" ]; then
    WORKING_DIRECTORY=$1
fi

if [ ! -d $WORKING_DIRECTORY ]; then
    echo "Creating working directory $WORKING_DIRECTORY."
    mkdir -p $WORKING_DIRECTORY
    cd $WORKING_DIRECTORY
    git clone https://github.com/KhronosGroup/Vulkan-Loader.git .
    git checkout -b v1.3.273 tags/v1.3.273
else
    echo "Reusing existing working directory $WORKING_DIRECTORY."
    cd $WORKING_DIRECTORY
fi

echo "Applying edits for Gfxstream."
VK_LOADER_ENV_VARS=(
    "VK_ADD_DRIVER_FILES"
    "VK_ADD_LAYER_PATH"
    "VK_DRIVER_FILES"
    "VK_ICD_FILENAMES"
    "VK_INSTANCE_LAYERS"
    "VK_LAYER_PATH"
    "VK_LAYER_PATH"
    "VK_LOADER_DEBUG"
    "VK_LOADER_DRIVERS_DISABLE"
    "VK_LOADER_DRIVERS_SELECT"
    "VK_LOADER_LAYERS_ALLOW"
    "VK_LOADER_LAYERS_DISABLE"
    "VK_LOADER_LAYERS_ENABLE"
)
for var in ${VK_LOADER_ENV_VARS[@]}; do
    echo "- Replacing occurrences of \"$var\" with \"GFXSTREAM_$var\"."
    sed -i -e "s/\\\"$var\\\"/\\\"GFXSTREAM_$var\\\"/g" loader/*.c
    sed -i -e "s/\\\"$var\\\"/\\\"GFXSTREAM_$var\\\"/g" loader/*.h
done
echo "- Adding in \"GFXSTREAM\" prefix to logging."
sed -i -e "s/    fputs(cmd_line_msg, stderr);/    fputs(\\\"GFXSTREAM-LOADER: \\\", stderr); fputs(cmd_line_msg, stderr);/g" loader/log.c

if [ ! -d build ]; then
    echo "Creating build directory."
    mkdir build
    cd build
    python ../scripts/update_deps.py
    cmake -C helper.cmake -D CMAKE_BUILD_TYPE=Debug ..
else
    echo "Reusing existing build directory."
    cd build
fi

echo "Building."
cmake --build .

cp loader/libvulkan.so loader/libvulkan_gfxstream.so

echo "Use export LD_LIBRARY_PATH=\"\$LD_LIBRARY_PATH:$(pwd)/loader\" to add to your path."
