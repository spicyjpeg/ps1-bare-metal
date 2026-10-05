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

/* PSCTP (controller/memory card) protocol definitions */

typedef enum {
	CTP_ADDR_CONTROLLER  = 0x01, // PS1 or PS2 controller
	CTP_ADDR_ACCESS_CARD = 0x21, // PS1 Net Yaroze Access Card
	CTP_ADDR_IMODE       = 0x41, // PS1 i-Mode adapter cable
	CTP_ADDR_REMOTE      = 0x61, // PS2 DVD remote receiver
	CTP_ADDR_MEMORY_CARD = 0x81  // PS1 memory card or PocketStation
} CTPDeviceAddress;

typedef enum {
	// Controller commands
	CTP_PAD_READ_DATA         = 0x42,
	CTP_PAD_ENTER_CONFIG_MODE = 0x43,

	// Controller commands in configuration mode
	CTP_CFG_VREF_PARAM        = 0x40, // CTP 2.0 (DualShock 2) only
	CTP_CFG_QUERY_BUTTON_MASK = 0x41, // CTP 2.0 (DualShock 2) only
	CTP_CFG_SET_MAIN_MODE     = 0x44,
	CTP_CFG_QUERY_MODEL       = 0x45,
	CTP_CFG_QUERY_ACT         = 0x46,
	CTP_CFG_QUERY_COMB        = 0x47,
	CTP_CFG_UNKNOWN           = 0x48,
	CTP_CFG_QUERY_COMB2       = 0x4b,
	CTP_CFG_QUERY_MODE        = 0x4c,
	CTP_CFG_SET_ACT_ALIGN     = 0x4d,
	CTP_CFG_SET_BUTTON_INFO   = 0x4f, // CTP 2.0 (DualShock 2) only

	// DVD remote receiver commands
	CTP_RM_POLL = 0x04,
	CTP_RM_INIT = 0x06,
	CTP_RM_FIND = 0x0f,

	// Memory card commands
	CTP_MC_READ  = 'R',
	CTP_MC_SIZE  = 'S',
	CTP_MC_WRITE = 'W',

	// PocketStation commands
	CTP_MCX_GET_MCX_INFO = 0x58,
	CTP_MCX_EXEC_APL     = 0x59,
	CTP_MCX_ALL_INFO     = 0x5a,
	CTP_MCX_READ_DEV     = 0x5b,
	CTP_MCX_WRITE_DEV    = 0x5c,
	CTP_MCX_SHOW_TRANS   = 0x5d,
	CTP_MCX_CURR_CTRL    = 0x5e,
	CTP_MCX_FLASH_ACS    = 0x5f
} CTPCommand;

typedef enum {
	CTP_TAP_NONE = 0,
	CTP_TAP_A    = 1,
	CTP_TAP_B    = 2,
	CTP_TAP_C    = 3,
	CTP_TAP_D    = 4
} CTPMultitapPort;

typedef enum {
	CTP_TYPE_NONE         =  0,
	CTP_TYPE_MOUSE        =  1,
	CTP_TYPE_NEGCON       =  2,
	CTP_TYPE_JUSTIFIER    =  3,
	CTP_TYPE_DIGITAL      =  4,
	CTP_TYPE_ANALOG_STICK =  5,
	CTP_TYPE_GUNCON       =  6,
	CTP_TYPE_ANALOG       =  7,
	CTP_TYPE_MULTITAP     =  8,
	CTP_TYPE_KEYBOARD     =  9,
	CTP_TYPE_JOGCON       = 14,
	CTP_TYPE_CONFIG_MODE  = 15
} CTPControllerType;

/* Controller button definitions */

typedef enum {
	BTN_PAD_SELECT   = 1 <<  0,
	BTN_PAD_L3       = 1 <<  1,
	BTN_PAD_R3       = 1 <<  2,
	BTN_PAD_START    = 1 <<  3,
	BTN_PAD_UP       = 1 <<  4,
	BTN_PAD_RIGHT    = 1 <<  5,
	BTN_PAD_DOWN     = 1 <<  6,
	BTN_PAD_LEFT     = 1 <<  7,
	BTN_PAD_L2       = 1 <<  8,
	BTN_PAD_R2       = 1 <<  9,
	BTN_PAD_L1       = 1 << 10,
	BTN_PAD_R1       = 1 << 11,
	BTN_PAD_TRIANGLE = 1 << 12,
	BTN_PAD_CIRCLE   = 1 << 13,
	BTN_PAD_CROSS    = 1 << 14,
	BTN_PAD_SQUARE   = 1 << 15
} ControllerButtonFlag;

typedef enum {
	BTN_MOUSE_RIGHT = BTN_PAD_L1,
	BTN_MOUSE_LEFT  = BTN_PAD_R1
} MouseButtonFlag;

