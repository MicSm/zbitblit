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
        UniqueOwnedFile out;
        out.path_ = path;
        out.file_.reset(std::fopen(path, "wb"));
        return out;
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
        UniqueOutBFile out;
        out.stdout_ = write_stdout;
        out.path_ = std::move(path);
        out.bf_ = write_stdout ? bfopen_as_stdout() : bfopen(out.path_.c_str(), "wb");
        return out;
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
        return ProcessError::none;
    }
};

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
    return write_u8(file, static_cast<std::uint8_t>((value >> 24) & 0xff))
        && write_u8(file, static_cast<std::uint8_t>((value >> 16) & 0xff))
        && write_u8(file, static_cast<std::uint8_t>((value >> 8) & 0xff))
        && write_u8(file, static_cast<std::uint8_t>(value & 0xff));
}

[[nodiscard]] bool read_be32(std::FILE* file, std::uint32_t& value)
{
    std::uint32_t acc = 0;
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        std::uint8_t byte = 0;
        if (!read_u8(file, byte))
        {
            return false;
        }
        acc |= static_cast<std::uint32_t>(byte) << shift;
    }
    value = acc;
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

void encode_be32(bfile* file, ArithCodingContext& ctx, std::uint32_t value)
{
    EncodeChar(static_cast<std::int16_t>((value >> 24) & 0xff), file, &ctx);
    EncodeChar(static_cast<std::int16_t>((value >> 16) & 0xff), file, &ctx);
    EncodeChar(static_cast<std::int16_t>((value >> 8) & 0xff), file, &ctx);
    EncodeChar(static_cast<std::int16_t>(value & 0xff), file, &ctx);
}

[[nodiscard]] std::uint32_t decode_be32(bfile* file, ArithCodingContext& ctx)
{
    std::uint32_t value = static_cast<std::uint32_t>(DecodeChar(file, &ctx));
    value <<= 8;
    value |= static_cast<std::uint32_t>(DecodeChar(file, &ctx));
    value <<= 8;
    value |= static_cast<std::uint32_t>(DecodeChar(file, &ctx));
    value <<= 8;
    value |= static_cast<std::uint32_t>(DecodeChar(file, &ctx));
    return value;
}

void copy_header_name(CompressedHeader& header, std::string_view path)
{
    const std::size_t n = std::min(path.size(), sizeof(header.FileName) - 1);
    std::memcpy(header.FileName, path.data(), n);
    header.FileName[n] = '\0';
}

[[nodiscard]] bool write_archive_header(bfile* out, const CompressedHeader& header)
{
    if (std::fwrite(ArcIdentifier, 12, 1, out->file) != 1)
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
    if (std::memcmp(arc, ArcIdentifier, 12) != 0)
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

void encode_zero_run(std::uint32_t zeroes_count, bfile* out, ArithCodingContext& ctx)
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
        EncodeChar(static_cast<std::int16_t>(zeroes_count & 1), out, &ctx);
        zeroes_count >>= 1;
    } while (--lhs);
}

void encode_mtf_block(
    const std::uint8_t* bwt_in,
    std::uint32_t len_out,
    const std::uint32_t* idxs,
    bfile* out,
    ArithCodingContext& ctx)
{
    std::uint32_t j = 0;
    while (j < len_out)
    {
        std::uint16_t mtf_value = 0;
        std::uint32_t zeroes_count = 0;
        while (j < len_out &&
               (mtf_value = GetMtfValue(bwt_in[((len_out + idxs[j]) - 3) % len_out])) == 0)
        {
            ++zeroes_count;
            ++j;
        }

        if (zeroes_count-- > 0)
        {
            encode_zero_run(zeroes_count, out, ctx);
        }

        if (mtf_value != 0)
        {
            EncodeChar(static_cast<std::int16_t>(mtf_value + 1), out, &ctx);
        }
        ++j;
    }
    EncodeChar(257, out, &ctx);
}

void encode_bwt_block(
    std::uint8_t* bwt_in,
    std::uint32_t len_out,
    std::uint32_t* idxs,
    bfile* out,
    ArithCodingContext& flag_ctx,
    ArithCodingContext& mtf_ctx)
{
    const std::uint32_t primary_index = BWT_TRANSFORM(len_out, bwt_in, idxs);
    EncodeChar(1, out, &flag_ctx);
    encode_be32(out, flag_ctx, primary_index);
    encode_mtf_block(bwt_in, len_out, idxs, out, mtf_ctx);
}

