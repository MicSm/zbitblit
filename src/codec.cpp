#include "inc/workspace.h"

#include "inc/byte_order.h"

#include <array>
#include <cstdint>

namespace zbb {
namespace {

constexpr std::array<std::uint32_t, 25> k_pyramid_table = {
    0,        2,        6,         14,        30,        62,        126,       254,      510,
    1022,     2046,     4094,      8190,      16382,     32766,     65534,     131070,   262142,
    524286,   1048574,  2097150,   4194302,   8388606,   16777214,  33554430};

void encode_be32(BitWriter& out, ArithCodingContext& ctx, ArithEncoder& enc, std::uint32_t value)
{
    for (const std::uint8_t byte : be32_bytes(value))
    {
        enc.encode(static_cast<std::int16_t>(byte), ctx, out);
    }
}

[[nodiscard]] std::uint32_t decode_be32(BitReader& in, ArithCodingContext& ctx, ArithDecoder& dec)
{
    std::array<std::uint8_t, 4> bytes{};
    for (std::uint8_t& byte : bytes)
    {
        byte = static_cast<std::uint8_t>(dec.decode(ctx, in));
    }
    return u32_from_be32(bytes);
}

void encode_zero_run(std::uint32_t zeroes_count, BitWriter& out, ArithCodingContext& ctx, ArithEncoder& enc)
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
        enc.encode(static_cast<std::int16_t>(zeroes_count & 1), ctx, out);
        zeroes_count >>= 1;
    } while (--lhs);
}

void encode_mtf_block(
    std::span<const std::uint8_t> bwt_in,
    std::span<const std::uint32_t> idxs,
    BitWriter& out,
    ArithCodingContext& ctx,
    ArithEncoder& enc,
    MtfEncoder& mtf)
{
    const auto len_out = static_cast<std::uint32_t>(bwt_in.size());
    std::uint32_t j = 0;
    while (j < len_out)
    {
        std::uint16_t mtf_value = 0;
        std::uint32_t zeroes_count = 0;
        while (j < len_out &&
               (mtf_value = mtf.encode(bwt_in[((len_out + idxs[j]) - 3) % len_out])) == 0)
        {
            ++zeroes_count;
            ++j;
        }

        if (zeroes_count-- > 0)
        {
            encode_zero_run(zeroes_count, out, ctx, enc);
        }

        if (mtf_value != 0)
        {
            enc.encode(static_cast<std::int16_t>(mtf_value + 1), ctx, out);
        }
        ++j;
    }
    enc.encode(k_mtf_terminator, ctx, out);
}

void encode_bwt_block(
    std::span<std::uint8_t> bwt_in,
    std::span<std::uint32_t> idxs,
    BitWriter& out,
    BlockWorkspace& ws)
{
    const auto len_out = static_cast<std::uint32_t>(bwt_in.size());
    const std::uint32_t primary_index = ws.bwt.transform(bwt_in, idxs.first(len_out));
    ws.encoder.encode(1, ws.flag_model, out);
    encode_be32(out, ws.flag_model, ws.encoder, primary_index);
    encode_mtf_block(bwt_in, idxs.first(len_out), out, ws.mtf_model, ws.encoder, ws.mtf_encoder);
}

void encode_raw_block(
    std::span<const std::uint8_t> data,
    BitWriter& out,
    ArithCodingContext& flag_ctx,
    ArithEncoder& enc)
{
    enc.encode(0, flag_ctx, out);
    for (const std::uint8_t byte : data)
    {
        enc.encode(static_cast<std::int16_t>(byte), flag_ctx, out);
    }
    enc.encode(k_raw_terminator, flag_ctx, out);
}

[[nodiscard]] bool emit_mtf_zeroes(
    std::span<std::uint8_t> output,
    std::uint32_t& len_out,
    std::uint32_t count,
    MtfDecoder& mtf)
{
    // boffin: refused wrapping remaining capacity when the fill cursor is already past the buffer
    if (len_out > output.size() || count > output.size() - len_out)
    {
        return false;
    }
    while (count-- != 0)
    {
        output[len_out++] = mtf.decode(0);
    }
    return true;
}

