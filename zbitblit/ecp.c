/****************************************************************************
 *   Copyright (C) 1999,2000   Michael_Semikov@p16.f27.n5059.z2.fidonet.org *
 *   Copyright (C) 2000 Semikov Michael Alexandrovitch                      *
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


#pragma warning(disable : 4996)

#if defined(_WIN32) || defined(_WIN64)
# define __WINDOWS_COMPILATION
#endif

#ifdef __WINDOWS_COMPILATION
# include <io.h>
# include <fcntl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mtypes.h"
#include "mio.h"
#include "cmstruct.h"
#include "lzp_prep.c"
#include "arithm.c"
#include "bwt.c"
#include "mtf.c"

CompressedHeader Header;

/* lempel-ziff prediction engine variables ---------------------*/
#define LZP_LenThreshold 16
/* lempel-ziff prediction engine variables ---------------------*/
/* for BWT */
#define BWT_LenThreshold 8
/* for BWT */



void PutHeader(CompressedHeader *Ptr, bfile *OutF) {
uint8 *p;

 fwrite(ArcIdentifier,12,1,OutF->file);
 p=Ptr->FileName;
 while(*p) { fputc(*p,OutF->file); p++; }
 fputc(0,OutF->file);

 fputc((Ptr->UncompressedLen>>24)&0xff,OutF->file);
 fputc((Ptr->UncompressedLen>>16)&0xff,OutF->file);
 fputc((Ptr->UncompressedLen>>8)&0xff,OutF->file);
 fputc(Ptr->UncompressedLen&0xff,OutF->file);
 fputc(Ptr->SystemFlag,OutF->file);

}

int32 GetHeader(CompressedHeader *Ptr, bfile *InF) {
uint8 *p;
uint8 ArcRd[12];
int16 c;

 for (c=0;c<12;c++) ArcRd[c]=0;
 fread(ArcRd,12,1,InF->file);
 for (c=0;c<12;c++) if(ArcRd[c]!=ArcIdentifier[c]) break;
 if (c!=12) return 1;

 p=Ptr->FileName;
 while(c=fgetc(InF->file)) *p++=(uint8)c;
 *p++=0;

 Ptr->UncompressedLen=fgetc(InF->file);
 Ptr->UncompressedLen<<=8;
 Ptr->UncompressedLen|=(uint8)fgetc(InF->file);
 Ptr->UncompressedLen<<=8;
 Ptr->UncompressedLen|=(uint8)fgetc(InF->file);
 Ptr->UncompressedLen<<=8;
 Ptr->UncompressedLen|=(uint8)fgetc(InF->file);

 Ptr->SystemFlag=(uint8)fgetc(InF->file);
 return 0;
}


/*
   errors list:
      1 - file size is zero
      2 - can not open file
      3 - no memory for allocating structure
      0 - all o'k
*/


uint32 PyramidTable[25]={
0, 2, 6, 14, 30, 62, 126, 254, 510, 1022, 2046, 4094, 8190, 16382,
32766, 65534, 131070, 262142, 524286, 1048574, 2097150, 4194302,
8388606, 16777214, 33554430};

SYMB StandardWriter, Code12Out;

