#pragma once

#include "inc/ecp.h"

#include <string>

namespace zbb {

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

[[nodiscard]] CliRequest parse_cli(int argc, char** argv);

void print_cli_error(CliError error);

void print_process_error(ProcessError error);

void print_usage();

void enable_stdout_binary();

} // namespace zbb
