/* parts-sound-name: the v14 parts' sound names (the on-cursor and the click
 * sound) and the on-cursor show link (G4, 2026-10-25). Read statically from
 * the original:
 *  - the component parser 0x553650 (its this is the component + 0x84, the
 *    only call 0x5390e7) reads オン指針音效 (key 0x7db0b4) into the string
 *    slot 2 of the four at +0x12c (0x554a11..0x554a49, 0x554bd0: i * 0x18)
 *    and 點擊音效 (0x7db0a4) into slot 3 (0x554a77..0x554aaf), both with an
 *    empty default; slots 0 and 1 stay empty (the block constructor makes
 *    four empty strings, 0x550a27..0x550a6a). Every component of an
 *    activity, the root as well, goes through this parser (0x55f8f0 from
 *    both loaders 0x560b20 and 0x5621d0).
 *  - Parts_SetSoundName(Number, Name, State) (0x590230) and
 *    Parts_GetSoundName(Number, State) (0x5902e0) write and read slot State
 *    of the component the get-or-create lookup gives (0x540250: none for a
 *    number of 0 or less); neither looks at the name.
 *  - The sound: a low-level parts copies the slots 1..3 into its widget
 *    every update (0x565200 -> 0x5b7f20), where slots 2 and 3 each get a
 *    CSound when the name is not empty (0x5b7590 -> 0x5bac00; SeID 3); the
 *    cursor walk's enter (0x577b90) plays slot 2 (0x564f20) whenever the
 *    element is newly entered, whether or not the left button is down on
 *    it; the release that sends a parts its own MouseClick plays slot 3
 *    (0x578990..0x5789bc). Parts_PlaySound(Number, State) (0x59b220): 2 is
 *    slot 2, 3 is slot 3, anything else nothing.
 *  - A disabled button empties its inner parts' slots 2 and 3 every update
 *    (0x5285b0: 0x528787..0x528802) and copies its own back while enabled
 *    (0x5286ed..0x52876a); its own slots are not touched.
 *  - オン指針表示連動 (0x7db054) is resolved at once, by name, in the
 *    activity's name table (0x55df10: the number, or 0 when the name is not
 *    there), which the loader fills for every component before it reads any
 *    (0x560b20 -> 0x55f6e0, then 0x55f8f0): the order of the components does
 *    not matter. The number goes to +0x124; Parts_SetOnCursorShowLinkPartsNumber
 *    (0x58e6d0) writes it, Parts_GetOnCursorShowLinkPartsNumber (0x58e770)
 *    reads it; no link is 0 (0x550927).
 *  - The component's save block holds the four names and the link number
 *    (0x550f00 writes them at 0x551676 and 0x551302, 0x5517b0 reads them back
 *    at 0x551e62 and 0x551be4).
 *  - SetClickMissSoundName (0x57bd27) / GetClickMissSoundName (0x57bd66)
 *    write and read one string of the engine (+0x1e4); the game sets it to
 *    "" only (parts::detail::WaitForClick); no reader was found.
 *  - The component's activity name table is keyed by the short names of
 *    every component under the activity node, the root included (0x5616f0
 *    walks them and its 子部件 recursively; 0x5617cc..0x561844 insert each
 *    short name with 0x562ac0); 0x55df10 looks a link up in that table.
 * Port strategies, checked here as such: a name is played as the number the
 * sound archive gives it (the original plays it by name through PlaySe 3);
 * an empty name, or one the archive does not have, plays nothing and asks
 * the archive for nothing (no "Failed to load WAV -1"), also when it
 * replaces a name that played; a State outside 0..3 writes nothing and
 * reads "" (the original indexes past its four slots); the click-miss name
 * is kept and not played, and reads "" before any Set (the original's
 * string at +0x1e4 is presumed default-constructed: 0x53ed10 does not write
 * it, the constructors of its sub-objects were not read). Not pinned here:
 * when a Set takes effect. The original copies the names to its low-level
 * parts at their next update (0x565200) and plays only what was copied; a
 * port may resolve a name at once. Every case updates the parts at least
 * once between a Set and what it observes, so both hold. "No link is 0" is
 * the v14 binding's (Parts_GetOnCursorShowLinkPartsNumber, 0x58e770); the
 * shared PE_Get/SetOnCursorShowLinkPartsNumber of the engines before v14
 * (GUIEngine, GoatGUIEngine, the older PartsEngine) keep their -1 and their
 * get-or-create (PS10).
 *
 *   PS1 the pactex keys through the production loader: slots 2 and 3 of the
 *       root, a child and a nested child, as the pactex has them byte for
 *       byte; empty values and missing keys are ""; slots 0 and 1 are "";
 *       the loaded children play their slot 2 when entered and their slot 3
 *       when clicked; renamed by the AIN they play the new names, and
 *       nothing once named what the archive does not have.
 *   PS2 Set and Get through the AIN declarations: the four slots apart, ""
 *       clears one, a number above 0 without a parts gets one, 0 and below
 *       do nothing, States -1 and 4 write nothing, a long name and double-
 *       byte names come back as they were.
 *   PS3 the hover and the click, frame by frame: slot 2 once at each enter
 *       (not while staying or moving; again after leaving; also with the
 *       left button held, pressed on it or elsewhere), slot 3 once at each
 *       release that clicks it (also after a drag out and back; none after a
 *       release outside, none for a press elsewhere); each sound comes with
 *       the parts' MouseEnter or MouseClick of the same update; two parts
 *       entered in one update play one each; names in slots 0 and 1 alone
 *       play nothing; renamed, the new names play, and nothing (not the old
 *       one) once named what the archive does not have.
 *   PS4 a disabled button plays nothing and still reads its names; enabled
 *       again it plays them.
 *   PS6 Parts_PlaySound: 2 and 3 play their slots, 0, 1, 4 and -1 nothing.
 *   PS7 the link: the pactex names resolved in their own activity, before
 *       and after the target, nested; the AIN Set and Get, a number without
 *       a parts kept and nothing made for it, 0 no link; the display's
 *       decision (parts_link_shown, which parts_render asks): hidden while
 *       the target is not entered, shown while it is, shown without a link
 *       and for a number without a parts.
 *   PS9 the save: the names and a link come back after Save and Load, and a
 *       loaded parts plays its sound.
 *   PS9M the save in the game's mode (layer-input's setup: the multi-
 *       controller mode; four layers, one inserted in the middle, so that
 *       their IDs are not the IDs of their positions), with a CG deform, a
 *       wheel flag and the names of parts of two layers: the optional
 *       sections follow the parts lists as PCDF, PWHE, PSND, each right
 *       where the one before it ends, and PCID is the last nr + 3 words (a
 *       reader from before PSND reads PCDF and PWHE where they were and the
 *       IDs from the end, then stops at the mark it does not know; WE13's
 *       rule for PWHE). After Load the layers, the active one, the wheel
 *       flag, the CG deform and the names come back, without a warning, and
 *       the active layer's parts plays its sounds. Without PSND (the save
 *       of a writer before it) the same loads, the names are "" and the
 *       parts plays the sound numbers of its record. A PSND whose count or
 *       whose first length does not fit is warned about and left; the rest
 *       loads as without it.
 *   PS12 SetClickMissSoundName / GetClickMissSoundName keep the name; a
 *       click where there is no parts plays nothing.
 *   PS14 the layers (G3b): with a box's layer active, a named button of the
 *       scene's layer under the cursor is neither entered nor heard; the
 *       box's button is; once the box's layer is erased, the scene's button
 *       is entered and heard.
 *   PS15 the engine's sounds and the script's channel IDs: the cursor and
 *       click sounds take none of the IDs the script prepares and plays
 *       (KiwiSoundEngine's IsPlay and IsExistID do not see them; the
 *       original's CSound plays through PlaySe 3, 0x5ba830 -> 0x4a3340, in
 *       the sound engine's SE table, whose IDs are apart from the general
 *       IDs, two tables built by 0x4a2f00); the script's Prepare of its
 *       music's second channel while they play cuts none of them, and its
 *       Play after a frame in which the cursor enters another button plays
 *       (the game's IDs: music 1 and 2, looped sounds 3..8, sounds 10 and
 *       11, the voice 12, system sounds 14..19, background sounds 20 and
 *       21 on, all fixed numbers; 0, 9 and 13 the script does not use);
 *       when they have ended they are collected and the script's channels
 *       play on; more enters than the engine has anonymous channels (64)
 *       are all heard; audio_reset closes them all. The sounds are set by
 *       number (PS13's API, which the names come to), heard through the
 *       engine's SDL callback (probe_audio_pull) on sound-groups' synthetic
 *       data (sg_make_data, removed at the end) in its group mode.
 * Guards, which pass before the change as well:
 *   PS5 no name, "", a name the archive does not have, a name cleared: the
 *       archive is asked for nothing.
 *   PS8 no link is 0: an unknown name, "", no key, a parts never linked.
 *   PS10 v13: the number API (SetPartsOnCursorSoundNumber and
 *       SetPartsClickSoundNumber) and the old input path play as before;
 *       the shared link functions keep -1 for no link and their
 *       get-or-create of the target; SetClickMissSoundNumber plays nothing.
 *   PS11 the string rules: PS1's activity, names and sounds along one path
 *       are the same under the GBK rule, the SJIS rule and the GB18030 flag
 *       alone.
 *   PS13 the observation: the port's own sound numbers of a v14 parts
 *       (SetPartsOnCursorSoundNumber, SetPartsClickSoundNumber) play at the
 *       v14 enter and release and are seen as one fetch each, so a case
 *       above that sees no fetch has nothing played.
 * The link's display itself (a parts shown while its target is entered,
 * faded over 100 ms, 0x535c90) is not checked, only the decision PS7 asks:
 * the windowless GL of the probe draws no parts (save_thumbnail_fixture.inc). The sounds are observed through the
 * sound archive of cg_assets_shadow.inc (probe_install_sound_assets): one
 * fetch of a sound's data is one attempt to play it; PS15 hears them in the
 * mixed output instead (a sound archive with data). Synthetic names and
 * rectangles, synthetic pactex fed to the production loader, the production
 * PE_UpdateInputState and the v14 message queue; the SDL dummy audio driver.
 * No GL, no game assets. Each case runs in its own fork.
 */

static int ps_failures;
#define PS_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ps_failures++; } } while (0)

extern void probe_install_sound_assets(const char *const *names, int n);
extern int probe_sound_fetch_count(void);
extern int probe_sound_fetch_at(int i);
extern void probe_sound_fetch_clear(void);

/* The CN executable's keys (GBK). */
#define PS_KEY_ON "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xd2\xf4\xd0\xa7"				/* オン指針音效 0x7db0b4 */
#define PS_KEY_CLICK "\xfc\x63\x93\xf4\xd2\xf4\xd0\xa7"					/* 點擊音效 0x7db0a4 */
#define PS_KEY_LINK "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xb1\xed\xca\xbe\xdf\x42\x84\xd3"	/* オン指針表示連動 0x7db054 */

