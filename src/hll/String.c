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

/*
 * GBK character rule. The Chinese EXE decides character boundaries with a
 * lead byte of 0x81..0xFE in every String helper (dispatcher 0x684120, e.g.
 * Length 0x6868f0, Find 0x686e00, GetPart 0x6882c0, Split 0x6892f0) and uses
 * (lead << 8) | trail character codes. libsys4 switches its string_* and
 * mbcs_* helpers with sys4_set_string_charset(), which the engine turns on
 * for GB18030 AINs (gbk_string_rules_enable). Every function below that
 * behaves differently under that rule has an early GBK branch; the code after
 * it is the unchanged SJIS behaviour.
 */
static inline bool str_gbk(void)
{
	return sys4_get_string_charset() == SYS4_CHARSET_GBK;
}

// Bytes of the character at p under the GBK rule, never more than remain.
static inline int gbk_bytes(const uint8_t *p, int remain)
{
	if (remain <= 0)
		return 0;
	return (remain >= 2 && GBK_LEAD(p[0])) ? 2 : 1;
}

static inline uint32_t gbk_code(const uint8_t *p, int n)
{
	return n == 2 ? ((uint32_t)p[0] << 8) | p[1] : p[0];
}

static bool gbk_set_has(const uint8_t *set, int set_len, uint32_t code)
{
	for (int i = 0; i < set_len; ) {
		int n = gbk_bytes(set + i, set_len - i);
		if (gbk_code(set + i, n) == code)
			return true;
		i += n;
	}
	return false;
}

// Growable list of byte spans (offset, length) of a source string.
struct gbk_spans { int nr, cap; int (*v)[2]; };

static void gbk_spans_push(struct gbk_spans *sp, int off, int len)
{
	if (sp->nr == sp->cap) {
		sp->cap = sp->cap ? sp->cap * 2 : 8;
		sp->v = xrealloc(sp->v, sp->cap * sizeof *sp->v);
	}
	sp->v[sp->nr][0] = off;
	sp->v[sp->nr][1] = len;
	sp->nr++;
}

// array<string> page with one new string per span; frees the span list.
static struct page *gbk_spans_to_page(const struct string *src, struct gbk_spans *sp)
{
	struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, sp->nr);
	result->array.rank = 1;
	for (int i = 0; i < sp->nr; i++) {
		int slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = make_string(src->text + sp->v[i][0], sp->v[i][1]);
		result->values[i].i = slot;
	}
	free(sp->v);
	*sp = (struct gbk_spans){ 0 };
	return result;
}

// [0] int ToInt(ref string self)
static int String_ToInt(struct string **self)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0) return 0;
	if (str_gbk()) {
		// EXE 0x686740 -> 0x686040: A3 B0..B9 and 81 44 become ASCII first
		char *buf = xstrdup(s->text);
		string_zen2han_number(buf);
		int n = atoi(buf);
		free(buf);
		return n;
	}
	return atoi(s->text);
}

// [1] float ToFloat(ref string self)
static float String_ToFloat(struct string **self)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0) return 0.0f;
	if (str_gbk()) {
		// EXE 0x686810 -> 0x686040, as ToInt
		char *buf = xstrdup(s->text);
		string_zen2han_number(buf);
		float f = (float)atof(buf);
		free(buf);
		return f;
	}
	return (float)atof(s->text);
}

// [2] int Length(ref string self)
static int String_Length(struct string **self)
{
	struct string *s = SELF_STR(self);
	return s ? mbcs_count_char(s->text) : 0;
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
	if (str_gbk()) {
		// EXE 0x686b80/0x686c40: nothing for index < 0, index >= Length or
		// length == 0; a negative or oversized length erases to the end.
		struct string *s = *self;
		if (index < 0 || length == 0)
			return;
		int b = mbcs_index(s->text, index);
		if (b < 0)
			return;
		int e = s->size;
		if (length > 0) {
			int n = mbcs_index(s->text + b, length);
			if (n >= 0)
				e = b + n;
		}
		if (e <= b)
			return;
		struct string *r = make_string(s->text, b);
		string_append_cstr(&r, s->text + e, s->size - e);
		free_string(*self);
		*self = r;
		return;
	}
	for (int i = 0; i < length; i++) {
		string_erase(self, index);
	}
}

