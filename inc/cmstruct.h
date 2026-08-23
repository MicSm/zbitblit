#pragma once

#include <array>
#include <cstdint>

namespace zbb {

inline constexpr std::array<std::uint8_t, 12> ArcIdentifier{
    0x55, 0x2e, 0x3d, 0xa5, 43, 54, 34, 72, 11, 22, 15, 65};

struct CompressedHeader
{
    char FileName[256]; /* name of compressed file */
    std::uint32_t UncompressedLen; /* uncompressed length of file */
    std::uint8_t SystemFlag; /* bit7 = LZP on/off; bits0-6 = block-size code */
};

} // namespace zbb