/* The synthetic sound archive: number i has name ps_sounds[i] (0 has none). */
enum { PS_ON = 1, PS_DOWN, PS_ROOT_ON, PS_ALT, PS_ALT_DOWN, PS_DBYTE, PS_HIGH, PS_B_ON, PS_B_DOWN, PS_ONE, PS_ZERO, PS_MISS, PS_NR };
static const char *const ps_sounds[PS_NR] = {
	NULL, "psn-on", "psn-down", "psn-root-on", "psn-alt", "psn-alt-down",
	"psn\x81\x5c" "dbyte",		/* a double byte whose trail is '\' */
	"psn\xa3\xaf\xfe\x40" "hi",	/* bytes above 0x7f */
	"psn-b-on", "psn-b-down", "psn-one", "psn-zero", "psn-miss",
};

/* --- the fetches --- */

struct ps_fetch { int n; int id[16]; };

static struct ps_fetch ps_take(void)
{
	struct ps_fetch f = { probe_sound_fetch_count() };
	for (int i = 0; i < f.n && i < 16; i++)
		f.id[i] = probe_sound_fetch_at(i);
	probe_sound_fetch_clear();
	return f;
}

static void ps_print(const char *what, const struct ps_fetch *f)
{
	printf("    %s: %d fetch(es):", what, f->n);
	for (int i = 0; i < f->n && i < 16; i++)
		printf(" %d", f->id[i]);
	printf("\n");
}

/* The fetches are exactly want[0..n-1], in any order. */
static bool ps_fetched(const char *what, struct ps_fetch f, int n, const int *want)
{
	bool ok = f.n == n;
	bool used[16] = { 0 };
	for (int i = 0; ok && i < n; i++) {
		bool found = false;
		for (int j = 0; j < f.n && j < 16 && !found; j++) {
			if (!used[j] && f.id[j] == want[i])
				used[j] = found = true;
		}
		ok = found;
	}
	if (!ok) {
		printf("  FAIL: %s\n", what);
		ps_print("got", &f);
		printf("    want %d:", n);
		for (int i = 0; i < n; i++)
			printf(" %d", want[i]);
		printf("\n");
		ps_failures++;
	}
	return ok;
}
#define PS_FETCHED(what, f, ...) do { const int ps_w_[] = { __VA_ARGS__ }; \
	ps_fetched(what, f, sizeof(ps_w_) / sizeof(ps_w_[0]), ps_w_); } while (0)
#define PS_NONE(what, f) ps_fetched(what, f, 0, NULL)

/* --- the HLLs through their AIN declarations --- */

static void ps_set(int no, const char *name, int state)
{
	int s = sf_str(name, -1);
	BU_PE("Parts_SetSoundName", BU_I(no), BU_I(s), BU_I(state));
	heap_unref(s);
}

static void ps_set_bytes(int no, const char *name, int len, int state)
{
	int s = sf_str(name, len);
	BU_PE("Parts_SetSoundName", BU_I(no), BU_I(s), BU_I(state));
	heap_unref(s);
}

static const char *ps_hex(const char *p, int n)
{
	static char ring[4][520];
	static int k;
	char *o = ring[k++ & 3];
	int w = 0;
	for (int i = 0; i < n && w < 500; i++)
		w += snprintf(o + w, sizeof ring[0] - w, (unsigned char)p[i] < 0x20 || (unsigned char)p[i] > 0x7e ? "\\x%02x" : "%c",
			      (unsigned char)p[i]);
	o[w] = 0;
	return o;
}

/* Get(no, state) is want (want_len bytes; -1: strlen). */
static bool ps_get_is(const char *what, int no, int state, const char *want, int want_len)
{
	if (want_len < 0)
		want_len = strlen(want);
	int slot = BU_PE("Parts_GetSoundName", BU_I(no), BU_I(state)).i;
	bool valid = slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_STRING && heap[slot].s;
	const char *got = valid ? heap[slot].s->text : "";
	int got_len = valid ? heap[slot].s->size : -1;
	bool ok = valid && got_len == want_len && !memcmp(got, want, want_len);
	if (!ok) {
		printf("  FAIL: %s: Parts_GetSoundName(%d, %d) = %s\"%s\" (%d bytes), want \"%s\" (%d bytes)\n", what, no, state,
		       valid ? "" : "(no string) ", ps_hex(got, got_len < 0 ? 0 : got_len), got_len, ps_hex(want, want_len), want_len);
		ps_failures++;
	}
	if (valid)
		heap_unref(slot);
	return ok;
}
#define PS_GET(what, no, state, want) ps_get_is(what, no, state, want, -1)

/* The four slots of no are want[0..3] (NULL: ""). */
static void ps_slots(const char *what, int no, const char *w0, const char *w1, const char *w2, const char *w3)
{
	const char *w[4] = { w0, w1, w2, w3 };
	for (int s = 0; s < 4; s++) {
		char buf[160];
		snprintf(buf, sizeof buf, "%s, slot %d", what, s);
		PS_GET(buf, no, s, w[s] ? w[s] : "");
	}
}

static int ps_link_get(int no) { return BU_PE("Parts_GetOnCursorShowLinkPartsNumber", BU_I(no)).i; }

/* The display's decision of the link (parts_render asks it; the probe draws
 * nothing), taken with dlsym: before the change the decision was inside
 * parts_render, and the probe builds there as well. */
static bool (*ps_link_shown_fn(void))(struct parts *)
{
	return (bool (*)(struct parts *))dlsym(RTLD_DEFAULT, "parts_link_shown");
}
static bool ps_shown(const char *what, int no, bool want)
{
	bool (*shown)(struct parts *) = ps_link_shown_fn();
	if (!shown) {
		PS_CHECK(false, "%s: the port has no parts_link_shown (the decision is inside parts_render)", what);
		return false;
	}
	bool got = shown(parts_get(no));
	PS_CHECK(got == want, "%s: parts %d %s, want %s", what, no, got ? "shown" : "hidden", want ? "shown" : "hidden");
	return got == want;
}

/* --- setup --- */

static void ps_audio(void)
{
	if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) || SDL_setenv("SDL_VIDEODRIVER", "psn-probe-no-video", 1)) {
		printf("PARTS-SOUND-NAME cannot select the dummy audio driver\n");
		fflush(stdout);
		_exit(2);
	}
	config.mixer_nr_channels = 0;
	config.default_volume = 100;
	config.bgi_path = config.wai_path = NULL;
	audio_init();
}

/* The input of hover-events, the dummy audio, the synthetic sound archive. */
static void ps_setup(void)
{
	he_env();
	br_input_setup();
	ps_audio();
	probe_install_sound_assets(ps_sounds, PS_NR);
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
}

/* One update; the sounds it fetched, and its messages in *msgs. */
static struct ps_fetch ps_update(int x, int y, bool left, struct he_frame *msgs)
{
	probe_sound_fetch_clear();
	struct he_frame f = he_update(x, y, left, 16);
	if (msgs)
		*msgs = f;
	return ps_take();
}

/* The update's sounds of a parts follow its messages: its slot 2 with its
 * MouseEnter (type 0), its slot 3 with its MouseClick (type 4). */
static void ps_follow(const char *what, const struct ps_fetch *f, const struct he_frame *m, int no, int on, int down)
{
	int n_on = 0, n_down = 0;
	for (int i = 0; i < f->n && i < 16; i++) {
		n_on += f->id[i] == on;
		n_down += f->id[i] == down;
	}
	int enter = he_count(m, 0, no), click = he_count(m, 4, no);
	PS_CHECK(n_on == enter && n_down == click, "%s: parts %d sounds %d enter / %d click, messages %d MouseEnter / %d MouseClick",
		 what, no, n_on, n_down, enter, click);
}

/* --- synthetic pactex --- */

/* name { 種類別情報 { 部件タイプ = type }, leaves..., [子部件 { nkids }] } */
static struct ex_tree ps_comp(const char *name, const char *type, int nleaves, const struct ex_tree *leaves, unsigned nkids)
{
	struct ex_tree t = bu_branch(name, 1 + nleaves + (nkids ? 1 : 0));
	t.children[0] = bu_branch(BU_INFO, 1);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, type);
	for (int i = 0; i < nleaves; i++)
		t.children[1 + i] = leaves[i];
	if (nkids)
		t.children[1 + nleaves] = bu_branch(BU_KIDS, nkids);
	return t;
}
static struct ex_tree *ps_kids(struct ex_tree *t) { return &t->children[t->nr_children - 1]; }

static bool ps_load(const char *act, struct ex_tree root)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string(act);
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch(act, 1);
	top->children[0] = root;
	e->blocks[0].val.tree = top;
	struct string *name = cstr_to_string(act);
	bool ok = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	PS_CHECK(ok, "the synthetic activity %s did not load", act);
	return ok;
}

static int ps_number(const char *act, const char *name)
{
	int a = sf_str(act, -1);
	int n = bu_number(a, name);
	heap_unref(a);
	return n;
}

/* PS1's activity: a root and five children, one of them a group with a
 * child. The containers are low-level parts (a layout box would place its
 * children). */
static void ps_sound_activity(const char *act)
{
	struct ex_tree root = ps_comp("PSNRoot", BU_LOW, 2, (struct ex_tree[]){
		bu_sleaf(PS_KEY_ON, ps_sounds[PS_ROOT_ON]), bu_sleaf(PS_KEY_CLICK, "") }, 5);
	struct ex_tree *kids = ps_kids(&root);
	kids->children[0] = ps_comp("Btn", BU_BUTTON, 2, (struct ex_tree[]){
		bu_sleaf(PS_KEY_ON, ps_sounds[PS_ON]), bu_sleaf(PS_KEY_CLICK, ps_sounds[PS_DOWN]) }, 0);
	kids->children[1] = ps_comp("Low", BU_LOW, 2, (struct ex_tree[]){ bu_sleaf(PS_KEY_ON, ""), bu_sleaf(PS_KEY_CLICK, "") }, 0);
	kids->children[2] = ps_comp("Nokeys", BU_LOW, 0, NULL, 0);
	kids->children[3] = ps_comp("Group", BU_LOW, 0, NULL, 1);
	ps_kids(&kids->children[3])->children[0] = ps_comp("Inner", BU_LOW, 2, (struct ex_tree[]){
		bu_sleaf(PS_KEY_ON, ps_sounds[PS_ALT]), bu_sleaf(PS_KEY_CLICK, ps_sounds[PS_ALT_DOWN]) }, 0);
	kids->children[4] = ps_comp("Dbyte", BU_LOW, 2, (struct ex_tree[]){
		bu_sleaf(PS_KEY_ON, ps_sounds[PS_DBYTE]), bu_sleaf(PS_KEY_CLICK, ps_sounds[PS_HIGH]) }, 0);
	ps_load(act, root);
}

