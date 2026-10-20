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

import re
import xml.etree.ElementTree as ET


def _extract_text_content(xml_node: ET.Element) -> str:
  """Extracts all text content from an XML element and its children."""
  texts = []
  for text_elem in xml_node.findall(".//Text"):
    if text_elem.text:
      texts.append(text_elem.text.strip())
  return "\n".join(texts)


def convert_qpa_to_test_xml(qpa_content: str) -> ET.Element:
  """Converts a dEQP QPA result into a JUnit-style testsuites XML result."""

  test_blocks = re.split(r"#beginTestCaseResult\s+", qpa_content)
  test_blocks.pop(0)  # Skip header before the first testcase

  testsuites_elem = ET.Element("testsuites", {"name": "dEQP"})
  testsuite_elem = ET.SubElement(testsuites_elem, "testsuite", {"name": "dEQP"})

  total_tests = 0
  total_failures = 0
  total_errors = 0
  total_skipped = 0
  total_time = 0.0

  for block in test_blocks:
    block = block.strip()
    if not block:
      continue

    xml_start = block.find("<TestCaseResult")
    xml_end = block.find("</TestCaseResult>")
    if xml_start == -1 or xml_end == -1:
      case_path = block.splitlines()[0].strip()
      if not case_path:
        raise ValueError(f"Failed to find CasePath in block: {block}")
      test_duration = 0.0
      status_code = "Crash"
      result_text = "Test crashed or terminated unexpectedly."
      details_text = ""
    else:
      xml_str = block[xml_start : xml_end + len("</TestCaseResult>")]

      case_xml = ET.fromstring(xml_str)
      case_path = case_xml.attrib.get("CasePath")
      if not case_path:
        raise ValueError(f"Failed to find CasePath in: {xml_str}")

      test_duration = 0.0
      for num_elem in case_xml.findall("Number"):
        if num_elem.attrib.get("Name") == "TestDuration":
          unit = num_elem.attrib.get("Unit", "us")
          val = float(num_elem.text) if num_elem.text else 0.0
          if unit == "us":
            test_duration = val / 1_000_000.0
          elif unit == "ms":
            test_duration = val / 1_000.0
          elif unit == "s":
            test_duration = val
          break

      status_code = "Crash"
      result_text = "Test crashed or terminated unexpectedly."

      result_elem = case_xml.find("Result")
      if result_elem is not None:
        status_code = result_elem.attrib.get("StatusCode", "Pass")
        result_text = (
            result_elem.text.strip() if result_elem.text else status_code
        )

      details_text = _extract_text_content(case_xml)

    if "." in case_path:
      classname, test_name = case_path.rsplit(".", 1)
    else:
      classname = "dEQP"
      test_name = case_path

    testcase_attrib = {
        "name": test_name,
        "classname": classname,
        "time": f"{test_duration:.6f}",
        "status": "run",
        "result": "completed",
    }

    testcase_elem = ET.SubElement(testsuite_elem, "testcase", testcase_attrib)

    total_tests += 1
    total_time += test_duration
    if status_code in ("Pass", "QualityWarning", "CompatibilityWarning"):
      pass
    elif status_code == "NotSupported":
      total_skipped += 1
      testcase_elem.set("status", "notrun")
      testcase_elem.set("result", "suppressed")
      skipped_elem = ET.SubElement(
          testcase_elem,
          "skipped",
          {"message": result_text, "type": status_code},
      )
      if details_text:
        skipped_elem.text = details_text
    elif status_code == "Fail":
      total_failures += 1
      failure_elem = ET.SubElement(
          testcase_elem,
          "failure",
          {"message": result_text, "type": status_code},
      )
      if details_text:
        failure_elem.text = details_text
    else:  # Crash, Timeout, ResourceError, InternalError, etc.
      total_errors += 1
      error_elem = ET.SubElement(
          testcase_elem,
          "error",
          {"message": result_text, "type": status_code},
      )
      if details_text:
        error_elem.text = details_text

  summary_attrib = {
      "tests": str(total_tests),
      "failures": str(total_failures),
      "errors": str(total_errors),
      "disabled": "0",
      "skipped": str(total_skipped),
      "time": f"{total_time:.6f}",
  }

  testsuites_elem.attrib.update(summary_attrib)
  testsuite_elem.attrib.update(summary_attrib)

  return testsuites_elem
