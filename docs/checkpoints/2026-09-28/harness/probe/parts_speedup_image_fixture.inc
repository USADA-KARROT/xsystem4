/* parts-speedup-image: the speed-up flag (SetEnableSpeedUp / IsEnableSpeedUp)
 * across a parts engine Save and Load. The original keeps the flag in each
 * component's parameter block: the setter (0x590a36) and the getter
 * (0x590ac8) use parts +0x22d, which is the block's +0x1a9 (the block is at
 * +0x84); the block's save writes it as 0/1 (0x551771, 0x55179a) and its
 * load reads it back from data version 36 on (0x551ee4, 0x551ee9 ->
 * 0x72c6b0, which stores "byte == 1"). A new component starts enabled
 * (0x550a9c). The XPE image keeps it in an optional section of the parts
 * that have it disabled, after the activity section and before the
 * controller ID section.
 *
 * Requires parts_image_fixture.inc (ci_setup, br_box, pi_array, pi_save,
 * pi_load, pi_page, pi_same_words, pi_count_mark, pi_fork), the
 * activity image fixture (ai_marker) and the dialogue fade fixture
 * (df_setup, df_text, df_layout, df_alpha) to be included first.
 *
 * Cases (each in its own process):
 *   PSU1a A save into the caller's array (HLL Save/Load): of two parts, the
 *         disabled one is disabled after the load and the enabled one
 *         enabled, although both were changed after the save.
 *   PSU1b The same through a one-slot wrap box.
 *   PSU2  The engine's own Save/Load (no adapter), five parts of which
 *         three are disabled: the section lists exactly those three, in
 *         list order, and they come back; the others stay enabled.
 *   PSU3  Two round trips: a save made after the load has the words of the
 *         first save, and loading it again gives the same flags.
 *   PSU4  SaveWithoutHideParts and Save: the section holds only parts that
 *         have a record (visible; hidden only with Save; never a parts not
 *         to be saved), and the load does not make the left-out parts.
 *   PSU5a With the controller ID section: the new section lies before it,
 *         the controller section stays the last words, and the layers and
 *         the active one come back. A layer's root parts with the flag
 *         disabled is not listed (roots have no record and come with their
 *         controllers) and is enabled after the load.
 *   PSU5b A single controller (no controller ID section): the new section
 *         is the end of the image and is read back.
 *   PSU6  An image without the section (what an engine before this section
 *         writes): it loads, every parts is enabled (the default), and the
 *         activity and controller sections are still read. The words of the
 *         image without the section are printed, to be compared with an
 *         engine before the section.
 *   PSU7  An engine before v14: a save with a disabled parts has the words of
 *         the same save without it, and no section.
 *   PSU8  A section that does not read as one (a count past the end, a
 *         negative count) fails the load; an entry naming no parts makes
 *         none, and an entry naming a controller root leaves the root as it
 *         was.
 *   PSU9  After the load a disabled parts' message window takes the passed
 *         time and an enabled one the scaled time (the window is made after
 *         the load, so only the flag is under test).
 * No game assets are used.
 */

#define PSU_MARK 0x55505350 /* "PSPU", port extension */
#define PSU_PCID 0x44494350 /* "PCID" */
#define PSU_PACT 0x54434150 /* "PACT" */
#define PSU_D1 7111
#define PSU_D2 7112
#define PSU_D3 7113
#define PSU_E1 7114
#define PSU_E2 7115

static void psu_set(int no, bool enable) { ri_hll("SetEnableSpeedUp", 2, (union vm_value[]){ RI_I(no), RI_I(enable) }); }
static int psu_get(int no) { return ri_hll("IsEnableSpeedUp", 1, (union vm_value[]){ RI_I(no) }).i; }

/* The script's calls reach the engine's functions (not the empty ones). */
static void psu_check_binding(void)
{
	int lib, s = ri_fn("SetEnableSpeedUp", &lib), g = ri_fn("IsEnableSpeedUp", &lib);
	if (libraries[lib][s].fun != (void *)PE_SetEnableSpeedUp || libraries[lib][g].fun != (void *)PE_IsEnableSpeedUp) {
		printf("  SETUP: SetEnableSpeedUp/IsEnableSpeedUp are not bound to the engine\n");
		fflush(NULL);
		_exit(2);
	}
}