/* A loaded component made a clickable rectangle at (x, y). */
static void ps_rect(int no, int x, int y)
{
	PE_SetPos(no, x, y);
	for (int s = 1; s <= 3; s++)
		PE_SetPartsRectangleDetectionSize(no, 200, 80, s);
	PE_SetClickable(no, true);
}

/* Enter, stay and click the parts at (x, y): its sounds are on and down. */
static void ps_hover_click(const char *what, int no, int x, int y, int on, int down)
{
	char buf[160];
	struct he_frame m;
	struct ps_fetch f = ps_update(640, 600, false, NULL);
	snprintf(buf, sizeof buf, "%s: beside it", what); PS_NONE(buf, f);
	f = ps_update(x, y, false, &m);
	snprintf(buf, sizeof buf, "%s: entering", what); PS_FETCHED(buf, f, on); ps_follow(buf, &f, &m, no, on, down);
	f = ps_update(x, y, false, NULL);
	snprintf(buf, sizeof buf, "%s: staying", what); PS_NONE(buf, f);
	f = ps_update(x, y, true, NULL);
	snprintf(buf, sizeof buf, "%s: the press", what); PS_NONE(buf, f);
	f = ps_update(x, y, false, &m);
	snprintf(buf, sizeof buf, "%s: the release", what); PS_FETCHED(buf, f, down); ps_follow(buf, &f, &m, no, on, down);
	f = ps_update(640, 600, false, NULL);
	snprintf(buf, sizeof buf, "%s: leaving", what); PS_NONE(buf, f);
}

/* Enter and click the parts at (x, y), which is entered and clicked (its
 * messages) and heard nothing. */
static void ps_hover_click_none(const char *what, int no, int x, int y)
{
	char buf[160];
	struct he_frame m;
	struct ps_fetch f = ps_update(640, 600, false, NULL);
	snprintf(buf, sizeof buf, "%s: beside it", what); PS_NONE(buf, f);
	f = ps_update(x, y, false, &m);
	snprintf(buf, sizeof buf, "%s: entering", what); PS_NONE(buf, f);
	PS_CHECK(he_count(&m, 0, no) == 1, "%s: %d MouseEnter, want 1", buf, he_count(&m, 0, no));
	f = ps_update(x, y, true, NULL);
	snprintf(buf, sizeof buf, "%s: the press", what); PS_NONE(buf, f);
	f = ps_update(x, y, false, &m);
	snprintf(buf, sizeof buf, "%s: the release", what); PS_NONE(buf, f);
	PS_CHECK(he_count(&m, 4, no) == 1, "%s: %d MouseClick, want 1", buf, he_count(&m, 4, no));
	f = ps_update(640, 600, false, NULL);
	snprintf(buf, sizeof buf, "%s: leaving", what); PS_NONE(buf, f);
}

/* --- PS1 --- */
static void ps_pactex(void)
{
	ps_setup();
	ps_sound_activity("PSNSounds");
	const char *A = "PSNSounds";
	int root = ps_number(A, ""), btn = ps_number(A, "Btn"), low = ps_number(A, "Low"), nokeys = ps_number(A, "Nokeys");
	int group = ps_number(A, "Group"), inner = ps_number(A, "Inner"), dbyte = ps_number(A, "Dbyte");
	PS_CHECK(root > 0 && btn > 0 && low > 0 && nokeys > 0 && group > 0 && inner > 0 && dbyte > 0,
		 "PS1 numbers: root %d Btn %d Low %d Nokeys %d Group %d Inner %d Dbyte %d", root, btn, low, nokeys, group, inner, dbyte);
	ps_slots("PS1 the root (pactex_load)", root, NULL, NULL, ps_sounds[PS_ROOT_ON], NULL);
	ps_slots("PS1 a child (pactex_create_component)", btn, NULL, NULL, ps_sounds[PS_ON], ps_sounds[PS_DOWN]);
	ps_slots("PS1 empty values", low, NULL, NULL, NULL, NULL);
	ps_slots("PS1 no keys", nokeys, NULL, NULL, NULL, NULL);
	ps_slots("PS1 a group without keys", group, NULL, NULL, NULL, NULL);
	ps_slots("PS1 a nested child", inner, NULL, NULL, ps_sounds[PS_ALT], ps_sounds[PS_ALT_DOWN]);
	ps_slots("PS1 double-byte names", dbyte, NULL, NULL, ps_sounds[PS_DBYTE], ps_sounds[PS_HIGH]);
	/* what the loaded components play */
	ps_rect(btn, 100, 100);
	ps_rect(inner, 400, 100);
	PE_BeginInput();
	ps_update(640, 600, false, NULL);
	ps_update(640, 600, false, NULL);
	ps_hover_click("PS1 the loaded child", btn, 150, 140, PS_ON, PS_DOWN);
	ps_hover_click("PS1 the loaded nested child", inner, 450, 140, PS_ALT, PS_ALT_DOWN);
	/* a loaded parts renamed by the AIN (UserConfig::SetSound does this to
	 * the settings' buttons): the new names; then names the archive does
	 * not have: nothing, not the pactex's */
	ps_set(btn, ps_sounds[PS_B_ON], 2);
	ps_set(btn, ps_sounds[PS_B_DOWN], 3);
	ps_slots("PS1 the loaded child renamed", btn, NULL, NULL, ps_sounds[PS_B_ON], ps_sounds[PS_B_DOWN]);
	ps_hover_click("PS1 the loaded child renamed", btn, 150, 140, PS_B_ON, PS_B_DOWN);
	ps_set(btn, "psn-no-such-sound", 2);
	ps_set(btn, "psn-no-such-sound", 3);
	ps_hover_click_none("PS1 the loaded child renamed to no sound", btn, 150, 140);
	PE_EndInput();
	printf("  PS1 the pactex keys\n");
}

/* --- PS2 --- */
enum { PS_N = 19601, PS_M, PS_FRESH, PS_LONG };
static void ps_set_get(void)
{
	ps_setup();
	parts_get(PS_N);
	ps_slots("PS2 a new parts", PS_N, NULL, NULL, NULL, NULL);
	ps_set(PS_N, ps_sounds[PS_ON], 2);
	ps_slots("PS2 Set(2)", PS_N, NULL, NULL, ps_sounds[PS_ON], NULL);
	ps_set(PS_N, ps_sounds[PS_DOWN], 3);
	ps_slots("PS2 Set(3)", PS_N, NULL, NULL, ps_sounds[PS_ON], ps_sounds[PS_DOWN]);
	ps_set(PS_N, ps_sounds[PS_ONE], 1);
	ps_set(PS_N, ps_sounds[PS_ZERO], 0);
	ps_slots("PS2 Set(1), Set(0)", PS_N, ps_sounds[PS_ZERO], ps_sounds[PS_ONE], ps_sounds[PS_ON], ps_sounds[PS_DOWN]);
	ps_set(PS_N, ps_sounds[PS_B_ON], 2);
	ps_slots("PS2 Set(2) again", PS_N, ps_sounds[PS_ZERO], ps_sounds[PS_ONE], ps_sounds[PS_B_ON], ps_sounds[PS_DOWN]);
	/* "" clears a slot (StateMark@SetClickable does it) */
	ps_set(PS_N, "", 2);
	ps_slots("PS2 Set(\"\", 2)", PS_N, ps_sounds[PS_ZERO], ps_sounds[PS_ONE], NULL, ps_sounds[PS_DOWN]);
	/* outside 0..3: nothing written, "" read (port strategy) */
	ps_set(PS_N, ps_sounds[PS_MISS], 4);
	ps_set(PS_N, ps_sounds[PS_MISS], -1);
	ps_slots("PS2 Set(4), Set(-1)", PS_N, ps_sounds[PS_ZERO], ps_sounds[PS_ONE], NULL, ps_sounds[PS_DOWN]);
	PS_GET("PS2 Get(4)", PS_N, 4, "");
	PS_GET("PS2 Get(-1)", PS_N, -1, "");
	/* a number above 0 without a parts gets one (0x540250 creates it) */
	PS_CHECK(!parts_try_get(PS_FRESH), "PS2 parts %d exists before the Set", PS_FRESH);
	ps_set(PS_FRESH, ps_sounds[PS_ON], 2);
	PS_CHECK(parts_try_get(PS_FRESH) != NULL, "PS2 Set on a number without a parts: no parts %d", PS_FRESH);
	PS_GET("PS2 Set on a number without a parts", PS_FRESH, 2, ps_sounds[PS_ON]);
	/* 0 and below: nothing */
	ps_set(0, ps_sounds[PS_ON], 2);
	ps_set(-3, ps_sounds[PS_ON], 2);
	PS_CHECK(!parts_try_get(0) && !parts_try_get(-3), "PS2 Set on 0 or -3 made a parts");
	PS_GET("PS2 Get(0)", 0, 2, "");
	PS_CHECK(!parts_try_get(0), "PS2 Get on 0 made a parts");
	/* bytes as they were */
	parts_get(PS_M);
	ps_set_bytes(PS_M, ps_sounds[PS_DBYTE], -1, 2);
	ps_set_bytes(PS_M, ps_sounds[PS_HIGH], -1, 3);
	ps_slots("PS2 double-byte names", PS_M, NULL, NULL, ps_sounds[PS_DBYTE], ps_sounds[PS_HIGH]);
	char long_name[241];
	for (int i = 0; i < 240; i++)
		long_name[i] = 'a' + i % 26;
	long_name[240] = 0;
	parts_get(PS_LONG);
	ps_set(PS_LONG, long_name, 2);
	PS_GET("PS2 a name of 240 bytes", PS_LONG, 2, long_name);
	ps_set(PS_LONG, ps_sounds[PS_ON], 2);
	PS_GET("PS2 a short name after the long one", PS_LONG, 2, ps_sounds[PS_ON]);
	printf("  PS2 Set and Get\n");
}

/* --- PS3 --- */
enum { PS_A = 19611, PS_B, PS_C };
static void ps_box(int no, int x, int y)
{
	br_box(no, x, y, 200, 80, true);
}
static void ps_scope(void)
{
	PE_BeginInput();
	ps_update(640, 600, false, NULL);
	ps_update(640, 600, false, NULL);
}

static void ps_hover(void)
{
	ps_setup();
	ps_box(PS_A, 100, 100);
	cp_register_event(PS_A, 3, 30);
	ps_set(PS_A, ps_sounds[PS_ON], 2);
	ps_set(PS_A, ps_sounds[PS_DOWN], 3);
	ps_scope();
	struct he_frame m;
	struct ps_fetch f;
#define PS_STEP(what, x, y, left, ...) do { f = ps_update(x, y, left, &m); \
	ps_follow(what, &f, &m, PS_A, PS_ON, PS_DOWN); PS_FETCHED(what, f, __VA_ARGS__); } while (0)
