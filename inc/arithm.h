#pragma once

#include "inc/mtypes.h"
#include "inc/mio.h"

/* our alphabet */
/* character code = 0, 1, ..., MAX_CHAR - 1 */
#define MAX_CHAR 258

typedef struct symb
{
	int16  c2s[MAX_CHAR], s2c[MAX_CHAR + 1];
	uint16 sf[MAX_CHAR + 1], scf[MAX_CHAR + 1];
} SYMB;

/* Initialize model */
void StartModel(SYMB* ptr, int16 n_char);

/* encode current char */
void EncodeChar(int16 ch, bfile* fil, SYMB* ptr, int16 n_char);

/* must be performed when end of stream */
void EncodeEnd(bfile* fil);

void StartDecode(bfile* fil);

int16 DecodeChar(bfile* fil, SYMB* ptr, int16 n_char);
