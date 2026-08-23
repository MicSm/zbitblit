#pragma once

#include <cstdint>
#include <cstdio>

namespace zbb {

constexpr int SBUFF = 4096;

struct bfile
{
    std::FILE* file;
    std::uint32_t rbuf;
    std::uint8_t rcnt;
    std::uint32_t wbuf;
    std::uint8_t wcnt;
    std::uint8_t buff[SBUFF];
};

[[nodiscard]] std::int32_t filesize(std::FILE* file);

bfile* bfopen_as_stdout();

// boffin: refused a writable pointer to the open-mode literals the caller passes
bfile* bfopen(const char* name, const char* mode);

std::uint8_t bfread(bfile* bf);

void bfwrite(std::uint8_t bit, bfile* bf);

void w_bfclose(bfile* bf);

void w_bfclose_as_stdout(bfile* bf);

void r_bfclose_as_stdout(bfile* bf);

void r_bfclose(bfile* bf);

} // namespace zbb
