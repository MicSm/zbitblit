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
};

std::uint32_t LZP_PREPROCESS(LzpTables& tables, std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength);

std::uint32_t UnPreprocess(LzpTables& tables, std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength);

} // namespace zbb
