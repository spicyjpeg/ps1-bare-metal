/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include "common/cache.h"
#include "common/isr.h"
#include "ps1/cop0.h"
#include "ps1/kernel.h"
#include "ps1/registers.h"

typedef enum {
	ISR_NONE   = 0,
	ISR_KERNEL = 1,
	ISR_CUSTOM = 2
} ISRType;

static uint8_t isrType = ISR_NONE;

/* BIOS kernel ISR setup */

#define ISR_STACK_SIZE 256

static uint32_t isrStack[ISR_STACK_SIZE];
extern uint8_t  _gp[];

static void isrWrapper(void) {
	interruptHandler();
	kernel_returnFromException();
}

static const jmp_buf isrContext = {{
	.ra = (uintptr_t) isrWrapper,
	.sp = (uintptr_t) &isrStack[ISR_STACK_SIZE - 4],
	.gp = (uintptr_t) _gp
}};

void installKernelISR(void) {
	COP0_CRITICAL_SECTION_THEN_SET(COP0_STATUS_IEc | COP0_STATUS_Im2);
	assert(isrType == ISR_NONE);

	kernel_hookEntryInt(isrContext);
	kernel_changeClearPad(false);

	for (int i = KERNEL_RCNT_TIMER0; i <= KERNEL_RCNT_VSYNC; i++)
		kernel_changeClearRCnt(i, false);

	isrType = ISR_KERNEL;
}

void restoreKernelISR(void) {
	COP0_CRITICAL_SECTION();
	assert(isrType == ISR_KERNEL);

	kernel_resetEntryInt();

	isrType = ISR_NONE;
}

/* Custom ISR setup */

#define ISR_STUB_ADDR ((uint32_t *) (RAM_BASE | 0x80))
#define ISR_STUB_SIZE 8

// When patching in the custom ISR stub, the kernel's original one is saved here
// so it can be restored later by uninstallCustomISR(), for instance to launch
// an executable that depends on the kernel.
static uint32_t savedISRStub[ISR_STUB_SIZE];

void isrStub(void);

void installCustomISR(void) {
	COP0_CRITICAL_SECTION_THEN_SET(COP0_STATUS_IEc | COP0_STATUS_Im2);
	assert(isrType == ISR_NONE);

	__builtin_memcpy(savedISRStub,  ISR_STUB_ADDR, ISR_STUB_SIZE * 4);
	__builtin_memcpy(ISR_STUB_ADDR, isrStub,       ISR_STUB_SIZE * 4);
	flushCache();

	isrType = ISR_CUSTOM;
}

void uninstallCustomISR(void) {
	COP0_CRITICAL_SECTION();
	assert(isrType == ISR_CUSTOM);

	__builtin_memcpy(ISR_STUB_ADDR, savedISRStub, ISR_STUB_SIZE * 4);
	flushCache();

	isrType = ISR_NONE;
}

/* Exception handler (custom ISR only) */

static const char *const causeNames[] = {
	"Load address error",    // COP0_CAUSE_EXC_AdEL
	"Store address error",   // COP0_CAUSE_EXC_AdES
	"Instruction bus error", // COP0_CAUSE_EXC_IBE
	"Data bus error",        // COP0_CAUSE_EXC_DBE
	"Syscall",               // COP0_CAUSE_EXC_SYS
	"Break instruction",     // COP0_CAUSE_EXC_BP
	"Reserved instruction",  // COP0_CAUSE_EXC_RI
	"Coprocessor unusable",  // COP0_CAUSE_EXC_CpU
	"Arithmetic overflow"    // COP0_CAUSE_EXC_Ov
};

typedef struct {
	char    name[3];
	uint8_t index;
} RegisterInfo;

static const RegisterInfo registerNames[] = {
	{ .name = "pc", .index =      11 }, { .name = "at", .index = 12 +  0 },
	{ .name = "v0", .index = 12 +  1 }, { .name = "v1", .index = 12 +  2 },
	{ .name = "a0", .index = 12 +  3 }, { .name = "a1", .index = 12 +  4 },
	{ .name = "a2", .index = 12 +  5 }, { .name = "a3", .index = 12 +  6 },
	{ .name = "t0", .index = 12 +  7 }, { .name = "t1", .index = 12 +  8 },
	{ .name = "t2", .index = 12 +  9 }, { .name = "t3", .index = 12 + 10 },
	{ .name = "t4", .index = 12 + 11 }, { .name = "t5", .index = 12 + 12 },
	{ .name = "t6", .index = 12 + 13 }, { .name = "t7", .index = 12 + 14 },
	{ .name = "s0", .index =       0 }, { .name = "s1", .index =       1 },
	{ .name = "s2", .index =       2 }, { .name = "s3", .index =       3 },
	{ .name = "s4", .index =       4 }, { .name = "s5", .index =       5 },
	{ .name = "s6", .index =       6 }, { .name = "s7", .index =       7 },
	{ .name = "t8", .index = 12 + 15 }, { .name = "t9", .index = 12 + 16 },
	{ .name = "gp", .index =       8 }, { .name = "sp", .index =       9 },
	{ .name = "fp", .index =      10 }, { .name = "ra", .index = 12 + 17 },
	{ .name = "hi", .index = 12 + 18 }, { .name = "lo", .index = 12 + 19 }
};

#define NUM_REGISTERS (sizeof(registerNames) / sizeof(RegisterInfo))

void otherExceptionHandler(int cause, const uintptr_t *registers) {
#ifndef NDEBUG
	cause -= COP0_CAUSE_EXC_AdEL >> 2;

	printf("%s - register dump:\n", causeNames[cause]);

	for (int i = 0; i < (int) NUM_REGISTERS; i++) {
		const RegisterInfo *reg = &registerNames[i];

		printf("  %s=%08x", reg->name, registers[reg->index]);

		if ((i % 4) == 3)
			putchar('\n');
	}
#endif

	for (;;)
		__asm__ volatile("");
}
