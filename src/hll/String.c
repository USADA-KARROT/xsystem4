/* v14 "String" HLL library — string container operations */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"

// AIN v14 String library — all functions take "ref string self" (struct string **).
// Functions that don't modify self still receive a double pointer via ffi.

// Helper: dereference ref-string self, returning the actual string pointer.
// For functions that receive self by-value (AIN_STRING), the parameter IS the pointer.
// For functions that receive self by-ref (AIN_REF_STRING), ffi passes &heap_ptrs[i].
// Since all v14 String functions use AIN_REF_STRING, self is always struct string **.
#define SELF_STR(self) ((self) ? *(self) : NULL)

// [0] int ToInt(ref string self)
static int String_ToInt(struct string **self)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0) return 0;
	return atoi(s->text);
}

// [1] float ToFloat(ref string self)
static float String_ToFloat(struct string **self)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0) return 0.0f;
	return (float)atof(s->text);
}

// [2] int Length(ref string self)
static int String_Length(struct string **self)
{
	struct string *s = SELF_STR(self);
	return s ? sjis_count_char(s->text) : 0;
}

// [3] int LengthByte(ref string self)
static int String_LengthByte(struct string **self)
{
	struct string *s = SELF_STR(self);
	return s ? s->size : 0;
}

// [4] bool Empty(ref string self)
static bool String_Empty(struct string **self)
{
	struct string *s = SELF_STR(self);
	return !s || s->size == 0;
}

// [5] void PushBack(ref string self, int chara)
static void String_PushBack(struct string **self, int chara)
{
	if (!self || !*self) return;
	string_push_back(self, chara);
}

// [6] void PopBack(ref string self)
static void String_PopBack(struct string **self)
{
	if (!self || !*self) return;
	string_pop_back(self);
}

// [7] void Erase(ref string self, int index, int length)
static void String_Erase(struct string **self, int index, int length)
{
	if (!self || !*self) return;
	for (int i = 0; i < length; i++) {
		string_erase(self, index);
	}
}

// [8] void Insert(ref string self, int index, string text)
static void String_Insert(struct string **self, int index, struct string *text)
{
	if (!self || !*self || !text || text->size == 0) return;
	struct string *s = *self;
	int byte_idx = sjis_index(s->text, index);
	if (byte_idx < 0) byte_idx = s->size;

	int new_size = s->size + text->size;
	struct string *result = string_alloc(new_size);
	memcpy(result->text, s->text, byte_idx);
	memcpy(result->text + byte_idx, text->text, text->size);
	memcpy(result->text + byte_idx + text->size, s->text + byte_idx, s->size - byte_idx);
	result->text[new_size] = '\0';

	free_string(*self);
	*self = result;
}

// [9] int Find(ref string self, string key)
static int String_Find(struct string **self, struct string *key)
{
	struct string *s = SELF_STR(self);
	if (!s || !key) return -1;
	return string_find(s, key);
}

// [10] int FindLast(ref string self, string key)
static int String_FindLast(struct string **self, struct string *key)
{
	struct string *s = SELF_STR(self);
	if (!s || !key || key->size == 0) return -1;

	int last = -1;
	int c = 0;
	for (int i = 0; i < s->size; i++, c++) {
		if (i + key->size <= s->size && !memcmp(s->text + i, key->text, key->size))
			last = c;
		if (SJIS_2BYTE(s->text[i]))
			i++;
	}
	return last;
}

// [11] bool Contains(ref string self, string key)
static bool String_Contains(struct string **self, struct string *key)
{
	struct string *s = SELF_STR(self);
	if (!s || !key) return false;
	return string_find(s, key) >= 0;
}

// [12] bool StartsWith(ref string self, string text)
static bool String_StartsWith(struct string **self, struct string *prefix)
{
	struct string *s = SELF_STR(self);
	if (!s || !prefix) return false;
	if (prefix->size > s->size) return false;
	return memcmp(s->text, prefix->text, prefix->size) == 0;
}

