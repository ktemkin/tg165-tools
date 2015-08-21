#!/usr/bin/env python3
#
# Quick, hackish scripts to pack/unpack TG165 upgrade binaries
#

import sys
from tg165.firmware_file import FirmwareFile

def usage():
    print("usage: {} [command] <input> <output>".format(sys.argv[0]))
    print("  command can be:")
    print("   pack - packs a raw binary into an upgrade binary")
    print("   unpack - unpacks an upgrade binary into a raw binary")

# If our args are wrong, print the usage.
if len(sys.argv) != 4:
    usage()
    sys.exit(0)

# Handle the relevant command.
if sys.argv[1] == "pack":
    firmware = FirmwareFile(sys.argv[2])
    firmware.to_upgrade_file(sys.argv[3])
elif sys.argv[1] == "unpack":
    firmware = FirmwareFile.from_upgrade_file(sys.argv[2])
    firmware.to_file(sys.argv[3])
else:
    usage()


