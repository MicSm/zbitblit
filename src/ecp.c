/****************************************************************************
 *   Copyright (C) 1999-2020 Semikov Michael Alexandrovitch                 *
 *                                                                          *
 *   This program is free software; you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation; either version 2 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with this program; if not, write to the Free Software            *
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.              *
 ****************************************************************************/


#if defined(_WIN32) || defined(_WIN64)
# define __WINDOWS_COMPILATION
#endif

#ifdef __WINDOWS_COMPILATION
# include <io.h>
# include <fcntl.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "inc/mio.h"
#include "inc/cmstruct.h"
#include "inc/arithm.h"
#include "inc/lzp_prep.h"
#include "inc/bwt.h"
#include "inc/mtf.h"

 /* lempel-ziff prediction engine variables ---------------------*/
#define LZP_LenThreshold 16

/* for BWT */
#define BWT_LenThreshold 8

static void PutHeader(CompressedHeader* Ptr, bfile* OutF)
{
	uint8_t* p;

	fwrite(ArcIdentifier, 12, 1, OutF->file);
	p = Ptr->FileName;
	while (*p) { fputc(*p, OutF->file); p++; }
	fputc(0, OutF->file);

	fputc((Ptr->UncompressedLen >> 24) & 0xff, OutF->file);
	fputc((Ptr->UncompressedLen >> 16) & 0xff, OutF->file);
	fputc((Ptr->UncompressedLen >> 8) & 0xff, OutF->file);
	fputc(Ptr->UncompressedLen & 0xff, OutF->file);
	fputc(Ptr->SystemFlag, OutF->file);

}

static int32_t GetHeader(CompressedHeader* Ptr, bfile* InF)
{
	uint8_t* p;
	uint8_t ArcRd[12];
	int16_t c;

	for (c = 0; c < 12; c++) ArcRd[c] = 0;
	fread(ArcRd, 12, 1, InF->file);
	for (c = 0; c < 12; c++) if (ArcRd[c] != ArcIdentifier[c]) break;
	if (c != 12) return 1;

	p = Ptr->FileName;
	while (c = fgetc(InF->file)) *p++ = (uint8_t)c;
	*p++ = 0;

	Ptr->UncompressedLen = fgetc(InF->file);
	Ptr->UncompressedLen <<= 8;
	Ptr->UncompressedLen |= (uint8_t)fgetc(InF->file);
	Ptr->UncompressedLen <<= 8;
	Ptr->UncompressedLen |= (uint8_t)fgetc(InF->file);
	Ptr->UncompressedLen <<= 8;
	Ptr->UncompressedLen |= (uint8_t)fgetc(InF->file);

	Ptr->SystemFlag = (uint8_t)fgetc(InF->file);
	return 0;
}

/*
   errors list:
	  1 - file size is zero
	  2 - can not open file
	  3 - no memory for allocating structure
	  0 - all o'k
*/

