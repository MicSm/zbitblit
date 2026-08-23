/*
   The algorithm in this module is almost identical to used in bzip, (c) by
   Julian Seward. The next version of <ecp> will include some original
   algorithm and probably reprocessed lexicographic ordering, found in
   bzip.
*/

#include "inc/bwt.h"
#include "inc/sort3.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace zbb {
namespace {

constexpr std::uint32_t C_B = 0x7fffffff;
constexpr std::uint32_t M_B = 0x80000000;

thread_local const std::uint32_t* tls_ord_freq = nullptr;

struct OrdFreqGuard
{
    explicit OrdFreqGuard(const std::uint32_t* freq) noexcept
    {
        tls_ord_freq = freq;
    }

    ~OrdFreqGuard()
    {
        tls_ord_freq = nullptr;
    }

    OrdFreqGuard(const OrdFreqGuard&) = delete;
    OrdFreqGuard& operator=(const OrdFreqGuard&) = delete;
};

int fcmp1(const void* lhs, const void* rhs)
{
    const std::int64_t elem_l = static_cast<std::int64_t>(tls_ord_freq[*static_cast<const std::uint8_t*>(lhs)]);
    const std::int64_t elem_r = static_cast<std::int64_t>(tls_ord_freq[*static_cast<const std::uint8_t*>(rhs)]);
    return static_cast<int>(elem_l - elem_r);
}

int fcmp2(std::uint32_t a, std::uint32_t b, void* ctx)
{
    auto& ws = *static_cast<BwtWorkspace*>(ctx);
    std::uint32_t remaining = ws.scan_len;
    while (ws.scan_buf[a] == ws.scan_buf[b] && remaining)
    {
        if (++a == (ws.scan_len + 2))
            a = 0;
        if (++b == (ws.scan_len + 2))
            b = 0;
        remaining--;
    }
    if (remaining)
        if (ws.scan_buf[a] < ws.scan_buf[b])
            return -1;
        else
            return 1;
    return 0;
}

} // namespace

void BwtWorkspace::ensure()
{
    sbkt.resize(65537);
    sbm.resize(65536);
}

std::uint32_t BwtWorkspace::transform(std::uint32_t len, std::uint8_t* pb, std::uint32_t* idxs)
{
    ensure();
    auto& sbkt = this->sbkt;
    auto& sbm = this->sbm;
    auto& v = this->v;

    std::uint32_t i = 0;
    std::uint32_t ptr = 0;
    std::uint32_t j = 0;
    std::uint32_t k = 0;
    std::array<std::uint8_t, 256> mask{};
    std::array<std::uint8_t, 256> st_mask{};
    std::array<std::uint8_t, 256> ord{};
    std::uint16_t vtmp0 = 0;

    scan_len = len - 2;
    scan_buf = pb;
    std::fill(sbkt.begin(), sbkt.end(), 0);

    for (i = 0; i < len - 1; i++)
        sbkt[(static_cast<std::uint16_t>(pb[i]) << 8) | static_cast<std::uint16_t>(pb[i + 1])]++;
    sbkt[(static_cast<std::uint16_t>(pb[len - 1]) << 8) | static_cast<std::uint16_t>(pb[0])]++;

    for (i = 0x0001; i < 0x10001; i++)
        sbkt[i] += sbkt[i - 1];

    for (i = 0; i < len - 2; i++)
        idxs[--sbkt[(static_cast<std::uint16_t>(pb[i]) << 8) | static_cast<std::uint16_t>(pb[i + 1])]] = i + 2;
    idxs[--sbkt[(static_cast<std::uint16_t>(pb[len - 2]) << 8) | static_cast<std::uint16_t>(pb[len - 1])]] = 0;
    idxs[--sbkt[(static_cast<std::uint16_t>(pb[len - 1]) << 8) | static_cast<std::uint16_t>(pb[0])]] = 1;

    for (i = 0; i < 256; i++)
    {
        v[i] = sbkt[(i + 1) << 8] - sbkt[i << 8];
        ord[i] = static_cast<std::uint8_t>(i);
    }
    {
        // boffin: kept CRT qsort so equal-frequency buckets stay in the same order
        const OrdFreqGuard guard(v.data());
        std::qsort(ord.data(), 256, sizeof(ord[0]), fcmp1);
    }

    std::fill(mask.begin(), mask.end(), 0);
    std::fill(sbm.begin(), sbm.end(), 0);

    for (i = 0; i < 256; i++)
    {
        const std::uint32_t i1 = ord[i];
        const std::uint32_t i1_hi = i1 << 8;

        if ((sbkt[i1_hi + 256] & C_B) - (sbkt[i1_hi] & C_B) > 1)
        {
            for (j = 0; j < 256; j++)
            {
                k = i1_hi | j;
                if ((sbkt[k] & M_B) == 0)
                {
                    const std::uint32_t bucket_size = (sbkt[k + 1] & C_B) - sbkt[k];
                    if (bucket_size > 1)
                    {
                        qsort4(&idxs[sbkt[k]], static_cast<long>((sbkt[k + 1] & C_B) - sbkt[k]), fcmp2, this);
                    }
                }
            }
        }

        ptr = 0;
        for (j = sbkt[i1_hi] & C_B; j < (sbkt[i1_hi + 256] & C_B); j++)
        {
            k = ((idxs[j] >= 3) ? idxs[j] - 3 : len + idxs[j] - 3);
            if (!mask[pb[k]])
            {
                mask[pb[k]] = 1;
                st_mask[ptr++] = pb[k];
            }
            vtmp0 = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pb[k]) << 8) | static_cast<std::uint16_t>(i1));
            idxs[(sbkt[vtmp0] & C_B) + sbm[vtmp0]] = ((idxs[j] >= 1) ? idxs[j] - 1 : len + idxs[j] - 1);
            sbm[vtmp0]++;
        }
        while (ptr)
        {
            mask[st_mask[--ptr]] = 0;
            sbm[(static_cast<std::uint16_t>(st_mask[ptr]) << 8) | i1] = 0;
            sbkt[(static_cast<std::uint16_t>(st_mask[ptr]) << 8) | i1] |= M_B;
        }
    }

    j = (static_cast<std::uint16_t>(pb[0]) << 8) | static_cast<std::uint16_t>(pb[1]);
    i = sbkt[j] & C_B;
    j = (sbkt[j + 1] & C_B);
    while (i < j && idxs[i] != 2)
        i++;
    return i;
}

void BwtWorkspace::unbwt(
    std::uint32_t StrPos,
    std::uint32_t len,
    std::uint8_t* InputBuffer,
    std::uint8_t* OutputBuffer,
    std::uint32_t* idxs)
{
    auto& v = this->v;
    v.fill(0);

    for (std::int32_t i = 0; i < static_cast<std::int32_t>(len); i++)
        v[InputBuffer[i]]++;
    for (std::int32_t i = 1; i < 256; i++)
        v[i] += v[i - 1];
    for (std::int32_t i = static_cast<std::int32_t>(len) - 1; i >= 0; i--)
        idxs[--v[InputBuffer[i]]] = static_cast<std::uint32_t>(i);

    for (std::int32_t i = 0; i < static_cast<std::int32_t>(len); i++)
        OutputBuffer[i] = InputBuffer[StrPos = idxs[StrPos]];
}

} // namespace zbb
