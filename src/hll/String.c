/* v14 "String" HLL library — string container operations */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"
#include "xsystem4.h"

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

// [14] bool Search(ref string self, wrap<array<string>> matchList, string regex)
// v14: AIN_WRAP — matchList is 1-slot heap index.
static bool String_Search(struct string **self, int ml_slot, struct string *regex)
{
	return String_SearchAll(self, ml_slot, regex);
}

// [15] bool Search(ref string self, ref array<string> matchList, string regex)
// [16] bool SearchAll(ref string self, wrap<array<string>> matchList, string regex)
//
// AIN library signature: wrap<array<string>> matchList (flat string array).
// The game uses regex: ([^\[\]]+?)+|\[[^\]]+?\]
// This is a bracket tokenizer: match [bracketed] tokens and non-bracket text.
// v14: ml_slot is either a wrap STRUCT_PAGE slot or a direct ARRAY_PAGE slot.
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

	// Build flat array<string> of tokens (matches AIN signature: wrap<array<string>>)
	struct page *flat = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, count);
	flat->array.rank = 1;

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
			struct string *token = make_string(text + start, end - start);
			flat->values[idx].i = heap_alloc_string(token);
			idx++;
		}
	}

	// Write flat array back to ml_slot.
	// ml_slot may be a wrap STRUCT_PAGE (values[0] = inner array slot)
	// OR a direct ARRAY_PAGE heap slot (local array<string> variable).
	if (ml_slot > 0 && (size_t)ml_slot < heap_size
	    && heap[ml_slot].type == VM_PAGE && heap[ml_slot].page
	    && heap[ml_slot].page->type == STRUCT_PAGE
	    && heap[ml_slot].page->nr_vars >= 1) {
		// wrap<array<string>>: store flat array as the wrap's inner slot
		wrap_set_slot(ml_slot, 0, heap_alloc_page(flat));
	} else if (ml_slot > 0 && (size_t)ml_slot < heap_size
	           && heap[ml_slot].type == VM_PAGE) {
		// Direct array slot: replace the page in-place
		heap_set_page(ml_slot, flat);
	} else {
		free_page(flat);
	}
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

/*
 * ---- Overload-aware String implementations (selected by string_select_function) ----
 *
 * Reference: CN dump dohnadohna_dump_SCY.exe, String dispatcher 0x684120,
 * jump table 0x684984 (index = AIN String function index).
 */

// Lead-byte rule for code that does not hand character indices back to the
// game (Trim/Pad/regex). The CN EXE tests 0x81..0xFE (GBK lead) in every
// String helper, e.g. 0x68832f (GetPart), 0x686914 (Length), 0x686352 (Trim
// charset). Non-GB games keep the SJIS rule.
static int string_lib_char_bytes(const uint8_t *p, int remain)
{
	if (remain <= 0)
		return 0;
	bool lead = ain_is_gb18030 ? (p[0] >= 0x81 && p[0] <= 0xFE) : SJIS_2BYTE(p[0]);
	return (lead && remain >= 2) ? 2 : 1;
}

static uint32_t string_lib_char_code(const uint8_t *p, int remain, int *bytes)
{
	int n = string_lib_char_bytes(p, remain);
	*bytes = n;
	return n == 2 ? ((uint32_t)p[0] << 8) | p[1] : p[0];
}

/*
 * GetPart(begin, length), EXE 0x6882c0:
 *   begin < 0 -> 0 (cmovs at 0x6882fe); length <= 0 -> empty (cmovg at 0x688309);
 *   begin past the last char -> empty; end past the last char -> to the end.
 * GetPart(index) is 0x688290 = GetPart(index, 0x7fffffff), i.e. to the end.
 * Character stepping deliberately uses SJIS_2BYTE, the same rule as
 * sjis_index/sjis_count_char/String_FindLast/string_find, so indices produced
 * by Length/Find/FindLast stay valid here. Switching to the GBK rule has to
 * happen for all of them at once (see open questions).
 */
