#pragma once

#include "inc/status.h"

#include <cstdint>
#include <string_view>

namespace zbb {

struct CompressOptions
{
    bool preprocess = false;
    std::uint8_t block_size_code = 3;
    bool write_stdout = false;
};

[[nodiscard]] ProcessError compress_file(
    std::string_view input_path,
    std::string_view output_path,
    CompressOptions options);

[[nodiscard]] ProcessError decompress_file(std::string_view archive_path, bool write_stdout);

int run(int argc, char** argv);

} // namespace zbb
