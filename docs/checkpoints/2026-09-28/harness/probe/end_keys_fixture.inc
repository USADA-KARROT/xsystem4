/* end-keys: the end keys of an activity (結束キー; G6, 2026-10-20), the keys
 * that close the settings and the two backlogs. Read statically (details in
 * src/hll/pe_v14_activity.c, end_key_insert and pactex_read_end_keys):
 *  - the activity loader 0x560210 reads the list block 結束キー (its key
 *    0x7db894) before the activity tree and inserts each item into a
 *    std::set<int> at activity +0x38 (0x562920; the size at +0x3c);
 *  - SetActivityEndKey (0x58ae30) inserts, EraseActivityEndKey (0x55e5f0)
 *    erases, IsExistActivityEndKey (0x58ae90) finds, NumofActivityEndKey
 *    (0x58aef0) is the size, GetActivityEndKey (0x58af30) walks from the
 *    smallest and is 0 out of range; under a name no activity has, nothing
 *    happens (false, 0);
 *  - the script: activity::detail::CActivity@BindEndType makes one CASClick
 *    per key of GetEndKeyList (Numof, then Get 0..n-1) with Init(key, 1);
 *    CActivity@UpdateActivity leaves its loop when IsClickEndKey is true,
 *    a CASClick@IsClick (IbisInputEngine.Key_IsDown) at the key's release,
 *    which leaves that key in g_LastEndKey.
 *
 *   EK1 the pactex block through the production loader: (2, 27, 32) as the
 *       settings and the backlog have it, (2, 27) as the scene backlog, no
 *       block, an unordered list with a key twice (once each, ascending), a
 *       block of that name that is not a list and a list under another name
 *       (no keys), and the block in a resource without an activity tree
 *       (read all the same; the load fails as before).
 *   EK2 the five HLLs through their AIN declarations: Set keeps a key once
 *       and in order, Erase takes one out (an absent one changes nothing),
 *       IsExist, Numof, Get out of range is 0; an unknown name does nothing
 *       and makes no activity; a released name loaded again starts from its
 *       pactex alone; releasing another activity keeps the keys of the one
 *       moved into its place.
 *   EK3 real bytecode: CActivity@BindEndType on the settings' keys makes
 *       three CASClick, key codes 2, 27, 32; CActivity@IsClickEndKey is
 *       false while a key is down, true at its release with g_LastEndKey
 *       that key, false again after it (one end per click); Return, not an
 *       end key, never ends it; a key down already at BindEndType does not
 *       end it at its release (Init's first key down cancel), the next click
 *       does.
 *   EK4 the right button, Esc and the space on that path: real SDL events (a
 *       right button press and release, key down and up of Esc and of the
 *       space) through handle_events reach the production Key_IsDown, and
 *       IsClickEndKey ends at each release (the right button's release after
 *       the engine's 50 ms hold, mouse_hold_until).
 *   EK5 the schedule's key step (key,T,VK[,MS], src/input.c): the key is
 *       down from T for MS ms (default 100) and up after, the pointer does
 *       not move, the next step starts after the release; the verb in either
 *       case; eleven wrong forms are not understood.
 * Expected values are written from the original's semantics above, not read
 * from the engine. The activities are synthetic pactex fed to the production
 * loader (pactex_load) by a probe-only ReadActivityFile. No GL, no game
 * assets. Each case runs in its own fork.
 */

static int ek_failures;
#define EK_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ek_failures++; } } while (0)

#define EK_KEY "\xbd\x59\xca\xf8\xa5\xad\xa9\x60"	/* 結束キー (GBK) */

extern int sact_Key_IsDown(int keycode);

static struct ex_list *ek_int_list(int n, const int *v)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = n;
	l->items = xcalloc(n ? n : 1, sizeof(*l->items));
	for (int i = 0; i < n; i++)
		l->items[i].value = (struct ex_value){ .type = EX_INT, .i = v[i] };
	return l;
}

