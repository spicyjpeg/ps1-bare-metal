/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
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

#define COP0_GET(reg, output) \
	__asm__ volatile("mfc0 %0, $%1\n" :  "=r"(output) : "i"(reg))
#define COP0_SET(reg, input) \
	__asm__ volatile("mtc0 %0, $%1\n" :: "r"(input), "i"(reg))

#define GTE_GET(reg, output) \
	__asm__ volatile("mfc2 %0, $%1\n" :  "=r"(output) : "i"(reg))
#define GTE_SET(reg, input) \
	__asm__ volatile("mtc2 %0, $%1\n" :: "r"(input), "i"(reg))

#define GTE_GETC(reg, output) \
	__asm__ volatile("cfc2 %0, $%1\n" :  "=r"(output) : "i"(reg))
#define GTE_SETC(reg, input) \
	__asm__ volatile("ctc2 %0, $%1\n" :: "r"(input), "i"(reg))

#define GTE_LOAD(reg, offset, ptr) \
	__asm__ volatile("lwc2 $%0, %1(%2)\n" :: "i"(reg), "i"(offset), "r"(ptr))
#define GTE_STORE(reg, offset, ptr) \
	__asm__ volatile("swc2 $%0, %1(%2)\n" :: "i"(reg), "i"(offset), "r"(ptr) : "memory")

/* Coprocessor 0 */

typedef enum {
	COP0_BPC      =  3, // Breakpoint program counter
	COP0_BDA      =  5, // Breakpoint data address
	COP0_DCIC     =  7, // Breakpoint control
	COP0_BADVADDR =  8, // Bad virtual address
	COP0_BDAM     =  9, // Breakpoint program counter bitmask
	COP0_BPCM     = 11, // Breakpoint data address bitmask
	COP0_SR       = 12, // Status register
	COP0_CAUSE    = 13, // Exception cause
	COP0_EPC      = 14, // Exception program counter
	COP0_PRID     = 15  // Processor identifier
} COP0Register;

typedef enum {
	COP0_CAUSE_EXC_BITMASK = 31 <<  2,
	COP0_CAUSE_EXC_INT     =  0 <<  2, // Interrupt
	COP0_CAUSE_EXC_AdEL    =  4 <<  2, // Load address error
	COP0_CAUSE_EXC_AdES    =  5 <<  2, // Store address error
	COP0_CAUSE_EXC_IBE     =  6 <<  2, // Instruction bus error
	COP0_CAUSE_EXC_DBE     =  7 <<  2, // Data bus error
	COP0_CAUSE_EXC_SYS     =  8 <<  2, // Syscall
	COP0_CAUSE_EXC_BP      =  9 <<  2, // Breakpoint or break instruction
	COP0_CAUSE_EXC_RI      = 10 <<  2, // Reserved instruction
	COP0_CAUSE_EXC_CpU     = 11 <<  2, // Coprocessor unusable
	COP0_CAUSE_EXC_Ov      = 12 <<  2, // Arithmetic overflow
	COP0_CAUSE_Ip0         =  1 <<  8, // IRQ 0 pending (software interrupt)
	COP0_CAUSE_Ip1         =  1 <<  9, // IRQ 1 pending (software interrupt)
	COP0_CAUSE_Ip2         =  1 << 10, // IRQ 2 pending (hardware interrupt)
	COP0_CAUSE_CE_BITMASK  =  3 << 28,
	COP0_CAUSE_BD          =  1 << 30  // Exception occurred in delay slot
} COP0CauseFlag;

typedef enum {
	COP0_SR_IEc = 1 <<  0, // Current interrupt enable
	COP0_SR_KUc = 1 <<  1, // Current privilege level
	COP0_SR_IEp = 1 <<  2, // Previous interrupt enable
	COP0_SR_KUp = 1 <<  3, // Previous privilege level
	COP0_SR_IEo = 1 <<  4, // Old interrupt enable
	COP0_SR_KUo = 1 <<  5, // Old privilege level
	COP0_SR_Im0 = 1 <<  8, // IRQ mask 0 (software interrupt)
	COP0_SR_Im1 = 1 <<  9, // IRQ mask 1 (software interrupt)
	COP0_SR_Im2 = 1 << 10, // IRQ mask 2 (hardware interrupt)
	COP0_SR_Isc = 1 << 16, // Isolate cache
	COP0_SR_BEV = 1 << 22, // Boot exception vector location
	COP0_SR_CU0 = 1 << 28, // Coprocessor 0 privilege level
	COP0_SR_CU2 = 1 << 30  // Coprocessor 2 enable
} COP0StatusFlag;

