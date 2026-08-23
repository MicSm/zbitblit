#pragma once

#include <stdio.h>
#include <stdint.h>

/* defines buffered i/o */
#define B_I_O

#define SBUFF 4096

typedef struct
{
	FILE* file;       /* for stream I/O */
	uint32_t  rbuf;   /* read bit buffer */
	uint8_t  rcnt;       /* read bit count */
	uint32_t  wbuf;   /* write bit buffer */
	uint8_t  wcnt;       /* write bit count */
#ifdef B_I_O
	uint8_t buff[SBUFF];
#endif
} bfile;

int32_t filesize(FILE* tmp__);

bfile* bfopen_as_stdout(void);

// boffin: refused a writable pointer to the open-mode literals the caller passes
bfile* bfopen(const char* name, const char* mode);

uint8_t bfread(bfile* bf);

void bfwrite(uint8_t bit, bfile* bf);

void w_bfclose(bfile* bf);

void w_bfclose_as_stdout(bfile* bf);

void r_bfclose_as_stdout(bfile* bf);

void r_bfclose(bfile* bf);