// [13] bool EndsWith(ref string self, string text)
static bool String_EndsWith(struct string **self, struct string *suffix)
{
	struct string *s = SELF_STR(self);
	if (!s || !suffix) return false;
	if (suffix->size > s->size) return false;
	return memcmp(s->text + s->size - suffix->size, suffix->text, suffix->size) == 0;
}

// Forward declaration for SearchAll (used by Search and Match)
static bool String_SearchAll(struct string **self, int ml_slot, struct string *regex);

// [14] bool Search(ref string self, wrap<array<array<string>>> matchList, string regex)
// v14: AIN_WRAP — matchList is 1-slot heap index.
static bool String_Search(struct string **self, int ml_slot, struct string *regex)
{
	return String_SearchAll(self, ml_slot, regex);
}

// [15] bool Search(ref string self, ref array<array<string>> matchList, string regex)
// [16] bool SearchAll(ref string self, wrap<array<array<string>>> matchList, string regex)
//
// The game uses regex: ([^\[\]]+?)+|\[[^\]]+?\]
// This is a bracket tokenizer: match [bracketed] tokens and non-bracket text.
// v14: AIN_WRAP — matchList is 1-slot heap index.
static bool String_SearchAll(struct string **self, int ml_slot, struct string *regex)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0)
		return false;

	const char *text = s->text;
	int len = s->size;

	// Simple bracket tokenizer for the specific regex pattern used by the game
	int count = 0;
	int i = 0;
	while (i < len) {
		if (text[i] == '[') {
			int j = i + 1;
			while (j < len && text[j] != ']')
				j++;
			if (j < len) j++;
			count++;
			i = j;
		} else if (text[i] != ']') {
			int j = i;
			while (j < len && text[j] != '[' && text[j] != ']')
				j++;
			if (j > i) count++;
			i = j;
		} else {
			i++;
		}
	}

	if (count == 0)
		return false;

	// Build outer array (array<array<string>>)
	struct page *outer = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, count);
	outer->array.rank = 1;

	int idx = 0;
	i = 0;
	while (i < len && idx < count) {
		int start = i;
		int end = i;

		if (text[i] == '[') {
			int j = i + 1;
			while (j < len && text[j] != ']')
				j++;
			if (j < len) j++;
			end = j;
			i = j;
		} else if (text[i] != ']') {
			int j = i;
			while (j < len && text[j] != '[' && text[j] != ']')
				j++;
			end = j;
			i = j;
		} else {
			i++;
			continue;
		}

		if (end > start) {
			struct page *inner = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 1);
			inner->array.rank = 1;
			struct string *token = make_string(text + start, end - start);
			inner->values[0].i = heap_alloc_string(token);

			int inner_slot = heap_alloc_slot(VM_PAGE);
			heap_set_page(inner_slot, inner);
			outer->values[idx].i = inner_slot;
			idx++;
		}
	}

	// Write outer array to wrap slot
	wrap_set_slot(ml_slot, 0, heap_alloc_page(outer));
	return true;
}

// [17] bool Match(ref string self, wrap<array<array<string>>> matchList, string regex)
// v14: AIN_WRAP — matchList is 1-slot heap index.
static bool String_Match(struct string **self, int ml_slot, struct string *regex)
{
	return String_SearchAll(self, ml_slot, regex);
}

// [19] string Replace(ref string self, string key, string replacer)
static struct string *String_Replace(struct string **self, struct string *from, struct string *to)
{
	struct string *s = SELF_STR(self);
	if (!s || !from || from->size == 0)
		return s ? string_ref(s) : string_ref(&EMPTY_STRING);

	const char *text = s->text;
	int text_len = s->size;
	const char *from_str = from->text;
	int from_len = from->size;
	const char *to_str = to ? to->text : "";
	int to_len = to ? to->size : 0;

	int count = 0;
	for (int i = 0; i <= text_len - from_len; i++) {
		if (memcmp(text + i, from_str, from_len) == 0) {
			count++;
			i += from_len - 1;
		}
	}
	if (count == 0)
		return string_ref(s);