static struct string *string_get_part(const struct string *s, int begin, int length)
{
	if (!s || s->size <= 0)
		return string_ref(&EMPTY_STRING);
	if (begin < 0)
		begin = 0;
	long long end = (long long)begin + (length > 0 ? length : 0);
	int b = -1, e = -1;
	long long c = 0;
	for (int i = 0; i < s->size; c++) {
		if (c == begin)
			b = i;
		if (c == end) {
			e = i;
			break;
		}
		i += (SJIS_2BYTE((uint8_t)s->text[i]) && i + 1 < s->size) ? 2 : 1;
	}
	if (b < 0)
		return string_ref(&EMPTY_STRING);
	if (e < 0)
		e = s->size;
	if (e <= b)
		return string_ref(&EMPTY_STRING);
	return make_string(s->text + b, e - b);
}

// [22] string GetPart(ref string self, int index, int length)
static struct string *String_GetPart(struct string **self, int begin, int length)
{
	return string_get_part(SELF_STR(self), begin, length);
}

// [21] string GetPart(ref string self, int index) -- index to end of string
static struct string *String_GetPartToEnd(struct string **self, int begin)
{
	return string_get_part(SELF_STR(self), begin, INT_MAX);
}

/*
 * PadLeft/PadRight(byteLength, paddingChar), EXE 0x6883d0 / 0x688640.
 * The one-argument forms are the same call with paddingChar = 0x20.
 * pad = byteLength - byte size of self; pad <= 0 returns self unchanged.
 * If (paddingChar >> 8) & 0xff is zero the low byte is repeated pad times;
 * otherwise the 2-byte char (high byte first, then low byte) is repeated
 * pad / 2 times, so an odd pad leaves the result one byte short.
 * self itself is never modified.
 */
static struct string *string_pad(struct string **self, int byte_length, int pad_char, bool left)
{
	struct string *s = SELF_STR(self);
	if (!s)
		return string_ref(&EMPTY_STRING);
	int pad = byte_length - s->size;
	if (pad <= 0)
		return string_ref(s);
	uint8_t hi = (pad_char >> 8) & 0xff;
	uint8_t lo = pad_char & 0xff;
	int fill = hi ? (pad / 2) * 2 : pad;
	struct string *r = string_alloc(s->size + fill);
	char *dst = r->text + (left ? 0 : s->size);
	for (int i = 0; i < fill; i++)
		dst[i] = hi ? ((i & 1) ? lo : hi) : lo;
	memcpy(r->text + (left ? fill : 0), s->text, s->size);
	r->text[s->size + fill] = '\0';
	return r;
}

static struct string *String_PadLeft(struct string **self, int byte_length)
{
	return string_pad(self, byte_length, ' ', true);
}

static struct string *String_PadLeftChar(struct string **self, int byte_length, int pad_char)
{
	return string_pad(self, byte_length, pad_char, true);
}

static struct string *String_PadRight(struct string **self, int byte_length)
{
	return string_pad(self, byte_length, ' ', false);
}

static struct string *String_PadRightChar(struct string **self, int byte_length, int pad_char)
{
	return string_pad(self, byte_length, pad_char, false);
}

/*
 * Trim family, EXE 0x688f10 / 0x6890c0 / 0x689230 (charList forms); the
 * no-argument forms pass the 8-byte default list at 0x7e5f5c/0x7e5f30/0x7e5f24:
 * 20 0c 0a 0d 09 0b 81 40. charList is a set of characters (1 or 2 bytes,
 * built by 0x6862a0). An empty charList trims nothing. The result is a new
 * string; self is NOT modified (the old do_trim wrote the result back).
 */
static const uint8_t string_trim_default[] = { 0x20, 0x0c, 0x0a, 0x0d, 0x09, 0x0b, 0x81, 0x40 };

static bool string_trim_set_has(const uint8_t *set, int set_len, uint32_t code)
{
	for (int i = 0; i < set_len; ) {
		int n;
		if (string_lib_char_code(set + i, set_len - i, &n) == code)
			return true;
		i += n;
	}
	return false;
}

