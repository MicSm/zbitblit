#include "inc/mtf.h"

namespace zbb {

void MtfSetup(MtfState& state)
{
    state.HeadPtr = 0;
    for (std::uint16_t i = 0; i < 256; i++)
    {
        state.data.MtfLinks[i] = static_cast<std::uint16_t>(i + 1);
    }
}

void DeMtfSetup(MtfState& state)
{
    for (std::uint16_t i = 0; i < 256; i++)
    {
        state.data.DeMtfArray[i] = static_cast<std::uint8_t>(i);
    }
}

std::uint16_t GetMtfValue(MtfState& state, std::uint16_t InValue)
{
    std::uint16_t skipped = 0;
    std::uint16_t pred = 0;
    std::uint16_t p = state.HeadPtr;
    while (p != InValue)
    {
        pred = p;
        p = state.data.MtfLinks[p];
        skipped++;
    }
    if (skipped != 0)
    {
        state.data.MtfLinks[pred] = state.data.MtfLinks[p];
        state.data.MtfLinks[p] = state.HeadPtr;
        state.HeadPtr = p;
    }
    return skipped;
}

std::uint8_t GetByMtfPosition(MtfState& state, std::uint8_t Position)
{
    const std::uint8_t result = state.data.DeMtfArray[Position];

    if (Position != 0)
    {
        for (std::uint8_t i = Position; i > 0; i--)
        {
            state.data.DeMtfArray[i] = state.data.DeMtfArray[i - 1];
        }
        state.data.DeMtfArray[0] = result;
    }
    return result;
}

} // namespace zbb