int32 CompressFile(char *InFileName, char *OutFileName, uint8 P_ON_OFF,
                   uint8 BlockSizeCode, uint8 StdOutOn) {
uint32 InputFileSize;
FILE *InputFileHeader;
bfile *OutputFileHeader;
uint32 BlockSize;
uint32 i,LenOut,TmpBlckLen;
uint8 *BwtInBuffer;
uint32 j,NumZeroes;
uint16 NxVal;
uint8 *InputBuffer, *OutputBuffer;
register uint32 Left,Right,Middle;


 InputFileHeader=fopen(InFileName,"rb");
 if (InputFileHeader==NULL) return 2;
 InputFileSize=(uint32)filesize(InputFileHeader);

 if (InputFileSize==0) {
    fclose(InputFileHeader);
    return 1;
 }


 BlockSize=((uint32)BlockSizeCode)*100*1024;

 if (StdOutOn)
    OutputFileHeader=bfopen_as_stdout();
  else {
    OutputFileHeader=bfopen(OutFileName,"wb");
    if (OutputFileHeader==NULL) return 2;
   }

 /* setup header, and then write header info */
 strcpy(Header.FileName,InFileName);
 Header.UncompressedLen=InputFileSize;
 Header.SystemFlag=(P_ON_OFF<<7)|BlockSizeCode;
 PutHeader(&Header,OutputFileHeader);

 /* setup 'preprocess' buffers, if necessary */
 if (P_ON_OFF!=0) {
    /* allocate memory for hash tables */
    HashTable4=(uint8 **)malloc(HTSIZE4*(uint32)sizeof(uint8 *));
    HashTable5=(uint8 **)malloc(HTSIZE5*(uint32)sizeof(uint8 *));

    if (HashTable4==NULL || HashTable5==NULL) {
       if (HashTable4!=NULL) free(HashTable4);
       if (HashTable5!=NULL) free(HashTable5);
       fclose(InputFileHeader);
       if (StdOutOn) w_bfclose_as_stdout(OutputFileHeader);
        else { w_bfclose(OutputFileHeader);
               remove(OutFileName);
             }
       return 3;
    }
 }

 /* setup input block buffer, that output && indexes for BWT */

 InputBuffer=(uint8 *)malloc(BlockSize*2);
 OutputBuffer=(uint8 *)malloc(BlockSize*2);
 idxs=(uint32 *)malloc(BlockSize*2*(uint32)sizeof(uint32));

 if (InputBuffer==NULL || OutputBuffer==NULL || idxs==NULL ||
    !SetupBwtBuffers()) {
    if (InputBuffer!=NULL) free(InputBuffer);
    if (OutputBuffer!=NULL) free(OutputBuffer);
    if (idxs!=NULL) free(idxs);
    if (P_ON_OFF!=0) {
       free(HashTable4);
       free(HashTable5);
    }
    fclose(InputFileHeader);
    if (StdOutOn) w_bfclose_as_stdout(OutputFileHeader);
     else { w_bfclose(OutputFileHeader);
               remove(OutFileName);
          }
    return 3;
 }
 free(idxs);
 StartModel(&Code12Out,258);
 StartModel(&StandardWriter,257);

 i=InputFileSize;
 MtfSetup();
 while (i!=0) {
       TmpBlckLen=(i>=BlockSize?BlockSize:i);
       fread(InputBuffer,TmpBlckLen,1,InputFileHeader);
       i-=TmpBlckLen;
       if (P_ON_OFF!=0 && TmpBlckLen>=LZP_LenThreshold) {
          /* then do preprocessing */
          CleanTabs();
          LenOut=LZP_PREPROCESS(InputBuffer,OutputBuffer,TmpBlckLen);

          BwtInBuffer=OutputBuffer;
       } else {
          LenOut=TmpBlckLen;
          BwtInBuffer=InputBuffer;
         }

       if (LenOut>=BWT_LenThreshold) {
          /* do BWT, mtf, 0-1 coding , arithmetic coding */

          idxs=(uint32 *)malloc(LenOut*(uint32)sizeof(uint32));
          TmpBlckLen=BWT_TRANSFORM(LenOut,BwtInBuffer);

          EncodeChar(1,OutputFileHeader,&StandardWriter,257);

          EncodeChar((int16)((TmpBlckLen>>24)&0xff),OutputFileHeader,
                    &StandardWriter,257);
          EncodeChar((int16)((TmpBlckLen>>16)&0xff),OutputFileHeader,
                    &StandardWriter,257);
          EncodeChar((int16)((TmpBlckLen>>8)&0xff),OutputFileHeader,
                    &StandardWriter,257);
          EncodeChar((int16)(TmpBlckLen&0xff),OutputFileHeader,
                    &StandardWriter,257);

          /* do mtf, 0-1, ari */
          //MtfSetup();
          j=0;

          while (j<LenOut) {
                NumZeroes=0;
                while (j<LenOut)
                      if ((NxVal=GetMtfValue(BwtInBuffer[idxs[j]>=3?
                         idxs[j]-3:LenOut+idxs[j]-3]))!=0) break;
                       else {
                             NumZeroes++;
                             j++;
                       }
                if (NumZeroes!=0) {
                   NumZeroes--;

                   Left=0;//-------
                   Right=24;
                   while (Left!=Right) {
                         Middle=(Left+Right)>>1;
                         if (NumZeroes>PyramidTable[Middle]) Left=Middle+1;
                          else Right=Middle;
                   }
                   if (PyramidTable[Left]==NumZeroes) Left++;
                   NumZeroes-=PyramidTable[Left-1];

                   do EncodeChar((int16)(NumZeroes&1),OutputFileHeader,
                                &Code12Out,258); while (NumZeroes>>=1,--Left);
                }
                if (NxVal!=0) { EncodeChar((int16)(NxVal+1),OutputFileHeader,
                                         &Code12Out,258);
                }
                j++;
          }
          free(idxs);
          EncodeChar(257,OutputFileHeader, &Code12Out,258);
       } else {
          EncodeChar(0,OutputFileHeader,&StandardWriter,257);
          for (j=0;j<LenOut;j++)
              EncodeChar((int16)BwtInBuffer[j],OutputFileHeader,&StandardWriter,257);
          EncodeChar(256,OutputFileHeader,&StandardWriter,257);
         }
 }
 EncodeEnd(OutputFileHeader);

 // free mem, close files

 if (P_ON_OFF!=0) {
    free(HashTable4);
    free(HashTable5);
 }
 free(InputBuffer);
 free(OutputBuffer);
 FreeBwtBuffers();
 fclose(InputFileHeader);
 if (StdOutOn) w_bfclose_as_stdout(OutputFileHeader);
  else w_bfclose(OutputFileHeader);

 return 0;
}

