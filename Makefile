#
# Simple (hackish) coordination makefile that produces an Upgrade.bin
# with an example software stack.
#
# Assumes that you've copied the original firmware into this directory
# under the filename Upgrade.orig.bin.
#

LINKER_SCRIPT = libopencm3/lib/libopencm3_stm32f1.ld

all: Upgrade.bin

Upgrade.bin: example_layout.yaml boot_select/bootsel.bin bootloader_extractor/extractor.bin alt_bootloader/usbdfu.bin
	python3 ./compose-fw.py example_layout.yaml

boot_select/bootsel.bin: boot_select/bootsel.S boot_select/bootsel.ld $(LINKER_SCRIPT)
	$(MAKE) -C boot_select

alt_bootloader/usbdfu.bin: alt_bootloader/usbdfu.c alt_bootloader/usbdfu.ld $(LINKER_SCRIPT)
	$(MAKE) -C alt_bootloader

bootloader_extractor/extractor.bin: bootloader_extractor/extractor.c bootloader_extractor/extractor.ld $(LINKER_SCRIPT)
	$(MAKE) -C bootloader_extractor

$(LINKER_SCRIPT):
	git submodule init
	git submodule update
	$(MAKE) -C libopencm3