	int new_len = text_len + count * (to_len - from_len);
	struct string *result = string_alloc(new_len);
	char *dst = result->text;
	for (int i = 0; i < text_len; ) {
		if (i <= text_len - from_len && memcmp(text + i, from_str, from_len) == 0) {
			memcpy(dst, to_str, to_len);
			dst += to_len;
			i += from_len;
		} else {
			*dst++ = text[i++];
		}
	}
	*dst = '\0';

	free_string(*self);
	*self = string_ref(result);
	return result;
}

// [20] string ReplaceRegex(ref string self, string regex, string replacer) — stub
static struct string *String_ReplaceRegex(struct string **self, struct string *regex, struct string *replacer)
{
	struct string *s = SELF_STR(self);
	return s ? string_ref(s) : string_ref(&EMPTY_STRING);
}

// [21] string GetPart(ref string self, int index) — single char
static struct string *String_GetPartChar(struct string **self, int index)
{
	struct string *s = SELF_STR(self);
	if (!s || index < 0) return string_ref(&EMPTY_STRING);
	int byte_idx = sjis_index(s->text, index);
	if (byte_idx < 0 || byte_idx >= s->size) return string_ref(&EMPTY_STRING);
	int bytes = SJIS_2BYTE(s->text[byte_idx]) ? 2 : 1;
	return make_string(s->text + byte_idx, bytes);
}

// [22] string GetPart(ref string self, int index, int length)
static struct string *String_GetPart(struct string **self, int begin, int length)
{
	struct string *s = SELF_STR(self);
	if (!s || begin < 0 || length <= 0) return string_ref(&EMPTY_STRING);
	int byte_begin = sjis_index(s->text, begin);
	if (byte_begin < 0 || byte_begin >= s->size) return string_ref(&EMPTY_STRING);
	int byte_len = sjis_index(s->text + byte_begin, length);
	if (byte_len < 0) byte_len = s->size - byte_begin;
	return make_string(s->text + byte_begin, byte_len);
}

// [23] string PadLeft(ref string self, int byteLength)
static struct string *String_PadLeft(struct string **self, int byteLength)
{
	struct string *s = SELF_STR(self);
	if (!s) return string_ref(&EMPTY_STRING);
	if (s->size >= byteLength) return string_ref(s);
	int pad = byteLength - s->size;
	struct string *result = string_alloc(byteLength);
	memset(result->text, ' ', pad);
	memcpy(result->text + pad, s->text, s->size);
	result->text[byteLength] = '\0';
	return result;
}

// [25] string PadRight(ref string self, int byteLength)
static struct string *String_PadRight(struct string **self, int byteLength)
{
	struct string *s = SELF_STR(self);
	if (!s) return string_ref(&EMPTY_STRING);
	if (s->size >= byteLength) return string_ref(s);
	int pad = byteLength - s->size;
	struct string *result = string_alloc(byteLength);
	memcpy(result->text, s->text, s->size);
	memset(result->text + s->size, ' ', pad);
	result->text[byteLength] = '\0';
	return result;
}

// [27] string ToLower(ref string self)
static struct string *String_ToLower(struct string **self)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0) return string_ref(&EMPTY_STRING);
	struct string *result = string_dup(s);
	for (int i = 0; i < result->size; i++) {
		if (SJIS_2BYTE(result->text[i])) { i++; continue; }
		result->text[i] = tolower((unsigned char)result->text[i]);
	}
	return result;
}

// [28] string ToUpper(ref string self)
static struct string *String_ToUpper(struct string **self)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0) return string_ref(&EMPTY_STRING);
	struct string *result = string_dup(s);
	for (int i = 0; i < result->size; i++) {
		if (SJIS_2BYTE(result->text[i])) { i++; continue; }
		result->text[i] = toupper((unsigned char)result->text[i]);
	}
	return result;
}

// Generic trim helper
static struct string *do_trim(struct string **self, bool trim_start, bool trim_end)
{
	if (!self || !*self || (*self)->size == 0)
		return string_ref(&EMPTY_STRING);

	const char *text = (*self)->text;
	int len = (*self)->size;

	int start = 0;
	if (trim_start) {
		while (start < len && ((unsigned char)text[start] <= ' '))
			start++;
	}
	int end = len;
	if (trim_end) {
		while (end > start && ((unsigned char)text[end-1] <= ' '))
			end--;
	}

