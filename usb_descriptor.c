#include "tusb.h"

tusb_desc_device_t const device_descriptor = {
	.bLength = sizeof(tusb_desc_device_t),
	.bDescriptorType = TUSB_DESC_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
	.idVendor = 0x04B5,
	.idProduct = 0x800B,
	.bcdDevice = 0x0100,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 0,
	.bNumConfigurations = 1
};

uint8_t const configuration_descriptor[] = {
	9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(0x20), 1, 1, 3, 0, 0xE1,
	9, TUSB_DESC_INTERFACE, 0, 0, 2, 0xFF, 1, 1, 4,
	7, TUSB_DESC_ENDPOINT, CFG_VENDOR_OUT_ENDPOINT, TUSB_XFER_BULK, U16_TO_U8S_LE(CFG_TUD_VENDOR_EPSIZE), 0,
	7, TUSB_DESC_ENDPOINT, 0x80 | CFG_VENDOR_IN_ENDPOINT, TUSB_XFER_BULK, U16_TO_U8S_LE(CFG_TUD_VENDOR_EPSIZE), 0
};

uint16_t const string_descriptor_0[] = {
	0x0304, 0x0409
};

uint16_t const string_descriptor_1[] = {
	0x0338,
	'L', 'A', 'P', 'I', 'S', ' ',
	(' ' << 8) + 'S', 'e', 'm', 'i', 'c', 'o', 'n', 'd', 'u', 'c', 't', 'o', 'r',
	(' ' << 8) + 'C', 'o', '.', ',',
	(' ' << 8) + 'L', 't', 'd', ('.' << 8) + ' '
};

uint16_t const string_descriptor_2[] = {
	0x030C,
	'u', 'E', 'A', 'S', 'E'
};

uint16_t const string_descriptor_3[] = {
	0x030A,
	'B', 'u', 'l', 'k'
};

uint16_t const string_descriptor_4[] = {
	0x031C,
	'B', 'u', 'l', 'k', '-', 'L', 'o', 'o', 'p', 'B', 'a', 'c', 'k'
};

uint16_t const* string_descriptors[] = {
	string_descriptor_0,
	string_descriptor_1,
	string_descriptor_2,
	string_descriptor_3,
	string_descriptor_4
};

uint8_t const* tud_descriptor_device_cb(void) {
	return (uint8_t const*)&device_descriptor;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
	return configuration_descriptor;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	if (!langid) return string_descriptors[0];
	if (langid != 0x409 || index > 4) return NULL;
	return string_descriptors[index];
}
