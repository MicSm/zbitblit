/* In this module you can find preprocessor algo, based on LZP (c) by
   Charles Bloom. This preprocessor especially useful on so called
   'water' data, and also it can help improve BWT speed.
*/

#include "inc/lzp_prep.h"

#include <algorithm>

namespace zbb {

constexpr std::size_t HTSIZE4 = 65536;
constexpr std::size_t HTSIZE5 = 32768;

void LzpTables::ensure()
{
    ht4.resize(HTSIZE4);
    ht5.resize(HTSIZE5);
}

void LzpTables::clear()
{
    std::fill(ht4.begin(), ht4.end(), nullptr);
    std::fill(ht5.begin(), ht5.end(), nullptr);
}

static std::uint32_t HashFunction4(std::uint32_t index, const std::uint8_t* ptr)
{
    std::uint32_t x = (static_cast<std::uint32_t>(ptr[index - 4]) << 24)
        | (static_cast<std::uint32_t>(ptr[index - 3]) << 16) | (static_cast<std::uint32_t>(ptr[index - 2]) << 8)
        | (static_cast<std::uint32_t>(ptr[index - 1]));
    x = (x >> 15) ^ x ^ (x >> 3);
    return x & (HTSIZE4 - 1);
}

static std::uint32_t HashFunction5(std::uint32_t index, const std::uint8_t* ptr)
{
    std::uint32_t x = (static_cast<std::uint32_t>(ptr[index - 4]) << 24)
        | (static_cast<std::uint32_t>(ptr[index - 3]) << 16) | (static_cast<std::uint32_t>(ptr[index - 2]) << 8)
        | (static_cast<std::uint32_t>(ptr[index - 1]));
    x = (x >> 25 | static_cast<std::uint32_t>(ptr[index - 5]) << 7) ^ x ^ (x << 4);
    return x & (HTSIZE5 - 1);
}

constexpr std::uint32_t LowerLimit = 38;

static void OutPutLength(std::uint32_t length, std::uint8_t* out_buffer, std::uint32_t* cursor)
{
    while (length > 254)
    {
        out_buffer[(*cursor)++] = 255;
        length -= 255;
    }
    out_buffer[(*cursor)++] = static_cast<std::uint8_t>(length);
}

static std::uint32_t GetLength(const std::uint8_t* input_buffer, std::uint32_t* cursor)
{
    std::uint32_t result = 0;
    while (result += static_cast<std::uint32_t>(input_buffer[*cursor]), input_buffer[(*cursor)++] == 255)
        ;
    return result;
}

std::uint32_t LZP_PREPROCESS(LzpTables& tables, std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength)
{
    std::copy_n(InData, 5, OutData);
    std::uint32_t out_length = 5;
    std::uint32_t pointer = 5;

    tables.ht4[HashFunction4(4, InData)] = InData + 4;
    while (pointer < InLength)
    {
        const std::uint32_t hash_index4 = HashFunction4(pointer, InData);
        const std::uint32_t hash_index5 = HashFunction5(pointer, InData);

        std::uint8_t* const pointer4 = tables.ht4[hash_index4];
        std::uint8_t* const pointer5 = tables.ht5[hash_index5];

        tables.ht5[hash_index5] = tables.ht4[hash_index4] = InData + pointer;

        if (pointer5 != nullptr || pointer4 != nullptr)
        {
            std::uint8_t* past = pointer5 != nullptr ? pointer5 : pointer4;
            std::uint32_t common_length = 0;

            while (pointer < InLength)
            {
                if (InData[pointer] != *past)
                    break;
                pointer++;
                past++;
                common_length++;
            }
            if (common_length > 0 && common_length < LowerLimit)
            {
                pointer -= common_length;
                common_length = 0;
            }
            if (common_length)
                OutPutLength(common_length - LowerLimit + 256UL, OutData, &out_length);
            else
                OutPutLength(static_cast<std::uint32_t>(InData[pointer++]), OutData, &out_length);
        }
        else
            OutData[out_length++] = InData[pointer++];
    }
    return out_length;
}

std::uint32_t UnPreprocess(LzpTables& tables, std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength)
{
    std::copy_n(InData, 5, OutData);
    std::uint32_t out_length = 5;
    std::uint32_t pointer = 5;

    tables.ht4[HashFunction4(4, OutData)] = OutData + 4;
    while (pointer < InLength)
    {
        const std::uint32_t hash_index4 = HashFunction4(out_length, OutData);
        const std::uint32_t hash_index5 = HashFunction5(out_length, OutData);

        std::uint8_t* const pointer4 = tables.ht4[hash_index4];
        std::uint8_t* const pointer5 = tables.ht5[hash_index5];

        tables.ht5[hash_index5] = tables.ht4[hash_index4] = OutData + out_length;

        if (pointer5 != nullptr || pointer4 != nullptr)
        {
            std::uint8_t* past = pointer5 != nullptr ? pointer5 : pointer4;
            std::uint32_t common_length = GetLength(InData, &pointer);
            if (common_length < 256)
                OutData[out_length++] = static_cast<std::uint8_t>(common_length);
            else
            {
                common_length = common_length - 256 + LowerLimit;
                while (common_length--)
                    OutData[out_length++] = *past++;
            }
        }
        else
            OutData[out_length++] = InData[pointer++];
    }
    return out_length;
}

} // namespace zbb
