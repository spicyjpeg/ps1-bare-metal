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

/* GTE data types */

// The GTE stores 16-bit vectors and matrices in 32-bit registers, packing two
// values into each register. Since lwc2/swc2 can only perform aligned memory
// accesses (see gte_loadDataReg() and gte_storeDataReg()), the structures below
// must always be aligned to 32 bits.
typedef struct __attribute__((aligned(4))) {
	int16_t x, y;
	int16_t z, _padding;
} GTEVector16;

typedef struct __attribute__((aligned(4))) {
	int32_t x, y, z;
} GTEVector32;

typedef struct __attribute__((aligned(4))) {
	int16_t values[3][3];
	int16_t _padding;
} GTEMatrix;

/* Command definitions */

typedef enum {
	GTE_CMD_BITMASK = 63 <<  0,
	GTE_CMD_RTPS    =  1 <<  0, // Perspective transformation (1 vertex)
	GTE_CMD_NCLIP   =  6 <<  0, // Normal clipping
	GTE_CMD_OP      = 12 <<  0, // Outer product
	GTE_CMD_DPCS    = 16 <<  0, // Depth cue (1 vertex)
	GTE_CMD_INTPL   = 17 <<  0, // Depth cue with vector
	GTE_CMD_MVMVA   = 18 <<  0, // Matrix-vector multiplication
	GTE_CMD_NCDS    = 19 <<  0, // Normal color depth (1 vertex)
	GTE_CMD_CDP     = 20 <<  0, // Color depth cue
	GTE_CMD_NCDT    = 22 <<  0, // Normal color depth (3 vertices)
	GTE_CMD_NCCS    = 27 <<  0, // Normal color color (1 vertex)
	GTE_CMD_CC      = 28 <<  0, // Color color
	GTE_CMD_NCS     = 30 <<  0, // Normal color (1 vertex)
	GTE_CMD_NCT     = 32 <<  0, // Normal color (3 vertices)
	GTE_CMD_SQR     = 40 <<  0, // Square of vector
	GTE_CMD_DCPL    = 41 <<  0, // Depth cue with light
	GTE_CMD_DPCT    = 42 <<  0, // Depth cue (3 vertices)
	GTE_CMD_AVSZ3   = 45 <<  0, // Average Z value (3 vertices)
	GTE_CMD_AVSZ4   = 46 <<  0, // Average Z value (4 vertices)
	GTE_CMD_RTPT    = 48 <<  0, // Perspective transformation (3 vertices)
	GTE_CMD_GPF     = 61 <<  0, // Linear interpolation
	GTE_CMD_GPL     = 62 <<  0, // Linear interpolation with base
	GTE_CMD_NCCT    = 63 <<  0, // Normal color color (3 vertices)
	GTE_LM          =  1 << 10, // Saturate IR to 0x0000-0x7fff
	GTE_CV_BITMASK  =  3 << 13,
	GTE_CV_TR       =  0 << 13, // Use TR as translation vector for MVMVA
	GTE_CV_BK       =  1 << 13, // Use BK as translation vector for MVMVA
	GTE_CV_FC       =  2 << 13, // Use FC as translation vector for MVMVA
	GTE_CV_NONE     =  3 << 13, // Skip translation for MVMVA
	GTE_V_BITMASK   =  3 << 15,
	GTE_V_V0        =  0 << 15, // Use V0 as operand for MVMVA
	GTE_V_V1        =  1 << 15, // Use V1 as operand for MVMVA
	GTE_V_V2        =  2 << 15, // Use V2 as operand for MVMVA
	GTE_V_IR        =  3 << 15, // Use IR as operand for MVMVA
	GTE_MX_BITMASK  =  3 << 17,
	GTE_MX_RT       =  0 << 17, // Use rotation matrix as operand for MVMVA
	GTE_MX_LLM      =  1 << 17, // Use light matrix as operand for MVMVA
	GTE_MX_LCM      =  2 << 17, // Use light color matrix as operand for MVMVA
	GTE_SF          =  1 << 19  // Shift results by 12 bits
} GTECommandFlag;