static int32_t CompressFile(char* InFileName, char* OutFileName, uint8_t P_ON_OFF,
	uint8_t BlockSizeCode, uint8_t StdOutOn)
{
	static const uint32_t PyramidTable[25] =
	{
	0, 2, 6, 14, 30, 62, 126, 254, 510, 1022, 2046, 4094, 8190, 16382,
	32766, 65534, 131070, 262142, 524286, 1048574, 2097150, 4194302,
	8388606, 16777214, 33554430
	};

	uint32_t InputFileSize;
	CompressedHeader Header;
	FILE* InputFileHeader;
	bfile* OutputFileHeader;
	ArithCodingContext StandardWriter, Code12Out;
	uint32_t BlockSize;
	uint32_t i, LenOut, TmpBlckLen;
	uint8_t* BwtInBuffer;
	uint32_t j;
	uint8_t* InputBuffer, * OutputBuffer;

	InputFileHeader = fopen(InFileName, "rb");
	if (InputFileHeader == NULL) return 2;
	InputFileSize = (uint32_t)filesize(InputFileHeader);

	if (InputFileSize == 0) {
		fclose(InputFileHeader);
		return 1;
	}

	BlockSize = ((uint32_t)BlockSizeCode) * 100 * 1024;

	if (StdOutOn)
		OutputFileHeader = bfopen_as_stdout();
	else {
		OutputFileHeader = bfopen(OutFileName, "wb");
		if (OutputFileHeader == NULL) return 2;
	}

	/* setup header, and then write header info */
	strcpy(Header.FileName, InFileName);
	Header.UncompressedLen = InputFileSize;
	Header.SystemFlag = (P_ON_OFF << 7) | BlockSizeCode;
	PutHeader(&Header, OutputFileHeader);

	/* setup 'preprocess' buffers, if necessary */
	if (P_ON_OFF != 0) {

		if (!CreateHashTables()) {
			fclose(InputFileHeader);
			if (StdOutOn) w_bfclose_as_stdout(OutputFileHeader);
			else {
				w_bfclose(OutputFileHeader);
				remove(OutFileName);
			}
			return 3;
		}
	}

	/* setup input block buffer, that output && indexes for BWT */

	InputBuffer = (uint8_t*)malloc(BlockSize * 2);
	OutputBuffer = (uint8_t*)malloc(BlockSize * 2);
	uint32_t* idxs = (uint32_t*)malloc(BlockSize * 2 * (uint32_t)sizeof(uint32_t));

	if (InputBuffer == NULL || OutputBuffer == NULL || idxs == NULL || !SetupBwtBuffers())
	{
		if (InputBuffer != NULL) free(InputBuffer);
		if (OutputBuffer != NULL) free(OutputBuffer);
		if (idxs != NULL) free(idxs);
		if (P_ON_OFF != 0)
		{
			DestructHashTables();
		}
		fclose(InputFileHeader);
		if (StdOutOn) w_bfclose_as_stdout(OutputFileHeader);
		else
		{
			w_bfclose(OutputFileHeader);
			remove(OutFileName);
		}
		return 3;
	}
	SetupContext(&Code12Out, 258);
	SetupContext(&StandardWriter, 257);

	i = InputFileSize;
	MtfSetup();
	while (i != 0)
	{
		TmpBlckLen = (i >= BlockSize ? BlockSize : i);
		fread(InputBuffer, TmpBlckLen, 1, InputFileHeader);
		i -= TmpBlckLen;
		if (P_ON_OFF != 0 && TmpBlckLen >= LZP_LenThreshold)
		{
			/* then do preprocessing */
			CleanHashTables();
			LenOut = LZP_PREPROCESS(InputBuffer, OutputBuffer, TmpBlckLen);

			BwtInBuffer = OutputBuffer;
		}
		else {
			LenOut = TmpBlckLen;
			BwtInBuffer = InputBuffer;
		}

		if (LenOut >= BWT_LenThreshold) {
			/* do BWT, mtf, 0-1 coding , arithmetic coding */
			TmpBlckLen = BWT_TRANSFORM(LenOut, BwtInBuffer, idxs);

			EncodeChar(1, OutputFileHeader, &StandardWriter);

			EncodeChar((int16_t)((TmpBlckLen >> 24) & 0xff), OutputFileHeader, &StandardWriter);
			EncodeChar((int16_t)((TmpBlckLen >> 16) & 0xff), OutputFileHeader, &StandardWriter);
			EncodeChar((int16_t)((TmpBlckLen >> 8) & 0xff), OutputFileHeader, &StandardWriter);
			EncodeChar((int16_t)(TmpBlckLen & 0xff), OutputFileHeader, &StandardWriter);

			/* do mtf, 0-1, ari */
			//MtfSetup();

			j = 0;
			while (j < LenOut) {
				uint16_t mtf_value = 0;
				uint32_t zeroes_count = 0;

				while (j < LenOut && (mtf_value = GetMtfValue(BwtInBuffer[((LenOut + idxs[j]) - 3) % LenOut])) == 0)
				{
					++zeroes_count;
					++j;
				}

				if (zeroes_count-- > 0)
				{
					uint32_t lhs = 0, rhs = 24, mid;
					while (lhs != rhs)
					{
						mid = (lhs + rhs) / 2;
						if (zeroes_count > PyramidTable[mid]) lhs = mid + 1;
						else rhs = mid;
					}
					
					if (PyramidTable[lhs] == zeroes_count)
					{
						++lhs;
					}
					
					zeroes_count -= PyramidTable[lhs - 1];

					do
					{
						EncodeChar((int16_t)(zeroes_count & 1), OutputFileHeader, &Code12Out);
						zeroes_count >>= 1;
					} while (--lhs);
				}

				if (mtf_value != 0)
				{
					EncodeChar((int16_t)(mtf_value + 1), OutputFileHeader, &Code12Out);
				}
				j++;
			}
			EncodeChar(257, OutputFileHeader, &Code12Out);
		}
		else {
			EncodeChar(0, OutputFileHeader, &StandardWriter);
			for (j = 0; j < LenOut; j++)
				EncodeChar((int16_t)BwtInBuffer[j], OutputFileHeader, &StandardWriter);
			EncodeChar(256, OutputFileHeader, &StandardWriter);
		}
	}
	FinishEncode(OutputFileHeader);

	// free mem, close files
	free(idxs);
	if (P_ON_OFF != 0)
	{
		DestructHashTables();
	}
	free(InputBuffer);
	free(OutputBuffer);
	FreeBwtBuffers();
	fclose(InputFileHeader);
	if (StdOutOn) w_bfclose_as_stdout(OutputFileHeader);
	else w_bfclose(OutputFileHeader);

	return 0;
}

