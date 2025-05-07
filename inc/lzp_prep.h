#pragma once

#include <stdlib.h>
#include "inc/mtypes.h"

extern uint8** HashTable4, ** HashTable5;
#define HTSIZE4 65536UL
#define HTSIZE5 32768UL

/* clean hash tables */
void CleanTabs(void);

uint32 LZP_PREPROCESS(uint8* InData, uint8* OutData, uint32 InLength);

uint32 UnPreprocess(uint8* InData, uint8* OutData, uint32 InLength);
