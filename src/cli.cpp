#include "inc/cli.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace zbb {
namespace {

struct CliScan
{
    bool c_key = false;
    bool p_key = false;
    bool d_key = false;
    std::optional<std::uint8_t> block_code;
    std::string process_file;
    CliError error = CliError::none;
};

[[nodiscard]] bool is_stdout_tty()
{
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(::fileno(stdout)) != 0;
#endif
}

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

} // namespace

CliRequest parse_cli(int argc, char** argv)
{
    return finish_cli(scan_cli(argc, argv));
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

void enable_stdout_binary()
{
#if defined(_WIN32)
    _setmode(_fileno(stdout), O_BINARY);
#endif
}

} // namespace zbb
