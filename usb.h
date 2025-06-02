#include "pico/multicore.h"
#include "tusb.h"
#include "device/usbd.h"

typedef struct {
	unsigned char* buffer;
	unsigned int BufCurrentPtr;
	unsigned int BufAllocEnd;
} BulkXferEndpoint;

typedef struct __attribute__((__packed__)) {
	unsigned int magic;
	unsigned char idWrapper_L;
	unsigned char id;
	unsigned long long idWrapper_H : 48;
	unsigned int payloadSize;
	unsigned char payload[0x800];
} uEASEPacket;

extern uEASEPacket ReceivePacket;
extern uEASEPacket TransmitPacket;

extern bool TransmitSyncFlag;

void usb_loop(void);

bool isPacketReceived(void);

static inline void SendPacket(unsigned int size) {
	TransmitSyncFlag = false;
	multicore_fifo_push_blocking_inline(size);
}
