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
        **kwargs):
    sh_test(
        name = name,
        srcs = ["//tests/end2end:deqp_wrapper.sh"],
        args = [
            "--deqp_binary=$(location @deqp//:deqp-vk)",
            "--deqp_cases=" + ",".join(deqp_cases),
            "--deqp_qpa_to_test_xml=$(location //tests/end2end:deqp_qpa_to_test_xml_main)",
            "--graphics_drivers_dir=$(location //common/testenv:graphics_test_environment_drivers)",
            "--kumquat_binary=$(location @rutabaga//:rutabaga_gfx_kumquat_server)",
            "--vulkan_driver=$(location @mesa//:mesa_gfxstream_guest_vulkan)",
        ],
        data = [
            "//common/testenv:graphics_test_environment_drivers",
            "//tests/end2end:deqp_qpa_to_test_xml_main",
            "@deqp//:data",
            "@deqp//:deqp-vk",
            "@mesa//:mesa_gfxstream_guest_vulkan",
            "@rutabaga//:rutabaga_gfx_kumquat_server",
        ],
        **kwargs
    )
