#!/usr/bin/env python3
# Copyright 2023 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import sys

def usage():
    print(
        'Usage:\n'
        '  regen.py INPUT OUT.in OUT.attrib\n'
        '    INPUT       json file containing the magma interface definition\n'
        '    OUT.in      destination path for the magma interface file to generate\n'
        '    OUT.attrib  destination path for the magma interface file to generate\n'
        '  Example: ./regen.py ./magma.json ./magma.in ./magma.attrib\n'
        '  Generates magma interface and attrib files based on a provided json definition.')

# returns a string that will block presubmit
def dnsstr():
    return (
        'DO '
        'NOT '
        'SUBMIT'
    )

def format_export(export):
    args = [f'{arg["type"]} {arg["name"]}' for arg in export["arguments"]]
    return f'MAGMA({export["type"]}, {export["name"]}, {", ".join(args)})'

def if_header():
    return (
        f'# !!!! {dnsstr()} !!!!\n'
        '#   This interface file was generated using the magma\n'
        '#   definition file. Some methods with nested structs must\n'
        '#   be manually serialized/deserialized and so modifications\n'
        '#   must be made to those method signatures prior to running\n'
        '#   emugen!\n'
        f'# !!!! {dnsstr()} !!!!\n'
        '\n'
    )

def attrib_header():
    return (
        f'# !!!! {dnsstr()} !!!!\n'
        '#   This attrib file was generated using HEURISTICS based on\n'
        '#   the magma definition file. It must be manually verified\n'
        '#   prior to submission!\n'
        f'# !!!! {dnsstr()} !!!!\n'
        '\n'
        '# For documentation on the .attrib file format, see:\n'
        '# android/android-emugl/host/tools/emugen/README\n'
        '\n'
        'GLOBAL\n'
	    '\tbase_opcode 100000\n'
	    '\tencoder_headers <stdint.h>\n'
    )

def format_attribs(export):
    out_args = [arg for arg in export["arguments"] if arg["name"].endswith('_out')]
    if len(out_args) == 0:
        return None
    ret = f'{export["name"]}\n'
    for arg in out_args:
        ret += f'\tdir {arg["name"]} out\n'
        ret += f'\tlen {arg["name"]} sizeof({arg["type"][:-1]})\n'
    return ret

def main():
    if (len(sys.argv) != 4):
        usage()
        return 2
    try:
        with open(sys.argv[1], 'r') as file:
            with open(sys.argv[2], 'w') as dest_in:
                with open(sys.argv[3], 'w') as dest_attrib:
                    magma = json.load(file)['magma-interface']
                    lines = [format_export(e) for e in magma['exports']]
                    dest_in.write(if_header())
                    dest_in.write('\n'.join(lines))
                    dest_attrib.write(attrib_header())
                    for export in magma['exports']:
                        attribs = format_attribs(export)
                        if (attribs):
                            dest_attrib.write('\n' + attribs)

    except Exception as e:
        print(f'Error accessing files: {e}')
        usage()
        return 1

if __name__ == '__main__':
    sys.exit(main())
