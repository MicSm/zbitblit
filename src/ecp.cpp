/****************************************************************************
 *   Copyright (C) 1999-2020 Semikov Michael Alexandrovitch                 *
 *                                                                          *
 *   This program is free software; you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation; either version 2 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with this program; if not, write to the Free Software            *
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.              *
 ****************************************************************************/

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "inc/ecp.h"

#include "inc/arithm.h"
#include "inc/bwt.h"
#include "inc/cmstruct.h"
#include "inc/lzp_prep.h"
#include "inc/mio.h"
#include "inc/mtf.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace zbb {
namespace {

constexpr std::uint32_t k_lzp_len_threshold = 16;
constexpr std::uint32_t k_bwt_len_threshold = 8;
constexpr std::uint32_t k_block_unit_bytes = 100u * 1024u;

constexpr std::array<std::uint32_t, 25> k_pyramid_table = {
    0,        2,        6,         14,        30,        62,        126,       254,      510,
    1022,     2046,     4094,      8190,      16382,     32766,     65534,     131070,   262142,
    524286,   1048574,  2097150,   4194302,   8388606,   16777214,  33554430};

[[nodiscard]] constexpr std::uint8_t pack_system_flag(bool preprocess, std::uint8_t block_code)
{
    return static_cast<std::uint8_t>((preprocess ? 0x80u : 0u) | block_code);
}

[[nodiscard]] constexpr bool system_flag_preprocess(std::uint8_t flag)
{
    return (flag & 0x80u) != 0;
}

[[nodiscard]] constexpr std::uint8_t system_flag_block_code(std::uint8_t flag)
{
    return static_cast<std::uint8_t>(flag & 0x7fu);
}

[[nodiscard]] constexpr std::uint32_t block_bytes(std::uint8_t code)
{
    return static_cast<std::uint32_t>(code) * k_block_unit_bytes;
}

[[nodiscard]] constexpr bool is_block_code(std::uint8_t code)
{
    return code >= 1 && code <= 127;
}

struct CFileCloser
{
    void operator()(std::FILE* file) const noexcept
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }
};

using UniqueCFile = std::unique_ptr<std::FILE, CFileCloser>;

class UniqueOwnedFile
{
public:
    UniqueOwnedFile() = default;
    UniqueOwnedFile(const UniqueOwnedFile&) = delete;
    UniqueOwnedFile& operator=(const UniqueOwnedFile&) = delete;

    UniqueOwnedFile(UniqueOwnedFile&& other) noexcept
        : file_(std::move(other.file_)), path_(std::move(other.path_)), committed_(other.committed_)
    {
        other.committed_ = true;
    }

    UniqueOwnedFile& operator=(UniqueOwnedFile&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        reset();
        file_ = std::move(other.file_);
        path_ = std::move(other.path_);
        committed_ = other.committed_;
        other.committed_ = true;
        return *this;
    }

    ~UniqueOwnedFile()
    {
        reset();
    }

    [[nodiscard]] static UniqueOwnedFile create(const char* path)
    {
        std::string owned{path};
        UniqueCFile file{std::fopen(owned.c_str(), "wb")};
        return UniqueOwnedFile(std::move(file), std::move(owned));
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(file_);
    }

    [[nodiscard]] std::FILE* get() const noexcept
    {
        return file_.get();
    }

    void commit() noexcept
    {
        committed_ = true;
    }

private:
    UniqueOwnedFile(UniqueCFile file, std::string path)
        : file_(std::move(file)), path_(std::move(path))
    {
    }

    void reset() noexcept
    {
        const bool doomed = static_cast<bool>(file_) && !committed_;
        file_.reset();
        if (doomed)
        {
            std::remove(path_.c_str());
        }
    }

    UniqueCFile file_;
    std::string path_;
    bool committed_{};
};

struct InBFileCloser
{
    void operator()(bfile* file) const noexcept
    {
        if (file != nullptr)
        {
            r_bfclose(file);
        }
    }
};