#define SETTER(reg, type) \
	static inline type cop0_get##reg(void) { \
		type value; \
		COP0_GET(COP0_##reg, value); \
		return value; \
	} \
	static inline void cop0_set##reg(type value) { \
		COP0_SET(COP0_##reg, value); \
	} \

SETTER(BPC,  void *)
SETTER(BDA,  void *)
SETTER(DCIC, uint32_t)
SETTER(BDAM, uint32_t)
SETTER(BPCM, uint32_t)
SETTER(SR,   uint32_t)

#undef SETTER

#define GETTER(reg, type) \
	static inline type cop0_get##reg(void) { \
		type value; \
		COP0_GET(COP0_##reg, value); \
		return value; \
	} \

GETTER(BADVADDR, void *)
GETTER(CAUSE,    uint32_t)
GETTER(EPC,      void *)

#undef GETTER

/* GTE commands */

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

typedef struct {
	int16_t x, y, z;
	uint8_t _padding[2];
} GTEVector16;

typedef struct {
	int32_t x, y, z;
} GTEVector32;

typedef struct {
	int16_t values[3][3];
	uint8_t _padding[2];
} GTEMatrix;

#define gte_command(cmd) \
	__asm__ volatile("nop\n" "nop\n" "cop2 %0\n" :: "i"(cmd))

/* GTE control registers */

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