DEF(void) gte_command(const uint32_t cmd) {
	__asm__ volatile(
		"nop\n"
		"nop\n"
		"cop2 %0\n"
		:: "i"(cmd)
	);
}

/* Control register definitions */

typedef enum {
	GTE_RT11RT12 =  0, // Rotation matrix
	GTE_RT13RT21 =  1, // Rotation matrix
	GTE_RT22RT23 =  2, // Rotation matrix
	GTE_RT31RT32 =  3, // Rotation matrix
	GTE_RT33     =  4, // Rotation matrix
	GTE_TRX      =  5, // Translation vector
	GTE_TRY      =  6, // Translation vector
	GTE_TRZ      =  7, // Translation vector
	GTE_L11L12   =  8, // Light matrix
	GTE_L13L21   =  9, // Light matrix
	GTE_L22L23   = 10, // Light matrix
	GTE_L31L32   = 11, // Light matrix
	GTE_L33      = 12, // Light matrix
	GTE_RBK      = 13, // Background color
	GTE_GBK      = 14, // Background color
	GTE_BBK      = 15, // Background color
	GTE_LC11LC12 = 16, // Light color matrix
	GTE_LC13LC21 = 17, // Light color matrix
	GTE_LC22LC23 = 18, // Light color matrix
	GTE_LC31LC32 = 19, // Light color matrix
	GTE_LC33     = 20, // Light color matrix
	GTE_RFC      = 21, // Far color
	GTE_GFC      = 22, // Far color
	GTE_BFC      = 23, // Far color
	GTE_OFX      = 24, // Screen coordinate offset
	GTE_OFY      = 25, // Screen coordinate offset
	GTE_H        = 26, // Projection plane distance
	GTE_DQA      = 27, // Depth cue scale factor
	GTE_DQB      = 28, // Depth cue base
	GTE_ZSF3     = 29, // Average Z scale factor
	GTE_ZSF4     = 30, // Average Z scale factor
	GTE_FLAG     = 31  // Error/overflow flags
} GTEControlRegister;

typedef enum {
	GTE_FLAG_IR0_SATURATED   = 1 << 12,
	GTE_FLAG_SY2_SATURATED   = 1 << 13,
	GTE_FLAG_SX2_SATURATED   = 1 << 14,
	GTE_FLAG_MAC0_UNDERFLOW  = 1 << 15,
	GTE_FLAG_MAC0_OVERFLOW   = 1 << 16,
	GTE_FLAG_DIVIDE_OVERFLOW = 1 << 17,
	GTE_FLAG_Z_SATURATED     = 1 << 18,
	GTE_FLAG_B_SATURATED     = 1 << 19,
	GTE_FLAG_G_SATURATED     = 1 << 20,
	GTE_FLAG_R_SATURATED     = 1 << 21,
	GTE_FLAG_IR3_SATURATED   = 1 << 22,
	GTE_FLAG_IR2_SATURATED   = 1 << 23,
	GTE_FLAG_IR1_SATURATED   = 1 << 24,
	GTE_FLAG_MAC3_UNDERFLOW  = 1 << 25,
	GTE_FLAG_MAC2_UNDERFLOW  = 1 << 26,
	GTE_FLAG_MAC1_UNDERFLOW  = 1 << 27,
	GTE_FLAG_MAC3_OVERFLOW   = 1 << 28,
	GTE_FLAG_MAC2_OVERFLOW   = 1 << 29,
	GTE_FLAG_MAC1_OVERFLOW   = 1 << 30,
	GTE_FLAG_ERROR           = 1 << 31
} GTEStatusFlag;

// Note that reg must be a constant value known at compile time, as the
// cfc2/ctc2 instructions only support addressing coprocessor registers directly
// through immediates.
DEF(void) gte_setControlReg(const GTEControlRegister reg, uint32_t value) {
	__asm__ volatile("ctc2 %0, $%1\n" :: "r"(value), "i"(reg));
}
DEF(uint32_t) gte_getControlReg(const GTEControlRegister reg) {
	uint32_t value;

	__asm__ volatile("cfc2 %0, $%1\n" : "=r"(value) : "i"(reg));
	return value;
}

