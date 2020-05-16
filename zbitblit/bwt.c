/*
   The algorithm in this module is almost identical to used in bzip, (c) by
   Julian Seward. The next version of <ecp> will include some original
   algorithm and probably reprocessed lexicographic ordering, found in
   bzip.
*/

#include <stdio.h>
#include "mtypes.h"
#include "sort3.c"

uint32 *SBck,*SBm;
uint32 *idxs;
uint32 v[256];
uint8 ord[256];

uint32 ScanLen;
uint8 *ScanBuf;

#define C_B ((uint32)0x7fffffff)
#define M_B ((uint32)0x80000000)

int SetupBwtBuffers(void) {

 SBck=(uint32 *)malloc((uint32)65537*(uint32)sizeof(uint32));
 SBm=(uint32 *)malloc((uint32)65536*(uint32)sizeof(uint32));

 if (SBck!=NULL && SBm!=NULL) return 1;

 if (SBck!=NULL) free(SBck);
 if (SBm!=NULL) free(SBm);
 return 0;
}

void FreeBwtBuffers(void) {

 free(SBck);
 free(SBm);
}

int fcmp1(uint8 a, uint8 b) {
uint32 elemL=v[a],elemR=v[b];

 if (elemL<elemR) return -1;
  else if (elemL>elemR) return 1;
 return 0;
}

int fcmp2(uint32 a, uint32 b) {
register uint32 c;

 c=ScanLen;
 while (ScanBuf[a]==ScanBuf[b] && c) {
       if (++a==(ScanLen+2)) a=0;
       if (++b==(ScanLen+2)) b=0;
       c--;
 }
 if (c)
    if (ScanBuf[a]<ScanBuf[b])
       return -1;
    else return 1;
 return 0;
}

uint32 BWT_TRANSFORM(uint32 len, uint8 *pb) {
register uint32 i,ptr;
register uint32 i1,j,k;
uint8 mask[256],st_mask[256];

register uint16 vtmp0;

 ScanLen=len-2;
 ScanBuf=pb;
 /* clean buckets */
 for (i=0; i<65537; i++) SBck[i]=0;

 /* calculate buckets */
 for (i=0; i<len-1; i++)
     SBck[(((uint16)pb[i])<<8) | (uint16)pb[i+1]]++;
 SBck[(((uint16)pb[len-1])<<8) | (uint16)pb[0]]++;

 /* fill indexes according to the buckets */
 for (i=0x0001;i<0x10001;i++) SBck[i]+=SBck[i-1];

 for (i=0; i<len-2; i++)
     idxs[--SBck[(((uint16)pb[i])<<8) | (uint16)pb[i+1]]]=i+2;
 idxs[--SBck[(((uint16)pb[len-2])<<8) | (uint16)pb[len-1]]]=0;
 idxs[--SBck[(((uint16)pb[len-1])<<8) | (uint16)pb[0]]]=1;

 /* sort buckets on value criteria */
 for (i=0;i<256;i++) {
     v[i]=SBck[(i+1)<<8]-SBck[i<<8];
     ord[i]=(uint8)i;
 }
 qsort1(ord, 256, fcmp1);

 /* now, begin sort buckets */
 for (i=0;i<256;i++) mask[i]=0;
 for (i=0;i<65536;i++) SBm[i]=0;
 for (i=0;i<256;i++) {
     i1=ord[i];
     if ((SBck[(i1+1)<<8]&C_B)-(SBck[i1<<8]&C_B)>1)
        /* sort this big bucket */
        for (j=0;j<256;j++) {
            k=(i1<<8)|j;
            if (((SBck[k+1]&C_B)-(SBck[k]&C_B)>1) && !(SBck[k]&M_B)) {
               qsort4(&idxs[SBck[k]], (SBck[k+1]&C_B)-SBck[k],
               fcmp2);
            }
        }
     /* setup sorted order for small buckets */
     ptr=0;
     for (j=SBck[i1<<8]&C_B; j<(SBck[(i1+1)<<8]&C_B); j++) {
         k=((idxs[j]>=3)?idxs[j]-3:len+idxs[j]-3);
         if (!mask[pb[k]]) {
            mask[pb[k]]=1;
            st_mask[ptr++]=pb[k];
         }
         vtmp0=(((uint16)pb[k])<<8)|(uint16)i1;
         idxs[(SBck[vtmp0]&C_B)+SBm[vtmp0]]=
         ((idxs[j]>=1)?idxs[j]-1:len+idxs[j]-1);
         SBm[vtmp0]++;
     }
     while (ptr) {
           mask[st_mask[--ptr]]=0;
           SBm[(((uint16)st_mask[ptr])<<8) | i1]=0;
           SBck[(((uint16)st_mask[ptr])<<8) | i1]|=M_B;
     }
 }
 /* find source string position */
 j=(((uint16)pb[0])<<8)|((uint16)pb[1]);
 i=SBck[j]&C_B;
 j=(SBck[j+1]&C_B);
 while (i<j && idxs[i]!=2) i++;
 return i;
}

void UnBWT(register uint32 StrPos, uint32 len,
          uint8 *InputBuffer, uint8 *OutputBuffer) {

 register int32 i;
 /* clean buffer */
 for (i=0;i<256;i++)
     v[i]=0;

 /* calc stat */
 for (i=0;i<len;i++)
     v[InputBuffer[i]]++;
 for (i=1;i<256;i++)
     v[i]+=v[i-1];
 for (i=len-1;i>=0;i--)
     idxs[--v[InputBuffer[i]]]=i;

 /* do reverse BWT transform */
 for (i=0;i<len;i++)
     OutputBuffer[i]=InputBuffer[StrPos=idxs[StrPos]];
}