/* The field and the getter agree. */
static int psu_enabled(int no)
{
	struct parts *p = parts_try_get(no);
	if (!p)
		return -1;
	int via = psu_get(no);
	PI_CHECK(via == !p->speed_up_disabled, "parts %d: IsEnableSpeedUp %d, field disabled %d", no, via, p->speed_up_disabled);
	return via;
}

static void psu_expect(const char *step, int no, int want)
{
	int got = psu_enabled(no);
	PI_CHECK(got == want, "%s: parts %d speed-up enabled %d, want %d", step, no, got, want);
}

/* Index of the section's mark (only one), its count and entries. */
static int psu_section(struct page *p, int *count)
{
	int m = ai_marker(p, PSU_MARK);
	if (m < 0 || m + 1 >= p->nr_vars)
		return -1;
	*count = p->values[m + 1].i;
	return m;
}

static bool psu_lists(struct page *p, int n, const int *want)
{
	int count, m = psu_section(p, &count);
	if (m < 0 || count != n || m + 2 + n > p->nr_vars)
		return false;
	for (int i = 0; i < n; i++)
		if (p->values[m + 2 + i].i != want[i])
			return false;
	return true;
}

/* The words of `p` without the section (a copy). */
static struct page *psu_strip(struct page *p)
{
	struct page *c = copy_page(p);
	int count, m = psu_section(c, &count);
	if (m < 0)
		return c;
	int cut = 2 + count;
	memmove(&c->values[m], &c->values[m + cut], (c->nr_vars - m - cut) * sizeof(c->values[0]));
	c->nr_vars -= cut;
	return c;
}

static unsigned psu_fnv(struct page *p)
{
	unsigned h = 2166136261u;
	for (int i = 0; p && i < p->nr_vars; i++) {
		unsigned w = (unsigned)p->values[i].i;
		for (int b = 0; b < 4; b++) {
			h ^= (w >> (8 * b)) & 0xff;
			h *= 16777619u;
		}
	}
	return h;
}

/* default layer: D1 disabled, E1 enabled */
static void psu_two(void)
{
	ci_setup();
	psu_check_binding();
	br_box(PSU_D1, 10, 20, 100, 50, true);
	br_box(PSU_E1, 30, 40, 120, 60, true);
	psu_set(PSU_D1, false);
	psu_set(PSU_E1, true);
	psu_expect("before the save", PSU_D1, 0);
	psu_expect("before the save", PSU_E1, 1);
}

static void psu_hll_roundtrip(int slot)
{
	PI_CHECK(pi_save(slot), "Save returned false");
	psu_set(PSU_D1, true);
	psu_set(PSU_E1, false);
	PI_CHECK(pi_load(slot), "Load returned false");
	psu_expect("after the load", PSU_D1, 0);
	psu_expect("after the load", PSU_E1, 1);
}

