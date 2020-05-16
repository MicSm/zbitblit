#ifndef __mio_h
#define __mio_h

#include <stdlib.h>
#include <stdio.h>
#include "mtypes.h"

/* defines buffered i/o */
#define B_I_O


int32 filesize(FILE *tmp__) {
int32 tmpSize__;
 fseek(tmp__, 0L, SEEK_END);
 tmpSize__=ftell(tmp__);
 fseek(tmp__, 0L, SEEK_SET);
 return tmpSize__;
}

#define SBUFF 4096

typedef struct  {
      FILE *file;       /* for stream I/O */
      uint32  rbuf;   /* read bit buffer */
      uint8  rcnt;       /* read bit count */
      uint32  wbuf;   /* write bit buffer */
      uint8  wcnt;       /* write bit count */
#ifdef B_I_O
      uint8 buff[SBUFF];
#endif
} bfile;

bfile *bfopen_as_stdout(void) {
bfile *bf;

      bf = (bfile *)malloc(sizeof(bfile));
      if (NULL == bf)
            return NULL;
      bf->file = stdout;
#ifdef B_I_O
      setvbuf(bf->file,(char *)bf->buff,_IOFBF,SBUFF);
#endif
      bf->rcnt = 0;
      bf->wcnt = 0;
      return bf;
}

bfile *bfopen(char *name, char *mode) {
bfile *bf;

      bf = (bfile *)malloc(sizeof(bfile));
      if (NULL == bf)
            return NULL;
      bf->file = fopen(name, mode);
      if (NULL == bf->file) {
         free(bf);
         return NULL;
      }
#ifdef B_I_O
      setvbuf(bf->file,(char *)bf->buff,_IOFBF,SBUFF);
#endif
      bf->rcnt = 0;
      bf->wcnt = 0;
      return bf;
}

uint8 bfread(bfile *bf) {
      if (0 == bf->rcnt) {
         bf->rbuf=0;
         bf->rbuf |= ((uint32)((uint8)fgetc(bf->file)))<<24;
         bf->rbuf |= ((uint32)((uint8)fgetc(bf->file)))<<16;
         bf->rbuf |= ((uint32)((uint8)fgetc(bf->file)))<<8;
         bf->rbuf |= (uint32)((uint8)fgetc(bf->file));
         bf->rcnt = 32;
      }
      bf->rcnt--;
      return (bf->rbuf & (1UL << bf->rcnt)) != 0;
}

void bfwrite(uint8 bit, bfile *bf) {
      if (32 == bf->wcnt) {
         fputc((uint8)(((bf->wbuf)>>24)&0xff), bf->file);
         fputc((uint8)(((bf->wbuf)>>16)&0xff), bf->file);
         fputc((uint8)(((bf->wbuf)>>8)&0xff), bf->file);
         fputc((uint8)((bf->wbuf)&0xff), bf->file);
         bf->wcnt = 0;
      }
      bf->wcnt++;
      bf->wbuf <<= 1;
      bf->wbuf |= (uint32)bit;
}

uint8 arr[4];

void w_bfclose(bfile *bf) {
      bf->wbuf <<= 32-bf->wcnt;
      arr[0]=((bf->wbuf)>>24)&0xff;
      arr[1]=((bf->wbuf)>>16)&0xff;
      arr[2]=((bf->wbuf)>>8)&0xff;
      arr[3]=(bf->wbuf)&0xff;
      fwrite(arr,((bf->wcnt)>>3)+((((bf->wcnt)&0x07)!=0)?1:0),1,bf->file);
#ifdef B_I_O
      fflush(bf->file);
#endif
      fclose(bf->file);
      free(bf);
}

void w_bfclose_as_stdout(bfile *bf) {
      bf->wbuf <<= 32-bf->wcnt;
      arr[0]=((bf->wbuf)>>24)&0xff;
      arr[1]=((bf->wbuf)>>16)&0xff;
      arr[2]=((bf->wbuf)>>8)&0xff;
      arr[3]=(bf->wbuf)&0xff;
      fwrite(arr,((bf->wcnt)>>3)+((((bf->wcnt)&0x07)!=0)?1:0),1,bf->file);
#ifdef B_I_O
      fflush(bf->file);
#endif
      free(bf);
}

void r_bfclose_as_stdout(bfile *bf) {
#ifdef B_I_O
      fflush(bf->file);
#endif
      free(bf);
}


void r_bfclose(bfile *bf) {
#ifdef B_I_O
      fflush(bf->file);
#endif
      fclose(bf->file);
      free(bf);
}

#endif