#define PS_STEP_NONE(what, x, y, left) do { f = ps_update(x, y, left, &m); \
	ps_follow(what, &f, &m, PS_A, PS_ON, PS_DOWN); PS_NONE(what, f); } while (0)
	PS_STEP("PS3 entering", 150, 140, false, PS_ON);
	PS_STEP_NONE("PS3 staying", 150, 140, false);
	PS_STEP_NONE("PS3 staying (2)", 150, 140, false);
	PS_STEP_NONE("PS3 moving on it", 170, 150, false);
	PS_STEP_NONE("PS3 leaving", 640, 600, false);
	PS_STEP_NONE("PS3 beside it", 640, 600, false);
	PS_STEP("PS3 entering again", 160, 150, false, PS_ON);
	PS_STEP_NONE("PS3 the press", 160, 150, true);
	PS_STEP_NONE("PS3 held", 160, 150, true);
	PS_STEP("PS3 the release", 160, 150, false, PS_DOWN);
	/* pressed on it, dragged out and back: entered again with the button
	 * down on it (state 3), clicked at the release */
	PS_STEP_NONE("PS3 the second press", 160, 150, true);
	PS_STEP_NONE("PS3 dragged out", 640, 600, true);
	PS_STEP("PS3 dragged back", 160, 150, true, PS_ON);
	PS_CHECK(parts_get(PS_A)->state == PARTS_STATE_CLICKED, "PS3 dragged back: state %d, want the down state", parts_get(PS_A)->state);
	PS_STEP("PS3 the release after dragging back", 160, 150, false, PS_DOWN);
	/* pressed on it, released outside: no click */
	PS_STEP_NONE("PS3 the third press", 160, 150, true);
	PS_STEP_NONE("PS3 dragged out again", 640, 600, true);
	PS_STEP_NONE("PS3 released outside", 640, 600, false);
	/* pressed elsewhere, entered with the button held: the sound, not the
	 * down state; released on it: no click */
	PS_STEP_NONE("PS3 a press elsewhere", 640, 600, true);
	PS_STEP("PS3 entered with the button held", 160, 150, true, PS_ON);
	PS_CHECK(parts_get(PS_A)->state == PARTS_STATE_HOVERED, "PS3 entered with the button held from elsewhere: state %d", parts_get(PS_A)->state);
	PS_STEP_NONE("PS3 released on it after a press elsewhere", 160, 150, false);
	PS_STEP_NONE("PS3 leaving at last", 640, 600, false);
	/* renamed: the new names play (0x5b7f20 replaces a name that differs) */
	ps_set(PS_A, ps_sounds[PS_B_ON], 2);
	ps_set(PS_A, ps_sounds[PS_B_DOWN], 3);
	f = ps_update(160, 150, false, &m);
	ps_follow("PS3 renamed: entering", &f, &m, PS_A, PS_B_ON, PS_B_DOWN);
	PS_FETCHED("PS3 renamed: entering", f, PS_B_ON);
	f = ps_update(160, 150, true, NULL);
	PS_NONE("PS3 renamed: the press", f);
	f = ps_update(160, 150, false, &m);
	ps_follow("PS3 renamed: the release", &f, &m, PS_A, PS_B_ON, PS_B_DOWN);
	PS_FETCHED("PS3 renamed: the release", f, PS_B_DOWN);
	f = ps_update(640, 600, false, NULL);
	PS_NONE("PS3 renamed: leaving", f);
	/* renamed to names the archive does not have: nothing, not the old
	 * names (the original's PlaySe of such a name fails silently) */
	ps_set(PS_A, "psn-no-such-sound", 2);
	ps_set(PS_A, "psn-no-such-sound", 3);
	f = ps_update(160, 150, false, &m);
	PS_NONE("PS3 renamed to no sound: entering", f);
	PS_CHECK(he_count(&m, 0, PS_A) == 1, "PS3 renamed to no sound: %d MouseEnter, want 1", he_count(&m, 0, PS_A));
	f = ps_update(160, 150, true, NULL);
	PS_NONE("PS3 renamed to no sound: the press", f);
	f = ps_update(160, 150, false, &m);
	PS_NONE("PS3 renamed to no sound: the release", f);
	PS_CHECK(he_count(&m, 4, PS_A) == 1, "PS3 renamed to no sound: %d MouseClick, want 1", he_count(&m, 4, PS_A));
	f = ps_update(640, 600, false, NULL);
	PS_NONE("PS3 renamed to no sound: leaving", f);
	/* and back to the first names for the cases below */
	ps_set(PS_A, ps_sounds[PS_ON], 2);
	ps_set(PS_A, ps_sounds[PS_DOWN], 3);
	PE_EndInput();
	/* two parts entered in one update: B passes the cursor in front of A */
	ps_box(PS_B, 120, 110);
	PE_SetZ(PS_B, 10);
	PE_SetClickable(PS_B, false);
	PE_SetPassCursor(PS_B, true);
	ps_set(PS_B, ps_sounds[PS_B_ON], 2);
	ps_set(PS_B, ps_sounds[PS_B_DOWN], 3);
	/* C: names in the slots 0 and 1 alone */
	ps_box(PS_C, 700, 100);
	ps_set(PS_C, ps_sounds[PS_ZERO], 0);
	ps_set(PS_C, ps_sounds[PS_ONE], 1);
	ps_scope();
	f = ps_update(160, 150, false, &m);
	PS_FETCHED("PS3 two parts entered in one update", f, PS_B_ON, PS_ON);
	ps_follow("PS3 two parts (A)", &f, &m, PS_A, PS_ON, PS_DOWN);
	ps_follow("PS3 two parts (B)", &f, &m, PS_B, PS_B_ON, PS_B_DOWN);
	f = ps_update(160, 150, true, NULL);
	PS_NONE("PS3 two parts: the press", f);
	f = ps_update(160, 150, false, &m);
	PS_FETCHED("PS3 two parts: the release clicks A alone", f, PS_DOWN);
	ps_follow("PS3 two parts: the release (B)", &f, &m, PS_B, PS_B_ON, PS_B_DOWN);
	f = ps_update(640, 600, false, NULL);
	PS_NONE("PS3 two parts left", f);
	f = ps_update(750, 140, false, &m);
	PS_NONE("PS3 slots 0 and 1 alone: entering", f);
	PS_CHECK(he_count(&m, 0, PS_C) == 1, "PS3 slots 0 and 1 alone: %d MouseEnter, want 1", he_count(&m, 0, PS_C));
	f = ps_update(750, 140, true, NULL);
	f = ps_update(750, 140, false, &m);
	PS_NONE("PS3 slots 0 and 1 alone: the click", f);
	PS_CHECK(he_count(&m, 4, PS_C) == 1, "PS3 slots 0 and 1 alone: %d MouseClick, want 1", he_count(&m, 4, PS_C));
	PE_EndInput();
#undef PS_STEP
#undef PS_STEP_NONE
	printf("  PS3 the hover and the click\n");
}

/* --- PS4 --- */
static void ps_disabled(void)
{
	ps_setup();
	ps_box(PS_A, 100, 100);
	ps_set(PS_A, ps_sounds[PS_ON], 2);
	ps_set(PS_A, ps_sounds[PS_DOWN], 3);
	BU_PE("SetButtonEnable", BU_I(PS_A), BU_I(0));
	PS_CHECK(!BU_PE("IsButtonEnable", BU_I(PS_A)).i, "PS4 IsButtonEnable after SetButtonEnable(false)");
	ps_scope();
	struct ps_fetch f = ps_update(150, 140, false, NULL);
	PS_NONE("PS4 disabled: entering", f);
	f = ps_update(150, 140, true, NULL);
	PS_NONE("PS4 disabled: the press", f);
	f = ps_update(150, 140, false, NULL);
	PS_NONE("PS4 disabled: the release", f);
	/* the button's own names stay (0x5285b0 empties the inner parts' copies) */
	ps_slots("PS4 disabled: its names", PS_A, NULL, NULL, ps_sounds[PS_ON], ps_sounds[PS_DOWN]);
	f = ps_update(640, 600, false, NULL);
	PS_NONE("PS4 disabled: leaving", f);
	BU_PE("SetButtonEnable", BU_I(PS_A), BU_I(1));
	PS_CHECK(BU_PE("IsButtonEnable", BU_I(PS_A)).i, "PS4 IsButtonEnable after SetButtonEnable(true)");
	ps_hover_click("PS4 enabled again", PS_A, 150, 140, PS_ON, PS_DOWN);
	PE_EndInput();
	printf("  PS4 a disabled button\n");
}

/* --- PS5 (guard) --- */
static void ps_nothing(void)
{
	ps_setup();
	ps_box(PS_A, 100, 100);		/* no names */
	ps_box(PS_B, 400, 100);
	ps_set(PS_B, "", 2);
	ps_set(PS_B, "", 3);
	ps_box(PS_C, 700, 100);
	ps_set(PS_C, "psn-no-such-sound", 2);
	ps_set(PS_C, "psn-no-such-sound", 3);
	ps_box(PS_N, 100, 300);
	ps_set(PS_N, ps_sounds[PS_ON], 2);
	ps_set(PS_N, ps_sounds[PS_DOWN], 3);
	ps_set(PS_N, "", 2);
	ps_set(PS_N, "", 3);
	ps_scope();
	struct { const char *what; int x, y; } k[] = {
		{ "PS5 no name", 150, 140 }, { "PS5 \"\"", 450, 140 }, { "PS5 a name the archive does not have", 750, 140 },
		{ "PS5 names cleared with \"\"", 150, 340 } };
	for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
		char buf[160];
		struct ps_fetch f = ps_update(k[i].x, k[i].y, false, NULL);
		snprintf(buf, sizeof buf, "%s: entering", k[i].what); PS_NONE(buf, f);
		f = ps_update(k[i].x, k[i].y, true, NULL);
		f = ps_update(k[i].x, k[i].y, false, NULL);
		snprintf(buf, sizeof buf, "%s: the click", k[i].what); PS_NONE(buf, f);
		f = ps_update(640, 600, false, NULL);
		snprintf(buf, sizeof buf, "%s: leaving", k[i].what); PS_NONE(buf, f);
	}
	PE_EndInput();
	printf("  PS5 nothing to play\n");
}

/* --- PS6 --- */
static void ps_play(void)
{
	ps_setup();
	ps_box(PS_A, 100, 100);
	ps_set(PS_A, ps_sounds[PS_ZERO], 0);
	ps_set(PS_A, ps_sounds[PS_ONE], 1);
	ps_set(PS_A, ps_sounds[PS_ON], 2);
	ps_set(PS_A, ps_sounds[PS_DOWN], 3);
	ps_box(PS_B, 400, 100);
	/* one update after the Sets: the original copies the names to the
	 * low-level parts there (0x565200), before which it plays nothing */
	PE_UpdateComponent(0);
	struct { int no, state, want; } k[] = {
		{ PS_A, 2, PS_ON }, { PS_A, 3, PS_DOWN }, { PS_A, 1, 0 }, { PS_A, 0, 0 }, { PS_A, 4, 0 }, { PS_A, -1, 0 },
		{ PS_B, 2, 0 }, { PS_B, 3, 0 }, { PS_A, 2, PS_ON } };
	for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
		char buf[160];
		snprintf(buf, sizeof buf, "PS6 call %u: Parts_PlaySound(%s, %d)", i + 1, k[i].no == PS_A ? "A" : "B (no names)", k[i].state);
		probe_sound_fetch_clear();
		BU_PE("Parts_PlaySound", BU_I(k[i].no), BU_I(k[i].state));
		struct ps_fetch f = ps_take();
		if (k[i].want)
			PS_FETCHED(buf, f, k[i].want);
		else
			PS_NONE(buf, f);
	}
	printf("  PS6 Parts_PlaySound\n");
}