/* A resource: the activity tree (unless `tree` is false), then the blocks. */
static struct ex *ek_resource(bool tree, int nr_blocks, const char **names, struct ex_value *vals)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = (tree ? 1 : 0) + nr_blocks;
	e->blocks = xcalloc(e->nr_blocks ? e->nr_blocks : 1, sizeof(*e->blocks));
	int b = 0;
	if (tree) {
		e->blocks[0].name = cstr_to_string("EndKeys");
		e->blocks[0].val.type = EX_TREE;
		struct ex_tree *top = xcalloc(1, sizeof(*top));
		*top = bu_branch("EndKeys", 1);
		top->children[0] = bu_comp(AR_ROOT, BU_LAYOUT, 0, 1);
		top->children[0].children[1].children[0] = bu_comp("Item", BU_LOW, 0, 0);
		e->blocks[0].val.tree = top;
		b = 1;
	}
	for (int i = 0; i < nr_blocks; i++, b++) {
		e->blocks[b].name = cstr_to_string(names[i]);
		e->blocks[b].val = vals[i];
	}
	return e;
}

static struct ex_value ek_keys(int n, const int *v)
{
	return (struct ex_value){ .type = EX_LIST, .list = ek_int_list(n, v) };
}

static bool ek_read_activity_file(struct string *name, struct string *filename, possibly_unused bool edit)
{
	const char *f = filename->text;
	struct ex *e;
	if (!strcmp(f, "EKSettings")) {
		e = ek_resource(true, 1, (const char *[]){ EK_KEY }, (struct ex_value[]){ ek_keys(3, (int[]){ 2, 27, 32 }) });
	} else if (!strcmp(f, "EKScene")) {
		e = ek_resource(true, 1, (const char *[]){ EK_KEY }, (struct ex_value[]){ ek_keys(2, (int[]){ 2, 27 }) });
	} else if (!strcmp(f, "EKUnordered")) {
		e = ek_resource(true, 1, (const char *[]){ EK_KEY }, (struct ex_value[]){ ek_keys(4, (int[]){ 32, 2, 32, 27 }) });
	} else if (!strcmp(f, "EKOther")) {
		e = ek_resource(true, 2, (const char *[]){ EK_KEY, "EndKey" },
			(struct ex_value[]){ { .type = EX_INT, .i = 27 }, ek_keys(3, (int[]){ 2, 27, 32 }) });
	} else {
		e = ek_resource(true, 0, NULL, NULL);
	}
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

static void ek_setup(void)
{
	lr_setup();
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)ek_read_activity_file);
	lr_replace_hll("IbisInputEngine", "Key_IsDown", (void *)df_key_is_down);
	ci_add(-1);
}

static bool ek_load(const char *name, const char *file)
{
	return BU_PE("ReadActivityFile", BU_I(sf_str(name, -1)), BU_I(sf_str(file, -1)), BU_I(0)).i;
}

static int ek_numof(const char *name) { return BU_PE("NumofActivityEndKey", BU_I(sf_str(name, -1))).i; }
static int ek_get(const char *name, int i) { return BU_PE("GetActivityEndKey", BU_I(sf_str(name, -1)), BU_I(i)).i; }
static bool ek_exists(const char *name, int key) { return BU_PE("IsExistActivityEndKey", BU_I(sf_str(name, -1)), BU_I(key)).i; }
static void ek_set(const char *name, int key) { BU_PE("SetActivityEndKey", BU_I(sf_str(name, -1)), BU_I(key)); }
static void ek_erase(const char *name, int key) { BU_PE("EraseActivityEndKey", BU_I(sf_str(name, -1)), BU_I(key)); }
static bool ek_activity(const char *name) { return BU_PE("IsExistActivity", BU_I(sf_str(name, -1))).i; }

/* The keys of `name` are exactly want[0..n-1], in this order. */
static void ek_expect(const char *what, const char *name, int n, const int *want)
{
	int got = ek_numof(name);
	EK_CHECK(got == n, "%s: NumofActivityEndKey %d, want %d", what, got, n);
	for (int i = 0; i < n; i++) {
		int k = ek_get(name, i);
		EK_CHECK(k == want[i], "%s: GetActivityEndKey(%d) %d, want %d", what, i, k, want[i]);
		EK_CHECK(ek_exists(name, want[i]), "%s: IsExistActivityEndKey(%d) false", what, want[i]);
	}
	EK_CHECK(ek_get(name, n) == 0 && ek_get(name, -1) == 0 && ek_get(name, n + 5) == 0,
		 "%s: GetActivityEndKey out of range (%d, -1, %d) gives %d, %d, %d, want 0", what, n, n + 5,
		 ek_get(name, n), ek_get(name, -1), ek_get(name, n + 5));
}

