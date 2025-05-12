#include "hardware.h"
#include "usb.h"

#define BYTEARRAY_WORD_READ_LE(x,y)			(x)[(y)] | ((x)[(y) + 1] << 8)
#define BYTEARRAY_WORD_READ_BE(x,y)			((x)[(y)] << 8) | (x)[(y) + 1]
#define BYTEARRAY_WORD_WRITE_LE(x,y,z)		(x)[(y)] = (unsigned char)(z);(x)[(y) + 1] = (unsigned char)((z) >> 8)
#define BYTEARRAY_WORD_WRITE_BE(x,y,z)		(x)[(y)] = (unsigned char)((z) >> 8);(x)[(y) + 1] = (unsigned char)(z)
#define BYTEARRAY_DWORD_READ_LE(x,y)		(x)[(y)] | ((x)[(y) + 1] << 8) | ((x)[(y) + 2] << 16) | ((x)[(y) + 3] << 24)
#define BYTEARRAY_DWORD_READ_BE(x,y)		((x)[(y)] << 24) | ((x)[(y) + 1] << 16) | ((x)[(y) + 2] << 8) | (x)[(y) + 3]
#define BYTEARRAY_DWORD_WRITE_LE(x,y,z)		(x)[(y)] = (unsigned char)(z);(x)[(y) + 1] = (unsigned char)((z) >> 8);(x)[(y) + 2] = (unsigned char)((z) >> 16);(x)[(y) + 3] = (unsigned char)((z) >> 24)
#define BYTEARRAY_DWORD_WRITE_BE(x,y,z)		(x)[(y)] = (unsigned char)((z) >> 24);(x)[(y) + 1] = (unsigned char)((z) >> 16);(x)[(y) + 2] = (unsigned char)((z) >> 8);(x)[(y) + 3] = (unsigned char)(z)

#define SAFE_EXEC_INSTRUCTION(x,y)			TargetRegisterWrite(2, (x));TargetRegisterWrite(3, (y));if (TargetInstructionExec()) return RET_TIMEOUT;

#define PASSWORD_INPUT_CNT_MAX				3
#define FLASH_ENABLE_TEST_AREA_WRITE		1
#define FLASH_ENABLE_WRITE_VERIFY			1
#define FLASH_FILL_MASK_OPTION_ON_INIT		1

typedef enum {
	STATE_FAILURE			= 0,	// uEASE Failure
	STATE_ILLEGAL_VDD		= 1,	// Detected illegal VDD input to VTref pin
	STATE_DEVICE_IDLE		= 2,	// Target is not connected
	STATE_CONNECTION_ERROR	= 3,	// Failed to connect to target
	STATE_TARGET_IDLE		= 4,	// Target is connected and no operation is in progress
	STATE_BUSY				= 5,	// An operation is in progress
} GLOBAL_STATE;

typedef enum {
	TRG_NOT_CONNECTED		= 0,	// Connection is not requested.
	TRG_CONNECTION_INIT		= 1,	// Connection is requested by the host. VTref check starts.
	TRG_CONNECTED			= -1,	// Target is successfully connected.
} CONNECTION_STATE;

typedef enum {
	RES_DATA_FLASH_NEAR		= 0x3001,
	RES_DATA_MEMORY			= 0x3011,
	RES_DATA_FLASH_FAR		= 0x3021,
	RES_CODE_FLASH_NEAR		= 0x3027,
	RES_CODE_FLASH_FAR		= 0x302B,
	RES_ICE_TYPE			= 0x3F00,
	RES_TRG_NAME			= 0x3F01,
	RES_FIRMWARE_VERSION	= 0x3F02,
	RES_HARDWARE_VERSION	= 0x3F03,
	RES_OCD_ID				= 0x3F07,
	RES_TRG_ID				= 0x3F09,
	RES_ERROR				= 0x7FFF,
} RESOURCE_NUMBER;

typedef enum {
	RET_SUCC					= 0,
	RET_TARGET_NOT_CONNECTED	= 1,
	RET_INVALID_PARAM			= 0x6000,
	RET_ADDR_OUT_OF_RANGE		= 0x6001,
	RET_INVALID_RES_NUMBER		= 0x6002,
	RET_PARAM_TOO_LARGE			= 0x6003,
	RET_FLASH_VERIFY_FAILURE	= 0x6100,
	RET_BUSY					= 0x6200,
	RET_TIMEOUT					= 0x6300,
	RET_RESET_FAILURE			= 0x6302,
	RET_FLASH_TIMEOUT			= 0x6303,
	RET_ERROR					= 0x6304,
	RET_ILLEGAL_FLASH_VDD		= 0x6305,
	RET_CONNECTION_ERROR		= 0x6307,
	RET_ILLEGAL_VPP				= 0x6308,
	RET_ILLEGAL_VDD				= 0x6309,
	RET_TARGET_INFO_ERROR		= 0x630A,
	RET_TRG_NOT_IN_EMULATION	= 0x630B,
	RET_UNSUPPORTED_TARGET		= 0x630C,
	RET_ISP_VERIFY_FAILURE		= 0x630D,
	RET_INVALID_COMMAND			= 0x6FFF,
} RETCODE;

typedef struct {
	unsigned short CmdId;
	RETCODE (*CmdHandler)(void);
} uEASECommand;

typedef struct {
	unsigned short Word0;
	unsigned short Word1;
	unsigned short Word2;
	unsigned short Word3;
} TargetIDOld;

typedef union {
	struct __attribute__((__packed__)) {
		unsigned char Revision						: 4;
		unsigned short TargetModelNumber			: 12;
		unsigned char ROMReadEnabled				: 4;
		unsigned char ROMProgramEnabled				: 4;
		unsigned char TargetCategory				: 4;
		unsigned char IdVersion						: 4;
		union {
			struct __attribute__((__packed__)) {
				unsigned char CoreMinorRev			: 4;
				unsigned char CoreMajorRev			: 3;
				bool CoreFamilyID					: 1;
			};
			unsigned char CoreRev					: 8;
		};
		unsigned char CodeFlashBlockNumNear_L		: 4;
		unsigned char CodeFlashBlockNumFar_L		: 4;
		union {
			struct __attribute__((__packed__)) {
				unsigned char CodeFlashBlockSize	: 4;
				bool FlashHasInternalClock			: 1;
				bool FlashHasInternalVPP			: 1;
				bool MemoryModel					: 1;
				bool LockedFlashInitDisabled		: 1;
			};
			struct __attribute__((__packed__)) {
				unsigned char PaddingBits			: 4;
				unsigned char FlashCharacteristics	: 2;
			};
		};
		unsigned char ROMWindowEnd					: 8;
		unsigned char CodeFlashBlockNumFar_H		: 4;
		unsigned char CodeFlashBlockNumNear_H		: 4;
		unsigned char CodeFlashSectorSize			: 2;
		unsigned char MaskOptionAreaSize			: 3;
		unsigned char TestAreaSize					: 3;
		unsigned char CoproID						: 8;
		unsigned char ExtraInfo						: 4;
		bool ExtraFlag								: 1;
		unsigned char Rsvd							: 3;
		unsigned int NotUsed						: 32;
	};
	struct {
		unsigned short Word0;
		unsigned short Word1;
		unsigned short Word2;
		unsigned short Word3;
		unsigned short Word4;
		unsigned short Word5;
		unsigned short Word6;
		unsigned short Word7;
	};
} TargetID;

typedef struct {
	unsigned int BlockStartAddr;
	unsigned int BlockEndAddr;
} CodeFlashBlockInfo;

typedef struct {
	unsigned short ROMWindowStart;
	unsigned short ROMWindowEnd;
	bool ROMReadEnabled;
	bool LockedFlashInitDisabled;
	bool MemoryModel;
	unsigned char FlashCharacteristics;
	unsigned short CodeFlashBlockSize;
	unsigned short CodeFlashBlockNumFar;
	unsigned short CodeFlashBlockNumNear;
	unsigned char CoreRev;
	char TargetNameStr[16];
	unsigned short BreakpointNum;
	unsigned int FlashPwdAddr;
	unsigned int MaskOptionAreaAddr;
	unsigned int TestAreaAddr;
	unsigned short MaskOptionAreaSize;
	unsigned short TestAreaSize;
	CodeFlashBlockInfo CodeBlocks[0x100];
} TargetInfo;

// Hardware version is defined by PIOF04, PIOF05 and PIOF06 in uEASE. Valid range is 01 to 07.
const unsigned char hardware_version = 0x03;

const char ice_type[] = "uEASE";
const char firmware_version[] = "3.21";

GLOBAL_STATE GlobalState;
CONNECTION_STATE ConnectionState;
unsigned short OCD_ID;
unsigned short PasswordInputCnt;
unsigned short NMICEFlag;
unsigned short NMICEControl;
unsigned short EmulationTime;
unsigned short MemFillRetcode;
unsigned int MemFillEndAddr;
bool OCDStateSyncFlag;
bool TargetRunningFlag;
bool TargetResetFlag;
bool isTargetAvailable;
bool isMemFillAvailable;

unsigned short FCON_addr;
unsigned short L2HBIT;
unsigned short H2LBIT;

unsigned short ER0_backup;
unsigned short EA_backup;
unsigned char DSR_backup;
unsigned char PSW_backup;
unsigned char FCON_backup;

TargetID targetID;
TargetInfo targetInfo;
unsigned char TargetInfoState;

TargetIDOld const TargetIDToFix[3] = {
	{0x4110, 0x0011, 0x1131, 0x1F18},	// ML610411
	{0x4120, 0x0011, 0x1131, 0x1F18},	// ML610412
	{0x4150, 0x0011, 0x1131, 0x1F18},	// ML610415
};

unsigned char const PasswordRegList[10] = {
	0x45, 0x44, 0x46, 0x47, 0x4A,
	0x4B, 0x4C, 0x4D, 0x4E, 0x4F
};

unsigned short PasswordBuf[10];
unsigned char FlashFillBuf[0x4000];

unsigned char PendingPacketID;
unsigned int RspPayloadSize;
unsigned char RspPayload[0x630];

void systick_int_handler(void) {
	if (TargetRunningFlag) EmulationTime++;
	OCDStateSyncFlag = true;
}

void SetGlobalState(GLOBAL_STATE state) {
	GlobalState = state;
	if (state == STATE_BUSY) systick_hw->csr |= 1;
	else systick_hw->csr &= ~1;
}

