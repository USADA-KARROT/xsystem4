/* v14 "Float" HLL library — float formatting functions */

#include <stdio.h>
#include <string.h>
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "hll.h"

// [0] string ToString(ref float self, int decimal, int zeroPadding)
static struct string *Float_ToString(float *self, int decimal, int zeroPadding)
{
	char buf[128];
	float val = self ? *self : 0.0f;
	if (decimal < 0) decimal = 6;
	if (zeroPadding > 0) {
		snprintf(buf, sizeof(buf), "%0*.*f", zeroPadding, decimal, val);
	} else {
		snprintf(buf, sizeof(buf), "%.*f", decimal, val);
	}
	return cstr_to_string(buf);
}

// [1] string ToWideString(ref float self, int decimal, int zeroPadding)
static struct string *Float_ToWideString(float *self, int decimal, int zeroPadding)
{
	char buf[128];
	float val = self ? *self : 0.0f;
	if (decimal < 0) decimal = 6;
	float_to_cstr(buf, sizeof(buf), val, zeroPadding, zeroPadding > 0, decimal, true);
	return cstr_to_string(buf);
}

HLL_LIBRARY(Float,
	    HLL_EXPORT(ToString, Float_ToString),
	    HLL_EXPORT(ToWideString, Float_ToWideString)
	    );