/* --- EK1 --- */
static void ek_pactex(void)
{
	ek_setup();
	EK_CHECK(ek_load("EK1S", "EKSettings"), "loading the settings' keys failed");
	ek_expect("(2, 27, 32)", "EK1S", 3, (int[]){ 2, 27, 32 });
	EK_CHECK(!ek_exists("EK1S", 13) && !ek_exists("EK1S", 0), "(2, 27, 32): Return or 0 is an end key");
	EK_CHECK(ek_load("EK1B", "EKScene"), "loading the scene backlog's keys failed");
	ek_expect("(2, 27)", "EK1B", 2, (int[]){ 2, 27 });
	EK_CHECK(!ek_exists("EK1B", 32), "(2, 27): the space is an end key");
	EK_CHECK(ek_load("EK1N", "EKNone"), "loading a resource without the block failed");
	ek_expect("no block", "EK1N", 0, NULL);
	EK_CHECK(ek_load("EK1U", "EKUnordered"), "loading the unordered keys failed");
	ek_expect("(32, 2, 32, 27)", "EK1U", 3, (int[]){ 2, 27, 32 });
	EK_CHECK(ek_load("EK1O", "EKOther"), "loading the other blocks failed");
	ek_expect("an int block of that name, a list of another", "EK1O", 0, NULL);
	/* The block is read before the tree is looked for. */
	struct ex *e = ek_resource(false, 1, (const char *[]){ EK_KEY }, (struct ex_value[]){ ek_keys(3, (int[]){ 2, 27, 32 }) });
	struct string *n = cstr_to_string("EK1T");
	bool ok = probe_load_activity_tree(n, e);
	free_string(n);
	ex_free(e);
	EK_CHECK(!ok, "a resource without an activity tree loaded");
	ek_expect("without a tree", "EK1T", 3, (int[]){ 2, 27, 32 });
	printf("  EK1 the pactex block\n");
}

/* --- EK2 --- */
static void ek_hlls(void)
{
	ek_setup();
	EK_CHECK(BU_PE("CreateActivity", BU_I(sf_str("EK2", -1))).i, "CreateActivity failed");
	ek_expect("a new activity", "EK2", 0, NULL);
	ek_set("EK2", 32);
	ek_set("EK2", 2);
	ek_set("EK2", 27);
	ek_set("EK2", 2);
	ek_expect("Set 32, 2, 27, 2", "EK2", 3, (int[]){ 2, 27, 32 });
	ek_erase("EK2", 27);
	ek_expect("Erase 27", "EK2", 2, (int[]){ 2, 32 });
	EK_CHECK(!ek_exists("EK2", 27), "Erase 27: 27 still exists");
	ek_erase("EK2", 99);
	ek_expect("Erase 99 (absent)", "EK2", 2, (int[]){ 2, 32 });
	ek_set("EK2", -5);
	ek_expect("Set -5", "EK2", 3, (int[]){ -5, 2, 32 });
	/* No activity of that name. */
	ek_set("EK2X", 27);
	ek_erase("EK2X", 27);
	EK_CHECK(!ek_activity("EK2X"), "Set or Erase under an unknown name made an activity");
	EK_CHECK(ek_numof("EK2X") == 0 && ek_get("EK2X", 0) == 0 && !ek_exists("EK2X", 27),
		 "an unknown name: Numof %d, Get(0) %d, IsExist(27) %d, want 0, 0, 0",
		 ek_numof("EK2X"), ek_get("EK2X", 0), ek_exists("EK2X", 27));
	/* A released name, loaded again. */
	EK_CHECK(ek_load("EK2R", "EKSettings"), "loading EK2R failed");
	ek_set("EK2R", 13);
	int list = ar_int_list();
	EK_CHECK(BU_PE("ReleaseActivity", BU_I(sf_str("EK2R", -1)), BU_I(list)).i, "ReleaseActivity(EK2R) failed");
	EK_CHECK(ek_numof("EK2R") == 0, "a released name has %d end keys, want 0", ek_numof("EK2R"));
	EK_CHECK(ek_load("EK2R", "EKSettings"), "loading EK2R again failed");
	ek_expect("loaded again", "EK2R", 3, (int[]){ 2, 27, 32 });
	/* Releasing one moves the last into its place. */
	EK_CHECK(ek_load("EK2A", "EKScene") && ek_load("EK2B", "EKUnordered"), "loading EK2A, EK2B failed");
	ek_set("EK2B", 13);
	int list2 = ar_int_list();
	EK_CHECK(BU_PE("ReleaseActivity", BU_I(sf_str("EK2", -1)), BU_I(list2)).i, "ReleaseActivity(EK2) failed");
	ek_expect("EK2B after EK2 went", "EK2B", 4, (int[]){ 2, 13, 27, 32 });
	ek_expect("EK2A after EK2 went", "EK2A", 2, (int[]){ 2, 27 });
	ek_expect("EK2R after EK2 went", "EK2R", 3, (int[]){ 2, 27, 32 });
	printf("  EK2 the five HLLs\n");
}

