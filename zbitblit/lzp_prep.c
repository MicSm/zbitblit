/* In this module you can find preprocessor algo, based on LZP (c) by
   Charles Bloom. This preprocessor especially useful on so called
   'water' data, and also it can help improve BWT speed.
*/

#include <stdlib.h>
#include "mtypes.h"

uint8 **HashTable4, **HashTable5;
#define HTSIZE4 65536UL
#define HTSIZE5 32768UL

uint32 HashFunction4(register uint32 index, register uint8 *PTR) {
register uint32 x;

 x=(((uint32)PTR[index-4])<<24) | (((uint32)PTR[index-3])<<16) |
   (((uint32)PTR[index-2])<<8) | (((uint32)PTR[index-1]));
 x=(x>>15)^x^(x>>3);
 return x&(HTSIZE4-1);
}

uint32 HashFunction5(register uint32 index, register uint8 *PTR) {
register uint32 x;

 x=(((uint32)PTR[index-4])<<24) | (((uint32)PTR[index-3])<<16) |
   (((uint32)PTR[index-2]))<<8 | (((uint32)PTR[index-1]));
 x=(x>>25 | ((uint32)PTR[index-5])<<7)^x^(x<<4);
 return x&(HTSIZE5-1);
}

/* You can modify this const and get better compression */
#define LowerLimit 38

void OutPutLength(register uint32 OutPutLength,
                         register uint8 *OutBuffer,
                         register uint32 *PBuffer) {
 while (OutPutLength>254) {
       OutBuffer[(*PBuffer)++]=255;
       OutPutLength-=255;
 }
 OutBuffer[(*PBuffer)++]=(uint8)OutPutLength;
}

uint32 GetLength(register uint8 *InputBuffer,
                        register uint32 *PBuff) {
register uint32 Result;

 Result=0;
 while (Result+=(uint32)InputBuffer[*PBuff],InputBuffer[(*PBuff)++]==255);
 return Result;
}

/* clean hash tables */
void CleanTabs(void) {
uint32 i;

 for (i=0;i<HTSIZE4;i++) HashTable4[i]=NULL;
 for (i=0;i<HTSIZE5;i++) HashTable5[i]=NULL;
}

uint32 LZP_PREPROCESS(uint8 *InData, uint8 *OutData, uint32 InLength) {
uint32 i,CommonLength;
uint32 OutLength;
register uint32 HashIndex4,HashIndex5;
register uint8 *Pointer4, *Pointer5;
uint32 Pointer;
register uint8 *PastPointer;

 /* send 5 bytes to output */
 OutLength=0;
 for (i=0; i<5; i++) OutData[i]=InData[i];
 OutLength+=5;

 /* begin preprocessing */

 Pointer=5;

 HashTable4[HashFunction4(4,InData)]=InData+4;
 while (Pointer<InLength) {
       /* get hash adresses */

       HashIndex4=HashFunction4(Pointer,InData);
       HashIndex5=HashFunction5(Pointer,InData);

       Pointer4=HashTable4[HashIndex4];
       Pointer5=HashTable5[HashIndex5];

       HashTable5[HashIndex5]=HashTable4[HashIndex4]=InData+Pointer;

       if (Pointer5!=NULL || Pointer4!=NULL) {
          if (Pointer5!=NULL) PastPointer=Pointer5;
           else PastPointer=Pointer4;

          CommonLength=0;

          while (Pointer<InLength) {
                if (InData[Pointer]!=*PastPointer) break;
                Pointer++;
                PastPointer++;
                CommonLength++;
          }
          if (CommonLength>0 && CommonLength<LowerLimit) {
             Pointer-=CommonLength;
             CommonLength=0;
          }
          if (CommonLength)
             OutPutLength(CommonLength-LowerLimit+256UL, OutData,
                          &OutLength);

           else
             OutPutLength((uint32)InData[Pointer++], OutData, &OutLength);

       } else  OutData[OutLength++]=InData[Pointer++];
 }
 return OutLength;
}

uint32 UnPreprocess(uint8 *InData, uint8 *OutData, uint32 InLength) {
uint32 i,CommonLength;
uint32 OutLength;
register uint32 HashIndex4,HashIndex5;
register uint8 *Pointer4, *Pointer5;
uint32 Pointer;
register uint8 *PastPointer;

 /* send 5 bytes to output */
 OutLength=0;
 for (i=0; i<5; i++) OutData[i]=InData[i];
 OutLength+=5;

 /* begin unpreprocessing */

 Pointer=5;

 HashTable4[HashFunction4(4,OutData)]=OutData+4;
 while (Pointer<InLength) {
       /* get hash adresses */

       HashIndex4=HashFunction4(OutLength,OutData);
       HashIndex5=HashFunction5(OutLength,OutData);

       Pointer4=HashTable4[HashIndex4];
       Pointer5=HashTable5[HashIndex5];

       HashTable5[HashIndex5]=HashTable4[HashIndex4]=OutData+OutLength;

       if (Pointer5!=NULL || Pointer4!=NULL) {
          if (Pointer5!=NULL) PastPointer=Pointer5;
           else PastPointer=Pointer4;
          CommonLength=GetLength(InData,&Pointer);
          if (CommonLength<256)
             OutData[OutLength++]=(uint8)CommonLength;
           else {
             CommonLength=CommonLength-256+LowerLimit;
             while (CommonLength--)
                   OutData[OutLength++]=*PastPointer++;
           }
       } else  OutData[OutLength++]=InData[Pointer++];
 }
 return OutLength;
}
