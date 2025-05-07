#pragma once

#include "inc/mtypes.h"

extern uint32* idxs;

int SetupBwtBuffers(void);

void FreeBwtBuffers(void);

uint32 BWT_TRANSFORM(uint32 len, uint8* pb);

void UnBWT(uint32 StrPos, uint32 len, uint8* InputBuffer, uint8* OutputBuffer);
