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

#include "inc/byte_order.h"
#include "inc/cli.h"
#include "inc/cmstruct.h"
#include "inc/codec.h"
#include "inc/file_guard.h"
#include "inc/workspace.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>
#include <string>
#include <string_view>

namespace zbb {
namespace {

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

void copy_header_name(CompressedHeader& header, std::string_view path)
{
    const std::size_t n = std::min(path.size(), sizeof(header.FileName) - 1);
    std::copy_n(path.begin(), n, header.FileName);
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
    std::array<std::uint8_t, 12> arc{};
    if (std::fread(arc.data(), arc.size(), 1, in->file) != 1)
    {
        return HeaderStatus::io_failed;
    }
    if (!std::equal(arc.begin(), arc.end(), ArcIdentifier.begin()))
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
