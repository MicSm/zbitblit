#pragma once

#include <cstdint>

namespace zbb {

[[nodiscard]] int SetupBwtBuffers();

void FreeBwtBuffers();

std::uint32_t BWT_TRANSFORM(std::uint32_t len, std::uint8_t* pb, std::uint32_t* idxs);

void UnBWT(
    std::uint32_t StrPos,
    std::uint32_t len,
    std::uint8_t* InputBuffer,
    std::uint8_t* OutputBuffer,
    std::uint32_t* idxs);

} // namespace zbb
