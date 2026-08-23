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

void StartEncode(ArithStream& stream)
{
    // boffin: refused carrying coder range state from a previous stream
    stream.low = 0;
    stream.high = Q4;
    stream.value = 0;
    stream.shifts = 0;
}

void SetupContext(ArithCodingContext* ctx, std::int16_t alphabet_size)
{
    std::int16_t ch, sym;

    assert(alphabet_size <= MAX_ALPHABET_SIZE);
    ctx->alphabet_size = alphabet_size;

    ctx->scf[alphabet_size] = 0;
    /* define start distribution */
    for (sym = alphabet_size; sym >= 1; sym--)
    {
        ch = sym - 1;
        ctx->c2s[ch] = sym;
        ctx->s2c[sym] = ch;
        ctx->sf[sym] = 1; /* here empirical frequency */
        ctx->scf[sym - 1] = ctx->scf[sym] + ctx->sf[sym];
    }
    /* sentinel (!= sf[1]) */
    ctx->sf[0] = 0;
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

static void Output(ArithStream& stream, std::uint8_t bit, bfile* fil)
{
    bfwrite(bit != 0, fil);
    for (; stream.shifts > 0; stream.shifts--)
        bfwrite(bit != 1, fil);
}

void EncodeChar(std::int16_t ch, bfile* bin_file, ArithCodingContext* ctx, ArithStream& stream)
{
    const std::int16_t sym = ctx->c2s[ch];
    const std::uint32_t range = stream.high - stream.low;

    stream.high = stream.low + (range * ctx->scf[sym - 1]) / ctx->scf[0];
    stream.low += (range * ctx->scf[sym]) / ctx->scf[0];

    for (;;)
    {
        if (stream.high <= Q2)
            Output(stream, 0, bin_file);
        else if (stream.low >= Q2)
        {
            Output(stream, 1, bin_file);
            stream.low -= Q2;
            stream.high -= Q2;
        }
        else if (stream.low >= Q1 && stream.high <= Q3)
        {
            stream.shifts++;
            stream.low -= Q1;
            stream.high -= Q1;
        }
        else
            break;
        stream.low <<= 1;
        stream.high <<= 1;
    }
    UpdateModel(sym, ctx);
}

void FinishEncode(ArithStream& stream, bfile* fil)
{
    stream.shifts++;
    Output(stream, stream.low < Q1 ? 0 : 1, fil);
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

void StartDecode(ArithStream& stream, bfile* fil)
{
    StartEncode(stream);
    for (std::int16_t i = 0; i < M + 2; i++)
        stream.value = 2 * stream.value + bfread(fil);
}

std::int16_t DecodeChar(bfile* bin_file, ArithCodingContext* ctx, ArithStream& stream)
{
    const std::uint32_t range = stream.high - stream.low;
    const std::int16_t sym = BinarySearchSym(
        static_cast<std::uint16_t>(((stream.value - stream.low + 1) * ctx->scf[0] - 1) / range), ctx);

    stream.high = stream.low + (range * ctx->scf[sym - 1]) / ctx->scf[0];
    stream.low += (range * ctx->scf[sym]) / ctx->scf[0];

    for (;;)
    {
        if (stream.low >= Q2)
        {
            stream.value -= Q2;
            stream.low -= Q2;
            stream.high -= Q2;
        }
        else if (stream.low >= Q1 && stream.high <= Q3)
        {
            stream.value -= Q1;
            stream.low -= Q1;
            stream.high -= Q1;
        }
        else if (stream.high > Q2)
            break;
        stream.low <<= 1;
        stream.high <<= 1;
        stream.value = 2 * stream.value + bfread(bin_file);
    }
    const std::int16_t ch = ctx->s2c[sym];
    UpdateModel(sym, ctx);

    return ch;
}

} // namespace zbb
