#include "inc/codec.h"

#include "inc/byte_order.h"

#include <array>
#include <cstdint>

namespace zbb {
namespace {

constexpr std::array<std::uint32_t, 25> k_pyramid_table = {
    0,        2,        6,         14,        30,        62,        126,       254,      510,
    1022,     2046,     4094,      8190,      16382,     32766,     65534,     131070,   262142,
    524286,   1048574,  2097150,   4194302,   8388606,   16777214,  33554430};

void encode_be32(bfile* file, ArithCodingContext& ctx, ArithStream& stream, std::uint32_t value)
{
    for (const std::uint8_t byte : be32_bytes(value))
    {
        EncodeChar(static_cast<std::int16_t>(byte), file, &ctx, stream);
    }
}

[[nodiscard]] std::uint32_t decode_be32(bfile* file, ArithCodingContext& ctx, ArithStream& stream)
{
    std::array<std::uint8_t, 4> bytes{};
    for (std::uint8_t& byte : bytes)
    {
        byte = static_cast<std::uint8_t>(DecodeChar(file, &ctx, stream));
    }
    return u32_from_be32(bytes);
}

void encode_zero_run(std::uint32_t zeroes_count, bfile* out, ArithCodingContext& ctx, ArithStream& stream)
{
    std::uint32_t lhs = 0;
    std::uint32_t rhs = 24;
    while (lhs != rhs)
    {
        const std::uint32_t mid = (lhs + rhs) / 2;
        if (zeroes_count > k_pyramid_table[mid])
        {
            lhs = mid + 1;
        }
        else
        {
            rhs = mid;
        }
    }

    if (k_pyramid_table[lhs] == zeroes_count)
    {
        ++lhs;
    }

    zeroes_count -= k_pyramid_table[lhs - 1];
    do
    {
        EncodeChar(static_cast<std::int16_t>(zeroes_count & 1), out, &ctx, stream);
        zeroes_count >>= 1;
    } while (--lhs);
}

void encode_mtf_block(
    std::span<const std::uint8_t> bwt_in,
    std::span<const std::uint32_t> idxs,
    bfile* out,
    ArithCodingContext& ctx,
    ArithStream& stream,
    MtfState& mtf)
{
    const auto len_out = static_cast<std::uint32_t>(bwt_in.size());
    std::uint32_t j = 0;
    while (j < len_out)
    {
        std::uint16_t mtf_value = 0;
        std::uint32_t zeroes_count = 0;
        while (j < len_out &&
               (mtf_value = GetMtfValue(mtf, bwt_in[((len_out + idxs[j]) - 3) % len_out])) == 0)
        {
            ++zeroes_count;
            ++j;
        }

        if (zeroes_count-- > 0)
        {
            encode_zero_run(zeroes_count, out, ctx, stream);
        }

        if (mtf_value != 0)
        {
            EncodeChar(static_cast<std::int16_t>(mtf_value + 1), out, &ctx, stream);
        }
        ++j;
    }
    EncodeChar(257, out, &ctx, stream);
}

void encode_bwt_block(std::span<std::uint8_t> bwt_in, std::span<std::uint32_t> idxs, bfile* out, BlockWorkspace& ws)
{
    const auto len_out = static_cast<std::uint32_t>(bwt_in.size());
    const std::uint32_t primary_index = BWT_TRANSFORM(ws.bwt, len_out, bwt_in.data(), idxs.data());
    EncodeChar(1, out, &ws.flag_ctx, ws.arith);
    encode_be32(out, ws.flag_ctx, ws.arith, primary_index);
    encode_mtf_block(
        std::span<const std::uint8_t>{bwt_in.data(), len_out},
        std::span<const std::uint32_t>{idxs.data(), len_out},
        out,
        ws.mtf_ctx,
        ws.arith,
        ws.mtf);
}

void encode_raw_block(
    std::span<const std::uint8_t> data,
    bfile* out,
    ArithCodingContext& flag_ctx,
    ArithStream& stream)
{
    EncodeChar(0, out, &flag_ctx, stream);
    for (const std::uint8_t byte : data)
    {
        EncodeChar(static_cast<std::int16_t>(byte), out, &flag_ctx, stream);
    }
    EncodeChar(256, out, &flag_ctx, stream);
}

[[nodiscard]] bool emit_mtf_zeroes(
    std::span<std::uint8_t> output,
    std::uint32_t& len_out,
    std::uint32_t count,
    MtfState& mtf)
{
    // boffin: refused wrapping remaining capacity when the fill cursor is already past the buffer
    if (len_out > output.size() || count > output.size() - len_out)
    {
        return false;
    }
    while (count-- != 0)
    {
        output[len_out++] = GetByMtfPosition(mtf, 0);
    }
    return true;
}

[[nodiscard]] bool decode_mtf_block(
    bfile* in,
    ArithCodingContext& ctx,
    ArithStream& stream,
    MtfState& mtf,
    std::span<std::uint8_t> output,
    std::uint32_t& len_out)
{
    std::uint32_t tmp_sum = 0;
    std::uint32_t j = 1;
    std::uint32_t num_zeroes = 0;
    std::uint16_t nx_val = 0;
    len_out = 0;
    while ((nx_val = static_cast<std::uint16_t>(DecodeChar(in, &ctx, stream))) != 257)
    {
        if (nx_val < 2)
        {
            if (num_zeroes >= 32)
            {
                return false;
            }
            if (nx_val != 0)
            {
                tmp_sum |= j;
            }
            j <<= 1;
            ++num_zeroes;
        }
        else
        {
            if (num_zeroes > 0)
            {
                // boffin: refused expanding a zero-run past the block buffer or shifting a 32-bit count
                if (num_zeroes >= 32)
                {
                    return false;
                }
                tmp_sum += (1u << num_zeroes) - 1u;
                if (!emit_mtf_zeroes(output, len_out, tmp_sum, mtf))
                {
                    return false;
                }
                tmp_sum = 0;
                j = 1;
                num_zeroes = 0;
            }
            if (len_out >= output.size())
            {
                return false;
            }
            output[len_out++] =
                static_cast<std::uint8_t>(GetByMtfPosition(mtf, static_cast<std::uint8_t>(nx_val - 1)));
        }
    }

    if (num_zeroes > 0)
    {
        if (num_zeroes >= 32)
        {
            return false;
        }
        tmp_sum += (1u << num_zeroes) - 1u;
        if (!emit_mtf_zeroes(output, len_out, tmp_sum, mtf))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decode_raw_block(
    bfile* in,
    ArithCodingContext& ctx,
    ArithStream& stream,
    std::span<std::uint8_t> output,
    std::uint32_t& len_out)
{
    std::uint16_t nx_val = 0;
    len_out = 0;
    while ((nx_val = static_cast<std::uint16_t>(DecodeChar(in, &ctx, stream))) != 256)
    {
        if (len_out >= output.size())
        {
            return false;
        }
        output[len_out++] = static_cast<std::uint8_t>(nx_val);
    }
    return true;
}

} // namespace

void encode_payload_block(std::span<std::uint8_t> raw, BlockWorkspace& ws, bfile* out, bool preprocess)
{
    const auto raw_len = static_cast<std::uint32_t>(raw.size());
    std::uint32_t len_out = raw_len;
    std::uint8_t* transformed = raw.data();

    if (preprocess && raw_len >= k_lzp_len_threshold)
    {
        ws.lzp.clear();
        len_out = LZP_PREPROCESS(ws.lzp, raw.data(), ws.back.data(), raw_len);
        transformed = ws.back.data();
    }

    // boffin: kept raw, BWT, and LZP as separate outcomes instead of one shared block path
    if (len_out >= k_bwt_len_threshold)
    {
        encode_bwt_block(
            std::span<std::uint8_t>{transformed, len_out},
            std::span<std::uint32_t>{ws.idxs.data(), ws.idxs.size()},
            out,
            ws);
    }
    else
    {
        encode_raw_block(std::span<const std::uint8_t>{transformed, len_out}, out, ws.flag_ctx, ws.arith);
    }
}

ProcessError decode_transformed_block(
    bfile* in,
    BlockWorkspace& ws,
    std::uint8_t*& decoded,
    std::uint32_t& len_out)
{
    const std::int16_t kind = DecodeChar(in, &ws.flag_ctx, ws.arith);
    if (kind == 1)
    {
        const std::uint32_t string_pos = decode_be32(in, ws.flag_ctx, ws.arith);
        if (!decode_mtf_block(in, ws.mtf_ctx, ws.arith, ws.mtf, ws.back, len_out))
        {
            return ProcessError::bad_archive;
        }
        if (len_out < k_bwt_len_threshold || string_pos >= len_out)
        {
            return ProcessError::bad_archive;
        }
        UnBWT(ws.bwt, string_pos, len_out, ws.back.data(), ws.front.data(), ws.idxs.data());
        decoded = ws.front.data();
    }
    else if (kind == 0)
    {
        if (!decode_raw_block(in, ws.flag_ctx, ws.arith, ws.back, len_out))
        {
            return ProcessError::bad_archive;
        }
        if (len_out >= k_bwt_len_threshold)
        {
            return ProcessError::bad_archive;
        }
        decoded = ws.back.data();
    }
    else
    {
        // boffin: refused treating an unknown block-kind symbol as a raw payload
        return ProcessError::bad_archive;
    }
    return ProcessError::none;
}

ProcessError finish_decoded_block(
    bool preprocess,
    std::uint32_t expected_len,
    std::uint8_t*& ready,
    std::uint32_t& len_out,
    BlockWorkspace& ws)
{
    if (preprocess && expected_len >= k_lzp_len_threshold)
    {
        ws.lzp.clear();
        std::uint8_t* const dest = (ready == ws.front.data()) ? ws.back.data() : ws.front.data();
        len_out = UnPreprocess(ws.lzp, ready, dest, len_out);
        ready = dest;
    }

    // boffin: refused writing a reconstructed chunk whose length is not the archive's remaining block
    if (len_out != expected_len)
    {
        return ProcessError::bad_archive;
    }
    return ProcessError::none;
}

} // namespace zbb
