/* activity-image: activity registry round-trips with a v14 parts image.
 *
 * In the original the parts save writes the activity registry (0x540665 ->
 * 0x563b10), and the load clears and restores it (0x56d870 -> 0x563c20).
 * The table includes the activity's component names and numbers, its close
 * names and its end keys. These cases use the public PartsEngine API and
 * synthetic activity trees; they use no game assets or graphics context.
 *
 * AI1 Saves one populated activity, releases it, creates another and loads:
 *     the saved mapping, close names and end keys return, the later activity
 *     goes away, and an activity loaded after that gets distinct parts.
 * AI2 Saves an empty table, creates an activity and loads: the table is empty.
 * AI3 An absent or invalid parts image does not change the activity table.
 *
 * AI4 Two independent activity mappings survive a save/load round trip.
 * AI5 A controller-released marker is restored to its saved state.
 * AI6-AI8 A truncated, unknown-version or oversized section is rejected;
 *     the port's activity table is not partially replaced.
 * AI9 An INT_MAX next number is rejected (port memory-safety guard).
 * AI10 A lower-than-live next number must not let a newly loaded activity
 *      overwrite an existing component (port allocation-safety guard).
 * AI11 (XS4_ACTIVITY_STRESS=1 only) 1025 valid activities can be saved and
 *      loaded. The existing registry has no 1024-entry limit. This requires
 *      more than 530 MiB of activity tables before sanitizer overhead.
 * AI12 A near-INT_MAX next number cannot cause signed overflow on allocation.
 * AI13/AI14 Raw GBK/SJIS activity and close names round-trip under the
 *      matching character rule; no conversion between rules is assumed.
 * AI15 A v13 parts save does not contain the v14 PACT section; changing only
 *      the activity table does not change the saved words (legacy guard).
 *
 * No actual image written by the old version was retained in this fixture.
 * The old writer/reader compatibility test must supply a genuine old image.
 */

static void ai_round_trip(void)
{
	ar_setup();
	ci_add(-1);
	PI_CHECK(ar_load_hll("AI_A", "AROuter"), "AI_A load failed");
	int root = ar_num("AI_A", AR_ROOT);
	int panel = ar_num("AI_A", "Panel");
	PI_CHECK(root >= 0 && panel >= 0 && root != panel, "AI_A did not have two parts");
	BU_PE("AddActivityCloseParts", BU_I(sf_str("AI_A", -1)), BU_I(sf_str("Panel", -1)));
	ek_set("AI_A", 27);
	ek_set("AI_A", 32);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "Save failed");
	int list = ar_int_list();
	PI_CHECK(BU_PE("ReleaseActivity", BU_I(sf_str("AI_A", -1)), BU_I(list)).i, "release failed");
	PI_CHECK(ar_load_hll("AI_B", "AROuter"), "AI_B load failed");
	PI_CHECK(pi_load(slot), "Load failed");
	PI_CHECK(ek_activity("AI_A"), "saved activity missing after load");
	PI_CHECK(!ek_activity("AI_B"), "post-save activity remains after load");
	PI_CHECK(ar_num("AI_A", AR_ROOT) == root && ar_num("AI_A", "Panel") == panel,
		 "saved component mapping was not restored");
	PI_CHECK(parts_try_get(root) && parts_try_get(panel), "saved parts are missing");
	PI_CHECK(BU_PE("IsExistActivityCloseParts", BU_I(sf_str("AI_A", -1)), BU_I(sf_str("Panel", -1))).i,
		 "saved close name missing");
	PI_CHECK(ek_numof("AI_A") == 2 && ek_exists("AI_A", 27) && ek_exists("AI_A", 32),
		 "saved end keys missing");
	PI_CHECK(ar_load_hll("AI_C", "AROuter"), "AI_C load failed");
	int new_root = ar_num("AI_C", AR_ROOT), new_panel = ar_num("AI_C", "Panel");
	PI_CHECK(new_root != root && new_root != panel && new_panel != root && new_panel != panel,
		 "a later activity reused a live saved parts number");
}

static void ai_empty_table(void)
{
	ar_setup();
	ci_add(-1);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "empty-table save failed");
	PI_CHECK(ar_load_hll("AI_LATER", "AROuter"), "later activity load failed");
	PI_CHECK(pi_load(slot), "empty-table load failed");
	PI_CHECK(!ek_activity("AI_LATER"), "later activity survived loading an empty table");
}

static void ai_invalid_image(void)
{
	ar_setup();
	ci_add(-1);
	PI_CHECK(ar_load_hll("AI_GUARD", "AROuter"), "guard activity load failed");
	int root = ar_num("AI_GUARD", AR_ROOT);
	ek_set("AI_GUARD", 27);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(!pi_load(slot), "empty image unexpectedly loaded");
	PI_CHECK(ek_activity("AI_GUARD") && ar_num("AI_GUARD", AR_ROOT) == root && ek_exists("AI_GUARD", 27),
		 "failed load changed the activity table");
}

