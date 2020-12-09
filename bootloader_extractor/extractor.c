/*
 * FLIR TG-165 bootloader fetcher (quick hack)
 *    Copyright (C) 2016 Kate J. Temkin <k@ktemkin.com>
 *
 * This file contains code from libopencm3:
 *    Copyright (C) 2013 Chuck McManis <cmcmanis@mcmanis.com>
 *    Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include <stdlib.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <string.h>

#include "ringbuf.h"

// The maximum packet size for the bulk endpoints for our ACM device.
#define MAX_PACKET_SIZE (64)

// Duration for a power-button press to be considered a long press.
// Units are arbitrary, but higher is longer. :)
#define LONG_PRESS_DURATION (0x10000)

usbd_device *usbdev;


/**
 * Stores a buffer for console communications with the host.
 */
static uint8_t raw_buffer[4096];
static struct ringbuf_t console_buffer;


static const struct usb_device_descriptor dev = {
  .bLength = USB_DT_DEVICE_SIZE,
  .bDescriptorType = USB_DT_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = USB_CLASS_CDC,
  .bDeviceSubClass = 0,
  .bDeviceProtocol = 0,
  .bMaxPacketSize0 = MAX_PACKET_SIZE,
  .idVendor = 0x0483,
  .idProduct = 0x5740,
  .bcdDevice = 0x0200,
  .iManufacturer = 1,
  .iProduct = 2,
  .iSerialNumber = 3,
  .bNumConfigurations = 1,
};

/*
 * This notification endpoint isn't implemented. According to CDC spec its
 * optional, but its absence causes a NULL pointer dereference in Linux
 * cdc_acm driver.
 */
static const struct usb_endpoint_descriptor comm_endp[] = {{
  .bLength = USB_DT_ENDPOINT_SIZE,
  .bDescriptorType = USB_DT_ENDPOINT,
  .bEndpointAddress = 0x83,
  .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
  .wMaxPacketSize = 16,
  .bInterval = 255,
}};

static const struct usb_endpoint_descriptor data_endp[] = {{
  .bLength = USB_DT_ENDPOINT_SIZE,
  .bDescriptorType = USB_DT_ENDPOINT,
  .bEndpointAddress = 0x01,
  .bmAttributes = USB_ENDPOINT_ATTR_BULK,
  .wMaxPacketSize = MAX_PACKET_SIZE,
  .bInterval = 1,
}, {
  .bLength = USB_DT_ENDPOINT_SIZE,
  .bDescriptorType = USB_DT_ENDPOINT,
  .bEndpointAddress = 0x82,
  .bmAttributes = USB_ENDPOINT_ATTR_BULK,
  .wMaxPacketSize = MAX_PACKET_SIZE,
  .bInterval = 1,
}};

static const struct {
  struct usb_cdc_header_descriptor header;
  struct usb_cdc_call_management_descriptor call_mgmt;
  struct usb_cdc_acm_descriptor acm;
  struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
  .header = {
    .bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
    .bDescriptorType = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_HEADER,
    .bcdCDC = 0x0110,
  },
  .call_mgmt = {
    .bFunctionLength = 
      sizeof(struct usb_cdc_call_management_descriptor),
    .bDescriptorType = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
    .bmCapabilities = 0,
    .bDataInterface = 1,
  },
  .acm = {
    .bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
    .bDescriptorType = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_ACM,
    .bmCapabilities = 0,
  },
  .cdc_union = {
    .bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
    .bDescriptorType = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_UNION,
    .bControlInterface = 0,
    .bSubordinateInterface0 = 1, 
   }
};

static const struct usb_interface_descriptor comm_iface[] = {{
  .bLength = USB_DT_INTERFACE_SIZE,
  .bDescriptorType = USB_DT_INTERFACE,
  .bInterfaceNumber = 0,
  .bAlternateSetting = 0,
  .bNumEndpoints = 1,
  .bInterfaceClass = USB_CLASS_CDC,
  .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
  .bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
  .iInterface = 0,

  .endpoint = comm_endp,

  .extra = &cdcacm_functional_descriptors,
  .extralen = sizeof(cdcacm_functional_descriptors)
}};

static const struct usb_interface_descriptor data_iface[] = {{
  .bLength = USB_DT_INTERFACE_SIZE,
  .bDescriptorType = USB_DT_INTERFACE,
  .bInterfaceNumber = 1,
  .bAlternateSetting = 0,
  .bNumEndpoints = 2,
  .bInterfaceClass = USB_CLASS_DATA,
  .bInterfaceSubClass = 0,
  .bInterfaceProtocol = 0,
  .iInterface = 0,

  .endpoint = data_endp,
}};

static const struct usb_interface ifaces[] = {{
  .num_altsetting = 1,
  .altsetting = comm_iface,
}, {
  .num_altsetting = 1,
  .altsetting = data_iface,
}};

