/*
   The algorithm in this module is almost identical to used in bzip, (c) by
   Julian Seward. The next version of <ecp> will include some original
   algorithm and probably reprocessed lexicographic ordering, found in
   bzip.
*/

#include "inc/bwt.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace zbb {
namespace {

constexpr std::uint32_t C_B = 0x7fffffff;
constexpr std::uint32_t M_B = 0x80000000;

} // namespace

void BwtWorkspace::ensure()
{
    sbkt.resize(65537);
    sbm.resize(65536);
}

std::uint32_t BwtWorkspace::transform(std::span<std::uint8_t> data, std::span<std::uint32_t> idxs)
{
    ensure();
    const auto len = static_cast<std::uint32_t>(data.size());
    const std::uint8_t* const pb = data.data();

    // boffin: cut the CRT qsort calls and their thread-local comparator context;
    // rotation order now comes from std::sort with captures, and equal-frequency
    // groups keep a deterministic order on every platform via stable_sort
    const auto rotation_less = [pb, len](std::uint32_t a, std::uint32_t b) {
        std::uint32_t remaining = len - 2;
        while (pb[a] == pb[b] && remaining != 0)
        {
            if (++a == len)
            {
                a = 0;
            }
            if (++b == len)
            {
                b = 0;
            }
            remaining--;
        }
        return remaining != 0 && pb[a] < pb[b];
    };

    std::fill(sbkt.begin(), sbkt.end(), 0u);

    for (std::uint32_t i = 0; i < len - 1; i++)
    {
        sbkt[(static_cast<std::uint16_t>(pb[i]) << 8) | static_cast<std::uint16_t>(pb[i + 1])]++;
    }
    sbkt[(static_cast<std::uint16_t>(pb[len - 1]) << 8) | static_cast<std::uint16_t>(pb[0])]++;

    for (std::uint32_t i = 0x0001; i < 0x10001; i++)
    {
        sbkt[i] += sbkt[i - 1];
    }

    for (std::uint32_t i = 0; i < len - 2; i++)
    {
        idxs[--sbkt[(static_cast<std::uint16_t>(pb[i]) << 8) | static_cast<std::uint16_t>(pb[i + 1])]] = i + 2;
    }
    idxs[--sbkt[(static_cast<std::uint16_t>(pb[len - 2]) << 8) | static_cast<std::uint16_t>(pb[len - 1])]] = 0;
    idxs[--sbkt[(static_cast<std::uint16_t>(pb[len - 1]) << 8) | static_cast<std::uint16_t>(pb[0])]] = 1;

    std::array<std::uint8_t, 256> ord{};
    for (std::uint32_t i = 0; i < 256; i++)
    {
        v[i] = sbkt[(i + 1) << 8] - sbkt[i << 8];
        ord[i] = static_cast<std::uint8_t>(i);
    }
    std::ranges::stable_sort(ord, {}, [this](std::uint8_t ch) { return v[ch]; });

    std::array<std::uint8_t, 256> mask{};
    std::array<std::uint8_t, 256> st_mask{};
    std::fill(sbm.begin(), sbm.end(), 0u);

    for (std::uint32_t i = 0; i < 256; i++)
    {
        const std::uint32_t i1 = ord[i];
        const std::uint32_t i1_hi = i1 << 8;

        if ((sbkt[i1_hi + 256] & C_B) - (sbkt[i1_hi] & C_B) > 1)
        {
            for (std::uint32_t j = 0; j < 256; j++)
            {
                const std::uint32_t k = i1_hi | j;
                if ((sbkt[k] & M_B) == 0)
                {
                    const std::uint32_t from = sbkt[k];
                    const std::uint32_t to = sbkt[k + 1] & C_B;
                    if (to - from > 1)
                    {
                        std::sort(
                            idxs.begin() + static_cast<std::ptrdiff_t>(from),
                            idxs.begin() + static_cast<std::ptrdiff_t>(to),
                            rotation_less);
                    }
                }
            }
        }

        std::uint32_t ptr = 0;
        for (std::uint32_t j = sbkt[i1_hi] & C_B; j < (sbkt[i1_hi + 256] & C_B); j++)
        {
            const std::uint32_t k = (idxs[j] >= 3) ? idxs[j] - 3 : len + idxs[j] - 3;
            if (!mask[pb[k]])
            {
                mask[pb[k]] = 1;
                st_mask[ptr++] = pb[k];
            }
            const auto vtmp0 =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(pb[k]) << 8) | static_cast<std::uint16_t>(i1));
            idxs[(sbkt[vtmp0] & C_B) + sbm[vtmp0]] = (idxs[j] >= 1) ? idxs[j] - 1 : len + idxs[j] - 1;
            sbm[vtmp0]++;
        }
        while (ptr)
        {
            mask[st_mask[--ptr]] = 0;
            sbm[(static_cast<std::uint16_t>(st_mask[ptr]) << 8) | i1] = 0;
            sbkt[(static_cast<std::uint16_t>(st_mask[ptr]) << 8) | i1] |= M_B;
        }
    }

    const std::uint32_t first_bucket = (static_cast<std::uint16_t>(pb[0]) << 8) | static_cast<std::uint16_t>(pb[1]);
    std::uint32_t i = sbkt[first_bucket] & C_B;
    const std::uint32_t j = sbkt[first_bucket + 1] & C_B;
    while (i < j && idxs[i] != 2)
    {
        i++;
    }
    return i;
}

void BwtWorkspace::unbwt(
    std::uint32_t str_pos,
    std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output,
    std::span<std::uint32_t> idxs)
{
    const auto len = static_cast<std::int32_t>(input.size());
    v.fill(0);

    for (std::int32_t i = 0; i < len; i++)
    {
        v[input[i]]++;
    }
    for (std::int32_t i = 1; i < 256; i++)
    {
        v[i] += v[i - 1];
    }
    for (std::int32_t i = len - 1; i >= 0; i--)
    {
        idxs[--v[input[i]]] = static_cast<std::uint32_t>(i);
    }

    for (std::int32_t i = 0; i < len; i++)
    {
        output[i] = input[str_pos = idxs[str_pos]];
    }
}

} // namespace zbb
