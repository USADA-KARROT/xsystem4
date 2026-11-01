/* parts-leaks: memory the parts engine kept after it was done with it, with
 * what the scripts see left as it was. Before the fix:
 *   - an ANIMATION state (SetLoopCG) kept a copy of its CG name, which
 *     parts_state_free did not free (when the parts goes, when the state
 *     becomes another type, when PE_Load replaces every parts);
 *   - a second SetLoopCG on the same state replaced that copy without
 *     freeing the first;
 *   - SetNumeralLinkedCGNumberWidthWidthList_by_index, and the by-name one
 *     of engines before v14, loaded the CG only to see that it exists and
 *     never freed it (parts_load_numeral_font_combined does not use it);
 *   - PE_Load did not free the CG name it read for a CG state
 *     (parts_cg_set keeps its own copy).
 * Nothing of this shows; the cases count with the ASan allocator's hooks
 * (forked_cases.inc says why LeakSanitizer is not used).
 *
 *   PK1 The name of an ANIMATION state is freed with the state: when the
 *       parts is released, when the state becomes a CG state, and when
 *       PE_Load replaces the parts; in v14 and with ain->version 13.
 *   PK2 A second SetLoopCG frees the first name; the state then has the
 *       second name, its frames and its size (v14 and 13); a SetLoopCG with
 *       the same name frees the copy it replaces and keeps a new copy (this
 *       binds the way of the fix, free then copy, as upstream 5d032d7 does:
 *       keeping the old copy for the same name would leak nothing either).
 *   PK3 SetNumeralLinkedCGNumberWidthWidthList_by_index, called again and
 *       again with the same CG: no CG stays allocated, neither its struct
 *       nor its pixels (a CG freed with free() instead of cg_free() leaves
 *       its pixels); it returns true and gives the same font each time; for
 *       a CG that does not exist it returns false, as before.
 *   PK4 The by-name one with ain->version 13: no CG stays allocated (struct
 *       nor pixels); true, the same font; false for a name that does not
 *       exist.
 *   PK5 Unchanged: the v14 by-name one (the one this game declares) loads
 *       no CG, before and after the fix (no struct cg nor pixels left), and
 *       returns true also for a name that does not exist (0x5992f0).
 *   PK6 PE_Load frees the CG names it reads: after loading a save of three
 *       CG states twice, no string with those names is left from the first
 *       load, nor any other string (the second load frees what the first
 *       one made for its parts); the states have their CGs.
 *   PK7 Unchanged: a SetLoopCG whose CGs do not all exist fails and keeps
 *       the state as it was: the same name (not freed), the same frames.
 *
 * Not covered: load_parts_cg when the CG a save names does not load (it is
 * gone). The fix frees the name read there whether parts_cg_set succeeds or
 * not, but a case for it cannot count to zero: when the CG does not load,
 * _parts_cg_set returns without freeing the copy parts_cg_set made of the
 * name (string_dup; an existing leak, upstream has it too, outside this
 * mode), and that copy holds the same text, so "no string with the name is
 * left" fails also for a right fix. It waits for the fix of that leak.
 *
 * The CGs are PNG files made in memory (cg_assets_shadow.inc, the PNG
 * writer of construction_cg_fixture.inc); every case has the construction
 * fixture's GL context. The pixels of a CG are told by their bytes: the
 * case loads the CG once itself, outside the tracking window, and counts
 * the blocks of that size with those bytes. Requires forked_cases.inc, construction_fixture.inc
 * and construction_cg_fixture.inc.
 */

#define PK_LOOP "pk/loop%d"
#define PK_ALT "pk/alt%d"
#define PK_STRIP "pk/strip"
#define PK_STILL "pk/still"
enum { PK_DUMMY, PK_LOOP0, PK_LOOP1, PK_ALT0, PK_ALT1, PK_STRIPCG, PK_STILLCG, PK_NR };
enum { PK_STRIP_W = 48, PK_STRIP_H = 8 };
static const struct { const char *name; int w, h; } pk_src[PK_NR] = {
	[PK_DUMMY] = { "pk/dummy", 2, 2 },
	[PK_LOOP0] = { "pk/loop0", 6, 4 },
	[PK_LOOP1] = { "pk/loop1", 7, 5 },
	[PK_ALT0] = { "pk/alt0", 9, 3 },
	[PK_ALT1] = { "pk/alt1", 9, 3 },
	[PK_STRIPCG] = { PK_STRIP, PK_STRIP_W, PK_STRIP_H },
	[PK_STILLCG] = { PK_STILL, 5, 5 },
};
static struct probe_cg_file pk_files[PK_NR];