/* --- EK3, EK4: the script's polling --- */
static int ek_activity_object(const char *name)
{
	int st = ain_get_struct(ain, "activity::detail::CActivity");
	int obj = new_object_from_ain(st);
	int fno = ain_get_function(ain, "activity::detail::CActivity@BindEndType");
	int arg = sf_str(name, -1);
	call_method_args(fno, obj, 1, &arg);
	return obj;
}

static int ek_end_clicks(int obj, int *keys, int max)
{
	int st = ain_get_struct(ain, "activity::detail::CActivity");
	int slot = heap[obj].page->values[br_member(st, "endKey")].i;
	struct page *a = slot > 0 ? heap[slot].page : NULL;
	int n = a ? a->nr_vars : 0;
	for (int i = 0; i < n && i < max; i++) {
		/* CASClick member 0, <KeyCode> (CASClick@KeyCode::get, FUNC 6362). */
		struct page *c = heap_index_valid(a->values[i].i) ? heap[a->values[i].i].page : NULL;
		keys[i] = c ? c->values[0].i : -1;
	}
	return n;
}

static bool ek_is_click_end_key(int obj)
{
	int fno = ain_get_function(ain, "activity::detail::CActivity@IsClickEndKey");
	call_method_args(fno, obj, 0, NULL);
	return stack_pop().i;
}

static int ek_last_end_key(void)
{
	return heap[global_page_slot].page->values[ain_get_global(ain, "activity::detail::g_LastEndKey")].i;
}

static void ek_set_last(int v)
{
	heap[global_page_slot].page->values[ain_get_global(ain, "activity::detail::g_LastEndKey")].i = v;
}

