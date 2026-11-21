/* Checks for user-component properties across PartsEngine Save/Load.
 *
 * Requires parts_image_fixture.inc to be included first. These are proposed
 * port expectations for investigation. Native component field semantics
 * and addresses are to be checked against the separate static research.
 * PI's ci_setup, br_box, pi_array, pi_save, pi_load and pi_fork are reused.
 * All component names, keys and values here are synthetic.
 * UCI1 checks two names, several independent key/value pairs and post-load
 * updates. UCI2-UCI4 check rejection of truncated, invalid-character and
 * oversized-count PSUC sections. Rejection is a port safety policy; the
 * enclosing PE_Load has already replaced parts and is not atomic.
 * UCI5/UCI6 check raw GBK/SJIS name, key and value bytes under the matching
 * rule; neither assumes conversion. UCI7 checks a v13 save is unchanged by
 * v14 user-component fields and has no PSUC mark. UCI8 checks that
 * SaveWithoutHideParts applies the main parts record's visible/hidden filter
 * to the associated user-component data as well.
 * An image produced by the old writer was not retained here and still needs
 * a separate real old-writer/new-reader test.
 */

static int uci_failures;

#define UCI_CHECK(condition, ...) do { \
	if (!(condition)) { \
		printf("  FAIL: "); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		uci_failures++; \
	} \
} while (0)

static void uci_name(int no, const char *name)
{
	int s = sf_str(name, -1);
	ri_hll("SetUserComponentName", 2, (union vm_value[]){ RI_I(no), RI_I(s) });
	heap_unref(s);
}

static void uci_data(int no, const char *key, const char *value)
{
	int k = sf_str(key, -1), v = sf_str(value, -1);
	ri_hll("SetUserComponentData", 3, (union vm_value[]){ RI_I(no), RI_I(k), RI_I(v) });
	heap_unref(k);
	heap_unref(v);
}

static void uci_expect_name(int no, const char *want, const char *stage)
{
	int s = ri_hll("GetUserComponentName", 1, (union vm_value[]){ RI_I(no) }).i;
	UCI_CHECK(!strcmp(bu_str(s), want), "%s: component %d name = '%s', want '%s'",
		stage, no, bu_str(s), want);
	heap_unref(s);
}

static void uci_expect_data(int no, const char *key, const char *want, const char *stage)
{
	int k = sf_str(key, -1);
	int v = ri_hll("GetUserComponentData", 2, (union vm_value[]){ RI_I(no), RI_I(k) }).i;
	UCI_CHECK(!strcmp(bu_str(v), want), "%s: component %d key '%s' = '%s', want '%s'",
		stage, no, key, bu_str(v), want);
	heap_unref(v);
	heap_unref(k);
}

static void uci_roundtrip(void)
{
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	br_box(PI_PA, 20, 30, 80, 40, true);
	uci_name(PI_P0, "SyntheticComponentA");
	uci_name(PI_PA, "SyntheticComponentB");
	uci_data(PI_P0, "alpha", "value-a");
	uci_data(PI_P0, "beta", "value-b");
	uci_data(PI_PA, "alpha", "value-other");
	uci_expect_name(PI_P0, "SyntheticComponentA", "before save");
	uci_expect_data(PI_P0, "alpha", "value-a", "before save");
	uci_expect_data(PI_PA, "alpha", "value-other", "before save");
	if (uci_failures)
		return;

	int slot = pi_array(true);
	heap_ref(slot);
	UCI_CHECK(pi_save(slot), "Save returned false");
	uci_name(PI_P0, "ChangedAfterSave");
	uci_data(PI_P0, "alpha", "changed");
	uci_data(PI_P0, "later", "created-later");
	UCI_CHECK(pi_load(slot), "Load returned false");
	UCI_CHECK(parts_try_get(PI_P0) && parts_try_get(PI_PA), "saved components did not return");
	if (parts_try_get(PI_P0) && parts_try_get(PI_PA)) {
		uci_expect_name(PI_P0, "SyntheticComponentA", "after load");
		uci_expect_name(PI_PA, "SyntheticComponentB", "after load");
		uci_expect_data(PI_P0, "alpha", "value-a", "after load");
		uci_expect_data(PI_P0, "beta", "value-b", "after load");
		uci_expect_data(PI_P0, "later", "", "after load");
		uci_expect_data(PI_PA, "alpha", "value-other", "after load");
		uci_data(PI_P0, "alpha", "updated");
		uci_expect_data(PI_P0, "alpha", "updated", "update after load");
		uci_expect_data(PI_PA, "alpha", "value-other", "update another component");
	}
	heap_unref(slot);
}

static void uci_case(void)
{
	uci_failures = 0;
	uci_roundtrip();
	pi_failures += uci_failures;
	printf("UCI1 component properties across Save/Load: %s\n", uci_failures ? "FAIL" : "ok");
}

#define UCI_MARK 0x43555350 /* PSUC, port extension */