void encode_raw_block(const std::uint8_t* data, std::uint32_t len_out, bfile* out, ArithCodingContext& flag_ctx)
{
    EncodeChar(0, out, &flag_ctx);
    for (std::uint32_t j = 0; j < len_out; ++j)
    {
        EncodeChar(static_cast<std::int16_t>(data[j]), out, &flag_ctx);
    }
    EncodeChar(256, out, &flag_ctx);
}

[[nodiscard]] bool emit_mtf_zeroes(std::span<std::uint8_t> output, std::uint32_t& len_out, std::uint32_t count)
{
    if (count > output.size() - len_out)
    {
        return false;
    }
    while (count-- != 0)
    {
        output[len_out++] = GetByMtfPosition(0);
    }
    return true;
}

[[nodiscard]] bool decode_mtf_block(
    bfile* in,
    ArithCodingContext& ctx,
    std::span<std::uint8_t> output,
    std::uint32_t& len_out)
{
    std::uint32_t tmp_sum = 0;
    std::uint32_t j = 1;
    std::uint32_t num_zeroes = 0;
    std::uint16_t nx_val = 0;
    len_out = 0;
    while ((nx_val = static_cast<std::uint16_t>(DecodeChar(in, &ctx))) != 257)
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
                if (!emit_mtf_zeroes(output, len_out, tmp_sum))
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
            output[len_out++] = static_cast<std::uint8_t>(GetByMtfPosition(static_cast<std::uint8_t>(nx_val - 1)));
        }
    }

    if (num_zeroes > 0)
    {
        if (num_zeroes >= 32)
        {
            return false;
        }
        tmp_sum += (1u << num_zeroes) - 1u;
        if (!emit_mtf_zeroes(output, len_out, tmp_sum))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decode_raw_block(
    bfile* in,
    ArithCodingContext& ctx,
    std::span<std::uint8_t> output,
    std::uint32_t& len_out)
{
    std::uint16_t nx_val = 0;
    len_out = 0;
    while ((nx_val = static_cast<std::uint16_t>(DecodeChar(in, &ctx))) != 256)
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
    if (DecodeChar(in, &ws.flag_ctx) == 1)
    {
        const std::uint32_t string_pos = decode_be32(in, ws.flag_ctx);
        if (!decode_mtf_block(in, ws.mtf_ctx, ws.back, len_out))
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
    else
    {
        if (!decode_raw_block(in, ws.flag_ctx, ws.back, len_out))
        {
            return ProcessError::bad_archive;
        }
        if (len_out >= k_bwt_len_threshold)
        {
            return ProcessError::bad_archive;
        }
        decoded = ws.back.data();
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

[[nodiscard]] CliRequest parse_cli(int argc, char** argv)
{
    CliRequest req;
    bool c_key = false;
    bool p_key = false;
    bool d_key = false;
    std::optional<std::uint8_t> block_code;
    std::string process_file;
    CliError err = CliError::none;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view token = argv[i] != nullptr ? std::string_view{argv[i]} : std::string_view{};
        if (!token.empty() && token[0] == '-')
        {
            if (token.size() >= 2)
            {
                for (std::size_t j = 1; j < token.size(); ++j)
                {
                    switch (token[j])
                    {
                    case 'c':
                    case 'C':
                        c_key = true;
                        break;
                    case 'p':
                    case 'P':
                        p_key = true;
                        break;
                    case 'b':
                    case 'B':
                    {
                        long long number = 0;
                        while (++j < token.size())
                        {
                            const unsigned char ch = static_cast<unsigned char>(token[j]);
                            if (!std::isdigit(ch))
                            {
                                break;
                            }
                            number = number * 10 + (ch - '0');
                        }
                        --j;
                        if (number < 1 || number > 127)
                        {
                            err = CliError::buf_size_wrong;
                        }
                        else
                        {
                            block_code = static_cast<std::uint8_t>(number);
                        }
                        break;
                    }
                    case 'd':
                    case 'D':
                        d_key = true;
                        break;
                    default:
                        err = CliError::bad_key;
                        break;
                    }
                    if (err != CliError::none)
                    {
                        break;
                    }
                }
            }
            else
            {
                err = CliError::unknown_action;
            }
        }
        else
        {
            if (!process_file.empty())
            {
                err = CliError::unknown_action;
            }
            else
            {
                process_file.assign(token.begin(), token.end());
            }
        }
        if (err != CliError::none)
        {
            break;
        }
    }

    if (err != CliError::none)
    {
        req.action = CliAction::error;
        req.error = err;
        return req;
    }
    if (d_key && (p_key || block_code.has_value()))
    {
        req.action = CliAction::error;
        req.error = CliError::unknown_action;
        return req;
    }
    if ((c_key || p_key || block_code.has_value() || d_key) && process_file.empty())
    {
        req.action = CliAction::error;
        req.error = CliError::no_process_file;
        return req;
    }
    if (!c_key && !p_key && !block_code.has_value() && !d_key && process_file.empty())
    {
        req.action = CliAction::usage;
        return req;
    }
    if (!d_key && !block_code.has_value())
    {
        block_code = 3;
    }
    if (c_key && is_stdout_tty())
    {
        req.action = CliAction::error;
        req.error = CliError::bad_stdout;
        return req;
    }

    req.input_path = std::move(process_file);
    req.options.preprocess = p_key;
    req.options.block_size_code = block_code.value_or(0);
    req.options.write_stdout = c_key;
    if (d_key)
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
    const float ss1 = static_cast<float>(filesize(in.get()));
    in.reset();

    UniqueCFile out{std::fopen(out_name.c_str(), "rb")};
    if (!out)
    {
        return;
    }
    const float ss2 = static_cast<float>(filesize(out.get()));
    const float bits_per_symbol = 8.0f * (ss2 / ss1);
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
        MtfSetup();

        std::uint8_t* const input_buf = ws.front.data();
        std::uint8_t* const output_buf = ws.back.data();
        std::uint32_t remaining = input_size;
        while (remaining != 0)
        {
            const std::uint32_t tmp_block_len = remaining >= block_size ? block_size : remaining;
            if (std::fread(input_buf, tmp_block_len, 1, input.get()) != 1)
            {
                return ProcessError::io_failed;
            }
            remaining -= tmp_block_len;

            std::uint32_t len_out = 0;
            std::uint8_t* bwt_in = nullptr;
            if (options.preprocess && tmp_block_len >= k_lzp_len_threshold)
            {
                CleanHashTables();
                len_out = LZP_PREPROCESS(input_buf, output_buf, tmp_block_len);
                bwt_in = output_buf;
            }
            else
            {
                len_out = tmp_block_len;
                bwt_in = input_buf;
            }

            // boffin: kept raw, BWT, and LZP as separate outcomes instead of one shared block path
            if (len_out >= k_bwt_len_threshold)
            {
                encode_bwt_block(bwt_in, len_out, ws.idxs.data(), output.get(), ws.flag_ctx, ws.mtf_ctx);
            }
            else
            {
                encode_raw_block(bwt_in, len_out, output.get(), ws.flag_ctx);
            }
        }

        FinishEncode(output.get());
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
        StartDecode(input.get());
        DeMtfSetup();

        std::uint32_t remaining = input_size;
        while (remaining != 0)
        {
            const std::uint32_t tmp_block_len = remaining >= block_size ? block_size : remaining;
            remaining -= tmp_block_len;

            std::uint8_t* decoded = nullptr;
            std::uint32_t len_out = 0;
            if (const ProcessError err = decode_transformed_block(input.get(), ws, decoded, len_out);
                err != ProcessError::none)
            {
                return err;
            }

            std::uint8_t* ready = decoded;
            if (preprocess && tmp_block_len >= k_lzp_len_threshold)
            {
                CleanHashTables();
                std::uint8_t* const dest = (decoded == ws.front.data()) ? ws.back.data() : ws.front.data();
                len_out = UnPreprocess(decoded, dest, len_out);
                ready = dest;
            }

            // boffin: refused writing a reconstructed chunk whose length is not the archive's remaining block
            if (len_out != tmp_block_len)
            {
                return ProcessError::bad_archive;
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
    if (req.action == CliAction::usage)
    {
        print_usage();
        return 0;
    }
    if (req.action == CliAction::error)
    {
        print_cli_error(req.error);
        return 1;
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
