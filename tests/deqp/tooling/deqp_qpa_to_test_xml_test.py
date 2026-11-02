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
"""Tests the dEQP QPA to test.xml converter."""

import unittest
import xml.etree.ElementTree as ET

from tests.deqp.tooling import deqp_qpa_to_test_xml


class QpaToTestXmlTest(unittest.TestCase):

  def test_convert_qpa_to_test_xml(self):
    test_xml = deqp_qpa_to_test_xml.convert_qpa_to_test_xml("""\
#sessionInfo releaseName unknown
#sessionInfo releaseId 0xcafebabe
#sessionInfo targetName "Android"
#sessionInfo logFormatVersion "0.3.4"
#sessionInfo commandLineParameters "deqp --deqp-case="dEQP-VK.api.object_management.multithreaded_per_thread_resources.image_view_cube_arr" --deqp-log-images=disable --deqp-log-shader-sources=disable --deqp-log-filename=/data/user/0/com.google.android.apps.internal.gpuinsights.eridani/files/TestResults.qpa"

#beginSession

#beginTestCaseResult dEQP-VK.api.object_management.multithreaded_per_thread_resources.image_view_cube_arr
<?xml version="1.0" encoding="UTF-8"?>
<TestCaseResult CasePath="dEQP-VK.api.object_management.multithreaded_per_thread_resources.image_view_cube_arr" Version="0.3.4" CaseType="SelfValidate">
 <Text>numThreads = 8</Text>
 <Number Name="TestDuration" Description="Test case duration in microseconds" Tag="Time" Unit="us">53456</Number>
 <Result StatusCode="Pass">Pass</Result>
</TestCaseResult>

#endTestCaseResult
""")

    ET.indent(test_xml, space="  ")
    actual = ET.tostring(test_xml, encoding="unicode")
    self.assertEqual(
        actual,
        """\
<testsuites name="dEQP" tests="1" failures="0" errors="0" disabled="0" skipped="0" time="0.053456">
  <testsuite name="dEQP" tests="1" failures="0" errors="0" disabled="0" skipped="0" time="0.053456">
    <testcase name="image_view_cube_arr" classname="dEQP-VK.api.object_management.multithreaded_per_thread_resources" time="0.053456" status="run" result="completed" />
  </testsuite>
</testsuites>""",
    )

  def test_convert_truncated_crash_qpa_to_test_xml(self):
    test_xml = deqp_qpa_to_test_xml.convert_qpa_to_test_xml("""\
#beginSession

#beginTestCaseResult dEQP-VK.api.smoke.triangle
<?xml version="1.0" encoding="UTF-8"?>
<TestCaseResult CasePath="dEQP-VK.api.smoke.triangle" Version="0.3.4" CaseType="SelfValidate">
""")

    ET.indent(test_xml, space="  ")
    actual = ET.tostring(test_xml, encoding="unicode")
    self.assertEqual(
        actual,
        """\
<testsuites name="dEQP" tests="1" failures="0" errors="1" disabled="0" skipped="0" time="0.000000">
  <testsuite name="dEQP" tests="1" failures="0" errors="1" disabled="0" skipped="0" time="0.000000">
    <testcase name="triangle" classname="dEQP-VK.api.smoke" time="0.000000" status="run" result="completed">
      <error message="Test crashed or terminated unexpectedly." type="Crash" />
    </testcase>
  </testsuite>
</testsuites>""",
    )

  def test_convert_not_supported_qpa_to_test_xml(self):
    test_xml = deqp_qpa_to_test_xml.convert_qpa_to_test_xml("""\
#beginSession

#beginTestCaseResult dEQP-VK.wsi.android.surface.create
<?xml version="1.0" encoding="UTF-8"?>
<TestCaseResult CasePath="dEQP-VK.wsi.android.surface.create" Version="0.3.4" CaseType="SelfValidate">
 <Text>VK_KHR_android_surface is not supported</Text>
 <Number Name="TestDuration" Description="Test case duration in microseconds" Tag="Time" Unit="us">1234</Number>
 <Result StatusCode="NotSupported">VK_KHR_android_surface not supported</Result>
</TestCaseResult>

#endTestCaseResult
""")

    ET.indent(test_xml, space="  ")
    actual = ET.tostring(test_xml, encoding="unicode")
    self.assertEqual(
        actual,
        """\
<testsuites name="dEQP" tests="1" failures="0" errors="0" disabled="0" skipped="1" time="0.001234">
  <testsuite name="dEQP" tests="1" failures="0" errors="0" disabled="0" skipped="1" time="0.001234">
    <testcase name="create" classname="dEQP-VK.wsi.android.surface" time="0.001234" status="notrun" result="suppressed">
      <skipped message="VK_KHR_android_surface not supported" type="NotSupported">VK_KHR_android_surface is not supported</skipped>
    </testcase>
  </testsuite>
</testsuites>""",
    )


if __name__ == "__main__":
  unittest.main()
