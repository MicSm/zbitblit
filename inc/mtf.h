#pragma once

#include <array>
#include <cstdint>

namespace zbb {

// Move-to-front rank list kept as a singly linked chain over byte values.
struct MtfEncoder
{
    std::array<std::uint16_t, 256> links{};
    std::uint16_t head = 0;

    void reset();
    [[nodiscard]] std::uint16_t encode(std::uint8_t value);
};

// Inverse move-to-front list kept as a dense value-by-rank array.
struct MtfDecoder
{
    std::array<std::uint8_t, 256> order{};

    void reset();
    [[nodiscard]] std::uint8_t decode(std::uint8_t rank);
};

} // namespace zbb
