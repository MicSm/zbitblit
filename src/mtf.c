#include "inc/mtf.h"

uint16_t MtfLinks[256];
uint16_t HeadPtr;

uint8_t* DeMtfArray;

void MtfSetup(void) {
	uint16_t i;
	HeadPtr = 0;
	for (i = 0; i < 256; i++)
		MtfLinks[i] = (uint16_t)(i + 1);
}

void DeMtfSetup(void) {
	uint16_t i;
	DeMtfArray = (uint8_t*)MtfLinks;
	for (i = 0; i < 256; i++) DeMtfArray[i] = (uint8_t)i;
}

uint16_t GetMtfValue(uint16_t InValue) {
	uint16_t SkippedNums, p, PredPtr;

	SkippedNums = 0;
	p = HeadPtr;
	while (p != InValue) {
		PredPtr = p;
		p = MtfLinks[p];
		SkippedNums++;
	}
	if (SkippedNums) {
		MtfLinks[PredPtr] = MtfLinks[p];
		MtfLinks[p] = HeadPtr;
		HeadPtr = p;
	}
	return SkippedNums;
}

uint8_t GetByMtfPosition(uint8_t Position) {
	uint8_t Result;
	uint8_t i;

	Result = DeMtfArray[Position];
	if (Position != 0) {
		for (i = Position; i > 0; i--) DeMtfArray[i] = DeMtfArray[i - 1];
		DeMtfArray[0] = Result;
	}
	return Result;
}