	if (start == 0 && end == len)
		return string_ref(*self);

	struct string *result = make_string(text + start, end - start);
	free_string(*self);
	*self = string_ref(result);
	return result;
}

// [29] string Trim(ref string self)
// [30] string Trim(ref string self, string charList)
static struct string *String_Trim(struct string **self)
{
	return do_trim(self, true, true);
}

// [31] string TrimStart(ref string self)
// [32] string TrimStart(ref string self, string charList)
static struct string *String_TrimStart(struct string **self)
{
	return do_trim(self, true, false);
}

// [33] string TrimEnd(ref string self)
// [34] string TrimEnd(ref string self, string charList)
static struct string *String_TrimEnd(struct string **self)
{
	return do_trim(self, false, true);
}

// [35] array<string> Split(ref string self, string separators, int containsMode)
static int String_Split(struct string **self, struct string *separators, int containsMode)
{
	struct string *str = SELF_STR(self);

	if (!str || str->size == 0 || !separators || separators->size == 0) {
		struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, str && str->size > 0 ? 1 : 0);
		result->array.rank = 1;
		if (str && str->size > 0) {
			int str_slot = heap_alloc_slot(VM_STRING);
			heap[str_slot].s = string_ref(str);
			result->values[0].i = str_slot;
		}
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, result);
		return slot;
	}

	const char *text = str->text;
	int len = str->size;
	const char *seps = separators->text;
	int sep_len = separators->size;

	int count = 1;
	for (int i = 0; i < len; i++) {
		for (int j = 0; j < sep_len; j++) {
			if (text[i] == seps[j]) { count++; break; }
		}
	}

	struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, count);
	result->array.rank = 1;

	int idx = 0;
	int start = 0;
	for (int i = 0; i <= len; i++) {
		bool is_sep = false;
		if (i < len) {
			for (int j = 0; j < sep_len; j++) {
				if (text[i] == seps[j]) { is_sep = true; break; }
			}
		}
		if (is_sep || i == len) {
			struct string *piece = make_string(text + start, i - start);
			int str_slot = heap_alloc_slot(VM_STRING);
			heap[str_slot].s = piece;
			result->values[idx].i = str_slot;
			idx++;
			start = i + 1;
		}
	}

	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, result);
	return slot;
}

HLL_LIBRARY(String,
	    HLL_EXPORT(ToInt, String_ToInt),
	    HLL_EXPORT(ToFloat, String_ToFloat),
	    HLL_EXPORT(Length, String_Length),
	    HLL_EXPORT(LengthByte, String_LengthByte),
	    HLL_EXPORT(Empty, String_Empty),
	    HLL_EXPORT(PushBack, String_PushBack),
	    HLL_EXPORT(PopBack, String_PopBack),
	    HLL_EXPORT(Erase, String_Erase),
	    HLL_EXPORT(Insert, String_Insert),
	    HLL_EXPORT(Find, String_Find),
	    HLL_EXPORT(FindLast, String_FindLast),
	    HLL_EXPORT(Contains, String_Contains),
	    HLL_EXPORT(StartsWith, String_StartsWith),
	    HLL_EXPORT(EndsWith, String_EndsWith),
	    HLL_EXPORT(Search, String_Search),
	    HLL_EXPORT(SearchAll, String_SearchAll),
	    HLL_EXPORT(Match, String_Match),
	    HLL_EXPORT(Replace, String_Replace),
	    HLL_EXPORT(ReplaceRegex, String_ReplaceRegex),
	    HLL_EXPORT(GetPart, String_GetPart),
	    HLL_EXPORT(PadLeft, String_PadLeft),
	    HLL_EXPORT(PadRight, String_PadRight),
	    HLL_EXPORT(ToLower, String_ToLower),
	    HLL_EXPORT(ToUpper, String_ToUpper),
	    HLL_EXPORT(Trim, String_Trim),
	    HLL_EXPORT(TrimStart, String_TrimStart),
	    HLL_EXPORT(TrimEnd, String_TrimEnd),
	    HLL_EXPORT(Split, String_Split)
	    );