int32 DeCompressFile(char *InFileName, uint8 StdOutOn) {
uint32 InputFileSize;
bfile *InputFileHeader;
FILE *OutputFileHeader;
uint32 BlockSize;
uint32 i,LenOut,TmpBlckLen;
uint8 *BwtInBuffer;
uint32 j,NumZeroes;
uint16 NxVal;
uint8 *InputBuffer, *OutputBuffer;
uint32 TmpSum;
uint32 StringPos;

 InputFileHeader=bfopen(InFileName,"rb");
 if (InputFileHeader==NULL) return 2;

 if (GetHeader(&Header,InputFileHeader)) {
    r_bfclose(InputFileHeader);
    return 2;
 }

 InputFileSize=Header.UncompressedLen;
 BlockSize=((uint32)(Header.SystemFlag&0x7f))*100*1024;
 if (StdOutOn) OutputFileHeader=stdout;
  else OutputFileHeader=fopen(Header.FileName,"wb");

 if (OutputFileHeader==NULL) {
    r_bfclose(InputFileHeader);
    fclose(OutputFileHeader);
    return 2;
 }

 /* setup 'preprocess' buffers, if necessary */
 if ((Header.SystemFlag&0x80) != 0) {
    /* allocate memory for hash tables */
    HashTable4=(uint8 **)malloc(HTSIZE4*(uint32)sizeof(uint8 *));
    HashTable5=(uint8 **)malloc(HTSIZE5*(uint32)sizeof(uint8 *));

    if (HashTable4==NULL || HashTable5==NULL) {
       if (HashTable4!=NULL) free(HashTable4);
       if (HashTable5!=NULL) free(HashTable5);
       r_bfclose(InputFileHeader);
       if (!StdOutOn) {
          fclose(OutputFileHeader);
          remove(Header.FileName);
       }
       return 3;
    }
 }

 /* setup input block buffer, that output && indexes for BWT */

 InputBuffer=(uint8 *)malloc(BlockSize*2);
 OutputBuffer=(uint8 *)malloc(BlockSize*2);
 idxs=(uint32 *)malloc(BlockSize*2*(uint32)sizeof(uint32));

 if (InputBuffer==NULL || OutputBuffer==NULL || idxs==NULL ||
    !SetupBwtBuffers()) {
    if (InputBuffer!=NULL) free(InputBuffer);
    if (OutputBuffer!=NULL) free(OutputBuffer);
    if (idxs!=NULL) free(idxs);
    if ((Header.SystemFlag&0x80)!=0) {
       free(HashTable4);
       free(HashTable5);
    }
    r_bfclose(InputFileHeader);
    if (!StdOutOn) {
       fclose(OutputFileHeader);
       remove(Header.FileName);
    }
    return 3;
 }
 free(idxs);
 StartModel(&Code12Out,258);
 StartModel(&StandardWriter,257);
 StartDecode(InputFileHeader);

 i=InputFileSize;
 DeMtfSetup();
 while (i!=0) {
       TmpBlckLen=(i>=BlockSize?BlockSize:i);
       i-=TmpBlckLen;

       LenOut=0;
       if (DecodeChar(InputFileHeader,&StandardWriter,257)==1) {
          //[unari,un1-2,unmtf],unbwt

          StringPos=DecodeChar(InputFileHeader,&StandardWriter,257);
          StringPos<<=8;
          StringPos|=DecodeChar(InputFileHeader,&StandardWriter,257);
          StringPos<<=8;
          StringPos|=DecodeChar(InputFileHeader,&StandardWriter,257);
          StringPos<<=8;
          StringPos|=DecodeChar(InputFileHeader,&StandardWriter,257);

          TmpSum=0;j=1;NumZeroes=0;
          while ((NxVal=DecodeChar(InputFileHeader,
                &Code12Out,258))!=257)
                if (NxVal<2) {
                   if (NxVal!=0) TmpSum|=j;
                   j<<=1;NumZeroes++;
                } else {
                   if (NumZeroes>0) {
                      TmpSum+=(1<<NumZeroes)-1;
                      while (TmpSum--)
                            OutputBuffer[LenOut++]=GetByMtfPosition(0);

                      TmpSum=0;j=1;NumZeroes=0;
                   }
                   OutputBuffer[LenOut++]=(uint8)GetByMtfPosition(NxVal-1);
                  }

          if (NumZeroes>0) {
             TmpSum+=(1<<NumZeroes)-1;
             while (TmpSum--)
                   OutputBuffer[LenOut++]=GetByMtfPosition(0);
          }

          idxs=(uint32 *)malloc(LenOut*(uint32)sizeof(uint32));
          // unbwt
          UnBWT(StringPos,LenOut,OutputBuffer,InputBuffer);
          free(idxs);
       } else {
          while((NxVal=DecodeChar(InputFileHeader,
               &StandardWriter,257))!=256) {
               OutputBuffer[LenOut++]=(uint8)NxVal;
          }
          BwtInBuffer=OutputBuffer; OutputBuffer=InputBuffer;
          InputBuffer=BwtInBuffer;
         }
       if ((Header.SystemFlag&0x80)!=0 && TmpBlckLen>=LZP_LenThreshold) {
          CleanTabs();
          LenOut=UnPreprocess(InputBuffer, OutputBuffer, LenOut);
       }
        else {
          BwtInBuffer=OutputBuffer; OutputBuffer=InputBuffer;
          InputBuffer=BwtInBuffer;
         }
       fwrite(OutputBuffer,/*TmpBlckLen*/LenOut,1,OutputFileHeader);//-----bug
 }

 // free mem, close files

 if ((Header.SystemFlag&0x80)!=0) {
    free(HashTable4);
    free(HashTable5);
 }
 free(InputBuffer);
 free(OutputBuffer);
 FreeBwtBuffers();
 r_bfclose(InputFileHeader);
 if (!StdOutOn)
    fclose(OutputFileHeader);
 return 0;
}

