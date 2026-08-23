#pragma once

#include "inc/mio.h"

#include <array>
#include <cstdint>

namespace zbb {

/* character code = 0, 1, ..., MAX_ALPHABET_SIZE - 1 */
constexpr std::int16_t MAX_ALPHABET_SIZE = 260;

struct ArithCodingContext
{
    std::int16_t alphabet_size = 0;
    std::array<std::int16_t, MAX_ALPHABET_SIZE> c2s{};
    std::array<std::int16_t, MAX_ALPHABET_SIZE + 1> s2c{};
    std::array<std::uint16_t, MAX_ALPHABET_SIZE + 1> sf{};
    std::array<std::uint16_t, MAX_ALPHABET_SIZE + 1> scf{};
};

struct ArithStream
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint32_t value = 0;
    std::int16_t shifts = 0;
};

void SetupContext(ArithCodingContext* ctx, std::int16_t alphabet_size);

void StartEncode(ArithStream& stream);

void EncodeChar(std::int16_t ch, bfile* bin_file, ArithCodingContext* ctx, ArithStream& stream);

[[nodiscard]] std::int16_t DecodeChar(bfile* bin_file, ArithCodingContext* ctx, ArithStream& stream);

void FinishEncode(ArithStream& stream, bfile* fil);

void StartDecode(ArithStream& stream, bfile* fil);

} // namespace zbb