/* --- PS7, PS8 --- */
static void ps_link_activity(const char *act)
{
	struct ex_tree root = ps_comp("LRoot", BU_LOW, 0, NULL, 7);
	struct ex_tree *kids = ps_kids(&root);
	kids->children[0] = ps_comp("Desc1", BU_LOW, 1, (struct ex_tree[]){ bu_sleaf(PS_KEY_LINK, "Target1") }, 0);
	kids->children[1] = ps_comp("Target1", BU_LOW, 0, NULL, 0);
	/* the target comes after it, in a group */
	kids->children[2] = ps_comp("Desc2", BU_LOW, 1, (struct ex_tree[]){ bu_sleaf(PS_KEY_LINK, "Target2") }, 0);
	kids->children[3] = ps_comp("Desc3", BU_LOW, 1, (struct ex_tree[]){ bu_sleaf(PS_KEY_LINK, "NoSuchTarget") }, 0);
	kids->children[4] = ps_comp("Desc4", BU_LOW, 1, (struct ex_tree[]){ bu_sleaf(PS_KEY_LINK, "") }, 0);
	kids->children[5] = ps_comp("Desc5", BU_LOW, 0, NULL, 0);
	kids->children[6] = ps_comp("Grp", BU_LOW, 0, NULL, 1);
	ps_kids(&kids->children[6])->children[0] = ps_comp("Target2", BU_LOW, 0, NULL, 0);
	ps_load(act, root);
}

static void ps_link(void)
{
	ps_setup();
	ps_link_activity("PSNLinkA");
	ps_link_activity("PSNLinkB");
	const char *acts[] = { "PSNLinkA", "PSNLinkB" };
	for (int a = 0; a < 2; a++) {
		int d1 = ps_number(acts[a], "Desc1"), t1 = ps_number(acts[a], "Target1");
		int d2 = ps_number(acts[a], "Desc2"), t2 = ps_number(acts[a], "Target2");
		PS_CHECK(d1 > 0 && t1 > 0 && d2 > 0 && t2 > 0, "PS7 %s numbers %d %d %d %d", acts[a], d1, t1, d2, t2);
		int g1 = ps_link_get(d1), g2 = ps_link_get(d2);
		PS_CHECK(g1 == t1, "PS7 %s: Desc1 links to %d, want Target1 = %d", acts[a], g1, t1);
		PS_CHECK(g2 == t2, "PS7 %s: Desc2 (the target after it, nested) links to %d, want Target2 = %d", acts[a], g2, t2);
	}
	PS_CHECK(ps_number(acts[0], "Target1") != ps_number(acts[1], "Target1"), "PS7 the two activities share a number");
	/* the display (0x535c90): a linked parts is shown while its target is
	 * entered; a parts without a link is shown */
	int d1 = ps_number(acts[0], "Desc1"), t1 = ps_number(acts[0], "Target1"), d3 = ps_number(acts[0], "Desc3");
	ps_shown("PS7 Desc1 while Target1 is not entered", d1, false);
	ps_shown("PS7 Desc3 (its link not found)", d3, true);
	ps_rect(t1, 100, 300);
	PE_BeginInput();
	ps_update(640, 600, false, NULL);
	ps_update(640, 600, false, NULL);
	ps_shown("PS7 Desc1 beside Target1", d1, false);
	ps_update(150, 340, false, NULL);
	PS_CHECK(parts_get(t1)->is_hovered, "PS7 Target1 is not entered under the cursor");
	ps_shown("PS7 Desc1 while Target1 is entered", d1, true);
	ps_update(640, 600, false, NULL);
	ps_shown("PS7 Desc1 after the cursor left Target1", d1, false);
	PE_EndInput();
	/* the AIN's Set and Get (0x58e6d0, 0x58e770) */
	ps_box(PS_A, 100, 100);
	ps_box(PS_B, 400, 100);
	BU_PE("Parts_SetOnCursorShowLinkPartsNumber", BU_I(PS_A), BU_I(PS_B));
	PS_CHECK(ps_link_get(PS_A) == PS_B, "PS7 Set(A, B) then Get(A) = %d, want %d", ps_link_get(PS_A), PS_B);
	ps_shown("PS7 A linked to B, B not entered", PS_A, false);
	/* a number without a parts: kept, nothing made, shown as if unlinked
	 * (0x58e6d0 writes the number; 0x539db0's range check) */
	PS_CHECK(!parts_try_get(PS_C), "PS7 parts %d exists before the link", PS_C);
	BU_PE("Parts_SetOnCursorShowLinkPartsNumber", BU_I(PS_A), BU_I(PS_C));
	PS_CHECK(ps_link_get(PS_A) == PS_C, "PS7 Set(A, a number without a parts) then Get(A) = %d, want %d", ps_link_get(PS_A), PS_C);
	ps_shown("PS7 A linked to a number without a parts", PS_A, true);
	PS_CHECK(!parts_try_get(PS_C), "PS7 the link (or its display) made parts %d", PS_C);
	/* 0 is no link */
	BU_PE("Parts_SetOnCursorShowLinkPartsNumber", BU_I(PS_A), BU_I(0));
	PS_CHECK(ps_link_get(PS_A) == 0 && !parts_try_get(0), "PS7 Set(A, 0) then Get(A) = %d (parts 0 %s), want 0", ps_link_get(PS_A),
		 parts_try_get(0) ? "made" : "not made");
	ps_shown("PS7 A after Set(A, 0)", PS_A, true);
	printf("  PS7 the link\n");
}

static void ps_no_link(void)
{
	ps_setup();
	ps_link_activity("PSNLinkA");
	const char *names[] = { "Desc3", "Desc4", "Desc5", "Target1", "LRoot" };
	const char *what[] = { "an unknown name", "\"\"", "no key", "a target", "the root" };
	for (int i = 0; i < 5; i++) {
		int no = ps_number("PSNLinkA", names[i]);
		PS_CHECK(no > 0 && ps_link_get(no) == 0, "PS8 %s (%s, %d): Get = %d, want 0", what[i], names[i], no, ps_link_get(no));
	}
	ps_box(PS_A, 100, 100);
	PS_CHECK(ps_link_get(PS_A) == 0, "PS8 a parts never linked: Get = %d, want 0", ps_link_get(PS_A));
	printf("  PS8 no link is 0\n");
}

/* --- PS9 --- */
static void ps_save(void)
{
	ps_setup();
	ps_sound_activity("PSNSounds");
	int btn = ps_number("PSNSounds", "Btn"), dbyte = ps_number("PSNSounds", "Dbyte");
	ps_rect(btn, 100, 100);
	ps_box(PS_A, 400, 100);
	ps_set(PS_A, ps_sounds[PS_ZERO], 0);
	ps_set(PS_A, ps_sounds[PS_ONE], 1);
	ps_set(PS_A, ps_sounds[PS_B_ON], 2);
	ps_set(PS_A, ps_sounds[PS_B_DOWN], 3);
	ps_box(PS_B, 700, 100);
	BU_PE("Parts_SetOnCursorShowLinkPartsNumber", BU_I(PS_B), BU_I(PS_A));
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	PS_CHECK(PE_Save(&saved) && saved, "PS9 Save failed");
	if (!saved)
		return;
	PS_CHECK(PE_Load(&saved), "PS9 Load failed");
	ps_slots("PS9 the loaded child after Load", btn, NULL, NULL, ps_sounds[PS_ON], ps_sounds[PS_DOWN]);
	ps_slots("PS9 double-byte names after Load", dbyte, NULL, NULL, ps_sounds[PS_DBYTE], ps_sounds[PS_HIGH]);
	ps_slots("PS9 the four slots after Load", PS_A, ps_sounds[PS_ZERO], ps_sounds[PS_ONE], ps_sounds[PS_B_ON], ps_sounds[PS_B_DOWN]);
	PS_CHECK(ps_link_get(PS_B) == PS_A, "PS9 the link after Load: Get = %d, want %d", ps_link_get(PS_B), PS_A);
	ps_scope();
	ps_hover_click("PS9 the loaded child plays", btn, 150, 140, PS_ON, PS_DOWN);
	ps_hover_click("PS9 a parts set by the script plays", PS_A, 450, 140, PS_B_ON, PS_B_DOWN);
	PE_EndInput();
	ca_free_buffer(saved);
	printf("  PS9 the save\n");
}

/* --- PS9M --- */
/* The save's optional sections (src/parts/save.c) and its controller IDs. */
#define PS_SAVE_DEFORMS 0x46444350	/* "PCDF" */
#define PS_SAVE_WHEEL 0x45485750	/* "PWHE" */
#define PS_SAVE_NAMES 0x444e5350	/* "PSND" */
#define PS_SAVE_IDS 0x44494350		/* "PCID" */
enum { PS_SAVE_LOW = 19641, PS_SAVE_BOX, PS_SAVE_CG };
static int ps_sl_a, ps_sl_b, ps_sl_c;	/* the layers [scene, a, c, b], active c */

static int ps_mark_at(struct page *p, int mark)
{
	for (int i = 0; p && i < p->nr_vars; i++)
		if (p->values[i].i == mark)
			return i;
	return -1;
}

/* The words of the section at pos, its mark included, read by its own
 * format; -1 when it does not read as one. nos[] gets the parts numbers of a
 * PWHE or a PSND (up to 8, *nr_nos of them). */
static int ps_section_words(struct page *p, int pos, int *nos, int *nr_nos)
{
	int n = p ? p->nr_vars : 0, got = 0;
	if (pos < 0 || pos + 1 >= n)
		return -1;
	int mark = p->values[pos].i, count = p->values[pos + 1].i, k = pos + 2;
	if (count < 0 || count > n)
		return -1;
	switch (mark) {
	case PS_SAVE_DEFORMS:
		k += count * (1 + PARTS_NR_STATES);
		break;
	case PS_SAVE_WHEEL:
		for (int i = 0; i < count && k < n; i++, k++)
			if (nos && got < 8)
				nos[got++] = p->values[k].i;
		k = pos + 2 + count;
		break;
	case PS_SAVE_NAMES:
		/* each parts: its number, then for each of the four names its
		 * length and its bytes, one a word */
		for (int i = 0; i < count; i++) {
			if (k >= n)
				return -1;
			if (nos && got < 8)
				nos[got++] = p->values[k].i;
			k++;
			for (int s = 0; s < 4; s++) {
				if (k >= n)
					return -1;
				int len = p->values[k++].i;
				if (len < 0 || len > n - k)
					return -1;
				k += len;
			}
		}
		break;
	case PS_SAVE_IDS:
		k += count + 1;	/* the IDs, then the active one */
		break;
	default:
		return -1;
	}
	if (nr_nos)
		*nr_nos = got;
	return k <= n ? k - pos : -1;
}

