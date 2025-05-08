#pragma once

#include <stdint.h>

int SetupBwtBuffers(void);

void FreeBwtBuffers(void);

uint32_t BWT_TRANSFORM(uint32_t len, uint8_t* pb, uint32_t* idxs);

void UnBWT(uint32_t StrPos, uint32_t len, uint8_t* InputBuffer, uint8_t* OutputBuffer, uint32_t* idxs);
