#!/usr/bin/env python3

import sys

from intelhex import IntelHex
from serial import Serial
from io import StringIO

def read_bootloader_ihex(port_name):
    """
    Reads the bootloader .text from the extractor running on the device.

    return: A string containing the ihex file contents.
    """

    lines = []

    # Open a connection to the bootloader-extractor firmware.
    sp = Serial(port_name, timeout=1)

    # Ask the target device to dump...
    sp.write(b'd')

    while True:

        # Attempt to read one line of the provided intel hex file.
        line = sp.read(45)
        lines.append(line)

        if line == b":00000001FF\r\n":
            break
        if line == '':
            break

    sublines = [line.decode() for line in lines]
    return ''.join(sublines)


def usage():
    print("usage: {} <serial_port> <bootloader_filename>".format(sys.argv[0]))


# Ensure we have proper-ish arguments.
if len(sys.argv) != 3:
    usage()
    sys.exit(0)

serial_port = sys.argv[1]
out_file    = sys.argv[2]

# Read the bootloader into an intel hex file...
raw_ihex = read_bootloader_ihex(sys.argv[1])

# ... and produce the output binary.
ihex = IntelHex(StringIO(raw_ihex))
ihex.tobinfile(out_file)