/* Takes the words [pos, pos + words) out of the page. */
static void ps_cut(struct page *p, int pos, int words)
{
	if (pos < 0 || words <= 0 || pos + words > p->nr_vars)
		return;
	memmove(p->values + pos, p->values + pos + words, (p->nr_vars - pos - words) * sizeof(*p->values));
	p->nr_vars -= words;
}

/* PE_Load with stderr captured (layer_root_fixture.inc). */
static struct page **ps_load_page;
static bool ps_load_ok;
static void ps_load_fn(void *arg)
{
	(void)arg;
	ps_load_ok = PE_Load(ps_load_page);
}

/* What a Load of *p gives back: the layers and the active one, the parts on
 * their layers, the wheel flag, the CG deform, the names (all "" when names
 * is false), and the active layer's parts plays its sounds (from its names,
 * or from the numbers of its record). warning: what stderr has to say, or
 * NULL for neither of the two warnings. */
static void ps_save_loaded(const char *what, struct page **p, const char *warning, bool names)
{
	char err[4096], buf[160];
	ps_load_page = p;
	ps_load_ok = false;
	lr_capture_stderr(ps_load_fn, NULL, err, sizeof err);
	PS_CHECK(ps_load_ok, "%s: Load failed", what);
	if (warning)
		PS_CHECK(strstr(err, warning), "%s: no \"%s\" warning; stderr: %.300s", what, warning, err);
	else
		PS_CHECK(!strstr(err, "invalid sound name section") && !strstr(err, "without controller IDs"),
			 "%s: warned: %.300s", what, err);
	const int want[4] = { li_scene, ps_sl_a, ps_sl_c, ps_sl_b };
	int len = ci_len(), got[4] = { 0 };
	bool layers = len == 4;
	for (int i = 0; i < 4 && i < len; i++) {
		got[i] = ci_id_at(i);
		layers &= got[i] == want[i];
	}
	PS_CHECK(layers && ci_active() == ps_sl_c, "%s: %d layers [%d %d %d %d], active %d; want [%d %d %d %d], active %d", what,
		 len, got[0], got[1], got[2], got[3], ci_active(), want[0], want[1], want[2], want[3], ps_sl_c);
	struct parts *box = parts_try_get(PS_SAVE_BOX), *low = parts_try_get(PS_SAVE_LOW), *cg = parts_try_get(PS_SAVE_CG);
	PS_CHECK(box && box->controller_no == ps_sl_c && low && low->controller_no == li_scene,
		 "%s: the box's parts on layer %d, the scene's on %d; want %d and %d", what, box ? box->controller_no : -1,
		 low ? low->controller_no : -1, ps_sl_c, li_scene);
	PS_CHECK(box && box->wheelable && low && !low->wheelable, "%s: the wheel flags: the box's %d, the scene's %d; want 1, 0",
		 what, box ? box->wheelable : -1, low ? low->wheelable : -1);
	PS_CHECK(cg && cg->cg_deform[0] == 1 && cg->cg_deform[1] == 0, "%s: the CG deform: %d %d, want 1 0", what,
		 cg ? cg->cg_deform[0] : -1, cg ? cg->cg_deform[1] : -1);
	snprintf(buf, sizeof buf, "%s: the box's names", what);
	if (names)
		ps_slots(buf, PS_SAVE_BOX, ps_sounds[PS_ZERO], ps_sounds[PS_ONE], ps_sounds[PS_B_ON], ps_sounds[PS_B_DOWN]);
	else
		ps_slots(buf, PS_SAVE_BOX, NULL, NULL, NULL, NULL);
	snprintf(buf, sizeof buf, "%s: the scene's names", what);
	ps_slots(buf, PS_SAVE_LOW, NULL, NULL, names ? ps_sounds[PS_ON] : NULL, names ? ps_sounds[PS_DOWN] : NULL);
	snprintf(buf, sizeof buf, "%s: the active layer's parts plays", what);
	ps_scope();
	ps_hover_click(buf, PS_SAVE_BOX, 450, 140, PS_B_ON, PS_B_DOWN);
	PE_EndInput();
}

static void ps_save_layers(void)
{
	/* layer-input's setup: the multi-controller mode, the scene's layer */
	li_setup();
	ps_audio();
	probe_install_sound_assets(ps_sounds, PS_NR);
	PS_CHECK(parts_multi_controller, "PS9M the setup is not the multi-controller mode");
	li_button(PS_SAVE_LOW, 100, 100, 200, 80, 3);
	ps_set(PS_SAVE_LOW, ps_sounds[PS_ON], 2);
	ps_set(PS_SAVE_LOW, ps_sounds[PS_DOWN], 3);
	/* [scene, a, c, b]: c inserted above a, so the layer at position 2 is
	 * not the one of position 2's ID (controller-id CI4) */
	ps_sl_a = ci_add(-1);
	ps_sl_b = ci_add(-1);
	ps_sl_c = ci_add(ps_sl_a);
	PS_CHECK(ci_len() == 4 && ci_id_at(2) == ps_sl_c && ci_active() == ps_sl_c && ps_sl_c != CI_ID(2) && ps_sl_b != CI_ID(3),
		 "PS9M the layers: %d, [2] %d, active %d (a %d b %d c %d, positions' IDs from %d)", ci_len(), ci_id_at(2),
		 ci_active(), ps_sl_a, ps_sl_b, ps_sl_c, CI_ID(0));
	li_button(PS_SAVE_BOX, 400, 100, 200, 80, 6);
	ps_set(PS_SAVE_BOX, ps_sounds[PS_ZERO], 0);
	ps_set(PS_SAVE_BOX, ps_sounds[PS_ONE], 1);
	ps_set(PS_SAVE_BOX, ps_sounds[PS_B_ON], 2);
	ps_set(PS_SAVE_BOX, ps_sounds[PS_B_DOWN], 3);
	we_set(PS_SAVE_BOX, true);
	/* empty CG states: a CG deform without assets (WE13) */
	struct string *empty = cstr_to_string("");
	PE_SetPartsCG(PS_SAVE_CG, empty, 1, 1);
	PE_SetPartsCG(PS_SAVE_CG, empty, 0, 2);
	free_string(empty);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	PS_CHECK(PE_Save(&saved) && saved, "PS9M Save failed");
	if (!saved)
		return;

	/* the layout */
	int nos_w[8], nos_s[8], nr_w = 0, nr_s = 0, nr = ctrl_stack.nr_controllers, words = saved->nr_vars;
	int d = ps_mark_at(saved, PS_SAVE_DEFORMS), w = ps_mark_at(saved, PS_SAVE_WHEEL);
	int s = ps_mark_at(saved, PS_SAVE_NAMES), c = ps_mark_at(saved, PS_SAVE_IDS);
	int nd = ps_section_words(saved, d, NULL, NULL), nw = ps_section_words(saved, w, nos_w, &nr_w);
	int ns = ps_section_words(saved, s, nos_s, &nr_s), nc = ps_section_words(saved, c, NULL, NULL);
	printf("    PS9M %d words, %d layers: PCDF at %d (%d words), PWHE at %d (%d), PSND at %d (%d), PCID at %d (%d)\n",
	       words, nr, d, nd, w, nw, s, ns, c, nc);
	PS_CHECK(nr == 4 && d > 0 && nd > 0 && w == d + nd && nw > 0 && s == w + nw && ns > 0 && c == s + ns && nc == nr + 3
		 && c + nc == words,
		 "PS9M the sections are not PCDF, PWHE, PSND, PCID one right after the other with PCID the last %d words: "
		 "PCDF %d+%d, PWHE %d+%d, PSND %d+%d, PCID %d+%d of %d words", nr + 3, d, nd, w, nw, s, ns, c, nc, words);
	PS_CHECK(nr_w == 1 && nos_w[0] == PS_SAVE_BOX, "PS9M PWHE has %d parts (first %d), want the box's %d alone", nr_w,
		 nr_w ? nos_w[0] : 0, PS_SAVE_BOX);
	PS_CHECK(nr_s == 2 && ((nos_s[0] == PS_SAVE_LOW && nos_s[1] == PS_SAVE_BOX) || (nos_s[0] == PS_SAVE_BOX && nos_s[1] == PS_SAVE_LOW)),
		 "PS9M PSND has %d parts (%d %d), want the scene's %d and the box's %d", nr_s, nr_s > 0 ? nos_s[0] : 0,
		 nr_s > 1 ? nos_s[1] : 0, PS_SAVE_LOW, PS_SAVE_BOX);
	if (c >= 0 && nc == 7 && c + nc <= words)
		PS_CHECK(saved->values[c + 2].i == li_scene && saved->values[c + 3].i == ps_sl_a && saved->values[c + 4].i == ps_sl_c
			 && saved->values[c + 5].i == ps_sl_b && saved->values[c + 6].i == ps_sl_c,
			 "PS9M PCID [%d %d %d %d] active %d", saved->values[c + 2].i, saved->values[c + 3].i,
			 saved->values[c + 4].i, saved->values[c + 5].i, saved->values[c + 6].i);

	/* something else in between: the layers and their parts erased */
	ci_erase(ps_sl_c);
	ci_erase(ps_sl_b);
	ci_erase(ps_sl_a);
	PS_CHECK(ci_len() == 1 && !parts_try_get(PS_SAVE_BOX), "PS9M EraseLayer: %d layers, the box's parts %s", ci_len(),
		 parts_try_get(PS_SAVE_BOX) ? "still there" : "gone");
	ps_save_loaded("PS9M Load", &saved, NULL, true);

	/* the save of a writer before PSND: the same without the section */
	struct page *old = copy_page(saved);
	if (s >= 0 && ns > 0)
		ps_cut(old, s, ns);
	PS_CHECK(ps_mark_at(old, PS_SAVE_NAMES) < 0, "PS9M the copy without PSND still has it");
	ps_save_loaded("PS9M Load without PSND", &old, NULL, false);

	/* a PSND whose count does not fit, one whose first length does not */
	struct page *bad_count = copy_page(saved), *bad_len = copy_page(saved);
	if (s >= 0 && ns > 0) {
		bad_count->values[s + 1].i = words;
		bad_len->values[s + 3].i = words;
	}
	ps_save_loaded("PS9M Load with a PSND count that does not fit", &bad_count, "invalid sound name section", false);
	ps_save_loaded("PS9M Load with a PSND length that does not fit", &bad_len, "invalid sound name section", false);
	ca_free_buffer(bad_len);
	ca_free_buffer(bad_count);
	ca_free_buffer(old);
	ca_free_buffer(saved);
	printf("  PS9M the save in the game's mode\n");
}

