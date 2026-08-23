#include "inc/mio.h"

#include <array>
#include <cstddef>
#include <utility>

namespace zbb {
namespace {

void attach_file_buffer(std::FILE* file, char* buffer)
{
    std::setvbuf(file, buffer, _IOFBF, static_cast<std::size_t>(SBUFF));
}

} // namespace

BitFile::BitFile(std::FILE* file, bool owns_file, bool writing)
    : file_(file), owns_file_(owns_file), writing_(writing)
{
    if (owns_file_)
    {
        iobuf_ = std::make_unique<char[]>(static_cast<std::size_t>(SBUFF));
        attach_file_buffer(file_, iobuf_.get());
    }
}

BitFile::BitFile(BitFile&& other) noexcept
    : file_(std::exchange(other.file_, nullptr)), owns_file_(std::exchange(other.owns_file_, false)),
      writing_(std::exchange(other.writing_, false)), rbuf_(std::exchange(other.rbuf_, 0)),
      rcnt_(std::exchange(other.rcnt_, 0)), wbuf_(std::exchange(other.wbuf_, 0)),
      wcnt_(std::exchange(other.wcnt_, 0)), iobuf_(std::move(other.iobuf_))
{
}

BitFile& BitFile::operator=(BitFile&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    close();
    file_ = std::exchange(other.file_, nullptr);
    owns_file_ = std::exchange(other.owns_file_, false);
    writing_ = std::exchange(other.writing_, false);
    rbuf_ = std::exchange(other.rbuf_, 0);
    rcnt_ = std::exchange(other.rcnt_, 0);
    wbuf_ = std::exchange(other.wbuf_, 0);
    wcnt_ = std::exchange(other.wcnt_, 0);
    iobuf_ = std::move(other.iobuf_);
    return *this;
}

BitFile::~BitFile()
{
    close();
}

BitFile BitFile::open_read(const char* name)
{
    std::FILE* const file = std::fopen(name, "rb");
    if (file == nullptr)
    {
        return {};
    }
    return BitFile(file, true, false);
}

BitFile BitFile::open_write(const char* name)
{
    std::FILE* const file = std::fopen(name, "wb");
    if (file == nullptr)
    {
        return {};
    }
    return BitFile(file, true, true);
}

BitFile BitFile::stdout_write()
{
    return BitFile(stdout, false, true);
}

void BitFile::flush_write_tail() noexcept
{
    // boffin: refused shifting a 32-bit pending word by 32 when no bits remain
    if (wcnt_ != 0)
    {
        std::array<std::uint8_t, 4> arr{};
        wbuf_ <<= 32 - wcnt_;
        arr[0] = static_cast<std::uint8_t>((wbuf_ >> 24) & 0xff);
        arr[1] = static_cast<std::uint8_t>((wbuf_ >> 16) & 0xff);
        arr[2] = static_cast<std::uint8_t>((wbuf_ >> 8) & 0xff);
        arr[3] = static_cast<std::uint8_t>(wbuf_ & 0xff);
        const std::size_t nbytes = static_cast<std::size_t>(wcnt_ >> 3) + ((wcnt_ & 0x07) != 0 ? 1u : 0u);
        std::fwrite(arr.data(), nbytes, 1, file_);
    }
    std::fflush(file_);
}

void BitFile::close() noexcept
{
    if (file_ == nullptr)
    {
        return;
    }
    if (writing_)
    {
        flush_write_tail();
    }
    else
    {
        std::fflush(file_);
    }
    if (owns_file_)
    {
        std::fclose(file_);
    }
    file_ = nullptr;
    owns_file_ = false;
    writing_ = false;
    iobuf_.reset();
}

std::uint8_t BitFile::read_bit()
{
    if (rcnt_ == 0)
    {
        rbuf_ = 0;
        rbuf_ |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(file_))) << 24;
        rbuf_ |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(file_))) << 16;
        rbuf_ |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(file_))) << 8;
        rbuf_ |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(file_)));
        rcnt_ = 32;
    }
    rcnt_--;
    return (rbuf_ & (1UL << rcnt_)) != 0;
}

void BitFile::write_bit(std::uint8_t bit)
{
    if (wcnt_ == 32)
    {
        std::fputc(static_cast<std::uint8_t>((wbuf_ >> 24) & 0xff), file_);
        std::fputc(static_cast<std::uint8_t>((wbuf_ >> 16) & 0xff), file_);
        std::fputc(static_cast<std::uint8_t>((wbuf_ >> 8) & 0xff), file_);
        std::fputc(static_cast<std::uint8_t>(wbuf_ & 0xff), file_);
        wcnt_ = 0;
    }
    wcnt_++;
    wbuf_ <<= 1;
    wbuf_ |= static_cast<std::uint32_t>(bit);
}

std::int32_t filesize(std::FILE* file)
{
    if (std::fseek(file, 0L, SEEK_END) != 0)
    {
        return -1;
    }
    const long size = std::ftell(file);
    if (size < 0)
    {
        std::fseek(file, 0L, SEEK_SET);
        return -1;
    }
    if (std::fseek(file, 0L, SEEK_SET) != 0)
    {
        return -1;
    }
    return static_cast<std::int32_t>(size);
}

} // namespace zbb
