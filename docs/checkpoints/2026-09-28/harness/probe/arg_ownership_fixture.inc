/* arg-ownership: a struct, delegate or array argument is handed over to the
 * function that is called; the function's page gives it back.
 *
 * Original (static disassembly). A call moves the arguments into the new
 * function page with 0x657430 (called at 0x66a082, 0x66a166 and 0x66a45b).
 * Its type table (0x657508 -> 0x6574fc) retains the value (0x679f10) for the
 * reference types 18-21, 51, 67, 80 and 93, a wrap (82), type 87 and an
 * interface (89), and stores a string (12), a struct (13), a delegate (63)
 * and an array (79) as they are; an option (86) that holds a value goes by
 * its payload. When the function returns its page releases every variable
 * (0x6570f0 -> 0x656cf0 -> 0x656c10, table 0x656c88): one release (0x679f90)
 * for each of those types alike, nothing for int, float, bool and enum, and
 * an option by its payload unless it is none.
 *
 * So the value of a string, struct, delegate or array argument carries a
 * reference of its own, and the bytecode always makes one: A_REF, NEW,
 * DG_NEW, DG_NEW_FROM_METHOD, the result of a call, a string expression, or
 * an array literal whose local is set to -1 before the call ("moved"). Of
 * the 1895 CALLFUNC and NEW sites in this AIN whose last argument is of one
 * of those three page types, none produces it any other way.
 *
 * This VM retained a struct, delegate or array argument again when it moved
 * it into the page, so each such call left one reference behind. A delegate
 * made from a lambda owns the local page it was made in (the third slot of
 * its entry), so a function that passed a lambda never lost its locals:
 * FrameImageCache@Join makes a SceneContext (a parts layer) in a local,
 * hands a lambda to Observer::Create and waits; when it returned the
 * SceneContext was not destroyed and the layer stayed, on top of the battle
 * screen and active, so the parts made afterwards (the skill range lines)
 * covered the skill buttons.
 *
 *   AO1  The real FrameImageCache@Join, twice: a SceneContext is constructed
 *        and destructed in each call (SceneContext@1 -> AFL_Parts_EraseLayer),
 *        the lambda ran and ended the wait, and the active controller is the
 *        one from before the call. The second call leaves only the known
 *        leaks listed below.
 *   AO2  Handed over, through the real callees: a Customer to
 *        MatchingInfo@0(Customer) (kept in a member: one reference, the
 *        member's), a CASPosF to MathHelper::CalcAngle (released), an array
 *        to MatchingInfo@0(Customer, array) (released: the member is a
 *        copy), a delegate to AFL_Parts_AddBeginUpdateEvent, which copies it
 *        down three calls (released, and one delegate page more in all: the
 *        list's), and a delegate to Observer::Create, whose own A_REF and
 *        NEW Observer pass a copy on (released; after the real
 *        Observer@Release the observer and its own delegates are gone, and
 *        the update record's delegate page stays, see below).
 *   AO3  Borrowed, as before: a wrap argument (ObserverCollection@Add) keeps
 *        the caller's reference; a string that is not a temporary keeps the
 *        caller's reference while a temporary one is consumed; and an
 *        argument C code pushes for vm_call stays the caller's.
 *   AO4  Two hundred rounds of Observer::Create and Observer@Release leave
 *        nothing but the known leaks below. The counts are exact because
 *        nothing else shows a value released once too often: heap_unref
 *        returns silently on a slot that is already free, so neither the
 *        sanitizer nor the log reports it.
 *   G1   An AIN before version 14 keeps the older rule: function_call
 *        retains the struct argument (two references on entry, the caller's
 *        one after the return).
 *
 * Where the expectations are xsystem4's, not the original's:
 *   - A string argument is told apart by this VM's temporary flag, not by
 *     its type (AO3).
 *   - vm_call's arguments are borrowed (AO3); the original's C entry
 *     (0x66a1a0) hands them over like bytecode does.
 *   - AO1 needs what the game's start-up would have made: the two global
 *     update-event lists (empty arrays here), PartsEngine's _PreLink forms,
 *     a windowless GL context, a key state that is never down and a window
 *     size. FrameImageCache.MinLoadTime is set to 0 so that the wait ends
 *     in the first frames (the probe's clock does not move).
 *
 * Known leaks of other mechanisms, pinned as they are (to be lowered when
 * they are fixed): the box this VM gives a wrap local that X_ASSIGN then
 * overwrites; and the update-event record: AddPartsUpdateEvent hands its
 * local record to Array.PushBack with A_REF (one reference) and the library
 * retains it again, so the record and its delegate page outlive the list
 * (the same as SelectableIndexArray<T>@Insert in array-literal). One round
 * of Observer::Create and Release leaves 2 boxes, 1 record and 1 emptied
 * delegate page; one FrameImageCache@Join 25, 4 and 1.
 *
 * What the cases do not reach: the arguments of DG_CALL (delegate-args) and
 * of an HLL callback (vm_call_nopop) are copied by their own code and are
 * not changed; a call that is skipped (a function without code, an invalid
 * method number) still drops its arguments unreleased.
 *
 * Every case runs in a child process. Exit code 1 is a failed check, exit
 * code 2 the environment (the AIN lacks something a case looks up, or no GL
 * context for AO1). A signal, a sanitizer abort or the instruction budget
 * running out is that case failing.
 */