static void pk_setup(void)
{
	cn_gl_setup();
	for (int i = 0; i < PK_NR; i++) {
		const int w = pk_src[i].w, h = pk_src[i].h;
		uint8_t *px = calloc((size_t)w * h, 4);
		for (int k = 0; k < w * h; k++) {
			px[k * 4] = 20 * i;
			px[k * 4 + 1] = k;
			px[k * 4 + 2] = 255 - k;
			px[k * 4 + 3] = 255;
		}
		pk_files[i].name = pk_src[i].name;
		pk_files[i].data = cg_png(w, h, px, false, &pk_files[i].size);
		free(px);
	}
	probe_install_cg_assets(pk_files, PK_NR);
	xc_ready();
}

static bool pk_loop(int no, const char *name, int start, int frames)
{
	struct string *s = cstr_to_string(name);
	bool r = PE_SetLoopCG(no, s, start, frames, 100, 1);
	free_string(s);
	return r;
}

static struct parts_animation *pk_anim(int no)
{
	struct parts *p = parts_try_get(no);
	return p && p->states[0].type == PARTS_ANIMATION ? &p->states[0].anim : NULL;
}

/* SetLoopCG and the state it made: the name it keeps (a copy, not the
 * argument), two frames and the larger CG's size. */
static struct string *pk_loop_state(int no, const char *name, int w, int h, const char *what)
{
	XC_CHECK(pk_loop(no, name, 0, 2), "%s: SetLoopCG(%s) failed", what, name);
	struct parts_animation *a = pk_anim(no);
	XC_CHECK(a && a->cg_name && !strcmp(a->cg_name->text, name) && a->nr_frames == 2,
		 "%s: the state is not the loop of %s", what, name);
	XC_CHECK(PE_GetPartsWidth(no, 1) == w && PE_GetPartsHeight(no, 1) == h, "%s: the state is %dx%d, want %dx%d",
		 what, PE_GetPartsWidth(no, 1), PE_GetPartsHeight(no, 1), w, h);
	return a ? a->cg_name : NULL;
}

static void pk1_state_free_at(int version)
{
	const int saved_version = ain->version;
	bu_postlink();
	ain->version = version;
	char what[64];

	snprintf(what, sizeof what, "PK1 v%d release", version);
	struct string *name = pk_loop_state(7101, PK_LOOP, 7, 5, what);
	xc_watch(name);
	PE_ReleaseParts(7101);
	XC_CHECK(!parts_try_get(7101), "%s: the parts is still there", what);
	XC_CHECK(xc_watched_freed, "%s: the state's CG name was not freed with the parts", what);

	snprintf(what, sizeof what, "PK1 v%d becomes a CG state", version);
	name = pk_loop_state(7102, PK_LOOP, 7, 5, what);
	xc_watch(name);
	struct string *still = cstr_to_string(PK_STILL);
	XC_CHECK(PE_SetPartsCG(7102, still, 0, 1), "%s: SetPartsCG failed", what);
	free_string(still);
	XC_CHECK(xc_watched_freed, "%s: the CG name was not freed when the state changed type", what);
	XC_CHECK(PE_GetPartsWidth(7102, 1) == 5, "%s: the CG state is %d wide, want 5", what, PE_GetPartsWidth(7102, 1));

	snprintf(what, sizeof what, "PK1 v%d PE_Load", version);
	name = pk_loop_state(7103, PK_LOOP, 7, 5, what);
	struct page *saved = NULL;
	XC_CHECK(PE_Save(&saved) && saved, "%s: PE_Save failed", what);
	xc_watch(name);
	XC_CHECK(saved && PE_Load(&saved), "%s: PE_Load failed", what);
	XC_CHECK(xc_watched_freed, "%s: the old state's CG name was not freed when PE_Load replaced it", what);
	struct parts_animation *a = pk_anim(7103);
	XC_CHECK(a && a->cg_name && a->cg_name != name && !strcmp(a->cg_name->text, PK_LOOP) && a->nr_frames == 2,
		 "%s: the loaded state is not the loop", what);
	if (saved) {
		delete_page_vars(saved);
		free_page(saved);
	}
	ain->version = saved_version;
}

static void pk1_state_free(void)
{
	pk_setup();
	pk1_state_free_at(14);
	pk1_state_free_at(13);
}

