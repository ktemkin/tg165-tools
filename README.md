# TG165 Tools

This repostiory contains tools for extending the functionality of the low-end FLIR TG165 thermal camera. With these tools, you can add alternate functionality to your TG165 _without_ having to replace its original firmware.

To this end, the repository provides a few hopefully-useful tools:

* A simple utility (```fwutil.py```) and python module (```tg165```) that can pack and unpack FLIR Upgrade.bin firmware images.
* A simple utility (```compose-fw.py```) that can be used to build firmware-upgrade files that contain multiple programs.
* A simple assembly bootstrap (```boot_select```) that allows you to select between multiple programs on device startup.
* A DFU "alternate-bootloader" (```alt_bootloader```) that allows you to upload custom programs via USB without distruping the main one. This should enable rapid development!

And some tools which are probably less useful to most people:

* An (example) firmware payload that allows you to dump the TG165's FLIR-provided bootloader.

### The FLIR what-now?

The FLIR TG165 is an relatively inepensive, low-resolution thermal camera built around FLIR's Lepton sensor. Unlike many of FLIR's more expensive offerings, the TG165 is centered around a simple usb-enabled Cortex-M Microcontroller, the STM32F103VE. Luckily for us, the flash of the microcontroller has a ton of free space (around 200KiB, of which 180KiB is easily accessible!), so there's plenty of room to shoehorn in different alternate applications.

### What will I need to use these tools?