#define VECTOR32_SETTER(regA, regB, regC, name) \
	static inline void gte_set##name(int x, int y, int z) { \
		GTE_SETC(GTE_##regA, x); \
		GTE_SETC(GTE_##regB, y); \
		GTE_SETC(GTE_##regC, z); \
	}

VECTOR32_SETTER(TRX, TRY, TRZ, TranslationVector)
VECTOR32_SETTER(RBK, GBK, BBK, BackgroundColor)
VECTOR32_SETTER(RFC, GFC, BFC, FarColor)

#undef VECTOR32_SETTER

#define MATRIX_SETTER(reg, name) \
	static inline void gte_set##name( \
		int16_t v11, int16_t v12, int16_t v13, \
		int16_t v21, int16_t v22, int16_t v23, \
		int16_t v31, int16_t v32, int16_t v33 \
	) { \
		uint32_t value; \
		value = ((uint32_t) v11 & 0xffff) | ((uint32_t) v12 << 16); \
		GTE_SETC(GTE_##reg##11##reg##12, value); \
		value = ((uint32_t) v13 & 0xffff) | ((uint32_t) v21 << 16); \
		GTE_SETC(GTE_##reg##13##reg##21, value); \
		value = ((uint32_t) v22 & 0xffff) | ((uint32_t) v23 << 16); \
		GTE_SETC(GTE_##reg##22##reg##23, value); \
		value = ((uint32_t) v31 & 0xffff) | ((uint32_t) v32 << 16); \
		GTE_SETC(GTE_##reg##31##reg##32, value); \
		GTE_SETC(GTE_##reg##33,          v33); \
	} \
	static inline void gte_load##name(const GTEMatrix *input) { \
		uint32_t value; \
		value = ((const uint32_t *) input)[0]; \
		GTE_SETC(GTE_##reg##11##reg##12, value); \
		value = ((const uint32_t *) input)[1]; \
		GTE_SETC(GTE_##reg##13##reg##21, value); \
		value = ((const uint32_t *) input)[2]; \
		GTE_SETC(GTE_##reg##22##reg##23, value); \
		value = ((const uint32_t *) input)[3]; \
		GTE_SETC(GTE_##reg##31##reg##32, value); \
		value = ((const uint32_t *) input)[4]; \
		GTE_SETC(GTE_##reg##33,          value); \
	}

MATRIX_SETTER(RT, RotationMatrix)
MATRIX_SETTER(L,  LightMatrix)
MATRIX_SETTER(LC, LightColorMatrix)

#undef MATRIX_SETTER

static inline void gte_setXYOrigin(int x, int y) {
	GTE_SETC(GTE_OFX, x << 16);
	GTE_SETC(GTE_OFY, y << 16);
}
static inline void gte_setFieldOfView(int value) {
	GTE_SETC(GTE_H, value);
}
static inline void gte_setDepthCueFactor(int base, int scale) {
	GTE_SETC(GTE_DQA, scale);
	GTE_SETC(GTE_DQB, base);
}
static inline void gte_setZScaleFactor(unsigned int scale) {
	unsigned int z3 = scale / 3, z4 = scale / 4;

	GTE_SETC(GTE_ZSF3, z3);
	GTE_SETC(GTE_ZSF4, z4);
}

/* GTE data registers */

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

#define VECTOR16_SETTER(regA, regB, name) \
	static inline void gte_set##name(int16_t x, int16_t y, int16_t z) { \
		uint32_t xy = ((uint32_t) x & 0xffff) | ((uint32_t) y << 16); \
		GTE_SET(GTE_##regA, xy); \
		GTE_SET(GTE_##regB, z); \
	} \
	static inline void gte_load##name(const GTEVector16 *input) { \
		GTE_LOAD(GTE_##regA, 0, input); \
		GTE_LOAD(GTE_##regB, 4, input); \
	}

VECTOR16_SETTER(VXY0, VZ0, V0)
VECTOR16_SETTER(VXY1, VZ1, V1)
VECTOR16_SETTER(VXY2, VZ2, V2)

#undef VECTOR16_SETTER

static inline void gte_loadV012(const GTEVector16 *input) {
	GTE_LOAD(GTE_VXY0,  0, input);
	GTE_LOAD(GTE_VZ0,   4, input);
	GTE_LOAD(GTE_VXY1,  8, input);
	GTE_LOAD(GTE_VZ1,  12, input);
	GTE_LOAD(GTE_VXY2, 16, input);
	GTE_LOAD(GTE_VZ2,  20, input);
}
static inline void gte_setColumnVectors(
	int16_t v11, int16_t v12, int16_t v13,
	int16_t v21, int16_t v22, int16_t v23,
	int16_t v31, int16_t v32, int16_t v33
) {
	uint32_t value;
	value = ((uint32_t) v11 & 0xffff) | ((uint32_t) v21 << 16);
	GTE_SET(GTE_VXY0, value);
	GTE_SET(GTE_VZ0,  v31);
	value = ((uint32_t) v12 & 0xffff) | ((uint32_t) v22 << 16);
	GTE_SET(GTE_VXY1, value);
	GTE_SET(GTE_VZ1,  v32);
	value = ((uint32_t) v13 & 0xffff) | ((uint32_t) v23 << 16);
	GTE_SET(GTE_VXY2, value);
	GTE_SET(GTE_VZ2,  v33);
}

#define SETTER(reg, type) \
	static inline type gte_get##reg(void) { \
		type value; \
		GTE_GET(GTE_##reg, value); \
		return value; \
	} \
	static inline void gte_set##reg(type value) { \
		GTE_SET(GTE_##reg, value); \
	} \
	static inline void gte_load##reg(const type *input) { \
		GTE_LOAD(GTE_##reg, 0, input); \
	} \
	static inline void gte_store##reg(type *output) { \
		GTE_STORE(GTE_##reg, 0, output); \
	}

SETTER(RGBC, uint32_t)
SETTER(OTZ,  int)
SETTER(IR0,  int)
SETTER(IR1,  int)
SETTER(IR2,  int)
SETTER(IR3,  int)
SETTER(SXY0, uint32_t)
SETTER(SXY1, uint32_t)
SETTER(SXY2, uint32_t)
SETTER(SZ0,  int)
SETTER(SZ1,  int)
SETTER(SZ2,  int)
SETTER(SZ3,  int)
SETTER(RGB0, uint32_t)
SETTER(RGB1, uint32_t)
SETTER(RGB2, uint32_t)
SETTER(MAC0, int)
SETTER(MAC1, int)
SETTER(MAC2, int)
SETTER(MAC3, int)
SETTER(LZCS, uint32_t)
SETTER(LZCR, int)

#undef SETTER

static inline void gte_storeSXY012(uint32_t *output) {
	GTE_STORE(GTE_SXY0, 0, output);
	GTE_STORE(GTE_SXY1, 4, output);
	GTE_STORE(GTE_SXY2, 8, output);
}

#undef COP0_GET
#undef COP0_SET
#undef GTE_GET
#undef GTE_SET
#undef GTE_GETC
#undef GTE_SETC
#undef GTE_LOAD
#undef GTE_STORE