RETCODE TargetFlashWrite(void* buffer, unsigned int startAddr, unsigned int endAddr, int alignMode, unsigned short* writeCount) {
	unsigned short seg = (startAddr >> 16) & 0xFF;
	unsigned short offset = startAddr & 0xFFFE;
	unsigned short endSeg = (endAddr >> 16) & 0xFF;
	unsigned short endOffset = endAddr & 0xFFFF;
	unsigned char flashCharacteristics = targetInfo.FlashCharacteristics;
	if (flashCharacteristics != 0) {
		TargetRegisterWrite(0x67, TargetRegisterRead(0x67) | 1);
		busy_wait_ms(2);
		if (!(flashCharacteristics & 2)) {
			// TODO: Enable VPP
		}
	}
#if FLASH_ENABLE_TEST_AREA_WRITE
	TargetRegisterWrite(0x68, TargetRegisterRead(0x68) | 2);
#endif
	TargetRegisterWrite(0x60, 5);
	TargetRegisterWrite(0x63, seg);
	TargetRegisterWrite(0x64, offset);
	unsigned int systick_csr_backup = systick_hw->csr;
	unsigned int systick_rvr_backup = systick_hw->rvr;
	systick_hw->csr = 0;
	systick_hw->rvr = 0x400;
	systick_hw->cvr = 0;
	systick_hw->csr = 1;
	switch (alignMode)
	{
	case 1:
	{
		unsigned short* bufptr = buffer;
		while (++seg < endSeg) {
			do {
				TargetRegisterWrite(0x65, *bufptr);
				TargetRegisterWrite(0x61, 4);
				TargetRegisterRead(0x61);
				systick_hw->cvr = 0;
				while (TargetRegisterRead(0x62) & 0x0100)
					if (systick_hw->csr >> 16)
						return RET_FLASH_TIMEOUT;
				bufptr += 2;
				*writeCount += 2;
			} while (offset += 2);
			TargetRegisterWrite(0x63, seg);
		}
		do {
			TargetRegisterWrite(0x65, *bufptr);
			TargetRegisterWrite(0x61, 4);
			TargetRegisterRead(0x61);
			systick_hw->cvr = 0;
			while (TargetRegisterRead(0x62) & 0x0100)
				if (systick_hw->csr >> 16)
					return RET_FLASH_TIMEOUT;
			bufptr += 2;
			*writeCount += 2;
		} while ((offset += 2) <= endOffset && offset);
		break;
	}
	case 2:
	{
		unsigned char* bufptr = buffer;
		while (++seg < endSeg) {
			do {
				TargetRegisterWrite(0x65, BYTEARRAY_WORD_READ_LE(bufptr, 0));
				TargetRegisterWrite(0x61, 4);
				TargetRegisterRead(0x61);
				systick_hw->cvr = 0;
				while (TargetRegisterRead(0x62) & 0x0100)
					if (systick_hw->csr >> 16)
						return RET_FLASH_TIMEOUT;
				bufptr += 2;
				*writeCount += 2;
			} while (offset += 2);
			TargetRegisterWrite(0x63, seg);
		}
		do {
			TargetRegisterWrite(0x65, BYTEARRAY_WORD_READ_LE(bufptr, 0));
			TargetRegisterWrite(0x61, 4);
			TargetRegisterRead(0x61);
			systick_hw->cvr = 0;
			while (TargetRegisterRead(0x62) & 0x0100)
				if (systick_hw->csr >> 16)
					return RET_FLASH_TIMEOUT;
			bufptr += 2;
			*writeCount += 2;
		} while ((offset += 2) <= endOffset && offset);
		break;
	}
	default:
		return RET_INVALID_PARAM;
	}
#if FLASH_ENABLE_TEST_AREA_WRITE
	TargetRegisterWrite(0x68, TargetRegisterRead(0x68) & ~2);
#endif
	if (flashCharacteristics != 0) {
		if (!(flashCharacteristics & 2)) {
			//TODO: Disable VPP
		}
		TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
	}
#if FLASH_ENABLE_WRITE_VERIFY
	seg = (startAddr >> 16) & 0xFF;
	offset = startAddr & 0xFFFE;
	TargetRegisterWrite(0x60, 3);
	TargetRegisterWrite(0x63, seg);
	TargetRegisterWrite(0x64, offset);
	if (alignMode == 1) {
		while (++seg < endSeg) {
			do {
				TargetRegisterWrite(0x61, 1);
				if (TargetRegisterRead(0x66) != *(unsigned short*)buffer)
					return RET_FLASH_VERIFY_FAILURE;
				buffer += 2;
			} while (offset += 2);
			TargetRegisterWrite(0x63, seg);
		}
		do {
			TargetRegisterWrite(0x61, 1);
			if (TargetRegisterRead(0x66) != *(unsigned short*)buffer)
				return RET_FLASH_VERIFY_FAILURE;
			buffer += 2;
		} while ((offset += 2) <= endOffset && offset);
	} else {
		while (++seg < endSeg) {
			do {
				TargetRegisterWrite(0x61, 1);
				if (TargetRegisterRead(0x66) != BYTEARRAY_WORD_READ_LE((unsigned char*)buffer, 0))
					return RET_FLASH_VERIFY_FAILURE;
				buffer += 2;
			} while (offset += 2);
			TargetRegisterWrite(0x63, seg);
		}
		do {
			TargetRegisterWrite(0x61, 1);
			if (TargetRegisterRead(0x66) != BYTEARRAY_WORD_READ_LE((unsigned char*)buffer, 0))
				return RET_FLASH_VERIFY_FAILURE;
			buffer += 2;
		} while ((offset += 2) <= endOffset && offset);
	}
#endif
	TargetRegisterWrite(0x60, 0);
	TargetRegisterWrite(0x61, 0);
	systick_hw->rvr = systick_rvr_backup;
	systick_hw->cvr = 0;
	systick_hw->csr = systick_csr_backup;
	return RET_SUCC;
}

RETCODE TargetFlashChipErase(void) {
	unsigned char flashCharacteristics = targetInfo.FlashCharacteristics;
	if (flashCharacteristics != 0) {
		TargetRegisterWrite(0x67, TargetRegisterRead(0x67) | 1);
		busy_wait_ms(2);
		if (!(flashCharacteristics & 2)) {
			// TODO: Enable VPP
		}
	}
#if FLASH_ENABLE_TEST_AREA_WRITE
	TargetRegisterWrite(0x68, TargetRegisterRead(0x68) | 2);
#endif
	TargetRegisterWrite(0x60, 1);
	TargetRegisterWrite(0x63, 0);
	TargetRegisterWrite(0x64, 0);
	TargetRegisterWrite(0x61, 6);
	TargetRegisterRead(0x61);
	absolute_time_t timeout = time_us_64() + 200000;
	while (TargetRegisterRead(0x62) & 0x0100)
		if (time_reached(timeout))
			return RET_FLASH_TIMEOUT;
	TargetRegisterWrite(0x60, 0);
	TargetRegisterWrite(0x61, 0);
#if FLASH_ENABLE_TEST_AREA_WRITE
	TargetRegisterWrite(0x68, TargetRegisterRead(0x68) & ~2);
#endif
	if (flashCharacteristics != 0) {
		if (!(flashCharacteristics & 2)) {
			//TODO: Disable VPP
		}
		TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
	}
	return RET_SUCC;
}

RETCODE TargetFlashBlockErase(unsigned int addr) {
	unsigned char flashCharacteristics = targetInfo.FlashCharacteristics;
	if (flashCharacteristics != 0) {
		TargetRegisterWrite(0x67, TargetRegisterRead(0x67) | 1);
		busy_wait_ms(2);
		if (!(flashCharacteristics & 2)) {
			// TODO: Enable VPP
		}
	}
#if FLASH_ENABLE_TEST_AREA_WRITE
	TargetRegisterWrite(0x68, TargetRegisterRead(0x68) | 2);
#endif
	TargetRegisterWrite(0x60, 1);
	TargetRegisterWrite(0x63, (addr >> 16) & 0xFF);
	TargetRegisterWrite(0x64, addr & 0xFFFE);
	TargetRegisterWrite(0x61, 5);
	TargetRegisterRead(0x61);
	absolute_time_t timeout = time_us_64() + 200000;
	while (TargetRegisterRead(0x62) & 0x0100)
		if (time_reached(timeout))
			return RET_FLASH_TIMEOUT;
	TargetRegisterWrite(0x60, 0);
	TargetRegisterWrite(0x61, 0);
#if FLASH_ENABLE_TEST_AREA_WRITE
	TargetRegisterWrite(0x68, TargetRegisterRead(0x68) & ~2);
#endif
	if (flashCharacteristics != 0) {
		if (!(flashCharacteristics & 2)) {
			//TODO: Disable VPP
		}
		TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
	}
	return RET_SUCC;
}

RETCODE TargetFlashRead(void* buffer, unsigned int startAddr, unsigned int endAddr, int alignMode) {
	unsigned short seg = (startAddr >> 16) & 0xFF;
	unsigned short offset = startAddr & 0xFFFE;
	unsigned short endSeg = (endAddr >> 16) & 0xFF;
	unsigned short endOffset = endAddr & 0xFFFF;
	TargetRegisterWrite(0x60, 3);
	TargetRegisterWrite(0x63, seg);
	TargetRegisterWrite(0x64, offset);
	switch (alignMode)
	{
	case 1:
		while (++seg < endSeg) {
			do {
				TargetRegisterWrite(0x61, 1);
				*(unsigned short*)buffer = TargetRegisterRead(0x66);
				buffer += 2;
			} while (offset += 2);
			TargetRegisterWrite(0x63, seg);
		}
		do {
			TargetRegisterWrite(0x61, 1);
			*(unsigned short*)buffer = TargetRegisterRead(0x66);
			buffer += 2;
		} while ((offset += 2) <= endOffset && offset);
		break;
	case 2:
		if (startAddr & 1) {
			TargetRegisterWrite(0x61, 1);
			*(unsigned char*)buffer = TargetRegisterRead(0x66) >> 8;
			buffer++;
			if (!(offset += 2)) seg++;
		}
		while (++seg < endSeg) {
			do {
				TargetRegisterWrite(0x61, 1);
				unsigned short data = TargetRegisterRead(0x66);
				BYTEARRAY_WORD_WRITE_LE((unsigned char*)buffer, 0, data);
				buffer += 2;
			} while (offset += 2);
			TargetRegisterWrite(0x63, seg);
		}
		do {
			TargetRegisterWrite(0x61, 1);
			unsigned short data = TargetRegisterRead(0x66);
			BYTEARRAY_WORD_WRITE_LE((unsigned char*)buffer, 0, data);
			buffer += 2;
		} while ((offset += 2) < endOffset && offset);
		if (!(endOffset & 1)) {
			TargetRegisterWrite(0x61, 1);
			*(unsigned char*)buffer = (unsigned char)TargetRegisterRead(0x66);
		}
		break;
	default:
		return RET_INVALID_PARAM;
	}
	TargetRegisterWrite(0x60, 0);
	TargetRegisterWrite(0x61, 0);
	return RET_SUCC;
}

