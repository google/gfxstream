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

"""
Command line tool to process minidump files and print what was logged by GfxApiLogger.

For more details see:

design: go/bstar-gfx-logging
g3doc:  http://g3doc/play/g3doc/games/battlestar/kiwivm/graphics-tips.md#gfx-logs
C++:    http://source/play-internal/battlestar/aosp/device/generic/vulkan-cereal/utils/include/utils/GfxApiLogger.h

Usage:

python3 print_gfx_logs.py <path to minidump file>
"""

from __future__ import annotations
import argparse
import ctypes
import sys
from datetime import datetime
import mmap
import textwrap
from . import command_printer
from typing import NamedTuple, Optional, List
import traceback


class Header(ctypes.Structure):
    """The C struct that we use to represent the data in memory
    Keep in sync with GfxApiLogger.h
    """
    _fields_ = [('signature', ctypes.c_char * 10),
                ('version', ctypes.c_uint16),
                ('thread_id', ctypes.c_uint32),
                ('last_written_time', ctypes.c_uint64),
                ('write_index', ctypes.c_uint32),
                ('committed_index', ctypes.c_uint32),
                ('capture_id', ctypes.c_uint64),
                ('data_size', ctypes.c_uint32)]


class Command(NamedTuple):
    """A single command in the stream"""
    timestamp: int # Unix timestamp when command was recorded, in microseconds
    opcode: int
    original_size: int
    data: bytes


class Stream(NamedTuple):
    """Stream of commands received from the guest"""
    pos_in_file: int  # Location of this stream in the minidump file, useful for debugging
    timestamp: int  # Unix timestamp of last command received, in microseconds
    thread_id: int
    capture_id: int
    commands: List[Command]
    error_message: Optional[str]  # `None` if there were no errors parsing this stream

    @staticmethod
    def error(pos_in_file: int, error_message: str) -> Stream:
        return Stream(
            pos_in_file=pos_in_file, timestamp=0, thread_id=0, capture_id=0, commands=[],
            error_message=error_message)

def timestampToUnixUs(timestamp: int) -> int:
    # Convert Windows' GetSystemTimeAsFileTime to Unix timestamp in microseconds
    # https://stackoverflow.com/questions/1695288/getting-the-current-time-in-milliseconds-from-the-system-clock-in-windows
    timestamp_us = int(timestamp / 10 - 11644473600000000)
    if timestamp_us <= 0: timestamp_us = 0
    return timestamp_us

def read_uint32(buf: bytes, pos: int) -> int:
    """Reads a single uint32 from buf at a given position"""
    assert pos + 4 <= len(buf)
    return int.from_bytes(buf[pos:pos + 4], byteorder='little', signed=False)

def read_uint64(buf: bytes, pos: int) -> int:
    """Reads a single uint32 from buf at a given position"""
    assert pos + 8 <= len(buf)
    return int.from_bytes(buf[pos:pos + 8], byteorder='little', signed=False)

def process_command(buf: bytes, version: int) -> Command:
    pos = 0
    if version >= 3:
        timestamp_us = read_uint64(buf, pos)
        pos += 8
    else:
        timestamp_us = 0
    opcode = read_uint32(buf, pos)
    pos += 4
    size = read_uint32(buf, pos)
    pos += 4
    return Command(timestamp_us, opcode, size, bytes(buf[pos:]))


