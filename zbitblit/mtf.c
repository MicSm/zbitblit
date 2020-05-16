#include "mtypes.h"

uint16 MtfLinks[256];
uint16 HeadPtr;

uint8 *DeMtfArray;

void MtfSetup(void) {
uint16 i;
 HeadPtr=0;
 for (i=0;i<256;i++)
     MtfLinks[i]=(uint16)(i+1);
}

void DeMtfSetup(void) {
uint16 i;
 DeMtfArray=(uint8 *)MtfLinks;
 for (i=0;i<256;i++) DeMtfArray[i]=(uint8)i;
}

uint16 GetMtfValue(register uint16 InValue) {
register uint16 SkippedNums, p, PredPtr;

 SkippedNums=0;
 p=HeadPtr;
 while (p!=InValue) {
       PredPtr=p;
       p=MtfLinks[p];
       SkippedNums++;
 }
 if (SkippedNums) {
    MtfLinks[PredPtr]=MtfLinks[p];
    MtfLinks[p]=HeadPtr;
    HeadPtr=p;
 }
 return SkippedNums;
}

uint8 GetByMtfPosition(register uint8 Position) {
uint8 Result;
register uint8 i;

 Result=DeMtfArray[Position];
 if (Position!=0) {
    for (i=Position;i>0;i--) DeMtfArray[i]=DeMtfArray[i-1];
    DeMtfArray[0]=Result;
 }
 return Result;
}
