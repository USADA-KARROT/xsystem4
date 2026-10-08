/* shuffle: Array.Shuffle and the Math seed entries against the original
 * (2026-10-08).
 *
 * Original Array.Shuffle (Array jump table 0x644f18[60] -> 0x644c3a ->
 * 0x6493f0): a 521-word generator is built on the stack for the call
 * (0x4c6e40 seeds it, 0x4c6d40 regenerates it); seed < 0 takes the monotonic
 * timeGetTime() value (0x41b3c0) and seed >= 0 is used as is; then for
 * i = 1..n-1, j = word % (i + 1) and elements i and j are swapped through the
 * array object's own swap (vtable +0x60), so an interface/option element moves
 * with all its slots. The Math library's SetSeed (0x4c673c), SetSeedByCurrentTime
 * (0x4c7060), Rand (0x4c676a: word & 0x7fffffff) and RandF (0x4c70c0:
 * (double)(uint32)word * 2^-32 to float, [0, 1]) use one global instance of the
 * same generator; Shuffle never touches it. Before the fix Shuffle called
 * srand(seed) and rand() - every Shuffle(-1) (all 37 call sites in the game
 * pass -1) produced the same permutation and reset the sequence behind
 * Math.Rand/RandF as well - and swapped single slots only.
 *
 *   SH1 Shuffle(-1) twice on 0..19: two different permutations, both of 0..19;
 *       Shuffle(0) twice: identical; an empty and a one-element array: untouched
 *   SH2 Shuffle(7) twice on 0..19: identical, and equal to the reference
 *       permutation
 *   SH3 Math.SetSeed(7); Rand x5 == reference words & 0x7fffffff; the first
 *       RandF after SetSeed(7) == reference; 1000 RandF in [0, 1]
 *   SH4 Math.SetSeed(7); Shuffle(-1); Rand x5 == the same words (Shuffle does
 *       not disturb the Math generator)
 *   SH5 Shuffle(-1) on a 2-slot (interface, arg3 0x10000|AIN_IFACE) array of
 *       8: every (object, offset) pair stays together, the multiset is unchanged
 *   SH6 XSYS4_RANDOM_SEED=5 (set inside the fork, before any time-seeded call):
 *       the first and second Shuffle(-1) equal Shuffle(5) and Shuffle(6)
 *   SH7 guard: with ain->version temporarily 13, Math.SetSeed/Rand/RandF are
 *       the C library's srand/rand again (Rand after SetSeed(7) == rand() after
 *       srand(7)); Array.Shuffle(7) is not version-gated and keeps the reference
 * The reference values come from a python re-implementation of the same
 * 0x4c6e40/0x4c6d40 disassembly (scratchpad r521_ref.py), which the C code was
 * checked against; they are not readings from the original program. Uses
 * fo_find_lib (first_overload_fixture.inc). */
#include <sys/wait.h>
#include <unistd.h>
#include <math.h>
static int sh_failures;
#define SH_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); sh_failures++; } } while (0)

static int sh_find(int lib, const char *name, int nargs)
{
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++) {
		struct ain_hll_function *f = &ain->libraries[lib].functions[j];
		if (!strcmp(f->name, name) && f->nr_arguments == nargs)
			return j;
	}
	return -1;
}

static int sh_int_array(int n, int stride)
{
	struct page *a = alloc_page(ARRAY_PAGE, stride > 1 ? AIN_ARRAY : AIN_ARRAY_INT, n * stride);
	a->array.rank = 1;
	a->array.struct_type = stride > 1 ? stride : -1;
	a->array.elem_slots = stride;
	for (int i = 0; i < n; i++)
		for (int k = 0; k < stride; k++)
			a->values[i * stride + k].i = k == 0 ? i : 1000 + i;
	int s = heap_alloc_slot(VM_PAGE);
	heap_set_page(s, a);
	return s;
}

static void sh_shuffle(int lib, int fno, int slot, int seed, int arg3)
{
	int sp = stack_ptr;
	stack_push(slot);
	stack_push(seed);
	hll_call(lib, fno, arg3);
	SH_CHECK(stack_ptr == sp, "Shuffle left %d slot(s) on the stack", stack_ptr - sp);
	stack_ptr = sp;
}