#define AI_MARK 0x54434150 /* PACT, port extension */

static int ai_marker(struct page *p, int marker)
{
	int found = -1;
	for (int i = 0; p && i < p->nr_vars; i++) {
		if (p->values[i].i != marker)
			continue;
		if (found >= 0)
			return -1;
		found = i;
	}
	return found;
}

static void ai_two_activities(void)
{
	ar_setup();
	ci_add(-1);
	PI_CHECK(ar_load_hll("AI_A", "AROuter") && ar_load_hll("AI_B", "ARInner"),
		 "two activities failed to load");
	int a = ar_num("AI_A", "Panel"), b = ar_num("AI_B", "Base");
	ek_set("AI_A", 27);
	ek_set("AI_B", 32);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot) && pi_load(slot), "two-activity round trip failed");
	PI_CHECK(ar_num("AI_A", "Panel") == a && ar_num("AI_B", "Base") == b,
		 "two-activity component mapping changed");
	PI_CHECK(parts_try_get(a) && parts_try_get(b), "two-activity parts missing");
	PI_CHECK(ek_exists("AI_A", 27) && !ek_exists("AI_A", 32) &&
		 ek_exists("AI_B", 32) && !ek_exists("AI_B", 27), "keys crossed activities");
}

static void ai_release_markers(void)
{
	ar_setup();
	int layer_a = ci_add(-1);
	PI_CHECK(ar_load_hll("AI_A", "AROuter"), "first activity load failed");
	int first = ar_num("AI_A", "Panel");
	ci_erase(layer_a);
	PI_CHECK(pe_v14_activity_number_released(first), "first release marker missing before save");
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "release-state save failed");
	int layer_b = ci_add(-1);
	PI_CHECK(ar_load_hll("AI_B", "AROuter"), "second activity load failed");
	int second = ar_num("AI_B", "Panel");
	ci_erase(layer_b);
	PI_CHECK(pe_v14_activity_number_released(second), "second release marker missing before load");
	PI_CHECK(pi_load(slot), "release-state load failed");
	PI_CHECK(pe_v14_activity_number_released(first), "saved release marker not restored");
	PI_CHECK(!pe_v14_activity_number_released(second), "post-save release marker survived");
}

static void ai_bad_section(int which)
{
	ar_setup();
	ci_add(-1);
	PI_CHECK(ar_load_hll("AI_SAVED", "AROuter"), "saved activity load failed");
	int slot = pi_array(true);
	heap_ref(slot);
	if (!pi_save(slot)) { PI_CHECK(false, "Save failed"); return; }
	struct page *image = pi_page(slot);
	int mark = ai_marker(image, AI_MARK);
	if (mark < 0 || mark + 5 >= image->nr_vars) {
		PI_CHECK(false, "activity section missing or ambiguous");
		return;
	}
	if (which == 0) image->values[mark + 1].i = 1; /* truncated payload */
	if (which == 1) image->values[mark + 2].i = 999; /* unknown version */
	if (which == 2) image->values[mark + 3].i = INT_MAX; /* count overflow */
	if (which == 3) image->values[mark + 4].i = INT_MAX; /* next overflow */
	PI_CHECK(ar_load_hll("AI_KEEP", "AROuter"), "guard activity load failed");
	PI_CHECK(!pi_load(slot), "invalid section unexpectedly accepted");
	PI_CHECK(ek_activity("AI_KEEP"), "invalid section partially replaced the activity table");
}
static void ai_truncated(void) { ai_bad_section(0); }
static void ai_bad_version(void) { ai_bad_section(1); }
static void ai_bad_count(void) { ai_bad_section(2); }
static void ai_max_next(void) { ai_bad_section(3); }

static void ai_low_next(void)
{
	ar_setup();
	ci_add(-1);
	PI_CHECK(ar_load_hll("AI_SAVED", "AROuter"), "activity load failed");
	int old_root = ar_num("AI_SAVED", AR_ROOT);
	int slot = pi_array(true);
	heap_ref(slot);
	if (!pi_save(slot)) { PI_CHECK(false, "Save failed"); return; }
	struct page *image = pi_page(slot);
	int mark = ai_marker(image, AI_MARK);
	if (mark < 0 || mark + 4 >= image->nr_vars) {
		PI_CHECK(false, "activity section missing or ambiguous");
		return;
	}
	image->values[mark + 4].i = old_root;
	if (!pi_load(slot))
		return; /* rejecting a corrupt allocation counter is safe */
	PI_CHECK(ar_load_hll("AI_AFTER", "AROuter"), "new activity failed after load");
	PI_CHECK(ar_num("AI_AFTER", AR_ROOT) != old_root &&
		 ar_num("AI_SAVED", AR_ROOT) == old_root,
		 "new activity reused the restored root number");
}