static void psu1a(void)
{
	psu_two();
	int slot = pi_array(true);
	heap_ref(slot);
	psu_hll_roundtrip(slot);
	printf("PSU1a save into an array: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu1b(void)
{
	psu_two();
	int box = variable_initval(AIN_WRAP).i;
	heap_ref(box);
	psu_hll_roundtrip(box);
	printf("PSU1b save into a wrap box: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu_five(void)
{
	ci_setup();
	psu_check_binding();
	static const int nos[] = { PSU_D1, PSU_E1, PSU_D2, PSU_E2, PSU_D3 };
	for (int i = 0; i < 5; i++)
		br_box(nos[i], 10 * i, 20, 50, 50, true);
	psu_set(PSU_D1, false);
	psu_set(PSU_D2, false);
	psu_set(PSU_D3, false);
}

static void psu_expect_five(const char *step)
{
	psu_expect(step, PSU_D1, 0);
	psu_expect(step, PSU_D2, 0);
	psu_expect(step, PSU_D3, 0);
	psu_expect(step, PSU_E1, 1);
	psu_expect(step, PSU_E2, 1);
}

static void psu_flip_five(void)
{
	psu_set(PSU_D1, true);
	psu_set(PSU_D2, true);
	psu_set(PSU_D3, true);
	psu_set(PSU_E1, false);
	psu_set(PSU_E2, false);
}

static void psu2(void)
{
	psu_five();
	struct page *image = NULL;
	PI_CHECK(PE_Save(&image) && image, "PE_Save failed");
	static const int want[] = { PSU_D1, PSU_D2, PSU_D3 };
	PI_CHECK(psu_lists(image, 3, want), "the section does not list exactly the three disabled parts in list order");
	psu_flip_five();
	PI_CHECK(image && PE_Load(&image), "PE_Load failed");
	psu_expect_five("after the load");
	ca_free_buffer(image);
	printf("PSU2 the engine's own Save/Load, several parts: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu3(void)
{
	psu_five();
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "first Save returned false");
	struct page *first = pi_page(slot) ? copy_page(pi_page(slot)) : NULL;
	psu_flip_five();
	PI_CHECK(pi_load(slot), "first Load returned false");
	psu_expect_five("after the first load");
	int again = pi_array(true);
	heap_ref(again);
	PI_CHECK(pi_save(again), "second Save returned false");
	PI_CHECK(pi_same_words(first, pi_page(again)), "the save made after the load has other words");
	psu_flip_five();
	PI_CHECK(pi_load(again), "second Load returned false");
	psu_expect_five("after the second load");
	printf("PSU3 two round trips: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu4(void)
{
	ci_setup();
	psu_check_binding();
	/* D1 visible, D2 hidden, D3 not to be saved: all disabled; E1 visible */
	br_box(PSU_D1, 10, 20, 100, 50, true);
	br_box(PSU_D2, 20, 30, 80, 40, true);
	br_box(PSU_D3, 30, 40, 60, 30, true);
	br_box(PSU_E1, 40, 50, 50, 20, true);
	psu_set(PSU_D1, false);
	psu_set(PSU_D2, false);
	psu_set(PSU_D3, false);
	PE_SetShow(PSU_D1, true);
	PE_SetShow(PSU_D2, false);
	PE_SetShow(PSU_D3, true);
	PE_SetShow(PSU_E1, true);
	PE_parts_set_want_save(PSU_D3, false);
	PE_UpdateComponent(0);
	PI_CHECK(parts_try_get(PSU_D1)->global.show && !parts_try_get(PSU_D2)->global.show && !parts_try_get(PSU_D3)->want_save,
		 "visible/hidden/not-saved setup was not established");
	struct page *all = NULL, *visible = NULL;
	PI_CHECK(PE_Save(&all) && all, "PE_Save failed");
	PI_CHECK(PE_SaveWithoutHideParts(&visible) && visible, "SaveWithoutHideParts failed");
	static const int want_all[] = { PSU_D1, PSU_D2 }, want_visible[] = { PSU_D1 };
	PI_CHECK(psu_lists(all, 2, want_all), "Save: the section does not list exactly the visible and the hidden parts");
	PI_CHECK(psu_lists(visible, 1, want_visible), "SaveWithoutHideParts: the section does not list exactly the visible parts");
	psu_set(PSU_D1, true);
	PI_CHECK(visible && PE_Load(&visible), "visible-only Load failed");
	psu_expect("visible-only load", PSU_D1, 0);
	psu_expect("visible-only load", PSU_E1, 1);
	PI_CHECK(!parts_try_get(PSU_D2) && !parts_try_get(PSU_D3), "the load made a left-out parts: hidden %d, not saved %d",
		 !!parts_try_get(PSU_D2), !!parts_try_get(PSU_D3));
	PI_CHECK(all && PE_Load(&all), "Load of Save failed");
	psu_expect("load of Save", PSU_D1, 0);
	psu_expect("load of Save", PSU_D2, 0);
	PI_CHECK(!parts_try_get(PSU_D3), "the load made the parts not to be saved");
	ca_free_buffer(all);
	ca_free_buffer(visible);
	printf("PSU4 only parts with a record: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu5a(void)
{
	psu_two();
	int a = ci_add(-1);
	br_box(PSU_D2, 50, 60, 140, 70, true);
	psu_set(PSU_D2, false);
	psu_set(a, false);	/* the layer's root, even when marked to be saved */
	PE_parts_set_want_save(a, true);
	ci_set_active(a);
	int nr = ci_len(), ids[8], active = ci_active();
	for (int i = 0; i < nr && i < 8; i++)
		ids[i] = ci_id_at(i);
	struct page *image = NULL;
	PI_CHECK(PE_Save(&image) && image, "PE_Save failed");
	int count, m = image ? psu_section(image, &count) : -1;
	int c = image ? ai_marker(image, PSU_PCID) : -1;
	PI_CHECK(m >= 0 && c >= 0 && m < c && m + 2 + count == c, "the section (%d) does not lie just before the controller section (%d)", m, c);
	PI_CHECK(c >= 0 && image->values[c + 1].i == nr && c + nr + 3 == image->nr_vars,
		 "the controller section is not the end of the image");
	int p = image ? ai_marker(image, PSU_PACT) : -1;
	PI_CHECK(p >= 0 && m > p, "the section (%d) is not after the activity section (%d)", m, p);
	static const int want[] = { PSU_D1, PSU_D2 };
	PI_CHECK(image && psu_lists(image, 2, want), "the section does not list exactly the two disabled parts (and not the root)");
	psu_set(PSU_D1, true);
	psu_set(PSU_D2, true);
	ci_set_active(ci_id_at(0));
	PI_CHECK(image && PE_Load(&image), "PE_Load failed");
	psu_expect("after the load", PSU_D1, 0);
	psu_expect("after the load", PSU_D2, 0);
	psu_expect("after the load", PSU_E1, 1);
	psu_expect("the root after the load", a, 1);
	PI_CHECK(ci_len() == nr && ci_active() == active, "layers %d active %d, want %d %d", ci_len(), ci_active(), nr, active);
	for (int i = 0; i < nr && i < 8; i++)
		PI_CHECK(ci_id_at(i) == ids[i], "layer %d is %d, want %d", i, ci_id_at(i), ids[i]);
	ca_free_buffer(image);
	printf("PSU5a with the controller ID section: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu5b(void)
{
	extern bool parts_multi_controller;
	if (parts_multi_controller) {
		printf("  SETUP: the multi controller is on\n");
		fflush(NULL);
		_exit(2);
	}
	probe_init_parts_table();
	br_box(PSU_D1, 10, 20, 100, 50, true);
	br_box(PSU_E1, 30, 40, 120, 60, true);
	PE_SetEnableSpeedUp(PSU_D1, false);
	struct page *image = NULL;
	PI_CHECK(PE_Save(&image) && image, "PE_Save failed");
	int count, m = image ? psu_section(image, &count) : -1;
	PI_CHECK(image && !pi_count_mark(image, PSU_PCID), "a single-controller image has a controller section");
	PI_CHECK(m >= 0 && count == 1 && m + 3 == image->nr_vars && image->values[m + 2].i == PSU_D1,
		 "the section is not the end of a single-controller image (mark %d of %d words)", m, image ? image->nr_vars : -1);
	PE_SetEnableSpeedUp(PSU_D1, true);
	PE_SetEnableSpeedUp(PSU_E1, false);
	PI_CHECK(image && PE_Load(&image), "PE_Load failed");
	PI_CHECK(parts_try_get(PSU_D1) && !PE_IsEnableSpeedUp(PSU_D1), "D1 is not disabled after the load");
	PI_CHECK(parts_try_get(PSU_E1) && PE_IsEnableSpeedUp(PSU_E1), "E1 is not enabled after the load");
	ca_free_buffer(image);
	printf("PSU5b a single controller: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu6(void)
{
	psu_two();
	int a = ci_add(-1);
	ci_set_active(a);
	int nr = ci_len(), active = ci_active();
	struct page *image = NULL;
	PI_CHECK(PE_Save(&image) && image, "PE_Save failed");
	struct page *old = image ? psu_strip(image) : NULL;
	PI_CHECK(old && !pi_count_mark(old, PSU_MARK) && ai_marker(old, PSU_PACT) >= 0 && ai_marker(old, PSU_PCID) >= 0,
		 "the image without the section lost another section");
	printf("PSU6 image without the section: words=%d fnv=%08x\n", old ? old->nr_vars : -1, psu_fnv(old));
	psu_set(PSU_E1, false);
	ci_set_active(ci_id_at(0));
	PI_CHECK(old && PE_Load(&old), "PE_Load of the image without the section failed");
	psu_expect("after the load", PSU_D1, 1);
	psu_expect("after the load", PSU_E1, 1);
	PI_CHECK(ci_len() == nr && ci_active() == active, "layers %d active %d, want %d %d", ci_len(), ci_active(), nr, active);
	ca_free_buffer(image);
	ca_free_buffer(old);
	printf("PSU6 an image without the section: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu7(void)
{
	ci_setup();
	ain->version = 13;
	probe_init_parts_table();
	br_box(PSU_D1, 10, 20, 100, 50, true);
	struct page *plain = NULL, *with = NULL;
	PI_CHECK(PE_Save(&plain), "PE_Save failed");
	parts_get(PSU_D1)->speed_up_disabled = true;
	PI_CHECK(PE_Save(&with), "PE_Save failed");
	PI_CHECK(pi_same_words(plain, with) && !pi_count_mark(with, PSU_MARK),
		 "a v13 save with a disabled parts differs from the one without (%d and %d words)", plain ? plain->nr_vars : -1,
		 with ? with->nr_vars : -1);
	ca_free_buffer(plain);
	ca_free_buffer(with);
	ain->version = 14;
	printf("PSU7 engines before v14: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu8(void)
{
	psu_two();
	int a = ci_add(-1);
	int root = a;
	struct page *image = NULL;
	PI_CHECK(PE_Save(&image) && image, "PE_Save failed");
	int count, m = image ? psu_section(image, &count) : -1;
	PI_CHECK(m >= 0 && count == 1, "no section of one entry (mark %d)", m);
	if (m < 0) {
		ca_free_buffer(image);
		return;
	}
	struct page *bad = copy_page(image);
	bad->values[m + 1].i = image->nr_vars;	/* past the end */
	PI_CHECK(!PE_Load(&bad), "a count past the end loaded");
	ca_free_buffer(bad);
	bad = copy_page(image);
	bad->values[m + 1].i = -1;
	PI_CHECK(!PE_Load(&bad), "a negative count loaded");
	ca_free_buffer(bad);
	bad = copy_page(image);
	bad->values[m + 2].i = 7199;	/* no such parts */
	PI_CHECK(PE_Load(&bad), "an entry naming no parts failed the load");
	PI_CHECK(!parts_try_get(7199), "an entry naming no parts made one");
	psu_expect("entry naming no parts", PSU_D1, 1);
	ca_free_buffer(bad);
	bad = copy_page(image);
	bad->values[m + 2].i = root;	/* a controller root */
	PI_CHECK(PE_Load(&bad), "an entry naming a controller root failed the load");
	struct parts *r = parts_try_get(root);
	PI_CHECK(r && parts_is_controller_root(r) && !r->speed_up_disabled, "the controller root %d: %s", root,
		 !r ? "missing" : !parts_is_controller_root(r) ? "no root" : "disabled");
	ca_free_buffer(bad);
	ca_free_buffer(image);
	printf("PSU8 sections that do not read as one: %s\n", pi_failures ? "FAIL" : "ok");
}

static void psu9(void)
{
	df_setup(.5f);
	psu_check_binding();
	void (*update)(int, int) = dlsym(RTLD_DEFAULT, "parts_message_window_update_times");
	if (!update)
		df_no_symbol("parts_message_window_update_times");
	br_box(9820, 10, 20, 100, 50, true);
	br_box(9821, 30, 40, 100, 50, true);
	psu_set(9820, false);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "Save returned false");
	psu_set(9820, true);
	psu_set(9821, false);
	PI_CHECK(pi_load(slot), "Load returned false");
	df_text(9820, "AB", 41);
	df_text(9821, "AB", 42);
	BU_PE("SetMessageWindowTextSpeed", BU_I(9820), BU_I(100));
	BU_PE("SetMessageWindowTextSpeed", BU_I(9821), BU_I(100));
	struct parts_text *t0 = df_layout(9820), *t1 = df_layout(9821);
	update(20, 100);
	float a0 = df_alpha(9820, t0, 0), a1 = df_alpha(9821, t1, 0);
	PI_CHECK(fabsf(a0 - .1f) < .001f, "disabled after the load: first character alpha %.4f, want 0.1 (passed time)", a0);
	PI_CHECK(fabsf(a1 - .5f) < .001f, "enabled after the load: first character alpha %.4f, want 0.5 (scaled time)", a1);
	printf("PSU9 the flag selects the time after the load: %s\n", pi_failures ? "FAIL" : "ok");
}

static int test_parts_speedup_image(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "PSU1a", psu1a }, { "PSU1b", psu1b }, { "PSU2", psu2 }, { "PSU3", psu3 }, { "PSU4", psu4 },
		{ "PSU5a", psu5a }, { "PSU5b", psu5b }, { "PSU6", psu6 }, { "PSU7", psu7 }, { "PSU8", psu8 },
		{ "PSU9", psu9 },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += pi_fork(cases[i].name, cases[i].fn);
	printf("PARTS-SPEEDUP-IMAGE %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
