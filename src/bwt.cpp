/*
   The algorithm in this module is almost identical to used in bzip, (c) by
   Julian Seward. The next version of <ecp> will include some original
   algorithm and probably reprocessed lexicographic ordering, found in
   bzip.
*/

#include "inc/bwt.h"
#include "inc/sort3.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace zbb {

static std::unique_ptr<std::uint32_t[]> SBck;
static std::unique_ptr<std::uint32_t[]> SBm;
static std::uint32_t v[256];

static std::uint32_t ScanLen;
static std::uint8_t* ScanBuf = nullptr;

constexpr std::uint32_t C_B = 0x7fffffff;
constexpr std::uint32_t M_B = 0x80000000;

int SetupBwtBuffers()
{
    SBck.reset(new (std::nothrow) std::uint32_t[65537]);
    SBm.reset(new (std::nothrow) std::uint32_t[65536]);
    if (SBck && SBm)
    {
        return 1;
    }
    SBck.reset();
    SBm.reset();
    return 0;
}

void FreeBwtBuffers()
{
    SBck.reset();
    SBm.reset();
}

static int fcmp1(const void* lhs, const void* rhs)
{
    const std::int64_t elemL = static_cast<std::int64_t>(v[*static_cast<const std::uint8_t*>(lhs)]);
    const std::int64_t elemR = static_cast<std::int64_t>(v[*static_cast<const std::uint8_t*>(rhs)]);

    return static_cast<int>(elemL - elemR);
}

static int fcmp2(std::uint32_t a, std::uint32_t b)
{
    std::uint32_t c = ScanLen;
    while (ScanBuf[a] == ScanBuf[b] && c)
    {
        if (++a == (ScanLen + 2))
            a = 0;
        if (++b == (ScanLen + 2))
            b = 0;
        c--;
    }
    if (c)
        if (ScanBuf[a] < ScanBuf[b])
            return -1;
        else
            return 1;
    return 0;
}

std::uint32_t BWT_TRANSFORM(std::uint32_t len, std::uint8_t* pb, std::uint32_t* idxs)
{
    std::uint32_t i, ptr;
    std::uint32_t j, k;
    std::uint8_t mask[256], st_mask[256];
    std::uint8_t ord[256];

    std::uint16_t vtmp0;

    ScanLen = len - 2;
    ScanBuf = pb;
    /* clean buckets */
    std::memset(SBck.get(), 0, 65537 * sizeof(std::uint32_t));

    /* calculate buckets */
    for (i = 0; i < len - 1; i++)
        SBck[(((std::uint16_t)pb[i]) << 8) | (std::uint16_t)pb[i + 1]]++;
    SBck[(((std::uint16_t)pb[len - 1]) << 8) | (std::uint16_t)pb[0]]++;

    /* fill indexes according to the buckets */
    for (i = 0x0001; i < 0x10001; i++)
        SBck[i] += SBck[i - 1];

    for (i = 0; i < len - 2; i++)
        idxs[--SBck[(((std::uint16_t)pb[i]) << 8) | (std::uint16_t)pb[i + 1]]] = i + 2;
    idxs[--SBck[(((std::uint16_t)pb[len - 2]) << 8) | (std::uint16_t)pb[len - 1]]] = 0;
    idxs[--SBck[(((std::uint16_t)pb[len - 1]) << 8) | (std::uint16_t)pb[0]]] = 1;

    /* sort buckets on value criteria */
    for (i = 0; i < 256; i++)
    {
        v[i] = SBck[(i + 1) << 8] - SBck[i << 8];
        ord[i] = static_cast<std::uint8_t>(i);
    }
    std::qsort(ord, 256, sizeof(*ord), fcmp1);

    /* now, begin sort buckets */
    std::memset(mask, 0, 256 * sizeof(mask[0]));
    std::memset(SBm.get(), 0, 65536 * sizeof(std::uint32_t));

    for (i = 0; i < 256; i++)
    {
        const std::uint32_t i1 = ord[i];
        const std::uint32_t i1_hi = i1 << 8;

        if ((SBck[i1_hi + 256] & C_B) - (SBck[i1_hi] & C_B) > 1)
        {
            /* sort this big bucket */
            for (j = 0; j < 256; j++)
            {
                k = i1_hi | j;
                if ((SBck[k] & M_B) == 0)
                {
                    const std::uint32_t bucket_size = (SBck[k + 1] & C_B) - SBck[k];
                    if (bucket_size > 1)
                    {
                        qsort4(&idxs[SBck[k]], (SBck[k + 1] & C_B) - SBck[k], fcmp2);
                    }
                }
            }
        }

        /* setup sorted order for small buckets */
        ptr = 0;
        for (j = SBck[i1_hi] & C_B; j < (SBck[i1_hi + 256] & C_B); j++)
        {
            k = ((idxs[j] >= 3) ? idxs[j] - 3 : len + idxs[j] - 3);
            if (!mask[pb[k]])
            {
                mask[pb[k]] = 1;
                st_mask[ptr++] = pb[k];
            }
            vtmp0 = static_cast<std::uint16_t>((((std::uint16_t)pb[k]) << 8) | (std::uint16_t)i1);
            idxs[(SBck[vtmp0] & C_B) + SBm[vtmp0]] = ((idxs[j] >= 1) ? idxs[j] - 1 : len + idxs[j] - 1);
            SBm[vtmp0]++;
        }
        while (ptr)
        {
            mask[st_mask[--ptr]] = 0;
            SBm[(((std::uint16_t)st_mask[ptr]) << 8) | i1] = 0;
            SBck[(((std::uint16_t)st_mask[ptr]) << 8) | i1] |= M_B;
        }
    }
    /* find source string position */
    j = (((std::uint16_t)pb[0]) << 8) | ((std::uint16_t)pb[1]);
    i = SBck[j] & C_B;
    j = (SBck[j + 1] & C_B);
    while (i < j && idxs[i] != 2)
        i++;
    return i;
}

void UnBWT(
    std::uint32_t StrPos,
    std::uint32_t len,
    std::uint8_t* InputBuffer,
    std::uint8_t* OutputBuffer,
    std::uint32_t* idxs)
{
    std::int32_t i;

    /* clean buffer */
    for (i = 0; i < 256; i++)
        v[i] = 0;

    /* calc stat */
    for (i = 0; i < static_cast<std::int32_t>(len); i++)
        v[InputBuffer[i]]++;
    for (i = 1; i < 256; i++)
        v[i] += v[i - 1];
    for (i = static_cast<std::int32_t>(len) - 1; i >= 0; i--)
        idxs[--v[InputBuffer[i]]] = static_cast<std::uint32_t>(i);

    /* do reverse BWT transform */
    for (i = 0; i < static_cast<std::int32_t>(len); i++)
        OutputBuffer[i] = InputBuffer[StrPos = idxs[StrPos]];
}

} // namespace zbb
