#!/usr/bin/env python3
#
# Quick, hackish scripts to extend FLIR firmware images with custom functionality
#

import sys
from tg165.firmware_file import FirmwareFile

SELECTOR_LOCATION = 0x08050000
ALT_FW_LOCATION   = 0x08054000

def usage():
    print("usage: {} <Upgrade.bin> <bootsel_firmware> <additional_firmware> <output>".format(sys.argv[0]))

# If our args are wrong, print the usage.
if len(sys.argv) != 5:
    usage()
    sys.exit(0)

#TODO: generalize using a YAML input file
selector_fw = FirmwareFile(sys.argv[2], SELECTOR_LOCATION)

firmware = FirmwareFile.from_upgrade_file(sys.argv[1])
firmware.merge_in(selector_fw)
firmware.merge_in(sys.argv[3], ALT_FW_LOCATION)
firmware.set_entry_point(SELECTOR_LOCATION)
firmware.to_upgrade_file(sys.argv[4])