* A FLIR TG165. Right now, this software is compatible with all known hardware revisions.
* A FLIR TG165 upgrade image, which is available from [FLIR's website](http://support.flir.com/SwDownload/Assets/TestandMeasurement/TG165_Firmware_1.5.3_plus_Instructions.zip).
* An ARM toolchain that can target baremetal Cortex-Ms. These are commonly available with the package name "arm-none-eabi-gcc" in your OS's repositories.
* python3 and pip. Sorry, but it has much better support for working with binary data than Python 2.7.

### Is this dangerous?

Any "hardware hacking" project always comes with a risk to your hardware-- but this project should be pretty safe. The main risk comes from putting untested user software on the TG165-- under certain conditions, bad firmware can "freeze" the device. As the device has a soft power button, you could have to wait for the battery to die to get back to the bootloader, which might result in a very boring few hours. It's suggested you set up the STM32F's independent watchdog in your programs to avoid this danger-of-bordedom.

You're also cautioned not to unlock the flash programming registers unless you're willing to solder wires to test points. Fortunately, this is desiged to be difficult to do by accident. If you manage to erase FLIR's bootloader, the path to recovery requires you to either attach via JTAG/SWD or use the rom-resident UART bootloader.

## How do I get started?

Before you begin, place the ```Upgrade.bin``` you downloaded from FLIR into your working directory. For our examples, we'll rename this file to ```Upgrade.orig.bin```, as that's the name used in our configuration file.

Next, let's make sure you have the necessary python modules to run our scripts:

```sh
$ python3 -m pip install -r requirements.txt
```

You can now build an example ```Upgrade.bin``` file just by running Make:

```sh
$ make
```

If everything went well, you should have an new ```Upgrade.bin``` file sitting in your repository. This is a new firmware image that contains several parts:

* A small boot-time selector that allows you to select which firmware image runs.
* The original FLIR fimrware from your Upgrade.bin.
* An "alternate firmware" application. This example contains a demonstration application that can dump your TG165's bootloader over USB.
* An "alternate bootloader", which can help you to reprogram just the "alternate firmware section".

Next, you're ready to try flashing your new firmware image! To do so, copy your new ```Upgrade.bin``` to the root of your TG165's SD card:

```sh
$ cp Upgrade.bin /media/IRM_CCSA_X6/
$ sync && eject /media/IRM_CCSA_X6
```

Place the SD card back into the TG165, if you used an external reader. Next, we'll install the image. Restart into the bootloader by pressing and holding both POWER and DOWN for about ten seconds. You should see the following messages:

```
Upgrade firmware V1.0.3
Initialize SD Card
Initialize file system
Open the firmware
Check the firmware
Erase hardware flash
Write firmware data
Upgrading success
Please reboot device
```

Once this process is complete, we're ready to test our new image:

* First, boot into the normal FLIR firmware by rebooting the machine while _not_ pressing UP or OK. You should be presented with the normal behavior: the camera should alternate between displaying thermal readings and acting as a USB Mass Storage device.
* Next, we'll try booting into our example program. Reboot the machine by pressing OK and POWER at the same time. If you connect the camera to your computer via USB, you should see a CDC/ACM deice attach with a VID/PID of 0x0483:0x5740. This is our example program working!
* Finally, we'll test booting into the Alternate Bootloader. With the machine connected via USB, reboot the machine by pressing and holding POWER, OK, and UP. You should see a USB DFU device enumerate! See the "Using the Alternate Bootloader" setion for information on how to use this.

### Okay, but what did that do?

Let's walk through the contents of the provided Makefile:

```make
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
```

As Make handles dependencies for us, thse sections are actually exected in reverse order. First, we build [libopencm3](http://libopencm3.org), which generates both the linker script and libraries used in the DFU bootloader and example program. This essentially just pulls down libopencm3 and builds it:

```make
$(LINKER_SCRIPT):
	git submodule init
	git submodule update
	$(MAKE) -C libopencm3
```

Next, we build the "boot selector" binary, which runs almost immediately after the TG165 is reset (right after the FLIR bootloader), and determines which
firmware image is executed. Note that we essentially just Make in the relevant directory.

```make
boot_select/bootsel.bin: boot_select/bootsel.S boot_select/bootsel.ld $(LINKER_SCRIPT)
	$(MAKE) -C boot_select
```

We do the same for our "alternate bootloader" and "alternate firmware":

```make
alt_bootloader/usbdfu.bin: alt_bootloader/usbdfu.c alt_bootloader/usbdfu.ld $(LINKER_SCRIPT)
	$(MAKE) -C alt_bootloader

bootloader_extractor/extractor.bin: bootloader_extractor/extractor.c bootloader_extractor/extractor.ld $(LINKER_SCRIPT)
	$(MAKE) -C bootloader_extractor
```

Finally, we need to stich the firmware files and original firmware (```Upgrade.orig.bin```) into a single image. This is accomplished by running the ```compose-fw``` script:

```
./compose-fw.py example_layout.yaml
```

This script accepts description from the provided layout YAML document, which describes how it should stitch files together into a final firmware image. The ```example_layout.yaml``` file contains documentation on how these scripts work.


### 'Recommended' load addresses and default sizes

Currently, the boot selector and linker scripts assume the following load addresses:

| Component            | Load Address | Default Size         | Notes  |
| ---------------------|--------------|----------------------|--------|
| FLIR Bootloader      | 0x08000000   | 64k (20k unused)     | Not included in Upgrade.bin files, which start at 0x08010000 |
| FLIR Main Program    | 0x08010000   | 235,024B / 229KiB    | Linked to load at 0x08010000, so we don't move it. \
| Boot Selector        | 0x08050000   | 80B                  | The firmware section that follows this must follow vector table alignment rules.
| Alt Bootloader       | 0x08050100   | 6,276B               | |
| Alt Firmware         | 0x08053000   | up to 180KiB         | |

(If more space is needed, addresses can be shifted, bitmap images from the main firmware can probably be trounced, and there's a free 20k in the FLIR bootloader region.)

### Using the Alternate Bootloader

The "alternate bootloader" is a DfuSe-compatible application that allows you to program the TG165's "alternate firmware" over USB using the STM DfuSe Device Firmware Update (DFU) protocol. The alt-bootloader is designed such that it can only program the alternate firmware image, ensuring you won't accidentally erase the bootloader, main program, or itself. It's thus perfect for rapid development!

To enter the alt-bootloader, restart into DFU mode by holding OK, UP, and POWER for a few seconds. Once in the DFU bootloader, you can program the relevant flash sections as you would any other DFU device, e.g. with the [dfu-util utility](http://dfu-util.sourceforge.net/). 

For example, to program an alternate firmware binary linked at ```0x08053000``` using dfu-util, one might execute the following commands:

```sh
dfu-util -s 0x08053000:leave -D my_binary.bin
```

The device will automatically restart once programming is complete. If one holds OK while the programming occurs, this restart will automatically load the newly-loaded Alternate Firmware.

### More Information

More detailed hardware information / documentation can be found [in the Wiki](https://github.com/ktemkin/tg165-tools/wiki).

### Contributions && Authors

Contributions are always welcome. Feel free to open Issues or Pull Requests on GitHub.

Major Contributors:
* Kate J. Temkin (@ktemkin)
