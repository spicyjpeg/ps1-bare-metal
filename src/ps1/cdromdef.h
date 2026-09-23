/*
 * ps1-bare-metal - (C) 2023-2026 spicyjpeg
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

/* CD-ROM drive command and status definitions */

typedef enum {
	CDROM_CMD_NOP        = 0x01,
	CDROM_CMD_SETLOC     = 0x02,
	CDROM_CMD_PLAY       = 0x03, // Sends data ready IRQs in report mode
	CDROM_CMD_FORWARD    = 0x04, // Sends data ready IRQs in report mode
	CDROM_CMD_BACKWARD   = 0x05, // Sends data ready IRQs in report mode
	CDROM_CMD_READN      = 0x06, // Sends data ready IRQs
	CDROM_CMD_STANDBY    = 0x07, // Sends complete IRQ
	CDROM_CMD_STOP       = 0x08, // Sends complete IRQ
	CDROM_CMD_PAUSE      = 0x09, // Sends complete IRQ
	CDROM_CMD_INIT       = 0x0a, // Sends complete IRQ
	CDROM_CMD_MUTE       = 0x0b,
	CDROM_CMD_DEMUTE     = 0x0c,
	CDROM_CMD_SETFILTER  = 0x0d,
	CDROM_CMD_SETMODE    = 0x0e,
	CDROM_CMD_GETPARAM   = 0x0f,
	CDROM_CMD_GETLOCL    = 0x10,
	CDROM_CMD_GETLOCP    = 0x11,
	CDROM_CMD_SETSESSION = 0x12, // Sends complete IRQ
	CDROM_CMD_GETTN      = 0x13,
	CDROM_CMD_GETTD      = 0x14,
	CDROM_CMD_SEEKL      = 0x15, // Sends complete IRQ
	CDROM_CMD_SEEKP      = 0x16, // Sends complete IRQ
	CDROM_CMD_SETCLOCK   = 0x17, // Broken (DTL-H2000 leftover)
	CDROM_CMD_GETCLOCK   = 0x18, // Broken (DTL-H2000 leftover)
	CDROM_CMD_TEST       = 0x19,
	CDROM_CMD_GETID      = 0x1a, // Sends complete IRQ
	CDROM_CMD_READS      = 0x1b, // Sends data ready IRQs
	CDROM_CMD_RESET      = 0x1c,
	CDROM_CMD_GETQ       = 0x1d, // Versions 0xc1 and later, sends complete IRQ
	CDROM_CMD_READTOC    = 0x1e, // Versions 0xc1 and later, sends complete IRQ
	CDROM_CMD_UNLOCK0    = 0x50, // Non-Japanese versions 0xc1 and later
	CDROM_CMD_UNLOCK1    = 0x51, // Non-Japanese versions 0xc1 and later
	CDROM_CMD_UNLOCK2    = 0x52, // Non-Japanese versions 0xc1 and later
	CDROM_CMD_UNLOCK3    = 0x53, // Non-Japanese versions 0xc1 and later
	CDROM_CMD_UNLOCK4    = 0x54, // Non-Japanese versions 0xc1 and later
	CDROM_CMD_UNLOCK5    = 0x55, // Non-Japanese versions 0xc1 and later
	CDROM_CMD_UNLOCK6    = 0x56, // Non-Japanese versions 0xc1 and later
	CDROM_CMD_LOCK       = 0x57  // Non-Japanese versions 0xc1 and later
} CDROMCommand;

typedef enum {
	CDROM_TEST_READ_ID              = 0x04,
	CDROM_TEST_GET_ID_COUNTERS      = 0x05,
	CDROM_TEST_GET_VERSION          = 0x20,
	CDROM_TEST_GET_SWITCHES         = 0x21,
	CDROM_TEST_GET_REGION           = 0x22, // Versions 0xc1 and later
	CDROM_TEST_GET_SERVO_TYPE       = 0x23, // Versions 0xc1 and later
	CDROM_TEST_GET_DSP_TYPE         = 0x24, // Versions 0xc1 and later
	CDROM_TEST_GET_DECODER_TYPE     = 0x25, // Versions 0xc1 and later
	CDROM_TEST_DSP_CMD              = 0x50,
	CDROM_TEST_DSP_CMD_RESP         = 0x51, // Versions 0xc2 and later
	CDROM_TEST_MCU_PEEK             = 0x60,
	CDROM_TEST_DECODER_GET_REG      = 0x71, // Versions 0xc1 and later
	CDROM_TEST_DECODER_SET_REG      = 0x72, // Versions 0xc1 and later
	CDROM_TEST_DECODER_GET_SRAM_PTR = 0x75, // Versions 0xc1 and later
	CDROM_TEST_DECODER_SET_SRAM_PTR = 0x76  // Versions 0xc1 and later
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
	CDROM_CMDSTAT_ERROR      = 1 << 0,
	CDROM_CMDSTAT_SPINDLE_ON = 1 << 1,
	CDROM_CMDSTAT_SEEK_ERROR = 1 << 2,
	CDROM_CMDSTAT_ID_ERROR   = 1 << 3,
	CDROM_CMDSTAT_LID_OPEN   = 1 << 4,
	CDROM_CMDSTAT_READING    = 1 << 5,
	CDROM_CMDSTAT_SEEKING    = 1 << 6,
	CDROM_CMDSTAT_PLAYING    = 1 << 7
} CDROMCommandStatusFlag;

