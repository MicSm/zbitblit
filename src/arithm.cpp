#include "inc/arithm.h"

#include <cassert>

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

constexpr std::uint32_t Q1 = 1UL << M;
constexpr std::uint32_t Q2 = 2 * Q1;
constexpr std::uint32_t Q3 = 3 * Q1;
constexpr std::uint32_t Q4 = 4 * Q1;
constexpr std::uint32_t MAX_CUM = Q1 - 1;

void ArithStream::start_encode()
{
    // boffin: refused carrying coder range state from a previous stream
    low = 0;
    high = Q4;
    value = 0;
    shifts = 0;
}

void ArithCodingContext::setup(std::int16_t new_alphabet_size)
{
    std::int16_t ch, sym;

    assert(new_alphabet_size <= MAX_ALPHABET_SIZE);
    alphabet_size = new_alphabet_size;

    scf[alphabet_size] = 0;
    /* define start distribution */
    for (sym = alphabet_size; sym >= 1; sym--)
    {
        ch = sym - 1;
        c2s[ch] = sym;
        s2c[sym] = ch;
        sf[sym] = 1; /* here empirical frequency */
        scf[sym - 1] = scf[sym] + sf[sym];
    }
    /* sentinel (!= sf[1]) */
    sf[0] = 0;
}

static void UpdateModel(int sym, ArithCodingContext* ptr)
{
    std::int16_t i, c, ch_i, ch_sym;

    if (ptr->scf[0] >= MAX_CUM)
    { /* if overflow */
        c = 0;
        /* scale down frequencies */
        for (i = ptr->alphabet_size; i > 0; i--)
        {
            ptr->scf[i] = c;
            c += (ptr->sf[i] = (ptr->sf[i] + 1) >> 1);
        }
        ptr->scf[0] = c;
    }
    for (i = static_cast<std::int16_t>(sym); ptr->sf[i] == ptr->sf[i - 1]; i--)
        ;
    if (i < sym)
    {
        ch_i = ptr->s2c[i];
        ch_sym = ptr->s2c[sym];
        ptr->s2c[i] = ch_sym;
        ptr->s2c[sym] = ch_i;
        ptr->c2s[ch_i] = static_cast<std::int16_t>(sym);
        ptr->c2s[ch_sym] = i;
    }
    ptr->sf[i]++;
    while (--i >= 0)
        ptr->scf[i]++;
}

static void Output(ArithStream& stream, std::uint8_t bit, BitFile& file)
{
    file.write_bit(bit != 0);
    for (; stream.shifts > 0; stream.shifts--)
        file.write_bit(bit != 1);
}

void ArithStream::encode_char(std::int16_t ch, BitFile& file, ArithCodingContext& ctx)
{
    const std::int16_t sym = ctx.c2s[ch];
    const std::uint32_t range = high - low;

    high = low + (range * ctx.scf[sym - 1]) / ctx.scf[0];
    low += (range * ctx.scf[sym]) / ctx.scf[0];

    for (;;)
    {
        if (high <= Q2)
            Output(*this, 0, file);
        else if (low >= Q2)
        {
            Output(*this, 1, file);
            low -= Q2;
            high -= Q2;
        }
        else if (low >= Q1 && high <= Q3)
        {
            shifts++;
            low -= Q1;
            high -= Q1;
        }
        else
            break;
        low <<= 1;
        high <<= 1;
    }
    UpdateModel(sym, &ctx);
}

void ArithStream::finish_encode(BitFile& file)
{
    shifts++;
    Output(*this, low < Q1 ? 0 : 1, file);
}

static std::int16_t BinarySearchSym(std::uint16_t x, ArithCodingContext* ptr)
{
    std::int16_t i = 1, j = ptr->alphabet_size;

    while (i < j)
    {
        const std::int16_t k = static_cast<std::int16_t>((i + j) >> 1);
        if (ptr->scf[k] > x)
            i = static_cast<std::int16_t>(k + 1);
        else
            j = k;
    }
    return i;
}

void ArithStream::start_decode(BitFile& file)
{
    start_encode();
    for (std::int16_t i = 0; i < M + 2; i++)
        value = 2 * value + file.read_bit();
}

std::int16_t ArithStream::decode_char(BitFile& file, ArithCodingContext& ctx)
{
    const std::uint32_t range = high - low;
    const std::int16_t sym = BinarySearchSym(
        static_cast<std::uint16_t>(((value - low + 1) * ctx.scf[0] - 1) / range), &ctx);

    high = low + (range * ctx.scf[sym - 1]) / ctx.scf[0];
    low += (range * ctx.scf[sym]) / ctx.scf[0];

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
            break;
        low <<= 1;
        high <<= 1;
        value = 2 * value + file.read_bit();
    }
    const std::int16_t ch = ctx.s2c[sym];
    UpdateModel(sym, &ctx);

    return ch;
}

void ArithCoder::setup_models()
{
    mtf.setup(258);
    flag.setup(257);
}

void ArithCoder::start_encode()
{
    stream.start_encode();
}

void ArithCoder::start_decode(BitFile& file)
{
    stream.start_decode(file);
}

void ArithCoder::finish_encode(BitFile& file)
{
    stream.finish_encode(file);
}

} // namespace zbb