#include <sys/wait.h>
#include <unistd.h>

static int ao_failures;
#define AO_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); ao_failures++; } } while (0)

extern int PE_get_active_controller(void);

static int ao_key_is_down(int key)
{
	(void)key;
	return 0;
}

static int ao_global(const char *name)
{
	for (int i = 0; i < ain->nr_globals; i++)
		if (!strcmp(ain->globals[i].name, name))
			return i;
	printf("ARG-OWNERSHIP global %s not declared\n", name);
	exit(2);
}

/* An empty array for a global the game's start-up would have initialized. */
static void ao_global_struct_array(const char *name)
{
	int g = ao_global(name);
	struct ain_type *t = &ain->globals[g].type;
	if (t->data != AIN_ARRAY || !t->array_type || t->array_type->data != AIN_STRUCT) {
		printf("ARG-OWNERSHIP global %s is not an array of structs\n", name);
		exit(2);
	}
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
	p->array.rank = 1;
	p->array.struct_type = t->array_type->struc;
	heap[global_page_slot].page->values[g].i = heap_alloc_page(p);
}

/* The probe links the libraries without their _PreLink. Run PartsEngine's,
 * which puts the v14 forms of its functions in its table (RemoveController
 * taking a wrap<array<int>>, ...), and bind the linked functions to them. */
static void ao_parts_prelink(void)
{
	extern struct static_library lib_PartsEngine;
	int pe = sp_lib("PartsEngine");
	for (int i = 0; lib_PartsEngine.functions[i].name; i++)
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PreLink"))
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
	for (int f = 0; f < ain->libraries[pe].nr_functions; f++)
		for (int i = 0; lib_PartsEngine.functions[i].name; i++)
			if (!strcmp(lib_PartsEngine.functions[i].name, ain->libraries[pe].functions[f].name)) {
				libraries[pe][f].fun = lib_PartsEngine.functions[i].fun;
				break;
			}
}

static int ao_count_pages(enum page_type type)
{
	int n = 0;
	for (size_t i = 2; i < heap_size; i++)
		if (HEAP_REF(i) > 0 && heap[i].type == VM_PAGE && heap[i].page && heap[i].page->type == type)
			n++;
	return n;
}

static bool ao_alive(int slot, unsigned seq)
{
	return slot > 1 && (size_t)slot < heap_size && HEAP_REF(slot) > 0 && heap[slot].seq == seq;
}

/* The slots that appeared since xl_mark, by kind. Three kinds are known
 * leaks of other mechanisms (see the header): the box of a wrap local, an
 * update-event record that Array.PushBack retained again, and the emptied
 * delegate page such a record holds. */
struct ao_left { int boxes, records, delegates, empty_delegates, other; };

