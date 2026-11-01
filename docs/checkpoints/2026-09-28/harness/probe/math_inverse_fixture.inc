/* math-inverse-trig: Math.Asin and Math.Acos through the AIN's own
 * declarations and the real ffi path (math_fixture.inc's mf_find / mf_call).
 *
 * The original's Math dispatcher (0x4c6520) multiplies the argument of
 * Cos, Sin and Tan (cases 0-2) by a degree-to-radian constant before the
 * call; Asin (case 3, 0x78641d) and Acos (case 4, 0x786040) are called on
 * the argument as it is and their result is returned as it is, like Atan
 * (case 5): no constant is applied to it. So in a v14 engine Asin and Acos
 * return radians, as Atan already does here. (That 0x78641d and 0x786040
 * are the CRT's asinf and acosf is read from their shape, not from a
 * symbol.) Before the fix both returned degrees.
 *
 * Engines before v14 are not read (no older executable was disassembled):
 * there the functions keep returning what they did, degrees (the port's old
 * behaviour, kept with no original to rest on; MI4, MI7). Engines after
 * v14 are not read either; they follow v14, as the port's other v14 logic
 * does (ain->version >= 14), which is a choice of the port (MI6), not
 * something read in an executable.
 *
 *   MI1 v14 Asin: 1, 0.5, -1, 0 and -0 give the arcsine in radians: within
 *       2 ulp of the double-precision asin of the argument (the original
 *       calls a CRT single-precision function; this C library's asinf is 1
 *       ulp off pi/2 at 1, so the result is not bound to one library), and
 *       exactly 0 and -0 for 0 and -0 (the sign kept).
 *   MI2 v14 Acos: 0, 1, -1 and 0.5 give the arccosine in radians, within
 *       2 ulp of the double-precision acos (exactly 0 for 1).
 *   MI3 v14 outside the domain: Asin(2) and Acos(-1.5) are NaN (before and
 *       after the fix).
 *   MI4 Unchanged before v14 (ain->version 13): Asin(1), Asin(0.5),
 *       Acos(0) and Acos(-1) are degrees, computed as before
 *       (float(asinf(x) * (180 / pi))).
 *   MI5 Unchanged: Cos, Sin and Tan take degrees, Atan and Atan2 return
 *       radians (v14).
 *   MI6 The port's choice for engines after v14 (ain->version 15): Asin(1)
 *       and Acos(0) are radians, as in v14.
 *   MI7 Unchanged in older engines too (ain->version 12 and 1; MI4 holds
 *       only the version just below v14): Asin(1) and Acos(0) are degrees,
 *       computed as before.
 *
 * The expected values of MI3-MI5 and MI7 are the C library's own functions
 * of the same float (what the port computed before the fix and computes for
 * them after it), so those comparisons are exact. The character rule does not
 * matter here; the mode runs the same under XS4_PROBE_GBK.
 * Requires forked_cases.inc, first_overload_fixture.inc and
 * math_fixture.inc.
 */

static float mi_call(const char *name, int nargs, const float *args)
{
	int lib = fo_find_lib("Math");
	if (lib < 0)
		xc_env_fail("the AIN declares no Math library");
	int fno = mf_find(lib, name, nargs, AIN_FLOAT);
	if (fno < 0) {
		printf("  ENVIRONMENT: Math.%s/%d (float) is not declared\n", name, nargs);
		fflush(stdout);
		_exit(XC_ENV_EXIT);
	}
	xc_ready();
	union vm_value a[4];
	for (int i = 0; i < nargs; i++)
		a[i].f = args[i];
	return mf_call(lib, fno, a, nargs).f;
}

static bool mi_same(float got, float want)
{
	if (isnan(want))
		return isnan(got);
	return got == want && signbit(got) == signbit(want);
}

static void mi_expect(const char *name, float x, float want, const char *what)
{
	float got = mi_call(name, 1, (float[]){ x });
	XC_CHECK(mi_same(got, want), "%s: %s(%g) = %.9g, want %.9g", what, name, (double)x, (double)got,
		 (double)want);
	printf("  %s %s(%g) = %.9g (want %.9g)\n", what, name, (double)x, (double)got, (double)want);
}

/* Radians within 2 ulp of the double-precision value; a zero exactly, with
 * its sign. */
