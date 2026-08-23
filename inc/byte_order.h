#pragma once

#include <array>
#include <cstdint>

namespace zbb {

[[nodiscard]] constexpr std::array<std::uint8_t, 4> be32_bytes(std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>(value >> 24),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value)};
}

[[nodiscard]] constexpr std::uint32_t u32_from_be32(std::array<std::uint8_t, 4> bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
        | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

} // namespace zbb