static struct ao_left ao_left_since_mark(bool show)
{
	struct ao_left l = { 0, 0, 0, 0, 0 };
	int record = sp_sno("parts::detail::SPartsUpdateData");
	for (size_t i = 2; i < heap_size; i++) {
		if (HEAP_REF(i) <= 0 || (i < xl_mark_size && xl_mark_map[i]))
			continue;
		struct page *p = heap[i].type == VM_PAGE ? heap[i].page : NULL;
		if (al_wrap_box(i))
			l.boxes++;
		else if (p && p->type == STRUCT_PAGE && p->index == record)
			l.records++;
		else if (p && p->type == DELEGATE_PAGE && p->nr_vars == 0)
			l.empty_delegates++;
		else if (p && p->type == DELEGATE_PAGE)
			l.delegates++;
		else
			l.other++;
	}
	if (show && l.other)
		al_show_left();
	return l;
}

/* A MatchingInfo as NEW leaves it for its constructor, except that the
 * Customer this VM puts in the wrap<Customer> member is taken out: the
 * constructor's X_ASSIGN would overwrite it without releasing it. */
static int ao_matching_info(void)
{
	int st = sp_sno("MatchingInfo"), obj = alloc_struct(st), m = sp_mem(st, "Customer");
	int first = heap[obj].page->values[m].i;
	heap[obj].page->values[m].i = -1;
	if (first > 0)
		heap_unref(first);
	return obj;
}

/* --- AO1: FrameImageCache@Join --- */

static void ao_join(void)
{
	cn_gl_setup();
	ao_parts_prelink();
	da_parts_init();
	sdl.w = sdl.viewport.w = 1280;
	sdl.h = sdl.viewport.h = 720;
	int il = sp_lib("IbisInputEngine");
	libraries[il][sp_libfn(il, "Key_IsDown")].fun = (void *)ao_key_is_down;
	ao_global_struct_array("parts::detail::g_dgPartsBeginUpdateEvent");
	ao_global_struct_array("parts::detail::g_dgPartsEndUpdateEvent");

	int st = sp_sno("FrameImageCache"), join = da_fn("FrameImageCache@Join");
	int ctor = al_fn_args("SceneContext@0", 1), dtor = da_fn("SceneContext@1"), leave = da_fn("SceneContext@Exit");
	int lambda = da_fn("FrameImageCache@<lambda : FrameImageCache@Join()(25, 26)>"), erase = da_fn("AFL_Parts_EraseLayer");
	int cache = vm_construct_struct(st);
	heap[cache].page->values[sp_mem(st, "MinLoadTime")].i = 0;
	for (int round = 1; round <= 2; round++) {
		unsigned long c0 = calls[ctor], d0 = calls[dtor], l0 = calls[lambda], x0 = calls[leave], e0 = calls[erase];
		int active = PE_get_active_controller();
		size_t base = live_slots();
		int sp = or_guard_push();
		vm_call(join, cache);
		or_guard_pop("FrameImageCache@Join", sp, 0, NULL);
		long grown = (long)live_slots() - (long)base;
		printf("AO1 Join %d: SceneContext constructed %lu, destructed %lu; the lambda ran %lu time(s), Exit %lu; "
		       "EraseLayer %lu; active controller %d -> %d; %+ld live\n", round, calls[ctor] - c0, calls[dtor] - d0,
		       calls[lambda] - l0, calls[leave] - x0, calls[erase] - e0, active, PE_get_active_controller(), grown);
		AO_CHECK(calls[ctor] - c0 == 1 && calls[lambda] - l0 >= 1 && calls[leave] - x0 == 1,
			"Join %d did not make one SceneContext and end its wait through the lambda", round);
		AO_CHECK(calls[dtor] - d0 == 1 && calls[erase] - e0 == 1,
			"Join %d returned and its SceneContext was destructed %lu time(s), its layer erased %lu time(s); want 1 and 1",
			round, calls[dtor] - d0, calls[erase] - e0);
		AO_CHECK(PE_get_active_controller() == active, "Join %d left controller %d active, was %d", round,
			PE_get_active_controller(), active);
		/* the first call makes the singletons (ObserverCollection, the
		 * timer manager, ...); what the second leaves is of the known kinds */
		if (round == 2) {
			struct ao_left l = ao_left_since_mark(true);
			printf("AO1 the second Join left %d wrap box(es), %d update record(s), %d empty delegate(s), %d other\n", l.boxes,
			       l.records, l.empty_delegates, l.other);
			AO_CHECK(l.other == 0 && l.delegates == 0, "the second Join left %d slot(s) of another kind", l.other + l.delegates);
			/* One record, not four, since Array.Realloc gives an element one
			 * reference (array-construct): the three were the elements of
			 * CallPartsUpdateEvent's local list, which Array.Copy replaced
			 * and the extra reference kept. */
			AO_CHECK(l.boxes == 25 && l.records == 1 && l.empty_delegates == 1,
				"the known leaks of a Join are 25 boxes, 1 record and 1 empty delegate; got %d, %d and %d", l.boxes, l.records,
				l.empty_delegates);
		}
		xl_mark();
	}
}

