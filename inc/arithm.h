#pragma once

#include "inc/mio.h"

#include <cstdint>

namespace zbb {

/* character code = 0, 1, ..., MAX_ALPHABET_SIZE - 1 */
constexpr std::int16_t MAX_ALPHABET_SIZE = 260;

struct ArithCodingContext
{
    std::int16_t alphabet_size;
    std::int16_t c2s[MAX_ALPHABET_SIZE];
    std::int16_t s2c[MAX_ALPHABET_SIZE + 1];
    std::uint16_t sf[MAX_ALPHABET_SIZE + 1];
    std::uint16_t scf[MAX_ALPHABET_SIZE + 1];
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
