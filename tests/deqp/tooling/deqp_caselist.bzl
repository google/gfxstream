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
"""Bazel rules for generating dEQP caselists."""

load("@bazel_skylib//rules:write_file.bzl", "write_file")

def generate_deqp_vk_caselist(name, deqp_cases, **kwargs):
    """Generates a text file containing the filtered subset of dEQP Vulkan tests.

    Args:
      name: The name of the genrule target (outputs `<name>.txt`).
      deqp_cases: List of dEQP test case patterns to include (or exclude when
        prefixed with `-`).
      **kwargs: Additional keyword arguments forwarded to `native.genrule`.
    """

    includes = []
    excludes = []
    for c in deqp_cases:
        if c.startswith("-"):
            excludes.append(c[1:])
        else:
            includes.append(c)

    srcs = ["@deqp//:data"]
    cmd_parts = [
        "$(location @deqp//:deqp-vk)",
        "--deqp-runmode=txt-caselist",
        "--deqp-archive-dir=$$(dirname $(location @deqp//:deqp-vk))/external/vulkancts/data",
        "--deqp-caselist-export-file=$(OUTS).tmp",
        "--deqp-log-filename=$(OUTS).qpa",
        "--deqp-shadercache-filename=$(OUTS).shadercache",
    ]

    has_wildcards = any(["*" in c or "?" in c for c in includes])
    if includes:
        if has_wildcards:
            cmd_parts.append("--deqp-case=" + ",".join(includes))
        else:
            input_name = name + "_input"
            input_file = input_name + ".txt"
            write_file(
                name = input_name,
                out = input_file,
                content = includes + [""],
                testonly = kwargs.get("testonly", False),
            )
            srcs.append(":" + input_file)
            cmd_parts.append("--deqp-caselist-file=$(location :" + input_file + ")")

    if excludes:
        cmd_parts.append("--deqp-exclude-case=" + ",".join(excludes))

    cmd_parts.append(
        "&& sed -n 's/^TEST: //p' $(OUTS).tmp > $(OUTS) && rm -f $(OUTS).tmp $(OUTS).qpa $(OUTS).shadercache",
    )

    cmd = " ".join(cmd_parts)

    native.genrule(
        name = name,
        srcs = srcs,
        outs = [name + ".txt"],
        tools = ["@deqp//:deqp-vk"],
        cmd = cmd,
        **kwargs
    )