/* --- PS10 (guard) --- */
static void ps_v13(void)
{
	ps_setup();
	ps_box(PS_A, 100, 100);
	PS_CHECK(PE_SetPartsOnCursorSoundNumber(PS_A, PS_ON) && PE_SetPartsClickSoundNumber(PS_A, PS_DOWN),
		 "PS10 the number API refused a sound of the archive");
	int version = ain->version;
	ain->version = 13;
	extern bool parts_multi_controller;
	bool multi = parts_multi_controller;
	for (int k = 0; k < 2; k++) {
		parts_multi_controller = k;
		char buf[160];
		PE_BeginInput();
		PE_ReleaseMessage();
		struct ps_fetch f = ps_update(640, 600, false, NULL);
		f = ps_update(150, 140, false, NULL);
		snprintf(buf, sizeof buf, "PS10 v13 (multi controller %d): entering", k); PS_FETCHED(buf, f, PS_ON);
		f = ps_update(150, 140, false, NULL);
		snprintf(buf, sizeof buf, "PS10 v13 (multi controller %d): staying", k); PS_NONE(buf, f);
		f = ps_update(150, 140, true, NULL);
		f = ps_update(150, 140, false, NULL);
		snprintf(buf, sizeof buf, "PS10 v13 (multi controller %d): the click", k); PS_FETCHED(buf, f, PS_DOWN);
		f = ps_update(640, 600, false, NULL);
		PE_EndInput();
		PE_ReleaseMessage();
	}
	parts_multi_controller = multi;
	/* the shared link functions of the engines before v14 (GUIEngine,
	 * GoatGUIEngine, the older PartsEngine bind them): -1 is no link, and
	 * the Set gets or makes both parts, as before */
	ps_box(PS_B, 400, 100);
	PS_CHECK(PE_GetOnCursorShowLinkPartsNumber(PS_B) == -1, "PS10 v13: a parts never linked: Get = %d, want -1",
		 PE_GetOnCursorShowLinkPartsNumber(PS_B));
	PS_CHECK(!parts_try_get(PS_C), "PS10 parts %d exists before the link", PS_C);
	PE_SetOnCursorShowLinkPartsNumber(PS_B, PS_C);
	PS_CHECK(PE_GetOnCursorShowLinkPartsNumber(PS_B) == PS_C, "PS10 v13: Set(B, C) then Get(B) = %d, want %d",
		 PE_GetOnCursorShowLinkPartsNumber(PS_B), PS_C);
	PS_CHECK(parts_try_get(PS_C) && parts_get(PS_C)->linked_from == PS_B,
		 "PS10 v13: Set(B, C) did not make C linked from B (C %s, linked_from %d)",
		 parts_try_get(PS_C) ? "made" : "not made", parts_try_get(PS_C) ? parts_get(PS_C)->linked_from : 0);
	/* (a guard: before the change there is no decision to ask, and the
	 * legacy branch it has kept is parts_render's own) */
	if (ps_link_shown_fn())
		ps_shown("PS10 v13: B linked to C, C not entered", PS_B, false);
	else
		printf("    PS10 no parts_link_shown (before the change): the display's decision not asked\n");
	/* SetClickMissSoundNumber (the older engines' click-miss sound): no
	 * sound for a click where there is no parts, as before */
	PE_SetClickMissSoundNumber(PS_MISS);
	PE_BeginInput();
	PE_ReleaseMessage();
	ps_update(640, 600, false, NULL);
	ps_update(640, 600, false, NULL);
	struct ps_fetch f = ps_update(640, 600, true, NULL);
	f = ps_update(640, 600, false, NULL);
	PS_NONE("PS10 v13: SetClickMissSoundNumber, then a click where there is no parts", f);
	PE_EndInput();
	PE_ReleaseMessage();
	ain->version = version;
	printf("  PS10 v13 unchanged\n");
}

/* --- PS11 (guard) --- */
static uint64_t ps_digest;
static int ps_nobs;
static void ps_fold(const void *p, size_t n)
{
	const uint8_t *b = p;
	for (size_t i = 0; i < n; i++)
		ps_digest = (ps_digest ^ b[i]) * 0x100000001b3ull;
	ps_nobs++;
}
static void ps_fold_get(int no, int state)
{
	int slot = BU_PE("Parts_GetSoundName", BU_I(no), BU_I(state)).i;
	bool valid = slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_STRING && heap[slot].s;
	int len = valid ? heap[slot].s->size : -1;
	ps_fold(&len, sizeof len);
	if (valid) {
		ps_fold(heap[slot].s->text, len);
		heap_unref(slot);
	}
}
static void ps_fold_fetch(struct ps_fetch f)
{
	ps_fold(&f.n, sizeof f.n);
	ps_fold(f.id, sizeof(int) * (f.n < 16 ? f.n : 16));
}
static void ps_rules_run(void)
{
	ps_digest = 0xcbf29ce484222325ull;
	ps_nobs = 0;
	ps_setup();
	ps_sound_activity("PSNSounds");
	int nos[] = { ps_number("PSNSounds", ""), ps_number("PSNSounds", "Btn"), ps_number("PSNSounds", "Inner"),
		ps_number("PSNSounds", "Dbyte") };
	for (int i = 0; i < 4; i++)
		for (int s = 0; s < 4; s++)
			ps_fold_get(nos[i], s);
	ps_rect(nos[1], 100, 100);
	ps_rect(nos[3], 400, 100);
	PE_BeginInput();
	int path[][3] = { { 640, 600, 0 }, { 640, 600, 0 }, { 150, 140, 0 }, { 150, 140, 1 }, { 150, 140, 0 }, { 450, 140, 0 },
		{ 450, 140, 1 }, { 450, 140, 0 }, { 640, 600, 0 } };
	for (unsigned i = 0; i < sizeof(path) / sizeof(path[0]); i++)
		ps_fold_fetch(ps_update(path[i][0], path[i][1], path[i][2], NULL));
	PE_EndInput();
}
static void ps_rules(void)
{
	const char *names[3] = { "the GBK rule", "the SJIS rule", "the GB18030 flag alone" };
	uint64_t got[3][2] = { { 0 } };
	for (int k = 0; k < 3; k++) {
		int fd[2];
		if (pipe(fd)) { printf("PARTS-SOUND-NAME PS11 pipe failed\n"); exit(2); }
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			close(fd[0]);
			li_charset(k);
			ps_rules_run();
			fflush(stdout);
			uint64_t out[2] = { ps_digest, (uint64_t)ps_nobs };
			ssize_t w = write(fd[1], out, sizeof out);
			_exit(w == (ssize_t)sizeof out ? 0 : 3);
		}
		close(fd[1]);
		ssize_t r = read(fd[0], got[k], sizeof got[k]);
		close(fd[0]);
		int status = 0;
		waitpid(pid, &status, 0);
		PS_CHECK(r == (ssize_t)sizeof got[k] && WIFEXITED(status) && WEXITSTATUS(status) == 0,
			 "PS11 the run under %s did not finish (status %d)", names[k], status);
	}
	printf("    PS11 %llu observations, digests %016llx %016llx %016llx\n", (unsigned long long)got[0][1],
	       (unsigned long long)got[0][0], (unsigned long long)got[1][0], (unsigned long long)got[2][0]);
	PS_CHECK(got[0][1] >= 20, "PS11 the run under %s made %llu observations, want 20 or more", names[0], (unsigned long long)got[0][1]);
	PS_CHECK(got[0][0] == got[1][0] && got[1][0] == got[2][0] && got[0][1] == got[1][1] && got[1][1] == got[2][1],
		 "PS11 the names or the sounds differ between the string rules");
	printf("  PS11 the string rules\n");
}

/* --- PS12 --- */
static bool ps_miss_is(const char *what, const char *want)
{
	union vm_value r[1] = { BR_I(-1) };
	br_call("PartsEngine", "GetClickMissSoundName", 0, 0, 0, NULL, r, 1);
	int slot = r[0].i;
	bool valid = slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_STRING && heap[slot].s;
	bool ok = valid && !strcmp(heap[slot].s->text, want);
	PS_CHECK(ok, "%s: GetClickMissSoundName = \"%s\", want \"%s\"", what, valid ? heap[slot].s->text : "(no string)", want);
	if (valid)
		heap_unref(slot);
	return ok;
}
static void ps_miss(void)
{
	ps_setup();
	ps_miss_is("PS12 at first", "");
	int s = sf_str(ps_sounds[PS_MISS], -1);
	BU_PE("SetClickMissSoundName", BU_I(s));
	heap_unref(s);
	ps_miss_is("PS12 after Set", ps_sounds[PS_MISS]);
	ps_box(PS_A, 100, 100);
	ps_scope();
	struct ps_fetch f = ps_update(640, 600, true, NULL);
	f = ps_update(640, 600, false, NULL);
	PS_NONE("PS12 a click where there is no parts", f);
	PE_EndInput();
	s = sf_str("", 0);
	BU_PE("SetClickMissSoundName", BU_I(s));
	heap_unref(s);
	ps_miss_is("PS12 after Set(\"\") (parts::detail::WaitForClick)", "");
	printf("  PS12 the click-miss name\n");
}

/* --- PS13 (guard) --- */
static void ps_numbers(void)
{
	ps_setup();
	ps_box(PS_A, 100, 100);
	PS_CHECK(PE_SetPartsOnCursorSoundNumber(PS_A, PS_ALT) && PE_SetPartsClickSoundNumber(PS_A, PS_ALT_DOWN),
		 "PS13 the number API refused a sound of the archive");
	ps_scope();
	ps_hover_click("PS13 the port's numbers in v14", PS_A, 150, 140, PS_ALT, PS_ALT_DOWN);
	PE_EndInput();
	printf("  PS13 the observation\n");
}