typedef enum {
	CDROM_CMDERR_SEEK_FAILED         = 1 << 2,
	CDROM_CMDERR_LID_OPENED          = 1 << 3,
	CDROM_CMDERR_INVALID_PARAM_VALUE = 1 << 4,
	CDROM_CMDERR_INVALID_PARAM_COUNT = 1 << 5,
	CDROM_CMDERR_INVALID_COMMAND     = 1 << 6,
	CDROM_CMDERR_NO_DISC             = 1 << 7
} CDROMCommandErrorFlag;

typedef enum {
	CDROM_MODE_CDDA          = 1 << 0,
	CDROM_MODE_AUTO_PAUSE    = 1 << 1,
	CDROM_MODE_CDDA_REPORT   = 1 << 2,
	CDROM_MODE_XA_FILTER     = 1 << 3,
	CDROM_MODE_SIZE_BITMASK  = 3 << 4,
	CDROM_MODE_SIZE_2048     = 0 << 4,
	CDROM_MODE_SIZE_2328     = 1 << 4,
	CDROM_MODE_SIZE_2340     = 2 << 4,
	CDROM_MODE_XA_ADPCM      = 1 << 6,
	CDROM_MODE_SPEED_BITMASK = 1 << 7,
	CDROM_MODE_SPEED_1X      = 0 << 7,
	CDROM_MODE_SPEED_2X      = 1 << 7
} CDROMModeFlag;

/* CD-XA sector header definitions */

typedef enum {
	XA_SM_END_OF_RECORD = 1 << 0,
	XA_SM_TYPE_VIDEO    = 1 << 1,
	XA_SM_TYPE_AUDIO    = 1 << 2,
	XA_SM_TYPE_DATA     = 1 << 3,
	XA_SM_TRIGGER       = 1 << 4,
	XA_SM_FORM2         = 1 << 5,
	XA_SM_REAL_TIME     = 1 << 6,
	XA_SM_END_OF_FILE   = 1 << 7
} XASubmodeFlag;

typedef enum {
	XA_CI_STEREO              = 1 << 0,
	XA_CI_SAMPLE_RATE_BITMASK = 3 << 2,
	XA_CI_SAMPLE_RATE_37800   = 0 << 2,
	XA_CI_SAMPLE_RATE_18900   = 1 << 2,
	XA_CI_BITS_BITMASK        = 3 << 4,
	XA_CI_BITS_4              = 0 << 4,
	XA_CI_BITS_8              = 1 << 4,
	XA_CI_EMPHASIS            = 1 << 6
} XACodingInfoFlag;

/* Sector address conversion */

DEF(uint8_t) cdrom_encodeBCD(uint8_t value) {
	return 0
		| ((value % 10) << 0)
		| ((value / 10) << 4);
}
DEF(uint8_t) cdrom_decodeBCD(uint8_t value) {
	return 0
		+ ((value & 15) *  1)
		+ ((value >> 4) * 10);
}

DEF(void) cdrom_lbaToMSF(uint8_t *msf, unsigned int lba) {
	lba += 150; // Skip lead-in area (LBA 0 is always at 00:02:00)

	msf[0] = cdrom_encodeBCD(lba  / 4500);
	msf[1] = cdrom_encodeBCD((lba /   75) % 60);
	msf[2] = cdrom_encodeBCD((lba /    1) % 75);
}
DEF(unsigned int) cdrom_msfToLBA(const uint8_t *msf) {
	return 0
		+ cdrom_decodeBCD(msf[0]) * 4500
		+ cdrom_decodeBCD(msf[1]) *   75
		+ cdrom_decodeBCD(msf[2])
		- 150;
}

#undef DEF
