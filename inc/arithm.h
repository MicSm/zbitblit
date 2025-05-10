#pragma once

#include <stdint.h>
#include "inc/mio.h"

/* our alphabet */
/* character code = 0, 1, ..., MAX_ALPHABET_SIZE - 1 */
#define MAX_ALPHABET_SIZE 260

typedef struct
{
	int16_t alphabet_size;
	int16_t  c2s[MAX_ALPHABET_SIZE], s2c[MAX_ALPHABET_SIZE + 1];
	uint16_t sf[MAX_ALPHABET_SIZE + 1], scf[MAX_ALPHABET_SIZE + 1];
} ArithCodingContext;

/* Initialize model */
void SetupContext(ArithCodingContext* ctx, int16_t alphabet_size);

/* encode pending char */
void EncodeChar(int16_t ch, bfile* bin_file, ArithCodingContext* ctx);

/* decode next char */
int16_t DecodeChar(bfile* bin_file, ArithCodingContext* ctx);

/* must be performed when end of stream */
void FinishEncode(bfile* fil);

void StartDecode(bfile* fil);