static void ek_polling(void)
{
	ek_setup();
	extern bool key_state[];
	EK_CHECK(ek_load("EK3", "EKSettings"), "loading EK3 failed");
	int obj = ek_activity_object("EK3");
	int keys[8] = { 0 };
	int n = ek_end_clicks(obj, keys, 8);
	EK_CHECK(n == 3 && keys[0] == 2 && keys[1] == 27 && keys[2] == 32,
		 "BindEndType made %d CASClick (%d, %d, %d), want 3 (2, 27, 32)", n, keys[0], keys[1], keys[2]);
	EK_CHECK(!ek_is_click_end_key(obj), "an end before any key");
	const int order[] = { VK_RBUTTON, VK_ESCAPE, VK_SPACE };
	for (int i = 0; i < 3; i++) {
		int k = order[i];
		ek_set_last(255);
		key_state[k] = true;
		EK_CHECK(!ek_is_click_end_key(obj), "key %d: an end while it is down", k);
		EK_CHECK(!ek_is_click_end_key(obj), "key %d: an end while it is still down", k);
		key_state[k] = false;
		EK_CHECK(ek_is_click_end_key(obj), "key %d: no end at its release", k);
		EK_CHECK(ek_last_end_key() == k, "key %d: g_LastEndKey %d", k, ek_last_end_key());
		EK_CHECK(!ek_is_click_end_key(obj), "key %d: a second end after one release", k);
	}
	key_state[VK_RETURN] = true;
	EK_CHECK(!ek_is_click_end_key(obj), "Return down ends it");
	key_state[VK_RETURN] = false;
	EK_CHECK(!ek_is_click_end_key(obj), "Return up ends it");
	/* Held at the bind, polled every frame as UpdateActivity does: its
	 * release is the first key down, cancelled. */
	key_state[VK_RBUTTON] = true;
	int held = ek_activity_object("EK3");
	EK_CHECK(!ek_is_click_end_key(held), "a right button held at BindEndType ends it");
	key_state[VK_RBUTTON] = false;
	EK_CHECK(!ek_is_click_end_key(held), "the release of a right button held at BindEndType ends it");
	key_state[VK_RBUTTON] = true;
	EK_CHECK(!ek_is_click_end_key(held), "the next right press ends it");
	key_state[VK_RBUTTON] = false;
	EK_CHECK(ek_is_click_end_key(held), "the next right click does not end it");
	/* The scene backlog: no space. */
	EK_CHECK(ek_load("EK3B", "EKScene"), "loading EK3B failed");
	int scene = ek_activity_object("EK3B");
	key_state[VK_SPACE] = true;
	EK_CHECK(!ek_is_click_end_key(scene), "(2, 27): space down ends it");
	key_state[VK_SPACE] = false;
	EK_CHECK(!ek_is_click_end_key(scene), "(2, 27): space up ends it");
	printf("  EK3 BindEndType and IsClickEndKey\n");
}

static void ek_push_button(bool down)
{
	SDL_Event e = { .type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP };
	e.button.button = SDL_BUTTON_RIGHT;
	e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.button.x = 640;
	e.button.y = 360;
	SDL_PushEvent(&e);
}

static void ek_push_key(SDL_Scancode sc, bool down)
{
	SDL_Event e = { .type = down ? SDL_KEYDOWN : SDL_KEYUP };
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = sc;
	SDL_PushEvent(&e);
}

/* handle_events reads the events once per millisecond of SDL's clock. */
static void ek_pump(int ms)
{
	SDL_Delay(ms);
	handle_events();
}

static void ek_real_input(void)
{
	if (SDL_Init(SDL_INIT_EVENTS)) {
		EK_CHECK(false, "SDL_Init: %s", SDL_GetError());
		return;
	}
	sdl.w = 1280;
	sdl.h = 720;
	sdl.viewport = (SDL_Rect){ 0, 0, 1280, 720 };
	ek_setup();
	/* The production Key_IsDown, as the script's AFL_IsKeyDown calls it. */
	lr_replace_hll("IbisInputEngine", "Key_IsDown", (void *)sact_Key_IsDown);
	extern bool key_state[];
	key_state[VK_RBUTTON] = key_state[VK_ESCAPE] = key_state[VK_SPACE] = false;
	EK_CHECK(ek_load("EK4", "EKSettings"), "loading EK4 failed");
	int obj = ek_activity_object("EK4");
	EK_CHECK(!ek_is_click_end_key(obj), "an end before any input");

	ek_set_last(255);
	ek_push_button(true);
	ek_pump(2);
	EK_CHECK(sact_Key_IsDown(VK_RBUTTON) == 1, "a right button press: Key_IsDown(2) is %d", sact_Key_IsDown(VK_RBUTTON));
	EK_CHECK(!ek_is_click_end_key(obj), "the right button: an end while it is down");
	ek_push_button(false);
	ek_pump(2);
	EK_CHECK(sact_Key_IsDown(VK_RBUTTON) == 1, "a right button released within 50 ms of its press: Key_IsDown(2) is %d", sact_Key_IsDown(VK_RBUTTON));
	EK_CHECK(!ek_is_click_end_key(obj), "the right button: an end within its 50 ms hold");
	ek_pump(60);
	EK_CHECK(sact_Key_IsDown(VK_RBUTTON) == 0, "a right button release: Key_IsDown(2) is %d", sact_Key_IsDown(VK_RBUTTON));
	EK_CHECK(ek_is_click_end_key(obj) && ek_last_end_key() == VK_RBUTTON,
		 "the right button's release: no end (g_LastEndKey %d)", ek_last_end_key());

	const SDL_Scancode sc[] = { SDL_SCANCODE_ESCAPE, SDL_SCANCODE_SPACE };
	const int vk[] = { VK_ESCAPE, VK_SPACE };
	for (int i = 0; i < 2; i++) {
		ek_set_last(255);
		ek_push_key(sc[i], true);
		ek_pump(2);
		EK_CHECK(sact_Key_IsDown(vk[i]) == 1, "key %d down: Key_IsDown is %d", vk[i], sact_Key_IsDown(vk[i]));
		EK_CHECK(!ek_is_click_end_key(obj), "key %d: an end while it is down", vk[i]);
		ek_push_key(sc[i], false);
		ek_pump(2);
		EK_CHECK(sact_Key_IsDown(vk[i]) == 0, "key %d up: Key_IsDown is %d", vk[i], sact_Key_IsDown(vk[i]));
		EK_CHECK(ek_is_click_end_key(obj) && ek_last_end_key() == vk[i],
			 "key %d's release: no end (g_LastEndKey %d)", vk[i], ek_last_end_key());
	}
	printf("  EK4 real events on the polling path\n");
}

