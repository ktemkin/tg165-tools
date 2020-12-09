

CROSS_COMPILE ?= arm-none-eabi-
CC            := $(CROSS_COMPILE)gcc

BINARY        := bootsel
LINKER_FILE   := bootsel.ld
CFLAGS        := -mcpu=cortex-m3 -mthumb -nostartfiles -ffreestanding -T $(LINKER_FILE)


all: $(BINARY).bin

.PHONY: clean
.SECONDARY: $(BINARY).elf

%.elf: %.S
	$(CC) $(CFLAGS) $< -o $@

%.bin: %.elf
	arm-none-eabi-objcopy -O binary $< $@

clean:
	rm -f *.elf *.bin
