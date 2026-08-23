/* In this module you can find preprocessor algo, based on LZP (c) by
   Charles Bloom. This preprocessor especially useful on so called
   'water' data, and also it can help improve BWT speed.
*/

#include "inc/lzp_prep.h"

#include <algorithm>

namespace zbb {
namespace {

constexpr std::size_t HTSIZE4 = 65536;
constexpr std::size_t HTSIZE5 = 32768;

constexpr std::uint32_t LowerLimit = 38;

std::uint32_t hash4(std::uint32_t index, const std::uint8_t* ptr)
{
    std::uint32_t x = (static_cast<std::uint32_t>(ptr[index - 4]) << 24)
        | (static_cast<std::uint32_t>(ptr[index - 3]) << 16) | (static_cast<std::uint32_t>(ptr[index - 2]) << 8)
        | (static_cast<std::uint32_t>(ptr[index - 1]));
    x = (x >> 15) ^ x ^ (x >> 3);
    return x & (HTSIZE4 - 1);
}

std::uint32_t hash5(std::uint32_t index, const std::uint8_t* ptr)
{
    std::uint32_t x = (static_cast<std::uint32_t>(ptr[index - 4]) << 24)
        | (static_cast<std::uint32_t>(ptr[index - 3]) << 16) | (static_cast<std::uint32_t>(ptr[index - 2]) << 8)
        | (static_cast<std::uint32_t>(ptr[index - 1]));
    x = (x >> 25 | static_cast<std::uint32_t>(ptr[index - 5]) << 7) ^ x ^ (x << 4);
    return x & (HTSIZE5 - 1);
}

void put_length(std::uint32_t length, std::span<std::uint8_t> output, std::uint32_t& cursor)
{
    while (length > 254)
    {
        output[cursor++] = 255;
        length -= 255;
    }
    output[cursor++] = static_cast<std::uint8_t>(length);
}

std::optional<std::uint32_t> get_length(std::span<const std::uint8_t> input, std::uint32_t& cursor)
{
    std::uint32_t result = 0;
    for (;;)
    {
        // boffin: refused reading a length run past the end of the decoded block
        if (cursor >= input.size())
        {
            return std::nullopt;
        }
        const std::uint8_t byte = input[cursor++];
        result += byte;
        if (byte != 255)
        {
            return result;
        }
    }
}

} // namespace

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

std::uint32_t LzpTables::preprocess(std::span<const std::uint8_t> input, std::span<std::uint8_t> output)
{
    const std::uint8_t* const in_data = input.data();
    const auto in_length = static_cast<std::uint32_t>(input.size());

    std::copy_n(in_data, 5, output.data());
    std::uint32_t out_length = 5;
    std::uint32_t pointer = 5;

    ht4[hash4(4, in_data)] = in_data + 4;
    while (pointer < in_length)
    {
        const std::uint32_t hash_index4 = hash4(pointer, in_data);
        const std::uint32_t hash_index5 = hash5(pointer, in_data);

        const std::uint8_t* const pointer4 = ht4[hash_index4];
        const std::uint8_t* const pointer5 = ht5[hash_index5];

        ht5[hash_index5] = ht4[hash_index4] = in_data + pointer;

        if (pointer5 != nullptr || pointer4 != nullptr)
        {
            const std::uint8_t* past = pointer5 != nullptr ? pointer5 : pointer4;
            std::uint32_t common_length = 0;

            while (pointer < in_length)
            {
                if (in_data[pointer] != *past)
                {
                    break;
                }
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
            {
                put_length(common_length - LowerLimit + 256u, output, out_length);
            }
            else
            {
                put_length(static_cast<std::uint32_t>(in_data[pointer++]), output, out_length);
            }
        }
        else
        {
            output[out_length++] = in_data[pointer++];
        }
    }
    return out_length;
}

std::optional<std::uint32_t> LzpTables::unpreprocess(
    std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output)
{
    if (input.size() < 5)
    {
        return std::nullopt;
    }

    std::uint8_t* const out_data = output.data();
    const auto in_length = static_cast<std::uint32_t>(input.size());
    const auto out_capacity = static_cast<std::uint32_t>(output.size());

    std::copy_n(input.data(), 5, out_data);
    std::uint32_t out_length = 5;
    std::uint32_t pointer = 5;

    ht4[hash4(4, out_data)] = out_data + 4;
    while (pointer < in_length)
    {
        const std::uint32_t hash_index4 = hash4(out_length, out_data);
        const std::uint32_t hash_index5 = hash5(out_length, out_data);

        const std::uint8_t* const pointer4 = ht4[hash_index4];
        const std::uint8_t* const pointer5 = ht5[hash_index5];

        ht5[hash_index5] = ht4[hash_index4] = out_data + out_length;

        if (pointer5 != nullptr || pointer4 != nullptr)
        {
            const std::uint8_t* past = pointer5 != nullptr ? pointer5 : pointer4;
            const std::optional<std::uint32_t> token = get_length(input, pointer);
            if (!token.has_value())
            {
                return std::nullopt;
            }
            std::uint32_t common_length = *token;
            if (common_length < 256)
            {
                if (out_length >= out_capacity)
                {
                    return std::nullopt;
                }
                out_data[out_length++] = static_cast<std::uint8_t>(common_length);
            }
            else
            {
                common_length = common_length - 256 + LowerLimit;
                // boffin: refused expanding a match run past the decode buffer
                if (common_length > out_capacity - out_length)
                {
                    return std::nullopt;
                }
                while (common_length--)
                {
                    out_data[out_length++] = *past++;
                }
            }
        }
        else
        {
            if (out_length >= out_capacity)
            {
                return std::nullopt;
            }
            out_data[out_length++] = input[pointer++];
        }
    }
    return out_length;
}

} // namespace zbb
