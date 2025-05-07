#pragma once

#include <stdio.h>
#include "mtypes.h"

/* defines buffered i/o */
#define B_I_O

#define SBUFF 4096

typedef struct
{
	FILE* file;       /* for stream I/O */
	uint32  rbuf;   /* read bit buffer */
	uint8  rcnt;       /* read bit count */
	uint32  wbuf;   /* write bit buffer */
	uint8  wcnt;       /* write bit count */
#ifdef B_I_O
	uint8 buff[SBUFF];
#endif
} bfile;

int32 filesize(FILE* tmp__);

bfile* bfopen_as_stdout(void);

bfile* bfopen(char* name, char* mode);

uint8 bfread(bfile* bf);

void bfwrite(uint8 bit, bfile* bf);

void w_bfclose(bfile* bf);

void w_bfclose_as_stdout(bfile* bf);

void r_bfclose_as_stdout(bfile* bf);

void r_bfclose(bfile* bf);
