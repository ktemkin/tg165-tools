#
# Object representing a TG165 firmware file
# Author: Kate J. Temkin <k@ktemkin.com>
#

import io
import crc16

class FirmwareFile(object):
    """
    Represents a TG165 firmware file.
    """

    # The size of a data chunk in an upgrade file, which contains
    # a kilobyte of data, 2 bytes of padding, and 2 bytes of checksum.
    UPGRADE_BIN_DATA_SIZE = 1024
    UPGRADE_BIN_SIZE_WITH_METADATA = 1028

    # By default, TG165 programs start at 0x08010000; right past the bootloader.
    DEFAULT_LOAD_ADDRESS = 0x08010000


    def __init__(self, raw_bytes=None, load_address=DEFAULT_LOAD_ADDRESS):
        """
        Sets up a new firmware file.

        raw_bytes: A bytearray containing the raw contents of this
            firmware file, a collection of bytes, an object that supports read(),
            a filename to be read into this object. If not provied, an empty

        load_address: The address at which this firmware file is expected
            to be loaded.
        """

        if raw_bytes is None:
            raw_bytes = bytearray()

        self.raw_bytes = self._bytearray_from_file_or_bytes(raw_bytes)
        self.load_address = load_address


    @staticmethod
    def _bytearray_from_file_or_bytes(file_or_bytes):
        """
        Convenience function that accepts a 'polymorphic'
        object (which can be any object that supports read,
        a string filename, or a collection of bytes).
        """

        if isinstance(file_or_bytes, bytes):
            return bytearray(file_or_bytes)
        elif isinstance(file_or_bytes, bytearray):
            return file_or_bytes
        elif isinstance(file_or_bytes, str):
            with open(file_or_bytes, "rb") as file:
                return bytearray(file.read())
        else:
            return bytearray(file_or_bytes.read())


    @classmethod
    def from_upgrade_file(cls, file_or_bytes):
        """
        Factory method that creates a FirmwareFile from a FLIR Upgrade.bin.
        """

        bytes_in = cls._bytearray_from_file_or_bytes(file_or_bytes)

        source = io.BytesIO(bytes_in)
        target = io.BytesIO()

        for chunk in cls.__read_in_blocks(source, cls.UPGRADE_BIN_SIZE_WITH_METADATA):

            # Every data chunk starts with two bytes of CRC16, then
            # two bytes of padding. The remainder of a chunk is data.
            checksum = chunk[0:2]
            padding  = chunk[2:4]
            data     = chunk[4:]

            # Check to make sure our padding are always zeroes.
            if padding != b"\x00\x00":
                issue = repr(padding)
                raise IOError("Data format error! Expected 0x0000, got {}\n".format(issue))

            # Check to make sure the CRCs are valid.
            data_crc = crc16.crc16xmodem(data).to_bytes(2, byteorder='little')
            if checksum != data_crc:
                expected = repr(checksum)
                actual = repr(data_crc)
                raise IOError("CRC mismatch! Expected {}, got {}\n".format(expected, actual))

            # Copy the unpacked data into our target object.
            target.write(data)

        return FirmwareFile(target.getvalue())


    @staticmethod
    def __read_in_blocks(source, block_size):
        """
        Simple helper iterator that iterates over a function in chunks.

        source: The source file.
        chunk: The size of the chunk to be read. The final chunk may be smaller.
        """
        while True:
            chunk = source.read(block_size)
            if chunk:
                yield chunk
            else:
                return


    def __len__(self):
        """ Returns the length of this firmware file, unencoded, in bytes. """
        return len(self.raw_bytes)


    def pad_to_length(self, new_length, padding_byte=b"\x00"):
        """
        Pads the firmware file out to a given length.

        new_length: The new (miniumum) length of this firmware file.
        padding_byte: The byte to use for padding. Optional.
        """

        necessary_padding = new_length - len(self)

        # If no padding is necessary, return without modification.
        if necessary_padding <= 0:
            return

        # Otherwise, pad out to length.
        self.raw_bytes = self.raw_bytes + (padding_byte * necessary_padding)


    def merge_in(self, new_firmware_file, load_address=None):
        """
        Merges a new firmware file into the current firmware.

        new_firmware_file: The new firmware file to be loaded in, or any
            object that can be accepted by FirmwareFile's constructor.
        load_address: Necessary if new_firmware_file isn't a firmware file.
            Specifies the load address at which the new file will be added.
        """

        if not isinstance(new_firmware_file, FirmwareFile):
            new_firmware_file = FirmwareFile(new_firmware_file, load_address)

        if load_address is None:
            load_address = new_firmware_file.load_address

        # Determine the load address as a byte offset into this file.
        relative_load_address = load_address - self.load_address

        # TODO: Figure out if there's a sane way to handle merging in a file with
        # an earlier load address. Is it obvious what the user wants?
        if relative_load_address < 0:
            raise ValueError("Cannot merge in a firmware with an earlier load address!")

        # Ensure we're at least long enough to accept the new firmware...
        self.pad_to_length(relative_load_address)

        # ... and merge it in!
        end_address = relative_load_address + len(new_firmware_file)
        self.raw_bytes[relative_load_address:end_address] = new_firmware_file.raw_bytes


    def set_entry_point(self, new_entry_point):
        """
        Patches the given firmware's vector table, setting its entry point.

        new_entry_point: The address of the firmware's new entry point.
        """

        if isinstance(new_entry_point, int):

            # Cortex-M addresses always point to thumb instructions, and thus
            # have an MSB of 1.
            new_entry_point = new_entry_point | 0x01;
            new_entry_point = new_entry_point.to_bytes(4, 'little')

        self.raw_bytes[4:8] = new_entry_point


    def get_entry_point(self):
        """
        Returns the entry point for the given module, assuming this is a
        valid Cortex-M3 firmware file.
        """
        raw_entry_point = self.raw_bytes[4:8]
        entry_point = int.from_bytes(raw_entry_point, 'little')

        # Cortex-M addresses always point to thumb instructions, and thus
        # are stored with an MSB of 1. We convert back to a raw address.
        return entry_point & 0xFE


    def to_file(self, file_or_filename):
        """
        Writes the given firmware file out to disk.

        file_or_filename: The filename that should be written, or
            a file-like object to write to.
        """

        if isinstance(file_or_filename, str):
            target = open(file_or_filename, 'wb')
            target.write(self.raw_bytes)
            target.close()
        else:
            target.write(self.raw_bytes)


    def to_upgrade_file(self, file_or_filename):
        """
            Packs a raw binary into a TG165 image.

            file_or_filename: The filename that should be written, or
                a file-like object to write to.
        """

        close_needed = False
        source = io.BytesIO(self.raw_bytes)

        if isinstance(file_or_filename, str):
            target = open(file_or_filename, 'wb')
            close_needed = True
        else:
            target = file_or_filename

        for data in self.__read_in_blocks(source, self.UPGRADE_BIN_DATA_SIZE):
            # Compute the CRC of the chunk.
            data_crc = crc16.crc16xmodem(data).to_bytes(2, byteorder='little')

            # Write the chunk with checksum in the FLIR-expected format.
            target.write(data_crc)
            target.write(b"\x00\x00")
            target.write(data)

        if close_needed:
            target.close()
