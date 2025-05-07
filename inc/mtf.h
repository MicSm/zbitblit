#pragma once

#include <stdint.h>

void MtfSetup(void);

void DeMtfSetup(void);

uint16_t GetMtfValue(uint16_t InValue);

uint8_t GetByMtfPosition(uint8_t Position);
