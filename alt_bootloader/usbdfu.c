/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dfu.h>

/*
 * Duration for a power-button press to be considered a long press.
 * Units are arbitrary, but higher is longer. :)
 */
#define LONG_PRESS_DURATION (0x10000)

/*
 * Memory address at which we should allow writes to begin.
 * This should match the DFuSe descriptor string below.
 *
 * This serves as a sanity check to make sure a bad DFU application
 * can't inadvertantly make life difficult for people by erasing code
 * we want to keep. For now, it's fixed to prohibit erasing anything
 * but the alternate firmware.
 */
#define DISALLOW_WRITES_BEFORE (0x08053000)

/* The page size for the TG165's STM32F103VE. */
#define PAGE_SIZE 2048

/* Commands sent with wBlockNum == 0 as per ST implementation. */
#define CMD_SETADDR 0x21
#define CMD_ERASE   0x41

/* We need a special large control buffer for this device: */
uint8_t usbd_control_buffer[1024];

static enum dfu_state usbdfu_state = STATE_DFU_IDLE;

static struct {
    uint8_t buf[sizeof(usbd_control_buffer)];
    uint16_t len;
    uint32_t addr;
    uint16_t blocknum;
} prog;

const struct usb_device_descriptor dev = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,
    .idProduct = 0xDF11,
    .bcdDevice = 0x0200,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const struct usb_dfu_descriptor dfu_function = {
    .bLength = sizeof(struct usb_dfu_descriptor),
    .bDescriptorType = DFU_FUNCTIONAL,
    .bmAttributes = USB_DFU_CAN_DOWNLOAD | USB_DFU_WILL_DETACH,
    .wDetachTimeout = 255,
    .wTransferSize = 1024,
    .bcdDFUVersion = 0x011A,
};

const struct usb_interface_descriptor iface = {
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 0,
    .bInterfaceClass = 0xFE, /* Device Firmware Upgrade */
    .bInterfaceSubClass = 1,
    .bInterfaceProtocol = 2,
    .iInterface = 4,

    .extra = &dfu_function,
    .extralen = sizeof(dfu_function),
};

const struct usb_interface ifaces[] = {{
    .num_altsetting = 1,
    .altsetting = &iface,
}};

const struct usb_config_descriptor config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0,
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0xC0,
    .bMaxPower = 0x32,

    .interface = ifaces,
};

static const char *usb_strings[] = {
    "Not Exactly FLIR",
    "DFU Bootloader",
    "ABCD",

    /* This string is used by ST Microelectronics' DfuSe utility. */
    /* It encodes the regions of memory, whether DFU should be able to read/
     * write to them, and their page sizes. Here, we mark most of memory
     * read-only, but mark the alternate firmware area as programmable */
    "@Internal Flash   /0x08000000/166*002Ka,90*002Kg",
};

static uint8_t usbdfu_getstatus(uint32_t *bwPollTimeout)
{
    switch (usbdfu_state) {
    case STATE_DFU_DNLOAD_SYNC:
        usbdfu_state = STATE_DFU_DNBUSY;
        *bwPollTimeout = 100;
        return DFU_STATUS_OK;
    case STATE_DFU_MANIFEST_SYNC:
        /* Device will reset when read is complete. */
        usbdfu_state = STATE_DFU_MANIFEST;
        return DFU_STATUS_OK;
    default:
        return DFU_STATUS_OK;
    }
}

static void usbdfu_getstatus_complete(usbd_device *usbd_dev, struct usb_setup_data *req)
{
    int i;
    (void)req;
    (void)usbd_dev;

    switch (usbdfu_state) {
    case STATE_DFU_DNBUSY:
        flash_unlock();
        if (prog.blocknum == 0) {
            switch (prog.buf[0]) {
            case CMD_ERASE:
                {
                    uint32_t *dat = (uint32_t *)(prog.buf + 1);

                    if(*dat >= DISALLOW_WRITES_BEFORE) {
                        flash_erase_page(*dat);
                    }
                }
            case CMD_SETADDR:
                {
                    uint32_t *dat = (uint32_t *)(prog.buf + 1);
                    prog.addr = *dat;
                }
            }
        } else {
            uint32_t baseaddr = prog.addr + ((prog.blocknum - 2) *
                       dfu_function.wTransferSize);
            for (i = 0; i < prog.len; i += 2) {
                uint16_t *dat = (uint16_t *)(prog.buf + i);

                if(baseaddr + i >= DISALLOW_WRITES_BEFORE) {
                    flash_program_half_word(baseaddr + i, *dat);
                }
            }
        }
        flash_lock();

        /* Jump straight to dfuDNLOAD-IDLE, skipping dfuDNLOAD-SYNC. */
        usbdfu_state = STATE_DFU_DNLOAD_IDLE;
        return;
    case STATE_DFU_MANIFEST:
        /* USB device must detach, we just reset... */
        scb_reset_system();
        return; /* Will never return. */
    default:
        return;
    }
}

