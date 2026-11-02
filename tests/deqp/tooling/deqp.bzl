# Copyright (C) 2026 The Android Open Source Project
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
"""Bazel rules for running dEQP Vulkan tests over Gfxstream."""

load("@rules_shell//shell:sh_test.bzl", "sh_test")

def gfxstream_deqp_vk_test(
        name,
        deqp_cases,
        vulkan_driver = "@mesa//:mesa_gfxstream_guest_vulkan",
        **kwargs):
    host_driver_args = select({
        "//common/testenv:graphics_test_environment_gles_angle_vulkan_lavapipe": [
            "--host_drivers_dir=$(location //common/testenv:graphics_test_environment_drivers)",
            "--host_vulkan_driver_manifest=$(location //common/testenv:graphics_test_environment_drivers)/vk_lavapipe_icd.json",
        ],
        "//common/testenv:graphics_test_environment_gles_angle_vulkan_swiftshader": [
            "--host_drivers_dir=$(location //common/testenv:graphics_test_environment_drivers)",
            "--host_vulkan_driver_manifest=$(location //common/testenv:graphics_test_environment_drivers)/vk_swiftshader_icd.json",
        ],
        "//conditions:default": [],
    })

    host_driver_data = select({
        "//common/testenv:graphics_test_environment_gles_angle_vulkan_lavapipe": [
            "//common/testenv:graphics_test_environment_drivers",
        ],
        "//common/testenv:graphics_test_environment_gles_angle_vulkan_swiftshader": [
            "//common/testenv:graphics_test_environment_drivers",
        ],
        "//conditions:default": [],
    })

    sh_test(
        name = name,
        srcs = ["//tests/deqp/tooling:deqp_wrapper.sh"],
        args = [
            "--deqp_binary=$(location @deqp//:deqp-vk)",
            "--deqp_cases=" + ",".join(deqp_cases),
            "--deqp_qpa_to_test_xml=$(location //tests/deqp/tooling:deqp_qpa_to_test_xml_main)",
            "--kumquat_binary=$(location @rutabaga//:rutabaga_gfx_kumquat_server)",
            "--vulkan_driver=$(location " + vulkan_driver + ")",
        ] + host_driver_args,
        data = [
            vulkan_driver,
            "//tests/deqp/tooling:deqp_qpa_to_test_xml_main",
            "@deqp//:data",
            "@deqp//:deqp-vk",
            "@rutabaga//:rutabaga_gfx_kumquat_server",
        ] + host_driver_data,
        **kwargs
    )
