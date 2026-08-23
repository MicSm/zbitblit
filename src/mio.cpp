#include "inc/mio.h"

#include <array>
#include <iostream>

namespace zbb {

bool BitReader::open(const std::filesystem::path& path)
{
    file_.open(path, std::ios::binary);
    rbuf_ = 0;
    rcnt_ = 0;
    return file_.is_open();
}

bool BitReader::read_exact(std::span<std::uint8_t> bytes)
{
    file_.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file_.gcount() == static_cast<std::streamsize>(bytes.size());
}

std::optional<std::uint8_t> BitReader::read_byte()
{
    const int ch = file_.get();
    if (ch == std::ifstream::traits_type::eof())
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(ch);
}

std::uint8_t BitReader::read_bit()
{
    if (rcnt_ == 0)
    {
        // boffin: kept truncated input reading as 0xff bytes so a short archive
        // keeps decoding into the length checks instead of crashing the coder
        std::array<std::uint8_t, 4> word{0xff, 0xff, 0xff, 0xff};
        file_.read(reinterpret_cast<char*>(word.data()), static_cast<std::streamsize>(word.size()));
        rbuf_ = (static_cast<std::uint32_t>(word[0]) << 24) | (static_cast<std::uint32_t>(word[1]) << 16)
            | (static_cast<std::uint32_t>(word[2]) << 8) | static_cast<std::uint32_t>(word[3]);
        rcnt_ = 32;
    }
    rcnt_--;
    return (rbuf_ & (1u << rcnt_)) != 0;
}

bool BitWriter::open_file(const std::filesystem::path& path)
{
    file_.open(path, std::ios::binary | std::ios::trunc);
    to_stdout_ = false;
    active_ = file_.is_open();
    wbuf_ = 0;
    wcnt_ = 0;
    return active_;
}

void BitWriter::open_stdout()
{
    to_stdout_ = true;
    active_ = true;
    wbuf_ = 0;
    wcnt_ = 0;
}

std::ostream& BitWriter::sink()
{
    return to_stdout_ ? std::cout : file_;
}

void BitWriter::write_bytes(std::span<const std::uint8_t> bytes)
{
    sink().write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void BitWriter::write_byte(std::uint8_t byte)
{
    write_bytes(std::span<const std::uint8_t>{&byte, 1});
}

void BitWriter::write_bit(std::uint8_t bit)
{
    if (wcnt_ == 32)
    {
        const std::array<std::uint8_t, 4> word{
            static_cast<std::uint8_t>((wbuf_ >> 24) & 0xff),
            static_cast<std::uint8_t>((wbuf_ >> 16) & 0xff),
            static_cast<std::uint8_t>((wbuf_ >> 8) & 0xff),
            static_cast<std::uint8_t>(wbuf_ & 0xff)};
        write_bytes(word);
        wcnt_ = 0;
    }
    wcnt_++;
    wbuf_ <<= 1;
    wbuf_ |= static_cast<std::uint32_t>(bit);
}

bool BitWriter::finish()
{
    if (!active_)
    {
        return true;
    }
    if (wcnt_ != 0)
    {
        // boffin: refused shifting the pending 32-bit word by 32 when no bits remain
        wbuf_ <<= 32 - wcnt_;
        const std::array<std::uint8_t, 4> word{
            static_cast<std::uint8_t>((wbuf_ >> 24) & 0xff),
            static_cast<std::uint8_t>((wbuf_ >> 16) & 0xff),
            static_cast<std::uint8_t>((wbuf_ >> 8) & 0xff),
            static_cast<std::uint8_t>(wbuf_ & 0xff)};
        const std::size_t nbytes = static_cast<std::size_t>(wcnt_ >> 3) + ((wcnt_ & 0x07) != 0 ? 1u : 0u);
        write_bytes(std::span<const std::uint8_t>{word.data(), nbytes});
        wcnt_ = 0;
        wbuf_ = 0;
    }
    sink().flush();
    return sink().good();
}

bool BitWriter::good()
{
    return active_ && sink().good();
}

void BitWriter::close()
{
    finish();
    if (!to_stdout_)
    {
        file_.close();
    }
    active_ = false;
}

} // namespace zbb