#define MATRIX_FUNCTIONS(reg0, reg1, reg2, reg3, reg4, name) \
	DEF(void) gte_set##name( \
		int16_t v11, int16_t v12, int16_t v13, \
		int16_t v21, int16_t v22, int16_t v23, \
		int16_t v31, int16_t v32, int16_t v33 \
	) { \
		gte_setControlReg(reg0, ((uint32_t) v11 & 0xffff) | ((uint32_t) v12 << 16)); \
		gte_setControlReg(reg1, ((uint32_t) v13 & 0xffff) | ((uint32_t) v21 << 16)); \
		gte_setControlReg(reg2, ((uint32_t) v22 & 0xffff) | ((uint32_t) v23 << 16)); \
		gte_setControlReg(reg3, ((uint32_t) v31 & 0xffff) | ((uint32_t) v32 << 16)); \
		gte_setControlReg(reg4, v33); \
	} \
	DEF(void) gte_load##name(const GTEMatrix *input) { \
		const uint32_t *values = (const uint32_t *) input; \
		\
		gte_setControlReg(reg0, values[0]); \
		gte_setControlReg(reg1, values[1]); \
		gte_setControlReg(reg2, values[2]); \
		gte_setControlReg(reg3, values[3]); \
		gte_setControlReg(reg4, values[4]); \
	} \
	DEF(void) gte_store##name(GTEMatrix *output) { \
		uint32_t *values = (uint32_t *) output; \
		\
		values[0] = gte_getControlReg(reg0); \
		values[1] = gte_getControlReg(reg1); \
		values[2] = gte_getControlReg(reg2); \
		values[3] = gte_getControlReg(reg3); \
		values[4] = gte_getControlReg(reg4); \
	}

MATRIX_FUNCTIONS(
	GTE_RT11RT12,
	GTE_RT13RT21,
	GTE_RT22RT23,
	GTE_RT31RT32,
	GTE_RT33,
	RotationMatrix
)
MATRIX_FUNCTIONS(
	GTE_L11L12,
	GTE_L13L21,
	GTE_L22L23,
	GTE_L31L32,
	GTE_L33,
	LightMatrix
)
MATRIX_FUNCTIONS(
	GTE_LC11LC12,
	GTE_LC13LC21,
	GTE_LC22LC23,
	GTE_LC31LC32,
	GTE_LC33,
	LightColorMatrix
)

#undef MATRIX_FUNCTIONS

/* Data register definitions */

typedef enum {
	GTE_VXY0 =  0, // Input vector 0
	GTE_VZ0  =  1, // Input vector 0
	GTE_VXY1 =  2, // Input vector 1
	GTE_VZ1  =  3, // Input vector 1
	GTE_VXY2 =  4, // Input vector 2
	GTE_VZ2  =  5, // Input vector 2
	GTE_RGBC =  6, // Input color and GPU command
	GTE_OTZ  =  7, // Average Z value output
	GTE_IR0  =  8, // Scalar accumulator
	GTE_IR1  =  9, // Vector accumulator
	GTE_IR2  = 10, // Vector accumulator
	GTE_IR3  = 11, // Vector accumulator
	GTE_SXY0 = 12, // X/Y coordinate output FIFO
	GTE_SXY1 = 13, // X/Y coordinate output FIFO
	GTE_SXY2 = 14, // X/Y coordinate output FIFO
	GTE_SXYP = 15, // X/Y coordinate output FIFO
	GTE_SZ0  = 16, // Z coordinate output FIFO
	GTE_SZ1  = 17, // Z coordinate output FIFO
	GTE_SZ2  = 18, // Z coordinate output FIFO
	GTE_SZ3  = 19, // Z coordinate output FIFO
	GTE_RGB0 = 20, // Color and GPU command output FIFO
	GTE_RGB1 = 21, // Color and GPU command output FIFO
	GTE_RGB2 = 22, // Color and GPU command output FIFO
	GTE_MAC0 = 24, // Extended scalar accumulator
	GTE_MAC1 = 25, // Extended vector accumulator
	GTE_MAC2 = 26, // Extended vector accumulator
	GTE_MAC3 = 27, // Extended vector accumulator
	GTE_IRGB = 28, // RGB conversion input
	GTE_ORGB = 29, // RGB conversion output
	GTE_LZCS = 30, // Leading zero count input
	GTE_LZCR = 31  // Leading zero count output
} GTEDataRegister;

