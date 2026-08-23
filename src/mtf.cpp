#include "inc/mtf.h"

typedef struct
{
	union
	{
		uint16_t MtfLinks[256];
		uint8_t DeMtfArray[256];
	} data;

	uint16_t HeadPtr;

} MtfState;

static MtfState mtf_state;

void MtfSetup(void)
{
	mtf_state.HeadPtr = 0;
	for (uint16_t i = 0; i < 256; i++)
	{
		mtf_state.data.MtfLinks[i] = (uint16_t)(i + 1);
	}
}

void DeMtfSetup(void)
{
	for (uint16_t i = 0; i < 256; i++)
	{
		mtf_state.data.DeMtfArray[i] = (uint8_t)i;
	}
}

uint16_t GetMtfValue(uint16_t InValue)
{
	uint16_t SkippedNums = 0, p = 0, PredPtr = 0;

	SkippedNums = 0;
	p = mtf_state.HeadPtr;
	while (p != InValue) {
		PredPtr = p;
		p = mtf_state.data.MtfLinks[p];
		SkippedNums++;
	}
	if (SkippedNums) {
		mtf_state.data.MtfLinks[PredPtr] = mtf_state.data.MtfLinks[p];
		mtf_state.data.MtfLinks[p] = mtf_state.HeadPtr;
		mtf_state.HeadPtr = p;
	}
	return SkippedNums;
}

uint8_t GetByMtfPosition(uint8_t Position)
{
	uint8_t Result = mtf_state.data.DeMtfArray[Position];

	if (Position != 0) {
		for (uint8_t i = Position; i > 0; i--)
		{
			mtf_state.data.DeMtfArray[i] = mtf_state.data.DeMtfArray[i - 1];
		}
		mtf_state.data.DeMtfArray[0] = Result;
	}
	return Result;
}
