/* parts-image: the parts engine's part of an engine image. Before an engine
 * image is written, the script saves the parts engine into an array<int> of
 * its own (PartsEngine.Save, declared with a wrap<array<int>> argument; the
 * FFI passes the heap slot of the caller's array), and after the image is
 * read back it hands that array to PartsEngine.Load. The original's
 * argument is the array object itself, and its Save writes into it
 * (0x589535 -> 0x59b580 -> 0x5405a0); its Load first releases every parts
 * and controller and then makes them again from the data (0x540830 ->
 * 0x56d870 -> 0x53d150, 0x53eb80).
 *
 * Cases (each in its own process):
 *   PI1  Save into the caller's empty array (no elements, as X_A_INIT 0
 *        leaves it; and a slot with no page): the array holds the save
 *        ("XPE", version 14), the slot keeps its reference count and no
 *        heap slot is left that nobody holds.
 *   PI2  A second save into the same array replaces its page in place: the
 *        same words, no slot more.
 *   PI3  The shape of a retry: three layers, the active one not at the top,
 *        parts on each; saved; then a further layer with a full-screen parts
 *        on top and made active, one layer erased, a parts added. The load
 *        releases all of that first: the layers, their order and IDs, the
 *        active one, every parts' layer, rectangle, clickable and pass
 *        cursor flags are those of the save, and the later parts are gone. A
 *        save made again has the same words.
 *   PI4  Unique IDs (SetEventID, the ID the script's message manager checks
 *        each message against) and delegate indices come back; a parts that
 *        had none has none.
 *   PI5  The load empties the parts message queue (the messages name the
 *        parts and unique IDs of the state that is replaced).
 * Guards:
 *   PI6  Loading an array with no elements, or a slot with no page (what a
 *        build before this one wrote, and what a script that never saved
 *        has), fails and leaves the layers, the active one, the parts, the
 *        unique IDs and the message queue as they were.
 *   PI7  Loading an array that is not a parts save fails and changes nothing.
 *   PI8  A one-slot wrap box holding nothing takes a save and loads it (the
 *        wrap model the adapter also accepts); a second save into the box
 *        replaces its array's page without a slot more.
 *   PI9  An engine before v14 (its own Save and Load declarations, no
 *        adapter): a save of parts with unique IDs has the words of the same
 *        save without them, and no unique ID section.
 * No game assets or GL are used.
 */

static int pi_failures;
#define PI_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); pi_failures++; } } while (0)
#define PI_P0 7101
#define PI_PA 7102
#define PI_PB 7103
#define PI_PG 7104
#define PI_PX 7105
#define PI_UID_MARK 0x44495550 /* "PUID" */

/* Heap slots that something holds. */
static int pi_live(void)
{
	int n = 0;
	for (size_t i = 0; i < heap_size; i++)
		if (heap[i].ref > 0)
			n++;
	return n;
}

/* The script's array<int> as X_A_INIT 0 leaves it (no elements), or a slot
 * without a page. */
static int pi_array(bool with_page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	union vm_value dim = { .i = 0 };
	heap_set_page(slot, with_page ? alloc_array(1, &dim, AIN_ARRAY_INT, 0, false) : NULL);
	return slot;
}

static bool pi_save(int slot) { return ri_hll("Save", 2, (union vm_value[]){ RI_I(slot), RI_I(0) }).i; }
static bool pi_load(int slot) { return ri_hll("Load", 1, (union vm_value[]){ RI_I(slot) }).i; }
static void pi_uid(int no, int dg, int uid) { ri_hll("SetEventID", 3, (union vm_value[]){ RI_I(no), RI_I(dg), RI_I(uid) }); }

static struct page *pi_page(int slot)
{
	return slot > 0 && (size_t)slot < heap_size && heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
}

static bool pi_is_save(struct page *p)
{
	return p && p->type == ARRAY_PAGE && p->nr_vars > 5 && p->values[0].i == 'X' && p->values[1].i == 'P'
		&& p->values[2].i == 'E' && p->values[3].i == 0 && p->values[4].i == 14;
}

static int pi_count_mark(struct page *p, int mark)
{
	int n = 0;
	for (int i = 0; p && i < p->nr_vars; i++)
		n += p->values[i].i == mark;
	return n;
}