DEF(void) gte_setDataReg(const GTEDataRegister reg, uint32_t value) {
	__asm__ volatile("mtc2 %0, $%1\n" :: "r"(value), "i"(reg));
}
DEF(uint32_t) gte_getDataReg(const GTEDataRegister reg) {
	uint32_t value;

	__asm__ volatile("mfc2 %0, $%1\n" : "=r"(value) : "i"(reg));
	return value;
}

// Unlike COP0 registers and GTE control registers, whose contents can only be
// moved to/from a CPU register, data registers can additionally be loaded from
// and stored to memory directly using the lwc2 and swc2 instructions.
DEF(void) gte_loadDataReg(
	const GTEDataRegister reg,
	const int16_t         offset,
	const void            *ptr
) {
	__asm__ volatile("lwc2 $%0, %1(%2)\n" :: "i"(reg), "i"(offset), "r"(ptr));
}
DEF(void) gte_storeDataReg(
	const GTEDataRegister reg,
	const int16_t         offset,
	void                  *ptr
) {
	__asm__ volatile("swc2 $%0, %1(%2)\n" :: "i"(reg), "i"(offset), "r"(ptr) : "memory");
}

#define VECTOR_FUNCTIONS(reg0, reg1, name) \
	DEF(void) gte_set##name(int16_t x, int16_t y, int16_t z) { \
		gte_setDataReg(reg0, ((uint32_t) x & 0xffff) | ((uint32_t) y << 16)); \
		gte_setDataReg(reg1, z); \
	} \
	DEF(void) gte_load##name(const GTEVector16 *input) { \
		gte_loadDataReg(reg0, 0, input); \
		gte_loadDataReg(reg1, 4, input); \
	} \
	DEF(void) gte_store##name(GTEVector16 *output) { \
		gte_storeDataReg(reg0, 0, output); \
		gte_storeDataReg(reg1, 4, output); \
	}

VECTOR_FUNCTIONS(GTE_VXY0, GTE_VZ0, V0)
VECTOR_FUNCTIONS(GTE_VXY1, GTE_VZ1, V1)
VECTOR_FUNCTIONS(GTE_VXY2, GTE_VZ2, V2)

#undef VECTOR_FUNCTIONS

DEF(void) gte_setRowVectors(
	int16_t v11, int16_t v12, int16_t v13,
	int16_t v21, int16_t v22, int16_t v23,
	int16_t v31, int16_t v32, int16_t v33
) {
	gte_setDataReg(GTE_VXY0, ((uint32_t) v11 & 0xffff) | ((uint32_t) v12 << 16));
	gte_setDataReg(GTE_VZ0,  v13);
	gte_setDataReg(GTE_VXY1, ((uint32_t) v21 & 0xffff) | ((uint32_t) v22 << 16));
	gte_setDataReg(GTE_VZ1,  v23);
	gte_setDataReg(GTE_VXY2, ((uint32_t) v31 & 0xffff) | ((uint32_t) v32 << 16));
	gte_setDataReg(GTE_VZ2,  v33);
}
DEF(void) gte_setColumnVectors(
	int16_t v11, int16_t v12, int16_t v13,
	int16_t v21, int16_t v22, int16_t v23,
	int16_t v31, int16_t v32, int16_t v33
) {
	gte_setDataReg(GTE_VXY0, ((uint32_t) v11 & 0xffff) | ((uint32_t) v21 << 16));
	gte_setDataReg(GTE_VZ0,  v31);
	gte_setDataReg(GTE_VXY1, ((uint32_t) v12 & 0xffff) | ((uint32_t) v22 << 16));
	gte_setDataReg(GTE_VZ1,  v32);
	gte_setDataReg(GTE_VXY2, ((uint32_t) v13 & 0xffff) | ((uint32_t) v23 << 16));
	gte_setDataReg(GTE_VZ2,  v33);
}

#undef DEF