/* --- PS14 --- */
enum { PS_LOW_BTN = 19621, PS_BOX_BTN };
static void ps_layers(void)
{
	/* layer-input's setup: the multi-controller mode, the scene's layer */
	li_setup();
	ps_audio();
	probe_install_sound_assets(ps_sounds, PS_NR);
	li_button(PS_LOW_BTN, 100, 100, 200, 80, 3);
	ps_set(PS_LOW_BTN, ps_sounds[PS_ON], 2);
	ps_set(PS_LOW_BTN, ps_sounds[PS_DOWN], 3);
	li_scope();
	/* a box's layer with a button away from the scene's */
	int box = ci_add(-1);
	PS_CHECK(box != li_scene && ci_active() == box, "PS14 AddLayer: layer %d, active %d (scene %d)", box, ci_active(), li_scene);
	li_button(PS_BOX_BTN, 700, 400, 200, 80, 6);
	ps_set(PS_BOX_BTN, ps_sounds[PS_B_ON], 2);
	ps_set(PS_BOX_BTN, ps_sounds[PS_B_DOWN], 3);
	PE_BeginInput();
	struct he_frame m;
	struct ps_fetch f = ps_update(640, 600, false, NULL);
	PS_NONE("PS14 the box opened", f);
	f = ps_update(150, 140, false, &m);
	PS_NONE("PS14 the cursor over the scene's button under the box", f);
	PS_CHECK(he_count(&m, 0, PS_LOW_BTN) == 0, "PS14 the scene's button got %d MouseEnter under the box, want 0",
		 he_count(&m, 0, PS_LOW_BTN));
	f = ps_update(150, 140, true, NULL);
	f = ps_update(150, 140, false, &m);
	PS_NONE("PS14 a click on the scene's button under the box", f);
	f = ps_update(750, 440, false, &m);
	ps_follow("PS14 the box's button: entering", &f, &m, PS_BOX_BTN, PS_B_ON, PS_B_DOWN);
	PS_FETCHED("PS14 the box's button: entering", f, PS_B_ON);
	f = ps_update(750, 440, true, NULL);
	PS_NONE("PS14 the box's button: the press", f);
	f = ps_update(750, 440, false, &m);
	ps_follow("PS14 the box's button: the release", &f, &m, PS_BOX_BTN, PS_B_ON, PS_B_DOWN);
	PS_FETCHED("PS14 the box's button: the release", f, PS_B_DOWN);
	f = ps_update(640, 600, false, NULL);
	PS_NONE("PS14 the box's button: leaving", f);
	/* the box closes: its WaitForClick's EndInput, then EraseLayer */
	PE_EndInput();
	ci_erase(box);
	PS_CHECK(ci_active() == li_scene, "PS14 after EraseLayer: active %d, want %d", ci_active(), li_scene);
	f = ps_update(150, 140, false, &m);
	ps_follow("PS14 the scene's button after the box", &f, &m, PS_LOW_BTN, PS_ON, PS_DOWN);
	PS_FETCHED("PS14 the scene's button after the box", f, PS_ON);
	PE_EndInput();
	printf("  PS14 the layers\n");
}

/* --- PS15 --- */
/* One update in the order of PE_UpdateScaled: the components, the anonymous
 * channels' collection (audio_update), then the input. */
static struct he_frame ps_frame(int x, int y, bool left)
{
	extern bool key_state[];
	mouse_set_pos(x, y);
	key_state[VK_LBUTTON] = left;
	PE_UpdateComponent(0);
	audio_update();
	PE_UpdateInputState(16);
	return he_drain();
}

static void ps_pull(int frames)
{
	static float buf[SG_CHUNK * 2];
	for (int off = 0; off < frames; off += SG_CHUNK)
		probe_audio_pull(buf, frames - off < SG_CHUNK ? frames - off : SG_CHUNK);
}

/* The script's channel id: IsPlay and IsExistID (KiwiSoundEngine, through
 * the AIN) are want. */
static void ps_script_id(const char *what, int id, bool want)
{
	int play = sg_i1("IsPlay", id), exist = sg_i1("IsExistID", id);
	PS_CHECK(!!play == want && !!exist == want, "%s: IsPlay(%d) = %d, IsExistID(%d) = %d, want %d", what, id, play, id,
		 exist, want);
}

enum { PS_SND_A = 19631, PS_SND_B };
static void ps_apart(void)
{
	if (!sg_make_data()) {
		ps_failures++;
		return;
	}
	sg_setup(&sg_group_opts);
	he_env();
	br_input_setup();
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	PS_CHECK(sg_numof() == SG_NR_GROUPS, "PS15 the synthetic sound groups are not there (%d groups)", sg_numof());
	/* one sg_tone_a (a 2 s constant of group 2) in the output */
	const float g = 8192 / 32768.0f * sg_gv(2);
	float got;
#define PS_LEVEL(what, n) do { got = sg_level(); \
	PS_CHECK(SG_NEAR(got, (n) * g), "%s: level %.5f, want %.5f (%d sounds of group 2)", what, got, (n) * g, n); } while (0)
	/* the script's music on 1, the first of sound::detail::g_BGM's two */
	PS_CHECK(sg_play_loop(1, "sg_tone_a", 0), "PS15 the script's Prepare and Play of 1 failed");
	PS_LEVEL("PS15 the script's music", 1);
	/* The sounds are set by number (the port's own fields, PS13): the
	 * names give the same numbers (PS1, PS3), and the case stands apart
	 * from them, so that before the change it fails on the channels alone. */
	int tone_a = -1, tone_b = -1;
	PS_CHECK(asset_exists_by_name(ASSET_SOUND, "sg_tone_a", &tone_a) && asset_exists_by_name(ASSET_SOUND, "sg_tone_b", &tone_b),
		 "PS15 the synthetic sounds are not in the archive");
	br_box(PS_SND_A, 100, 100, 200, 80, true);
	br_box(PS_SND_B, 400, 100, 200, 80, true);
	PS_CHECK(PE_SetPartsOnCursorSoundNumber(PS_SND_A, tone_a) && PE_SetPartsClickSoundNumber(PS_SND_A, tone_a)
		 && PE_SetPartsOnCursorSoundNumber(PS_SND_B, tone_a), "PS15 the number API refused a sound of the archive");
	PE_BeginInput();
	ps_frame(640, 600, false);
	ps_frame(640, 600, false);
	/* the engine's sounds play and take none of the script's IDs (the
	 * original plays them in the SE table, apart from the general IDs) */
	struct he_frame m = ps_frame(150, 140, false);
	PS_CHECK(he_count(&m, 0, PS_SND_A) == 1, "PS15 entering A: %d MouseEnter", he_count(&m, 0, PS_SND_A));
	PS_LEVEL("PS15 entering A: the music and A's cursor sound", 2);
	ps_script_id("PS15 the cursor sound took no ID", 0, false);
	ps_frame(150, 140, true);
	m = ps_frame(150, 140, false);
	PS_CHECK(he_count(&m, 4, PS_SND_A) == 1, "PS15 clicking A: %d MouseClick", he_count(&m, 4, PS_SND_A));
	PS_LEVEL("PS15 clicking A: and its click sound", 3);
	ps_script_id("PS15 the click sound took no ID (0)", 0, false);
	ps_script_id("PS15 the click sound took no ID (2)", 2, false);
	/* the script prepares the music's second channel (a change of music,
	 * CASBGM@Prepare), and plays it after a frame in which the cursor
	 * enters B: neither the engine's sounds nor the script's channel are
	 * cut */
	PS_CHECK(sg_prepare(2, "sg_tone_a", false), "PS15 the script's Prepare of 2 failed");
	PS_LEVEL("PS15 after the script's Prepare of 2: the engine's sounds go on", 3);
	m = ps_frame(450, 140, false);
	PS_CHECK(he_count(&m, 0, PS_SND_B) == 1, "PS15 entering B: %d MouseEnter", he_count(&m, 0, PS_SND_B));
	PS_CHECK(sg_i2("SetLoopCount", 2, 0), "PS15 SetLoopCount(2, 0) failed");
	PS_CHECK(sg_i1("Play", 2), "PS15 the script's Play of 2 after a frame failed (its channel was closed)");
	ps_script_id("PS15 the script's 2 plays", 2, true);
	ps_script_id("PS15 the script's 1 plays", 1, true);
	PS_LEVEL("PS15 the music on 1 and 2, A's two sounds, B's cursor sound", 5);
	/* the engine's sounds end and are collected; the script's go on */
	ps_pull(88200 + SG_CHUNK);
	ps_frame(640, 600, false);
	ps_frame(640, 600, false);
	ps_script_id("PS15 the engine's sounds collected: the script's 1", 1, true);
	ps_script_id("PS15 the engine's sounds collected: the script's 2", 2, true);
	PS_LEVEL("PS15 the engine's sounds ended: the music on 1 and 2", 2);
	/* every ended sound frees its channel: more enters than there are
	 * anonymous channels (64) still play (sg_tone_b: 8192 frames, group 1) */
	PS_CHECK(PE_SetPartsOnCursorSoundNumber(PS_SND_A, tone_b), "PS15 the number API refused sg_tone_b");
	const float b = 4096 / 32768.0f * sg_gv(1);
	int played = 0;
	for (int i = 0; i < 70; i++) {
		ps_frame(640, 600, false);
		ps_frame(150, 140, false);
		got = sg_level();
		played += SG_NEAR(got, 2 * g + b);
		ps_pull(4 * SG_CHUNK);
	}
	PS_CHECK(played == 70, "PS15 %d of 70 enters were heard (a channel of an ended sound is not freed)", played);
	/* audio_reset (SACT2's _ModuleFini; v14 does not reset the sounds)
	 * closes them all, the engine's as well */
	ps_frame(640, 600, false);
	ps_frame(150, 140, false);
	PE_EndInput();
	audio_reset();
	got = sg_level();
	PS_CHECK(got == 0.0f, "PS15 after audio_reset: level %.5f, want 0", got);
#undef PS_LEVEL
	ps_failures += sg_failures;
	sg_remove_data();
	printf("  PS15 the engine's sounds and the script's IDs\n");
}

static int test_parts_sound_name(void)
{
	void (*cases[])(void) = { ps_pactex, ps_set_get, ps_hover, ps_disabled, ps_nothing, ps_play, ps_link, ps_no_link,
		ps_save, ps_save_layers, ps_v13, ps_rules, ps_miss, ps_numbers, ps_layers, ps_apart };
	const char *names[] = { "PS1", "PS2", "PS3", "PS4", "PS5", "PS6", "PS7", "PS8", "PS9", "PS9M", "PS10", "PS11", "PS12",
		"PS13", "PS14", "PS15" };
	const char *what[] = {
		"the pactex keys: the root, a child, a nested child; played when entered and clicked",
		"Set and Get through the AIN: four slots, \"\" clears, get-or-create, bounds, bytes",
		"slot 2 at each enter, slot 3 at each click, with their messages",
		"a disabled button plays nothing and keeps its names",
		"guard: no name, \"\", an unknown name, a cleared name ask the archive for nothing",
		"Parts_PlaySound: 2 and 3 play, other states nothing",
		"the link resolved by name in its own activity; the AIN Set and Get",
		"guard: no link is 0",
		"the save: the names and the link survive Save and Load",
		"the save in the game's mode: PCDF, PWHE, PSND, PCID; without PSND; a broken PSND",
		"guard: v13, the number API and the old input path",
		"guard: the same under the GBK, SJIS and flag-only string rules",
		"the click-miss name is kept and not played",
		"guard: the port's sound numbers play at the v14 enter and release (the observation works)",
		"the layers: a button under a box's layer is not heard, the box's is; after the box, the scene's is",
		"the engine's sounds take none of the script's channel IDs; a Prepare before its Play is not closed",
	};
	int n = sizeof(cases) / sizeof(cases[0]), failed = 0;
	for (int i = 0; i < n; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			ps_failures = he_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(ps_failures + he_failures ? 1 : 0);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (!ok)
			failed++;
		printf("%s %s (status %d): %s\n", names[i], ok ? "PASS" : "FAIL", status, what[i]);
	}
	printf("PARTS-SOUND-NAME %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