def process_stream(file_bytes: mmap, file_pos: int) -> Stream:
    # Read the header
    file_bytes.seek(file_pos)
    header = Header()
    header_bytes = file_bytes.read(ctypes.sizeof(header))
    ctypes.memmove(ctypes.addressof(header), header_bytes, ctypes.sizeof(header))

    if header.signature != b'GFXAPILOG':
        return Stream.error(file_pos, error_message="Signature doesn't match")

    if header.version < 2:
        return Stream.error(
            file_pos,
            error_message=(
                "This script can only process version 2 or later of the graphics API logs, but the "
                + "dump file uses version {} ").format(header.version))

    if header.version == 2:
        timestamp_us = timestampToUnixUs(int(header.last_written_time))
    else:
        timestamp_us = int(header.last_written_time)

    # Sanity check the size
    if header.data_size > 5_000_000:
        return Stream.error(
            file_pos,
            error_message="data size is larger than 5MB. This likely indicates garbage/corrupted " +
            "data")

    if header.committed_index >= header.data_size:
        return Stream.error(
            file_pos,
            error_message="index is larger than buffer size. Likely indicates garbage/corrupted " +
            "data")

    file_bytes.seek(file_pos + ctypes.sizeof(header))
    data = file_bytes.read(header.data_size)

    # Reorder the buffer so that we can read it in a single pass from back to front
    buf = data[header.committed_index:] + data[:header.committed_index]

    commands = []
    i = len(buf)
    while i >= 4:
        i -= 4
        size = read_uint32(buf, i)
        if size == 0 or size > i:
            # We reached the end of the stream
            break
        cmd = process_command(buf[i - size:i], header.version)

        commands.append(cmd)
        i -= size

    commands.reverse()  # so that they're sorted from oldest to most recent
    return Stream(file_pos, timestamp_us, header.thread_id, header.capture_id, commands, None)


def process_minidump(mm: mmap) -> List[Stream]:
    """
    Extracts a list of commands streams from a minidump file
    """
    streams = []
    pos = 0
    while True:
        pos = mm.find(b'GFXAPILOG', pos)
        if pos == -1:
            break
        streams.append(process_stream(mm, pos))
        pos += 1

    return streams


def main():
    parser = argparse.ArgumentParser(description="""Command line tool to process crash reports and print out the 
    commands logged by GfxApiLogger""")
    parser.add_argument('dump_file', help="Path to  minidump file")

    args = parser.parse_args()
    streams = None
    with open(args.dump_file, "r+b") as f:
        with mmap.mmap(f.fileno(), 0) as mm:
            streams = process_minidump(mm)

    streams.sort(key=lambda s: s.timestamp)

    total_commands = 0
    num_errors = 0
    for stream_idx, stream in enumerate(streams):
        print(textwrap.dedent("""
                  =======================================================
                  GfxApiLog command stream #{} at offset {} in dump
                    - Timestamp: {}
                    - Thread id: {}
                    - Capture id: {}""".format(stream_idx, stream.pos_in_file,
                                               datetime.fromtimestamp(stream.timestamp / 1000000.0),
                                               stream.thread_id,
                                               stream.capture_id)))
        if stream.error_message:
            print("Could not decode stream. Error: ", stream.error_message)
            continue

        subdecode_size = 0
        for cmd_idx, cmd in enumerate(stream.commands):
            total_commands += 1
            cmd_printer = command_printer.CommandPrinter(
                cmd.opcode, cmd.original_size, cmd.data, cmd.timestamp, stream_idx, cmd_idx)

            try:
                cmd_printer.print_cmd()
            except:
                num_errors += 1
                # Print stack trace and continue
                traceback.print_exc(file=sys.stdout)

            if subdecode_size > 0:
                subdecode_size -= cmd.original_size
                assert subdecode_size >= 0
                if subdecode_size == 0:
                    print("\n--- end of subdecode ---")

            if cmd_printer.cmd_name() == "OP_vkQueueFlushCommandsGOOGLE":
                assert subdecode_size == 0
                subdecode_size = cmd.original_size - 36
                print("\n--- start of subdecode, size = {} bytes ---".format(subdecode_size))
    print("\nDone: {} commands, {} errors".format(total_commands, num_errors))
    if num_errors > 0:
        print("""
NOTE: This script uses some simplifying assumptions to decode the commands. All
decoding errors are almost certainly a bug with this script, NOT a sign of bad
or corrupted data.""")


if __name__ == '__main__':
    main()
