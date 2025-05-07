#pragma once

void qsort4(unsigned long* base, long nelem,
	int (*fcmp)(unsigned long, unsigned long));

void qsort1(unsigned char* base, long nelem,
	int (*fcmp)(unsigned char, unsigned char));
