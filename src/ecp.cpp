/****************************************************************************
 *   Copyright (c) 1999-2026 Mike Semikov                                   *
 *   SPDX-License-Identifier: MIT                                           *
 ****************************************************************************/

#include "inc/ecp.h"

#include "inc/byte_order.h"
#include "inc/cli.h"
#include "inc/file_guard.h"
#include "inc/format.h"
#include "inc/workspace.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <optional>
#include <ostream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace zbb {
namespace {

[[nodiscard]] std::span<const std::uint8_t> byte_span(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path path_of(std::string_view text)
{
    return std::filesystem::path{std::string{text}};
}

[[nodiscard]] bool write_archive_header(BitWriter& out, const ArchiveHeader& header)
{
    out.write_bytes(k_archive_magic);
    out.write_bytes(byte_span(header.name));
    out.write_byte(0);
    out.write_bytes(be32_bytes(header.uncompressed_len));
    out.write_byte(header.system_flag);
    return out.good();
}

enum class HeaderStatus
{
    ok,
    io_failed,
    bad_archive,
};

[[nodiscard]] HeaderStatus read_archive_header(BitReader& in, ArchiveHeader& header)
{
    std::array<std::uint8_t, k_archive_magic.size()> magic{};
    if (!in.read_exact(magic))
    {
        return HeaderStatus::io_failed;
    }
    if (magic != k_archive_magic)
    {
        return HeaderStatus::bad_archive;
    }

    header.name.clear();
    for (;;)
    {
        const std::optional<std::uint8_t> ch = in.read_byte();
        if (!ch.has_value())
        {
            return HeaderStatus::io_failed;
        }
        if (*ch == 0)
        {
            break;
        }
        // boffin: kept the stored-name cap equal on both sides so every archive we write reads back
        if (header.name.size() >= k_max_stored_name)
        {
            return HeaderStatus::bad_archive;
        }
        header.name.push_back(static_cast<char>(*ch));
    }

    std::array<std::uint8_t, 4> len_bytes{};
    if (!in.read_exact(len_bytes))
    {
        return HeaderStatus::io_failed;
    }
    header.uncompressed_len = u32_from_be32(len_bytes);

    const std::optional<std::uint8_t> flag = in.read_byte();
    if (!flag.has_value())
    {
        return HeaderStatus::io_failed;
    }
    header.system_flag = *flag;
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

[[nodiscard]] bool safe_output_name(const std::filesystem::path& path)
{
    if (path.empty() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }
    for (const auto& part : path)
    {
        if (part == "..")
        {
            return false;
        }
    }
    return true;
}

void report_ratio(std::string_view input_path, std::string_view output_path)
{
    std::error_code in_ec;
    std::error_code out_ec;
    const std::uintmax_t in_size = std::filesystem::file_size(path_of(input_path), in_ec);
    const std::uintmax_t out_size = std::filesystem::file_size(path_of(output_path), out_ec);
    // boffin: refused publishing a bits-per-symbol figure from a failed or empty size query
    if (in_ec || out_ec || in_size == 0)
    {
        return;
    }
    const float bits_per_symbol = 8.0f * (static_cast<float>(out_size) / static_cast<float>(in_size));
    // boffin: kept the seven-wide bits-per-symbol field; the line goes through std::print
    std::print(
        std::cerr,
        "\nFile \"{}\" was compressed\nThe {:7f} bits per symbol ratio was obtained\n",
        input_path,
        bits_per_symbol);
}

} // namespace

ProcessError compress_file(std::string_view input_path, std::string_view output_path, CompressOptions options)
{
    if (!is_block_code(options.block_size_code))
    {
        return ProcessError::bad_archive;
    }

    const std::filesystem::path in_path = path_of(input_path);
    std::ifstream input{in_path, std::ios::binary};
    if (!input.is_open())
    {
        return ProcessError::file_not_opened;
    }

    std::error_code size_ec;
    const std::uintmax_t raw_size = std::filesystem::file_size(in_path, size_ec);
    if (size_ec)
    {
        return ProcessError::io_failed;
    }
    if (raw_size == 0)
    {
        return ProcessError::zero_file_size;
    }
    // boffin: refused packing a length the 32-bit header field cannot carry
    if (raw_size > 0xffffffffu)
    {
        return ProcessError::file_too_large;
    }
    const auto input_size = static_cast<std::uint32_t>(raw_size);

    const std::uint32_t block_size = block_bytes(options.block_size_code);
    PendingArchive output;
    if (!output.open(path_of(output_path), options.write_stdout))
    {
        return ProcessError::file_not_opened;
    }

    ArchiveHeader header;
    header.name = std::string{input_path.substr(0, k_max_stored_name)};
    header.uncompressed_len = input_size;
    header.system_flag = pack_system_flag(options.preprocess, options.block_size_code);
    if (!write_archive_header(output.stream(), header))
    {
        return ProcessError::io_failed;
    }

    try
    {
        BlockWorkspace ws;
        ws.acquire(options.preprocess, block_size);
        ws.begin_encode();

        std::uint32_t remaining = input_size;
        while (remaining != 0)
        {
            const std::uint32_t tmp_block_len = remaining >= block_size ? block_size : remaining;
            input.read(reinterpret_cast<char*>(ws.front.data()), static_cast<std::streamsize>(tmp_block_len));
            if (input.gcount() != static_cast<std::streamsize>(tmp_block_len))
            {
                return ProcessError::io_failed;
            }
            remaining -= tmp_block_len;
            ws.encode_payload(std::span<std::uint8_t>{ws.front.data(), tmp_block_len}, output.stream());
        }

        ws.encoder.finish(output.stream());
        // boffin: kept the bit-tail flush ahead of the success check so a failed final write cannot commit
        if (!output.stream().finish())
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
    BitReader input;
    if (!input.open(path_of(archive_path)))
    {
        return ProcessError::file_not_opened;
    }

    ArchiveHeader header;
    if (const ProcessError err = map_header_status(read_archive_header(input, header));
        err != ProcessError::none)
    {
        return err;
    }

    const std::uint32_t input_size = header.uncompressed_len;
    const std::uint8_t block_code = system_flag_block_code(header.system_flag);
    if (!is_block_code(block_code))
    {
        return ProcessError::bad_archive;
    }
    const std::uint32_t block_size = block_bytes(block_code);
    const bool preprocess = system_flag_preprocess(header.system_flag);

    PendingFile owned_output;
    if (!write_stdout)
    {
        const std::filesystem::path out_path{header.name};
        // boffin: refused stored names that climb out of the extraction directory
        if (!safe_output_name(out_path))
        {
            return ProcessError::bad_archive;
        }
        if (!owned_output.open(out_path))
        {
            return ProcessError::file_not_opened;
        }
    }
    std::ostream& out = write_stdout ? std::cout : owned_output.stream();

    try
    {
        BlockWorkspace ws;
        ws.acquire(preprocess, block_size);
        ws.begin_decode(input);

        std::uint32_t remaining = input_size;
        while (remaining != 0)
        {
            const std::uint32_t tmp_block_len = remaining >= block_size ? block_size : remaining;
            remaining -= tmp_block_len;

            const auto decoded = ws.decode_transformed(input);
            if (!decoded.has_value())
            {
                return decoded.error();
            }
            const auto final_bytes = ws.finish_decoded(tmp_block_len, *decoded);
            if (!final_bytes.has_value())
            {
                return final_bytes.error();
            }
            out.write(
                reinterpret_cast<const char*>(final_bytes->data()),
                static_cast<std::streamsize>(final_bytes->size()));
            if (!out.good())
            {
                return ProcessError::io_failed;
            }
        }

        out.flush();
        if (!out.good())
        {
            return ProcessError::io_failed;
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
            std::println(std::cerr, "\nArchive file \"{}\" was successfully processed", req.input_path);
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
