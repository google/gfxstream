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
from datetime import datetime
import sys
import textwrap
from typing import Dict, Optional
from . import vulkan_printer
from . import opcodes
import struct


class CommandPrinter:
    """This class is responsible for printing the commands found in the minidump file to the terminal."""

    def __init__(self, opcode: int, original_size: int, data: bytes, timestamp: int,
                 stream_idx: int, cmd_idx: int, out=sys.stdout):
        self.opcode = opcode
        self.original_size = original_size
        self.data = io.BytesIO(data)
        self.timestamp = timestamp
        self.stream_idx = stream_idx
        self.cmd_idx = cmd_idx
        self.out = out

    def print_cmd(self):
        """
        Tries to decode and pretty print the command to the terminal.
        Falls back to printing hex data if the command doesn't have a printer.
        """

        # Print out the command name
        print("\n{}.{} - {}: ({} bytes)".format(self.stream_idx, self.cmd_idx, self.cmd_name(),
                                                self.original_size - 8), file=self.out)
        self.write_timestamp(4)

        if len(self.data.getbuffer()) == 0:
            return

        pretty_printer = getattr(vulkan_printer, self.cmd_name(), None)
        if not pretty_printer:
            self.print_raw()
            return

        try:
            pretty_printer(self, indent=4)
            self.check_no_more_bytes()
        except Exception as ex:
            print("Error while processing {}: {}".format(self.cmd_name(), repr(ex)), file=self.out)
            print("Command raw data:", file=self.out)
            self.print_raw()
            raise ex

    def check_no_more_bytes(self):
        """
        Checks that we processed all the bytes, otherwise there's probably a bug in the decoding
        logic
        """
        if self.data.tell() != len(self.data.getbuffer()):
            raise BufferError(
                "Not all data was decoded. Decoded {} bytes but command had {}".format(
                    self.data.tell(), len(self.data.getbuffer())))

    def cmd_name(self) -> str:
        """Returns the command name (e.g.: "OP_vkBeginCommandBuffer", or the opcode as a string if unknown"""
        return opcodes.opcodes.get(self.opcode, str(self.opcode))

    def print_raw(self):
        """Prints the command data as a hex bytes, as a fallback if we don't know how to decode it"""
        truncated = self.original_size > len(self.data.getbuffer()) + 8
        indent = 8
        hex = ' '.join(["{:02x}".format(x) for x in self.data.getbuffer()])
        if truncated:
            hex += " [...]"
        lines = textwrap.wrap(hex, width=16 * 3 + indent, initial_indent=' ' * indent,
                              subsequent_indent=' ' * indent)
        for l in lines:
            print(l, file=self.out)

    def read_bytes(self, size: int):
        buf = self.data.read(size)
        if len(buf) != size:
            raise EOFError("Unexpectedly reached the end of the buffer")
        return buf

    def read_int(self, size: int, signed: bool = False, big_endian: bool = False) -> int:
        assert size in [1, 2, 4, 8], "Invalid size to read: " + str(size)
        buf = self.read_bytes(size)
        byte_order = 'big' if big_endian else 'little'
        return int.from_bytes(buf, byteorder=byte_order, signed=signed)

    def read_float(self) -> float:
        buf = self.read_bytes(4)
        return struct.unpack('f', buf)[0]

    def write(self, msg: str, indent: int):
        """Prints a string at a given indentation level"""
        assert type(msg) == str
        assert type(indent) == int and indent >= 0
        print("  " * indent + msg, end='', file=self.out)

    def write_int(self,
                  field_name: str,
                  size: int,
                  indent: int,
                  signed: bool = False,
                  big_endian: bool = False,
                  optional: bool = False,
                  count: Optional[int] = None) -> Optional[int]:
        """
        Reads and prints integers from the data stream.

        When reading a single int (ie: when count=None), returns the int that was read, otherwise
        returns None.

        size: size of the integer in bytes
        indent: indentation level that we should write at
        signed: whether to treat it as a signed or unsigned int
        big_endian: whether to treat it as little endian or big endian
        optional: if True, we will first read an 8 byte boolean value. If that value is false, we
        will return without further reading.
        count: how many integers to read, for repeated values.
        """
        if optional and self.check_null(field_name, indent):
            return

        # Write the field name
        self.write("{name}: ".format(name=field_name), indent)

        if count is not None:
            values = ["0x{:x}".format(self.read_int(size, signed, big_endian)) for i in
                      range(0, count)]
            self.write("[{}]\n".format(", ".join(values)), indent=0)
        else:
            value = self.read_int(size, signed, big_endian)
            # Print small values as decimal only, otherwise hex + decimal
            format_str = ("{val}\n" if value < 10 else "0x{val:x} ({val})\n")
            self.write(format_str.format(val=value), indent=0)
            return value

    def write_float(self, field_name: str, indent: int, count: Optional[int] = None):
        if count is not None:
            values = [str(self.read_float()) for i in range(0, count)]
            self.write("{}: [{}]\n".format(field_name, ", ".join(values)), indent)
        else:
            self.write("{}: {}\n".format(field_name, self.read_float()), indent)

    def write_enum(self, field_name: str, enum: Dict[int, str], indent: int) -> int:
        """Reads the next 32-byte int from the data stream, prints it as an enum, and return it"""
        value = self.read_int(4)
        self.write("{}: {} ({})\n".format(field_name, enum.get(value, ""), value), indent)
        return value

    def write_flags(self, field_name: str, enum: Dict[int, str], indent: int):
        """Reads and prints Vulkan flags (byte masks)"""
        remaining_flags = flags = self.read_int(4)
        flags_list = []
        if remaining_flags == 0xffffffff:
            # When the value is set to all flags, don't bother listing them all
            flags_list.append("(all flags)")
        else:
            for (value, flag) in enum.items():
                if value & remaining_flags:
                    remaining_flags ^= value
                    flags_list.append(flag)
            if remaining_flags != 0:
                flags_list.insert(0, "0x{:x}".format(remaining_flags))
        self.write("{}: {} (0x{:x})\n".format(field_name, " | ".join(flags_list), flags), indent)

    def write_stype_and_pnext(self, expected_stype: str, indent: int):
        """Reads and prints the sType and pNext fields found in many Vulkan structs, while also sanity checking them"""
        stype = self.write_enum("sType", vulkan_printer.VkStructureType, indent)
        stype_str = vulkan_printer.VkStructureType.get(stype)
        if stype_str != expected_stype:
            raise ValueError("Wrong structure type. Expected: {}, got {} ({}) instead".format(
                expected_stype, stype, stype_str))

        pnext_size = self.write_int("pNext_size", 4, indent, big_endian=True)
        if pnext_size != 0:
            self.write_enum("ext type", vulkan_printer.VkStructureType, indent + 1)
            raise NotImplementedError("Decoding structs with extensions is not supported")

    def write_timestamp(self, indent):
        if self.timestamp != 0:
            self.write(
                "Recorded at: {}\n".format(datetime.fromtimestamp(self.timestamp / 1000000.0)),
                indent)

    def check_null(self, field_name: str, indent) -> bool:
        is_null = self.read_int(8, big_endian=True) == 0
        if is_null:
            self.write("{}: (null)\n".format(field_name), indent)
        return is_null

    def write_struct(self, field_name: str, struct_fn, optional: bool, count: Optional[int],
                     indent: int):
        """
        Reads and prints a struct, calling `struct_fn` to pretty-print it
        optional: whether this is an optional structure. In this case, we will read an int64 first
                  and skip the struct if the result is null.
        count: how many times this is repeated. Pass None for non-repeated fields.
        """
        if optional and self.check_null(field_name, indent):
            return

        is_repeated = count is not None
        for i in range(0, count if is_repeated else 1):
            suffix = " #{}".format(i) if is_repeated else ""
            self.write("{}{}:\n".format(field_name, suffix), indent)
            struct_fn(self, indent + 1)

    def write_string(self, field_name: str, size: Optional[int], indent: int):
        """
        Reads a null-terminated string from the stream.
        size: if specified, reads up to this many characters
        """
        buf = bytearray()
        if size is not None:
            buf = self.read_bytes(size)
            buf = buf.rstrip(b'\x00')
        else:
            # Reads from the string one char at a time, until we find a null
            # Not the most efficient way of doing this, but whatever
            while True:
                c = self.read_int(1)
                if c == 0:
                    break
                buf.append(c)

        self.write("{}: \"{}\"\n".format(field_name, buf.decode('utf-8')), indent)