static void uci_bad_section(int which)
{
	uci_failures = 0;
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	uci_name(PI_P0, "SyntheticComponentA");
	uci_data(PI_P0, "alpha", "value-a");
	int slot = pi_array(true);
	heap_ref(slot);
	if (!pi_save(slot)) {
		UCI_CHECK(false, "Save returned false");
		pi_failures += uci_failures;
		return;
	}
	struct page *image = pi_page(slot);
	int mark = ai_marker(image, UCI_MARK);
	if (mark < 0 || mark + 5 >= image->nr_vars) {
		UCI_CHECK(false, "user-component section missing or ambiguous");
		pi_failures += uci_failures;
		return;
	}
	if (which == 0) image->values[mark + 1].i = 1; /* truncated payload */
	if (which == 1) image->values[mark + 5].i = 0; /* embedded NUL in name */
	if (which == 2) image->values[mark + 2].i = INT_MAX; /* count overflow */
	UCI_CHECK(!pi_load(slot), "invalid user-component section unexpectedly loaded");
	pi_failures += uci_failures;
}

static void uci_truncated(void) { uci_bad_section(0); }
static void uci_bad_character(void) { uci_bad_section(1); }
static void uci_bad_count(void) { uci_bad_section(2); }

static void uci_multibyte(bool gbk)
{
	const char *name = gbk ? "UC_\xd6\xd0\xce\xc4" : "UC_\x82\xa0\x82\xa2";
	const char *key = gbk ? "K_\xb2\xbf\xbc\xfe" : "K_\x95\x94\x95\x69";
	const char *value = gbk ? "V_\xd6\xd0\xce\xc4" : "V_\x82\xa0\x82\xa2";
	uci_failures = 0;
	sys4_set_string_charset(gbk ? SYS4_CHARSET_GBK : SYS4_CHARSET_SJIS);
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	uci_name(PI_P0, name);
	uci_data(PI_P0, key, value);
	uci_expect_name(PI_P0, name, "before save");
	uci_expect_data(PI_P0, key, value, "before save");
	int slot = pi_array(true);
	heap_ref(slot);
	UCI_CHECK(pi_save(slot), "multibyte Save returned false");
	UCI_CHECK(pi_load(slot), "multibyte Load returned false");
	uci_expect_name(PI_P0, name, "after load");
	uci_expect_data(PI_P0, key, value, "after load");
	pi_failures += uci_failures;
}

static void uci_gbk(void) { uci_multibyte(true); }
static void uci_sjis(void) { uci_multibyte(false); }

static void uci_v13(void)
{
	uci_failures = 0;
	ci_setup();
	ain->version = 13;
	probe_init_parts_table();
	struct parts *p = br_box(PI_P0, 10, 20, 100, 50, true);
	struct page *before = NULL, *after = NULL;
	UCI_CHECK(PE_Save(&before) && before, "v13 Save returned false");
	p->user_component_name = xstrdup("synthetic");
	parts_uc_data_set(p, "key", "value");
	UCI_CHECK(PE_Save(&after) && after, "v13 Save after field change returned false");
	UCI_CHECK(pi_same_words(before, after), "v13 save changed with v14 component fields");
	UCI_CHECK(pi_count_mark(before, UCI_MARK) == 0 && pi_count_mark(after, UCI_MARK) == 0,
		 "v13 save contains a PSUC section mark");
	ca_free_buffer(before);
	ca_free_buffer(after);
	ain->version = 14;
	pi_failures += uci_failures;
}

static void uci_visible_only(void)
{
	uci_failures = 0;
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	br_box(PI_PA, 20, 30, 80, 40, true);
	uci_name(PI_P0, "visible");
	uci_data(PI_P0, "key", "visible-value");
	uci_name(PI_PA, "hidden");
	uci_data(PI_PA, "key", "hidden-value");
	PE_SetShow(PI_P0, true);
	PE_SetShow(PI_PA, false);
	PE_UpdateComponent(0);
	UCI_CHECK(parts_try_get(PI_P0) && parts_try_get(PI_P0)->global.show &&
		 parts_try_get(PI_PA) && !parts_try_get(PI_PA)->global.show,
		 "visible/hidden setup was not established");
	struct page *image = NULL;
	UCI_CHECK(PE_SaveWithoutHideParts(&image) && image, "SaveWithoutHideParts failed");
	int mark = ai_marker(image, UCI_MARK);
	UCI_CHECK(mark >= 0 && mark + 3 < image->nr_vars &&
		 image->values[mark + 2].i == 1 && image->values[mark + 3].i == PI_P0,
		 "saved user-component section does not contain exactly the visible part");
	if (image) {
		UCI_CHECK(PE_Load(&image), "visible-only Load failed");
		UCI_CHECK(parts_try_get(PI_P0) && !parts_try_get(PI_PA),
			 "loaded parts records do not match visible-only save");
		if (parts_try_get(PI_P0)) {
			uci_expect_name(PI_P0, "visible", "visible-only load");
			uci_expect_data(PI_P0, "key", "visible-value", "visible-only load");
		}
	}
	ca_free_buffer(image);
	pi_failures += uci_failures;
}

static int test_user_component_image(void)
{
	int failed = pi_fork("UCI1", uci_case);
	failed += pi_fork("UCI2", uci_truncated);
	failed += pi_fork("UCI3", uci_bad_character);
	failed += pi_fork("UCI4", uci_bad_count);
	failed += pi_fork("UCI5", uci_gbk);
	failed += pi_fork("UCI6", uci_sjis);
	failed += pi_fork("UCI7", uci_v13);
	failed += pi_fork("UCI8", uci_visible_only);
	printf("USER-COMPONENT-IMAGE %s: %d of 8 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