static const struct usb_config_descriptor config = {
  .bLength = USB_DT_CONFIGURATION_SIZE,
  .bDescriptorType = USB_DT_CONFIGURATION,
  .wTotalLength = 0,
  .bNumInterfaces = 2,
  .bConfigurationValue = 1,
  .iConfiguration = 0,
  .bmAttributes = 0x80,
  .bMaxPower = 0x32,

  .interface = ifaces,
};

static const char *usb_strings[] = {
  "Not Exactly FLIR (TM)",
  "Bootloader Extractor",
  "ABCD",
};

/* Buffer to be used for control requests. */
uint8_t usbd_control_buffer[128];

static int cdcacm_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
    uint16_t *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
  (void)complete;
  (void)buf;
  (void)usbd_dev;

  switch(req->bRequest) {
  case USB_CDC_REQ_SET_CONTROL_LINE_STATE: {
    /*
     * This Linux cdc_acm driver requires this to be implemented
     * even though it's optional in the CDC spec, and we don't
     * advertise it in the ACM functional descriptor.
     */
    char local_buf[10];
    struct usb_cdc_notification *notif = (void *)local_buf;

    /* We echo signals back to host as notification. */
    notif->bmRequestType = 0xA1;
    notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
    notif->wValue = 0;
    notif->wIndex = 0;
    notif->wLength = 2;
    local_buf[8] = req->wValue & 3;
    local_buf[9] = 0;
    return 1;
    }
  case USB_CDC_REQ_SET_LINE_CODING: 
    if(*len < sizeof(struct usb_cdc_line_coding))
      return 0;

    return 1;
  }
  return 0;
}

static void console_putc(char c)  {
    while(ringbuf_is_full(&console_buffer))
      usbd_poll(usbdev);

    ringbuf_memcpy_into(&console_buffer, &c, 1);
}

static void console_puts(char * str) {
    size_t len = strlen(str);

    // TODO: Chunk up the string, rather than failing, here.
    // Since this is a quick hack that should never need this,
    // I'm not implementing this at the moment.
    if(len > ringbuf_capacity(&console_buffer)) {
        console_puts("OVERRUN!\r\n");
        return;
    }

    while(ringbuf_bytes_free(&console_buffer) < len)
      usbd_poll(usbdev);

    ringbuf_memcpy_into(&console_buffer, str, len);
}

/* make a nybble into an ascii hex character 0 - 9, A-F */
#define HEX_CHAR(x) ((((x) + '0') > '9') ? ((x) + '7') : ((x) + '0'))

/* send an 8 bit byte as two HEX characters to the console */
static void dump_byte(uint8_t b)
{
  console_putc(HEX_CHAR((b >> 4) & 0xf));
  console_putc(HEX_CHAR(b & 0xf));
}


/* send a 16 bit value as 4 hex characters to the console */
static void dump_word(uint16_t w)
{
    dump_byte(w >> 8);
    dump_byte(w & 0xFF);
}

/* send a 32 bit value as 8 hex characters to the console */
static void dump_long(uint16_t l)
{
    dump_word(l >> 16);
    dump_word(l & 0xFFFF);
}

/*
 * dump a 'line' (an address, 16 bytes, and then the
 * ASCII representation of those bytes) to the console.
 * Takes an address (and possiblye a 'base' parameter
 * so that you can offset the address) and sends 16
 * bytes out. Returns the address +16 so you can
 * just call it repeatedly and it will send the
 * next 16 bytes out.
 */
static uint8_t * dump_line(uint8_t *addr, uint8_t *base)
{
  uint8_t *line_addr;
  uint32_t tmp;

  // Start the checksum with the first byte we're sending:
  // the length of the record, 0x10 bytes.
  uint8_t checksum = 0x10;

  line_addr = addr;
  tmp = (uint32_t)line_addr - (uint32_t) base;
  
  // Print the intel hex header...
  console_puts(":10");

  // ... the current working address...
  dump_word((uint16_t)tmp);
  checksum += tmp >> 8;
  checksum += tmp & 0xFF;

  // ... that this is a data line ...
  console_puts("00");

  // ... the record itself...
  for (int i = 0; i < 16; i++) {
    uint8_t byte = *(line_addr+i);

    dump_byte(byte);
    checksum += byte;
  }

  // ... and finally, the checksum.
  dump_byte(~checksum + 1);

  console_puts("\r\n");
  return line_addr;
}

/*
 * dump a 'page' like the function dump_line except this
 * does 16 lines for a total of 256 bytes. Back in the
 * day when you had a 24 x 80 terminal this fit nicely
 * on the screen with some other information.
 */
static uint8_t * dump_page(uint8_t *addr, uint8_t *base)
{
  int i;
  for (i = 0; i < 16; i++) {
    dump_line(addr + (i * 16), base);
  }
  return addr;
}


