/* In this module you can find preprocessor algo, based on LZP (c) by
   Charles Bloom. This preprocessor especially useful on so called
   'water' data, and also it can help improve BWT speed.
*/

#include "inc/lzp_prep.h"

#include <cstring>
#include <memory>
#include <new>

namespace zbb {

constexpr std::size_t HTSIZE4 = 65536;
constexpr std::size_t HTSIZE5 = 32768;

static std::unique_ptr<std::uint8_t*[]> HashTable4;
static std::unique_ptr<std::uint8_t*[]> HashTable5;

int CreateHashTables()
{
    HashTable4.reset(new (std::nothrow) std::uint8_t*[HTSIZE4]);
    if (!HashTable4)
    {
        return 0;
    }

    HashTable5.reset(new (std::nothrow) std::uint8_t*[HTSIZE5]);
    if (!HashTable5)
    {
        HashTable4.reset();
        return 0;
    }
    return 1;
}

void DestructHashTables()
{
    HashTable4.reset();
    HashTable5.reset();
}

void CleanHashTables()
{
    std::memset(HashTable4.get(), 0, HTSIZE4 * sizeof(std::uint8_t*));
    std::memset(HashTable5.get(), 0, HTSIZE5 * sizeof(std::uint8_t*));
}

static std::uint32_t HashFunction4(std::uint32_t index, std::uint8_t* PTR)
{
    std::uint32_t x;

    x = (static_cast<std::uint32_t>(PTR[index - 4]) << 24) | (static_cast<std::uint32_t>(PTR[index - 3]) << 16)
        | (static_cast<std::uint32_t>(PTR[index - 2]) << 8) | (static_cast<std::uint32_t>(PTR[index - 1]));
    x = (x >> 15) ^ x ^ (x >> 3);
    return x & (HTSIZE4 - 1);
}

static std::uint32_t HashFunction5(std::uint32_t index, std::uint8_t* PTR)
{
    std::uint32_t x;

    x = (static_cast<std::uint32_t>(PTR[index - 4]) << 24) | (static_cast<std::uint32_t>(PTR[index - 3]) << 16)
        | (static_cast<std::uint32_t>(PTR[index - 2]) << 8) | (static_cast<std::uint32_t>(PTR[index - 1]));
    x = (x >> 25 | static_cast<std::uint32_t>(PTR[index - 5]) << 7) ^ x ^ (x << 4);
    return x & (HTSIZE5 - 1);
}

/* You can modify this const and get better compression */
constexpr std::uint32_t LowerLimit = 38;

static void OutPutLength(std::uint32_t OutPutLength, std::uint8_t* OutBuffer, std::uint32_t* PBuffer)
{
    while (OutPutLength > 254)
    {
        OutBuffer[(*PBuffer)++] = 255;
        OutPutLength -= 255;
    }
    OutBuffer[(*PBuffer)++] = static_cast<std::uint8_t>(OutPutLength);
}

static std::uint32_t GetLength(std::uint8_t* InputBuffer, std::uint32_t* PBuff)
{
    std::uint32_t Result = 0;
    while (Result += static_cast<std::uint32_t>(InputBuffer[*PBuff]), InputBuffer[(*PBuff)++] == 255)
        ;
    return Result;
}

std::uint32_t LZP_PREPROCESS(std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength)
{
    std::uint32_t i, CommonLength;
    std::uint32_t OutLength;
    std::uint32_t HashIndex4, HashIndex5;
    std::uint8_t* Pointer4;
    std::uint8_t* Pointer5;
    std::uint32_t Pointer;
    std::uint8_t* PastPointer;

    /* send 5 bytes to output */
    OutLength = 0;
    for (i = 0; i < 5; i++)
        OutData[i] = InData[i];
    OutLength += 5;

    /* begin preprocessing */

    Pointer = 5;

    HashTable4[HashFunction4(4, InData)] = InData + 4;
    while (Pointer < InLength)
    {
        /* get hash adresses */

        HashIndex4 = HashFunction4(Pointer, InData);
        HashIndex5 = HashFunction5(Pointer, InData);

        Pointer4 = HashTable4[HashIndex4];
        Pointer5 = HashTable5[HashIndex5];

        HashTable5[HashIndex5] = HashTable4[HashIndex4] = InData + Pointer;

        if (Pointer5 != nullptr || Pointer4 != nullptr)
        {
            if (Pointer5 != nullptr)
                PastPointer = Pointer5;
            else
                PastPointer = Pointer4;

            CommonLength = 0;

            while (Pointer < InLength)
            {
                if (InData[Pointer] != *PastPointer)
                    break;
                Pointer++;
                PastPointer++;
                CommonLength++;
            }
            if (CommonLength > 0 && CommonLength < LowerLimit)
            {
                Pointer -= CommonLength;
                CommonLength = 0;
            }
            if (CommonLength)
                OutPutLength(CommonLength - LowerLimit + 256UL, OutData, &OutLength);

            else
                OutPutLength(static_cast<std::uint32_t>(InData[Pointer++]), OutData, &OutLength);
        }
        else
            OutData[OutLength++] = InData[Pointer++];
    }
    return OutLength;
}

std::uint32_t UnPreprocess(std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength)
{
    std::uint32_t i, CommonLength;
    std::uint32_t OutLength;
    std::uint32_t HashIndex4, HashIndex5;
    std::uint8_t* Pointer4;
    std::uint8_t* Pointer5;
    std::uint32_t Pointer;
    std::uint8_t* PastPointer;

    /* send 5 bytes to output */
    OutLength = 0;
    for (i = 0; i < 5; i++)
        OutData[i] = InData[i];
    OutLength += 5;

    /* begin unpreprocessing */

    Pointer = 5;

    HashTable4[HashFunction4(4, OutData)] = OutData + 4;
    while (Pointer < InLength)
    {
        /* get hash adresses */

        HashIndex4 = HashFunction4(OutLength, OutData);
        HashIndex5 = HashFunction5(OutLength, OutData);

        Pointer4 = HashTable4[HashIndex4];
        Pointer5 = HashTable5[HashIndex5];

        HashTable5[HashIndex5] = HashTable4[HashIndex4] = OutData + OutLength;

        if (Pointer5 != nullptr || Pointer4 != nullptr)
        {
            if (Pointer5 != nullptr)
                PastPointer = Pointer5;
            else
                PastPointer = Pointer4;
            CommonLength = GetLength(InData, &Pointer);
            if (CommonLength < 256)
                OutData[OutLength++] = static_cast<std::uint8_t>(CommonLength);
            else
            {
                CommonLength = CommonLength - 256 + LowerLimit;
                while (CommonLength--)
                    OutData[OutLength++] = *PastPointer++;
            }
        }
        else
            OutData[OutLength++] = InData[Pointer++];
    }
    return OutLength;
}

} // namespace zbb