static void pk2_replace_at(int version)
{
	const int saved_version = ain->version;
	bu_postlink();
	ain->version = version;
	char what[64];
	snprintf(what, sizeof what, "PK2 v%d", version);
	struct string *first = pk_loop_state(7201, PK_LOOP, 7, 5, what);
	xc_watch(first);
	struct string *second = pk_loop_state(7201, PK_ALT, 9, 3, what);
	XC_CHECK(xc_watched_freed, "%s: the first CG name was not freed by the second SetLoopCG", what);
	XC_CHECK(second && second != first, "%s: the second name is not a new copy", what);
	xc_watch(second);
	struct string *third = pk_loop_state(7201, PK_ALT, 9, 3, what);
	XC_CHECK(xc_watched_freed, "%s: SetLoopCG with the same name did not free the copy it replaced", what);
	XC_CHECK(third && third != second, "%s: the same name is not a new copy", what);
	PE_ReleaseParts(7201);
	ain->version = saved_version;
}

static void pk2_replace(void)
{
	pk_setup();
	pk2_replace_at(14);
	pk2_replace_at(13);
}

#define PK_WIDTHS 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
enum { PK_CALLS = 6 };

static int pk_font_no(int no)
{
	struct parts *p = parts_try_get(no);
	return p && p->states[0].type == PARTS_NUMERAL ? p->states[0].num.font_no : -2;
}

/* Calls one of the two linked-CG functions again and again: once to make
 * the font, then PK_CALLS times with the tracking on. */
static void pk_linked(int no, bool by_index, const char *what)
{
	struct string *name = cstr_to_string(PK_STRIP);
	struct cg *ref = asset_cg_load(PK_STRIPCG);
	XC_CHECK(ref && ref->metrics.w == PK_STRIP_W && ref->metrics.h == PK_STRIP_H, "%s: the strip CG does not load",
		 what);
	if (!ref)
		return;
	bool r = by_index ? PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(no, PK_STRIPCG, PK_WIDTHS, 1)
		: PE_SetNumeralLinkedCGNumberWidthWidthList(no, name, PK_WIDTHS, 1);
	XC_CHECK(r, "%s: the first call returned false", what);
	const int font = pk_font_no(no);
	XC_CHECK(font >= 0, "%s: no font after the first call (%d)", what, font);
	xc_track_begin();
	int ok = 0, same = 0;
	for (int i = 0; i < PK_CALLS; i++) {
		ok += by_index ? PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(no, PK_STRIPCG, PK_WIDTHS, 1)
			: PE_SetNumeralLinkedCGNumberWidthWidthList(no, name, PK_WIDTHS, 1);
		same += pk_font_no(no) == font;
	}
	xc_track_end();
	const int kept = xc_track_cgs(PK_STRIP_W, PK_STRIP_H), pixels = xc_track_pixels(ref);
	printf("  %s: %d calls, %d true, %d same font, %d CGs of %dx%d and %d pixel buffers still allocated\n", what,
	       PK_CALLS, ok, same, kept, PK_STRIP_W, PK_STRIP_H, pixels);
	XC_CHECK(ok == PK_CALLS && same == PK_CALLS, "%s: %d of %d calls true, %d gave font %d", what, ok, PK_CALLS,
		 same, font);
	XC_CHECK(kept == 0, "%s: %d of the %d calls left their CG allocated", what, kept, PK_CALLS);
	XC_CHECK(pixels == 0, "%s: %d of the %d calls left their CG's pixels allocated", what, pixels, PK_CALLS);
	cg_free(ref);
	free_string(name);
}

static void pk3_by_index(void)
{
	pk_setup();
	pk_linked(7301, true, "PK3 v14 by_index");
	XC_CHECK(!PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(7302, PK_NR + 5, PK_WIDTHS, 1),
		 "PK3: a CG number that does not exist returned true");
	const int version = ain->version;
	ain->version = 13;
	pk_linked(7303, true, "PK3 v13 by_index");
	ain->version = version;
}

static void pk4_by_name_v13(void)
{
	pk_setup();
	const int version = ain->version;
	ain->version = 13;
	pk_linked(7401, false, "PK4 v13 by name");
	struct string *missing = cstr_to_string("pk/none");
	XC_CHECK(!PE_SetNumeralLinkedCGNumberWidthWidthList(7402, missing, PK_WIDTHS, 1),
		 "PK4: a CG name that does not exist returned true before v14");
	free_string(missing);
	ain->version = version;
}