RETCODE TargetFlashFill(
	unsigned int blockStartAddr,
	unsigned int blockEndAddr,
	unsigned int fillStartAddr,
	unsigned int fillEndAddr,
	int alignMode,
	unsigned short FillWord,
	unsigned char FillByte)
{
#if !FLASH_ENABLE_TEST_AREA_WRITE
	if (blockEndAddr == targetInfo.TestAreaAddr + targetInfo.TestAreaSize - 1)
		blockEndAddr -= targetInfo.TestAreaSize;
#endif
	unsigned int startAddr = blockStartAddr;
	unsigned int endAddr = blockEndAddr;
	int bufStartAddr = 0;
	int bufEndAddr = 0x4000;
	if (fillStartAddr > blockStartAddr) {
		bufStartAddr = fillStartAddr - blockStartAddr;
		if (bufStartAddr > 0x4000) return RET_PARAM_TOO_LARGE;
		TargetFlashRead(FlashFillBuf, blockStartAddr, fillStartAddr - 1, alignMode);
		startAddr = fillStartAddr;
	}
	if (fillEndAddr < blockEndAddr) {
		bufEndAddr = 0x4000 + fillEndAddr - blockEndAddr;
		if (bufEndAddr < bufStartAddr) return RET_PARAM_TOO_LARGE;
		TargetFlashRead(FlashFillBuf + bufEndAddr, fillEndAddr + 1, blockEndAddr, alignMode);
		endAddr = fillEndAddr;
	}
	if (startAddr > endAddr) return RET_SUCC;
	RETCODE retcode = TargetFlashBlockErase(blockStartAddr);
	if (retcode != RET_SUCC) goto abort;
	unsigned short writeCount = 0;
	// TODO: Turn on BUSY indicator
	if (bufStartAddr > 0) {
		retcode = TargetFlashWrite(FlashFillBuf, blockStartAddr, fillStartAddr - 1, alignMode, &writeCount);
		if (retcode != RET_SUCC) goto abort;
	}
	if (bufEndAddr < 0x4000) {
		retcode = TargetFlashWrite(FlashFillBuf + bufEndAddr, fillEndAddr + 1, blockEndAddr, alignMode, &writeCount);
		if (retcode != RET_SUCC) goto abort;
	}
	if (alignMode == 1)
		for (int i = 0; i < 0x2000; i++)
			*((unsigned short*)FlashFillBuf + i) = FillWord;
	else
		for (int i = 0; i < 0x4000; i++)
			FlashFillBuf[i] = FillByte;
	for (unsigned int addr = blockStartAddr + 0x3FFF; addr < endAddr; addr += 0x4000) {
		retcode = TargetFlashWrite(FlashFillBuf, startAddr, addr, alignMode, &writeCount);
		if (retcode != RET_SUCC) goto abort;
		startAddr = addr + 1;
	}
	retcode = TargetFlashWrite(FlashFillBuf, startAddr, endAddr, alignMode, &writeCount);
	if (retcode != RET_SUCC) {
		abort:
		// TODO: Disable VPP
		TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
		TargetRegisterWrite(0x60, 0);
		TargetRegisterWrite(0x61, 0);
	}
	// TODO: Turn off BUSY indicator
	return retcode;
}

static inline void TargetDataFlashWordWrite(unsigned short seg, unsigned short offset, unsigned short data) {
	TargetRegisterWrite(0x60, 1);
	TargetRegisterWrite(0x63, seg);
	TargetRegisterWrite(0x64, offset & 0xFFFE);
	TargetRegisterWrite(0x65, data);
	TargetRegisterWrite(0x61, 4);
	TargetRegisterRead(0x61);
}

static inline unsigned short TargetDataFlashWordRead(unsigned short seg, unsigned short offset) {
	TargetRegisterWrite(0x60, 1);
	TargetRegisterWrite(0x63, seg);
	TargetRegisterWrite(0x64, offset & 0xFFFE);
	TargetRegisterWrite(0x61, 1);
	return TargetRegisterRead(0x66);
}

RETCODE TargetDataFlashWrite(void* buffer, unsigned int startAddr, unsigned int endAddr, int alignMode, unsigned short* writeCount) {
	unsigned short seg = (startAddr >> 16) & 0xFF;
	unsigned short offset = startAddr & 0xFFFE;
	unsigned short endSeg = (endAddr >> 16) & 0xFF;
	unsigned short endOffset = endAddr & 0xFFFF;
	unsigned int systick_csr_backup = systick_hw->csr;
	unsigned int systick_rvr_backup = systick_hw->rvr;
	systick_hw->csr = 0;
	systick_hw->rvr = 0x400;
	systick_hw->cvr = 0;
	systick_hw->csr = 1;
	switch (alignMode)
	{
	case 1:
		while (++seg < endSeg) {
			do {
				unsigned short data = *(unsigned short*)buffer;
				TargetDataFlashWordWrite(seg, offset, data);
				systick_hw->cvr = 0;
				while (TargetRegisterRead(0x62) & 0x0100)
					if (systick_hw->csr >> 16)
						return RET_FLASH_TIMEOUT;
#if FLASH_ENABLE_WRITE_VERIFY
				if (TargetDataFlashWordRead(seg, offset) != data)
					return RET_FLASH_VERIFY_FAILURE;
#endif
				buffer += 2;
				*writeCount += 2;
			} while (offset += 2);
		}
		do {
			unsigned short data = *(unsigned short*)buffer;
			TargetDataFlashWordWrite(seg, offset, data);
			systick_hw->cvr = 0;
			while (TargetRegisterRead(0x62) & 0x0100)
				if (systick_hw->csr >> 16)
					return RET_FLASH_TIMEOUT;
#if FLASH_ENABLE_WRITE_VERIFY
			if (TargetDataFlashWordRead(seg, offset) != data)
				return RET_FLASH_VERIFY_FAILURE;
#endif
			buffer += 2;
			*writeCount += 2;
		} while ((offset += 2) <= endOffset && offset);
		break;
	case 2:
		if (startAddr & 1) {
			unsigned short data = (unsigned char)TargetDataFlashWordRead(seg, offset) | (*(unsigned char*)buffer << 8);
			TargetDataFlashWordWrite(seg, offset, data);
			systick_hw->cvr = 0;
			while (TargetRegisterRead(0x62) & 0x0100)
				if (systick_hw->csr >> 16)
					return RET_FLASH_TIMEOUT;
#if FLASH_ENABLE_WRITE_VERIFY
			if (TargetDataFlashWordRead(seg, offset) != data)
				return RET_FLASH_VERIFY_FAILURE;
#endif
			buffer++;
			if (!(offset += 2)) seg++;
		}
		while (++seg < endSeg) {
			do {
				unsigned short data = BYTEARRAY_WORD_READ_LE((unsigned char*)buffer, 0);
				TargetDataFlashWordWrite(seg, offset, data);
				systick_hw->cvr = 0;
				while (TargetRegisterRead(0x62) & 0x0100)
					if (systick_hw->csr >> 16)
						return RET_FLASH_TIMEOUT;
#if FLASH_ENABLE_WRITE_VERIFY
				if (TargetDataFlashWordRead(seg, offset) != data)
					return RET_FLASH_VERIFY_FAILURE;
#endif
				buffer += 2;
				*writeCount += 2;
			} while (offset += 2);
		}
		do {
			unsigned short data = BYTEARRAY_WORD_READ_LE((unsigned char*)buffer, 0);
			TargetDataFlashWordWrite(seg, offset, data);
			systick_hw->cvr = 0;
			while (TargetRegisterRead(0x62) & 0x0100)
				if (systick_hw->csr >> 16)
					return RET_FLASH_TIMEOUT;
#if FLASH_ENABLE_WRITE_VERIFY
			if (TargetDataFlashWordRead(seg, offset) != data)
				return RET_FLASH_VERIFY_FAILURE;
#endif
			buffer += 2;
			*writeCount += 2;
		} while ((offset += 2) < endOffset && offset);
		if (!(endOffset & 1)) {
			unsigned short data = (TargetDataFlashWordRead(seg, offset) & 0xFF00) | *(unsigned char*)buffer;
			TargetDataFlashWordWrite(seg, offset, data);
			systick_hw->cvr = 0;
			while (TargetRegisterRead(0x62) & 0x0100)
				if (systick_hw->csr >> 16)
					return RET_FLASH_TIMEOUT;
#if FLASH_ENABLE_WRITE_VERIFY
			if (TargetDataFlashWordRead(seg, offset) != data)
				return RET_FLASH_VERIFY_FAILURE;
#endif
		}
		break;
	default:
		return RET_INVALID_PARAM;
	}
	TargetRegisterWrite(0x60, 0);
	TargetRegisterWrite(0x61, 0);
	systick_hw->rvr = systick_rvr_backup;
	systick_hw->cvr = 0;
	systick_hw->csr = systick_csr_backup;
	return RET_SUCC;
}

