#pragma once

#include <cstdint>

namespace zbb {

[[nodiscard]] int CreateHashTables();

void DestructHashTables();

void CleanHashTables();

std::uint32_t LZP_PREPROCESS(std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength);

std::uint32_t UnPreprocess(std::uint8_t* InData, std::uint8_t* OutData, std::uint32_t InLength);

} // namespace zbb
