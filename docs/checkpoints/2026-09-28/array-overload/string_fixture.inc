#include <time.h>
/* String GetPart / Trim / Pad overloads through the real ffi path (2026-09-28).
 * Requires first_overload_fixture.inc. */
static int sf_fail;
static int sf_str(const char *t, int n)
{
	int s = heap_alloc_slot(VM_STRING);
	heap[s].s = make_string(t, n < 0 ? (int)strlen(t) : n);
	return s;
}

static int sf_find(int lib, const char *name, int nargs)
{
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++) {
		struct ain_hll_function *f = &ain->libraries[lib].functions[j];
		if (!strcmp(f->name, name) && f->nr_arguments == nargs)
			return j;
	}
	return -1;
}

/* Calls name(self, ints...) and compares the returned string. */
static void sf_case(const char *what, int lib, const char *name, const char *self_text, int self_len,
		    const int *ints, int nints, const char *want, int want_len)
{
	int fno = sf_find(lib, name, 1 + nints);
	int self = sf_str(self_text, self_len);
	char before[64];
	int blen = heap[self].s->size;
	memcpy(before, heap[self].s->text, blen);
	int sp = stack_ptr;
	stack_push(self);
	for (int i = 0; i < nints; i++) stack_push(ints[i]);
	hll_call(lib, fno, 0);
	assert(stack_ptr == sp + 1);
	int r = stack_pop().i;
	struct string *rs = heap[r].s;
	int wl = want_len < 0 ? (int)strlen(want) : want_len;
	bool ok = rs->size == wl && !memcmp(rs->text, want, wl);
	bool self_kept = heap[self].s->size == blen && !memcmp(heap[self].s->text, before, blen);
	printf("STRING %-36s got=\"%.*s\" want=\"%.*s\" self_kept=%d %s\n", what, rs->size, rs->text,
	       wl, want, self_kept, ok && self_kept ? "ok" : "FAIL");
	if (!ok || !self_kept) sf_fail++;
	heap_unref(r);
	heap_unref(self);
}


/* Calls Match/Search(self, pattern) and compares the bool result. */
static void sf_rx(const char *what, int lib, const char *name, const char *subject, const char *pattern, int want)
{
	int fno = -1;
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++) {
		struct ain_hll_function *f = &ain->libraries[lib].functions[j];
		if (!strcmp(f->name, name) && f->nr_arguments == 2 && f->arguments[1].type.data == AIN_STRING)
			fno = j;
	}
	int self = sf_str(subject, -1), pat = sf_str(pattern, -1);
	int sp = stack_ptr;
	stack_push(self);
	stack_push(pat);
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	hll_call(lib, fno, 0);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
	assert(stack_ptr == sp + 1);
	int got = stack_pop().i;
	bool ok = got == want && ms < 1000;
	printf("STRING %-36s got=%d want=%d (%.1f ms) %s\n", what, got, want, ms, ok ? "ok" : "FAIL");
	if (!ok) sf_fail++;
	heap_unref(self);
	heap_unref(pat);
}

static int test_string(void)
{
	int lib = fo_find_lib("String");
	assert(lib >= 0);
	sf_case("GetPart#0(\"abcdef\", 2) to end", lib, "GetPart", "abcdef", -1, (int[]){2}, 1, "cdef", -1);
	sf_case("GetPart#0(\"abc\", 5)", lib, "GetPart", "abc", -1, (int[]){5}, 1, "", -1);
	sf_case("GetPart#0(\"abc\", -1) clamps", lib, "GetPart", "abc", -1, (int[]){-1}, 1, "abc", -1);
	sf_case("GetPart#1(\"abcdef\", 1, 3)", lib, "GetPart", "abcdef", -1, (int[]){1, 3}, 2, "bcd", -1);
	sf_case("GetPart#1(\"abcdef\", -2, 3) clamps", lib, "GetPart", "abcdef", -1, (int[]){-2, 3}, 2, "abc", -1);
	sf_case("GetPart#1(\"abcdef\", 2, 0)", lib, "GetPart", "abcdef", -1, (int[]){2, 0}, 2, "", -1);
	sf_case("GetPart#1(\"abcdef\", 4, 10)", lib, "GetPart", "abcdef", -1, (int[]){4, 10}, 2, "ef", -1);
	sf_case("Trim#0 spaces/tabs, self untouched", lib, "Trim", "  hi \t", -1, NULL, 0, "hi", -1);
	sf_case("Trim#0 default set has 81 40", lib, "Trim", "\x81\x40hi\x81\x40", -1, NULL, 0, "hi", -1);
	sf_case("Trim#0 keeps inner spaces", lib, "Trim", " a b ", -1, NULL, 0, "a b", -1);
	sf_case("TrimStart#0", lib, "TrimStart", "  hi  ", -1, NULL, 0, "hi  ", -1);
	sf_case("TrimEnd#0", lib, "TrimEnd", "  hi  ", -1, NULL, 0, "  hi", -1);
	sf_case("PadLeft#0(\"ab\", 5)", lib, "PadLeft", "ab", -1, (int[]){5}, 1, "   ab", -1);
	sf_case("PadRight#0(\"ab\", 4)", lib, "PadRight", "ab", -1, (int[]){4}, 1, "ab  ", -1);
	sf_case("PadLeft#0 no pad needed", lib, "PadLeft", "abcdef", -1, (int[]){3}, 1, "abcdef", -1);
	sf_rx("Match Button1 ~ Button\\d", lib, "Match", "Button1", "Button\\d", 1);
	sf_rx("Match Button10 ~ Button\\d (full)", lib, "Match", "Button10", "Button\\d", 0);
	sf_rx("Match RivalShop3 ~ RivalShop\\d", lib, "Match", "RivalShop3", "RivalShop\\d", 1);
	sf_rx("Match Skill ~ Skill\\d", lib, "Match", "Skill", "Skill\\d", 0);
	sf_rx("Match ButtonX ~ Button.*", lib, "Match", "ButtonX", "Button.*", 1);
	sf_rx("Match FooterButton_1 ~ ^zzz$", lib, "Match", "FooterButton_1", "^zzz$", 0);
	sf_rx("Match abc ~ b (needs whole)", lib, "Match", "abc", "b", 0);
	sf_rx("Search abc ~ b", lib, "Search", "abc", "b", 1);
	sf_rx("Search a\\nb ~ ^b (multiline)", lib, "Search", "a\nb", "^b", 1);
	sf_rx("Search a\\nb ~ a$ (multiline)", lib, "Search", "a\nb", "a$", 1);
	sf_rx("Match x ~ ( invalid", lib, "Match", "x", "(", 0);
	sf_rx("Match nested empty repeats bounded", lib, "Match", "x", "(?:(?:(?:a{0}){1000}){1000}){1000}", 0);
	sf_rx("Match ab|cd alternation", lib, "Match", "cd", "ab|cd", 1);
	sf_rx("Match class [0-9]{2,3}", lib, "Match", "123", "[0-9]{2,3}", 1);
	printf("STRING summary: %d failure(s)\n", sf_fail);
	return sf_fail ? 88 : 0;
}