/* --- AO2: handed over --- */

static void ao_handed_over(void)
{
	da_parts_init();
	int mi = sp_sno("MatchingInfo"), cu = sp_sno("Customer");
	if (ain->structures[mi].destructor > 0 || ain->structures[cu].destructor > 0) {
		printf("ARG-OWNERSHIP MatchingInfo or Customer has a destructor\n");
		exit(2);
	}
	int ctor1 = al_fn_args("MatchingInfo@0", 1), ctor2 = al_fn_args("MatchingInfo@0", 2);
	size_t base = live_slots();

	/* a struct that the callee keeps: this.Customer = c */
	int obj = ao_matching_info(), c = alloc_struct(cu);
	unsigned cseq = heap[c].seq;
	or_method(ctor1, obj, 1, &c);
	printf("AO2 struct kept by the callee: %d reference(s) after the call\n", ao_alive(c, cseq) ? HEAP_REF(c) : 0);
	AO_CHECK(ao_alive(c, cseq) && HEAP_REF(c) == 1 && heap[obj].page->values[sp_mem(mi, "Customer")].i == c,
		"MatchingInfo@0(Customer): the customer has %d reference(s), want the member's one", ao_alive(c, cseq) ? HEAP_REF(c) : 0);
	heap_unref(obj);
	AO_CHECK(!ao_alive(c, cseq), "the customer outlived the MatchingInfo");

	/* a struct the callee only reads */
	int angle = da_fn("MathHelper::CalcAngle"), pos = alloc_struct(sp_sno("CASPosF"));
	unsigned pseq = heap[pos].seq;
	union vm_value r[2];
	int n = da_callfunc(angle, 1, &pos, r);
	printf("AO2 struct only read: %s after the call\n", ao_alive(pos, pseq) ? "ALIVE" : "released");
	AO_CHECK(n == 1 && !ao_alive(pos, pseq), "MathHelper::CalcAngle(CASPosF): %d result slot(s), the argument has %d reference(s)", n,
		ao_alive(pos, pseq) ? HEAP_REF(pos) : 0);

	/* an array: the callee stores a copy */
	obj = ao_matching_info();
	c = alloc_struct(cu);
	struct page *wp = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
	wp->array.rank = 1;
	wp->array.struct_type = 1;
	int w = heap_alloc_page(wp);
	unsigned wseq = heap[w].seq;
	int a2[2] = { c, w };
	or_method(ctor2, obj, 2, a2);
	int member = heap[obj].page->values[sp_mem(mi, "FrontWorker")].i;
	printf("AO2 array: %s after the call; the member is %s\n", ao_alive(w, wseq) ? "ALIVE" : "released",
	       member != w && al_page(member) ? "a copy" : "NOT A COPY");
	AO_CHECK(!ao_alive(w, wseq) && member != w && al_page(member),
		"MatchingInfo@0(Customer, array): the array has %d reference(s), want it released and a copy in the member",
		ao_alive(w, wseq) ? HEAP_REF(w) : 0);
	heap_unref(obj);
	AO_CHECK(live_slots() == base, "%+ld live slot(s) after the struct and array calls", (long)live_slots() - (long)base);

	/* a delegate, copied down three calls into the update-event list */
	ao_global_struct_array("parts::detail::g_dgPartsBeginUpdateEvent");
	int g = ao_global("parts::detail::g_dgPartsBeginUpdateEvent"), list = heap[global_page_slot].page->values[g].i;
	int target = alloc_struct(sp_sno("Observer")), update = da_fn("Observer@Update");
	int d = heap_alloc_page(delegate_new_from_method(target, update));
	unsigned dseq = heap[d].seq;
	int pages = ao_count_pages(DELEGATE_PAGE);
	xl_mark();
	da_callfunc(da_fn("AFL_Parts_AddBeginUpdateEvent"), 1, &d, NULL);
	printf("AO2 delegate copied down three calls: %s after the call; delegate pages %d -> %d; the list has %d\n",
	       ao_alive(d, dseq) ? "ALIVE" : "released", pages, ao_count_pages(DELEGATE_PAGE), wc_size(list));
	AO_CHECK(!ao_alive(d, dseq), "AFL_Parts_AddBeginUpdateEvent: the delegate has %d reference(s)", ao_alive(d, dseq) ? HEAP_REF(d) : 0);
	AO_CHECK(wc_size(list) == 1 && ao_count_pages(DELEGATE_PAGE) == pages,
		"%d delegate page(s) more than before (the argument went and the record's came: want 0), the list has %d",
		ao_count_pages(DELEGATE_PAGE) - pages, wc_size(list));
	heap[global_page_slot].page->values[g].i = -1;
	heap_unref(list);
	/* known: the record outlives the list (Array.PushBack retained the
	 * value A_REF handed it), and its delegate page with it */
	struct ao_left l = ao_left_since_mark(true);
	AO_CHECK(l.other == 0 && l.records == 1 && l.delegates == 1 && l.empty_delegates == 0 && ao_count_pages(DELEGATE_PAGE) == pages,
		"after the list went: %d record(s), %d delegate page(s) and %d slot(s) of another kind; the known leak is 1 record with "
		"its delegate page", l.records, l.delegates + l.empty_delegates, l.other);

	/* a delegate through Observer::Create: its own A_REF and NEW Observer hand a copy on */
	ao_global_struct_array("parts::detail::g_dgPartsBeginUpdateEvent");
	list = heap[global_page_slot].page->values[g].i;
	int create = da_fn("Observer::Create"), release = da_fn("Observer@Release");
	int f = heap_alloc_page(delegate_new_from_method(target, update));
	unsigned fseq = heap[f].seq;
	pages = ao_count_pages(DELEGATE_PAGE);
	da_callfunc(create, 1, &f, NULL);
	int coll = heap[global_page_slot].page->values[ao_global("ObserverCollection::m_instance")].i;
	int observers = da_live_page(coll) ? sp_get(coll, "m_observers") : -1;
	struct page *op = al_page(observers);
	int obs = op && op->nr_vars == 1 ? op->values[0].i : -1;
	unsigned oseq = da_live_page(obs) ? heap[obs].seq : 0;
	int with = ao_count_pages(DELEGATE_PAGE);
	printf("AO2 Observer::Create: the argument %s; the observer has %d reference(s); delegate pages %d\n",
	       ao_alive(f, fseq) ? "ALIVE" : "released", da_live_page(obs) ? HEAP_REF(obs) : -1, with);
	AO_CHECK(!ao_alive(f, fseq), "Observer::Create: the delegate has %d reference(s)", ao_alive(f, fseq) ? HEAP_REF(f) : 0);
	AO_CHECK(da_live_page(obs) && HEAP_REF(obs) == 1, "the observer is not the collection's alone (%d)",
		da_live_page(obs) ? HEAP_REF(obs) : -1);
	if (da_live_page(obs)) {
		/* the argument went; m_func (the copy), m_checkFunc (empty) and the
		 * update record's came */
		AO_CHECK(with == pages - 1 + 3, "%d delegate page(s) while the observer lives, want %d", with, pages - 1 + 3);
		heap_ref(obs);
		or_method(release, obs, 0, NULL);
		heap_unref(obs);
		printf("AO2 Observer@Release: the observer %s; delegate pages %d; the update list has %d\n",
		       ao_alive(obs, oseq) ? "ALIVE" : "released", ao_count_pages(DELEGATE_PAGE), wc_size(list));
		AO_CHECK(!ao_alive(obs, oseq), "the observer has %d reference(s) after its Release", HEAP_REF(obs));
		/* known: the record and its (emptied) delegate page stay, as above */
		AO_CHECK(wc_size(list) == 0 && ao_count_pages(DELEGATE_PAGE) == pages - 1 + 1,
			"%d delegate page(s) and %d update event(s) after the Release, want %d (the leaked record's) and 0",
			ao_count_pages(DELEGATE_PAGE), wc_size(list), pages);
	}
	heap_unref(target);
}

