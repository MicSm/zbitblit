#pragma once

#include <stdint.h>

extern uint32_t* idxs;

int SetupBwtBuffers(void);

void FreeBwtBuffers(void);

uint32_t BWT_TRANSFORM(uint32_t len, uint8_t* pb);

void UnBWT(uint32_t StrPos, uint32_t len, uint8_t* InputBuffer, uint8_t* OutputBuffer);
