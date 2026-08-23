#pragma once

#include "inc/mio.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <system_error>

namespace zbb {

// Output file that self-deletes unless the writer commits a complete result.
class PendingFile
{
public:
    PendingFile() = default;
    PendingFile(const PendingFile&) = delete;
    PendingFile& operator=(const PendingFile&) = delete;

    ~PendingFile()
    {
        discard_if_uncommitted();
    }

    [[nodiscard]] bool open(const std::filesystem::path& path)
    {
        path_ = path;
        file_.open(path, std::ios::binary | std::ios::trunc);
        return file_.is_open();
    }

    [[nodiscard]] std::ostream& stream()
    {
        return file_;
    }

    void commit() noexcept
    {
        committed_ = true;
    }

private:
    void discard_if_uncommitted()
    {
        if (!file_.is_open() || committed_)
        {
            return;
        }
        file_.close();
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    std::ofstream file_;
    std::filesystem::path path_;
    bool committed_ = false;
};

// Archive sink that self-deletes a partial file unless committed.
class PendingArchive
{
public:
    PendingArchive() = default;
    PendingArchive(const PendingArchive&) = delete;
    PendingArchive& operator=(const PendingArchive&) = delete;

    ~PendingArchive()
    {
        discard_if_uncommitted();
    }

    [[nodiscard]] bool open(const std::filesystem::path& path, bool to_stdout)
    {
        if (to_stdout)
        {
            writer_.open_stdout();
            return true;
        }
        path_ = path;
        file_backed_ = writer_.open_file(path);
        return file_backed_;
    }

    [[nodiscard]] BitWriter& stream()
    {
        return writer_;
    }

    void commit() noexcept
    {
        committed_ = true;
    }

private:
    // boffin: refused deleting or closing stdout; only a file-backed sink rolls back
    void discard_if_uncommitted()
    {
        if (!file_backed_ || committed_)
        {
            return;
        }
        writer_.close();
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    BitWriter writer_;
    std::filesystem::path path_;
    bool file_backed_ = false;
    bool committed_ = false;
};

} // namespace zbb
