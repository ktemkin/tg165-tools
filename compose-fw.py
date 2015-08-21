#!/usr/bin/env python3
#
# Utility to create TG165 firmware images.
#

import sys
import yaml

from tg165.firmware_file import FirmwareFile

def usage():
    print("usage: {} <layout.yaml>".format(sys.argv[0]))
    print("  see example_layout.yaml in the repo for an example")

# If we don't have the right amount of arguments, print our usage.
if len(sys.argv) != 2:
    usage()
    sys.exit(0)


# Parse our in layout description.
with open(sys.argv[1], 'r') as file:
    layout = yaml.load(file)

# Load our base firmware file, and adjust its entry point.
firmware = FirmwareFile.from_upgrade_file(layout['original_firmware'])
firmware.set_entry_point(layout['entry_point'])

# Merge in all of our input files.
for entry in layout['input']:
    try:
        format = entry['format']
    except KeyError:
        format = 'binary'

    format = format.lower()

    # Load the new firmware according to its format.
    if format == 'binary':
        new_firmware = FirmwareFile(entry['filename'], entry['load_address'])
    else:
        new_firmware = FirmwareFile.from_upgrade_file(entry['filename'], entry['load_address'])

    # Merge in the new firmware.
    firmware.merge_in(new_firmware)


# Produce each of our output files.
for entry in layout['output']:
    format = entry['format'].lower()

    if format == "upgrade.bin":
        firmware.to_upgrade_file(entry['filename'])
    elif format == "binary":
        firmware.to_file(entry['filename'])
    else:
        sys.stderr.write("Skipping entry with unknown filetype {}!\n".format(format))