static int usbdfu_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
        uint16_t *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
    (void)usbd_dev;

    if ((req->bmRequestType & 0x7F) != 0x21)
        return 0; /* Only accept class request. */

    switch (req->bRequest) {
    case DFU_DNLOAD:
        if ((len == NULL) || (*len == 0)) {
            usbdfu_state = STATE_DFU_MANIFEST_SYNC;
            return 1;
        } else {
            /* Copy download data for use on GET_STATUS. */
            prog.blocknum = req->wValue;
            prog.len = *len;
            memcpy(prog.buf, *buf, *len);
            usbdfu_state = STATE_DFU_DNLOAD_SYNC;
            return 1;
        }
    case DFU_CLRSTATUS:
        /* Clear error and return to dfuIDLE. */
        if (usbdfu_state == STATE_DFU_ERROR)
            usbdfu_state = STATE_DFU_IDLE;
        return 1;
    case DFU_ABORT:
        /* Abort returns to dfuIDLE state. */
        usbdfu_state = STATE_DFU_IDLE;
        return 1;
    case DFU_UPLOAD:
        /* Upload not supported for now. */
        return 0;
    case DFU_GETSTATUS: {
        uint32_t bwPollTimeout = 0; /* 24-bit integer in DFU class spec */
        (*buf)[0] = usbdfu_getstatus(&bwPollTimeout);
        (*buf)[1] = bwPollTimeout & 0xFF;
        (*buf)[2] = (bwPollTimeout >> 8) & 0xFF;
        (*buf)[3] = (bwPollTimeout >> 16) & 0xFF;
        (*buf)[4] = usbdfu_state;
        (*buf)[5] = 0; /* iString not used here */
        *len = 6;
        *complete = usbdfu_getstatus_complete;
        return 1;
        }
    case DFU_GETSTATE:
        /* Return state with no state transision. */
        *buf[0] = usbdfu_state;
        *len = 1;
        return 1;
    }

    return 0;
}

static void usbdfu_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
    (void)wValue;

    usbd_register_control_callback(
                usbd_dev,
                USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
                USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
                usbdfu_control_request);
}

static void setup_gpio(void)
{
    // Enable the clocks for every GPIO port, as we'll use them all during
    // readback. Don't set up any GPIO beyond that.
    rcc_periph_clock_enable(RCC_GPIOE);

    /// Start with the USB pull-up disabled, so we don't trigger a connection until we're ready.
    gpio_set(GPIOE, GPIO0);
    gpio_set_mode(GPIOE, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO0);
}

static bool power_button_pressed(void)
{
    return !gpio_get(GPIOB, GPIO1);
}

static void handle_long_press(void) {
    static size_t press_duration = 0;

    if(power_button_pressed()) {
        ++press_duration;

        if(press_duration > LONG_PRESS_DURATION) {
          scb_reset_system();
        }
    } else {
        press_duration = 0;
    }
}

int main(void)
{
    usbd_device *usbd_dev;

    // Set up use of the system's external crystal, as the 103VE series requires
    // an external crystal to drive the USB PLL.
    rcc_clock_setup_in_hse_8mhz_out_72mhz();

    // Set up our GPIO and console.
    setup_gpio();

    // Enable clocking for the resources we'll be using.
    rcc_periph_clock_enable(RCC_AFIO);

    // Ensure SWD is enabled and JTAG is not, as that's what we have test points
    // for on the TG165.
    AFIO_MAPR |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON;

    // Start up our USB device controller...
    usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev, &config, usb_strings, 4, usbd_control_buffer, sizeof(usbd_control_buffer));
    usbd_register_set_config_callback(usbd_dev, usbdfu_set_config);

    // Waiting a moment seems to prevent itermittent enumeration issues.
    for (int i = 0; i < 800000; i++)
      __asm__("nop");

    // Finally, turn on the USB pull-up to signal that we're ready to connect.
    gpio_clear(GPIOE, GPIO0);

    while (1) {
        handle_long_press();
        usbd_poll(usbd_dev);
    }

}
