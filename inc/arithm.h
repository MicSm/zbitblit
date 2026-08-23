#pragma once

#include "inc/mio.h"

#include <array>
#include <cstdint>

namespace zbb {

/* character code = 0, 1, ..., k_max_alphabet - 1 */
inline constexpr std::int16_t k_max_alphabet = 260;

// Adaptive order-0 model shared by the encoder and the decoder; both sides
// must feed it the same symbol sequence for the bitstream to stay in sync.
struct ArithCodingContext
{
    std::int16_t alphabet_size = 0;
    std::array<std::int16_t, k_max_alphabet> char_to_sym{};
    std::array<std::int16_t, k_max_alphabet + 1> sym_to_char{};
    std::array<std::uint16_t, k_max_alphabet + 1> freq{};
    std::array<std::uint16_t, k_max_alphabet + 1> cum_freq{};

    void setup(std::int16_t new_alphabet_size);
    void update(std::int16_t sym);
    [[nodiscard]] std::int16_t find_symbol(std::uint16_t scaled_value) const;
};

struct ArithEncoder
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::int16_t pending_bits = 0;

    void start();
    void encode(std::int16_t ch, ArithCodingContext& ctx, BitWriter& out);
    void finish(BitWriter& out);

private:
    void emit(std::uint8_t bit, BitWriter& out);
};

struct ArithDecoder
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint32_t value = 0;

    void start(BitReader& in);
    [[nodiscard]] std::int16_t decode(ArithCodingContext& ctx, BitReader& in);
};

} // namespace zbb
