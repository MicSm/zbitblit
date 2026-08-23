#pragma once

#include <stdint.h>

// boffin: kept 32-bit index elements so the sorter matches the BWT index array
void qsort4(uint32_t* base, long nelem,
	int (*fcmp)(uint32_t, uint32_t));
