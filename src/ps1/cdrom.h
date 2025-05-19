/*
 * ps1-bare-metal - (C) 2023-2025 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include <stdint.h>

#define DEF(type) static inline type __attribute__((always_inline))

/* CD-ROM XA sector header structure */

typedef struct __attribute__((packed)) {
	uint8_t file, channel, submode, codingInfo;
} CDROMXAHeader;

typedef enum {
	CDROM_XA_SM_END_OF_RECORD = 1 << 0,
	CDROM_XA_SM_TYPE_VIDEO    = 1 << 1,
	CDROM_XA_SM_TYPE_AUDIO    = 1 << 2,
	CDROM_XA_SM_TYPE_DATA     = 1 << 3,
	CDROM_XA_SM_TRIGGER       = 1 << 4,
	CDROM_XA_SM_FORM2         = 1 << 5,
	CDROM_XA_SM_REAL_TIME     = 1 << 6,
	CDROM_XA_SM_END_OF_FILE   = 1 << 7
} CDROMXASubmodeFlag;

typedef enum {
	CDROM_XA_CI_STEREO              = 1 << 0,
	CDROM_XA_CI_SAMPLE_RATE_BITMASK = 1 << 2,
	CDROM_XA_CI_SAMPLE_RATE_18900   = 0 << 2,
	CDROM_XA_CI_SAMPLE_RATE_37800   = 1 << 2,
	CDROM_XA_CI_BITS_BITMASK        = 1 << 4,
	CDROM_XA_CI_BITS_4              = 0 << 4,
	CDROM_XA_CI_BITS_8              = 1 << 4,
	CDROM_XA_CI_EMPHASIS            = 1 << 6
} CDROMXACodingInfoFlag;

/* CD-ROM drive data types */

typedef struct __attribute__((packed)) {
	uint8_t minute, second, frame;
} CDROMMSF;

typedef struct __attribute__((packed)) {
	CDROMMSF      absoluteMSF;
	uint8_t       mode;
	CDROMXAHeader header;
} CDROMGetlocLResult;

typedef struct __attribute__((packed)) {
	uint8_t  track, index;
	CDROMMSF relativeMSF, absoluteMSF;
} CDROMGetlocPResult;

typedef struct __attribute__((packed)) {
	uint8_t status, flag, type, atip;
	char    license[4];
} CDROMGetIDResult;

typedef struct __attribute__((packed)) {
	uint8_t  status, track, index;
	CDROMMSF msf;
	uint16_t peak;
} CDROMReportPacket;

DEF(uint8_t) cdrom_encodeBCD(uint8_t value) {
	// output = units + tens * 16
	//        = units + tens * 10 + tens * 6
	//        = value             + tens * 6
	return value + (value / 10) * 6;
}

DEF(uint8_t) cdrom_decodeBCD(uint8_t value) {
	// output = low + high * 10
	//        = low + high * 16 - high * 6
	//        = value           - high * 6
	return value - (value >> 4) * 6;
}

DEF(void) cdrom_convertLBAToMSF(CDROMMSF *msf, unsigned int lba) {
	lba += 150; // Skip lead-in area (LBA 0 is always at 00:02:00)

	msf->minute = cdrom_encodeBCD( lba / (75 * 60));
	msf->second = cdrom_encodeBCD((lba / 75) % 60);
	msf->frame  = cdrom_encodeBCD( lba % 75);
}

DEF(unsigned int) cdrom_convertMSFToLBA(const CDROMMSF *msf) {
	return 0
		+ cdrom_decodeBCD(msf->minute) * (75 * 60)
		+ cdrom_decodeBCD(msf->second) * 75
		+ cdrom_decodeBCD(msf->frame)
		- 150;
}

/* CD-ROM drive command and status definitions */

typedef enum {
	CDROM_CMD_NOP        = 0x01,
	CDROM_CMD_SETLOC     = 0x02,
	CDROM_CMD_PLAY       = 0x03,
	CDROM_CMD_FORWARD    = 0x04,
	CDROM_CMD_BACKWARD   = 0x05,
	CDROM_CMD_READ_N     = 0x06,
	CDROM_CMD_STANDBY    = 0x07,
	CDROM_CMD_STOP       = 0x08,
	CDROM_CMD_PAUSE      = 0x09,
	CDROM_CMD_INIT       = 0x0a,
	CDROM_CMD_MUTE       = 0x0b,
	CDROM_CMD_DEMUTE     = 0x0c,
	CDROM_CMD_SETFILTER  = 0x0d,
	CDROM_CMD_SETMODE    = 0x0e,
	CDROM_CMD_GETPARAM   = 0x0f,
	CDROM_CMD_GETLOC_L   = 0x10,
	CDROM_CMD_GETLOC_P   = 0x11,
	CDROM_CMD_SETSESSION = 0x12,
	CDROM_CMD_GET_TN     = 0x13,
	CDROM_CMD_GET_TD     = 0x14,
	CDROM_CMD_SEEK_L     = 0x15,
	CDROM_CMD_SEEK_P     = 0x16,
	CDROM_CMD_TEST       = 0x19,
	CDROM_CMD_GET_ID     = 0x1a,
	CDROM_CMD_READ_S     = 0x1b,
	CDROM_CMD_RESET      = 0x1c,
	CDROM_CMD_GET_Q      = 0x1d, // Versions 0xc1 and later only
	CDROM_CMD_READ_TOC   = 0x1e, // Versions 0xc1 and later only
	CDROM_CMD_UNLOCK0    = 0x50, // Versions 0xc1 and later only
	CDROM_CMD_UNLOCK1    = 0x51, // Versions 0xc1 and later only
	CDROM_CMD_UNLOCK2    = 0x52, // Versions 0xc1 and later only
	CDROM_CMD_UNLOCK3    = 0x53, // Versions 0xc1 and later only
	CDROM_CMD_UNLOCK4    = 0x54, // Versions 0xc1 and later only
	CDROM_CMD_UNLOCK5    = 0x55, // Versions 0xc1 and later only
	CDROM_CMD_UNLOCK6    = 0x56, // Versions 0xc1 and later only
	CDROM_CMD_LOCK       = 0x57  // Versions 0xc1 and later only
} CDROMCommand;

