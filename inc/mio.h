#pragma once

#include <array>
#include <cstdint>
#include <cstdio>

namespace zbb {

constexpr int SBUFF = 4096;

struct bfile
{
    std::FILE* file = nullptr;
    std::uint32_t rbuf = 0;
    std::uint8_t rcnt = 0;
    std::uint32_t wbuf = 0;
    std::uint8_t wcnt = 0;
    std::array<std::uint8_t, SBUFF> buff{};
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