static void sh_read(int slot, int *out, int n, int stride)
{
	struct page *a = heap[slot].page;
	for (int i = 0; i < n; i++)
		out[i] = a->values[i * stride].i;
}

static bool sh_is_perm(const int *v, int n)
{
	bool seen[64] = { false };
	for (int i = 0; i < n; i++) {
		if (v[i] < 0 || v[i] >= n || seen[v[i]])
			return false;
		seen[v[i]] = true;
	}
	return true;
}

static void sh_print(const char *label, const int *v, int n)
{
	printf("%s:", label);
	for (int i = 0; i < n; i++) printf(" %d", v[i]);
	printf("\n");
}

static int sh_math_call(int lib, int fno, union vm_value *ret)
{
	int sp = stack_ptr;
	hll_call(lib, fno, 0);
	int n = stack_ptr - sp;
	if (n > 0) *ret = stack[sp];
	stack_ptr = sp;
	return n;
}

static void sh_set_seed(int lib, int fno, int seed)
{
	int sp = stack_ptr;
	stack_push(seed);
	hll_call(lib, fno, 0);
	stack_ptr = sp;
}

/* reference: seed 7, first five words of the 0x4c6e40/0x4c6d40 generator */
static const unsigned sh_ref_words7[5] = { 0xa06574b3u, 0xbdcf7c0du, 0x232a2595u, 0x275a4615u, 0x6b21ee5du };
/* reference: Shuffle(0..19, 7) */
static const int sh_ref_perm7[20] = { 16, 15, 17, 10, 18, 8, 14, 4, 1, 11, 5, 3, 2, 7, 12, 9, 19, 6, 0, 13 };
/* reference: (float)(0xa06574b3 * 2^-32) */
static const float sh_ref_randf7 = 0.6265481114387512f;

static void sh_random_and_seeded(void)
{
	int lib = fo_find_lib("Array"), fno = sh_find(lib, "Shuffle", 2);
	assert(lib >= 0 && fno >= 0);
	int a = sh_int_array(20, 1), b = sh_int_array(20, 1);
	int va[20], vb[20];
	sh_shuffle(lib, fno, a, -1, 1);
	sh_shuffle(lib, fno, b, -1, 1);
	sh_read(a, va, 20, 1); sh_read(b, vb, 20, 1);
	sh_print("SH1 Shuffle(-1) #1", va, 20); sh_print("SH1 Shuffle(-1) #2", vb, 20);
	SH_CHECK(sh_is_perm(va, 20) && sh_is_perm(vb, 20), "SH1 results are not permutations of 0..19");
	SH_CHECK(memcmp(va, vb, sizeof(va)) != 0, "SH1 Shuffle(-1) twice gave the same permutation (fixed seed)");
	/* seed 0 is a seed, not "random" */
	int z1 = sh_int_array(20, 1), z2 = sh_int_array(20, 1);
	int vz1[20], vz2[20];
	sh_shuffle(lib, fno, z1, 0, 1);
	sh_shuffle(lib, fno, z2, 0, 1);
	sh_read(z1, vz1, 20, 1); sh_read(z2, vz2, 20, 1);
	sh_print("SH1 Shuffle(0)", vz1, 20);
	SH_CHECK(sh_is_perm(vz1, 20) && memcmp(vz1, vz2, sizeof(vz1)) == 0, "SH1 Shuffle(0) twice differs or is not a permutation");
	/* nothing to shuffle: 0x6493f0 returns when the count is <= 1 */
	int e = sh_int_array(0, 1), one = sh_int_array(1, 1);
	sh_shuffle(lib, fno, e, -1, 1);
	sh_shuffle(lib, fno, one, -1, 1);
	SH_CHECK(heap[e].page->nr_vars == 0, "SH1 empty array changed size to %d", heap[e].page->nr_vars);
	SH_CHECK(heap[one].page->nr_vars == 1 && heap[one].page->values[0].i == 0, "SH1 one-element array changed");
	int c = sh_int_array(20, 1), d = sh_int_array(20, 1);
	int vc[20], vd[20];
	sh_shuffle(lib, fno, c, 7, 1);
	sh_shuffle(lib, fno, d, 7, 1);
	sh_read(c, vc, 20, 1); sh_read(d, vd, 20, 1);
	sh_print("SH2 Shuffle(7)", vc, 20);
	SH_CHECK(memcmp(vc, vd, sizeof(vc)) == 0, "SH2 Shuffle(7) twice differs");
	SH_CHECK(memcmp(vc, sh_ref_perm7, sizeof(vc)) == 0, "SH2 Shuffle(7) differs from the 0x4c6e40 reference");
	heap_unref(a); heap_unref(b); heap_unref(z1); heap_unref(z2); heap_unref(e); heap_unref(one); heap_unref(c); heap_unref(d);
}