static void mi_expect_rad(const char *name, float x, double want, const char *what)
{
	float got = mi_call(name, 1, (float[]){ x });
	const float w = (float)want;
	bool ok;
	if (want == 0)
		ok = got == 0 && signbit(got) == signbit(want);
	else
		ok = !isnan(got) && fabs((double)got - want) <= 2 * (double)(nextafterf(fabsf(w), INFINITY) - fabsf(w));
	XC_CHECK(ok, "%s: %s(%g) = %.9g, want %.9g (radians, within 2 ulp)", what, name, (double)x, (double)got, want);
	printf("  %s %s(%g) = %.9g (want %.9g)\n", what, name, (double)x, (double)got, want);
}

/* What the functions gave before the fix (Math.c's rad2deg). */
static float mi_deg(float rad)
{
	return rad * (180.0 / M_PI);
}

static void mi1_asin(void)
{
	const float xs[] = { 1.0f, 0.5f, -1.0f, 0.0f, -0.0f };
	for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++)
		mi_expect_rad("Asin", xs[i], asin((double)xs[i]), "MI1 v14");
	XC_CHECK(fabsf(mi_call("Asin", 1, (float[]){ 1.0f }) - (float)(M_PI / 2)) < 1e-6f,
		 "MI1 v14: Asin(1) is not pi/2");
}

static void mi2_acos(void)
{
	const float xs[] = { 0.0f, 1.0f, -1.0f, 0.5f };
	for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++)
		mi_expect_rad("Acos", xs[i], acos((double)xs[i]), "MI2 v14");
	XC_CHECK(fabsf(mi_call("Acos", 1, (float[]){ -1.0f }) - (float)M_PI) < 1e-6f,
		 "MI2 v14: Acos(-1) is not pi");
}

static void mi3_domain(void)
{
	mi_expect("Asin", 2.0f, NAN, "MI3 v14");
	mi_expect("Acos", -1.5f, NAN, "MI3 v14");
}

static void mi4_v13(void)
{
	const int version = ain->version;
	ain->version = 13;
	mi_expect("Asin", 1.0f, mi_deg(asinf(1.0f)), "MI4 v13");
	mi_expect("Asin", 0.5f, mi_deg(asinf(0.5f)), "MI4 v13");
	mi_expect("Acos", 0.0f, mi_deg(acosf(0.0f)), "MI4 v13");
	mi_expect("Acos", -1.0f, mi_deg(acosf(-1.0f)), "MI4 v13");
	XC_CHECK(fabsf(mi_call("Asin", 1, (float[]){ 1.0f }) - 90.0f) < 1e-4f, "MI4 v13: Asin(1) is not 90");
	ain->version = version;
}

static void mi5_others(void)
{
	mi_expect("Cos", 60.0f, cosf(60.0f * (M_PI / 180.0)), "MI5 v14");
	mi_expect("Sin", 30.0f, sinf(30.0f * (M_PI / 180.0)), "MI5 v14");
	mi_expect("Tan", 45.0f, tanf(45.0f * (M_PI / 180.0)), "MI5 v14");
	mi_expect("Atan", 1.0f, atanf(1.0f), "MI5 v14");
	float got = mi_call("Atan2", 2, (float[]){ 1.0f, -1.0f });
	XC_CHECK(mi_same(got, atan2f(1.0f, -1.0f)), "MI5 v14: Atan2(1, -1) = %.9g, want %.9g", (double)got,
		 (double)atan2f(1.0f, -1.0f));
}

static void mi6_v15(void)
{
	const int version = ain->version;
	ain->version = 15;
	mi_expect_rad("Asin", 1.0f, asin(1.0), "MI6 v15");
	mi_expect_rad("Acos", 0.0f, acos(0.0), "MI6 v15");
	ain->version = version;
}

static void mi7_older(void)
{
	static const int versions[] = { 12, 1 };
	const int version = ain->version;
	for (unsigned i = 0; i < sizeof versions / sizeof versions[0]; i++) {
		char what[16];
		snprintf(what, sizeof what, "MI7 v%d", versions[i]);
		ain->version = versions[i];
		mi_expect("Asin", 1.0f, mi_deg(asinf(1.0f)), what);
		mi_expect("Acos", 0.0f, mi_deg(acosf(0.0f)), what);
	}
	ain->version = version;
}

static int test_math_inverse_trig(void)
{
	static const struct xc_case cases[] = {
		{ "MI1", mi1_asin }, { "MI2", mi2_acos }, { "MI3", mi3_domain },
		{ "MI4", mi4_v13 }, { "MI5", mi5_others }, { "MI6", mi6_v15 },
		{ "MI7", mi7_older },
	};
	return xc_run("MATH-INVERSE-TRIG", cases, sizeof cases / sizeof cases[0], 30);
}