typedef enum {
	CDROM_TEST_READ_ID              = 0x04,
	CDROM_TEST_GET_ID_COUNTERS      = 0x05,
	CDROM_TEST_GET_VERSION          = 0x20,
	CDROM_TEST_GET_SWITCHES         = 0x21,
	CDROM_TEST_GET_REGION           = 0x22, // Versions 0xc1 and later only
	CDROM_TEST_GET_SERVO_TYPE       = 0x23, // Versions 0xc1 and later only
	CDROM_TEST_GET_DSP_TYPE         = 0x24, // Versions 0xc1 and later only
	CDROM_TEST_GET_DECODER_TYPE     = 0x25, // Versions 0xc1 and later only
	CDROM_TEST_DSP_CMD              = 0x50,
	CDROM_TEST_DSP_CMD_RESP         = 0x51, // Versions 0xc2 and later only
	CDROM_TEST_MCU_PEEK             = 0x60,
	CDROM_TEST_DECODER_GET_REG      = 0x71, // Versions 0xc1 and later only
	CDROM_TEST_DECODER_SET_REG      = 0x72, // Versions 0xc1 and later only
	CDROM_TEST_DECODER_GET_SRAM_PTR = 0x75, // Versions 0xc1 and later only
	CDROM_TEST_DECODER_SET_SRAM_PTR = 0x76  // Versions 0xc1 and later only
} CDROMTestCommand;

typedef enum {
	CDROM_IRQ_NONE        = 0,
	CDROM_IRQ_DATA_READY  = 1,
	CDROM_IRQ_COMPLETE    = 2,
	CDROM_IRQ_ACKNOWLEDGE = 3,
	CDROM_IRQ_DATA_END    = 4,
	CDROM_IRQ_ERROR       = 5
} CDROMIRQType;

typedef enum {
	CDROM_CMD_STAT_ERROR      = 1 << 0,
	CDROM_CMD_STAT_SPINDLE_ON = 1 << 1,
	CDROM_CMD_STAT_SEEK_ERROR = 1 << 2,
	CDROM_CMD_STAT_ID_ERROR   = 1 << 3,
	CDROM_CMD_STAT_LID_OPEN   = 1 << 4,
	CDROM_CMD_STAT_READING    = 1 << 5,
	CDROM_CMD_STAT_SEEKING    = 1 << 6,
	CDROM_CMD_STAT_PLAYING    = 1 << 7
} CDROMCommandStatusFlag;

typedef enum {
	CDROM_CMD_ERR_SEEK_FAILED         = 1 << 2,
	CDROM_CMD_ERR_LID_OPENED          = 1 << 3,
	CDROM_CMD_ERR_INVALID_PARAM_VALUE = 1 << 4,
	CDROM_CMD_ERR_INVALID_PARAM_COUNT = 1 << 5,
	CDROM_CMD_ERR_INVALID_COMMAND     = 1 << 6,
	CDROM_CMD_ERR_NO_DISC             = 1 << 7
} CDROMCommandErrorFlag;

typedef enum {
	CDROM_MODE_CDDA         = 1 << 0,
	CDROM_MODE_AUTO_PAUSE   = 1 << 1,
	CDROM_MODE_CDDA_REPORT  = 1 << 2,
	CDROM_MODE_XA_FILTER    = 1 << 3,
	CDROM_MODE_SIZE_BITMASK = 3 << 4,
	CDROM_MODE_SIZE_2048    = 0 << 4,
	CDROM_MODE_SIZE_2340    = 2 << 4,
	CDROM_MODE_XA_ADPCM     = 1 << 6,
	CDROM_MODE_SPEED_1X     = 0 << 7,
	CDROM_MODE_SPEED_2X     = 1 << 7
} CDROMModeFlag;

#undef DEF
