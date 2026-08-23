#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace zbb {

struct BwtWorkspace
{
    std::vector<std::uint32_t> sbkt;
    std::vector<std::uint32_t> sbm;
    std::array<std::uint32_t, 256> v{};

    void ensure();
    [[nodiscard]] std::uint32_t transform(std::span<std::uint8_t> data, std::span<std::uint32_t> idxs);
    void unbwt(
        std::uint32_t str_pos,
        std::span<const std::uint8_t> input,
        std::span<std::uint8_t> output,
        std::span<std::uint32_t> idxs);
};

} // namespace zbb