static bool pi_same_words(struct page *a, struct page *b)
{
	if (!a || !b || a->nr_vars != b->nr_vars)
		return false;
	for (int i = 0; i < a->nr_vars; i++)
		if (a->values[i].i != b->values[i].i)
			return false;
	return true;
}

static int pi_msgs(void)
{
	/* the queue is read without taking anything: count by popping a copy is
	 * not possible, so only emptiness is asked (-1: no message) */
	return br_msg_type_raw() == -1 ? 0 : 1;
}

/* default layer with P0; A with PA; B with PB; A made active (not the top) */
static void pi_three(int *a, int *b)
{
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	*a = ci_add(-1);
	br_box(PI_PA, 30, 40, 120, 60, true);
	*b = ci_add(-1);
	br_box(PI_PB, 50, 60, 140, 70, true);
	PE_SetClickable(PI_PB, false);
	parts_get(PI_PB)->pass_cursor = true;
	pi_uid(PI_PA, 5, 9001);
	pi_uid(PI_PB, 6, 9002);
	ci_set_active(*a);
}

struct pi_snap { int nr, ids[8], active; int owner[3], dg[3], uid[3], click[3], pass[3], w[3], type[3]; };

static void pi_take(struct pi_snap *s)
{
	static const int nos[3] = { PI_P0, PI_PA, PI_PB };
	memset(s, 0, sizeof *s);
	s->nr = ci_len();
	for (int i = 0; i < s->nr && i < 8; i++)
		s->ids[i] = ci_id_at(i);
	s->active = ci_active();
	for (int i = 0; i < 3; i++) {
		struct parts *p = parts_try_get(nos[i]);
		s->owner[i] = p ? p->controller_no : -999;
		s->dg[i] = p ? p->delegate_index : -999;
		s->uid[i] = p ? p->unique_id : -999;
		s->click[i] = p ? p->clickable : -999;
		s->pass[i] = p ? p->pass_cursor : -999;
		s->w[i] = p ? p->states[0].common.w : -999;
		s->type[i] = p ? p->states[0].type : -999;
	}
}

static void pi_compare(const char *step, struct pi_snap *want, struct pi_snap *got)
{
	PI_CHECK(got->nr == want->nr && !memcmp(got->ids, want->ids, sizeof got->ids), "%s: %d layers [%d %d %d %d], want %d [%d %d %d %d]",
		 step, got->nr, got->ids[0], got->ids[1], got->ids[2], got->ids[3], want->nr, want->ids[0], want->ids[1],
		 want->ids[2], want->ids[3]);
	PI_CHECK(got->active == want->active, "%s: active %d, want %d", step, got->active, want->active);
	for (int i = 0; i < 3; i++) {
		PI_CHECK(got->owner[i] == want->owner[i] && got->dg[i] == want->dg[i] && got->uid[i] == want->uid[i]
			 && got->click[i] == want->click[i] && got->pass[i] == want->pass[i] && got->w[i] == want->w[i]
			 && got->type[i] == want->type[i],
			 "%s: parts %d: layer %d dg %d uid %d click %d pass %d w %d type %d; want %d %d %d %d %d %d %d", step, i,
			 got->owner[i], got->dg[i], got->uid[i], got->click[i], got->pass[i], got->w[i], got->type[i],
			 want->owner[i], want->dg[i], want->uid[i], want->click[i], want->pass[i], want->w[i], want->type[i]);
	}
}

static void pi_save_empty(bool with_page)
{
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	int slot = pi_array(with_page);
	heap_ref(slot);
	int ref = heap[slot].ref, live = pi_live();
	bool ok = pi_save(slot);
	PI_CHECK(ok, "Save returned false");
	PI_CHECK(pi_is_save(pi_page(slot)), "the caller's array holds %d words, not a save (want XPE v14)",
		 pi_page(slot) ? pi_page(slot)->nr_vars : -1);
	PI_CHECK(heap[slot].ref == ref, "the array's reference count %d, was %d", heap[slot].ref, ref);
	PI_CHECK(pi_live() == live, "%d live heap slots after the save, %d before", pi_live(), live);
}
static void pi1a(void) { pi_save_empty(true); printf("PI1 save into an empty array: %s\n", pi_failures ? "FAIL" : "ok"); }
static void pi1b(void) { pi_save_empty(false); printf("PI1 save into a slot without a page: %s\n", pi_failures ? "FAIL" : "ok"); }

