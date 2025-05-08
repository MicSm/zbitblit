#pragma once

#include <stdlib.h>
#include <stdint.h>

/* allocate hashtables */
int CreateHashTables(void);

/* destroy hashtables */
void DestructHashTables(void);

/* clean hashtables */
void CleanHashTables(void);

uint32_t LZP_PREPROCESS(uint8_t* InData, uint8_t* OutData, uint32_t InLength);

uint32_t UnPreprocess(uint8_t* InData, uint8_t* OutData, uint32_t InLength);
