#pragma once

#include "inc/mio.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace zbb {

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

class UniqueOutBFile
{
public:
    UniqueOutBFile() = default;
    UniqueOutBFile(const UniqueOutBFile&) = delete;
    UniqueOutBFile& operator=(const UniqueOutBFile&) = delete;

    UniqueOutBFile(UniqueOutBFile&& other) noexcept
        : file_(std::move(other.file_)), stdout_(other.stdout_), committed_(other.committed_),
          path_(std::move(other.path_))
    {
        other.stdout_ = false;
        other.committed_ = true;
    }

    UniqueOutBFile& operator=(UniqueOutBFile&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        reset();
        file_ = std::move(other.file_);
        stdout_ = other.stdout_;
        committed_ = other.committed_;
        path_ = std::move(other.path_);
        other.stdout_ = false;
        other.committed_ = true;
        return *this;
    }

    ~UniqueOutBFile()
    {
        reset();
    }

    [[nodiscard]] static UniqueOutBFile open(std::string path, bool write_stdout)
    {
        BitFile bits = write_stdout ? BitFile::stdout_write() : BitFile::open_write(path.c_str());
        return UniqueOutBFile{std::move(bits), write_stdout, std::move(path)};
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(file_);
    }

    [[nodiscard]] BitFile& stream() noexcept
    {
        return file_;
    }

    void commit() noexcept
    {
        committed_ = true;
    }

private:
    UniqueOutBFile(BitFile file, bool write_stdout, std::string path)
        : file_(std::move(file)), stdout_(write_stdout), path_(std::move(path))
    {
    }

    // boffin: refused fclose of stdout, and remove the archive only when the write never committed
    void reset() noexcept
    {
        if (!file_)
        {
            return;
        }
        const bool doomed = !stdout_ && !committed_;
        file_.close();
        if (doomed)
        {
            std::remove(path_.c_str());
        }
    }

    BitFile file_{};
    bool stdout_{};
    bool committed_{};
    std::string path_;
};

} // namespace zbb
