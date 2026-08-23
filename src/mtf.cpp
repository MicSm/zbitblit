#include "inc/mtf.h"

namespace zbb {

void MtfEncoder::reset()
{
    head = 0;
    for (std::uint16_t i = 0; i < 256; i++)
    {
        links[i] = static_cast<std::uint16_t>(i + 1);
    }
}

std::uint16_t MtfEncoder::encode(std::uint8_t value)
{
    std::uint16_t skipped = 0;
    std::uint16_t pred = 0;
    std::uint16_t p = head;
    while (p != value)
    {
        pred = p;
        p = links[p];
        skipped++;
    }
    if (skipped != 0)
    {
        links[pred] = links[p];
        links[p] = head;
        head = p;
    }
    return skipped;
}

void MtfDecoder::reset()
{
    for (std::uint16_t i = 0; i < 256; i++)
    {
        order[i] = static_cast<std::uint8_t>(i);
    }
}

std::uint8_t MtfDecoder::decode(std::uint8_t rank)
{
    const std::uint8_t result = order[rank];

    if (rank != 0)
    {
        for (std::uint8_t i = rank; i > 0; i--)
        {
            order[i] = order[i - 1];
        }
        order[0] = result;
    }
    return result;
}

} // namespace zbb