static void ai_many(void)
{
	ar_setup();
	ci_add(-1);
	for (int i = 0; i < 1025; i++) {
		char name[32];
		snprintf(name, sizeof name, "AI_MANY_%04d", i);
		if (!BU_PE("CreateActivity", BU_I(sf_str(name, -1))).i) {
			PI_CHECK(false, "CreateActivity failed at %d", i);
			return;
		}
	}
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "1025 activities could not be saved");
	PI_CHECK(pi_load(slot), "1025 activities could not be loaded");
	PI_CHECK(ek_activity("AI_MANY_0000") && ek_activity("AI_MANY_1024"),
		 "1025-activity table did not round trip");
}

static void ai_near_max_next(void)
{
	ar_setup();
	ci_add(-1);
	PI_CHECK(ar_load_hll("AI_SAVED", "AROuter"), "activity load failed");
	int slot = pi_array(true);
	heap_ref(slot);
	if (!pi_save(slot)) { PI_CHECK(false, "Save failed"); return; }
	struct page *image = pi_page(slot);
	int mark = ai_marker(image, AI_MARK);
	if (mark < 0 || mark + 4 >= image->nr_vars) {
		PI_CHECK(false, "activity section missing or ambiguous");
		return;
	}
	image->values[mark + 4].i = INT_MAX - 1;
	if (!pi_load(slot))
		return; /* rejecting an unsafe counter is also safe */
	/* Loading this tree allocates a root and children. Either safely refusing
	 * further allocations or completing without overflow is acceptable. */
	(void)ar_load_hll("AI_AFTER", "AROuter");
}

static void ai_multibyte(bool gbk)
{
	/* Distinct byte sequences are passed and checked unchanged. The section
	 * format is a port representation of names as bytes, not a claim that
	 * the original stores these names in the same encoding or layout. */
	const char *name = gbk ? "AI_\xd6\xd0\xce\xc4" : "AI_\x82\xa0\x82\xa2";
	const char *close = gbk ? "\xb2\xbf\xbc\xfe" : "\x95\x94\x95\x69";
	sys4_set_string_charset(gbk ? SYS4_CHARSET_GBK : SYS4_CHARSET_SJIS);
	ar_setup();
	ci_add(-1);
	PI_CHECK(ar_load_hll(name, "AROuter"), "multibyte activity load failed");
	int root = ar_num(name, AR_ROOT);
	BU_PE("AddActivityCloseParts", BU_I(sf_str(name, -1)), BU_I(sf_str(close, -1)));
	ek_set(name, 27);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "multibyte activity save failed");
	int list = ar_int_list();
	PI_CHECK(BU_PE("ReleaseActivity", BU_I(sf_str(name, -1)), BU_I(list)).i,
		 "multibyte activity release failed");
	PI_CHECK(pi_load(slot), "multibyte activity load failed");
	PI_CHECK(ek_activity(name) && ar_num(name, AR_ROOT) == root,
		 "multibyte activity name or root changed");
	PI_CHECK(BU_PE("IsExistActivityCloseParts", BU_I(sf_str(name, -1)), BU_I(sf_str(close, -1))).i,
		 "multibyte close name changed");
	PI_CHECK(ek_exists(name, 27), "multibyte activity end key changed");
}

static void ai_gbk(void) { ai_multibyte(true); }
static void ai_sjis(void) { ai_multibyte(false); }

static void ai_v13(void)
{
	ar_setup();
	ci_add(-1);
	ain->version = 13;
	struct page *before = NULL, *after = NULL;
	PI_CHECK(PE_Save(&before) && before, "v13 save failed");
	PI_CHECK(BU_PE("CreateActivity", BU_I(sf_str("AI_OLD_GUARD", -1))).i,
		 "v13 guard activity creation failed");
	PI_CHECK(PE_Save(&after) && after, "v13 save after table change failed");
	PI_CHECK(pi_same_words(before, after), "v13 save changed with the activity table");
	PI_CHECK(pi_count_mark(before, AI_MARK) == 0 && pi_count_mark(after, AI_MARK) == 0,
		 "v13 save contains a PACT section mark");
	ca_free_buffer(before);
	ca_free_buffer(after);
	ain->version = 14;
}

static int test_activity_image(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "AI1", ai_round_trip }, { "AI2", ai_empty_table }, { "AI3", ai_invalid_image },
		{ "AI4", ai_two_activities }, { "AI5", ai_release_markers },
		{ "AI6", ai_truncated }, { "AI7", ai_bad_version }, { "AI8", ai_bad_count },
		{ "AI9", ai_max_next }, { "AI10", ai_low_next },
		{ "AI12", ai_near_max_next },
		{ "AI13", ai_gbk }, { "AI14", ai_sjis }, { "AI15", ai_v13 },
	};
	int failed = 0;
	int n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += pi_fork(cases[i].name, cases[i].fn);
	if (getenv("XS4_ACTIVITY_STRESS")) {
		failed += pi_fork("AI11", ai_many);
		n++;
	}
	printf("ACTIVITY-IMAGE %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
