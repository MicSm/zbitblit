#include "inc/sort3.h"

#include <array>
#include <utility>

namespace zbb {

void qsort4(std::uint32_t* base, long nelem, int (*fcmp)(std::uint32_t, std::uint32_t, void*), void* ctx)
{
    const auto cmp = [&](std::uint32_t lhs, std::uint32_t rhs) { return fcmp(lhs, rhs, ctx); };
    const auto swp = [&](long lhs, long rhs) { std::swap(base[lhs], base[rhs]); };

    std::array<long, 32> stack_start{};
    std::array<long, 32> stack_end{};
    long left = 0;
    long right = 0;
    long num = 0;
    long j = 0;
    long i = 0;
    long middle = 0;
    long stack_on = 0;

    stack_start[0] = 0;
    stack_end[0] = nelem - 1;

    while (stack_on >= 0)
    {
        left = stack_start[stack_on];
        right = stack_end[stack_on];
        stack_on--;

        while (left < right)
        {
            num = right - left;
            if (num < 2)
            {
                if (num == 1)
                    if (cmp(base[left], base[right]) > 0)
                        swp(left, right);
                break;
            }

            middle = (right + left) >> 1;

            if (cmp(base[middle], base[right]) > 0)
                swp(middle, right);

            if (cmp(base[middle], base[left]) > 0)
                swp(left, middle);
            else if (cmp(base[left], base[right]) > 0)
                swp(left, right);

            if (num == 2)
            {
                swp(left, middle);
                break;
            }

            i = left + 1;
            while (cmp(base[left], base[i]) > 0)
                i++;

            j = right;
            while (cmp(base[j], base[left]) > 0)
                j--;

            while (i < j)
            {
                swp(i, j);
                i++;
                while (cmp(base[left], base[i]) > 0)
                    i++;
                j--;
                while (cmp(base[j], base[left]) > 0)
                    j--;
            }

            swp(left, j);

            if (j - left > right - j)
            {
                stack_start[++stack_on] = left;
                stack_end[stack_on] = j - 1;
                left = j + 1;
            }
            else
            {
                stack_start[++stack_on] = j + 1;
                stack_end[stack_on] = right;
                right = j - 1;
            }
        }
    }
}

} // namespace zbb