static void pk5_by_name_v14(void)
{
	pk_setup();
	struct string *name = cstr_to_string(PK_STRIP), *missing = cstr_to_string("pk/none");
	struct cg *ref = asset_cg_load(PK_STRIPCG);
	XC_CHECK(ref, "PK5: the strip CG does not load");
	if (!ref)
		return;
	int ok = PE_SetNumeralLinkedCGNumberWidthWidthList(7501, name, PK_WIDTHS, 1);
	xc_track_begin();
	for (int i = 0; i < PK_CALLS; i++)
		ok += PE_SetNumeralLinkedCGNumberWidthWidthList(7501, name, PK_WIDTHS, 1);
	ok += PE_SetNumeralLinkedCGNumberWidthWidthList(7502, missing, PK_WIDTHS, 1);
	xc_track_end();
	const int kept = xc_track_cgs(PK_STRIP_W, PK_STRIP_H), pixels = xc_track_pixels(ref);
	printf("  PK5 v14 by name: %d of %d true, %d CGs and %d pixel buffers still allocated\n", ok, PK_CALLS + 2, kept,
	       pixels);
	XC_CHECK(ok == PK_CALLS + 2, "PK5: %d of %d calls returned true", ok, PK_CALLS + 2);
	XC_CHECK(kept == 0 && pixels == 0, "PK5: %d CGs and %d pixel buffers left allocated", kept, pixels);
	cg_free(ref);
	free_string(name);
	free_string(missing);
}

static void pk6_load_names(void)
{
	pk_setup();
	const char *names[3] = { PK_STILL, PK_STRIP, "pk/dummy" };
	const int w[3] = { 5, PK_STRIP_W, 2 };
	for (int i = 0; i < 3; i++) {
		struct string *s = cstr_to_string(names[i]);
		XC_CHECK(PE_SetPartsCG(7601 + i, s, 0, 1), "PK6: SetPartsCG(%s) failed", names[i]);
		free_string(s);
	}
	struct page *saved = NULL;
	XC_CHECK(PE_Save(&saved) && saved, "PK6: PE_Save failed");
	if (!saved)
		return;
	xc_track_begin();
	XC_CHECK(PE_Load(&saved), "PK6: the first PE_Load failed");
	xc_track_end();
	XC_CHECK(PE_Load(&saved), "PK6: the second PE_Load failed");
	const int any = xc_track_any_strings();
	printf("  PK6: %d string(s) in all left from the first load\n", any);
	XC_CHECK(any == 0, "PK6: %d string(s) in all left from the first PE_Load", any);
	for (int i = 0; i < 3; i++) {
		const int kept = xc_track_strings(names[i]);
		printf("  PK6 %s: %d string(s) left from the first load\n", names[i], kept);
		XC_CHECK(kept == 0, "PK6: %d string(s) %s left from the first PE_Load", kept, names[i]);
		struct parts *p = parts_try_get(7601 + i);
		XC_CHECK(p && p->states[0].type == PARTS_CG && p->states[0].cg.name
			 && !strcmp(p->states[0].cg.name->text, names[i]) && PE_GetPartsWidth(7601 + i, 1) == w[i],
			 "PK6: parts %d is not the CG %s after loading", 7601 + i, names[i]);
	}
	delete_page_vars(saved);
	free_page(saved);
}

static void pk7_failed_loop(void)
{
	pk_setup();
	struct string *name = pk_loop_state(7701, PK_LOOP, 7, 5, "PK7");
	struct parts_animation *a = pk_anim(7701);
	Texture *frames = a ? a->frames : NULL;
	xc_watch(name);
	XC_CHECK(!pk_loop(7701, PK_ALT, 0, 3), "PK7: a loop whose third CG does not exist did not fail");
	a = pk_anim(7701);
	XC_CHECK(!xc_watched_freed, "PK7: the failed SetLoopCG freed the state's name");
	XC_CHECK(a && a->cg_name == name && a->frames == frames && a->nr_frames == 2
		 && !strcmp(a->cg_name->text, PK_LOOP), "PK7: the failed SetLoopCG changed the state");
	XC_CHECK(PE_GetPartsWidth(7701, 1) == 7 && PE_GetPartsHeight(7701, 1) == 5, "PK7: the size changed to %dx%d",
		 PE_GetPartsWidth(7701, 1), PE_GetPartsHeight(7701, 1));
}

static int test_parts_leaks(void)
{
	static const struct xc_case cases[] = {
		{ "PK1", pk1_state_free }, { "PK2", pk2_replace }, { "PK3", pk3_by_index },
		{ "PK4", pk4_by_name_v13 }, { "PK5", pk5_by_name_v14 }, { "PK6", pk6_load_names },
		{ "PK7", pk7_failed_loop },
	};
	return xc_run("PARTS-LEAKS", cases, sizeof cases / sizeof cases[0], 60);
}