typedef enum {
	BTN_NEGCON_START = BTN_PAD_START,
	BTN_NEGCON_UP    = BTN_PAD_UP,
	BTN_NEGCON_RIGHT = BTN_PAD_RIGHT,
	BTN_NEGCON_DOWN  = BTN_PAD_DOWN,
	BTN_NEGCON_LEFT  = BTN_PAD_LEFT,
	BTN_NEGCON_R     = BTN_PAD_R1,
	BTN_NEGCON_B     = BTN_PAD_TRIANGLE,
	BTN_NEGCON_A     = BTN_PAD_CIRCLE
} NeGconButtonFlag;

typedef enum {
	BTN_JUSTIFIER_START   = BTN_PAD_START,
	BTN_JUSTIFIER_BACK    = BTN_PAD_CROSS,
	BTN_JUSTIFIER_TRIGGER = BTN_PAD_SQUARE
} JustifierButtonFlag;

typedef enum {
	BTN_GUNCON_A       = BTN_PAD_START,
	BTN_GUNCON_TRIGGER = BTN_PAD_CIRCLE,
	BTN_GUNCON_B       = BTN_PAD_CROSS
} GunconButtonFlag;

typedef enum {
	BTN_BM_SELECT         = BTN_PAD_SELECT,
	BTN_BM_START          = BTN_PAD_START,
	BTN_BM_TURNTABLE_UP   = BTN_PAD_UP,
	BTN_BM_TURNTABLE_DOWN = BTN_PAD_DOWN,
	BTN_BM_WHITE4         = BTN_PAD_LEFT,   // PS2 IIDX controllers only
	BTN_BM_BLACK3         = BTN_PAD_L2,     // PS2 IIDX controllers only
	BTN_BM_BLACK1         = BTN_PAD_L1,
	BTN_BM_BLACK2         = BTN_PAD_R1,
	BTN_BM_WHITE3         = BTN_PAD_CIRCLE,
	BTN_BM_WHITE2         = BTN_PAD_CROSS,
	BTN_BM_WHITE1         = BTN_PAD_SQUARE
} BeatmaniaButtonFlag;

typedef enum {
	BTN_POPN_SELECT     = BTN_PAD_SELECT,
	BTN_POPN_START      = BTN_PAD_START,
	BTN_POPN_YELLOW2    = BTN_PAD_UP,
	BTN_POPN_ID_BITMASK = BTN_PAD_RIGHT | BTN_PAD_DOWN | BTN_PAD_LEFT,
	BTN_POPN_WHITE2     = BTN_PAD_L2,
	BTN_POPN_GREEN2     = BTN_PAD_R2,
	BTN_POPN_RED        = BTN_PAD_L1,
	BTN_POPN_GREEN1     = BTN_PAD_R1,
	BTN_POPN_WHITE1     = BTN_PAD_TRIANGLE,
	BTN_POPN_YELLOW1    = BTN_PAD_CIRCLE,
	BTN_POPN_BLUE1      = BTN_PAD_CROSS,
	BTN_POPN_BLUE2      = BTN_PAD_SQUARE
} PopnButtonFlag;

typedef enum {
	BTN_TAIKO_SELECT = BTN_PAD_SELECT,
	BTN_TAIKO_START  = BTN_PAD_START,
	BTN_TAIKO_DON1   = BTN_PAD_LEFT,
	BTN_TAIKO_KA1    = BTN_PAD_L1,
	BTN_TAIKO_KA2    = BTN_PAD_R1,
	BTN_TAIKO_DON2   = BTN_PAD_CIRCLE
} TaikoButtonFlag;

typedef enum {
	BTN_DENSHA_SELECT     = BTN_PAD_SELECT,
	BTN_DENSHA_START      = BTN_PAD_START,
	BTN_DENSHA_ID_BITMASK = BTN_PAD_UP | BTN_PAD_DOWN,
	BTN_DENSHA_POWER2     = BTN_PAD_RIGHT,
	BTN_DENSHA_POWER1     = BTN_PAD_LEFT,
	BTN_DENSHA_BRAKE1     = BTN_PAD_L2,
	BTN_DENSHA_BRAKE3     = BTN_PAD_R2,
	BTN_DENSHA_BRAKE0     = BTN_PAD_L1,
	BTN_DENSHA_BRAKE2     = BTN_PAD_R1,
	BTN_DENSHA_POWER0     = BTN_PAD_TRIANGLE,
	BTN_DENSHA_C          = BTN_PAD_CIRCLE,
	BTN_DENSHA_B          = BTN_PAD_CROSS,
	BTN_DENSHA_A          = BTN_PAD_SQUARE
} DenshaButtonFlag;