static void pi2(void)
{
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "first Save returned false");
	struct page *first = pi_page(slot) ? copy_page(pi_page(slot)) : NULL;
	int live = pi_live();
	PI_CHECK(pi_save(slot), "second Save returned false");
	PI_CHECK(pi_is_save(pi_page(slot)) && pi_same_words(first, pi_page(slot)), "the second save's words differ from the first's");
	PI_CHECK(pi_live() == live, "%d live heap slots after the second save, %d before", pi_live(), live);
	printf("PI2 a second save replaces the page: %s\n", pi_failures ? "FAIL" : "ok");
}

static void pi3(void)
{
	int a, b;
	pi_three(&a, &b);
	struct pi_snap want, got;
	pi_take(&want);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "Save returned false");
	struct page *saved = pi_page(slot) ? copy_page(pi_page(slot)) : NULL;
	/* what happens after the image: a layer on top with a full-screen parts,
	 * made active; B erased; a parts added on the default layer */
	int g = ci_add(b);
	br_box(PI_PG, 0, 0, 1280, 720, true);
	pi_uid(PI_PG, 7, 9003);
	ci_set_active(g);
	ci_erase(b);
	ci_set_active(ci_id_at(0));
	br_box(PI_PX, 0, 0, 10, 10, true);
	ci_set_active(g);
	PI_CHECK(pi_load(slot), "Load returned false");
	pi_take(&got);
	pi_compare("after the load", &want, &got);
	PI_CHECK(!parts_try_get(PI_PG) && !parts_try_get(PI_PX), "parts made after the save are still there: %s %s",
		 parts_try_get(PI_PG) ? "the full-screen one" : "-", parts_try_get(PI_PX) ? "the added one" : "-");
	PI_CHECK(ci_index_of(g) < 0, "the layer made after the save (%d) is still there", g);
	int again = pi_array(true);
	heap_ref(again);
	PI_CHECK(pi_save(again) && pi_same_words(saved, pi_page(again)), "a save made after the load has other words");
	printf("PI3 the load replaces the layers and parts: %s\n", pi_failures ? "FAIL" : "ok");
}

static void pi4(void)
{
	int a, b;
	pi_three(&a, &b);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "Save returned false");
	PI_CHECK(pi_count_mark(pi_page(slot), PI_UID_MARK) == 1, "%d unique ID sections in a save with unique IDs, want 1",
		 pi_count_mark(pi_page(slot), PI_UID_MARK));
	pi_uid(PI_PA, 11, 1);
	pi_uid(PI_PB, 12, 2);
	pi_uid(PI_P0, 13, 3);
	PI_CHECK(pi_load(slot), "Load returned false");
	struct parts *p0 = parts_try_get(PI_P0), *pa = parts_try_get(PI_PA), *pb = parts_try_get(PI_PB);
	PI_CHECK(pa && pa->unique_id == 9001 && pa->delegate_index == 5, "A: unique ID %d, delegate %d; want 9001 5",
		 pa ? pa->unique_id : -999, pa ? pa->delegate_index : -999);
	PI_CHECK(pb && pb->unique_id == 9002 && pb->delegate_index == 6, "B: unique ID %d, delegate %d; want 9002 6",
		 pb ? pb->unique_id : -999, pb ? pb->delegate_index : -999);
	PI_CHECK(p0 && p0->unique_id == 0, "a parts without a unique ID has %d after the load", p0 ? p0->unique_id : -999);
	/* a save of parts without unique IDs has no section */
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	int plain = pi_array(true);
	heap_ref(plain);
	PI_CHECK(pi_save(plain) && pi_count_mark(pi_page(plain), PI_UID_MARK) == 0, "a save without unique IDs has a section");
	printf("PI4 unique IDs: %s\n", pi_failures ? "FAIL" : "ok");
}

static void pi5(void)
{
	int a, b;
	pi_three(&a, &b);
	int slot = pi_array(true);
	heap_ref(slot);
	PI_CHECK(pi_save(slot), "Save returned false");
	parts_enqueue_message(2, PI_PA, 5, 9001);
	parts_enqueue_message(4, 0, -1, 0);
	parts_enqueue_message(2, PI_PB, 6, 9002);
	PI_CHECK(pi_msgs() == 1, "no message queued before the load");
	PI_CHECK(pi_load(slot), "Load returned false");
	PI_CHECK(pi_msgs() == 0, "a message (type %d, parts %d) is still queued after the load", br_msg_type_raw(), br_msg_parts());
	printf("PI5 the load empties the message queue: %s\n", pi_failures ? "FAIL" : "ok");
}