// [8] void Insert(ref string self, int index, string text)
static void String_Insert(struct string **self, int index, struct string *text)
{
	if (!self || !*self || !text || text->size == 0) return;
	struct string *s = *self;
	int byte_idx = mbcs_index(s->text, index);
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
	if (str_gbk()) {
		// EXE 0x686ea0: compares at every character start and keeps the last
		// hit, so an empty key gives the index of the last character.
		if (!s || !key) return -1;
		int last = -1;
		for (int i = 0, c = 0; i < s->size; i += gbk_bytes((uint8_t *)s->text + i, s->size - i), c++) {
			if (i + key->size <= s->size && !memcmp(s->text + i, key->text, key->size))
				last = c;
		}
		return last;
	}
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
// Stores the token array in matchList (a wrap or a direct array slot).
static void string_searchall_store(int ml_slot, struct page *flat)
{
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
}

// The same bracket tokenizer on GBK characters: '[' and ']' only count as
// single-byte characters, never as the trail byte of a 2-byte character.
static bool string_searchall_gbk(const struct string *s, int ml_slot)
{
	const uint8_t *t = (const uint8_t *)s->text;
	int len = s->size;
	struct gbk_spans spans = { 0 };
	for (int i = 0; i < len; ) {
		int n = gbk_bytes(t + i, len - i), j;
		if (n == 1 && t[i] == ']') {
			i++;
			continue;
		}
		if (n == 1 && t[i] == '[') {
			for (j = i + 1; j < len; ) {
				int m = gbk_bytes(t + j, len - j);
				if (m == 1 && t[j] == ']')
					break;
				j += m;
			}
			if (j < len)
				j++;
		} else {
			for (j = i; j < len; ) {
				int m = gbk_bytes(t + j, len - j);
				if (m == 1 && (t[j] == '[' || t[j] == ']'))
					break;
				j += m;
			}
		}
		gbk_spans_push(&spans, i, j - i);
		i = j;
	}
	if (!spans.nr) {
		free(spans.v);
		return false;
	}
	string_searchall_store(ml_slot, gbk_spans_to_page(s, &spans));
	return true;
}

static bool String_SearchAll(struct string **self, int ml_slot, struct string *regex)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0)
		return false;
	if (str_gbk())
		return string_searchall_gbk(s, ml_slot);

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
	string_searchall_store(ml_slot, flat);
	return true;
}

// [17] bool Match(ref string self, wrap<array<array<string>>> matchList, string regex)
// v14: AIN_WRAP — matchList is 1-slot heap index.
static bool String_Match(struct string **self, int ml_slot, struct string *regex)
{
	return String_SearchAll(self, ml_slot, regex);
}

// [19] string Replace(ref string self, string key, string replacer)
// EXE 0x687df0 (dispatcher case 0x684525): compares key at character starts,
// emits replacer and skips the key's bytes on a hit, else copies one
// character; the result is returned and self is left untouched.
static struct string *string_replace_gbk(struct string *s, struct string *from, struct string *to)
{
	if (!from || from->size == 0)
		return string_ref(s); // the EXE loops forever on an empty key
	const uint8_t *t = (const uint8_t *)s->text;
	int len = s->size;
	struct string *out = NULL;
	for (int i = 0; i < len; ) {
		if (i + from->size <= len && !memcmp(t + i, from->text, from->size)) {
			if (!out)
				out = make_string(s->text, i);
			if (to)
				string_append(&out, to);
			i += from->size;
		} else {
			int n = gbk_bytes(t + i, len - i);
			if (out)
				string_append_cstr(&out, s->text + i, n);
			i += n;
		}
	}
	return out ? out : string_ref(s);
}