RETCODE TargetDataFlashFill(unsigned int startAddr, unsigned int endAddr, int alignMode, unsigned short FillWord, unsigned char FillByte) {
	unsigned short seg = (startAddr >> 16) & 0xFF;
	unsigned short offset = startAddr & 0xFFFE;
	unsigned short endSeg = (endAddr >> 16) & 0xFF;
	unsigned short endOffset = endAddr & 0xFFFF;
	unsigned int systick_csr_backup = systick_hw->csr;
	unsigned int systick_rvr_backup = systick_hw->rvr;
	systick_hw->csr = 0;
	systick_hw->rvr = 0x400;
	systick_hw->cvr = 0;
	systick_hw->csr = 1;
	unsigned short FillData = alignMode == 1 ? FillWord : (FillByte | (FillByte << 8));
	if (startAddr & 1) {
		TargetDataFlashWordWrite(seg, offset, (unsigned char)TargetDataFlashWordRead(seg, offset) | (FillData & 0xFF00));
		systick_hw->cvr = 0;
		while (TargetRegisterRead(0x62) & 0x0100)
			if (systick_hw->csr >> 16)
				return RET_FLASH_TIMEOUT;
		if (!(offset += 2)) seg++;
	}
	while (++seg < endSeg) {
		do {
			TargetDataFlashWordWrite(seg, offset, FillData);
			systick_hw->cvr = 0;
			while (TargetRegisterRead(0x62) & 0x0100)
				if (systick_hw->csr >> 16)
					return RET_FLASH_TIMEOUT;
		} while (offset += 2);
	}
	do {
		TargetDataFlashWordWrite(seg, offset, FillData);
		systick_hw->cvr = 0;
		while (TargetRegisterRead(0x62) & 0x0100)
			if (systick_hw->csr >> 16)
				return RET_FLASH_TIMEOUT;
	} while ((offset += 2) < endOffset && offset);
	if (!(endOffset & 1)) {
		TargetDataFlashWordWrite(seg, offset, (TargetDataFlashWordRead(seg, offset) & 0xFF00) | (unsigned char)FillData);
		systick_hw->cvr = 0;
		while (TargetRegisterRead(0x62) & 0x0100)
			if (systick_hw->csr >> 16)
				return RET_FLASH_TIMEOUT;
	}
#if FLASH_ENABLE_WRITE_VERIFY
	seg = (startAddr >> 16) & 0xFF;
	offset = startAddr & 0xFFFE;
	TargetRegisterWrite(0x60, 3);
	TargetRegisterWrite(0x63, seg);
	TargetRegisterWrite(0x64, offset);
	if (startAddr & 1) {
		TargetRegisterWrite(0x61, 1);
		if ((TargetRegisterRead(0x66) ^ FillData) >> 8)
			return RET_FLASH_VERIFY_FAILURE;
		if (!(offset += 2)) seg++;
	}
	while (++seg < endSeg) {
		do {
			TargetRegisterWrite(0x61, 1);
			if (TargetRegisterRead(0x66) != FillData)
				return RET_FLASH_VERIFY_FAILURE;
		} while (offset += 2);
	}
	do {
		TargetRegisterWrite(0x61, 1);
		if (TargetRegisterRead(0x66) != FillData)
			return RET_FLASH_VERIFY_FAILURE;
	} while ((offset += 2) < endOffset && offset);
	if (!(endOffset & 1)) {
		TargetRegisterWrite(0x61, 1);
		if ((TargetRegisterRead(0x66) ^ FillData) & 0xFF)
			return RET_FLASH_VERIFY_FAILURE;
	}
#endif
	TargetRegisterWrite(0x60, 0);
	TargetRegisterWrite(0x61, 0);
	systick_hw->rvr = systick_rvr_backup;
	systick_hw->cvr = 0;
	systick_hw->csr = systick_csr_backup;
	return RET_SUCC;
}

static inline RETCODE TargetInstructionExec() {
	TargetRegisterWrite(0xC, 1);
	absolute_time_t timeout = time_us_64() + 2000000;
	while (TargetRegisterRead(0xC))
		if (time_reached(timeout))
			return RET_TIMEOUT;
	return RET_SUCC;
}

RETCODE TargetDataMemoryWrite(void* buffer, unsigned int startAddr, unsigned int endAddr, int alignMode, unsigned short* writeCount) {
	unsigned short seg = (startAddr >> 16) & 0xFF;
	unsigned short offset;
	unsigned int size;
	switch (alignMode)
	{
	case 1:
		offset = startAddr & 0xFFFE;
		size = endAddr - startAddr + 2;
		SAFE_EXEC_INSTRUCTION(0xF00C, offset);
		do {
			unsigned short data = *(unsigned short*)buffer;
			if (!seg) {
				if (offset == 0xF000) {
					DSR_backup = data & 0xFF;
					SAFE_EXEC_INSTRUCTION(0xF00C, offset + 1);
					SAFE_EXEC_INSTRUCTION(data >> 8, 0x9051);
				} else if (offset == FCON_addr) {
					FCON_backup = data & 0xFF;
					SAFE_EXEC_INSTRUCTION(0xF00C, offset + 1);
					SAFE_EXEC_INSTRUCTION(data >> 8, 0x9051);
				} else if (offset + 1 == FCON_addr) {
					FCON_backup = data >> 8;
					SAFE_EXEC_INSTRUCTION(data & 0xFF, 0x9051);
					SAFE_EXEC_INSTRUCTION(0xF00C, offset + 2);
				} else {
					SAFE_EXEC_INSTRUCTION(data & 0xFF, 0x0100 | (data >> 8));
					SAFE_EXEC_INSTRUCTION(0xE300, 0x9053);
				}
			} else {
				SAFE_EXEC_INSTRUCTION(data & 0xFF, 0x0100 | (data >> 8));
				SAFE_EXEC_INSTRUCTION(0xE300 | seg, 0x9053);
			}
			buffer += 2;
			*writeCount += 2;
			if (!(offset += 2)) seg++;
		} while (size -= 2);
		return RET_SUCC;
	case 2:
		offset = startAddr & 0xFFFF;
		size = endAddr - startAddr + 1;
		SAFE_EXEC_INSTRUCTION(0xF00C, offset);
		do {
			if (!seg) {
				if (offset == 0xF000) {
					DSR_backup = *(unsigned char*)buffer;
					SAFE_EXEC_INSTRUCTION(0xF00C, offset + 1);
				} else if (offset == FCON_addr) {
					FCON_backup = *(unsigned char*)buffer;
					SAFE_EXEC_INSTRUCTION(0xF00C, offset + 1);
				} else {
					SAFE_EXEC_INSTRUCTION(*(unsigned char*)buffer, 0xFE8F);
					SAFE_EXEC_INSTRUCTION(0xE300 | seg, 0x9051);
				}
			} else {
				SAFE_EXEC_INSTRUCTION(*(unsigned char*)buffer, 0xFE8F);
				SAFE_EXEC_INSTRUCTION(0xE300 | seg, 0x9051);
			}
			buffer++;
			*writeCount++;
			if (!(++offset)) seg++;
		} while (--size);
		return RET_SUCC;
	default:
		return RET_INVALID_PARAM;
	}
}

RETCODE TargetDataMemoryRead(void* buffer, unsigned int startAddr, unsigned int endAddr, int alignMode) {
	unsigned short seg = (startAddr >> 16) & 0xFF;
	unsigned short offset;
	unsigned int size;
	switch (alignMode)
	{
	case 1:
		offset = startAddr & 0xFFFE;
		size = endAddr - startAddr + 2;
		SAFE_EXEC_INSTRUCTION(0xF00C, offset);
		TargetRegisterWrite(2, 0xE300 | seg);
		TargetRegisterWrite(3, 0x9052);
		do {
			if (TargetInstructionExec()) return RET_TIMEOUT;
			unsigned short data = TargetRegisterRead(4);
			if (!seg) {
				if (offset == 0xF000)
					data = (data & 0xFF00) | DSR_backup;
				else if (offset == FCON_addr)
					data = (data & 0xFF00) | FCON_backup;
				else if (offset + 1 == FCON_addr)
					data = (data & 0xFF) | (FCON_backup << 8);
			}
			*(unsigned short*)buffer = data;
			buffer += 2;
			if (!(offset += 2)) TargetRegisterWrite(2, 0xE300 | (++seg));
		} while (size -= 2);
		return RET_SUCC;
	case 2:
		offset = startAddr & 0xFFFF;
		size = endAddr - startAddr + 1;
		SAFE_EXEC_INSTRUCTION(0xF00C, offset);
		TargetRegisterWrite(2, 0xE300 | seg);
		TargetRegisterWrite(3, 0x9050);
		do {
			if (TargetInstructionExec()) return RET_TIMEOUT;
			if (!seg) {
				if (offset == 0xF000)
					*(unsigned char*)buffer = DSR_backup;
				else if (offset == FCON_addr)
					*(unsigned char*)buffer = FCON_backup;
				else
					*(unsigned char*)buffer = (unsigned char)TargetRegisterRead(4);
			} else {
				*(unsigned char*)buffer = (unsigned char)TargetRegisterRead(4);
			}
			buffer++;
			if (!(++offset)) TargetRegisterWrite(2, 0xE300 | (++seg));
		} while (--size);
		return RET_SUCC;
	default:
		return RET_INVALID_PARAM;
	}
}

RETCODE TargetDataMemoryFill(unsigned int startAddr, unsigned int endAddr, int alignMode, unsigned short FillWord, unsigned char FillByte) {
	unsigned short seg = (startAddr >> 16) & 0xFF;
	unsigned short offset;
	unsigned int size;
	switch (alignMode)
	{
	case 1:
		offset = startAddr & 0xFFFE;
		size = endAddr - startAddr + 2;
		SAFE_EXEC_INSTRUCTION(0xF00C, offset);
		SAFE_EXEC_INSTRUCTION(FillWord & 0xFF, 0x0100 | (FillWord >> 8));
		TargetRegisterWrite(2, 0xE300 | seg);
		TargetRegisterWrite(3, 0x9053);
		do {
			if (TargetInstructionExec()) return RET_TIMEOUT;
			if (!(offset += 2)) TargetRegisterWrite(2, 0xE300 | (++seg));
		} while (size -= 2);
		return RET_SUCC;
	case 2:
		offset = startAddr & 0xFFFF;
		size = endAddr - startAddr + 1;
		SAFE_EXEC_INSTRUCTION(0xF00C, offset);
		SAFE_EXEC_INSTRUCTION(FillByte, 0xFE8F);
		TargetRegisterWrite(2, 0xE300 | seg);
		TargetRegisterWrite(3, 0x9051);
		do {
			if (TargetInstructionExec()) return RET_TIMEOUT;
			if (!(++offset)) TargetRegisterWrite(2, 0xE300 | (++seg));
		} while (--size);
		return RET_SUCC;
	default:
		return RET_INVALID_PARAM;
	}
}

