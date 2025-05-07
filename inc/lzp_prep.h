#pragma once

#include <stdlib.h>
#include <stdint.h>

extern uint8_t** HashTable4, ** HashTable5;
#define HTSIZE4 65536UL
#define HTSIZE5 32768UL

/* clean hash tables */
void CleanTabs(void);

uint32_t LZP_PREPROCESS(uint8_t* InData, uint8_t* OutData, uint32_t InLength);

uint32_t UnPreprocess(uint8_t* InData, uint8_t* OutData, uint32_t InLength);