static struct string *string_trim(struct string **self, const uint8_t *set, int set_len,
				  bool trim_start, bool trim_end)
{
	struct string *s = SELF_STR(self);
	if (!s)
		return string_ref(&EMPTY_STRING);
	const uint8_t *t = (const uint8_t *)s->text;
	int b = 0, e = s->size;
	if (set && set_len > 0) {
		if (trim_start) {
			while (b < s->size) {
				int n;
				uint32_t c = string_lib_char_code(t + b, s->size - b, &n);
				if (!string_trim_set_has(set, set_len, c))
					break;
				b += n;
			}
		}
		if (trim_end) {
			// 0x6865b0: walk forward, remember the end of the last char not in the set
			int last = b;
			for (int i = b; i < s->size; ) {
				int n;
				uint32_t c = string_lib_char_code(t + i, s->size - i, &n);
				if (!string_trim_set_has(set, set_len, c))
					last = i + n;
				i += n;
			}
			e = last;
		}
	}
	if (b == 0 && e == s->size)
		return string_ref(s);
	if (e <= b)
		return string_ref(&EMPTY_STRING);
	return make_string(s->text + b, e - b);
}

#define TRIM_ARGS(chars) \
	(chars) ? (const uint8_t *)(chars)->text : NULL, (chars) ? (chars)->size : 0

// [29]/[30] Trim
static struct string *String_Trim(struct string **self)
{
	return string_trim(self, string_trim_default, sizeof(string_trim_default), true, true);
}
static struct string *String_TrimChars(struct string **self, struct string *chars)
{
	return string_trim(self, TRIM_ARGS(chars), true, true);
}
// [31]/[32] TrimStart
static struct string *String_TrimStart(struct string **self)
{
	return string_trim(self, string_trim_default, sizeof(string_trim_default), true, false);
}
static struct string *String_TrimStartChars(struct string **self, struct string *chars)
{
	return string_trim(self, TRIM_ARGS(chars), true, false);
}
// [33]/[34] TrimEnd
static struct string *String_TrimEnd(struct string **self)
{
	return string_trim(self, string_trim_default, sizeof(string_trim_default), false, true);
}
static struct string *String_TrimEndChars(struct string **self, struct string *chars)
{
	return string_trim(self, TRIM_ARGS(chars), false, true);
}
#undef TRIM_ARGS

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

/*
 * Pick the implementation for one String declaration. Overloads share a
 * name, so the static table only supplies a default; shapes not handled
 * here keep it.
 */
void *string_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (!f || !f->name || (f->nr_arguments > 0 && !f->arguments))
		return dflt;
	int n = f->nr_arguments;
	enum ain_data_type a1 = n > 1 ? f->arguments[1].type.data : AIN_VOID;
	enum ain_data_type a2 = n > 2 ? f->arguments[2].type.data : AIN_VOID;
	if (!strcmp(f->name, "GetPart")) {
		if (n == 2 && a1 == AIN_INT)
			return (void *)String_GetPartToEnd;
		if (n == 3 && a1 == AIN_INT && a2 == AIN_INT)
			return (void *)String_GetPart;
	} else if (!strcmp(f->name, "PadLeft")) {
		if (n == 2 && a1 == AIN_INT)
			return (void *)String_PadLeft;
		if (n == 3 && a1 == AIN_INT && a2 == AIN_INT)
			return (void *)String_PadLeftChar;
	} else if (!strcmp(f->name, "PadRight")) {
		if (n == 2 && a1 == AIN_INT)
			return (void *)String_PadRight;
		if (n == 3 && a1 == AIN_INT && a2 == AIN_INT)
			return (void *)String_PadRightChar;
	} else if (!strcmp(f->name, "Trim")) {
		if (n == 1)
			return (void *)String_Trim;
		if (n == 2 && a1 == AIN_STRING)
			return (void *)String_TrimChars;
	} else if (!strcmp(f->name, "TrimStart")) {
		if (n == 1)
			return (void *)String_TrimStart;
		if (n == 2 && a1 == AIN_STRING)
			return (void *)String_TrimStartChars;
	} else if (!strcmp(f->name, "TrimEnd")) {
		if (n == 1)
			return (void *)String_TrimEnd;
		if (n == 2 && a1 == AIN_STRING)
			return (void *)String_TrimEndChars;
	}
	return dflt;
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