RETCODE TargetBackupCPURegisters() {
	ER0_backup = TargetRegisterRead(4);
	EA_backup = TargetRegisterRead(5);
	SAFE_EXEC_INSTRUCTION(0xA004, 0xFE8F);
	PSW_backup = TargetRegisterRead(4);
	SAFE_EXEC_INSTRUCTION(0xF00C, 0xF000);
	SAFE_EXEC_INSTRUCTION(0xFE8F, 0x9030);
	DSR_backup = TargetRegisterRead(4);
	SAFE_EXEC_INSTRUCTION(0xF00C, FCON_addr);
	SAFE_EXEC_INSTRUCTION(0xE300, 0x9030);
	FCON_backup = TargetRegisterRead(4);
	SAFE_EXEC_INSTRUCTION(L2HBIT, 0xFE8F);
	SAFE_EXEC_INSTRUCTION(0xE300, 0x9031);
	SAFE_EXEC_INSTRUCTION(0xE300, 0x9031);
	busy_wait_us(200);
	return RET_SUCC;
}

RETCODE TargetRaiseNMICE() {
	RETCODE retcode = RET_SUCC;
	isTargetAvailable = true;
	if (!(TargetRegisterRead(0) & 0x20)) return retcode;
	unsigned short RegBackup = TargetRegisterRead(0xD);
	TargetRegisterWrite(0xE, 0);
	TargetRegisterWrite(0xD, 8);
	absolute_time_t timeout = time_us_64() + 2000000;
	while ((TargetRegisterRead(0) & 0x20) && !time_reached(timeout));
	if (!(TargetRegisterRead(0xE) & 0xC)) retcode = RET_ERROR;
	TargetRegisterWrite(0xD, RegBackup);
	TargetRegisterWrite(0xE, 0);
	return retcode;
}

RETCODE TargetResetAndBreak() {
	RETCODE retcode = TargetRaiseNMICE();
	if (retcode != RET_SUCC) return retcode;
	unsigned short Reg0DBackup = TargetRegisterRead(0xD);
	unsigned short Reg10Backup = TargetRegisterRead(0x10);
	unsigned short Reg11Backup = TargetRegisterRead(0x11);
	TargetRegisterWrite(0x60, 1);
	TargetRegisterWrite(0x63, 0);
	TargetRegisterWrite(0x64, 2);
	TargetRegisterWrite(0x61, 1);
	TargetRegisterWrite(0x10, TargetRegisterRead(0x66));
	TargetRegisterWrite(0x11, 0);
	TargetRegisterWrite(0xD, 2);
	TargetRegisterWrite(0, 1);
	if (!(TargetRegisterRead(0) & 1)) return RET_ERROR;
	delayTicks(0x1500);
	TargetRegisterWrite(0xE, 0);
	TargetRegisterWrite(0, 0);
	absolute_time_t timeout = time_us_64() + 2000000;
	while ((TargetRegisterRead(0) & 0x20) && !time_reached(timeout));
	if (!(TargetRegisterRead(0xE) & 2)) retcode = RET_RESET_FAILURE;
	if (time_reached(timeout)) {
		TargetRegisterWrite(0xE, 0);
		TargetRegisterWrite(0xD, 8);
		timeout = time_us_64() + 2000000;
		while ((TargetRegisterRead(0) & 0x20) && !time_reached(timeout));
		if (!(TargetRegisterRead(0xE) & 0xC)) retcode = RET_ERROR;
		else retcode = RET_RESET_FAILURE;
	}
	TargetRegisterWrite(0xE, 0);
	TargetRegisterWrite(0x10, Reg10Backup);
	TargetRegisterWrite(0x11, Reg11Backup);
	TargetRegisterWrite(0xD, Reg0DBackup);
	return retcode;
}

void TargetFixConnection() {
	TargetRegisterWrite(0, 0);
	TargetRegisterWrite(0, 0);
	TargetRegisterWrite(0, 0);
	TargetRegisterWrite(0, 0);
	TargetRegisterRead(0);
}

void TargetInputPassword(int size) {
	for (int i = size - 1; i >= 0; i--)
		TargetRegisterWrite(PasswordRegList[i], PasswordBuf[i]);
}

