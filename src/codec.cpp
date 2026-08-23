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

void encode_be32(BitFile& file, ArithCodingContext& ctx, ArithStream& stream, std::uint32_t value)
{
    for (const std::uint8_t byte : be32_bytes(value))
    {
        stream.encode_char(static_cast<std::int16_t>(byte), file, ctx);
    }
}

[[nodiscard]] std::uint32_t decode_be32(BitFile& file, ArithCodingContext& ctx, ArithStream& stream)
{
    std::array<std::uint8_t, 4> bytes{};
    for (std::uint8_t& byte : bytes)
    {
        byte = static_cast<std::uint8_t>(stream.decode_char(file, ctx));
    }
    return u32_from_be32(bytes);
}

void encode_zero_run(std::uint32_t zeroes_count, BitFile& out, ArithCodingContext& ctx, ArithStream& stream)
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
        stream.encode_char(static_cast<std::int16_t>(zeroes_count & 1), out, ctx);
        zeroes_count >>= 1;
    } while (--lhs);
}

void encode_mtf_block(
    std::span<const std::uint8_t> bwt_in,
    std::span<const std::uint32_t> idxs,
    BitFile& out,
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
               (mtf_value = mtf.get_value(bwt_in[((len_out + idxs[j]) - 3) % len_out])) == 0)
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
            stream.encode_char(static_cast<std::int16_t>(mtf_value + 1), out, ctx);
        }
        ++j;
    }
    stream.encode_char(257, out, ctx);
}

void encode_bwt_block(std::span<std::uint8_t> bwt_in, std::span<std::uint32_t> idxs, BitFile& out, BlockWorkspace& ws)
{
    const auto len_out = static_cast<std::uint32_t>(bwt_in.size());
    const std::uint32_t primary_index = ws.bwt.transform(len_out, bwt_in.data(), idxs.data());
    ws.arith.stream.encode_char(1, out, ws.arith.flag);
    encode_be32(out, ws.arith.flag, ws.arith.stream, primary_index);
    encode_mtf_block(
        std::span<const std::uint8_t>{bwt_in.data(), len_out},
        std::span<const std::uint32_t>{idxs.data(), len_out},
        out,
        ws.arith.mtf,
        ws.arith.stream,
        ws.mtf);
}

void encode_raw_block(
    std::span<const std::uint8_t> data,
    BitFile& out,
    ArithCodingContext& flag_ctx,
    ArithStream& stream)
{
    stream.encode_char(0, out, flag_ctx);
    for (const std::uint8_t byte : data)
    {
        stream.encode_char(static_cast<std::int16_t>(byte), out, flag_ctx);
    }
    stream.encode_char(256, out, flag_ctx);
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
        output[len_out++] = mtf.by_position(0);
    }
    return true;
}

[[nodiscard]] bool decode_mtf_block(
    BitFile& in,
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
    while ((nx_val = static_cast<std::uint16_t>(stream.decode_char(in, ctx))) != 257)
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
                static_cast<std::uint8_t>(mtf.by_position(static_cast<std::uint8_t>(nx_val - 1)));
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
    BitFile& in,
    ArithCodingContext& ctx,
    ArithStream& stream,
    std::span<std::uint8_t> output,
    std::uint32_t& len_out)
{
    std::uint16_t nx_val = 0;
    len_out = 0;
    while ((nx_val = static_cast<std::uint16_t>(stream.decode_char(in, ctx))) != 256)
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

void BlockWorkspace::encode_payload(std::span<std::uint8_t> raw, BitFile& out)
{
    const auto raw_len = static_cast<std::uint32_t>(raw.size());
    std::uint32_t len_out = raw_len;
    std::uint8_t* transformed = raw.data();

    if (preprocess && raw_len >= k_lzp_len_threshold)
    {
        lzp.clear();
        len_out = lzp.preprocess(raw.data(), back.data(), raw_len);
        transformed = back.data();
    }

    // boffin: kept raw, BWT, and LZP as separate outcomes instead of one shared block path
    if (len_out >= k_bwt_len_threshold)
    {
        encode_bwt_block(
            std::span<std::uint8_t>{transformed, len_out},
            std::span<std::uint32_t>{idxs.data(), idxs.size()},
            out,
            *this);
    }
    else
    {
        encode_raw_block(std::span<const std::uint8_t>{transformed, len_out}, out, arith.flag, arith.stream);
    }
}

DecodeOutcome BlockWorkspace::decode_transformed(BitFile& in)
{
    DecodeOutcome decoded;
    const std::int16_t kind = arith.stream.decode_char(in, arith.flag);
    if (kind == 1)
    {
        const std::uint32_t string_pos = decode_be32(in, arith.flag, arith.stream);
        if (!decode_mtf_block(in, arith.mtf, arith.stream, mtf, back, decoded.length))
        {
            decoded.error = ProcessError::bad_archive;
            return decoded;
        }
        if (decoded.length < k_bwt_len_threshold || string_pos >= decoded.length)
        {
            decoded.error = ProcessError::bad_archive;
            return decoded;
        }
        bwt.unbwt(string_pos, decoded.length, back.data(), front.data(), idxs.data());
        decoded.data = front.data();
    }
    else if (kind == 0)
    {
        if (!decode_raw_block(in, arith.flag, arith.stream, back, decoded.length))
        {
            decoded.error = ProcessError::bad_archive;
            return decoded;
        }
        if (decoded.length >= k_bwt_len_threshold)
        {
            decoded.error = ProcessError::bad_archive;
            return decoded;
        }
        decoded.data = back.data();
    }
    else
    {
        // boffin: refused treating an unknown block-kind symbol as a raw payload
        decoded.error = ProcessError::bad_archive;
        return decoded;
    }
    return decoded;
}

ProcessError BlockWorkspace::finish_decoded(std::uint32_t expected_len, DecodeOutcome& decoded)
{
    if (preprocess && expected_len >= k_lzp_len_threshold)
    {
        lzp.clear();
        std::uint8_t* const dest = alternate(decoded.data);
        decoded.length = lzp.unpreprocess(decoded.data, dest, decoded.length);
        decoded.data = dest;
    }

    // boffin: refused writing a reconstructed chunk whose length is not the archive's remaining block
    if (decoded.length != expected_len)
    {
        return ProcessError::bad_archive;
    }
    return ProcessError::none;
}

} // namespace zbb
