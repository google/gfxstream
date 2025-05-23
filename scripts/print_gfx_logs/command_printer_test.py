#
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

import io

import unittest
from typing import Dict

from . import opcodes
from . import command_printer

reverse_opcodes: Dict[str, int] = {v: k for k, v in opcodes.opcodes.items()}


class ComandPrinterOutputTestCase(unittest.TestCase):
    """
    Tests individual aspects of the command printer logic.
    """

    def get_printer(self, hex_data: str, timestamp_ms=0):
        """Helper function to return the command printer"""
        out = io.StringIO()
        buf = bytes.fromhex(hex_data)
        cmd_printer = command_printer.CommandPrinter(
            0, len(buf), buf, timestamp_ms, 0, 0, out)
        return cmd_printer, out

    def test_raises_if_not_all_bytes_decoded(self):
        # Make a command printer with 5 bytes
        cmd_printer, output = self.get_printer("01 02 03 04 05")
        # Decode 4 of them
        cmd_printer.write_int("foobar", size=4, indent=0)
        self.assertRaises(BufferError, cmd_printer.check_no_more_bytes)

    def test_decode_int(self):
        cmd_printer, output = self.get_printer("02 00 00 00")
        r = cmd_printer.write_int("foobar", size=4, indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(r, 2)
        self.assertEqual(output.getvalue(), "foobar: 2\n")

    def test_decode_optional_int(self):
        cmd_printer, output = self.get_printer("00 00 00 00 00 00 00 01 08")
        r = cmd_printer.write_int("i", size=1, indent=0, optional=True)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(r, 8)
        self.assertEqual(output.getvalue(), "i: 8\n")

    def test_decode_missing_int(self):
        cmd_printer, output = self.get_printer("00 00 00 00 00 00 00 00")
        r = cmd_printer.write_int("i", size=1, indent=0, optional=True)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(r, None)
        self.assertEqual(output.getvalue(), "i: (null)\n")

    def test_decode_optional_repeated_int(self):
        cmd_printer, output = self.get_printer("00 00 00 00 00 00 00 01 02 00 03 00")
        cmd_printer.write_int("i", size=2, indent=0, optional=True, count=2)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), "i: [0x2, 0x3]\n")

    def test_decode_float(self):
        cmd_printer, output = self.get_printer("00 00 00 3f")
        cmd_printer.write_float("foo", indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), "foo: 0.5\n")

    def test_decode_repeated_float(self):
        cmd_printer, output = self.get_printer("00 00 00 3f  00 00 80 3f")
        cmd_printer.write_float("foo", indent=0, count=2)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), "foo: [0.5, 1.0]\n")

    def test_decode_null_terminated_string(self):
        cmd_printer, output = self.get_printer("77 6f 72 6c 64 00")
        cmd_printer.write_string("hello", indent=1, size=None)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), '  hello: "world"\n')

    def test_decode_fixed_size_string(self):
        cmd_printer, output = self.get_printer("77 6f 72 6c 64 00 00 00")
        cmd_printer.write_string("hello", indent=1, size=8)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), '  hello: "world"\n')

    def test_decode_enum(self):
        enum = {1000156007: "FOOBAR"}
        cmd_printer, output = self.get_printer("67 2B 9D 3B")
        cmd_printer.write_enum("foo", enum, indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), 'foo: FOOBAR (1000156007)\n')

    def test_decode_unknown_enum(self):
        cmd_printer, output = self.get_printer("67 2B 9D 3B")
        cmd_printer.write_enum("foo", {}, indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), 'foo:  (1000156007)\n')

    def test_decode_flags(self):
        enum = {1: "FOO", 2: "BAR", 4: "BAZ"}
        cmd_printer, output = self.get_printer("03 00 00 00")
        cmd_printer.write_flags("foo", enum, indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), 'foo: FOO | BAR (0x3)\n')

    def test_decode_unknown_flags(self):
        enum = {1: "FOO", 2: "BAR", 4: "BAZ"}
        cmd_printer, output = self.get_printer("0A 00 00 00")
        cmd_printer.write_flags("foo", enum, indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), 'foo: 0x8 | BAR (0xa)\n')

    def test_decode_all_flags(self):
        enum = {1: "FOO", 2: "BAR", 4: "BAZ"}
        cmd_printer, output = self.get_printer("ff ff ff ff")
        cmd_printer.write_flags("foo", enum, indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), 'foo: (all flags) (0xffffffff)\n')

    def test_decode_zero_timestamp(self):
        cmd_printer, output = self.get_printer("", 0)
        cmd_printer.write_timestamp(indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), "") # Print nothing

    def test_decode_zero_timestamp(self):
        cmd_printer, output = self.get_printer("", 100)
        cmd_printer.write_timestamp(indent=0)
        cmd_printer.check_no_more_bytes()
        self.assertEqual(output.getvalue(), "Recorded at: 1969-12-31 16:00:00.000100\n")