[[nodiscard]] bool decode_mtf_block(
    BitReader& in,
    ArithCodingContext& ctx,
    ArithDecoder& dec,
    MtfDecoder& mtf,
    std::span<std::uint8_t> output,
    std::uint32_t& len_out)
{
    std::uint32_t tmp_sum = 0;
    std::uint32_t j = 1;
    std::uint32_t num_zeroes = 0;
    std::uint16_t nx_val = 0;
    len_out = 0;
    while ((nx_val = static_cast<std::uint16_t>(dec.decode(ctx, in))) != k_mtf_terminator)
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
            output[len_out++] = mtf.decode(static_cast<std::uint8_t>(nx_val - 1));
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
    BitReader& in,
    ArithCodingContext& ctx,
    ArithDecoder& dec,
    std::span<std::uint8_t> output,
    std::uint32_t& len_out)
{
    std::uint16_t nx_val = 0;
    len_out = 0;
    while ((nx_val = static_cast<std::uint16_t>(dec.decode(ctx, in))) != k_raw_terminator)
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

void BlockWorkspace::encode_payload(std::span<std::uint8_t> raw, BitWriter& out)
{
    std::span<std::uint8_t> transformed = raw;

    if (preprocess && raw.size() >= k_lzp_len_threshold)
    {
        lzp.clear();
        const std::uint32_t len_out = lzp.preprocess(raw, back);
        transformed = std::span<std::uint8_t>{back.data(), len_out};
    }

    // boffin: kept raw, BWT, and LZP as separate outcomes instead of one shared block path
    if (transformed.size() >= k_bwt_len_threshold)
    {
        encode_bwt_block(transformed, idxs, out, *this);
    }
    else
    {
        encode_raw_block(transformed, out, flag_model, encoder);
    }
}

std::expected<std::span<std::uint8_t>, ProcessError> BlockWorkspace::decode_transformed(BitReader& in)
{
    const std::int16_t kind = decoder.decode(flag_model, in);
    std::uint32_t length = 0;
    if (kind == 1)
    {
        const std::uint32_t string_pos = decode_be32(in, flag_model, decoder);
        if (!decode_mtf_block(in, mtf_model, decoder, mtf_decoder, back, length))
        {
            return std::unexpected(ProcessError::bad_archive);
        }
        if (length < k_bwt_len_threshold || string_pos >= length)
        {
            return std::unexpected(ProcessError::bad_archive);
        }
        bwt.unbwt(
            string_pos,
            std::span<const std::uint8_t>{back.data(), length},
            std::span<std::uint8_t>{front.data(), length},
            std::span<std::uint32_t>{idxs.data(), length});
        return std::span<std::uint8_t>{front.data(), length};
    }
    if (kind == 0)
    {
        if (!decode_raw_block(in, flag_model, decoder, back, length))
        {
            return std::unexpected(ProcessError::bad_archive);
        }
        if (length >= k_bwt_len_threshold)
        {
            return std::unexpected(ProcessError::bad_archive);
        }
        return std::span<std::uint8_t>{back.data(), length};
    }
    // boffin: refused treating an unknown block-kind symbol as a raw payload
    return std::unexpected(ProcessError::bad_archive);
}

std::expected<std::span<const std::uint8_t>, ProcessError> BlockWorkspace::finish_decoded(
    std::uint32_t expected_len,
    std::span<std::uint8_t> decoded)
{
    std::span<const std::uint8_t> result = decoded;
    if (preprocess && expected_len >= k_lzp_len_threshold)
    {
        lzp.clear();
        std::uint8_t* const dest = alternate(decoded.data());
        const std::optional<std::uint32_t> expanded =
            lzp.unpreprocess(decoded, std::span<std::uint8_t>{dest, front.size()});
        if (!expanded.has_value())
        {
            return std::unexpected(ProcessError::bad_archive);
        }
        result = std::span<const std::uint8_t>{dest, *expanded};
    }

    // boffin: refused writing a reconstructed chunk whose length is not the archive's remaining block
    if (result.size() != expected_len)
    {
        return std::unexpected(ProcessError::bad_archive);
    }
    return result;
}

} // namespace zbb
