/* parts-reverse: PartsEngine.SetComponentReverseLR/TB and GetComponentReverseLR/TB
 * (v14). The native setter stores a flag on an existing parts (0x58e070 writes
 * +0xaa) and ignores an unknown number; the getter returns false for it.
 * AdvStand flips stands placed on the left through SetComponentReverseLR.
 * Called through the real AIN declarations and ffi (hll_call). Before the fix
 * the setter was an empty stub and the getters were not linked. */
extern void probe_init_parts_table(void);

static int pr_fn(const char *lib_name, const char *fn_name, int *lib_out)
{
	for (int l = 0; l < ain->nr_libraries; l++) {
		if (strcmp(ain->libraries[l].name, lib_name))
			continue;
		for (int f = 0; f < ain->libraries[l].nr_functions; f++) {
			if (!strcmp(ain->libraries[l].functions[f].name, fn_name)) {
				*lib_out = l;
				return f;
			}
		}
	}
	printf("PARTS-REVERSE %s.%s not declared\n", lib_name, fn_name);
	exit(2);
}

static void pr_set(const char *fn, int number, int value)
{
	int lib, f = pr_fn("PartsEngine", fn, &lib);
	int sp = stack_ptr;
	stack_push(number);
	stack_push(value);
	hll_call(lib, f, 0);
	stack_ptr = sp;
}

static int pr_get(const char *fn, int number)
{
	int lib, f = pr_fn("PartsEngine", fn, &lib);
	int sp = stack_ptr;
	stack_push(number);
	hll_call(lib, f, 0);
	int r = stack_ptr > sp ? stack_pop().i : -999;
	stack_ptr = sp;
	return r;
}

static int test_parts_reverse(void)
{
	int failures = 0;
#define PR_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)
	probe_init_parts_table();
	/* init_probe links HLLs without module startup. Production runs the
	 * PartsEngine _PostLink hook, which registers the v14 component functions
	 * (pe_v14_register_batch); run that same hook here. */
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++) {
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PostLink")) {
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
			break;
		}
	}
	const int n = 9001, unknown = 9002;
	/* Only the HLL getters are used below (no struct fields), so this file
	 * also compiles against revisions before the flags existed. */
	PR_CHECK(parts_get(n) != NULL, "parts_get(%d)", n);
	PR_CHECK(pr_get("GetComponentReverseLR", n) == 0 && pr_get("GetComponentReverseTB", n) == 0,
		 "new parts starts unflipped");

	pr_set("SetComponentReverseLR", n, 1);
	PR_CHECK(pr_get("GetComponentReverseLR", n) == 1, "GetComponentReverseLR after set = %d", pr_get("GetComponentReverseLR", n));
	PR_CHECK(pr_get("GetComponentReverseTB", n) == 0, "GetComponentReverseTB changed by the LR setter");

	pr_set("SetComponentReverseTB", n, 1);
	PR_CHECK(pr_get("GetComponentReverseTB", n) == 1, "SetComponentReverseTB(n, true)");
	PR_CHECK(pr_get("GetComponentReverseLR", n) == 1, "LR flag lost after the TB setter");

	pr_set("SetComponentReverseLR", n, 0);
	PR_CHECK(pr_get("GetComponentReverseLR", n) == 0, "SetComponentReverseLR(n, false)");
	pr_set("SetComponentReverseTB", n, 0);
	PR_CHECK(pr_get("GetComponentReverseTB", n) == 0, "SetComponentReverseTB(n, false)");

	pr_set("SetComponentReverseLR", unknown, 1);
	PR_CHECK(!parts_try_get(unknown), "the setter created parts %d for an unknown number", unknown);
	PR_CHECK(pr_get("GetComponentReverseLR", unknown) == 0, "GetComponentReverseLR(unknown) != 0");
	PR_CHECK(pr_get("GetComponentReverseTB", unknown) == 0, "GetComponentReverseTB(unknown) != 0");
#undef PR_CHECK
	printf("PARTS-REVERSE summary: %d failure(s)\n", failures);
	return failures ? 88 : 0;
}
