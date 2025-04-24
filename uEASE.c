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
	RES_CODE_NEAR_DIRECT	= 0x3001,
	RES_DATA				= 0x3011,
	RES_CODE_FAR_DIRECT		= 0x3021,
	RES_CODE_NEAR			= 0x3027,
	RES_CODE_FAR			= 0x302B,
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
		unsigned char CodeFlashBlockNumFar_L			: 4;
		union {
			struct __attribute__((__packed__)) {
				unsigned char CodeFlashBlockSize	: 4;
				bool FlashProtectionEnabled			: 1;
				bool FlashVPPDisabled				: 1;
				bool MemoryModel					: 1;
				bool LockedFlashInitDisabled		: 1;
			};
			struct __attribute__((__packed__)) {
				unsigned char PaddingBits			: 4;
				unsigned char FlashCharacteristics	: 2;
			};
		};
		unsigned char ROMWindowEnd					: 8;
		unsigned char CodeFlashBlockNumFar_H			: 4;
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

const char* ice_type = "uEASE";
const char* firmware_version = "3.21";

GLOBAL_STATE GlobalState;
CONNECTION_STATE ConnectionState;
unsigned short OCD_ID;
unsigned short PasswordInputCnt;
unsigned short NMICEFlag;
unsigned short NMICEControl;
unsigned short EmulationTime;
bool OCDStateSyncFlag;
bool TargetRunningFlag;
bool TargetResetFlag;
bool isTargetAvailable;

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
		} while ((offset += 2) < endOffset);
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
		} while ((offset += 2) < endOffset);
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
		} while ((offset += 2) < endOffset);
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
		} while ((offset += 2) < endOffset);
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
		} while ((offset += 2) < endOffset);
		break;
	case 2:
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
		} while ((offset += 2) < endOffset);
		break;
	default:
		return RET_INVALID_PARAM;
	}
	TargetRegisterWrite(0x60, 0);
	TargetRegisterWrite(0x61, 0);
	return RET_SUCC;
}

RETCODE TargetFlashDirectWrite(void* buffer, unsigned int startAddr, unsigned int endAddr, int alignMode, unsigned short* writeCount) {
	
}

static inline RETCODE TargetInstructionExec() {
	TargetRegisterWrite(0xC, 1);
	absolute_time_t timeout = time_us_64() + 2000000;
	while (TargetRegisterRead(0xC))
		if (time_reached(timeout))
			return RET_TIMEOUT;
	return RET_SUCC;
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
	OCDStateSyncFlag = false;
	TargetRunningFlag = false;
	TargetResetFlag = false;
	isTargetAvailable = true;
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