static void dump_bootloader(void)
{
    uintptr_t addr;

    for(addr = 0x08000000; addr < 0x08010000; addr += 256)
        dump_page((uint8_t *)addr, (uint8_t *)0x08000000);

    // Send an EOF.
    console_puts(":00000001FF\r\n");
}

static void unknown_command(char c)
{
    console_puts("Unknown command (0x");
    dump_byte(c);
    console_puts(")!\r\n");
}

/**
 * Reads back the status of each port on each GPIO pin.
 *
 * Useful for identifying the GPIO pins coresponding to a given button
 * with the case closed.
 */
static void read_back_gpio(void)
{
    console_puts("Port ");

    uint32_t gpioports[] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE };
    size_t num_gpio = (sizeof(gpioports) / sizeof(gpioports[0]));

    // Read back each of our GPIO ports.
    for(size_t i = 0; i < num_gpio; ++i) {
        console_putc('A' + i );
        console_puts(": ");
        dump_long(gpio_port_read(gpioports[i]));
        console_putc(' ');
    }

    console_puts("\r\n");
}

static void print_help(void) {
    console_puts("d: dump bootloader\r\n");
    console_puts("r: reset device\r\n");
    console_puts("g: read all GPIO\r\n");
    console_puts("h: this help message\r\n");
    console_puts("\r\n");
}

static void handle_command(char c)
{
    switch(c) {
        case 'd':
        case 'D':
          dump_bootloader();
          return;
        case 'r':
        case 'R':
          scb_reset_system();
          return;
        case 'g':
        case 'G':
          read_back_gpio();
          return;
        case 'h':
        case 'H':
          print_help();
          return;
        case 0x0D: // enter
          return;
        default:
          unknown_command(c);
          return;
    }
}

/**
 * Called when the host has sent data to us.
 */
static void cdcacm_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
  (void)ep;

  char buf[MAX_PACKET_SIZE];
  int len = usbd_ep_read_packet(usbd_dev, ep, buf, 64);

  // Perform a nullary write to the endpoint. This marks the relevant
  // interrupt as serviced (and does not send a ZLP).
  usbd_ep_write_packet(usbdev, 0x82, NULL, 0);

  // Handle each command present in the relevant data.
  for(int i = 0; i < len; ++i) {
      handle_command(buf[i]);
  }
}

/**
 * Called when we're ready to accept new serial data.
 */
static void cdcacm_tx_ready_cb(usbd_device *usbd_dev, uint8_t ep)
{
    (void)ep;

    size_t to_transmit = ringbuf_bytes_used(&console_buffer);

    // Perform a nullary read from the endpoint; this marks the
    // relevant 'interrupt' as serviced.
    usbd_ep_read_packet(usbd_dev, ep, NULL, 0);

    // If we don't have any data to send, return without transmitting.
    if(to_transmit == 0)
        return;

    // If we can't send this all in one packet, send as much as we can.
    if(to_transmit > MAX_PACKET_SIZE)
        to_transmit = MAX_PACKET_SIZE;

    // Get the data to be transmitted...
    uint8_t buf[MAX_PACKET_SIZE];
    ringbuf_memcpy_from(buf, &console_buffer, to_transmit);

    //... and transmit it.
    usbd_ep_write_packet(usbd_dev, 0x82, buf, to_transmit);
}



static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
  (void)wValue;

  usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, MAX_PACKET_SIZE, cdcacm_data_rx_cb);
  usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, MAX_PACKET_SIZE, cdcacm_tx_ready_cb);
  usbd_ep_setup(usbd_dev, 0x83, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

  usbd_register_control_callback(
        usbd_dev,
        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        cdcacm_control_request);
}

static void setup_gpio(void)
{
    // Enable the clocks for every GPIO port, as we'll use them all during
    // readback. Don't set up any GPIO beyond that.
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOD);
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
    // Set up use of the system's external crystal, as the 103VE series requires
    // an external crystal to drive the USB PLL.
    rcc_clock_setup_in_hse_8mhz_out_72mhz();

    // Set up our GPIO and console.
    setup_gpio();
    ringbuf_init(&console_buffer, raw_buffer, sizeof(raw_buffer));

    // Enable clocking for the resources we'll be using.
    rcc_periph_clock_enable(RCC_AFIO);

    // Ensure SWD is enabled and JTAG is not, as that's what we have test points
    // for on the TG165.
    AFIO_MAPR |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON;

    // Start up our USB device controller...
    usbdev = usbd_init(&st_usbfs_v1_usb_driver, &dev, &config, usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
    usbd_register_set_config_callback(usbdev, cdcacm_set_config);


    // Waiting a moment seems to prevent itermittant enumeration issues.
    for (int i = 0; i < 800000; i++)
      __asm__("nop");

    // Finally, turn on the USB pull-up to signal that we're ready to connect.
    gpio_clear(GPIOE, GPIO0);

    while (1) {
        handle_long_press();
        usbd_poll(usbdev);
    }
}