RETCODE Cmd0500_MemoryWrite(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (isTargetAvailable) {
			RETCODE retcode = RET_SUCC;
			RESOURCE_NUMBER resNum = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 2);
			unsigned int startAddr = BYTEARRAY_DWORD_READ_BE(ReceivePacket.payload, 4);
			int alignMode = ReceivePacket.payload[8];
			unsigned short size = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 9);
			unsigned int endAddr;
			switch (alignMode)
			{
			case 1:
				startAddr &= 0xFFFE;
				endAddr = startAddr + 2 * size - 1;
				break;
			case 2:
				endAddr = startAddr + size - 1;
				break;
			default:
				return RET_INVALID_PARAM;
			}
			unsigned short writeCount = 0;
			switch (resNum)
			{
			case RES_CODE_FLASH_NEAR:
				if (endAddr > 0xFFFF) return RET_ADDR_OUT_OF_RANGE;
				goto code_flash_write;
			case RES_CODE_FLASH_FAR:
				if (startAddr < 0x10000 || endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				code_flash_write:
				if (startAddr < 0x100000) {
					// TODO: Turn on BUSY indicator
					retcode = TargetFlashWrite(ReceivePacket.payload + 11, startAddr, endAddr, alignMode, &writeCount);
					if (retcode != RET_SUCC) {
						// TODO: Disable VPP
						TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
						TargetRegisterWrite(0x60, 0);
						TargetRegisterWrite(0x61, 0);
					}
					// TODO: Turn off BUSY indicator
				} else {
					retcode = TargetDataMemoryWrite(ReceivePacket.payload + 11, startAddr, endAddr, alignMode, &writeCount);
				}
				break;
			case RES_DATA_FLASH_NEAR:
				if (endAddr > 0xFFFF) return RET_ADDR_OUT_OF_RANGE;
				goto data_flash_write;
			case RES_DATA_FLASH_FAR:
				if (startAddr < 0x10000 || endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				data_flash_write:
				// TODO: Turn on BUSY indicator
				retcode = TargetDataFlashWrite(ReceivePacket.payload + 11, startAddr, endAddr, alignMode, &writeCount);
				if (retcode != RET_SUCC) {
					TargetRegisterWrite(0x60, 0);
					TargetRegisterWrite(0x61, 0);
				}
				// TODO: Turn off BUSY indicator
				break;
			case RES_DATA_MEMORY:
				if (endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				retcode = TargetDataMemoryWrite(ReceivePacket.payload + 11, startAddr, endAddr, alignMode, &writeCount);
				break;
			default:
				return RET_INVALID_RES_NUMBER;
			}
			if (!retcode) {
				BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
				RspPayloadSize = 2;
			}
			return retcode;
		}
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0504_GetMemFillState(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (isTargetAvailable) {
			BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, MemFillRetcode);
			MemFillRetcode = RET_SUCC;
			RspPayload[2] = isMemFillAvailable;
			RspPayloadSize = 3;
			return RET_SUCC;
		}
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0506_SyncMemFillState(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (isTargetAvailable) {
			isMemFillAvailable = true;
			if (MemFillRetcode != RET_SUCC) {
				RspPayload[0] = (unsigned char)MemFillRetcode;
				BYTEARRAY_DWORD_WRITE_BE(RspPayload, 1, MemFillEndAddr);
			} else {
				BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
				BYTEARRAY_DWORD_WRITE_BE(RspPayload, 2, MemFillEndAddr);
			}
			RspPayloadSize = 6;
			return RET_SUCC;
		}
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0502_MemoryFill(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (isTargetAvailable) {
			RETCODE retcode = RET_SUCC;
			RESOURCE_NUMBER resNum = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 2);
			unsigned int startAddr = BYTEARRAY_DWORD_READ_BE(ReceivePacket.payload, 4);
			unsigned int endAddr = BYTEARRAY_DWORD_READ_BE(ReceivePacket.payload, 8);
			int alignMode = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 12);
			unsigned short FillWord = 0;
			unsigned char FillByte = 0;
			if (endAddr < startAddr) return RET_ADDR_OUT_OF_RANGE;
			switch (alignMode)
			{
			case 1:
				FillWord = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 14);
				break;
			case 2:
				FillByte = ReceivePacket.payload[14];
				break;
			default:
				return RET_INVALID_PARAM;
			}
			int blockStartAddr;
			int blockEndAddr;
			switch (resNum)
			{
			case RES_CODE_FLASH_NEAR:
				if (endAddr > 0xFFFF) return RET_ADDR_OUT_OF_RANGE;
				goto code_flash_fill;
			case RES_CODE_FLASH_FAR:
				if (startAddr < 0x10000 || endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				code_flash_fill:
				BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
				RspPayloadSize = 2;
				MemFillRetcode = RET_SUCC;
				isMemFillAvailable = false;
				if (!targetInfo.CodeBlocks[0].BlockEndAddr) return RET_TARGET_INFO_ERROR;
				int firstBlockIdx = 0;
				while (targetInfo.CodeBlocks[firstBlockIdx].BlockStartAddr > startAddr || targetInfo.CodeBlocks[firstBlockIdx].BlockEndAddr < startAddr) {
					firstBlockIdx++;
					if (firstBlockIdx >= 0x100 || !targetInfo.CodeBlocks[firstBlockIdx].BlockEndAddr)
						return RET_ADDR_OUT_OF_RANGE;
				}
				int lastBlockIdx = 0;
				while (targetInfo.CodeBlocks[lastBlockIdx].BlockStartAddr > endAddr || targetInfo.CodeBlocks[lastBlockIdx].BlockEndAddr < endAddr) {
					lastBlockIdx++;
					if (lastBlockIdx >= 0x100 || !targetInfo.CodeBlocks[lastBlockIdx].BlockEndAddr)
						return RET_ADDR_OUT_OF_RANGE;
				}
				int blockIdx = firstBlockIdx;
				blockStartAddr = targetInfo.CodeBlocks[blockIdx].BlockStartAddr;
				do {
					blockEndAddr = targetInfo.CodeBlocks[blockIdx].BlockEndAddr;
					retcode = TargetFlashFill(blockStartAddr, blockEndAddr, startAddr, endAddr, alignMode, FillWord, FillByte);
					if (retcode != RET_SUCC) {
						isMemFillAvailable = true;
						MemFillRetcode = retcode;
						return RET_SUCC;
					}
					if (++blockIdx >= 0x100) return RET_TARGET_INFO_ERROR;
					blockStartAddr = targetInfo.CodeBlocks[blockIdx].BlockStartAddr;
					if (!blockStartAddr) blockStartAddr = blockEndAddr + 1;
				} while (blockEndAddr < targetInfo.CodeBlocks[lastBlockIdx].BlockEndAddr);
				isMemFillAvailable = true;
				MemFillEndAddr = blockEndAddr;
				return RET_SUCC;
			case RES_DATA_FLASH_NEAR:
				if (endAddr > 0xFFFF) return RET_ADDR_OUT_OF_RANGE;
				goto data_flash_fill;
			case RES_DATA_FLASH_FAR:
				if (startAddr < 0x10000 || endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				data_flash_fill:
				BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
				RspPayloadSize = 2;
				MemFillRetcode = RET_SUCC;
				blockStartAddr = startAddr;
				do {
					blockEndAddr = endAddr > blockStartAddr + 0xFF ? blockStartAddr + 0xFF : endAddr;
					// TODO: Turn on BUSY indicator
					retcode = TargetDataFlashFill(blockStartAddr, blockEndAddr, alignMode, FillWord, FillByte);
					if (retcode != RET_SUCC) {
						TargetRegisterWrite(0x60, 0);
						TargetRegisterWrite(0x61, 0);
						// TODO: Turn off BUSY indicator
						MemFillEndAddr = alignMode == 1 ? -2 : -1;
						MemFillRetcode = retcode;
						return RET_SUCC;
					}
					// TODO: Turn off BUSY indicator
					blockStartAddr = blockEndAddr + 1;
				} while (blockEndAddr < endAddr);
				MemFillEndAddr = blockEndAddr;
				return RET_SUCC;
			case RES_DATA_MEMORY:
				if (endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
				RspPayloadSize = 2;
				MemFillRetcode = RET_SUCC;
				blockStartAddr = startAddr;
				do {
					blockEndAddr = endAddr > blockStartAddr + 0xFF ? blockStartAddr + 0xFF : endAddr;
					retcode = TargetDataMemoryFill(blockStartAddr, blockEndAddr, alignMode, FillWord, FillByte);
					if (retcode != RET_SUCC) {
						MemFillEndAddr = alignMode == 1 ? -2 : -1;
						MemFillRetcode = retcode;
						return RET_SUCC;
					}
					blockStartAddr = blockEndAddr + 1;
				} while (blockEndAddr < endAddr);
				MemFillEndAddr = blockEndAddr;
				return RET_SUCC;
			default:
				return RET_INVALID_RES_NUMBER;
			}
		}
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0510_MemoryRead(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (isTargetAvailable) {
			if (!(TargetInfoState & 1)) return RET_TARGET_INFO_ERROR;
			RETCODE retcode = RET_SUCC;
			RESOURCE_NUMBER resNum = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 2);
			unsigned int startAddr = BYTEARRAY_DWORD_READ_BE(ReceivePacket.payload, 4);
			unsigned short size = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 8);
			int alignMode = ReceivePacket.payload[10];
			if (alignMode == 1) {
				startAddr &= 0xFFFE;
				size <<= 1;
			} else if (alignMode != 2) {
				return RET_INVALID_PARAM;
			}
			unsigned int endAddr = startAddr + size - 1;
			BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
			BYTEARRAY_WORD_WRITE_BE(RspPayload, 2, size);
			RspPayloadSize = 4 + size;
			switch (resNum)
			{
			case RES_CODE_FLASH_NEAR:
				if (endAddr > 0xFFFF) return RET_ADDR_OUT_OF_RANGE;
				goto code_flash_read;
			case RES_CODE_FLASH_FAR:
				if (startAddr < 0x10000 || endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				code_flash_read:
				if (startAddr < 0x100000) {
					retcode = TargetFlashRead(RspPayload + 4, startAddr, endAddr, alignMode);
					if (retcode != RET_SUCC) {
						TargetRegisterWrite(0x60, 0);
						TargetRegisterWrite(0x61, 0);
						return retcode;
					}
					return RET_SUCC;
				}
				return TargetDataMemoryRead(RspPayload + 4, startAddr, endAddr, alignMode);
			case RES_DATA_FLASH_NEAR:
				if (endAddr > 0xFFFF) return RET_ADDR_OUT_OF_RANGE;
				goto data_flash_read;
			case RES_DATA_FLASH_FAR:
				if (startAddr < 0x10000 || endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				data_flash_read:
				retcode = TargetFlashRead(RspPayload + 4, startAddr, endAddr, alignMode);
				if (retcode != RET_SUCC) {
					TargetRegisterWrite(0x60, 0);
					TargetRegisterWrite(0x61, 0);
					return retcode;
				}
				return RET_SUCC;
			case RES_DATA_MEMORY:
				if (endAddr > 0xFFFFFF) return RET_ADDR_OUT_OF_RANGE;
				if (!targetInfo.ROMReadEnabled || startAddr > targetInfo.ROMWindowEnd || endAddr < targetInfo.ROMWindowStart)
					return TargetDataMemoryRead(RspPayload + 4, startAddr, endAddr, alignMode);
				if (startAddr >= targetInfo.ROMWindowStart) {
					if (endAddr <= targetInfo.ROMWindowEnd) {
						retcode = TargetFlashRead(RspPayload + 4, startAddr, endAddr, alignMode);
						if (retcode != RET_SUCC) {
							TargetRegisterWrite(0x60, 0);
							TargetRegisterWrite(0x61, 0);
							return retcode;
						}
						return RET_SUCC;
					} else {
						retcode = TargetFlashRead(RspPayload + 4, startAddr, targetInfo.ROMWindowEnd, alignMode);
						if (retcode != RET_SUCC) {
							TargetRegisterWrite(0x60, 0);
							TargetRegisterWrite(0x61, 0);
							return retcode;
						}
						return TargetDataMemoryRead(RspPayload + 4 + (targetInfo.ROMWindowEnd - startAddr + 1), targetInfo.ROMWindowEnd + 1, endAddr, alignMode);
					}
				} else {
					if (endAddr <= targetInfo.ROMWindowEnd) {
						retcode = TargetDataMemoryRead(RspPayload + 4, startAddr, targetInfo.ROMWindowStart - 1, alignMode);
						if (retcode != RET_SUCC) return retcode;
						retcode = TargetFlashRead(RspPayload + 4 + (targetInfo.ROMWindowStart - startAddr), targetInfo.ROMWindowStart, endAddr, alignMode);
						if (retcode != RET_SUCC) {
							TargetRegisterWrite(0x60, 0);
							TargetRegisterWrite(0x61, 0);
							return retcode;
						}
						return RET_SUCC;
					} else {
						retcode = TargetDataMemoryRead(RspPayload + 4, startAddr, targetInfo.ROMWindowStart - 1, alignMode);
						if (retcode != RET_SUCC) return retcode;
						retcode = TargetFlashRead(RspPayload + 4 + (targetInfo.ROMWindowStart - startAddr), targetInfo.ROMWindowStart, targetInfo.ROMWindowEnd, alignMode);
						if (retcode != RET_SUCC) {
							TargetRegisterWrite(0x60, 0);
							TargetRegisterWrite(0x61, 0);
							return retcode;
						}
						return TargetDataMemoryRead(RspPayload + 4 + (targetInfo.ROMWindowEnd - startAddr + 1), targetInfo.ROMWindowEnd + 1, endAddr, alignMode);
					}
				}
			default:
				return RET_INVALID_RES_NUMBER;
			}
		}
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0700_ResetAndBreak(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (isTargetAvailable) {
			RETCODE retcode = RET_SUCC;
			if (retcode = TargetResetAndBreak()) return retcode;
			if (retcode = TargetBackupCPURegisters()) return retcode;
			// TODO: Turn off BUSY indicator
			NMICEFlag = 0;
			EmulationTime = 0;
			BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
			RspPayloadSize = 2;
			return RET_SUCC;
		}
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd1210_InitializeFlash(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (!(TargetInfoState & 1)) return RET_TARGET_INFO_ERROR;
		if (!(TargetRegisterRead(0x48) && targetInfo.LockedFlashInitDisabled)) {
			RETCODE retcode = TargetFlashChipErase();
			if (retcode != RET_SUCC) {
				// TODO: Disable VPP
				TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
				TargetRegisterWrite(0x60, 0);
				TargetRegisterWrite(0x61, 0);
				return retcode;
			}
#if FLASH_FILL_MASK_OPTION_ON_INIT
			unsigned char MOBuffer[0x800];
			if (targetInfo.MaskOptionAreaSize) {
				memset(MOBuffer, 0xFF, targetInfo.MaskOptionAreaSize);
				unsigned short writeCount = 0;
				retcode = TargetFlashWrite(MOBuffer,
					targetInfo.MaskOptionAreaAddr,
					targetInfo.MaskOptionAreaAddr + targetInfo.MaskOptionAreaSize - 1,
					2,
					&writeCount);
				if (retcode != RET_SUCC) {
					// TODO: Disable VPP
					TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
					TargetRegisterWrite(0x60, 0);
					TargetRegisterWrite(0x61, 0);
					return retcode;
				}
			}
#endif
		}
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
		RspPayloadSize = 2;
		return RET_SUCC;
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd1212_FlashBlockErase(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (!(TargetInfoState & 1)) return RET_TARGET_INFO_ERROR;
		unsigned int addr = BYTEARRAY_DWORD_READ_BE(ReceivePacket.payload, 2);
		int blockIdx = 0;
		while (targetInfo.CodeBlocks[blockIdx].BlockEndAddr) {
			if (targetInfo.CodeBlocks[blockIdx].BlockStartAddr <= addr && targetInfo.CodeBlocks[blockIdx].BlockEndAddr >= addr)
				break;
			blockIdx++;
		}
		addr = targetInfo.CodeBlocks[blockIdx].BlockStartAddr;
		RETCODE retcode = TargetFlashBlockErase(addr);
		if (retcode != RET_SUCC) {
			// TODO: Disable VPP
			TargetRegisterWrite(0x67, TargetRegisterRead(0x67) & ~1);
			TargetRegisterWrite(0x60, 0);
			TargetRegisterWrite(0x61, 0);
			return retcode;
		}
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
		RspPayload[2] = targetInfo.FlashPwdAddr >= addr && targetInfo.FlashPwdAddr <= targetInfo.CodeBlocks[blockIdx].BlockEndAddr;
		RspPayloadSize = 3;
		return RET_SUCC;
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd1230_InputPassword(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		if (PasswordInputCnt < PASSWORD_INPUT_CNT_MAX) {
			int bufSize = (ReceivePacket.payloadSize - 2) & 0xFFFFFFFE;
			if (bufSize == 2) {
				TargetRegisterWrite(0x44, BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 2));
			} else if (bufSize) {
				PasswordBuf[0] = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, bufSize - 2);
				PasswordBuf[1] = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, bufSize);
				for (int i = 2, j = bufSize - 4; i < 10 && j; i++, j -= 2)
					PasswordBuf[i] = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, j);
				if (bufSize > 20) bufSize = 20;
				TargetInputPassword(bufSize >> 1);
			}
			PasswordInputCnt |= 0x8000;
		}
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
		RspPayloadSize = 2;
		return RET_SUCC;
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd1232_SyncLockState(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
		unsigned short lockState = TargetRegisterRead(0x48);
		if (lockState) {
			if (PasswordInputCnt & 0x8000) {
				PasswordInputCnt = (PasswordInputCnt & 0x7FFF) + 1;
				if (PasswordInputCnt >= 0xF) PasswordInputCnt = 0xF;
			}
		} else {
			PasswordInputCnt = 0;
			if (TargetBackupCPURegisters()) return RET_TIMEOUT;
		}
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 2, lockState);
		RspPayloadSize = 4;
		return RET_SUCC;
	case STATE_BUSY:
		return RET_BUSY;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0320_GetNMICESource(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
	case STATE_BUSY:
		unsigned short NMICESource = 0;
		if (NMICEFlag) {
			unsigned int ELR3 = ((TargetRegisterRead(0xB) & 0xF000) << 4) | TargetRegisterRead(0xA);
			unsigned int BP1_addr = ((TargetRegisterRead(0x11) & 0x00F0) << 12) | TargetRegisterRead(0x12);
			if (NMICEFlag & 2) NMICESource |= (NMICEControl & 0x20) && ELR3 == BP1_addr ? 2 : 4;
			if (NMICEFlag & 0xC) NMICESource |= (NMICEControl & 4) ? 0x40 : 0x80;
			if (NMICEFlag & 0x10) NMICESource |= 8;
		}
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 2, 0xFEFF);
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 4, NMICESource);
		RspPayloadSize = 6;
		return RET_SUCC;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0A00_GetInfo(void) {
	RESOURCE_NUMBER resNum = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 2);
	if (resNum == RES_ERROR) {
		// TODO: Blink POWER indicator
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
		RspPayload[2] = 0;
		RspPayloadSize = 3;
		return RET_SUCC;
	}
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
	case STATE_BUSY:
		break;
	default:
		return RET_ERROR;
	}
	switch (resNum)
	{
	case RES_ICE_TYPE:
		strcpy(&RspPayload[3], ice_type);
		break;
	case RES_TRG_NAME:
		strcpy(&RspPayload[3], targetInfo.TargetNameStr);
		break;
	case RES_FIRMWARE_VERSION:
		strcpy(&RspPayload[3], firmware_version);
		break;
	case RES_HARDWARE_VERSION:
		RspPayload[3] = '0';
		RspPayload[4] = hardware_version + '1';
		RspPayload[5] = 0;
		break;
	case RES_OCD_ID:
		unsigned char OCDId_L = (OCD_ID >> 8) & 0xF;
		unsigned char OCDId_H = OCD_ID >> 12;
		RspPayload[3] = OCDId_H >= 0xA ? OCDId_H + 'A' - 10 : OCDId_H + '0';
		RspPayload[4] = OCDId_L >= 0xA ? OCDId_L + 'A' - 10 : OCDId_L + '0';
		RspPayload[5] = 0;
		break;
	default:
		return RET_INVALID_RES_NUMBER;
	}
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
	size_t length = strlen(&RspPayload[3]);
	RspPayload[2] = length;
	RspPayloadSize = length + 3;
	return RET_SUCC;
}

RETCODE Cmd0A04_SetTargetInfo(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
	case STATE_BUSY:
		targetInfo.ROMWindowStart = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 2);
		targetInfo.ROMWindowEnd = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 4);
		targetInfo.FlashPwdAddr = BYTEARRAY_DWORD_READ_BE(ReceivePacket.payload, 6);
		targetInfo.BreakpointNum = ReceivePacket.payload[10];
		TargetInfoState |= 2;
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_SUCC);
		RspPayloadSize = 2;
		return RET_SUCC;
	default:
		return RET_ERROR;
	}
}