/* --- AO3: borrowed --- */

static void ao_borrowed(void)
{
	/* a wrap: ObserverCollection@Add(wrap<Observer>) stores it with Array.Add */
	int coll = vm_construct_struct(sp_sno("ObserverCollection")), obs = alloc_struct(sp_sno("Observer"));
	or_method(da_fn("ObserverCollection@Add"), coll, 1, &obs);
	printf("AO3 wrap: %d reference(s) after the call (the caller's and the array's)\n", HEAP_REF(obs));
	AO_CHECK(HEAP_REF(obs) == 2, "ObserverCollection@Add: the observer has %d reference(s), want 2", HEAP_REF(obs));
	heap_unref(coll);
	AO_CHECK(HEAP_REF(obs) == 1, "the observer has %d reference(s) after the collection went, want the caller's", HEAP_REF(obs));
	heap_unref(obs);

	/* a string: AdvFuncFinder@0(string) */
	int ctor = al_fn_args("AdvFuncFinder@0", 1), st = sp_sno("AdvFuncFinder");
	int finder = alloc_struct(st), plain = sf_str("folder", -1);
	or_method(ctor, finder, 1, &plain);
	heap_unref(finder);
	AO_CHECK(HEAP_REF(plain) == 1 && !strcmp(xl_text(plain), "folder"), "a string that is not a temporary has %d reference(s)",
		HEAP_REF(plain));
	heap_unref(plain);
	finder = alloc_struct(st);
	size_t base = live_slots();
	int sp = stack_ptr;
	stack_push_string(cstr_to_string("folder"));	/* as S_PUSH leaves it */
	int temp = stack[stack_ptr - 1].i;
	unsigned tseq = heap[temp].seq;
	stack_ptr = sp;
	or_method(ctor, finder, 1, &temp);
	printf("AO3 string: not a temporary kept; a temporary %s, %+ld live\n", ao_alive(temp, tseq) ? "ALIVE" : "consumed",
	       (long)live_slots() - (long)base);
	AO_CHECK(!ao_alive(temp, tseq), "a temporary string has %d reference(s) after the call", ao_alive(temp, tseq) ? HEAP_REF(temp) : 0);
	heap_unref(finder);

	/* vm_call: C code keeps what it pushed (a sort comparison's elements) */
	int pos = alloc_struct(sp_sno("CASPosF"));
	unsigned pseq = heap[pos].seq;
	sp = stack_ptr;
	stack_push(pos);
	vm_call(da_fn("MathHelper::CalcAngle"), -1);
	int results = stack_ptr - sp;
	stack_ptr = sp;
	printf("AO3 vm_call: the struct C code pushed has %d reference(s) after the call\n", ao_alive(pos, pseq) ? HEAP_REF(pos) : 0);
	AO_CHECK(results == 1 && ao_alive(pos, pseq) && HEAP_REF(pos) == 1,
		"vm_call: %d result slot(s), the struct has %d reference(s), want the caller's one", results,
		ao_alive(pos, pseq) ? HEAP_REF(pos) : 0);
	if (ao_alive(pos, pseq))
		heap_unref(pos);
}

