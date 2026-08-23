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

    std::uint16_t HeadPtr = 0;

    void setup();
    void setup_decode();
    [[nodiscard]] std::uint16_t get_value(std::uint16_t in_value);
    [[nodiscard]] std::uint8_t by_position(std::uint8_t position);
};

} // namespace zbb
