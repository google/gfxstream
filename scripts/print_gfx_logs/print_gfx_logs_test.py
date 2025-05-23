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

import ctypes
from typing import List
from .print_gfx_logs import process_minidump, Command, Stream, Header
import mmap
import unittest
import sys


def create_vk_destroy_instance_command(vk_instance: int, p_allocator: int) -> Command:
    OP_vkDestroyInstance = 20001
    opcode = OP_vkDestroyInstance
    data = bytearray()
    # seq number
    seqno = 20
    data += seqno.to_bytes(4, byteorder='big')
    # VkInstance
    data += vk_instance.to_bytes(8, sys.byteorder)
    # VkAllocationCallbacks
    data += p_allocator.to_bytes(8, sys.byteorder)
    return Command(opcode=opcode, original_size=len(data) + 8, data=bytes(data))


def create_command_data(commands: List[Command]) -> bytearray:
    data = bytearray()
    for command in commands:
        data += command.opcode.to_bytes(4, sys.byteorder)
        data += command.original_size.to_bytes(4, sys.byteorder)
        data += command.data
        assert(len(command.data) + 8 == command.original_size)
        data += command.original_size.to_bytes(4, byteorder='little')
    return data


def create_dump(stream: Stream) -> bytearray:
    data = create_command_data(stream.commands)
    header = Header()
    header.signature = b'GFXAPILOG\0'
    header.version = 2
    header.thread_id = stream.thread_id
    # Convert Windows' GetSystemTimeAsFileTime to Unix timestamp
    # https://stackoverflow.com/questions/1695288/getting-the-current-time-in-milliseconds-from-the-system-clock-in-windows
    header.last_written_time = (stream.timestamp + 11644473600000) * 10_000
    header.write_index = 0
    header.committed_index = 0
    header.capture_id = stream.capture_id
    header.data_size = len(data)
    res = b'\0' * ctypes.sizeof(header)
    ctypes.memmove(res, ctypes.addressof(header), ctypes.sizeof(header))
    return res + data


class ProcessMinidumpTestCase(unittest.TestCase):
    def test_single_command(self):
        command = create_vk_destroy_instance_command(vk_instance=0x1234, p_allocator=0x7321)
        stream = Stream(pos_in_file=0, timestamp=123456, thread_id=4726,
                        capture_id=8261, commands=[command], error_message=None)
        offset = 24
        dump = b'\0' * offset + create_dump(stream)
        streams = None
        with mmap.mmap(-1, len(dump)) as mm:
            mm.write(dump)
            mm.seek(0)
            streams = process_minidump(mm)
        self.assertEqual(len(streams), 1)
        self.assertEqual(streams[0].error_message, None)
        self.assertEqual(streams[0].pos_in_file, offset)
        self.assertEqual(streams[0].timestamp, stream.timestamp)
        self.assertEqual(streams[0].thread_id, stream.thread_id)
        self.assertEqual(streams[0].capture_id, stream.capture_id)
        self.assertEqual(len(streams[0].commands), 1)
        self.assertEqual(streams[0].commands[0].opcode, command.opcode)
        self.assertEqual(streams[0].commands[0].original_size, command.original_size)
        self.assertEqual(len(streams[0].commands[0].data), len(command.data))
        self.assertEqual(streams[0].commands[0].data, command.data)

    def test_multiple_commands(self):
        commands = [create_vk_destroy_instance_command(
            vk_instance=0x1234, p_allocator=0x7321),
            create_vk_destroy_instance_command(
            vk_instance=0x3621, p_allocator=0x7672)]
        stream = Stream(pos_in_file=0, timestamp=123456, thread_id=4726,
                        capture_id=8261, commands=commands, error_message=None)
        dump = create_dump(stream)
        streams = None
        with mmap.mmap(-1, len(dump)) as mm:
            mm.write(dump)
            mm.seek(0)
            streams = process_minidump(mm)
        self.assertEqual(len(streams), 1)
        self.assertEqual(streams[0].error_message, None)
        self.assertEqual(len(streams[0].commands), len(commands))
        for actual_command, expected_command in zip(streams[0].commands, commands):
            self.assertEqual(actual_command.opcode, expected_command.opcode)
            self.assertEqual(actual_command.original_size, expected_command.original_size)
            self.assertEqual(len(actual_command.data), len(expected_command.data))
            self.assertEqual(actual_command.data, expected_command.data)

    def test_multiple_streams(self):
        command = create_vk_destroy_instance_command(vk_instance=0x1234, p_allocator=0x7321)
        streams = []
        offsets = []
        dump = bytearray()
        for i in range(10):
            stream = Stream(pos_in_file=0, timestamp=i, thread_id=i,
                            capture_id=i, commands=[command], error_message=None)
            streams.append(stream)
            dump += b'\0' * i
            offsets.append(len(dump))
            dump += create_dump(stream)
        actual_streams = None
        with mmap.mmap(-1, len(dump)) as mm:
            mm.write(dump)
            mm.seek(0)
            actual_streams = process_minidump(mm)
        self.assertEqual(len(actual_streams), len(streams))
        for i, (actual_stream, expected_stream) in enumerate(zip(actual_streams, streams)):
            self.assertEqual(actual_stream.error_message, None)
            self.assertEqual(actual_stream.pos_in_file, offsets[i])
            self.assertEqual(actual_stream.timestamp, expected_stream.timestamp)
            self.assertEqual(actual_stream.thread_id, expected_stream.thread_id)
            self.assertEqual(actual_stream.capture_id, expected_stream.capture_id)
            self.assertEqual(len(actual_stream.commands), len(expected_stream.commands))