RETCODE Cmd0A05_ParseTargetID(void) {
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		return RET_ILLEGAL_VDD;
	case STATE_DEVICE_IDLE:
		return RET_TARGET_NOT_CONNECTED;
	case STATE_TARGET_IDLE:
	case STATE_BUSY:
		break;
	default:
		return RET_ERROR;
	}
	targetID.Word0 = TargetRegisterRead(0x40);
	targetID.Word1 = TargetRegisterRead(0x41);
	targetID.Word2 = TargetRegisterRead(0x42);
	targetID.Word3 = TargetRegisterRead(0x43);
	targetID.Word4 = TargetRegisterRead(0x50);
	targetID.Word5 = TargetRegisterRead(0x51);
	targetID.Word6 = TargetRegisterRead(0x52);
	targetID.Word7 = TargetRegisterRead(0x53);
	for (int i = 0; i < 3; i++) {
		if (targetID.Word0 == TargetIDToFix[i].Word0
		&& targetID.Word1 == TargetIDToFix[i].Word1
		&& targetID.Word2 == TargetIDToFix[i].Word2
		&& targetID.Word3 == TargetIDToFix[i].Word3) {
			targetID.Word3 = 0x3F18;
			break;
		}
	}
	if (targetID.IdVersion & 4) {
		// 4-bit signed shift count
		targetInfo.CodeFlashBlockSize = targetID.CodeFlashBlockSize & 8 ?
			0x800 >> ((-targetID.CodeFlashBlockSize) & 0xF) :
			0x800 << targetID.CodeFlashBlockSize;
		// 3-bit signed shift count
		targetInfo.MaskOptionAreaSize = (targetID.MaskOptionAreaSize & 4 ?
			0x200 >> ((-targetID.MaskOptionAreaSize) & 7) :
			0x200 << targetID.MaskOptionAreaSize) & 0xFFF;
		// 3-bit signed shift count
		targetInfo.TestAreaSize = (targetID.TestAreaSize & 4 ?
			0x200 >> ((-targetID.TestAreaSize) & 7) :
			0x200 << targetID.TestAreaSize) & 0xFFF;
		targetInfo.CodeFlashBlockNumNear = targetID.CodeFlashBlockNumNear_L | (targetID.CodeFlashBlockNumNear_H << 4);
		targetInfo.CodeFlashBlockNumFar = targetID.CodeFlashBlockNumFar_L | (targetID.CodeFlashBlockNumFar_H << 4);
	} else {
		targetInfo.CodeFlashBlockSize = (unsigned short)targetID.CodeFlashBlockSize << 11;
		targetInfo.CodeFlashBlockNumNear = targetID.CodeFlashBlockNumNear_L;
		targetInfo.CodeFlashBlockNumFar = targetID.CodeFlashBlockNumFar_L;
		targetInfo.MaskOptionAreaSize = 0x200;
		targetInfo.TestAreaSize = 0x200;
	}
	targetInfo.TestAreaAddr = targetInfo.CodeFlashBlockSize * targetInfo.CodeFlashBlockNumNear - targetInfo.TestAreaSize;
	targetInfo.MaskOptionAreaAddr = targetInfo.TestAreaAddr - targetInfo.MaskOptionAreaSize;
	targetInfo.FlashPwdAddr = targetInfo.TestAreaAddr - 0x10;
	targetInfo.ROMWindowEnd = ((unsigned short)targetID.ROMWindowEnd << 8) | 0xFF;
	for (int i = 0; i < targetInfo.CodeFlashBlockNumFar; i++) {
		targetInfo.CodeBlocks[i].BlockStartAddr = targetInfo.CodeFlashBlockSize * i;
		targetInfo.CodeBlocks[i].BlockEndAddr = targetInfo.CodeFlashBlockSize * (i + 1) - 1;
	}
	for (int i = targetInfo.CodeFlashBlockNumFar; i < 0x100; i++) {
		targetInfo.CodeBlocks[i].BlockStartAddr = 0;
		targetInfo.CodeBlocks[i].BlockEndAddr = 0;
	}
	targetInfo.ROMReadEnabled = targetID.ROMReadEnabled;
	targetInfo.CoreRev = targetID.CoreRev;
	targetInfo.FlashCharacteristics = targetID.FlashCharacteristics;
	targetInfo.MemoryModel = targetID.MemoryModel;
	targetInfo.LockedFlashInitDisabled = targetID.LockedFlashInitDisabled;
	FCON_addr = 0xF003;
	L2HBIT = 3;
	H2LBIT = 3;
	TargetInfoState |= 1;
	BYTEARRAY_WORD_WRITE_LE(RspPayload, 0, 0);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 2, targetID.Word7);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 4, targetID.Word6);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 6, targetID.Word5);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 8, targetID.Word4);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 10, targetID.Word3);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 12, targetID.Word2);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 14, targetID.Word1);
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 16, targetID.Word0);
	RspPayloadSize = 18;
	return RET_SUCC;
}

RETCODE Cmd00FD_InitConnection(void) {
	RETCODE retcode = RET_SUCC;
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		if (ConnectionState != TRG_NOT_CONNECTED) {
			retcode = RET_ILLEGAL_VDD;
			break;
		}
	case STATE_DEVICE_IDLE:
		if (ConnectionState != TRG_CONNECTED) {
			ConnectionState = TRG_CONNECTION_INIT;
		}
		OCD_ID = TargetRegisterRead(0) & 0xFF80;
		if (OCD_ID != 0x480) {
			TargetRegisterWrite(0, 0xAAFE);
			OCD_ID = TargetRegisterRead(0) & 0xFF80;
		}
		if (OCD_ID == 0x480) {
			ConnectionState = TRG_CONNECTED;
			retcode = RET_SUCC;
			if (TargetResetFlag) {
				TargetRegisterWrite(0, 1);
				TargetRegisterRead(0);
				delayTicks(0x1500);
				TargetRegisterWrite(0, 0);
				TargetResetFlag = false;
				TargetRegisterWrite(0xD, 8);
				absolute_time_t timeout = time_us_64() + 2000000;
				while (TargetRegisterRead(0) & 0x20) {
					if (time_reached(timeout)) {
						retcode = RET_ERROR;
						break;
					}
				}
				TargetRegisterWrite(0xD, 0);
			} else {
				retcode = TargetRaiseNMICE();
			}
			if (retcode != RET_SUCC) {
				RspPayload[2] = 1;
				SetGlobalState(STATE_CONNECTION_ERROR);
			} else {
				FCON_addr = 0xF003;
				L2HBIT = 3;
				H2LBIT = 3;
				RspPayload[2] = 3;
				SetGlobalState(STATE_TARGET_IDLE);
				memset(&PasswordBuf, 0xFF, 20);
				TargetInputPassword(10);
			}
		} else {
			RspPayload[2] = 0;
			SetGlobalState(STATE_CONNECTION_ERROR);
		}
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, 0);
		RspPayloadSize = 3;
		break;
	case STATE_TARGET_IDLE:
	case STATE_BUSY:
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, 0);
		RspPayload[2] = 3;
		RspPayloadSize = 3;
		retcode = RET_SUCC;
		break;
	default:
		retcode = RET_ERROR;
		break;
	}
	return retcode;
}

