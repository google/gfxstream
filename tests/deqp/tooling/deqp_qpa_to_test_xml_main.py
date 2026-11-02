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
"""Converts dEQP QPA files to `test.xml` files for Bazel consumption."""

import argparse
import xml.etree.ElementTree as ET

from tests.deqp.tooling import deqp_qpa_to_test_xml


def main() -> None:
  parser = argparse.ArgumentParser(
      description="Converts dEQP QPA files to test.xml files."
  )
  parser.add_argument(
      "--input_qpa_file",
      required=True,
      help="Path to the input QPA file.",
  )
  parser.add_argument(
      "--output_test_xml_file",
      required=True,
      help="Path to the output test.xml file.",
  )
  args = parser.parse_args()

  with open(args.input_qpa_file, "r", encoding="utf-8") as f:
    input_qpa_content = f.read()

  output_test_xml = deqp_qpa_to_test_xml.convert_qpa_to_test_xml(
      input_qpa_content
  )

  tree = ET.ElementTree(output_test_xml)
  ET.indent(output_test_xml, space="  ")
  tree.write(
      args.output_test_xml_file,
      encoding="utf-8",
      xml_declaration=True,
  )


if __name__ == "__main__":
  main()
