#include "inc/mio.h"

#include <array>
#include <memory>

namespace zbb {
namespace {

void init_bit_state(bfile& bf)
{
    bf.rbuf = 0;
    bf.rcnt = 0;
    bf.wbuf = 0;
    bf.wcnt = 0;
}

void attach_file_buffer(bfile& bf)
{
    std::setvbuf(bf.file, reinterpret_cast<char*>(bf.buff.data()), _IOFBF, SBUFF);
}

void destroy_bit_file(bfile* bf)
{
    std::unique_ptr<bfile> owned{bf};
}

void flush_write_tail(bfile* bf)
{
    // boffin: refused shifting a 32-bit pending word by 32 when no bits remain
    if (bf->wcnt != 0)
    {
        std::array<std::uint8_t, 4> arr{};
        bf->wbuf <<= 32 - bf->wcnt;
        arr[0] = static_cast<std::uint8_t>((bf->wbuf >> 24) & 0xff);
        arr[1] = static_cast<std::uint8_t>((bf->wbuf >> 16) & 0xff);
        arr[2] = static_cast<std::uint8_t>((bf->wbuf >> 8) & 0xff);
        arr[3] = static_cast<std::uint8_t>(bf->wbuf & 0xff);
        const std::size_t nbytes = static_cast<std::size_t>(bf->wcnt >> 3) + ((bf->wcnt & 0x07) != 0 ? 1u : 0u);
        std::fwrite(arr.data(), nbytes, 1, bf->file);
    }
    std::fflush(bf->file);
}

} // namespace

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

bfile* bfopen_as_stdout()
{
    auto bf = std::make_unique<bfile>();
    bf->file = stdout;
    attach_file_buffer(*bf);
    init_bit_state(*bf);
    return bf.release();
}

bfile* bfopen(const char* name, const char* mode)
{
    auto bf = std::make_unique<bfile>();
    bf->file = std::fopen(name, mode);
    if (bf->file == nullptr)
    {
        return nullptr;
    }
    attach_file_buffer(*bf);
    init_bit_state(*bf);
    return bf.release();
}

std::uint8_t bfread(bfile* bf)
{
    if (bf->rcnt == 0)
    {
        bf->rbuf = 0;
        bf->rbuf |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(bf->file))) << 24;
        bf->rbuf |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(bf->file))) << 16;
        bf->rbuf |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(bf->file))) << 8;
        bf->rbuf |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(std::fgetc(bf->file)));
        bf->rcnt = 32;
    }
    bf->rcnt--;
    return (bf->rbuf & (1UL << bf->rcnt)) != 0;
}

void bfwrite(std::uint8_t bit, bfile* bf)
{
    if (bf->wcnt == 32)
    {
        std::fputc(static_cast<std::uint8_t>((bf->wbuf >> 24) & 0xff), bf->file);
        std::fputc(static_cast<std::uint8_t>((bf->wbuf >> 16) & 0xff), bf->file);
        std::fputc(static_cast<std::uint8_t>((bf->wbuf >> 8) & 0xff), bf->file);
        std::fputc(static_cast<std::uint8_t>(bf->wbuf & 0xff), bf->file);
        bf->wcnt = 0;
    }
    bf->wcnt++;
    bf->wbuf <<= 1;
    bf->wbuf |= static_cast<std::uint32_t>(bit);
}

void w_bfclose(bfile* bf)
{
    flush_write_tail(bf);
    std::fclose(bf->file);
    destroy_bit_file(bf);
}

void w_bfclose_as_stdout(bfile* bf)
{
    flush_write_tail(bf);
    destroy_bit_file(bf);
}

void r_bfclose_as_stdout(bfile* bf)
{
    std::fflush(bf->file);
    destroy_bit_file(bf);
}

void r_bfclose(bfile* bf)
{
    std::fflush(bf->file);
    std::fclose(bf->file);
    destroy_bit_file(bf);
}

} // namespace zbb