/* --- EK5 --- */
static void ek_schedule_key(void)
{
	extern bool key_state[];
	key_state[VK_ESCAPE] = key_state[VK_SPACE] = false;
	EK_CHECK(input_auto_load("move,0,300,200;key,1000,27;KEY,2000,32,50;move,3000,7,8") == 4,
		 "the key steps are not 4 steps");
	input_auto_update(0);
	int x0 = -1, y0 = -1, x, y;
	mouse_peek_pos(&x0, &y0);
	EK_CHECK(x0 == 300 && y0 == 200, "the first move put the pointer at (%d,%d)", x0, y0);
	input_auto_update(999);
	EK_CHECK(!key_state[VK_ESCAPE], "Esc down before its time");
	input_auto_update(1000);
	EK_CHECK(key_state[VK_ESCAPE], "Esc not down at its time");
	mouse_peek_pos(&x, &y);
	EK_CHECK(x == x0 && y == y0, "the key step moved the pointer to (%d,%d)", x, y);
	input_auto_update(1099);
	EK_CHECK(key_state[VK_ESCAPE], "Esc up before 100 ms");
	input_auto_update(1100);
	EK_CHECK(!key_state[VK_ESCAPE], "Esc still down after 100 ms");
	input_auto_update(1999);
	EK_CHECK(!key_state[VK_SPACE], "the space down before its time");
	input_auto_update(2000);
	EK_CHECK(key_state[VK_SPACE] && !key_state[VK_ESCAPE], "the space not down at its time (or Esc down)");
	input_auto_update(2049);
	EK_CHECK(key_state[VK_SPACE], "the space up before 50 ms");
	input_auto_update(3000);	/* the release; the move waits for the next update */
	EK_CHECK(!key_state[VK_SPACE], "the space still down after 50 ms");
	mouse_peek_pos(&x, &y);
	EK_CHECK(x == x0 && y == y0, "the move started in the update of the release: (%d,%d)", x, y);
	input_auto_update(3001);
	mouse_peek_pos(&x, &y);
	EK_CHECK(x == 7 && y == 8, "the move after the key did not happen: (%d,%d)", x, y);
	const char *bad[] = { "key,1", "key,1,0", "key,1,146", "key,1,1", "key,1,2", "key,1,4", "key,-1,27",
			      "key,1,27,-1", "key,1,27,5,6", "key,1,27,X", "key,1,-27" };
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		EK_CHECK(input_auto_load(bad[i]) == 0, "'%s' is understood", bad[i]);
	input_auto_load(NULL);
	printf("  EK5 the schedule's key step\n");
}

static int test_end_keys(void)
{
	void (*cases[])(void) = { ek_pactex, ek_hlls, ek_polling, ek_real_input, ek_schedule_key };
	const char *names[] = { "EK1", "EK2", "EK3", "EK4", "EK5" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			ek_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(ek_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("END-KEYS %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}