enum ErrorsVariants { NoProcessFile, BufSizeWrong, UnknownAction, BadStdout,
                      ZeroFileSize, FileNotOpened, NoMemory, BadKey, NoErr };

void ExitWithError(enum ErrorsVariants ErVar) {
#define P(a,b) case a : fprintf(stderr,"\n"b"\n"); break;
 switch (ErVar) {
        P(BadStdout,"_Can't write to current STDOUT_");
        P(NoProcessFile,"_No file to process_")
        P(BufSizeWrong,"_Uncorrect buffer size_")
        P(UnknownAction,"_Unknown action requested_")
        P(ZeroFileSize,"_File size is zero_")
        P(FileNotOpened,"_Can't open file_")
        P(NoMemory,"_No memory for processing_")
        P(BadKey,"_Unknown key in command line_")
 }
#undef P
}

int c_key,p_key,b_key,d_key;
char ProcessFile[257],OutputFile[257];

void main(int ArgsNum, char **ArgsValue) {
int number;
int32 ErrorCode,i,j;
char *p;
enum ErrorsVariants Err;

 c_key=0; p_key=0; b_key=0; *ProcessFile=0;
 d_key=0;
 Err=NoErr;
 for (i=1; i<ArgsNum; i++) {
     p=ArgsValue[i];
     if (*p=='-')
        if (strlen(p)>=2)
           for (j=1;j<strlen(p);j++) {
               switch (p[j]) {
                      case 'c': case 'C': c_key=1; break;
                      case 'p': case 'P': p_key=1; break;
                      case 'b': case 'B':
                        number=0;
                        while (++j<strlen(p))
                         if (!isdigit(p[j])) break;
                          else number=number*10+(int)(p[j]-'0');
                        j--;
                        if (number<1 || number>127)
                           Err=BufSizeWrong;
                         else b_key=number;
                        break;
                      case 'd': case 'D': d_key=1; break;
                      default: Err=BadKey;
               }
               if (Err!=NoErr) break;
           }
         else Err=UnknownAction;
      else {
            if (*ProcessFile) Err=UnknownAction;
             else strcpy(ProcessFile,p);
           }
     if (Err!=NoErr) break;
 }
 if (Err!=NoErr) { ExitWithError(Err); return; }
 if (d_key==1 && (p_key==1 || b_key!=0)) {
    ExitWithError(UnknownAction);
    return;
 }

 if ((c_key || p_key || b_key || d_key) && !*ProcessFile) {
    ExitWithError(NoProcessFile);
    return;
 }
 if (!c_key && !p_key && !b_key && !d_key && !*ProcessFile) {
    printf("\n"
    "Experimental Compression Program. (c) 2000 by Michael Semikov\n"
    "Version 0.experimental.slow\n\n"
    "use: ecp [ [-c] { [-p] [-bNNN] file_to_compress | -d file_to_decompress} ]\n"
    "\nThis program is one-file archiver and also it has some keys:\n"
    "    -c - Write data to STDOUT\n"
    "    -p - Compress and use preprocess stage.\n"
    "         Sometimes \"-p\" can improve compression,\n"
    "         especially on so called \"water\" data\n"
    "    -b{1 .. 127} - Use block size of N*100 KBytes on compression stage\n"
    "                   this option also improves compression ratio\n"
    "                   By default this key equal to 3\n"
    "    -d - Uncompress archive\n\n\n"
    "Warning! You use this program at own risk!\n"
    );
    return;
 }
 if (!d_key && !b_key) b_key=3;
 if (c_key && isatty(fileno(stdout))) {
    ExitWithError(BadStdout);
    return;
 }
#ifdef __WINDOWS_COMPILATION
 if (c_key)
    setmode(fileno(stdout),O_BINARY);
#endif
 if (!d_key) {
    strcpy(OutputFile,ProcessFile);
    strcat(OutputFile,".ecp");
    ErrorCode=CompressFile(ProcessFile, OutputFile, (uint8)p_key,
                           (uint8)b_key, (uint8)c_key);
    if (!ErrorCode && !c_key) {
       float ss1,ss2;FILE *fpn;
       fpn=fopen(ProcessFile,"rb");
       ss1=(float)filesize(fpn);
       fclose(fpn);
       fpn=fopen(OutputFile,"rb");
       ss2=(float)filesize(fpn);
       fclose(fpn);
       ss1=8.0f*(ss2/ss1);
       fprintf(stderr,"\nFile \"%s\" was compressed\nThe %7f bits per symbol ratio was obtained\n",
             ProcessFile,ss1);
    }
 } else {
       ErrorCode=DeCompressFile(ProcessFile,(uint8)c_key);
       if (!ErrorCode)
          fprintf(stderr,"\nArchive file \"%s\" was successfully processed\n",
                ProcessFile);
   }
 switch (ErrorCode) {
        case 1: Err=ZeroFileSize; break;
        case 2: Err=FileNotOpened; break;
        case 3: Err=NoMemory;
 }
 if (ErrorCode) ExitWithError(Err);
}
