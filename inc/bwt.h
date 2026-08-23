#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace zbb {

struct BwtWorkspace
{
    std::vector<std::uint32_t> sbkt;
    std::vector<std::uint32_t> sbm;
    std::array<std::uint32_t, 256> v{};
    std::uint32_t scan_len = 0;
    std::uint8_t* scan_buf = nullptr;

    void ensure();
};

std::uint32_t BWT_TRANSFORM(BwtWorkspace& ws, std::uint32_t len, std::uint8_t* pb, std::uint32_t* idxs);

void UnBWT(
    BwtWorkspace& ws,
    std::uint32_t StrPos,
    std::uint32_t len,
    std::uint8_t* InputBuffer,
    std::uint8_t* OutputBuffer,
    std::uint32_t* idxs);

} // namespace zbb