class SuccessfullyDecodesCommandTestCase(unittest.TestCase):
    """
    This test suite checks that we're able to successfully decode each command (but doesn't check
    the exact output.)
    Each command that we pretty print should have at least one test here (unless the command takes
    no arguments).

    Please keep the test methods sorted in alphabetical order.
    """

    def run_test(self, opcode_str: str, cmd_data_hex: str):
        opcode = reverse_opcodes[opcode_str]
        cmd_data = bytes.fromhex(cmd_data_hex)
        cmd_printer = command_printer.CommandPrinter(opcode, len(cmd_data), cmd_data, 0, 0, 0)
        cmd_printer.print_cmd()

    def test_OP_vkAcquireImageANDROID(self):
        self.run_test("OP_vkAcquireImageANDROID", """
        e2 00 00 00 b8 08 00 00 02 00 03 00 e5 08 00 00
        02 00 06 00 ff ff ff ff f3 08 00 00 02 00 17 00
        00 00 00 00 00 00 00 00
        """)

    @unittest.skip("Needs support for struct extensions")
    def test_OP_vkAllocateMemory(self):
        self.run_test("OP_vkAllocateMemory", """
        d7 01 00 00 02 00 00 00 02 00 03 00 05 00 00 00
        00 00 00 18 e8 a9 a0 3b e8 a9 a0 3b 00 00 00 00
        0d 00 00 00 00 90 7e 00 00 00 00 00 07 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkBeginCommandBufferAsyncGOOGLE(self):
        self.run_test("OP_vkBeginCommandBufferAsyncGOOGLE", """
        2a 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkBindBufferMemory(self):
        self.run_test("OP_vkBindBufferMemory", """
        cc 00 00 00 b8 08 00 00 02 00 03 00 e3 08 00 00
        03 00 05 00 e4 08 00 00 02 00 07 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkBindImageMemory(self):
        self.run_test("OP_vkBindImageMemory", """
        de 00 00 00 b8 08 00 00 02 00 03 00 f1 08 00 00
        02 00 06 00 f2 08 00 00 02 00 07 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkCmdBeginRenderPass(self):
        self.run_test("OP_vkCmdBeginRenderPass", """
        2b 00 00 00 00 00 00 00 65 0a 00 00 02 00 11 00
        e7 09 00 00 03 00 12 00 00 00 00 00 00 00 00 00
        c4 01 00 00 80 00 00 00 02 00 00 00 00 00 79 bd
        2d fe 70 70 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 80 3f 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCmdBindDescriptorSets(self):
        self.run_test("OP_vkCmdBindDescriptorSets", """
        00 00 00 00 c8 09 00 00 03 00 13 00 01 00 00 00
        01 00 00 00 41 09 00 00 03 00 0d 00 02 00 00 00
        00 03 00 00 00 00 00 00
        """)

    def test_OP_vkCmdBindIndexBuffer(self):
        self.run_test("OP_vkCmdBindIndexBuffer", """
        a4 09 00 00 04 00 05 00 00 80 00 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkCmdBindPipeline(self):
        self.run_test("OP_vkCmdBindPipeline", """
        00 00 00 00 ba 09 00 00 02 00 15 00
        """)

    def test_OP_vkCmdBindVertexBuffers(self):
        self.run_test("OP_vkCmdBindVertexBuffers", """
        00 00 00 00 03 00 00 00 a4 09 00 00 04 00 05 00
        a4 09 00 00 04 00 05 00 a4 09 00 00 04 00 05 00
        00 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00
        0c 00 00 00 00 00 00 00
        """)

    def test_OP_vkCmdClearAttachments(self):
        self.run_test("OP_vkCmdClearAttachments", """
        01 00 00 00 06 00 00 00 00 00 00 00 00 00 80 3f
        00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00
        00 00 00 00 00 00 00 00 80 07 00 00 38 04 00 00
        00 00 00 00 01 00 00 00
        """)

    def test_OP_vkCmdClearColorImage(self):
        self.run_test("OP_vkCmdClearColorImage", """
        e5 08 00 00 02 00 06 00 07 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00
        01 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        01 00 00 00
        """)

    def test_OP_vkCmdCopyBufferToImage(self):
        self.run_test("OP_vkCmdCopyBufferToImage", """
        9f 09 00 00 02 00 05 00 a1 09 00 00 02 00 06 00
        07 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
        40 0b 00 00 00 0a 00 00 01 00 00 00 00 00 00 00
        00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 40 0b 00 00 00 0a 00 00 01 00 00 00
        """)

    def test_OP_vkCmdCopyImageToBuffer(self):
        self.run_test("OP_vkCmdCopyImageToBuffer", """
        99 09 00 00 09 00 06 00 06 00 00 00 98 09 00 00
        07 00 05 00 01 00 00 00 00 00 00 00 00 00 00 00
        20 00 00 00 20 00 00 00 01 00 00 00 00 00 00 00
        00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 20 00 00 00 20 00 00 00 01 00 00 00
        """)

    def test_OP_vkCmdDraw(self):
        self.run_test("OP_vkCmdDraw", """
        06 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCmdDrawIndexed(self):
        self.run_test("OP_vkCmdDrawIndexed", """
        6c 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkCmdPipelineBarrier(self):
        self.run_test("OP_vkCmdPipelineBarrier", """
        01 04 00 00 80 04 00 00 00 00 00 00 01 00 00 00
        2e 00 00 00 00 00 00 00 00 01 00 00 a0 01 00 00
        00 00 00 00 03 00 00 00 2d 00 00 00 00 00 00 00
        00 01 00 00 20 00 00 00 02 00 00 00 05 00 00 00
        00 00 00 00 00 00 00 00 d2 11 00 00 04 00 06 00
        01 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        01 00 00 00 2d 00 00 00 00 00 00 00 00 01 00 00
        20 00 00 00 02 00 00 00 05 00 00 00 00 00 00 00
        00 00 00 00 ac 15 00 00 02 00 06 00 01 00 00 00
        00 00 00 00 01 00 00 00 00 00 00 00 01 00 00 00
        2d 00 00 00 00 00 00 00 00 01 00 00 20 00 00 00
        02 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00
        9d 16 00 00 02 00 06 00 01 00 00 00 00 00 00 00
        01 00 00 00 00 00 00 00 01 00 00 00
        """)

    def test_OP_vkCmdSetScissor(self):
        self.run_test("OP_vkCmdSetScissor", """
        00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
        40 0b 00 00 00 0a 00 00
        """)

    def test_OP_vkCmdSetViewport(self):
        self.run_test("OP_vkCmdSetViewport", """
        00 00 00 00 01 00 00 00 00 00 00 00 00 00 20 45
        00 00 34 45 00 00 20 c5 00 00 00 00 00 00 80 3f
        """)

    def test_OP_vkCollectDescriptorPoolIdsGOOGLE(self):
        self.run_test("OP_vkCollectDescriptorPoolIdsGOOGLE", """
        c5 00 00 00 b8 08 00 00 02 00 03 00 c1 08 00 00
        02 00 0c 00 10 00 00 00 00 00 79 bc 3d fd b6 40
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCreateBufferWithRequirementsGOOGLE(self):
        self.run_test("OP_vkCreateBufferWithRequirementsGOOGLE", """
        2b 00 00 00 b8 08 00 00 02 00 03 00 0c 00 00 00
        00 00 00 00 00 00 00 00 00 10 00 00 00 00 00 00
        03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCreateDescriptorPool(self):
        self.run_test("OP_vkCreateDescriptorPool", """
        e1 00 00 00 b7 15 00 00 03 00 03 00 21 00 00 00
        00 00 00 00 00 00 00 00 10 00 00 00 02 00 00 00
        08 00 00 00 10 00 00 00 08 00 00 00 10 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCreateDescriptorSetLayout(self):
        self.run_test("OP_vkCreateDescriptorSetLayout", """
        c3 00 00 00 b8 08 00 00 02 00 03 00 20 00 00 00
        00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        08 00 00 00 01 00 00 00 3f 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkCreateFence(self):
        self.run_test("OP_vkCreateFence", """
        e3 00 00 00 b8 08 00 00 02 00 03 00 08 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCreateFramebuffer(self):
        self.run_test("OP_vkCreateFramebuffer", """
        83 02 00 00 fb 08 00 00 02 00 03 00 25 00 00 00
        00 00 00 00 00 00 00 00 65 0a 00 00 02 00 11 00
        02 00 00 00 e8 09 00 00 03 00 09 00 e9 09 00 00
        03 00 09 00 c4 01 00 00 80 00 00 00 01 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        """)

    @unittest.skip("Needs support for stream features hasRasterization / hasTessellation")
    def test_OP_vkCreateGraphicsPipelines(self):
        self.run_test("OP_vkCreateGraphicsPipelines", """
        3b 01 00 00 fb 08 00 00 02 00 03 00 02 09 00 00
        02 00 14 00 01 00 00 00 00 00 00 01 00 00 00 00
        1c 00 00 00 00 00 00 00 00 00 00 00 02 00 00 00
        12 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00
        b7 09 00 00 02 00 0a 00 00 00 00 04 6d 61 69 6e
        00 00 79 bd 2d fd a3 60 05 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 04 01 00 00 00
        04 00 00 00 00 00 00 00 00 00 00 04 02 00 00 00
        08 00 00 00 00 00 00 00 00 00 00 04 03 00 00 00
        0c 00 00 00 00 00 00 00 00 00 00 04 04 00 00 00
        10 00 00 00 00 00 00 00 00 00 00 04 00 00 00 00
        00 00 00 14 00 00 00 00 00 00 00 00 00 00 80 3f
        00 00 80 3f 00 00 00 00 12 00 00 00 00 00 00 00
        00 00 00 00 10 00 00 00 b8 09 00 00 02 00 0a 00
        00 00 00 04 6d 61 69 6e 00 00 79 bd 2d fd a3 f0
        05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 04 01 00 00 00 04 00 00 00 00 00 00 00
        00 00 00 04 02 00 00 00 08 00 00 00 00 00 00 00
        00 00 00 04 03 00 00 00 0c 00 00 00 00 00 00 00
        00 00 00 04 04 00 00 00 10 00 00 00 00 00 00 00
        00 00 00 04 00 00 00 00 00 00 00 14 00 00 00 00
        00 00 00 00 00 00 80 3f 00 00 80 3f 00 00 00 00
        00 00 79 bd 2d fd a4 78 13 00 00 00 00 00 00 00
        00 00 00 00 02 00 00 00 00 00 00 00 08 00 00 00
        00 00 00 00 01 00 00 00 08 00 00 00 00 00 00 00
        02 00 00 00 00 00 00 00 00 00 00 00 67 00 00 00
        00 00 00 00 01 00 00 00 01 00 00 00 67 00 00 00
        00 00 00 00 00 00 79 bd 2d fd a4 e0 14 00 00 00
        00 00 00 00 00 00 00 00 03 00 00 00 00 00 00 00
        00 00 79 bd 2d fd a5 00 00 00 79 bd 2d fd a5 18
        16 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00
        00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        00 00 00 00 00 00 79 bd 2d fd a5 48 17 00 00 00
        00 00 00 20 b9 bd 9e 3b b9 bd 9e 3b 00 00 00 18
        31 aa 9e 3b 31 aa 9e 3b 00 00 00 18 62 37 9b 3b
        62 37 9b 3b 00 00 00 00 00 00 00 00 00 00 00 00
        01 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 80 3f 00 00
        79 bd 2d fd a5 d8 18 00 00 00 00 00 00 00 00 00
        00 00 01 00 00 00 00 00 00 00 00 00 80 3f 00 00
        79 bd 2d fd a6 08 ff ff ff ff 00 00 00 00 00 00
        00 00 00 00 79 bd 2d fd a6 10 19 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 07 00 00 00 ff 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 07 00 00 00 ff 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 79 bd 2d fd
        a6 78 1a 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 07 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 79 bb ee 02
        ea 20 1b 00 00 00 00 00 00 00 00 00 00 00 02 00
        00 00 00 00 00 00 01 00 00 00 6b 09 00 00 02 00
        13 00 b9 09 00 00 03 00 11 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCreateImageView(self):
        self.run_test("OP_vkCreateImageView", """
        3c 01 00 00 fb 08 00 00 02 00 03 00 0f 00 00 00
        00 00 00 00 00 00 00 00 06 09 00 00 02 00 06 00
        01 00 00 00 25 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        01 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00
        """)

    @unittest.skip("Needs support for struct extensions")
    def test_OP_vkCreateImageWithRequirementsGOOGLE(self):
        self.run_test("OP_vkCreateImageWithRequirementsGOOGLE", """
        d0 00 00 00 b8 08 00 00 02 00 03 00 0e 00 00 00
        00 00 00 38 10 f1 9a 3b 10 f1 9a 3b 00 00 00 00
        00 00 79 bd 2d fd df e8 0d 00 00 00 80 07 00 00
        01 00 00 00 33 0b 00 00 06 09 00 00 00 00 00 00
        66 02 00 00 00 00 00 00 00 00 00 00 01 00 00 00
        25 00 00 00 80 07 00 00 38 04 00 00 01 00 00 00
        01 00 00 00 01 00 00 00 01 00 00 00 00 00 00 00
        97 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCreatePipelineCache(self):
        self.run_test("OP_vkCreatePipelineCache", """
        38 01 00 00 fb 08 00 00 02 00 03 00 11 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkCreateRenderPass(self):
        self.run_test("OP_vkCreateRenderPass", """
        3a 01 00 00 fb 08 00 00 02 00 03 00 26 00 00 00
        00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
        25 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 02 00 00 00 02 00 00 00
        01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        01 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkCreateSampler(self):
        self.run_test("OP_vkCreateSampler", """
        eb 00 00 00 b7 15 00 00 03 00 03 00 1f 00 00 00
        00 00 00 00 00 00 00 00 01 00 00 00 01 00 00 00
        00 00 00 00 02 00 00 00 02 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 80 3f 00 00 00 00
        03 00 00 00 00 00 00 00 00 00 80 3e 01 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkCreateSemaphore(self):
        self.run_test("OP_vkCreateSemaphore", """
        d3 00 00 00 b8 08 00 00 02 00 03 00 09 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyBuffer(self):
        self.run_test("OP_vkDestroyBuffer", """
        2a 00 00 00 b8 08 00 00 02 00 03 00 c0 08 00 00
        02 00 05 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyCommandPool(self):
        self.run_test("OP_vkDestroyCommandPool", """
        fb 05 00 00 b1 0a 00 00 05 00 03 00 a3 0a 00 00
        06 00 21 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyDescriptorPool(self):
        self.run_test("OP_vkDestroyDescriptorPool", """
        8b 01 00 00 fb 08 00 00 02 00 03 00 44 09 00 00
        02 00 0c 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyDescriptorSetLayout(self):
        self.run_test("OP_vkDestroyDescriptorSetLayout", """
        a3 01 00 00 fb 08 00 00 02 00 03 00 69 09 00 00
        02 00 0b 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyDevice(self):
        self.run_test("OP_vkDestroyDevice", """
        00 06 00 00 b1 0a 00 00 05 00 03 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkDestroyFence(self):
        self.run_test("OP_vkDestroyFence", """
        fc 05 00 00 b1 0a 00 00 05 00 03 00 b2 0a 00 00
        05 00 16 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyFramebuffer(self):
        self.run_test("OP_vkDestroyFramebuffer", """
        61 01 00 00 fb 08 00 00 02 00 03 00 bc 09 00 00
        02 00 12 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyImage(self):
        self.run_test("OP_vkDestroyImage", """
        ee 00 00 00 b8 08 00 00 02 00 03 00 f1 08 00 00
        02 00 06 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyImageView(self):
        self.run_test("OP_vkDestroyImageView", """
        60 01 00 00 fb 08 00 00 02 00 03 00 bb 09 00 00
        02 00 09 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyInstance(self):
        self.run_test("OP_vkDestroyInstance", """
        01 06 00 00 e7 08 00 00 07 00 01 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkDestroyPipeline(self):
        self.run_test("OP_vkDestroyPipeline", """
        cd 05 00 00 b1 0a 00 00 05 00 03 00 8a 0a 00 00
        05 00 15 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyPipelineCache(self):
        self.run_test("OP_vkDestroyPipelineCache", """
        39 01 00 00 fb 08 00 00 02 00 03 00 b9 09 00 00
        02 00 14 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyPipelineLayout(self):
        self.run_test("OP_vkDestroyPipelineLayout", """
        a2 01 00 00 fb 08 00 00 02 00 03 00 6b 09 00 00
        02 00 13 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyRenderPass(self):
        self.run_test("OP_vkDestroyRenderPass", """
        9f 01 00 00 fb 08 00 00 02 00 03 00 c2 09 00 00
        02 00 11 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroySemaphore(self):
        self.run_test("OP_vkDestroySemaphore", """
        f3 00 00 00 b8 08 00 00 02 00 03 00 f3 08 00 00
        02 00 17 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkDestroyShaderModule(self):
        self.run_test("OP_vkDestroyShaderModule", """
        7e 01 00 00 fb 08 00 00 02 00 03 00 b7 09 00 00
        02 00 0a 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkFreeCommandBuffers(self):
        self.run_test("OP_vkFreeCommandBuffers", """
        f9 05 00 00 b1 0a 00 00 05 00 03 00 a3 0a 00 00
        06 00 21 00 01 00 00 00 00 00 79 bb dd fe 4a b0
        ec 08 00 00 07 00 22 00
        """)

    def test_OP_vkFreeMemory(self):
        self.run_test("OP_vkFreeMemory", """
        ef 00 00 00 b8 08 00 00 02 00 03 00 f2 08 00 00
        02 00 07 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkFreeMemorySyncGOOGLE(self):
        self.run_test("OP_vkFreeMemorySyncGOOGLE", """
        ff 05 00 00 b1 0a 00 00 05 00 03 00 74 0a 00 00
        06 00 07 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkGetFenceStatus(self):
        self.run_test("OP_vkGetFenceStatus", """
        e6 00 00 00 b8 08 00 00 02 00 03 00 f6 08 00 00
        02 00 16 00
        """)

    def test_OP_vkGetMemoryHostAddressInfoGOOGLE(self):
        self.run_test("OP_vkGetMemoryHostAddressInfoGOOGLE", """
        01 01 00 00 5a 0c 00 00 05 00 03 00 5a 11 00 00
        02 00 07 00 00 00 00 00 bd d3 07 f0 00 00 00 00
        00 00 00 00 00 00 00 00 bd d3 07 f8 00 00 00 00
        00 00 00 00 00 00 00 00 bd d3 08 00 00 00 00 00
        00 00 00 00
        """)

    def test_OP_vkGetPhysicalDeviceFormatProperties(self):
        self.run_test("OP_vkGetPhysicalDeviceFormatProperties", """
        2f 00 00 00 b7 08 00 00 02 00 02 00 7c 00 00 00
        00 00 00 00 00 00 00 00 ff ff ff ff
        """)

    def test_OP_vkGetPhysicalDeviceProperties2KHR(self):
        self.run_test("OP_vkGetPhysicalDeviceProperties2KHR", """
        cd 00 00 00 b7 08 00 00 02 00 02 00 79 b0 9b 3b
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00
        """)

    def test_OP_vkGetSwapchainGrallocUsageANDROID(self):
        self.run_test("OP_vkGetSwapchainGrallocUsageANDROID", """
        cf 00 00 00 b8 08 00 00 02 00 03 00 25 00 00 00
        97 00 00 00 00 00 00 00
        """)

    def test_OP_vkQueueCommitDescriptorSetUpdatesGOOGLE(self):
        self.run_test("OP_vkQueueCommitDescriptorSetUpdatesGOOGLE", """
        bd 02 00 00 04 00 00 00 02 00 04 00 04 00 00 00
        0b 00 00 00 02 00 0c 00 79 01 00 00 02 00 0c 00
        de 00 00 00 02 00 0c 00 8a 01 00 00 02 00 0c 00
        07 00 00 00 0a 00 00 00 05 00 0b 00 31 00 00 00
        02 00 0b 00 31 00 00 00 02 00 0b 00 44 01 00 00
        02 00 0b 00 44 01 00 00 02 00 0b 00 44 01 00 00
        02 00 0b 00 44 01 00 00 02 00 0b 00 1b 00 00 00
        02 00 0d 00 89 01 00 00 02 00 0d 00 ee 00 00 00
        02 00 0d 00 9a 01 00 00 02 00 0d 00 98 01 00 00
        02 00 0d 00 99 01 00 00 02 00 0d 00 97 01 00 00
        02 00 0d 00 00 00 00 00 01 00 00 00 02 00 00 00
        03 00 00 00 03 00 00 00 03 00 00 00 03 00 00 00
        01 00 00 00 01 00 00 00 01 00 00 00 01 00 00 00
        01 00 00 00 01 00 00 00 01 00 00 00 00 00 00 00
        01 00 00 00 03 00 00 00 05 00 00 00 06 00 00 00
        07 00 00 00 08 00 00 00 09 00 00 00 23 00 00 00
        00 00 00 00 1b 00 00 00 02 00 0d 00 00 00 00 00
        00 00 00 00 01 00 00 00 08 00 00 00 00 00 00 00
        00 00 00 00 00 00 74 3c ce b3 aa 50 bd 16 00 00
        03 00 05 00 00 00 00 00 00 00 00 00 50 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 23 00 00 00
        00 00 00 00 89 01 00 00 02 00 0d 00 00 00 00 00
        00 00 00 00 01 00 00 00 08 00 00 00 00 00 00 00
        00 00 00 00 00 00 74 3c ce b3 aa 68 a8 16 00 00
        03 00 05 00 00 00 00 00 00 00 00 00 00 01 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 23 00 00 00
        00 00 00 00 89 01 00 00 02 00 0d 00 01 00 00 00
        00 00 00 00 01 00 00 00 08 00 00 00 00 00 00 00
        00 00 00 00 00 00 74 3c ce b3 aa 80 2d 00 00 00
        03 00 05 00 00 00 00 00 00 00 00 00 10 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 23 00 00 00
        00 00 00 00 ee 00 00 00 02 00 0d 00 00 00 00 00
        00 00 00 00 01 00 00 00 08 00 00 00 00 00 00 00
        00 00 00 00 00 00 74 3c ce b3 aa 98 a8 16 00 00
        03 00 05 00 00 00 00 00 00 00 00 00 00 01 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 23 00 00 00
        00 00 00 00 ee 00 00 00 02 00 0d 00 01 00 00 00
        00 00 00 00 01 00 00 00 08 00 00 00 00 00 00 00
        00 00 00 00 00 00 74 3c ce b3 aa b0 a8 16 00 00
        03 00 05 00 00 00 00 00 00 00 00 00 00 01 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 23 00 00 00
        00 00 00 00 9a 01 00 00 02 00 0d 00 00 00 00 00
        00 00 00 00 01 00 00 00 01 00 00 00 00 00 74 3c
        ce b3 aa c8 c1 16 00 00 02 00 0e 00 be 16 00 00
        02 00 09 00 05 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 23 00 00 00 00 00 00 00
        98 01 00 00 02 00 0d 00 00 00 00 00 00 00 00 00
        01 00 00 00 01 00 00 00 00 00 74 3c ce b3 aa e0
        c1 16 00 00 02 00 0e 00 c9 16 00 00 02 00 09 00
        05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 23 00 00 00 00 00 00 00 99 01 00 00
        02 00 0d 00 00 00 00 00 00 00 00 00 01 00 00 00
        01 00 00 00 00 00 74 3c ce b3 aa f8 c8 16 00 00
        02 00 0e 00 c6 16 00 00 02 00 09 00 05 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        23 00 00 00 00 00 00 00 97 01 00 00 02 00 0d 00
        00 00 00 00 00 00 00 00 01 00 00 00 01 00 00 00
        00 00 74 3c ce b3 ab 10 c1 16 00 00 02 00 0e 00
        cc 16 00 00 02 00 09 00 05 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00
        """)

    def test_OP_vkQueueFlushCommandsGOOGLE(self):
        self.run_test("OP_vkQueueFlushCommandsGOOGLE", """
        e4 00 00 00 b9 08 00 00 02 00 04 00 be 08 00 00
        02 00 22 00 18 01 00 00 00 00 00 00
        """)

    def test_OP_vkQueueSignalReleaseImageANDROIDAsyncGOOGLE(self):
        self.run_test("OP_vkQueueSignalReleaseImageANDROIDAsyncGOOGLE", """
        e7 00 00 00 b9 08 00 00 02 00 04 00 01 00 00 00
        00 00 79 bd 0d fe c9 20 e8 08 00 00 02 00 17 00
        e5 08 00 00 02 00 06 00
        """)

    def test_OP_vkQueueSubmitAsyncGOOGLE(self):
        self.run_test("OP_vkQueueSubmitAsyncGOOGLE", """
        c0 02 00 00 04 00 00 00 02 00 04 00 01 00 00 00
        04 00 00 00 00 00 00 00 03 00 00 00 c0 16 00 00
        02 00 17 00 cb 16 00 00 02 00 17 00 ce 16 00 00
        02 00 17 00 00 00 01 00 00 00 01 00 00 00 01 00
        01 00 00 00 08 00 00 00 02 00 22 00 00 00 00 00
        d1 16 00 00 02 00 16 00
        """)

    def test_OP_vkQueueWaitIdle(self):
        self.run_test("OP_vkQueueWaitIdle", """
        f3 05 00 00 8f 09 00 00 06 00 04 00
        """)

    def test_OP_vkResetFences(self):
        self.run_test("OP_vkResetFences", """
        4f 01 00 00 fb 08 00 00 02 00 03 00 01 00 00 00
        c3 09 00 00 02 00 16 00
        """)

    def test_OP_vkWaitForFences(self):
        self.run_test("OP_vkWaitForFences", """
        ed 00 00 00 b8 08 00 00 02 00 03 00 01 00 00 00
        03 09 00 00 05 00 16 00 01 00 00 00 00 b0 8e f0
        1b 00 00 00
        """)


if __name__ == '__main__':
    unittest.main()
