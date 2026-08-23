#include "inc/arithm.h"

namespace zbb {
/********** Adaptive Arithmetic Compression **********/

/*  If you are not familiar with arithmetic compression, you should read
        I. E. Witten, R. M. Neal, and J. G. Cleary,
            Communications of the ACM, Vol. 30, pp. 520-540 (1987),
    from which much have been borrowed.  */

constexpr int M = 15;

/*
    Q1 (= 2 to the M) must be sufficiently large, but not so
       large as the unsigned long 4 * Q1 * (Q1 - 1) overflows.
*/

constexpr std::uint32_t Q1 = 1u << M;
constexpr std::uint32_t Q2 = 2 * Q1;
constexpr std::uint32_t Q3 = 3 * Q1;
constexpr std::uint32_t Q4 = 4 * Q1;
constexpr std::uint32_t MAX_CUM = Q1 - 1;

void ArithCodingContext::setup(std::int16_t new_alphabet_size)
{
    alphabet_size = new_alphabet_size;

    cum_freq[alphabet_size] = 0;
    /* define start distribution */
    for (std::int16_t sym = alphabet_size; sym >= 1; sym--)
    {
        const std::int16_t ch = static_cast<std::int16_t>(sym - 1);
        char_to_sym[ch] = sym;
        sym_to_char[sym] = ch;
        freq[sym] = 1; /* here empirical frequency */
        cum_freq[sym - 1] = static_cast<std::uint16_t>(cum_freq[sym] + freq[sym]);
    }
    /* sentinel (!= freq[1]) */
    freq[0] = 0;
}

void ArithCodingContext::update(std::int16_t sym)
{
    if (cum_freq[0] >= MAX_CUM)
    { /* if overflow, scale down frequencies */
        std::uint16_t c = 0;
        for (std::int16_t i = alphabet_size; i > 0; i--)
        {
            cum_freq[i] = c;
            c = static_cast<std::uint16_t>(c + (freq[i] = static_cast<std::uint16_t>((freq[i] + 1) >> 1)));
        }
        cum_freq[0] = c;
    }

    std::int16_t i = sym;
    while (freq[i] == freq[i - 1])
    {
        i--;
    }
    if (i < sym)
    {
        const std::int16_t ch_i = sym_to_char[i];
        const std::int16_t ch_sym = sym_to_char[sym];
        sym_to_char[i] = ch_sym;
        sym_to_char[sym] = ch_i;
        char_to_sym[ch_i] = sym;
        char_to_sym[ch_sym] = i;
    }
    freq[i]++;
    while (--i >= 0)
    {
        cum_freq[i]++;
    }
}

std::int16_t ArithCodingContext::find_symbol(std::uint16_t scaled_value) const
{
    std::int16_t i = 1;
    std::int16_t j = alphabet_size;
    while (i < j)
    {
        const std::int16_t k = static_cast<std::int16_t>((i + j) >> 1);
        if (cum_freq[k] > scaled_value)
        {
            i = static_cast<std::int16_t>(k + 1);
        }
        else
        {
            j = k;
        }
    }
    return i;
}

void ArithEncoder::start()
{
    // boffin: refused carrying coder range state from a previous stream
    low = 0;
    high = Q4;
    pending_bits = 0;
}

void ArithEncoder::emit(std::uint8_t bit, BitWriter& out)
{
    out.write_bit(bit != 0);
    for (; pending_bits > 0; pending_bits--)
    {
        out.write_bit(bit != 1);
    }
}

void ArithEncoder::encode(std::int16_t ch, ArithCodingContext& ctx, BitWriter& out)
{
    const std::int16_t sym = ctx.char_to_sym[ch];
    const std::uint32_t range = high - low;

    high = low + (range * ctx.cum_freq[sym - 1]) / ctx.cum_freq[0];
    low += (range * ctx.cum_freq[sym]) / ctx.cum_freq[0];

    for (;;)
    {
        if (high <= Q2)
        {
            emit(0, out);
        }
        else if (low >= Q2)
        {
            emit(1, out);
            low -= Q2;
            high -= Q2;
        }
        else if (low >= Q1 && high <= Q3)
        {
            pending_bits++;
            low -= Q1;
            high -= Q1;
        }
        else
        {
            break;
        }
        low <<= 1;
        high <<= 1;
    }
    ctx.update(sym);
}

void ArithEncoder::finish(BitWriter& out)
{
    pending_bits++;
    emit(low < Q1 ? 0 : 1, out);
}

void ArithDecoder::start(BitReader& in)
{
    low = 0;
    high = Q4;
    value = 0;
    for (int i = 0; i < M + 2; i++)
    {
        value = 2 * value + in.read_bit();
    }
}

std::int16_t ArithDecoder::decode(ArithCodingContext& ctx, BitReader& in)
{
    const std::uint32_t range = high - low;
    const std::int16_t sym =
        ctx.find_symbol(static_cast<std::uint16_t>(((value - low + 1) * ctx.cum_freq[0] - 1) / range));

    high = low + (range * ctx.cum_freq[sym - 1]) / ctx.cum_freq[0];
    low += (range * ctx.cum_freq[sym]) / ctx.cum_freq[0];

    for (;;)
    {
        if (low >= Q2)
        {
            value -= Q2;
            low -= Q2;
            high -= Q2;
        }
        else if (low >= Q1 && high <= Q3)
        {
            value -= Q1;
            low -= Q1;
            high -= Q1;
        }
        else if (high > Q2)
        {
            break;
        }
        low <<= 1;
        high <<= 1;
        value = 2 * value + in.read_bit();
    }
    const std::int16_t ch = ctx.sym_to_char[sym];
    ctx.update(sym);

    return ch;
}

} // namespace zbb