static struct string *String_Replace(struct string **self, struct string *from, struct string *to)
{
	struct string *s = SELF_STR(self);
	if (str_gbk())
		return s ? string_replace_gbk(s, from, to) : string_ref(&EMPTY_STRING);
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
 * Characters are stepped with the active rule (GBK lead 0x81..0xFE as in
 * 0x68832f, or SJIS_2BYTE), the same one Length/Find/FindLast use, so the
 * indices they produce stay valid here.
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
	bool gbk = str_gbk();
	for (int i = 0; i < s->size; c++) {
		if (c == begin)
			b = i;
		if (c == end) {
			e = i;
			break;
		}
		uint8_t byte = s->text[i];
		bool lead = gbk ? GBK_LEAD(byte) : SJIS_2BYTE(byte);
		i += (lead && i + 1 < s->size) ? 2 : 1;
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

/*
 * EXE ToLower 0x688880 / ToUpper 0x688b70 under the GBK rule: first the SJIS
 * full-width letters 82 60..79 <-> 82 81..9A (trail +-0x21, checked before
 * the GBK test, e.g. 0x688c22..0x688c9c), then any other 2-byte character is
 * copied, and ASCII A-Z/a-z is converted.
 */
static struct string *string_case_gbk(const struct string *s, bool upper)
{
	struct string *r = string_dup(s);
	uint8_t *t = (uint8_t *)r->text;
	for (int i = 0; i < r->size; ) {
		uint8_t b = t[i];
		if (GBK_LEAD(b) && i + 1 < r->size) {
			uint8_t c = t[i+1];
			if (b == 0x82 && !upper && c >= 0x60 && c <= 0x79)
				t[i+1] = c + 0x21;
			else if (b == 0x82 && upper && c >= 0x81 && c <= 0x9a)
				t[i+1] = c - 0x21;
			i += 2;
			continue;
		}
		if (!upper && b >= 'A' && b <= 'Z')
			t[i] = b + 0x20;
		else if (upper && b >= 'a' && b <= 'z')
			t[i] = b - 0x20;
		i++;
	}
	return r;
}

// [27] string ToLower(ref string self)
static struct string *String_ToLower(struct string **self)
{
	struct string *s = SELF_STR(self);
	if (!s || s->size == 0) return string_ref(&EMPTY_STRING);
	if (str_gbk()) return string_case_gbk(s, false);
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
	if (str_gbk()) return string_case_gbk(s, true);
	struct string *result = string_dup(s);
	for (int i = 0; i < result->size; i++) {
		if (SJIS_2BYTE(result->text[i])) { i++; continue; }
		result->text[i] = toupper((unsigned char)result->text[i]);
	}
	return result;
}

/*
 * Split under the GBK rule, EXE 0x6892f0 (set 0x6862a0, scan 0x686440):
 * separators is a set of characters; containsMode bit 0 adds each separator
 * as its own piece, bit 1 keeps empty pieces; a separator at the very end
 * does not produce a trailing piece.
 */
static struct page *string_split_gbk(const struct string *str, const struct string *separators, int mode)
{
	const uint8_t *t = (const uint8_t *)str->text;
	const uint8_t *set = (const uint8_t *)separators->text;
	int len = str->size;
	struct gbk_spans spans = { 0 };
	for (int p = 0; p < len; ) {
		int q = p, n = 0;
		while (q < len) {
			n = gbk_bytes(t + q, len - q);
			if (gbk_set_has(set, separators->size, gbk_code(t + q, n)))
				break;
			q += n;
		}
		if (q > p)
			gbk_spans_push(&spans, p, q - p);
		else if (mode & 2)
			gbk_spans_push(&spans, p, 0);
		if (q >= len)
			break;
		if (mode & 1)
			gbk_spans_push(&spans, q, n);
		p = q + n;
	}
	return gbk_spans_to_page(str, &spans);
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

	if (str_gbk()) {
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, string_split_gbk(str, separators, containsMode));
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
 * ---- Minimal ECMAScript regex subset, boolean results only ----
 *
 * The EXE converts self and the pattern to wstring with the "CHS" locale
 * (0x685e20, locale name at 0x811eac), builds std::wregex with flags
 * ECMAScript (_Parser ctor 0x68d120 stores 1 at +0x40), then:
 *   Match(self, regex)  [17] -> 0x6878f0 -> _Regex_match1 0x68bda0, _Match(nullptr, full=1)
 *   Search(self, regex) [14] -> 0x686ff0 -> _Regex_search2 0x68bc20 (retries each start)
 * A malformed pattern is caught (message at 0x7e5e44) and treated as no match here.
 *
 * Supported: literals, '.', [...] (ranges, negation, \d\w\s inside), \d \D \w \W
 * \s \S \b \B, \t \n \v \f \r \0 \xHH \cX, identity escapes, ^ $, ( ), (?: ),
 * |, * + ? {n} {n,} {n,m} (lazy '?' accepted; irrelevant for a yes/no answer).
 * Not supported (pattern rejected -> false): backreferences, lookahead, \u.
 * Characters are compared as code units: ASCII byte, or (lead << 8 | trail)
 * for a double-byte char, so ranges over non-ASCII chars follow GBK/SJIS
 * order rather than Unicode order. None of the CN call sites use non-ASCII
 * patterns (they are "Button\d", "RivalShop\d", "Skill\d", "Bonus\d",
 * "Player\d", "Button.*").
 *
 * Matching is an NFA simulation (Thompson/Pike without captures), so it runs
 * in O(len * prog); compilation is bounded by RX_MAX_INST and a node-visit
 * budget. Known difference: \d \w \s only classify ASCII (plus the
 * ideographic space), while the native wregex also classifies non-ASCII
 * characters through ctype<wchar_t>.
 */

#define RX_MAX_INST   8192
#define RX_MAX_REPEAT 1000
#define RX_MAX_DEPTH  64
#define RX_MAX_PATTERN 4096

enum rx_op { RX_CHAR, RX_ANY, RX_CLASS, RX_BOL, RX_EOL, RX_WORDB, RX_NWORDB, RX_SPLIT, RX_JMP, RX_MATCH };
struct rx_inst { enum rx_op op; int x, y; };
struct rx_class { bool negate; int nr; int cap; uint32_t (*r)[2]; };

enum rx_ntype { RXN_EMPTY, RXN_CHAR, RXN_ANY, RXN_CLASS, RXN_BOL, RXN_EOL, RXN_WORDB, RXN_NWORDB,
		RXN_CAT, RXN_ALT, RXN_REPEAT };
struct rx_node { enum rx_ntype type; int val, min, max, a, b; };

struct rx {
	const uint32_t *p; int n, pos, depth;
	bool error;
	struct rx_node *nodes; int nr_nodes, cap_nodes;
	struct rx_class *cls; int nr_cls, cap_cls;
	struct rx_inst *prog; int nr_prog, cap_prog;
	int visits; // rx_compile calls; bounds repeats of empty subtrees
};

static uint32_t *rx_decode(const struct string *s, int *out_n)
{
	int size = s ? s->size : 0;
	uint32_t *u = xcalloc(size + 1, sizeof(uint32_t));
	int n = 0;
	for (int i = 0; i < size; ) {
		int b;
		u[n++] = string_lib_char_code((const uint8_t *)s->text + i, size - i, &b);
		i += b;
	}
	*out_n = n;
	return u;
}

static int rx_node(struct rx *rx, enum rx_ntype t, int val, int a, int b)
{
	if (rx->nr_nodes == rx->cap_nodes) {
		rx->cap_nodes = rx->cap_nodes ? rx->cap_nodes * 2 : 32;
		rx->nodes = xrealloc(rx->nodes, rx->cap_nodes * sizeof(struct rx_node));
	}
	rx->nodes[rx->nr_nodes] = (struct rx_node){ .type = t, .val = val, .a = a, .b = b };
	return rx->nr_nodes++;
}

static int rx_new_class(struct rx *rx, bool negate)
{
	if (rx->nr_cls == rx->cap_cls) {
		rx->cap_cls = rx->cap_cls ? rx->cap_cls * 2 : 8;
		rx->cls = xrealloc(rx->cls, rx->cap_cls * sizeof(struct rx_class));
	}
	rx->cls[rx->nr_cls] = (struct rx_class){ .negate = negate };
	return rx->nr_cls++;
}

static void rx_class_add(struct rx *rx, int ci, uint32_t lo, uint32_t hi)
{
	struct rx_class *c = &rx->cls[ci];
	if (c->nr == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 8;
		c->r = xrealloc(c->r, c->cap * sizeof(*c->r));
	}
	c->r[c->nr][0] = lo;
	c->r[c->nr][1] = hi;
	c->nr++;
}

static bool rx_is_word(uint32_t c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// Add \d \w \s (or their complements) to class ci. kind is the escape letter.
static void rx_class_add_escape(struct rx *rx, int ci, uint32_t kind)
{
	static const uint32_t d[][2] = { { '0', '9' } };
	static const uint32_t w[][2] = { { '0', '9' }, { 'A', 'Z' }, { '_', '_' }, { 'a', 'z' } };
	// ASCII whitespace plus the ideographic space of the active encoding
	uint32_t s[][2] = { { 0x09, 0x0d }, { 0x20, 0x20 }, { 0, 0 } };
	s[2][0] = s[2][1] = ain_is_gb18030 ? 0xa1a1 : 0x8140;
	const uint32_t (*set)[2];
	int n;
	switch (kind | 0x20) {
	case 'd': set = d; n = 1; break;
	case 'w': set = w; n = 4; break;
	default:  set = (const uint32_t (*)[2])s; n = 3; break;
	}
	if (kind >= 'a') {
		for (int i = 0; i < n; i++)
			rx_class_add(rx, ci, set[i][0], set[i][1]);
		return;
	}
	// complement of a sorted, non-overlapping range list
	uint32_t next = 0;
	for (int i = 0; i < n; i++) {
		if (set[i][0] > next)
			rx_class_add(rx, ci, next, set[i][0] - 1);
		next = set[i][1] + 1;
	}
	rx_class_add(rx, ci, next, UINT32_MAX);
}

static bool rx_class_match(const struct rx_class *c, uint32_t ch)
{
	bool in = false;
	for (int i = 0; i < c->nr && !in; i++)
		in = ch >= c->r[i][0] && ch <= c->r[i][1];
	return in != c->negate;
}

static int rx_hex(uint32_t c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static bool rx_peek(struct rx *rx, uint32_t c)
{
	return rx->pos < rx->n && rx->p[rx->pos] == c;
}

// Parse the char after '\'. Returns the literal code, or sets *cls_kind for
// \d\D\w\W\s\S. in_class selects \b = backspace.
static uint32_t rx_parse_escape(struct rx *rx, bool in_class, uint32_t *cls_kind, int *assert_node)
{
	*cls_kind = 0;
	if (rx->pos >= rx->n) { rx->error = true; return 0; }
	uint32_t c = rx->p[rx->pos++];
	switch (c) {
	case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
		*cls_kind = c;
		return 0;
	case 'b':
		if (in_class) return 0x08;
		*assert_node = RXN_WORDB;
		return 0;
	case 'B':
		if (in_class) { rx->error = true; return 0; }
		*assert_node = RXN_NWORDB;
		return 0;
	case 't': return '\t';
	case 'n': return '\n';
	case 'v': return '\v';
	case 'f': return '\f';
	case 'r': return '\r';
	case '0':
		if (rx->pos < rx->n && rx->p[rx->pos] >= '0' && rx->p[rx->pos] <= '9') { rx->error = true; return 0; }
		return 0;
	case 'c':
		if (rx->pos < rx->n && ((rx->p[rx->pos] | 0x20) >= 'a' && (rx->p[rx->pos] | 0x20) <= 'z'))
			return rx->p[rx->pos++] % 32;
		rx->error = true;
		return 0;
	case 'x': {
		int h1 = rx->pos < rx->n ? rx_hex(rx->p[rx->pos]) : -1;
		int h2 = rx->pos + 1 < rx->n ? rx_hex(rx->p[rx->pos + 1]) : -1;
		if (h1 < 0 || h2 < 0) { rx->error = true; return 0; }
		rx->pos += 2;
		return h1 * 16 + h2;
	}
	default:
		// backreferences and \u are not supported; other letters/digits are
		// not identity escapes in ECMAScript
		if (c < 0x80 && ((c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'))) {
			rx->error = true;
			return 0;
		}
		return c;
	}
}

static int rx_parse_class(struct rx *rx)
{
	bool negate = rx_peek(rx, '^');
	if (negate) rx->pos++;
	int ci = rx_new_class(rx, negate);
	while (rx->pos < rx->n && rx->p[rx->pos] != ']') {
		uint32_t lo, kind = 0;
		int dummy = 0;
		if (rx->p[rx->pos] == '\\') {
			rx->pos++;
			lo = rx_parse_escape(rx, true, &kind, &dummy);
			if (rx->error) return -1;
			if (kind) {
				rx_class_add_escape(rx, ci, kind);
				continue;
			}
		} else {
			lo = rx->p[rx->pos++];
		}
		uint32_t hi = lo;
		if (rx_peek(rx, '-') && rx->pos + 1 < rx->n && rx->p[rx->pos + 1] != ']') {
			rx->pos++;
			if (rx->p[rx->pos] == '\\') {
				rx->pos++;
				hi = rx_parse_escape(rx, true, &kind, &dummy);
				if (rx->error || kind) { rx->error = true; return -1; }
			} else {
				hi = rx->p[rx->pos++];
			}
			if (hi < lo) { rx->error = true; return -1; }
		}
		rx_class_add(rx, ci, lo, hi);
	}
	if (!rx_peek(rx, ']')) { rx->error = true; return -1; }
	rx->pos++;
	return ci;
}

static int rx_parse_alt(struct rx *rx);

static int rx_parse_atom(struct rx *rx)
{
	uint32_t c = rx->p[rx->pos++];
	switch (c) {
	case '.': return rx_node(rx, RXN_ANY, 0, -1, -1);
	case '^': return rx_node(rx, RXN_BOL, 0, -1, -1);
	case '$': return rx_node(rx, RXN_EOL, 0, -1, -1);
	case '[': {
		int ci = rx_parse_class(rx);
		return rx->error ? -1 : rx_node(rx, RXN_CLASS, ci, -1, -1);
	}
	case '(': {
		if (rx_peek(rx, '?')) {
			if (rx->pos + 1 < rx->n && rx->p[rx->pos + 1] == ':') {
				rx->pos += 2;
			} else {
				rx->error = true; // lookahead etc.
				return -1;
			}
		}
		if (++rx->depth > RX_MAX_DEPTH) { rx->error = true; return -1; }
		int inner = rx_parse_alt(rx);
		rx->depth--;
		if (rx->error || !rx_peek(rx, ')')) { rx->error = true; return -1; }
		rx->pos++;
		return inner;
	}
	case ')': case '*': case '+': case '?':
		rx->error = true;
		return -1;
	case '\\': {
		uint32_t kind;
		int assert_node = 0;
		uint32_t lit = rx_parse_escape(rx, false, &kind, &assert_node);
		if (rx->error) return -1;
		if (assert_node) return rx_node(rx, assert_node, 0, -1, -1);
		if (kind) {
			int ci = rx_new_class(rx, false);
			rx_class_add_escape(rx, ci, kind);
			return rx_node(rx, RXN_CLASS, ci, -1, -1);
		}
		return rx_node(rx, RXN_CHAR, lit, -1, -1);
	}
	default:
		return rx_node(rx, RXN_CHAR, c, -1, -1);
	}
}

// Parses "{n}", "{n,}", "{n,m}" at rx->pos (pointing at '{'). Returns false
// and leaves pos unchanged if it is not a quantifier ('{' is then a literal).
static bool rx_parse_braces(struct rx *rx, int *min, int *max)
{
	int pos = rx->pos + 1;
	long lo = -1, hi;
	for (; pos < rx->n && rx->p[pos] >= '0' && rx->p[pos] <= '9'; pos++)
		lo = (lo < 0 ? 0 : lo) * 10 + (rx->p[pos] - '0'), lo = lo > 100000 ? 100000 : lo;
	if (lo < 0)
		return false;
	hi = lo;
	if (pos < rx->n && rx->p[pos] == ',') {
		pos++;
		hi = -1;
		for (; pos < rx->n && rx->p[pos] >= '0' && rx->p[pos] <= '9'; pos++)
			hi = (hi < 0 ? 0 : hi) * 10 + (rx->p[pos] - '0'), hi = hi > 100000 ? 100000 : hi;
	}
	if (pos >= rx->n || rx->p[pos] != '}')
		return false;
	rx->pos = pos + 1;
	*min = (int)lo;
	*max = (int)hi;
	return true;
}

static int rx_parse_repeat(struct rx *rx)
{
	int atom = rx_parse_atom(rx);
	while (!rx->error && rx->pos < rx->n) {
		int min, max;
		uint32_t c = rx->p[rx->pos];
		if (c == '*') { min = 0; max = -1; rx->pos++; }
		else if (c == '+') { min = 1; max = -1; rx->pos++; }
		else if (c == '?') { min = 0; max = 1; rx->pos++; }
		else if (c == '{' && rx_parse_braces(rx, &min, &max)) { }
		else break;
		if ((max >= 0 && max < min) || min > RX_MAX_REPEAT || max > RX_MAX_REPEAT) {
			rx->error = true;
			return -1;
		}
		enum rx_ntype t = rx->nodes[atom].type;
		if (t == RXN_BOL || t == RXN_EOL || t == RXN_WORDB || t == RXN_NWORDB) {
			rx->error = true; // quantified assertion
			return -1;
		}
		if (rx_peek(rx, '?'))
			rx->pos++; // lazy: same yes/no answer
		int r = rx_node(rx, RXN_REPEAT, 0, atom, -1);
		rx->nodes[r].min = min;
		rx->nodes[r].max = max;
		atom = r;
	}
	return atom;
}

static int rx_parse_cat(struct rx *rx)
{
	int left = rx_node(rx, RXN_EMPTY, 0, -1, -1);
	while (!rx->error && rx->pos < rx->n && rx->p[rx->pos] != '|' && rx->p[rx->pos] != ')') {
		int right = rx_parse_repeat(rx);
		if (rx->error) return -1;
		left = rx_node(rx, RXN_CAT, 0, left, right);
	}
	return left;
}

static int rx_parse_alt(struct rx *rx)
{
	int left = rx_parse_cat(rx);
	while (!rx->error && rx_peek(rx, '|')) {
		rx->pos++;
		int right = rx_parse_cat(rx);
		left = rx_node(rx, RXN_ALT, 0, left, right);
	}
	return left;
}

static int rx_emit(struct rx *rx, enum rx_op op, int x, int y)
{
	if (rx->nr_prog >= RX_MAX_INST) {
		rx->error = true;
		return 0;
	}
	if (rx->nr_prog == rx->cap_prog) {
		rx->cap_prog = rx->cap_prog ? rx->cap_prog * 2 : 64;
		rx->prog = xrealloc(rx->prog, rx->cap_prog * sizeof(struct rx_inst));
	}
	rx->prog[rx->nr_prog] = (struct rx_inst){ op, x, y };
	return rx->nr_prog++;
}

static void rx_compile(struct rx *rx, int ni)
{
	if (rx->error || ni < 0)
		return;
	if (++rx->visits > RX_MAX_INST * 4) {
		rx->error = true;
		return;
	}
	struct rx_node n = rx->nodes[ni];
	switch (n.type) {
	case RXN_EMPTY: break;
	case RXN_CHAR:   rx_emit(rx, RX_CHAR, n.val, 0); break;
	case RXN_ANY:    rx_emit(rx, RX_ANY, 0, 0); break;
	case RXN_CLASS:  rx_emit(rx, RX_CLASS, n.val, 0); break;
	case RXN_BOL:    rx_emit(rx, RX_BOL, 0, 0); break;
	case RXN_EOL:    rx_emit(rx, RX_EOL, 0, 0); break;
	case RXN_WORDB:  rx_emit(rx, RX_WORDB, 0, 0); break;
	case RXN_NWORDB: rx_emit(rx, RX_NWORDB, 0, 0); break;
	case RXN_CAT:
		rx_compile(rx, n.a);
		rx_compile(rx, n.b);
		break;
	case RXN_ALT: {
		int split = rx_emit(rx, RX_SPLIT, 0, 0);
		rx->prog[split].x = rx->nr_prog;
		rx_compile(rx, n.a);
		int jmp = rx_emit(rx, RX_JMP, 0, 0);
		rx->prog[split].y = rx->nr_prog;
		rx_compile(rx, n.b);
		rx->prog[jmp].x = rx->nr_prog;
		break;
	}
	case RXN_REPEAT: {
		for (int i = 0; i < n.min && !rx->error; i++)
			rx_compile(rx, n.a);
		if (n.max < 0) {
			int split = rx_emit(rx, RX_SPLIT, 0, 0);
			rx->prog[split].x = rx->nr_prog;
			rx_compile(rx, n.a);
			rx_emit(rx, RX_JMP, split, 0);
			rx->prog[split].y = rx->nr_prog;
		} else {
			int opt = n.max - n.min;
			int *splits = opt > 0 ? xcalloc(opt, sizeof(int)) : NULL;
			for (int i = 0; i < opt && !rx->error; i++) {
				splits[i] = rx_emit(rx, RX_SPLIT, 0, 0);
				rx->prog[splits[i]].x = rx->nr_prog;
				rx_compile(rx, n.a);
			}
			for (int i = 0; i < opt && !rx->error; i++)
				rx->prog[splits[i]].y = rx->nr_prog;
			free(splits);
		}
		break;
	}
	}
}

// Adds pc and everything reachable through non-consuming instructions.
static void rx_add(const struct rx *rx, int *list, int *nlist, int *mark, int gen, int *stack,
		   int pc, const uint32_t *s, int n, int pos)
{
	int sp = 0;
	stack[sp++] = pc;
	while (sp > 0) {
		pc = stack[--sp];
		if (mark[pc] == gen)
			continue;
		mark[pc] = gen;
		const struct rx_inst *in = &rx->prog[pc];
		switch (in->op) {
		case RX_JMP:
			stack[sp++] = in->x;
			break;
		case RX_SPLIT:
			stack[sp++] = in->y;
			stack[sp++] = in->x;
			break;
		case RX_BOL:
			// The native wregex treats ^ and $ as multiline: they also
			// hold after and before a '\n' (0x68e866 / 0x68e89a).
			if (pos == 0 || s[pos - 1] == '\n') stack[sp++] = pc + 1;
			break;
		case RX_EOL:
			if (pos == n || s[pos] == '\n') stack[sp++] = pc + 1;
			break;
		case RX_WORDB:
		case RX_NWORDB: {
			bool a = pos > 0 && rx_is_word(s[pos - 1]);
			bool b = pos < n && rx_is_word(s[pos]);
			if ((a != b) == (in->op == RX_WORDB))
				stack[sp++] = pc + 1;
			break;
		}
		default:
			list[(*nlist)++] = pc;
			break;
		}
	}
}

static bool rx_run(const struct rx *rx, const uint32_t *s, int n, bool full)
{
	int np = rx->nr_prog;
	int *clist = xcalloc(np, sizeof(int)), *nlist = xcalloc(np, sizeof(int));
	int *mark = xcalloc(np, sizeof(int));
	// every pc is pushed at most twice before it is marked (SPLIT pushes 2)
	int *stack = xcalloc(np * 2 + 2, sizeof(int));
	int nc = 0, nn = 0;
	bool matched = false;
	for (int pos = 0; pos <= n && !matched; pos++) {
		// full match (regex_match) starts only at 0; search restarts everywhere
		if (pos == 0 || !full)
			rx_add(rx, clist, &nc, mark, pos + 1, stack, 0, s, n, pos);
		if (nc == 0 && full)
			break;
		nn = 0;
		for (int i = 0; i < nc && !matched; i++) {
			const struct rx_inst *in = &rx->prog[clist[i]];
			bool step = false;
			switch (in->op) {
			case RX_MATCH:
				if (!full || pos == n)
					matched = true;
				break;
			case RX_CHAR:
				step = pos < n && s[pos] == (uint32_t)in->x;
				break;
			case RX_ANY:
				step = pos < n && s[pos] != '\n' && s[pos] != '\r';
				break;
			case RX_CLASS:
				step = pos < n && rx_class_match(&rx->cls[in->x], s[pos]);
				break;
			default:
				break;
			}
			if (step)
				rx_add(rx, nlist, &nn, mark, pos + 2, stack, clist[i] + 1, s, n, pos + 1);
		}
		int *t = clist; clist = nlist; nlist = t;
		nc = nn;
	}
	free(clist);
	free(nlist);
	free(mark);
	free(stack);
	return matched;
}

// Returns 1 = match, 0 = no match, -1 = pattern rejected.
static int string_regex(const struct string *subject, const struct string *pattern, bool full)
{
	struct rx rx = { 0 };
	int pn, sn;
	uint32_t *p = rx_decode(pattern, &pn);
	rx.p = p;
	rx.n = pn;
	if (pn > RX_MAX_PATTERN)
		rx.error = true;
	int root = rx.error ? -1 : rx_parse_alt(&rx);
	if (!rx.error && rx.pos != rx.n)
		rx.error = true; // unbalanced ')'
	rx_compile(&rx, root);
	rx_emit(&rx, RX_MATCH, 0, 0);
	int result = -1;
	if (!rx.error) {
		uint32_t *s = rx_decode(subject, &sn);
		result = rx_run(&rx, s, sn, full) ? 1 : 0;
		free(s);
	}
	for (int i = 0; i < rx.nr_cls; i++)
		free(rx.cls[i].r);
	free(rx.cls);
	free(rx.nodes);
	free(rx.prog);
	free(p);
	return result;
}

static bool string_regex_bool(struct string **self, struct string *regex, bool full)
{
	struct string *s = SELF_STR(self);
	if (!s)
		return false; // EXE: invalid self -> false (0x68793a)
	int r = string_regex(s, regex, full);
	if (r < 0) {
		WARNING("String.%s: unsupported or invalid regex \"%s\"",
			full ? "Match" : "Search", regex ? display_game0(regex->text) : "");
		return false;
	}
	return r;
}

// [17] bool Match(ref string self, string regex) -- std::regex_match, whole string
static bool String_MatchRegex(struct string **self, struct string *regex)
{
	return string_regex_bool(self, regex, true);
}

// [14] bool Search(ref string self, string regex) -- std::regex_search
static bool String_SearchRegex(struct string **self, struct string *regex)
{
	return string_regex_bool(self, regex, false);
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
	} else if (!strcmp(f->name, "Match")) {
		if (n == 2 && a1 == AIN_STRING)
			return (void *)String_MatchRegex;
	} else if (!strcmp(f->name, "Search")) {
		if (n == 2 && a1 == AIN_STRING)
			return (void *)String_SearchRegex;
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