RETCODE Cmd0706_ResetConnection(void) {
	RETCODE retcode = RET_SUCC;
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
		retcode = RET_ILLEGAL_VDD;
		break;
	case STATE_DEVICE_IDLE:
		retcode = STATE_DEVICE_IDLE;
		if (ReceivePacket.payload[2] == 1) return retcode;
		break;
	case STATE_CONNECTION_ERROR:
	case STATE_TARGET_IDLE:
	case STATE_BUSY:
		if (ConnectionState == TRG_CONNECTED) TargetRegisterWrite(0, TargetRegisterRead(0) & 0xFFF7);
		TargetHardwareReset();
		busy_wait_ms(300);
		break;
	default:
		return RET_ERROR;
	}
	switch (ReceivePacket.payload[2])
	{
	case 0:
		SetGlobalState(STATE_DEVICE_IDLE);
		TargetResetFlag = true;
		ConnectionState = TRG_NOT_CONNECTED;
		memset(&targetInfo, 0, sizeof(TargetInfo));
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_TARGET_NOT_CONNECTED);
		RspPayload[2] = 0;
		RspPayloadSize = 3;
		return RET_SUCC;
	case 1:
		TargetRegisterWrite(0, 0xAAFE);
		OCD_ID = TargetRegisterRead(0) & 0xFF80;
		if (OCD_ID != 0x480) {
			SetGlobalState(STATE_CONNECTION_ERROR);
			return RET_CONNECTION_ERROR;
		}
		ConnectionState = TRG_CONNECTED;
		memset(&PasswordBuf, 0xFF, 20);
		TargetInputPassword(10);
		TargetRegisterWrite(0, 1);
		TargetRegisterRead(0);
		delayTicks(0x1500);
		TargetRegisterWrite(0, 0);
		if (TargetRegisterRead(0x48)) retcode = TargetRaiseNMICE();
		else TargetResetAndBreak();
		if (retcode != RET_SUCC) {
			SetGlobalState(STATE_CONNECTION_ERROR);
			return retcode == RET_RESET_FAILURE ? retcode : RET_TIMEOUT;
		}
		SetGlobalState(STATE_TARGET_IDLE);
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, 0);
		RspPayload[2] = 3;
		RspPayloadSize = 3;
		return retcode;
	case 2:
		// TODO: Reset device
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_TARGET_NOT_CONNECTED);
		RspPayload[2] = 0;
		RspPayloadSize = 3;
		return RET_SUCC;
	default:
		return RET_PARAM_TOO_LARGE;
	}
}

RETCODE Cmd0708_UninitConnection(void) {
	switch (ReceivePacket.payload[2])
	{
	case 0:
		switch (GlobalState)
		{
		case STATE_ILLEGAL_VDD:
			return RET_ILLEGAL_VDD;
		case STATE_TARGET_IDLE:
			return RET_TRG_NOT_IN_EMULATION;
		case STATE_DEVICE_IDLE:
		case STATE_BUSY:
			TargetRegisterWrite(0xD, 0);
			TargetRegisterWrite(0xF, 0x5555);
			break;
		default:
			return RET_ERROR;
		}
		break;
	case 1:
		switch (GlobalState)
		{
		case STATE_TARGET_IDLE:
		case STATE_BUSY:
			if (targetInfo.ROMReadEnabled && targetInfo.MaskOptionAreaSize) {
				unsigned short PasswordLoDword[2];
				TargetFlashRead(PasswordLoDword, targetInfo.FlashPwdAddr, targetInfo.FlashPwdAddr + 3, 1);
				TargetRegisterWrite(0x44, PasswordLoDword[0] ? 0 : 0xFFFF);
				TargetRegisterWrite(0x45, PasswordLoDword[1]);
			}
			break;
		case STATE_ILLEGAL_VDD:
		case STATE_DEVICE_IDLE:
		case STATE_CONNECTION_ERROR:
			break;
		default:
			return RET_ERROR;
		}
		break;
	default:
		return RET_PARAM_TOO_LARGE;
	}
	SetGlobalState(STATE_DEVICE_IDLE);
	ConnectionState = TRG_NOT_CONNECTED;
	memset(&targetInfo, 0, sizeof(TargetInfo));
	BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, RET_TARGET_NOT_CONNECTED);
	RspPayloadSize = 2;
	return RET_SUCC;
}

// Not supported
RETCODE Cmd1400_UpdateFirmware(void) {
	RETCODE retcode;
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
	case STATE_DEVICE_IDLE:
	case STATE_CONNECTION_ERROR:
	case STATE_TARGET_IDLE:
		retcode = RET_ISP_VERIFY_FAILURE;
		break;
	case STATE_BUSY:
		retcode = RET_BUSY;
		break;
	default:
		retcode = RET_ERROR;
		break;
	}
	return retcode;
}

RETCODE CmdFFFF_InvalidCommand(void) {
	RETCODE retcode;
	switch (GlobalState)
	{
	case STATE_ILLEGAL_VDD:
	case STATE_CONNECTION_ERROR:
	case STATE_TARGET_IDLE:
	case STATE_BUSY:
		retcode = RET_INVALID_COMMAND;
		break;
	case STATE_DEVICE_IDLE:
		retcode = RET_TARGET_NOT_CONNECTED;
		break;
	default:
		retcode = RET_ERROR;
		break;
	}
	return retcode;
}

uEASECommand const CmdList[] = {
	{0x0500, Cmd0500_MemoryWrite},
	{0x0502, Cmd0502_MemoryFill},
	{0x0504, Cmd0504_GetMemFillState},
	{0x0506, Cmd0506_SyncMemFillState},
	{0x0510, Cmd0510_MemoryRead},
	{0x0700, Cmd0700_ResetAndBreak},
	{0x1210, Cmd1210_InitializeFlash},
	{0x1212, Cmd1212_FlashBlockErase},
	{0x1230, Cmd1230_InputPassword},
	{0x1232, Cmd1232_SyncLockState},
	{0x0320, Cmd0320_GetNMICESource},
	{0x0A00, Cmd0A00_GetInfo},
	{0x0A04, Cmd0A04_SetTargetInfo},
	{0x0A05, Cmd0A05_ParseTargetID},
	{0x0706, Cmd0706_ResetConnection},
	{0x00FD, Cmd00FD_InitConnection},
	{0x0708, Cmd0708_UninitConnection},
	{0x1400, Cmd1400_UpdateFirmware},
	{0xFFFF, CmdFFFF_InvalidCommand},
};

bool CheckPacketValidity() {
	if (ReceivePacket.magic == 0x4D524843 && ReceivePacket.idWrapper_L == 1 && ReceivePacket.idWrapper_H == 1ull << 40) {
		PendingPacketID = ReceivePacket.id;
		return true;
	}
	return false;
}

unsigned int RspPacketInit(unsigned int size) {
	TransmitPacket.magic = 0x4D504843;
	TransmitPacket.idWrapper_L = 1;
	TransmitPacket.id = PendingPacketID;
	TransmitPacket.idWrapper_H = 1ull << 40;
	TransmitPacket.payloadSize = size;
	memcpy(TransmitPacket.payload, RspPayload, size);
	return size + 0x10;
}

void SendRspPacket(RETCODE retcode) {
	if (retcode != RET_SUCC) {
		BYTEARRAY_WORD_WRITE_BE(RspPayload, 0, retcode);
		SendPacket(RspPacketInit(2));
		return;
	}
	SendPacket(RspPacketInit(RspPayloadSize));
}

RETCODE parseReceivePacket(void) {
	CheckPacketValidity();
	int idx = 0;
	unsigned short cmdId = BYTEARRAY_WORD_READ_BE(ReceivePacket.payload, 0);
	while (CmdList[idx].CmdId != 0xFFFF) {
		if (CmdList[idx].CmdId == cmdId)
			break;
		idx++;
	}
	if (ConnectionState == TRG_CONNECTED && (TargetRegisterRead(0) & 0xFF80) != 0x480) {
		TargetFixConnection();
		if ((TargetRegisterRead(0) & 0xFF80) != 0x480) return RET_ERROR;
	}
	return CmdList[idx].CmdHandler();
}

int main()
{
	IOInit();
	TimerInit();

	multicore_launch_core1(usb_loop);

	SetGlobalState(STATE_DEVICE_IDLE);
	ConnectionState = TRG_NOT_CONNECTED;
	OCD_ID = 0;
	PasswordInputCnt = 0;
	EmulationTime = 0;
	MemFillRetcode = RET_SUCC;
	MemFillEndAddr = 0;
	OCDStateSyncFlag = false;
	TargetRunningFlag = false;
	TargetResetFlag = false;
	isTargetAvailable = true;
	isMemFillAvailable = true;
	TargetInfoState = 0;

	while (true) {
		if (OCDStateSyncFlag && ConnectionState == TRG_CONNECTED) {
			systick_hw->csr &= ~1;
			OCDStateSyncFlag = false;
			unsigned short OCDState = TargetRegisterRead(0);
			if ((OCDState & 0xFF80) != 0x480) {
				TargetFixConnection();
				OCDState = TargetRegisterRead(0);
			}
			TargetRunningFlag = (OCDState >> 5) & 1;
			systick_hw->csr |= 1;
		}
		if (isPacketReceived()) {
			SendRspPacket(parseReceivePacket());
		}
	}
}
