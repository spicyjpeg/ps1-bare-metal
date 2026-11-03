/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define SPU_RAM_ADDR_UNIT 8
#define SPU_RAM_SIZE      (0x10000 * SPU_RAM_ADDR_UNIT)
#define SPU_SAMPLE_OFFSET 0x1000

#define SPU_NUM_CHANNELS 24
#define SPU_PITCH_UNIT   (1 << 12)

#ifdef __cplusplus
extern "C" {
#endif

void initSPU(void);
void waitForSPUDMADone(void);

void sendSPURAMData   (const void *data, unsigned int offset, size_t length);
void receiveSPURAMData(void       *data, unsigned int offset, size_t length);

void resetAllSPUChannels(void);
int findFreeSPUChannel(void);

int playSample(unsigned int offset, unsigned int sampleRate, int16_t volume);

#ifdef __cplusplus
}
#endif
