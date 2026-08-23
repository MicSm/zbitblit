#include "inc/mtf.h"

namespace zbb {

void MtfState::setup()
{
    HeadPtr = 0;
    for (std::uint16_t i = 0; i < 256; i++)
    {
        data.MtfLinks[i] = static_cast<std::uint16_t>(i + 1);
    }
}

void MtfState::setup_decode()
{
    for (std::uint16_t i = 0; i < 256; i++)
    {
        data.DeMtfArray[i] = static_cast<std::uint8_t>(i);
    }
}

std::uint16_t MtfState::get_value(std::uint16_t in_value)
{
    std::uint16_t skipped = 0;
    std::uint16_t pred = 0;
    std::uint16_t p = HeadPtr;
    while (p != in_value)
    {
        pred = p;
        p = data.MtfLinks[p];
        skipped++;
    }
    if (skipped != 0)
    {
        data.MtfLinks[pred] = data.MtfLinks[p];
        data.MtfLinks[p] = HeadPtr;
        HeadPtr = p;
    }
    return skipped;
}

std::uint8_t MtfState::by_position(std::uint8_t position)
{
    const std::uint8_t result = data.DeMtfArray[position];

    if (position != 0)
    {
        for (std::uint8_t i = position; i > 0; i--)
        {
            data.DeMtfArray[i] = data.DeMtfArray[i - 1];
        }
        data.DeMtfArray[0] = result;
    }
    return result;
}

} // namespace zbb
