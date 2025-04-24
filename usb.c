#include "usb.h"

bool VendorControlReqFlag;
uint32_t BulkTransferSize;
uint32_t BulkReceivedBytes;

BulkXferEndpoint outEndpoint;
BulkXferEndpoint inEndpoint;

uEASEPacket ReceivePacket;
uEASEPacket TransmitPacket;

tusb_rhport_init_t const usb_init_config = {
	.role = TUSB_ROLE_DEVICE,
	.speed = TUSB_SPEED_FULL
};

bool isPacketReceived(void) {
	if (ReceivePacket.payloadSize + 0x10 == BulkReceivedBytes) {
		BulkReceivedBytes = 0;
		return true;
	}
	return false;
}

void BulkXferSetup(void* buffer, BulkXferEndpoint* endpoint, unsigned int size) {
	endpoint->buffer = buffer;
	endpoint->BufCurrentPtr = 0;
	endpoint->BufAllocEnd = size;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
	switch (stage) {
	case CONTROL_STAGE_SETUP:
		if (request->bRequest != 1 || request->wIndex != CFG_VENDOR_OUT_ENDPOINT) return false;
		VendorControlReqFlag = true;
		return tud_control_xfer(rhport, request, &BulkTransferSize, request->wLength);
	case CONTROL_STAGE_DATA:
		if (!VendorControlReqFlag) return false;
		VendorControlReqFlag = false;
		BulkReceivedBytes = 0;
		BulkXferSetup(&ReceivePacket, &outEndpoint, BulkTransferSize);
		break;
	default:
		break;
	}
	return true;
}

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
	unsigned int RemainingBytes = outEndpoint.BufAllocEnd - outEndpoint.BufCurrentPtr;
	if (bufsize >= RemainingBytes) {
		memcpy(outEndpoint.buffer + outEndpoint.BufCurrentPtr, buffer, RemainingBytes);
		tud_vendor_n_read_flush(itf);
		BulkReceivedBytes += RemainingBytes;
		BulkXferSetup(outEndpoint.buffer + outEndpoint.BufAllocEnd, &outEndpoint, BulkTransferSize);
		return;
	}
	memcpy(outEndpoint.buffer + outEndpoint.BufCurrentPtr, buffer, bufsize);
	tud_vendor_n_read_flush(itf);
	BulkReceivedBytes += bufsize;
	outEndpoint.BufCurrentPtr += bufsize;
}

void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes) {
	unsigned int bufsize = inEndpoint.BufAllocEnd - inEndpoint.BufCurrentPtr;
	if (!bufsize) return;
	if (bufsize > CFG_TUD_VENDOR_EPSIZE) bufsize = CFG_TUD_VENDOR_EPSIZE;
	tud_vendor_n_write(itf, inEndpoint.buffer + inEndpoint.BufCurrentPtr, bufsize);
	tud_vendor_n_write_flush(itf);
	inEndpoint.BufCurrentPtr += bufsize;
}

void VarInit(void) {
	VendorControlReqFlag = false;
	BulkTransferSize = CFG_TUD_VENDOR_EPSIZE;
	BulkReceivedBytes = 0;
	BulkXferSetup(&ReceivePacket, &outEndpoint, BulkTransferSize);
	BulkXferSetup(&TransmitPacket, &inEndpoint, 0);
	memset(&ReceivePacket, 0, sizeof(uEASEPacket));
	memset(&TransmitPacket, 0, sizeof(uEASEPacket));
}

void usb_loop(void) {
	VarInit();
	tusb_init(0, &usb_init_config);

	while (true) {
		if (multicore_fifo_rvalid()) {
			BulkXferSetup(&ReceivePacket, &outEndpoint, BulkTransferSize);
			BulkXferSetup(&TransmitPacket, &inEndpoint, multicore_fifo_pop_blocking_inline());
			tud_vendor_tx_cb(0, 0);
		}
		tud_task();
	}
}
