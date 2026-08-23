#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <span>

namespace zbb {

// Archive input: whole-byte reads for the header, then most-significant-first
// bit reads for the coded payload. The two phases must not interleave.
class BitReader
{
public:
    BitReader() = default;
    BitReader(const BitReader&) = delete;
    BitReader& operator=(const BitReader&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& path);

    [[nodiscard]] explicit operator bool() const
    {
        return file_.is_open();
    }

    [[nodiscard]] bool read_exact(std::span<std::uint8_t> bytes);
    [[nodiscard]] std::optional<std::uint8_t> read_byte();
    [[nodiscard]] std::uint8_t read_bit();

private:
    std::ifstream file_;
    std::uint32_t rbuf_ = 0;
    std::uint8_t rcnt_ = 0;
};

// Archive output: whole-byte writes for the header, then bit writes for the
// coded payload. finish() drains the pending bit tail and reports stream
// health; commit decisions belong to the owner, not to this class.
class BitWriter
{
public:
    BitWriter() = default;
    BitWriter(const BitWriter&) = delete;
    BitWriter& operator=(const BitWriter&) = delete;

    ~BitWriter()
    {
        finish();
    }

    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    void open_stdout();

    [[nodiscard]] explicit operator bool() const
    {
        return to_stdout_ || file_.is_open();
    }

    void write_bytes(std::span<const std::uint8_t> bytes);
    void write_byte(std::uint8_t byte);
    void write_bit(std::uint8_t bit);

    /* flush the partial bit word and the stream; safe to call repeatedly */
    bool finish();

    [[nodiscard]] bool good();

    /* release the file handle so the owner may remove the path */
    void close();

private:
    [[nodiscard]] std::ostream& sink();

    std::ofstream file_;
    bool to_stdout_ = false;
    bool active_ = false;
    std::uint32_t wbuf_ = 0;
    std::uint8_t wcnt_ = 0;
};

} // namespace zbb
