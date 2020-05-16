#include "mtypes.h"

uint8 ArcIdentifier[12]={0x55,0x2e,0x3d,0xa5,43,54,34,72,11,22,15,65};

typedef struct {
         char FileName[256]; /* name of compressed file */
         uint32 UncompressedLen; /* uncompressed length of file */
         uint8 SystemFlag; /* [b8 b7 b6 b5 b4 b3 b2 b1 b0
                               |  |-----------+---------|
                            preprocessing     |
                               on/off         number of size of block */
} CompressedHeader;