using UniqueInBFile = std::unique_ptr<bfile, InBFileCloser>;

class UniqueOutBFile
{
public:
    UniqueOutBFile() = default;
    UniqueOutBFile(const UniqueOutBFile&) = delete;
    UniqueOutBFile& operator=(const UniqueOutBFile&) = delete;

    UniqueOutBFile(UniqueOutBFile&& other) noexcept
        : bf_(std::exchange(other.bf_, nullptr)), stdout_(other.stdout_), committed_(other.committed_),
          path_(std::move(other.path_))
    {
    }

    UniqueOutBFile& operator=(UniqueOutBFile&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        reset();
        bf_ = std::exchange(other.bf_, nullptr);
        stdout_ = other.stdout_;
        committed_ = other.committed_;
        path_ = std::move(other.path_);
        return *this;
    }

    ~UniqueOutBFile()
    {
        reset();
    }

    [[nodiscard]] static UniqueOutBFile open(std::string path, bool write_stdout)
    {
        bfile* const file = write_stdout ? bfopen_as_stdout() : bfopen(path.c_str(), "wb");
        return UniqueOutBFile{file, write_stdout, std::move(path)};
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return bf_ != nullptr;
    }

    [[nodiscard]] bfile* get() const noexcept
    {
        return bf_;
    }

    void commit() noexcept
    {
        committed_ = true;
    }

private:
    UniqueOutBFile(bfile* file, bool write_stdout, std::string path)
        : bf_(file), stdout_(write_stdout), path_(std::move(path))
    {
    }

    // boffin: refused fclose of stdout, and remove the archive only when the write never committed
    void reset() noexcept
    {
        if (bf_ == nullptr)
        {
            return;
        }
        bfile* const file = std::exchange(bf_, nullptr);
        if (stdout_)
        {
            w_bfclose_as_stdout(file);
            return;
        }
        w_bfclose(file);
        if (!committed_)
        {
            std::remove(path_.c_str());
        }
    }

    bfile* bf_{};
    bool stdout_{};
    bool committed_{};
    std::string path_;
};

template <int (*AcquireFn)(), void (*ReleaseFn)()>
class ArmedGlobalGuard
{
public:
    ArmedGlobalGuard() = default;
    ArmedGlobalGuard(const ArmedGlobalGuard&) = delete;
    ArmedGlobalGuard& operator=(const ArmedGlobalGuard&) = delete;

    ArmedGlobalGuard(ArmedGlobalGuard&& other) noexcept : armed_(std::exchange(other.armed_, false))
    {
    }

    ArmedGlobalGuard& operator=(ArmedGlobalGuard&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        release();
        armed_ = std::exchange(other.armed_, false);
        return *this;
    }

    ~ArmedGlobalGuard()
    {
        release();
    }

    [[nodiscard]] bool acquire()
    {
        if (armed_)
        {
            return true;
        }
        armed_ = AcquireFn() != 0;
        return armed_;
    }

private:
    void release() noexcept
    {
        if (armed_)
        {
            ReleaseFn();
            armed_ = false;
        }
    }

    bool armed_{};
};

using HashTableGuard = ArmedGlobalGuard<CreateHashTables, DestructHashTables>;
using BwtBufferGuard = ArmedGlobalGuard<SetupBwtBuffers, FreeBwtBuffers>;

struct BlockWorkspace
{
    HashTableGuard lzp;
    BwtBufferGuard bwt;
    std::vector<std::uint8_t> front;
    std::vector<std::uint8_t> back;
    std::vector<std::uint32_t> idxs;
    ArithCodingContext flag_ctx{};
    ArithCodingContext mtf_ctx{};
    ArithStream arith{};
    MtfState mtf{}; // boffin: kept encode and decode tables on the caller-owned MTF state

