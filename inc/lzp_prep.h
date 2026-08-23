#pragma once

#include <cstdint>
#include <vector>

namespace zbb {

struct LzpTables
{
    std::vector<std::uint8_t*> ht4;
    std::vector<std::uint8_t*> ht5;

    void ensure();
    void clear();
    std::uint32_t preprocess(std::uint8_t* in_data, std::uint8_t* out_data, std::uint32_t in_length);
    std::uint32_t unpreprocess(std::uint8_t* in_data, std::uint8_t* out_data, std::uint32_t in_length);
};

} // namespace zbb