static void sh_math(void)
{
	int alib = fo_find_lib("Array"), shuffle = sh_find(alib, "Shuffle", 2);
	int mlib = fo_find_lib("Math");
	int set_seed = sh_find(mlib, "SetSeed", 1), rand_i = sh_find(mlib, "Rand", 0), rand_f = sh_find(mlib, "RandF", 0);
	assert(mlib >= 0 && set_seed >= 0 && rand_i >= 0 && rand_f >= 0 && shuffle >= 0);
	union vm_value v;
	sh_set_seed(mlib, set_seed, 7);
	unsigned got[5];
	for (int i = 0; i < 5; i++) { sh_math_call(mlib, rand_i, &v); got[i] = (unsigned)v.i; }
	printf("SH3 SetSeed(7); Rand: %u %u %u %u %u\n", got[0], got[1], got[2], got[3], got[4]);
	for (int i = 0; i < 5; i++)
		SH_CHECK(got[i] == (sh_ref_words7[i] & 0x7fffffffu), "SH3 Rand #%d = %u, reference %u", i, got[i], sh_ref_words7[i] & 0x7fffffffu);
	sh_set_seed(mlib, set_seed, 7);
	sh_math_call(mlib, rand_f, &v);
	printf("SH3 SetSeed(7); RandF: %.9g\n", v.f);
	SH_CHECK(fabsf(v.f - sh_ref_randf7) < 1e-6f, "SH3 first RandF after SetSeed(7) = %.9g, reference %.9g", v.f, sh_ref_randf7);
	/* [0, 1]: the rounding to float reaches 1.0 for words >= 0xFFFFFF80 (2^-25 per draw) */
	for (int i = 0; i < 1000; i++) {
		sh_math_call(mlib, rand_f, &v);
		SH_CHECK(v.f >= 0.0f && v.f <= 1.0f, "SH3 RandF = %g out of [0, 1]", v.f);
		if (sh_failures > 3) break;
	}
	/* SH4: reseed, shuffle with -1 in between, the Math sequence must be untouched */
	sh_set_seed(mlib, set_seed, 7);
	int arr = sh_int_array(20, 1);
	sh_shuffle(alib, shuffle, arr, -1, 1);
	heap_unref(arr);
	for (int i = 0; i < 5; i++) {
		sh_math_call(mlib, rand_i, &v);
		SH_CHECK((unsigned)v.i == (sh_ref_words7[i] & 0x7fffffffu), "SH4 Rand #%d after Shuffle(-1) = %u, want %u (Shuffle reseeded the Math generator)", i, (unsigned)v.i, sh_ref_words7[i] & 0x7fffffffu);
	}
	printf("SH4 Math sequence after Shuffle(-1): %s\n", sh_failures ? "disturbed" : "unchanged");
}

static void sh_two_slot(void)
{
	int lib = fo_find_lib("Array"), fno = sh_find(lib, "Shuffle", 2);
	/* arg3 0x10000 | AIN_IFACE: an interface element is [object, vtable offset]
	 * (hll_generic_slots); 0x10002, what GetShuffle<BattlePlayer&> passes, is a
	 * one-slot object handle. */
	int arr = sh_int_array(8, 2);
	sh_shuffle(lib, fno, arr, -1, 0x10000 | AIN_IFACE);
	struct page *a = heap[arr].page;
	int first[8];
	bool pairs = true;
	for (int i = 0; i < 8; i++) {
		first[i] = a->values[i * 2].i;
		if (a->values[i * 2 + 1].i != 1000 + a->values[i * 2].i)
			pairs = false;
	}
	sh_print("SH5 2-slot Shuffle(-1) objects", first, 8);
	SH_CHECK(pairs, "SH5 a (object, offset) pair was split");
	SH_CHECK(sh_is_perm(first, 8), "SH5 elements lost or duplicated");
	heap_unref(arr);
}

