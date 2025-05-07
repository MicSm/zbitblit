/* In this module you can find preprocessor algo, based on LZP (c) by
   Charles Bloom. This preprocessor especially useful on so called
   'water' data, and also it can help improve BWT speed.
*/

#include <stdlib.h>
#include "inc/lzp_prep.h"

uint8_t** HashTable4, ** HashTable5;

static uint32_t HashFunction4(uint32_t index, uint8_t* PTR) {
	uint32_t x;

	x = (((uint32_t)PTR[index - 4]) << 24) | (((uint32_t)PTR[index - 3]) << 16) |
		(((uint32_t)PTR[index - 2]) << 8) | (((uint32_t)PTR[index - 1]));
	x = (x >> 15) ^ x ^ (x >> 3);
	return x & (HTSIZE4 - 1);
}

static uint32_t HashFunction5(uint32_t index, uint8_t* PTR) {
	uint32_t x;

	x = (((uint32_t)PTR[index - 4]) << 24) | (((uint32_t)PTR[index - 3]) << 16) |
		(((uint32_t)PTR[index - 2])) << 8 | (((uint32_t)PTR[index - 1]));
	x = (x >> 25 | ((uint32_t)PTR[index - 5]) << 7) ^ x ^ (x << 4);
	return x & (HTSIZE5 - 1);
}

/* You can modify this const and get better compression */
#define LowerLimit 38

static void OutPutLength(uint32_t OutPutLength, uint8_t* OutBuffer, uint32_t* PBuffer) {
	while (OutPutLength > 254) {
		OutBuffer[(*PBuffer)++] = 255;
		OutPutLength -= 255;
	}
	OutBuffer[(*PBuffer)++] = (uint8_t)OutPutLength;
}

static uint32_t GetLength(uint8_t* InputBuffer, uint32_t* PBuff) {
	uint32_t Result;

	Result = 0;
	while (Result += (uint32_t)InputBuffer[*PBuff], InputBuffer[(*PBuff)++] == 255);
	return Result;
}

/* clean hash tables */
void CleanTabs(void) {
	uint32_t i;

	for (i = 0; i < HTSIZE4; i++) HashTable4[i] = NULL;
	for (i = 0; i < HTSIZE5; i++) HashTable5[i] = NULL;
}

uint32_t LZP_PREPROCESS(uint8_t* InData, uint8_t* OutData, uint32_t InLength) {
	uint32_t i, CommonLength;
	uint32_t OutLength;
	uint32_t HashIndex4, HashIndex5;
	uint8_t* Pointer4, * Pointer5;
	uint32_t Pointer;
	uint8_t* PastPointer;

	/* send 5 bytes to output */
	OutLength = 0;
	for (i = 0; i < 5; i++) OutData[i] = InData[i];
	OutLength += 5;

	/* begin preprocessing */

	Pointer = 5;

	HashTable4[HashFunction4(4, InData)] = InData + 4;
	while (Pointer < InLength) {
		/* get hash adresses */

		HashIndex4 = HashFunction4(Pointer, InData);
		HashIndex5 = HashFunction5(Pointer, InData);

		Pointer4 = HashTable4[HashIndex4];
		Pointer5 = HashTable5[HashIndex5];

		HashTable5[HashIndex5] = HashTable4[HashIndex4] = InData + Pointer;

		if (Pointer5 != NULL || Pointer4 != NULL) {
			if (Pointer5 != NULL) PastPointer = Pointer5;
			else PastPointer = Pointer4;

			CommonLength = 0;

			while (Pointer < InLength) {
				if (InData[Pointer] != *PastPointer) break;
				Pointer++;
				PastPointer++;
				CommonLength++;
			}
			if (CommonLength > 0 && CommonLength < LowerLimit) {
				Pointer -= CommonLength;
				CommonLength = 0;
			}
			if (CommonLength)
				OutPutLength(CommonLength - LowerLimit + 256UL, OutData,
					&OutLength);

			else
				OutPutLength((uint32_t)InData[Pointer++], OutData, &OutLength);

		}
		else  OutData[OutLength++] = InData[Pointer++];
	}
	return OutLength;
}

uint32_t UnPreprocess(uint8_t* InData, uint8_t* OutData, uint32_t InLength) {
	uint32_t i, CommonLength;
	uint32_t OutLength;
	uint32_t HashIndex4, HashIndex5;
	uint8_t* Pointer4, * Pointer5;
	uint32_t Pointer;
	uint8_t* PastPointer;

	/* send 5 bytes to output */
	OutLength = 0;
	for (i = 0; i < 5; i++) OutData[i] = InData[i];
	OutLength += 5;

	/* begin unpreprocessing */

	Pointer = 5;

	HashTable4[HashFunction4(4, OutData)] = OutData + 4;
	while (Pointer < InLength) {
		/* get hash adresses */

		HashIndex4 = HashFunction4(OutLength, OutData);
		HashIndex5 = HashFunction5(OutLength, OutData);

		Pointer4 = HashTable4[HashIndex4];
		Pointer5 = HashTable5[HashIndex5];

		HashTable5[HashIndex5] = HashTable4[HashIndex4] = OutData + OutLength;

		if (Pointer5 != NULL || Pointer4 != NULL) {
			if (Pointer5 != NULL) PastPointer = Pointer5;
			else PastPointer = Pointer4;
			CommonLength = GetLength(InData, &Pointer);
			if (CommonLength < 256)
				OutData[OutLength++] = (uint8_t)CommonLength;
			else {
				CommonLength = CommonLength - 256 + LowerLimit;
				while (CommonLength--)
					OutData[OutLength++] = *PastPointer++;
			}
		}
		else  OutData[OutLength++] = InData[Pointer++];
	}
	return OutLength;
}
