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

    void setup(std::int16_t new_alphabet_size);
};

struct ArithStream
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint32_t value = 0;
    std::int16_t shifts = 0;

    void start_encode();
    void start_decode(BitFile& file);
    void encode_char(std::int16_t ch, BitFile& file, ArithCodingContext& ctx);
    [[nodiscard]] std::int16_t decode_char(BitFile& file, ArithCodingContext& ctx);
    void finish_encode(BitFile& file);
};

struct ArithCoder
{
    ArithStream stream{};
    ArithCodingContext flag{};
    ArithCodingContext mtf{};

    void setup_models();
    void start_encode();
    void start_decode(BitFile& file);
    void finish_encode(BitFile& file);
};

} // namespace zbb
