#include "inc/mio.h"

#include <cstdlib>

namespace zbb {

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

static void init_bit_state(bfile* bf)
{
    bf->rbuf = 0;
    bf->rcnt = 0;
    bf->wbuf = 0;
    bf->wcnt = 0;
}

static void attach_file_buffer(bfile* bf)
{
    std::setvbuf(bf->file, reinterpret_cast<char*>(bf->buff), _IOFBF, SBUFF);
}

bfile* bfopen_as_stdout()
{
    bfile* const bf = static_cast<bfile*>(std::malloc(sizeof(bfile)));
    if (bf == nullptr)
    {
        return nullptr;
    }
    bf->file = stdout;
    attach_file_buffer(bf);
    init_bit_state(bf);
    return bf;
}

bfile* bfopen(const char* name, const char* mode)
{
    bfile* const bf = static_cast<bfile*>(std::malloc(sizeof(bfile)));
    if (bf == nullptr)
    {
        return nullptr;
    }
    bf->file = std::fopen(name, mode);
    if (bf->file == nullptr)
    {
        std::free(bf);
        return nullptr;
    }
    attach_file_buffer(bf);
    init_bit_state(bf);
    return bf;
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

static void flush_write_tail(bfile* bf)
{
    // boffin: refused shifting a 32-bit pending word by 32 when no bits remain
    if (bf->wcnt != 0)
    {
        std::uint8_t arr[4];
        bf->wbuf <<= 32 - bf->wcnt;
        arr[0] = static_cast<std::uint8_t>((bf->wbuf >> 24) & 0xff);
        arr[1] = static_cast<std::uint8_t>((bf->wbuf >> 16) & 0xff);
        arr[2] = static_cast<std::uint8_t>((bf->wbuf >> 8) & 0xff);
        arr[3] = static_cast<std::uint8_t>(bf->wbuf & 0xff);
        const std::size_t nbytes = static_cast<std::size_t>(bf->wcnt >> 3) + ((bf->wcnt & 0x07) != 0 ? 1u : 0u);
        std::fwrite(arr, nbytes, 1, bf->file);
    }
    std::fflush(bf->file);
}

void w_bfclose(bfile* bf)
{
    flush_write_tail(bf);
    std::fclose(bf->file);
    std::free(bf);
}

void w_bfclose_as_stdout(bfile* bf)
{
    flush_write_tail(bf);
    std::free(bf);
}

void r_bfclose_as_stdout(bfile* bf)
{
    std::fflush(bf->file);
    std::free(bf);
}

void r_bfclose(bfile* bf)
{
    std::fflush(bf->file);
    std::fclose(bf->file);
    std::free(bf);
}

} // namespace zbb