/* --- AO4: many rounds --- */

static void ao_rounds(void)
{
	da_parts_init();
	ao_global_struct_array("parts::detail::g_dgPartsBeginUpdateEvent");
	int g = ao_global("parts::detail::g_dgPartsBeginUpdateEvent");
	int create = da_fn("Observer::Create"), release = da_fn("Observer@Release"), update = da_fn("Observer@Update");
	int target = alloc_struct(sp_sno("Observer"));
	size_t after_first = 0;
	int pages_first = 0, bad = 0;
	xl_mark();
	for (int round = 0; round < 200; round++) {
		int f = heap_alloc_page(delegate_new_from_method(target, update));
		da_callfunc(create, 1, &f, NULL);
		int coll = heap[global_page_slot].page->values[ao_global("ObserverCollection::m_instance")].i;
		struct page *op = da_live_page(coll) ? al_page(sp_get(coll, "m_observers")) : NULL;
		int obs = op && op->nr_vars == 1 ? op->values[0].i : -1;
		if (!da_live_page(obs)) {
			bad++;
			break;
		}
		heap_ref(obs);
		or_method(release, obs, 0, NULL);
		heap_unref(obs);
		if (round == 0) {
			after_first = live_slots();
			pages_first = ao_count_pages(DELEGATE_PAGE);
		}
		if (round == 0)
			xl_mark();
	}
	struct ao_left l = ao_left_since_mark(true);
	printf("AO4 199 more rounds of Observer::Create and Release: %+ld live slot(s): %d wrap box(es), %d update record(s), "
	       "%d empty delegate(s), %d other; the update list has %d\n", (long)live_slots() - (long)after_first, l.boxes, l.records,
	       l.empty_delegates, l.other, wc_size(heap[global_page_slot].page->values[g].i));
	(void)pages_first;
	AO_CHECK(!bad, "a round found no observer in the collection");
	AO_CHECK(l.other == 0 && l.delegates == 0, "%d slot(s) of another kind were left by 199 rounds", l.other + l.delegates);
	/* known, per round: two boxes, one record and its emptied delegate page */
	AO_CHECK(l.boxes == 2 * 199 && l.records == 199 && l.empty_delegates == 199,
		"the known leaks of 199 rounds are 398 boxes, 199 records and 199 empty delegates; got %d, %d and %d", l.boxes,
		l.records, l.empty_delegates);
}

