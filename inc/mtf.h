#pragma once

#include <cstdint>

namespace zbb {

struct MtfState
{
    union
    {
        std::uint16_t MtfLinks[256];
        std::uint8_t DeMtfArray[256];
    } data;

    std::uint16_t HeadPtr;
};

void MtfSetup(MtfState& state);

void DeMtfSetup(MtfState& state);

[[nodiscard]] std::uint16_t GetMtfValue(MtfState& state, std::uint16_t InValue);

[[nodiscard]] std::uint8_t GetByMtfPosition(MtfState& state, std::uint8_t Position);

} // namespace zbb