    [[nodiscard]] ProcessError acquire(bool preprocess, std::uint32_t block_size)
    {
        if (preprocess && !lzp.acquire())
        {
            return ProcessError::no_memory;
        }
        if (!bwt.acquire())
        {
            return ProcessError::no_memory;
        }

        const std::size_t buf_len = static_cast<std::size_t>(block_size) * 2u;
        front.resize(buf_len);
        back.resize(buf_len);
        idxs.resize(buf_len);
        SetupContext(&mtf_ctx, 258);
        SetupContext(&flag_ctx, 257);
        StartEncode(arith);
        return ProcessError::none;
    }
};

[[nodiscard]] constexpr std::array<std::uint8_t, 4> be32_bytes(std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>(value >> 24),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value)};
}

[[nodiscard]] constexpr std::uint32_t u32_from_be32(std::array<std::uint8_t, 4> bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
        | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] bool write_u8(std::FILE* file, std::uint8_t value)
{
    return std::fputc(static_cast<int>(value), file) != EOF;
}

[[nodiscard]] bool read_u8(std::FILE* file, std::uint8_t& value)
{
    const int ch = std::fgetc(file);
    if (ch == EOF)
    {
        return false;
    }
    value = static_cast<std::uint8_t>(ch);
    return true;
}

[[nodiscard]] bool write_be32(std::FILE* file, std::uint32_t value)
{
    const auto bytes = be32_bytes(value);
    return std::fwrite(bytes.data(), bytes.size(), 1, file) == 1;
}

[[nodiscard]] bool read_be32(std::FILE* file, std::uint32_t& value)
{
    std::array<std::uint8_t, 4> bytes{};
    if (std::fread(bytes.data(), bytes.size(), 1, file) != 1)
    {
        return false;
    }
    value = u32_from_be32(bytes);
    return true;
}

[[nodiscard]] ProcessError read_input_size(std::FILE* file, std::uint32_t& size)
{
    const std::int32_t raw = filesize(file);
    // boffin: refused treating a failed size query as a 4GiB payload
    if (raw < 0)
    {
        return ProcessError::io_failed;
    }
    if (raw == 0)
    {
        return ProcessError::zero_file_size;
    }
    size = static_cast<std::uint32_t>(raw);
    return ProcessError::none;
}

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

void copy_header_name(CompressedHeader& header, std::string_view path)
{
    const std::size_t n = std::min(path.size(), sizeof(header.FileName) - 1);
    std::memcpy(header.FileName, path.data(), n);
    header.FileName[n] = '\0';
}

[[nodiscard]] bool write_archive_header(bfile* out, const CompressedHeader& header)
{
    if (std::fwrite(ArcIdentifier.data(), ArcIdentifier.size(), 1, out->file) != 1)
    {
        return false;
    }
    if (std::fwrite(header.FileName, std::strlen(header.FileName) + 1, 1, out->file) != 1)
    {
        return false;
    }
    if (!write_be32(out->file, header.UncompressedLen))
    {
        return false;
    }
    return write_u8(out->file, header.SystemFlag);
}

enum class HeaderStatus
{
    ok,
    io_failed,
    bad_archive,
};

[[nodiscard]] HeaderStatus read_archive_header(bfile* in, CompressedHeader& header)
{
    std::uint8_t arc[12]{};
    if (std::fread(arc, 12, 1, in->file) != 1)
    {
        return HeaderStatus::io_failed;
    }
    if (std::memcmp(arc, ArcIdentifier.data(), ArcIdentifier.size()) != 0)
    {
        return HeaderStatus::bad_archive;
    }

    std::size_t n = 0;
    for (;;)
    {
        const int ch = std::fgetc(in->file);
        if (ch == EOF)
        {
            return HeaderStatus::io_failed;
        }
        if (ch == 0)
        {
            break;
        }
        // boffin: refused writing the stored name past FileName's last byte
        if (n + 1 >= sizeof(header.FileName))
        {
            return HeaderStatus::bad_archive;
        }
        header.FileName[n++] = static_cast<char>(static_cast<unsigned char>(ch));
    }
    header.FileName[n] = '\0';

    if (!read_be32(in->file, header.UncompressedLen))
    {
        return HeaderStatus::io_failed;
    }

    std::uint8_t flag = 0;
    if (!read_u8(in->file, flag))
    {
        return HeaderStatus::io_failed;
    }
    header.SystemFlag = flag;
    return HeaderStatus::ok;
}

