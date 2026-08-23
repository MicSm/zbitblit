#pragma once

namespace zbb {

enum class ProcessError
{
    none,
    zero_file_size,
    file_not_opened,
    no_memory,
    bad_archive,
    io_failed,
};

} // namespace zbb
