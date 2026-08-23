#pragma once

#include <cstdint>

namespace zbb {

// boffin: kept 32-bit index elements so the sorter matches the BWT index array
void qsort4(std::uint32_t* base, long nelem, int (*fcmp)(std::uint32_t, std::uint32_t));

} // namespace zbb
