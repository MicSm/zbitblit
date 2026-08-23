#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace zbb {

struct LzpTables
{
    std::vector<const std::uint8_t*> ht4;
    std::vector<const std::uint8_t*> ht5;

    void ensure();
    void clear();

    /* output must hold at least twice input.size(); returns the produced length */
    [[nodiscard]] std::uint32_t preprocess(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);

    /* input is untrusted archive data; nullopt means the stream is malformed */
    [[nodiscard]] std::optional<std::uint32_t> unpreprocess(
        std::span<const std::uint8_t> input,
        std::span<std::uint8_t> output);
};

} // namespace zbb