/* --- G1: before version 14 --- */

static void ao_guard_v13(void)
{
	int angle = da_fn("MathHelper::CalcAngle"), pos = alloc_struct(sp_sno("CASPosF"));
	unsigned pseq = heap[pos].seq;
	size_t ip = instr_ptr;
	int sp = stack_ptr, version = ain->version;
	stack_push(pos);
	ain->version = 13;
	function_call(angle, VM_RETURN);
	ain->version = version;
	int entered = HEAP_REF(pos);
	vm_execute();
	instr_ptr = ip;
	stack_ptr = sp;
	printf("G1 version 13: %d reference(s) on entry, %d after the return\n", entered, ao_alive(pos, pseq) ? HEAP_REF(pos) : 0);
	AO_CHECK(entered == 2 && ao_alive(pos, pseq) && HEAP_REF(pos) == 1,
		"version 13: the struct argument had %d reference(s) on entry and %d after the return, want 2 and 1", entered,
		ao_alive(pos, pseq) ? HEAP_REF(pos) : 0);
	if (ao_alive(pos, pseq))
		heap_unref(pos);
}

static int test_arg_ownership(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "AO1", ao_join }, { "AO2", ao_handed_over }, { "AO3", ao_borrowed }, { "AO4", ao_rounds }, { "G1", ao_guard_v13 },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			ao_failures = da_failures = wc_failures = oa_failures = bu_failures = or_failures = xl_failures = al_failures = 0;
			cases[i].run();
			al_collect();
			ao_failures += al_failures;
			fflush(stdout);
			_exit(ao_failures ? 1 : 0);
		}
		int st = 0;
		if (pid < 0 || waitpid(pid, &st, 0) != pid) {
			printf("%s ENVIRONMENT (fork/wait failed)\n", cases[i].name);
			environment++;
		} else if (WIFSIGNALED(st)) {
			printf("%s FAIL (terminated by signal %d)\n", cases[i].name, WTERMSIG(st));
			failed++;
		} else if (WEXITSTATUS(st) == 0) {
			printf("%s ok\n", cases[i].name);
		} else if (WEXITSTATUS(st) == 2 || WEXITSTATUS(st) == CN_ENV_EXIT) {
			printf("%s ENVIRONMENT (the AIN lacks something this case looks up, or no GL context)\n", cases[i].name);
			environment++;
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("ARG-OWNERSHIP %s: %d of %zu cases failed, %d environment error(s)\n",
		failed || environment ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}