static int32_t DeCompressFile(char* InFileName, uint8_t StdOutOn)
{
	uint32_t InputFileSize;
	CompressedHeader Header;
	bfile* InputFileHeader;
	FILE* OutputFileHeader;
	ArithCodingContext StandardWriter, Code12Out;
	uint32_t BlockSize;
	uint32_t i, LenOut, TmpBlckLen;
	uint8_t* BwtInBuffer;
	uint32_t j, NumZeroes;
	uint16_t NxVal;
	uint8_t* InputBuffer, * OutputBuffer;
	uint32_t TmpSum;
	uint32_t StringPos;

	InputFileHeader = bfopen(InFileName, "rb");
	if (InputFileHeader == NULL) return 2;

	if (GetHeader(&Header, InputFileHeader)) {
		r_bfclose(InputFileHeader);
		return 2;
	}

	InputFileSize = Header.UncompressedLen;
	BlockSize = ((uint32_t)(Header.SystemFlag & 0x7f)) * 100 * 1024;
	if (StdOutOn) OutputFileHeader = stdout;
	else OutputFileHeader = fopen(Header.FileName, "wb");

	if (OutputFileHeader == NULL) {
		r_bfclose(InputFileHeader);
		fclose(OutputFileHeader);
		return 2;
	}

	/* setup 'preprocess' buffers, if necessary */
	if ((Header.SystemFlag & 0x80) != 0) {

		if (!CreateHashTables()) {
			r_bfclose(InputFileHeader);
			if (!StdOutOn) {
				fclose(OutputFileHeader);
				remove(Header.FileName);
			}
			return 3;
		}
	}

	/* setup input block buffer, that output && indexes for BWT */

	InputBuffer = (uint8_t*)malloc(BlockSize * 2);
	OutputBuffer = (uint8_t*)malloc(BlockSize * 2);
	uint32_t* idxs = (uint32_t*)malloc(BlockSize * 2 * (uint32_t)sizeof(uint32_t));

	if (InputBuffer == NULL || OutputBuffer == NULL || idxs == NULL ||
		!SetupBwtBuffers()) {
		if (InputBuffer != NULL) free(InputBuffer);
		if (OutputBuffer != NULL) free(OutputBuffer);
		if (idxs != NULL) free(idxs);
		if ((Header.SystemFlag & 0x80) != 0) {
			DestructHashTables();
		}
		r_bfclose(InputFileHeader);
		if (!StdOutOn) {
			fclose(OutputFileHeader);
			remove(Header.FileName);
		}
		return 3;
	}
	SetupContext(&Code12Out, 258);
	SetupContext(&StandardWriter, 257);
	StartDecode(InputFileHeader);

	i = InputFileSize;
	DeMtfSetup();
	while (i != 0) {
		TmpBlckLen = (i >= BlockSize ? BlockSize : i);
		i -= TmpBlckLen;

		LenOut = 0;
		if (DecodeChar(InputFileHeader, &StandardWriter) == 1) {
			//[unari,un1-2,unmtf],unbwt

			StringPos = DecodeChar(InputFileHeader, &StandardWriter);
			StringPos <<= 8;
			StringPos |= DecodeChar(InputFileHeader, &StandardWriter);
			StringPos <<= 8;
			StringPos |= DecodeChar(InputFileHeader, &StandardWriter);
			StringPos <<= 8;
			StringPos |= DecodeChar(InputFileHeader, &StandardWriter);

			TmpSum = 0; j = 1; NumZeroes = 0;
			while ((NxVal = DecodeChar(InputFileHeader, &Code12Out)) != 257)
			{
				if (NxVal < 2) {
					if (NxVal != 0) TmpSum |= j;
					j <<= 1; NumZeroes++;
				}
				else {
					if (NumZeroes > 0) {
						TmpSum += (1 << NumZeroes) - 1;
						while (TmpSum--)
							OutputBuffer[LenOut++] = GetByMtfPosition(0);

						TmpSum = 0; j = 1; NumZeroes = 0;
					}
					OutputBuffer[LenOut++] = (uint8_t)GetByMtfPosition(NxVal - 1);
				}
			}

			if (NumZeroes > 0) {
				TmpSum += (1 << NumZeroes) - 1;
				while (TmpSum--)
					OutputBuffer[LenOut++] = GetByMtfPosition(0);
			}

			// unbwt
			UnBWT(StringPos, LenOut, OutputBuffer, InputBuffer, idxs);
		}
		else {
			while ((NxVal = DecodeChar(InputFileHeader, &StandardWriter)) != 256)
			{
				OutputBuffer[LenOut++] = (uint8_t)NxVal;
			}
			BwtInBuffer = OutputBuffer; OutputBuffer = InputBuffer;
			InputBuffer = BwtInBuffer;
		}
		if ((Header.SystemFlag & 0x80) != 0 && TmpBlckLen >= LZP_LenThreshold) {
			CleanHashTables();
			LenOut = UnPreprocess(InputBuffer, OutputBuffer, LenOut);
		}
		else {
			BwtInBuffer = OutputBuffer; OutputBuffer = InputBuffer;
			InputBuffer = BwtInBuffer;
		}
		fwrite(OutputBuffer, LenOut, 1, OutputFileHeader);
	}

	// free mem, close files
	free(idxs);
	if ((Header.SystemFlag & 0x80) != 0) {
		DestructHashTables();
	}
	free(InputBuffer);
	free(OutputBuffer);
	FreeBwtBuffers();
	r_bfclose(InputFileHeader);
	if (!StdOutOn)
		fclose(OutputFileHeader);
	return 0;
}

