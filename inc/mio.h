#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

namespace zbb {

constexpr int SBUFF = 4096;

class BitFile
{
public:
    BitFile() = default;
    BitFile(const BitFile&) = delete;
    BitFile& operator=(const BitFile&) = delete;
    BitFile(BitFile&& other) noexcept;
    BitFile& operator=(BitFile&& other) noexcept;
    ~BitFile();

    [[nodiscard]] static BitFile open_read(const char* name);
    [[nodiscard]] static BitFile open_write(const char* name);
    [[nodiscard]] static BitFile stdout_write();

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return file_ != nullptr;
    }

    [[nodiscard]] std::FILE* native() const noexcept
    {
        return file_;
    }

    [[nodiscard]] std::uint8_t read_bit();
    void write_bit(std::uint8_t bit);
    // boffin: refused leaving FILE* and pending bits without one owner
    void close() noexcept;

private:
    BitFile(std::FILE* file, bool owns_file, bool writing);

    void flush_write_tail() noexcept;

    std::FILE* file_ = nullptr;
    bool owns_file_ = false;
    bool writing_ = false;
    std::uint32_t rbuf_ = 0;
    std::uint8_t rcnt_ = 0;
    std::uint32_t wbuf_ = 0;
    std::uint8_t wcnt_ = 0;
    std::unique_ptr<char[]> iobuf_{};
};

[[nodiscard]] std::int32_t filesize(std::FILE* file);

} // namespace zbb