[[nodiscard]] ProcessError map_header_status(HeaderStatus status)
{
    switch (status)
    {
    case HeaderStatus::ok:
        return ProcessError::none;
    case HeaderStatus::io_failed:
        return ProcessError::io_failed;
    case HeaderStatus::bad_archive:
        return ProcessError::bad_archive;
    }
    return ProcessError::bad_archive;
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
    const std::uint32_t primary_index = BWT_TRANSFORM(len_out, bwt_in.data(), idxs.data());
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

void encode_payload_block(std::span<std::uint8_t> raw, BlockWorkspace& ws, bfile* out, bool preprocess)
{
    const auto raw_len = static_cast<std::uint32_t>(raw.size());
    std::uint32_t len_out = raw_len;
    std::uint8_t* transformed = raw.data();

    if (preprocess && raw_len >= k_lzp_len_threshold)
    {
        CleanHashTables();
        len_out = LZP_PREPROCESS(raw.data(), ws.back.data(), raw_len);
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

[[nodiscard]] ProcessError decode_transformed_block(
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
        UnBWT(string_pos, len_out, ws.back.data(), ws.front.data(), ws.idxs.data());
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

[[nodiscard]] ProcessError finish_decoded_block(
    bool preprocess,
    std::uint32_t expected_len,
    std::uint8_t*& ready,
    std::uint32_t& len_out,
    BlockWorkspace& ws)
{
    if (preprocess && expected_len >= k_lzp_len_threshold)
    {
        CleanHashTables();
        std::uint8_t* const dest = (ready == ws.front.data()) ? ws.back.data() : ws.front.data();
        len_out = UnPreprocess(ready, dest, len_out);
        ready = dest;
    }

    // boffin: refused writing a reconstructed chunk whose length is not the archive's remaining block
    if (len_out != expected_len)
    {
        return ProcessError::bad_archive;
    }
    return ProcessError::none;
}

enum class CliError
{
    none,
    no_process_file,
    buf_size_wrong,
    unknown_action,
    bad_stdout,
    bad_key,
};

enum class CliAction
{
    usage,
    error,
    compress,
    decompress,
};

struct CliRequest
{
    CliAction action = CliAction::usage;
    CliError error = CliError::none;
    std::string input_path;
    std::string output_path;
    CompressOptions options;
};

[[nodiscard]] bool is_stdout_tty()
{
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(::fileno(stdout)) != 0;
#endif
}

void print_cli_error(CliError error)
{
    const char* message = nullptr;
    switch (error)
    {
    case CliError::bad_stdout:
        message = "_Can't write to current STDOUT_";
        break;
    case CliError::no_process_file:
        message = "_No file to process_";
        break;
    case CliError::buf_size_wrong:
        message = "_Uncorrect buffer size_";
        break;
    case CliError::unknown_action:
        message = "_Unknown action requested_";
        break;
    case CliError::bad_key:
        message = "_Unknown key in command line_";
        break;
    case CliError::none:
        return;
    }
    std::fprintf(stderr, "\n%s\n", message);
}

void print_process_error(ProcessError error)
{
    switch (error)
    {
    case ProcessError::zero_file_size:
        std::fprintf(stderr, "\n_File size is zero_\n");
        break;
    case ProcessError::file_not_opened:
        std::fprintf(stderr, "\n_Can't open file_\n");
        break;
    case ProcessError::no_memory:
        std::fprintf(stderr, "\n_No memory for processing_\n");
        break;
    case ProcessError::bad_archive:
        std::fprintf(stderr, "\n_Can't process archive_\n");
        break;
    case ProcessError::io_failed:
        std::fprintf(stderr, "\n_I/O error_\n");
        break;
    case ProcessError::none:
        break;
    }
}

void print_usage()
{
    std::printf(
        "\n"
        "Experimental compression program. (c) 1999-2020 by Michael Semikov\n"
        "Version 0.1\n\n"
        "use: zbitblit [ [-c] { [-p] [-bNNN] file_to_compress | -d file_to_decompress} ]\n\n"
        "This program is one-file archiver and also it has some keys:\n"
        "    -c - Write data to STDOUT\n\n"
        "    -p - Compress with use of preprocessing stage.\n\n"
        "         Sometimes \"-p\" can improve compression (enables LZP stage),\n"
        "         especially on highly redundant data\n\n"
        "    -b{1 .. 127} - Use block compression size of N*100 KBytes,\n"
        "                   this option also improves compression ratio\n"
        "                   Default key=3\n\n"
        "    -d - Decompress archive\n\n\n"
        "Warning! You use this program at your own risk!\n");
}

struct CliScan
{
    bool c_key = false;
    bool p_key = false;
    bool d_key = false;
    std::optional<std::uint8_t> block_code;
    std::string process_file;
    CliError error = CliError::none;
};

[[nodiscard]] std::optional<std::uint8_t> parse_block_size_digits(std::string_view digits)
{
    if (digits.empty())
    {
        return std::nullopt;
    }

    unsigned value = 0;
    for (const char ch : digits)
    {
        const unsigned digit = static_cast<unsigned>(static_cast<unsigned char>(ch) - '0');
        if (value > 127u)
        {
            return std::nullopt;
        }
        value = value * 10u + digit;
    }
    if (value < 1u || value > 127u)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

void apply_packed_flags(std::string_view token, CliScan& scan)
{
    for (std::size_t j = 1; j < token.size(); ++j)
    {
        switch (token[j])
        {
        case 'c':
        case 'C':
            scan.c_key = true;
            break;
        case 'p':
        case 'P':
            scan.p_key = true;
            break;
        case 'b':
        case 'B':
        {
            std::size_t end = j + 1;
            while (end < token.size() && std::isdigit(static_cast<unsigned char>(token[end])))
            {
                ++end;
            }
            const auto code = parse_block_size_digits(token.substr(j + 1, end - (j + 1)));
            j = end - 1;
            if (!code.has_value())
            {
                scan.error = CliError::buf_size_wrong;
            }
            else
            {
                scan.block_code = code;
            }
            break;
        }
        case 'd':
        case 'D':
            scan.d_key = true;
            break;
        default:
            scan.error = CliError::bad_key;
            break;
        }
        if (scan.error != CliError::none)
        {
            return;
        }
    }
}

[[nodiscard]] CliScan scan_cli(int argc, char** argv)
{
    CliScan scan;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view token = argv[i] != nullptr ? std::string_view{argv[i]} : std::string_view{};
        if (!token.empty() && token[0] == '-')
        {
            if (token.size() >= 2)
            {
                apply_packed_flags(token, scan);
            }
            else
            {
                scan.error = CliError::unknown_action;
            }
        }
        else if (!scan.process_file.empty())
        {
            scan.error = CliError::unknown_action;
        }
        else
        {
            scan.process_file.assign(token.begin(), token.end());
        }
        if (scan.error != CliError::none)
        {
            break;
        }
    }
    return scan;
}

[[nodiscard]] CliRequest finish_cli(CliScan scan)
{
    CliRequest req;
    if (scan.error != CliError::none)
    {
        req.action = CliAction::error;
        req.error = scan.error;
        return req;
    }
    if (scan.d_key && (scan.p_key || scan.block_code.has_value()))
    {
        req.action = CliAction::error;
        req.error = CliError::unknown_action;
        return req;
    }
    if ((scan.c_key || scan.p_key || scan.block_code.has_value() || scan.d_key) && scan.process_file.empty())
    {
        req.action = CliAction::error;
        req.error = CliError::no_process_file;
        return req;
    }
    if (!scan.c_key && !scan.p_key && !scan.block_code.has_value() && !scan.d_key && scan.process_file.empty())
    {
        req.action = CliAction::usage;
        return req;
    }
    if (!scan.d_key && !scan.block_code.has_value())
    {
        scan.block_code = std::uint8_t{3};
    }
    if (scan.c_key && is_stdout_tty())
    {
        req.action = CliAction::error;
        req.error = CliError::bad_stdout;
        return req;
    }

    req.input_path = std::move(scan.process_file);
    req.options.preprocess = scan.p_key;
    req.options.block_size_code = scan.block_code.value_or(0);
    req.options.write_stdout = scan.c_key;
    if (scan.d_key)
    {
        req.action = CliAction::decompress;
    }
    else
    {
        req.action = CliAction::compress;
        req.output_path = req.input_path + ".zbb";
    }
    return req;
}

[[nodiscard]] CliRequest parse_cli(int argc, char** argv)
{
    return finish_cli(scan_cli(argc, argv));
}

void enable_stdout_binary()
{
#if defined(_WIN32)
    _setmode(_fileno(stdout), O_BINARY);
#endif
}

void report_ratio(std::string_view input_path, std::string_view output_path)
{
    const std::string in_name{input_path};
    const std::string out_name{output_path};
    UniqueCFile in{std::fopen(in_name.c_str(), "rb")};
    if (!in)
    {
        return;
    }
    const std::int32_t in_size = filesize(in.get());
    in.reset();

    UniqueCFile out{std::fopen(out_name.c_str(), "rb")};
    if (!out)
    {
        return;
    }
    const std::int32_t out_size = filesize(out.get());
    // boffin: refused publishing a bits-per-symbol figure from a failed or empty size query
    if (in_size <= 0 || out_size < 0)
    {
        return;
    }
    const float bits_per_symbol = 8.0f * (static_cast<float>(out_size) / static_cast<float>(in_size));
    std::fprintf(
        stderr,
        "\nFile \"%s\" was compressed\nThe %7f bits per symbol ratio was obtained\n",
        in_name.c_str(),
        bits_per_symbol);
}

} // namespace

ProcessError compress_file(std::string_view input_path, std::string_view output_path, CompressOptions options)
{
    if (!is_block_code(options.block_size_code))
    {
        return ProcessError::bad_archive;
    }

    const std::string in_name{input_path};
    const std::string out_name{output_path};

    UniqueCFile input{std::fopen(in_name.c_str(), "rb")};
    if (!input)
    {
        return ProcessError::file_not_opened;
    }

    std::uint32_t input_size = 0;
    if (const ProcessError err = read_input_size(input.get(), input_size); err != ProcessError::none)
    {
        return err;
    }

    const std::uint32_t block_size = block_bytes(options.block_size_code);
    UniqueOutBFile output = UniqueOutBFile::open(out_name, options.write_stdout);
    if (!output)
    {
        return ProcessError::file_not_opened;
    }

    CompressedHeader header{};
    copy_header_name(header, input_path);
    header.UncompressedLen = input_size;
    header.SystemFlag = pack_system_flag(options.preprocess, options.block_size_code);
    if (!write_archive_header(output.get(), header))
    {
        return ProcessError::io_failed;
    }

    try
    {
        BlockWorkspace ws;
        if (const ProcessError err = ws.acquire(options.preprocess, block_size); err != ProcessError::none)
        {
            return err;
        }
        MtfSetup(ws.mtf);

        std::uint32_t remaining = input_size;
        while (remaining != 0)
        {
            const std::uint32_t tmp_block_len = remaining >= block_size ? block_size : remaining;
            if (std::fread(ws.front.data(), tmp_block_len, 1, input.get()) != 1)
            {
                return ProcessError::io_failed;
            }
            remaining -= tmp_block_len;
            encode_payload_block(
                std::span<std::uint8_t>{ws.front.data(), tmp_block_len},
                ws,
                output.get(),
                options.preprocess);
        }

        FinishEncode(ws.arith, output.get());
        if (std::ferror(output.get()->file) != 0)
        {
            return ProcessError::io_failed;
        }
        output.commit();
        return ProcessError::none;
    }
    catch (const std::bad_alloc&)
    {
        return ProcessError::no_memory;
    }
}

ProcessError decompress_file(std::string_view archive_path, bool write_stdout)
{
    const std::string in_name{archive_path};
    UniqueInBFile input{bfopen(in_name.c_str(), "rb")};
    if (!input)
    {
        return ProcessError::file_not_opened;
    }

    CompressedHeader header{};
    if (const ProcessError err = map_header_status(read_archive_header(input.get(), header));
        err != ProcessError::none)
    {
        return err;
    }

    const std::uint32_t input_size = header.UncompressedLen;
    const std::uint8_t block_code = system_flag_block_code(header.SystemFlag);
    if (!is_block_code(block_code))
    {
        return ProcessError::bad_archive;
    }
    const std::uint32_t block_size = block_bytes(block_code);
    const bool preprocess = system_flag_preprocess(header.SystemFlag);

    UniqueOwnedFile owned_output;
    std::FILE* output_file = stdout;
    if (!write_stdout)
    {
        owned_output = UniqueOwnedFile::create(header.FileName);
        output_file = owned_output.get();
        if (!owned_output)
        {
            return ProcessError::file_not_opened;
        }
    }

    try
    {
        BlockWorkspace ws;
        if (const ProcessError err = ws.acquire(preprocess, block_size); err != ProcessError::none)
        {
            return err;
        }
        StartDecode(ws.arith, input.get());
        DeMtfSetup(ws.mtf);

        std::uint32_t remaining = input_size;
        while (remaining != 0)
        {
            const std::uint32_t tmp_block_len = remaining >= block_size ? block_size : remaining;
            remaining -= tmp_block_len;

            std::uint8_t* ready = nullptr;
            std::uint32_t len_out = 0;
            if (const ProcessError err = decode_transformed_block(input.get(), ws, ready, len_out);
                err != ProcessError::none)
            {
                return err;
            }
            if (const ProcessError err = finish_decoded_block(preprocess, tmp_block_len, ready, len_out, ws);
                err != ProcessError::none)
            {
                return err;
            }
            if (len_out != 0 && std::fwrite(ready, len_out, 1, output_file) != 1)
            {
                return ProcessError::io_failed;
            }
        }

        owned_output.commit();
        return ProcessError::none;
    }
    catch (const std::bad_alloc&)
    {
        return ProcessError::no_memory;
    }
}

int run(int argc, char** argv)
{
    const CliRequest req = parse_cli(argc, argv);
    switch (req.action)
    {
    case CliAction::usage:
        print_usage();
        return 0;
    case CliAction::error:
        print_cli_error(req.error);
        return 1;
    case CliAction::compress:
    case CliAction::decompress:
        break;
    }

    if (req.options.write_stdout)
    {
        enable_stdout_binary();
    }

    ProcessError error = ProcessError::none;
    if (req.action == CliAction::compress)
    {
        error = compress_file(req.input_path, req.output_path, req.options);
        if (error == ProcessError::none && !req.options.write_stdout)
        {
            report_ratio(req.input_path, req.output_path);
        }
    }
    else
    {
        error = decompress_file(req.input_path, req.options.write_stdout);
        if (error == ProcessError::none)
        {
            std::fprintf(stderr, "\nArchive file \"%s\" was successfully processed\n", req.input_path.c_str());
        }
    }

    if (error != ProcessError::none)
    {
        print_process_error(error);
        return 1;
    }
    return 0;
}

} // namespace zbb

int main(int argc, char** argv)
{
    return zbb::run(argc, argv);
}