enum ErrorsVariants
{
	NoProcessFile, BufSizeWrong, UnknownAction, BadStdout,
	ZeroFileSize, FileNotOpened, NoMemory, BadKey, NoErr
};

static void ExitWithError(enum ErrorsVariants ErVar)
{
#define P(a,b) case a : { fprintf(stderr,"\n"b"\n"); } break;
	switch (ErVar) {
		P(BadStdout, "_Can't write to current STDOUT_");
		P(NoProcessFile, "_No file to process_");
		P(BufSizeWrong, "_Uncorrect buffer size_");
		P(UnknownAction, "_Unknown action requested_");
		P(ZeroFileSize, "_File size is zero_");
		P(FileNotOpened, "_Can't open file_");
		P(NoMemory, "_No memory for processing_");
		P(BadKey, "_Unknown key in command line_");
	}
#undef P
}

void main(int ArgsNum, char** ArgsValue)
{
	int number;
	int32_t ErrorCode, i, j;
	char* p;
	enum ErrorsVariants Err;

	int c_key, p_key, b_key, d_key;
	char ProcessFile[257], OutputFile[257];

	c_key = 0; p_key = 0; b_key = 0; *ProcessFile = 0;
	d_key = 0;
	Err = NoErr;
	for (i = 1; i < ArgsNum; i++) {
		p = ArgsValue[i];
		if (*p == '-')
			if (strlen(p) >= 2)
				for (j = 1; j < strlen(p); j++) {
					switch (p[j]) {
					case 'c': case 'C': c_key = 1; break;
					case 'p': case 'P': p_key = 1; break;
					case 'b': case 'B':
						number = 0;
						while (++j < strlen(p))
							if (!isdigit(p[j])) break;
							else number = number * 10 + (int)(p[j] - '0');
						j--;
						if (number < 1 || number>127)
							Err = BufSizeWrong;
						else b_key = number;
						break;
					case 'd': case 'D': d_key = 1; break;
					default: Err = BadKey;
					}
					if (Err != NoErr) break;
				}
			else Err = UnknownAction;
		else {
			if (*ProcessFile) Err = UnknownAction;
			else strcpy(ProcessFile, p);
		}
		if (Err != NoErr) break;
	}
	if (Err != NoErr) { ExitWithError(Err); return; }
	if (d_key == 1 && (p_key == 1 || b_key != 0)) {
		ExitWithError(UnknownAction);
		return;
	}

	if ((c_key || p_key || b_key || d_key) && !*ProcessFile) {
		ExitWithError(NoProcessFile);
		return;
	}
	if (!c_key && !p_key && !b_key && !d_key && !*ProcessFile) {
		printf
		("\n"
			"Experimental compression program. (c) 1999-2020 by Michael Semikov\n"
			"Version 0.1\n\n"
			"use: zbitblit [ [-c] { [-p] [-bNNN] file_to_compress | -d file_to_decompress} ]\n\n"
			"This program is one-file archiver and also it has some keys:\n"
			"    -c - Write data to STDOUT\n\n"
			"    -p - Compress with use of preprocessing stage.\n\n"
			"         Sometimes \"-p\" can improve compression (enables LZP stage),\n"
			"         especially on highly redundant data\n\n"
			"    -b{1 .. 127} - Use block compression size of N*100 KBytes,\n"
			"                   this option also improves compression ratio\n"
			"                   Default key=3\n\n"
			"    -d - Decompress archive\n\n\n"
			"Warning! You use this program at your own risk!\n"
		);
		return;
	}
	if (!d_key && !b_key) b_key = 3;
	if (c_key && isatty(fileno(stdout))) {
		ExitWithError(BadStdout);
		return;
	}
#ifdef __WINDOWS_COMPILATION
	if (c_key)
		setmode(fileno(stdout), O_BINARY);
#endif
	if (!d_key) {
		strcpy(OutputFile, ProcessFile);
		strcat(OutputFile, ".zbb");
		ErrorCode = CompressFile(ProcessFile, OutputFile, (uint8_t)p_key,
			(uint8_t)b_key, (uint8_t)c_key);
		if (!ErrorCode && !c_key) {
			float ss1, ss2; FILE* fpn;
			fpn = fopen(ProcessFile, "rb");
			ss1 = (float)filesize(fpn);
			fclose(fpn);
			fpn = fopen(OutputFile, "rb");
			ss2 = (float)filesize(fpn);
			fclose(fpn);
			ss1 = 8.0f * (ss2 / ss1);
			fprintf(stderr, "\nFile \"%s\" was compressed\nThe %7f bits per symbol ratio was obtained\n",
				ProcessFile, ss1);
		}
	}
	else {
		ErrorCode = DeCompressFile(ProcessFile, (uint8_t)c_key);
		if (!ErrorCode)
			fprintf(stderr, "\nArchive file \"%s\" was successfully processed\n",
				ProcessFile);
	}
	switch (ErrorCode) {
	case 1: Err = ZeroFileSize; break;
	case 2: Err = FileNotOpened; break;
	case 3: Err = NoMemory;
	}
	if (ErrorCode) ExitWithError(Err);
}