/* XSYS4_RANDOM_SEED=5 is read on the first time-seeded call of this process
 * (the fork), so the two Shuffle(-1) calls get the seeds 5 and 6. */
static void sh_env_seed(void)
{
	setenv("XSYS4_RANDOM_SEED", "5", 1);
	int lib = fo_find_lib("Array"), fno = sh_find(lib, "Shuffle", 2);
	int a = sh_int_array(20, 1), b = sh_int_array(20, 1);
	int va[20], vb[20];
	sh_shuffle(lib, fno, a, -1, 1);
	sh_shuffle(lib, fno, b, -1, 1);
	sh_read(a, va, 20, 1); sh_read(b, vb, 20, 1);
	int c = sh_int_array(20, 1), d = sh_int_array(20, 1);
	int vc[20], vd[20];
	sh_shuffle(lib, fno, c, 5, 1);
	sh_shuffle(lib, fno, d, 6, 1);
	sh_read(c, vc, 20, 1); sh_read(d, vd, 20, 1);
	sh_print("SH6 XSYS4_RANDOM_SEED=5 Shuffle(-1) #1", va, 20);
	SH_CHECK(memcmp(va, vc, sizeof(va)) == 0 && memcmp(vb, vd, sizeof(vb)) == 0,
		 "SH6 with XSYS4_RANDOM_SEED=5 the two Shuffle(-1) calls should equal Shuffle(5) and Shuffle(6)");
	heap_unref(a); heap_unref(b); heap_unref(c); heap_unref(d);
}

/* The R521 generator was read out of this game's v14 EXE; other AIN versions
 * keep the C library entries the Math library had before. */
static void sh_version_guard(void)
{
	int alib = fo_find_lib("Array"), shuffle = sh_find(alib, "Shuffle", 2);
	int mlib = fo_find_lib("Math");
	int set_seed = sh_find(mlib, "SetSeed", 1), rand_i = sh_find(mlib, "Rand", 0), rand_f = sh_find(mlib, "RandF", 0);
	union vm_value v;
	srand(7);
	int want0 = rand(), want1 = rand();
	int saved = ain->version;
	ain->version = 13;
	sh_set_seed(mlib, set_seed, 7);
	sh_math_call(mlib, rand_i, &v);
	int got0 = v.i;
	sh_math_call(mlib, rand_i, &v);
	int got1 = v.i;
	sh_math_call(mlib, rand_f, &v);
	float f = v.f;
	ain->version = saved;
	printf("SH7 v13: SetSeed(7); Rand = %d %d (srand(7); rand() = %d %d), RandF = %g\n", got0, got1, want0, want1, f);
	SH_CHECK(got0 == want0 && got1 == want1, "SH7 v13 Math.Rand is not the C library sequence");
	SH_CHECK(got0 != (int)(sh_ref_words7[0] & 0x7fffffffu), "SH7 v13 Math.Rand used the v14 generator");
	SH_CHECK(f >= 0.0f && f < 1.0f, "SH7 v13 RandF = %g", f);
	int c = sh_int_array(20, 1);
	int vc[20];
	sh_shuffle(alib, shuffle, c, 7, 1);
	sh_read(c, vc, 20, 1);
	SH_CHECK(memcmp(vc, sh_ref_perm7, sizeof(vc)) == 0, "SH7 Array.Shuffle(7) is not version-gated and must keep the reference permutation");
	heap_unref(c);
}

static int sh_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		sh_failures = 0;
		fn();
		fflush(stdout);
		_exit(sh_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_shuffle(void)
{
	int failed = 0;
	failed += sh_run("SH1-SH2 Shuffle(-1) random, Shuffle(0/7) deterministic, empty/one untouched", sh_random_and_seeded);
	failed += sh_run("SH3-SH4 Math.SetSeed/Rand/RandF, Shuffle leaves Math alone", sh_math);
	failed += sh_run("SH5 two-slot elements move whole", sh_two_slot);
	failed += sh_run("SH6 XSYS4_RANDOM_SEED makes Shuffle(-1) repeatable", sh_env_seed);
	failed += sh_run("SH7 v13 keeps the C library Math entries", sh_version_guard);
	printf("SHUFFLE summary: %d/5 group(s) failed\n", failed);
	return failed ? 92 : 0;
}