static void pi_unchanged(const char *what, int slot, int a, int b)
{
	(void)a; (void)b;
	struct pi_snap want, got;
	pi_take(&want);
	parts_enqueue_message(2, PI_PA, 5, 9001);
	PI_CHECK(!pi_load(slot), "%s: Load returned true", what);
	pi_take(&got);
	pi_compare(what, &want, &got);
	PI_CHECK(pi_msgs() == 1 && br_msg_parts() == PI_PA, "%s: the queued message went", what);
}

static void pi6(void)
{
	int a, b;
	pi_three(&a, &b);
	pi_unchanged("an array with no elements", pi_array(true), a, b);
	br_msg_release();
	pi_unchanged("a slot with no page", pi_array(false), a, b);
	printf("PI6 loading nothing changes nothing: %s\n", pi_failures ? "FAIL" : "ok");
}

static void pi7(void)
{
	int a, b;
	pi_three(&a, &b);
	int slot = heap_alloc_slot(VM_PAGE);
	union vm_value dim = { .i = 8 };
	struct page *junk = alloc_array(1, &dim, AIN_ARRAY_INT, 0, false);
	for (int i = 0; i < 8; i++)
		junk->values[i].i = 'X' + i;
	heap_set_page(slot, junk);
	pi_unchanged("an array that is not a save", slot, a, b);
	printf("PI7 loading something else changes nothing: %s\n", pi_failures ? "FAIL" : "ok");
}

static void pi8(void)
{
	ci_setup();
	br_box(PI_P0, 10, 20, 100, 50, true);
	int box = variable_initval(AIN_WRAP).i;	/* a struct page without a number, one slot, -1 */
	heap_ref(box);
	PI_CHECK(pi_save(box), "Save into an empty box returned false");
	int inner = heap[box].page->values[0].i;
	PI_CHECK(pi_is_save(pi_page(inner)), "the box holds no save (inner slot %d)", inner);
	int live = pi_live();
	PI_CHECK(pi_save(box), "a second Save into the box returned false");
	PI_CHECK(heap[box].page->values[0].i == inner && pi_is_save(pi_page(inner)) && pi_live() == live,
		 "the second save: inner slot %d (was %d), %d live slots (were %d)", heap[box].page->values[0].i, inner,
		 pi_live(), live);
	ci_erase(ci_add(-1));
	PE_SetClickable(PI_P0, false);
	PI_CHECK(pi_load(box), "Load from the box returned false");
	PI_CHECK(parts_try_get(PI_P0) && parts_try_get(PI_P0)->clickable, "the box's save did not come back");
	printf("PI8 a wrap box: %s\n", pi_failures ? "FAIL" : "ok");
}

static void pi9(void)
{
	ci_setup();
	ain->version = 13;
	probe_init_parts_table();
	br_box(PI_P0, 10, 20, 100, 50, true);
	struct page *plain = NULL, *with = NULL;
	PI_CHECK(PE_Save(&plain), "PE_Save failed");
	parts_get(PI_P0)->unique_id = 4242;
	parts_get(PI_P0)->delegate_index = -1;
	PI_CHECK(PE_Save(&with), "PE_Save failed");
	PI_CHECK(pi_same_words(plain, with) && !pi_count_mark(with, PI_UID_MARK),
		 "a v13 save with a unique ID differs from the one without (%d and %d words)", plain ? plain->nr_vars : -1,
		 with ? with->nr_vars : -1);
	ca_free_buffer(plain);
	ca_free_buffer(with);
	ain->version = 14;
	printf("PI9 engines before v14: %s\n", pi_failures ? "FAIL" : "ok");
}

static int pi_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		pi_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(pi_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_parts_image(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "PI1a", pi1a }, { "PI1b", pi1b }, { "PI2", pi2 }, { "PI3", pi3 }, { "PI4", pi4 },
		{ "PI5", pi5 }, { "PI6", pi6 }, { "PI7", pi7 }, { "PI8", pi8 }, { "PI9", pi9 },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += pi_fork(cases[i].name, cases[i].fn);
	printf("PARTS-IMAGE %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
