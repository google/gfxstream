#!/bin/bash

# Copyright 2022 The Android Open Source Project
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
export GFXSTREAM_DIR=$(pwd)
if [ -z "$1" ];
then
    export MESA_DIR=$(pwd)/../../../external/mesa3d
else
    export MESA_DIR=$1
fi

export PREFIX_DIR=src/gfxstream

# We should use just use one vk.xml eventually..
export VK_MESA_XML=$MESA_DIR/src/vulkan/registry/vk.xml
export VK_XML=$GFXSTREAM_DIR/codegen/vulkan/vulkan-docs-next/xml/vk.xml

export GFXSTREAM_GUEST_ENCODER_DIR=/tmp/
export GFXSTREAM_HOST_DECODER_DIR=$GFXSTREAM_DIR/host/vulkan
export GFXSTREAM_OUTPUT_DIR=$GFXSTREAM_HOST_DECODER_DIR/cereal
export GFXSTREAM_SCRIPTS_DIR=$GFXSTREAM_DIR/scripts

export GEN_VK=$GFXSTREAM_DIR/$PREFIX_DIR/codegen/scripts/genvk.py
export CUSTOM_XML=$GFXSTREAM_DIR/$PREFIX_DIR/codegen/xml/vk_gfxstream.xml

# For testing Mesa codegen copy only
#export GEN_VK=$MESA_DIR/$PREFIX_DIR/codegen/scripts/genvk.py
#export CUSTOM_XML=$MESA_DIR/$PREFIX_DIR/codegen/xml/vk_gfxstream.xml

python3 $GEN_VK -registry $VK_XML -registryGfxstream $CUSTOM_XML cereal -o $GFXSTREAM_OUTPUT_DIR

export CEREAL_VARIANT=guest
export GFXSTREAM_GUEST_ENCODER_DIR=$GFXSTREAM_DIR/guest/vulkan_enc
python3 $GEN_VK -registry $VK_MESA_XML -registryGfxstream $CUSTOM_XML cereal -o /tmp/

# Should have a unified headers dir here:
python3 $GEN_VK -registry $CUSTOM_XML vulkan_gfxstream.h -o $GFXSTREAM_GUEST_ENCODER_DIR
python3 $GEN_VK -registry $CUSTOM_XML vulkan_gfxstream.h -o $GFXSTREAM_HOST_DECODER_DIR
