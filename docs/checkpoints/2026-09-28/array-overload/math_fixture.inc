/* Math overloads through the real ffi path (2026-09-28). Requires
 * first_overload_fixture.inc for fo_find_lib. */
static int mf_fail;
static int mf_find(int lib, const char *name, int nargs, enum ain_data_type ret)
{
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++) {
		struct ain_hll_function *f = &ain->libraries[lib].functions[j];
		if (!strcmp(f->name, name) && f->nr_arguments == nargs && f->return_type.data == ret)
			return j;
	}
	return -1;
}

static union vm_value mf_call(int lib, int fno, const union vm_value *a, int n)
{
	int sp = stack_ptr;
	for (int i = 0; i < n; i++) stack_push(a[i]);
	hll_call(lib, fno, 0);
	assert(stack_ptr == sp + 1);
	return stack_pop();
}

static void mf_f(const char *what, int lib, const char *name, int n, const float *args, float want)
{
	int fno = mf_find(lib, name, n, AIN_FLOAT);
	union vm_value a[4];
	for (int i = 0; i < n; i++) a[i].f = args[i];
	float got = mf_call(lib, fno, a, n).f;
	bool ok = (isnan(want) && isnan(got)) || (got == want && signbit(got) == signbit(want));
	printf("MATH %-30s got=%-10g want=%-10g %s\n", what, got, want, ok ? "ok" : "FAIL");
	if (!ok) mf_fail++;
}

static void mf_i(const char *what, int lib, const char *name, int n, const int *args, int want)
{
	int fno = mf_find(lib, name, n, AIN_INT);
	union vm_value a[4];
	for (int i = 0; i < n; i++) a[i].i = args[i];
	int got = mf_call(lib, fno, a, n).i;
	printf("MATH %-30s got=%-10d want=%-10d %s\n", what, got, want, got == want ? "ok" : "FAIL");
	if (got != want) mf_fail++;
}

static int test_math(void)
{
	int lib = fo_find_lib("Math");
	assert(lib >= 0);
	float nan = NAN;
	mf_f("Abs(-3.5)", lib, "Abs", 1, (float[]){-3.5f}, 3.5f);
	mf_f("Abs(-0.0)", lib, "Abs", 1, (float[]){-0.0f}, 0.0f);
	mf_i("Abs(-7)", lib, "Abs", 1, (int[]){-7}, 7);
	mf_i("Abs(INT_MIN)", lib, "Abs", 1, (int[]){INT_MIN}, INT_MIN);
	mf_f("Min(5.0, 2.0)", lib, "Min", 2, (float[]){5.0f, 2.0f}, 2.0f);
	mf_f("Min(NaN, 2.0)", lib, "Min", 2, (float[]){nan, 2.0f}, 2.0f);
	mf_f("Min(0.0, -0.0) prefers -0", lib, "Min", 2, (float[]){0.0f, -0.0f}, -0.0f);
	mf_f("Min(3.0, 1.5, 2.0)", lib, "Min", 3, (float[]){3.0f, 1.5f, 2.0f}, 1.5f);
	mf_f("Min(4,3,2,1 float)", lib, "Min", 4, (float[]){4, 3, 2, 1}, 1.0f);
	mf_i("Min(3, 2, 1)", lib, "Min", 3, (int[]){3, 2, 1}, 1);
	mf_i("Min(4, 3, 2, 1)", lib, "Min", 4, (int[]){4, 3, 2, 1}, 1);
	mf_i("Min(5, 2) int unchanged", lib, "Min", 2, (int[]){5, 2}, 2);
	mf_f("Max(1.0, 7.0)", lib, "Max", 2, (float[]){1.0f, 7.0f}, 7.0f);
	mf_f("Max(-0.0, 0.0) prefers +0", lib, "Max", 2, (float[]){-0.0f, 0.0f}, 0.0f);
	mf_f("Max(1, 9, 3 float)", lib, "Max", 3, (float[]){1, 9, 3}, 9.0f);
	mf_f("Max(1,2,3,4 float)", lib, "Max", 4, (float[]){1, 2, 3, 4}, 4.0f);
	mf_i("Max(1, 2, 3)", lib, "Max", 3, (int[]){1, 2, 3}, 3);
	mf_i("Max(1, 2, 3, 4)", lib, "Max", 4, (int[]){1, 2, 3, 4}, 4);
	mf_f("Clamp(5.0, 0, 1)", lib, "Clamp", 3, (float[]){5.0f, 0.0f, 1.0f}, 1.0f);
	mf_f("Clamp(-2.0, 0, 1)", lib, "Clamp", 3, (float[]){-2.0f, 0.0f, 1.0f}, 0.0f);
	mf_f("Clamp(0.25, 0, 1)", lib, "Clamp", 3, (float[]){0.25f, 0.0f, 1.0f}, 0.25f);
	mf_f("Clamp(NaN, 0, 1) -> high", lib, "Clamp", 3, (float[]){nan, 0.0f, 1.0f}, 1.0f);
	mf_i("Clamp(15, 0, 10) int unchanged", lib, "Clamp", 3, (int[]){15, 0, 10}, 10);
	printf("MATH summary: %d failure(s)\n", mf_fail);
	return mf_fail ? 88 : 0;
}
