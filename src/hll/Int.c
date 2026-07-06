/* v14 "Int" HLL library — integer formatting functions */

#include <stdio.h>
#include <string.h>
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "hll.h"

// [0] string ToString(ref int self, int zeroPadding)
static struct string *Int_ToString(int *self, int zeroPadding)
{
	char buf[64];
	int val = self ? *self : 0;
	if (zeroPadding > 0) {
		snprintf(buf, sizeof(buf), "%0*d", zeroPadding, val);
	} else {
		snprintf(buf, sizeof(buf), "%d", val);
	}
	return cstr_to_string(buf);
}

// [1] string ToWideString(ref int self, int zeroPadding)
static struct string *Int_ToWideString(int *self, int zeroPadding)
{
	// Wide = zenkaku digits. First format as half-width, then convert.
	char buf[64];
	int val = self ? *self : 0;
	if (zeroPadding > 0) {
		snprintf(buf, sizeof(buf), "%0*d", zeroPadding, val);
	} else {
		snprintf(buf, sizeof(buf), "%d", val);
	}
	// Convert to zenkaku
	char wide[256];
	int_to_cstr(wide, sizeof(wide), val, zeroPadding, zeroPadding > 0, true);
	return cstr_to_string(wide);
}

// [2] string ToHexString(ref int self, int zeroPadding)
static struct string *Int_ToHexString(int *self, int zeroPadding)
{
	char buf[64];
	int val = self ? *self : 0;
	if (zeroPadding > 0) {
		snprintf(buf, sizeof(buf), "%0*X", zeroPadding, (unsigned)val);
	} else {
		snprintf(buf, sizeof(buf), "%X", (unsigned)val);
	}
	return cstr_to_string(buf);
}

// [3] string ToLowerHexString(ref int self, int zeroPadding)
static struct string *Int_ToLowerHexString(int *self, int zeroPadding)
{
	char buf[64];
	int val = self ? *self : 0;
	if (zeroPadding > 0) {
		snprintf(buf, sizeof(buf), "%0*x", zeroPadding, (unsigned)val);
	} else {
		snprintf(buf, sizeof(buf), "%x", (unsigned)val);
	}
	return cstr_to_string(buf);
}

// [4] string ToCharacter(ref int self)
static struct string *Int_ToCharacter(int *self)
{
	int val = self ? *self : 0;
	if (val == 0) return string_ref(&EMPTY_STRING);
	char buf[4];
	if (SJIS_2BYTE(val & 0xFF)) {
		buf[0] = val & 0xFF;
		buf[1] = (val >> 8) & 0xFF;
		buf[2] = '\0';
		return make_string(buf, 2);
	} else {
		buf[0] = val & 0xFF;
		buf[1] = '\0';
		return make_string(buf, 1);
	}
}

HLL_LIBRARY(Int,
	    HLL_EXPORT(ToString, Int_ToString),
	    HLL_EXPORT(ToWideString, Int_ToWideString),
	    HLL_EXPORT(ToHexString, Int_ToHexString),
	    HLL_EXPORT(ToLowerHexString, Int_ToLowerHexString),
	    HLL_EXPORT(ToCharacter, Int_ToCharacter)
	    );
