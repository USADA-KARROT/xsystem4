/* controller-id: a v14 controller (the script's "layer") has an ID that is
 * not its position in the stack.
 *
 * The original manager ([0x87f8bc] + 0x14c) keeps a vector of controllers
 * (+0x88) and the active one (+0x94, may be null) and finds a controller by
 * the number of its root parts:
 *   AddController(Index)        0x58a6b0 -> 0x53d480 -> 0x53d3a0: Index is the
 *       ID to insert after; any other value: after the active one (0x53d350).
 *   RemoveController(list, Idx) 0x53d5f0 -> 0x53d500: by ID, else the active
 *       one; the last controller stays; removing the active one activates the
 *       one below.
 *   SetActiveController(ID)     0x53d870: an ID that is not there leaves none
 *       designated, and the top one acts (0x58a6f0 -> 0x53e640, 0x53d1d0).
 *   MoveController 0x53d740, GetControllerIndex 0x53fe30, GetControllerID
 *       0x53d800.
 * Before: the ID was the position and AddController took nothing but -1
 * ("index != -1 not supported"), which every CASPartsLayer hit: the config
 * and backlog screens, the scene viewer, the movie, the choices.
 *
 *   CI1  real bytecode AFL_Parts_AddTopLayer on the default layer.
 *   CI2  real bytecode CASPartsLayer@0 and @1.
 *   CI3  the backlog's shape: two scenes, two nested CASPartsLayer.
 *   CI4  AddLayer(ID) inserts in the middle; the layer above keeps its ID and
 *        its parts, which are drawn above the new layer's.
 *   CI5  erasing a middle layer: the layer above keeps its ID.
 *   CI6  erasing the active layer activates the one below (0x53d5b3).
 *   CI7  AddLayer(-1) goes above the active layer, not on top.
 *   CI8  SetActiveLayer with an ID that is not there.
 *   CI9  EraseLayer with an ID that is not there; the last layer stays.
 *   CI10 GetControllerID, GetControllerIndex, MoveController.
 *   CI11 PE_Save and PE_Load of a stack whose IDs are not the positions; a
 *        save without the ID section gives every layer the ID of its
 *        position (layer-root LR8: and warns).
 *   CI12 hit order follows the stack after a move.
 *   CI13 the system overlay controller is not in the stack: a layer added
 *        while it is active goes on top.
 * Guards, which pass before the change as well:
 *   CI14 layers only pushed with -1 and popped: the stack, the active layer
 *        and the sprite layers as before, and the words of a save of such a
 *        stack before its ID section.
 *   CI15 an engine before v14: the push and pop contract, its errors, its
 *        save bytes.
 * Each case runs in its own fork. Only functions that exist before the change
 * are referenced. No game assets or GL are used.
 */

/* IDs are taken lowest-free from a base: 0 while the ID was the position,
 * the layer root parts numbers (PARTS_CONTROLLER_ID_BASE) since layer-root
 * (the original's come from the free-number allocator, 0x418650). CI_ID(i)
 * is the i-th ID over the default layer's, which a stack only pushed holds
 * at position i; the cases here are about the stack, not the numbers
 * (layer-root pins those). CI15 makes its v13 stack anew: that engine's IDs
 * are positions. */
static int ci_base;
#define CI_ID(i) (ci_base + (i))

static int ci_failures;
#define CI_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ci_failures++; } } while (0)
#define CI_OVERLAY 10000

extern void probe_init_parts_table(void);

static int ci_fn(const char *name)
{
	int f = ain_get_function(ain, (char *)name);
	if (f < 0) { printf("CONTROLLER-ID function %s missing\n", name); exit(2); }
	return f;
}

/* Calls a script function with one-slot arguments; returns its one-slot
 * result (0 for void). */
static int ci_call(const char *name, int nargs, const int *args)
{
	int fno = ci_fn(name);
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	int nret = ain_return_slots_type(&ain->functions[fno].return_type);
	assert(call_stack_ptr == csp && stack_ptr == sp + nret);
	int r = nret ? stack[sp].i : 0;
	stack_ptr = sp;
	return r;
}

/* The script's own layer functions (01_AFL\Parts), unchanged. */
static int ci_add(int id) { return ci_call("AFL_Parts_AddLayer", 1, (int[]){ id }); }
static int ci_add_top(void) { return ci_call("AFL_Parts_AddTopLayer", 0, NULL); }
static void ci_erase(int id) { ci_call("AFL_Parts_EraseLayer", 1, (int[]){ id }); }
static int ci_set_active(int id) { return ci_call("AFL_Parts_SetActiveLayer", 1, (int[]){ id }); }
static int ci_active(void) { return ci_call("AFL_Parts_GetActiveLayer", 0, NULL); }
static int ci_len(void) { return ci_call("parts::detail::NumofLayer", 0, NULL); }
static int ci_id_at(int index) { return ci_call("parts::detail::GetLayerID", 1, (int[]){ index }); }
static int ci_index_of(int id) { return ci_call("AFL_Parts_GetLayerIndex", 1, (int[]){ id }); }
static void ci_move(int id, int index) { ci_call("AFL_Parts_MoveLayer", 2, (int[]){ id, index }); }
static bool ci_exists(int id) { return ci_call("parts::detail::IsExistLayer", 1, (int[]){ id }); }

static int ci_global(const char *name)
{
	for (int i = 0; i < ain->nr_globals; i++)
		if (!strcmp(ain->globals[i].name, name))
			return i;
	printf("CONTROLLER-ID global %s missing\n", name);
	exit(2);
}

/* init_probe links without _PreLink (the v14 RemoveController, the
 * multi-controller mode) and leaves every global at -1; the game's alloc
 * function gives the layer functions their manager, events and update lists. */
static void ci_setup(void)
{
	fp_prelink_parts_engine();
	br_input_setup();
	const char *names[] = {
		"parts::detail::g_PartsMessageManager",
		"parts::detail::g_dgPartsErasingLayerEvent",
		"parts::detail::g_dgPartsChangedActiveLayerEvent",
		"parts::detail::g_dgPartsBeginUpdateEvent",
		"parts::detail::g_dgPartsEndUpdateEvent",
	};
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		int g = ci_global(names[i]);
		struct ain_type *t = &ain->globals[g].type;
		heap[global_page_slot].page->values[g] = t->data == AIN_STRUCT
			? (union vm_value){ .i = vm_construct_struct(t->struc) }
			: variable_initval(t->data);
	}
	ci_base = ci_id_at(0);
	if (!parts_multi_controller || ci_len() != 1 || ci_active() != ci_base || ci_base < 0) {
		printf("CONTROLLER-ID setup: multi=%d layers=%d first=%d active=%d\n",
		       parts_multi_controller, ci_len(), ci_id_at(0), ci_active());
		exit(2);
	}
}

/* The layers bottom to top are exactly want[0..n-1] (GetLayerID alone, which
 * the guards use: GetControllerIndex is new). */
static bool ci_ids(const char *step, int n, const int *want)
{
	int len = ci_len();
	bool ok = len == n;
	for (int i = 0; ok && i < n; i++)
		ok = ci_id_at(i) == want[i] && ci_exists(want[i]);
	if (!ok) {
		printf("  FAIL: %s: layers [", step);
		for (int i = 0; i < len && i < 12; i++)
			printf("%s%d", i ? " " : "", ci_id_at(i));
		printf("], want [");
		for (int i = 0; i < n; i++)
			printf("%s%d", i ? " " : "", want[i]);
		printf("]\n");
		ci_failures++;
	}
	return ok;
}

static bool ci_layers(const char *step, int n, const int *want)
{
	if (!ci_ids(step, n, want))
		return false;
	for (int i = 0; i < n; i++) {
		int index = ci_index_of(want[i]);
		CI_CHECK(index == i, "%s: GetLayerIndex(%d) is %d, want %d", step, want[i], index, i);
	}
	return true;
}

/* A rectangle created now, so on the active layer. */
static struct parts *ci_part(int no)
{
	return br_box(no, 100, 100, 200, 80, true);
}

static int ci_owner(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? p->controller_no : -999;
}

static int ci_z(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? p->sp.z : -999;
}

/* The given parts appear in the parts list (drawing order, back to front) in
 * exactly this order. */
static bool ci_drawn(const char *step, int n, const int *nos)
{
	int k = 0;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		for (int i = 0; i < n; i++) {
			if (p->no != nos[i])
				continue;
			if (i != k) {
				printf("  FAIL: %s: parts %d is drawn at place %d, want %d\n", step, p->no, k, i);
				ci_failures++;
				return false;
			}
			k++;
		}
	}
	CI_CHECK(k == n, "%s: %d of %d parts in the list", step, k, n);
	return k == n;
}

enum { CI_P0 = 19600, CI_PA, CI_PB, CI_PC, CI_PX, CI_PO };

/* --- CI1 --- */

static void ci_add_top_layer(void)
{
	ci_setup();
	int id = ci_add_top();
	CI_CHECK(ci_len() == 2, "%d layers after AFL_Parts_AddTopLayer, want 2", ci_len());
	CI_CHECK(id != 0 && id == ci_active() && id == ci_id_at(1),
		 "AddTopLayer returned %d; active %d, top %d", id, ci_active(), ci_id_at(1));
	ci_part(CI_PA);
	CI_CHECK(ci_owner(CI_PA) == id, "a new parts belongs to layer %d, want %d", ci_owner(CI_PA), id);
	printf("CI1 AFL_Parts_AddTopLayer: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI2 / CI3 --- */

static int ci_layer_struct(void)
{
	int st = ain_get_struct(ain, "CASPartsLayer");
	if (st < 0) { printf("CONTROLLER-ID struct CASPartsLayer missing\n"); exit(2); }
	return st;
}

static int ci_layer_id(int obj)
{
	return heap[obj].page->values[br_member(ci_layer_struct(), "<LayerID>")].i;
}

static int ci_layer_prev(int obj)
{
	return heap[obj].page->values[br_member(ci_layer_struct(), "prevLayerID")].i;
}

static void ci_parts_layer(void)
{
	ci_setup();
	int st = ci_layer_struct();
	CI_CHECK(ain->structures[st].constructor == ci_fn("CASPartsLayer@0")
		 && ain->structures[st].destructor == ci_fn("CASPartsLayer@1"),
		 "CASPartsLayer constructor/destructor are %d/%d", ain->structures[st].constructor,
		 ain->structures[st].destructor);
	ci_part(CI_P0);
	int layer = vm_construct_struct(st);	/* a CASPartsLayer local */
	int id = ci_layer_id(layer);
	/* CASPartsLayer@Add keeps AFL_Parts_GetActiveLayer() as prevLayerID: the default layer's ID. */
	CI_CHECK(ci_len() == 2 && id > 0 && id == ci_active() && ci_layer_prev(layer) == CI_ID(0),
		 "after the constructor: %d layers, LayerID %d, active %d, prevLayerID %d; want 2, the new layer twice, %d",
		 ci_len(), id, ci_active(), ci_layer_prev(layer), CI_ID(0));
	ci_part(CI_PA);
	CI_CHECK(ci_owner(CI_PA) == id && ci_owner(CI_P0) == CI_ID(0), "parts owners %d %d, want %d %d",
		 ci_owner(CI_PA), ci_owner(CI_P0), id, CI_ID(0));
	heap_unref(layer);			/* the local goes out of scope */
	CI_CHECK(ci_len() == 1 && ci_active() == CI_ID(0), "after the destructor: %d layers, active %d; want 1, %d",
		 ci_len(), ci_active(), CI_ID(0));
	CI_CHECK(!parts_try_get(CI_PA) && parts_try_get(CI_P0), "the layer's parts %s, the default layer's %s",
		 parts_try_get(CI_PA) ? "stayed" : "went", parts_try_get(CI_P0) ? "stayed" : "went");
	printf("CI2 CASPartsLayer: %s\n", ci_failures ? "FAIL" : "ok");
}

static void ci_backlog_shape(void)
{
	ci_setup();
	int st = ci_layer_struct();
	/* SceneAzito and SceneHome: SceneContext@0 calls AFL_Parts_AddLayer(-1). */
	int azito = ci_add(-1), home = ci_add(-1);
	ci_layers("two scenes", 3, (int[]){ CI_ID(0), azito, home });
	ci_part(CI_PA);
	/* CBackLogView@Execute's layer, then CActivity@Run's. */
	int outer = vm_construct_struct(st);
	int oid = ci_layer_id(outer);
	ci_layers("outer layer", 4, (int[]){ CI_ID(0), azito, home, oid });
	CI_CHECK(ci_active() == oid && ci_layer_prev(outer) == home, "outer: active %d, prev %d; want %d, %d",
		 ci_active(), ci_layer_prev(outer), oid, home);
	int inner = vm_construct_struct(st);
	int iid = ci_layer_id(inner);
	ci_layers("inner layer", 5, (int[]){ CI_ID(0), azito, home, oid, iid });
	CI_CHECK(ci_active() == iid && ci_layer_prev(inner) == oid, "inner: active %d, prev %d; want %d, %d",
		 ci_active(), ci_layer_prev(inner), iid, oid);
	ci_part(CI_PB);
	heap_unref(inner);
	ci_layers("inner gone", 4, (int[]){ CI_ID(0), azito, home, oid });
	CI_CHECK(ci_active() == oid && !parts_try_get(CI_PB), "inner gone: active %d (want %d), its parts %s",
		 ci_active(), oid, parts_try_get(CI_PB) ? "stayed" : "went");
	heap_unref(outer);
	ci_layers("both gone", 3, (int[]){ CI_ID(0), azito, home });
	CI_CHECK(ci_active() == home && ci_owner(CI_PA) == home, "both gone: active %d, the scene's parts on %d; want %d",
		 ci_active(), ci_owner(CI_PA), home);
	printf("CI3 backlog shape: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI4 / CI5 --- */

/* [default, A, C, B] with one parts each, C inserted last; active C. */
static void ci_inserted(int *a, int *b, int *c)
{
	ci_part(CI_P0);
	*a = ci_add(-1);
	ci_part(CI_PA);
	*b = ci_add(-1);
	ci_part(CI_PB);
	*c = ci_add(*a);
	ci_part(CI_PC);
}

static void ci_insert_middle(void)
{
	ci_setup();
	int a, b, c;
	ci_inserted(&a, &b, &c);
	CI_CHECK(a != b && b != c && a != c && c > 0, "layer IDs %d %d %d are not distinct", a, b, c);
	ci_layers("AddLayer(A)", 4, (int[]){ CI_ID(0), a, c, b });
	CI_CHECK(ci_active() == c, "active %d, want the new layer %d", ci_active(), c);
	CI_CHECK(ci_owner(CI_P0) == CI_ID(0) && ci_owner(CI_PA) == a && ci_owner(CI_PB) == b && ci_owner(CI_PC) == c,
		 "parts owners %d %d %d %d, want %d %d %d %d", ci_owner(CI_P0), ci_owner(CI_PA), ci_owner(CI_PB),
		 ci_owner(CI_PC), CI_ID(0), a, b, c);
	ci_drawn("AddLayer(A)", 4, (int[]){ CI_P0, CI_PA, CI_PC, CI_PB });
	CI_CHECK(ci_z(CI_P0) == 0 && ci_z(CI_PA) == 1 && ci_z(CI_PC) == 2 && ci_z(CI_PB) == 3,
		 "sprite layers %d %d %d %d, want 0 1 2 3 (default, A, C, B)", ci_z(CI_P0), ci_z(CI_PA),
		 ci_z(CI_PC), ci_z(CI_PB));
	/* the same ID again: directly above A once more, below the first one */
	int d = ci_add(a);
	ci_layers("AddLayer(A) twice", 5, (int[]){ CI_ID(0), a, d, c, b });
	printf("CI4 insert in the middle: %s\n", ci_failures ? "FAIL" : "ok");
}

static void ci_erase_middle(void)
{
	ci_setup();
	int a, b, c;
	ci_inserted(&a, &b, &c);
	ci_set_active(b);
	ci_erase(c);				/* neither the top nor the active layer */
	ci_layers("EraseLayer(C)", 3, (int[]){ CI_ID(0), a, b });
	CI_CHECK(ci_active() == b, "active %d after erasing another layer, want %d", ci_active(), b);
	CI_CHECK(!parts_try_get(CI_PC) && ci_owner(CI_PA) == a && ci_owner(CI_PB) == b,
		 "C's parts %s; owners of A's and B's parts %d %d, want %d %d",
		 parts_try_get(CI_PC) ? "stayed" : "went", ci_owner(CI_PA), ci_owner(CI_PB), a, b);
	ci_drawn("EraseLayer(C)", 3, (int[]){ CI_P0, CI_PA, CI_PB });
	CI_CHECK(ci_z(CI_PA) == 1 && ci_z(CI_PB) == 2, "sprite layers %d %d, want 1 2", ci_z(CI_PA), ci_z(CI_PB));
	ci_erase(b);				/* the ID the script kept is still B's */
	ci_layers("EraseLayer(B)", 2, (int[]){ CI_ID(0), a });
	CI_CHECK(!parts_try_get(CI_PB) && parts_try_get(CI_PA) && parts_try_get(CI_P0),
		 "after EraseLayer(B): B's parts %s, A's %s", parts_try_get(CI_PB) ? "stayed" : "went",
		 parts_try_get(CI_PA) ? "stayed" : "went");
	CI_CHECK(ci_active() == a, "active %d after erasing the active top layer, want %d", ci_active(), a);
	printf("CI5 erase a middle layer: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI6 --- */

static void ci_erase_active(void)
{
	ci_setup();
	int a = ci_add(-1), b = ci_add(-1);
	ci_set_active(a);
	ci_erase(a);
	ci_layers("erase the active middle layer", 2, (int[]){ CI_ID(0), b });
	CI_CHECK(ci_active() == CI_ID(0), "active %d, want the layer below (%d)", ci_active(), CI_ID(0));
	/* the bottom layer is active and erased: the new bottom acts (max(pos-1, 0)) */
	int c = ci_add(b);
	ci_set_active(CI_ID(0));
	ci_erase(-1);
	ci_layers("erase the active bottom layer", 2, (int[]){ b, c });
	CI_CHECK(ci_active() == b, "active %d, want the new bottom layer %d", ci_active(), b);
	printf("CI6 erase the active layer: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI7 --- */

static void ci_add_above_active(void)
{
	ci_setup();
	int a = ci_add(-1), b = ci_add(-1);
	ci_part(CI_PB);
	ci_set_active(a);
	int x = ci_add(-1);
	ci_layers("AddLayer(-1) with A active", 4, (int[]){ CI_ID(0), a, x, b });
	CI_CHECK(ci_active() == x, "active %d, want the new layer %d", ci_active(), x);
	ci_part(CI_PX);
	ci_drawn("AddLayer(-1) with A active", 2, (int[]){ CI_PX, CI_PB });
	printf("CI7 AddLayer(-1) above the active layer: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI8 --- */

static void ci_active_missing(void)
{
	ci_setup();
	int a = ci_add(-1), b = ci_add(-1);
	ci_erase(b);
	ci_set_active(a);
	/* parts::detail::CallPartsUpdateEvent ends with SetActiveLayer(pre); an
	 * update event may have erased that layer. */
	int prev = ci_set_active(b);
	CI_CHECK(prev == a, "SetActiveLayer returned the previous layer %d, want %d", prev, a);
	CI_CHECK(ci_active() == a, "active %d with none designated, want the top layer %d", ci_active(), a);
	int c = ci_add(CI_ID(0));			/* [0, c, a]: the top layer is still a */
	ci_set_active(777);
	CI_CHECK(ci_active() == a, "active %d with none designated, want the top layer %d", ci_active(), a);
	ci_part(CI_PA);
	CI_CHECK(ci_owner(CI_PA) == a, "a new parts belongs to layer %d, want the top layer %d", ci_owner(CI_PA), a);
	CI_CHECK(BU_PE("GetControllerID", BU_I(-1)).i == a, "GetControllerID(-1) is %d, want the top layer %d",
		 BU_PE("GetControllerID", BU_I(-1)).i, a);
	int d = ci_add(-1);			/* above the top layer */
	ci_layers("AddLayer(-1) with none designated", 4, (int[]){ CI_ID(0), c, a, d });
	ci_set_active(777);
	ci_erase(-1);				/* the top one */
	ci_layers("EraseLayer(-1) with none designated", 3, (int[]){ CI_ID(0), c, a });
	printf("CI8 SetActiveLayer with a missing ID: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI9 --- */

static void ci_erase_edges(void)
{
	ci_setup();
	ci_part(CI_P0);
	int a = ci_add(-1), b = ci_add(-1);
	ci_part(CI_PB);
	ci_set_active(a);
	ci_part(CI_PA);
	ci_erase(555);				/* no such layer: the active one goes */
	ci_layers("EraseLayer(missing)", 2, (int[]){ CI_ID(0), b });
	CI_CHECK(!parts_try_get(CI_PA) && parts_try_get(CI_PB), "the active layer's parts %s, the other layer's %s",
		 parts_try_get(CI_PA) ? "stayed" : "went", parts_try_get(CI_PB) ? "stayed" : "went");
	/* -1 with none designated: the top one */
	int c = ci_add(0), list = tr_int_array();
	ci_part(CI_PC);
	ci_set_active(555);
	((void (*)(int, int))br_pe_fn("RemoveController"))(list, -1);
	ci_layers("RemoveController(-1) with none designated", 2, (int[]){ CI_ID(0), c });
	CI_CHECK(!parts_try_get(CI_PB) && parts_try_get(CI_PC) && ci_active() == c,
		 "RemoveController(-1) with none designated: the top layer's parts %s, active %d (want %d)",
		 parts_try_get(CI_PB) ? "stayed" : "went", ci_active(), c);
	ci_erase(c);
	/* the last layer is never removed */
	list = tr_int_array();
	((void (*)(int, int))br_pe_fn("RemoveController"))(list, CI_ID(0));
	CI_CHECK(ci_len() == 1 && ci_id_at(0) == CI_ID(0) && ci_active() == CI_ID(0), "the last layer: %d layers, first %d, active %d",
		 ci_len(), ci_id_at(0), ci_active());
	CI_CHECK(parts_try_get(CI_P0) && heap[list].page && heap[list].page->nr_vars == 0,
		 "removing the last layer: its parts %s, %d list entries; want stayed, 0",
		 parts_try_get(CI_P0) ? "stayed" : "went", heap[list].page ? heap[list].page->nr_vars : -1);
	ci_erase(-1);
	CI_CHECK(ci_len() == 1 && parts_try_get(CI_P0), "EraseLayer(-1) on the last layer: %d layers", ci_len());
	printf("CI9 EraseLayer edges: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI10 --- */

static int ci_id(int index) { return BU_PE("GetControllerID", BU_I(index)).i; }
static int ci_index(int id) { return BU_PE("GetControllerIndex", BU_I(id)).i; }

static void ci_queries(void)
{
	ci_setup();
	ci_part(CI_P0);
	int a = ci_add(-1);
	ci_part(CI_PA);
	int b = ci_add(-1);
	ci_part(CI_PB);
	ci_set_active(a);
	CI_CHECK(ci_id(-1) == a && ci_id(-7) == a, "GetControllerID(negative) is %d, want the active layer %d", ci_id(-1), a);
	CI_CHECK(ci_id(99) == b && ci_id(3) == b, "GetControllerID(past the end) is %d, want the top layer %d", ci_id(99), b);
	CI_CHECK(ci_id(0) == CI_ID(0) && ci_id(1) == a && ci_id(2) == b, "GetControllerID(0..2) is %d %d %d", ci_id(0), ci_id(1), ci_id(2));
	CI_CHECK(ci_index(424242) == -1 && ci_index(-1) == -1 && ci_index(CI_OVERLAY) == -1,
		 "GetControllerIndex of a missing ID, -1, the overlay: %d %d %d; want -1", ci_index(424242),
		 ci_index(-1), ci_index(CI_OVERLAY));
	CI_CHECK(ci_index(CI_ID(0)) == 0 && ci_index(a) == 1 && ci_index(b) == 2, "GetControllerIndex: %d %d %d",
		 ci_index(CI_ID(0)), ci_index(a), ci_index(b));
	ci_move(b, 1);				/* [0, b, a] */
	ci_layers("MoveLayer(B, 1)", 3, (int[]){ CI_ID(0), b, a });
	CI_CHECK(ci_active() == a, "active %d after the move, want %d", ci_active(), a);
	CI_CHECK(ci_owner(CI_PA) == a && ci_owner(CI_PB) == b, "owners after the move %d %d", ci_owner(CI_PA), ci_owner(CI_PB));
	ci_drawn("MoveLayer(B, 1)", 3, (int[]){ CI_P0, CI_PB, CI_PA });
	CI_CHECK(ci_z(CI_PB) == 1 && ci_z(CI_PA) == 2, "sprite layers %d %d, want 1 2", ci_z(CI_PB), ci_z(CI_PA));
	ci_move(b, 1);				/* already there */
	ci_move(b, 2);				/* the place before its own removal: stays */
	ci_move(31337, 0);			/* no such layer */
	ci_layers("moves that change nothing", 3, (int[]){ CI_ID(0), b, a });
	ci_move(CI_ID(0), 99);				/* clamped to the length: on top */
	ci_layers("MoveLayer(0, 99)", 3, (int[]){ b, a, CI_ID(0) });
	ci_drawn("MoveLayer(0, 99)", 3, (int[]){ CI_PB, CI_PA, CI_P0 });
	ci_move(CI_ID(0), -5);				/* clamped to 0 */
	ci_layers("MoveLayer(0, -5)", 3, (int[]){ CI_ID(0), b, a });
	ci_move(CI_ID(0), 2);				/* [b, 0, a] */
	ci_layers("MoveLayer(0, 2)", 3, (int[]){ b, CI_ID(0), a });
	ci_drawn("MoveLayer(0, 2)", 3, (int[]){ CI_PB, CI_P0, CI_PA });
	printf("CI10 queries and MoveLayer: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI11 --- */

static int ci_words(struct page *buffer)
{
	return buffer ? buffer->nr_vars : -1;
}

/* Words of the controller ID section at the end of a save of `nr` layers
 * ("PCID", count, IDs, active), 0 when the save has none. */
static int ci_id_section(struct page *saved, int nr)
{
	int len = nr + 3;
	return saved && saved->nr_vars > len && saved->values[saved->nr_vars - len].i == 0x44494350
		&& saved->values[saved->nr_vars - len + 1].i == nr ? len : 0;
}

/* Rewrites a version 8 save of `nr` layers as the version 7 writer wrote it:
 * version 8 only added the list of the parts in 減算色モード, between the
 * hidden list and the ID section (parts-transform PT4 pins a non-empty
 * one). Here it must be empty, so its count word goes and the section moves
 * up. Returns the number of words removed (the page keeps its allocation;
 * add it back to nr_vars before freeing). */
static int ci_to_v7(struct page *saved, int nr)
{
	struct iarray_reader r;
	if (!saved || !iarray_init_reader(&r, saved, "XPE"))
		return 0;
	int section = ci_id_section(saved, nr), list = saved->nr_vars - section - 1;
	if (saved->values[r.pos].i != 8 || list <= (int)r.pos || saved->values[list].i != 0)
		return 0;
	memmove(saved->values + list, saved->values + list + 1, section * sizeof(union vm_value));
	saved->values[r.pos].i = 7;
	saved->nr_vars--;
	return 1;
}

/* Is any of the four parts of ci_inserted, or any layer's root, in
 * 減算色モード? (Through the AIN declaration: the flag is new.) */
static bool ci_any_sub(void)
{
	int nos[4 + 16] = { CI_P0, CI_PA, CI_PB, CI_PC }, n = 4, len = ci_len();
	for (int i = 0; i < len && i < 16; i++)
		nos[n++] = ci_id_at(i);
	for (int i = 0; i < n; i++) {
		if (parts_try_get(nos[i])
				&& ri_hll("IsComponentSubColorMode", 1, (union vm_value[]){ RI_I(nos[i]) }).i == 1)
			return true;
	}
	return false;
}

static void ci_save_load(void)
{
	ci_setup();
	int a, b, c;
	ci_inserted(&a, &b, &c);		/* [0, a, c, b], active c */
	struct page *saved = NULL;
	CI_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	CI_CHECK(ca_save_version(saved) == 8, "XPE v%d, want 8 (the ID section does not change the version)", ca_save_version(saved));
	/* something else in between */
	ci_erase(c);
	ci_erase(b);
	ci_erase(a);
	ci_part(CI_PX);
	CI_CHECK(PE_Load(&saved), "PE_Load failed");
	ci_layers("after PE_Load", 4, (int[]){ CI_ID(0), a, c, b });
	CI_CHECK(ci_active() == c, "active %d after PE_Load, want %d", ci_active(), c);
	CI_CHECK(ci_owner(CI_P0) == CI_ID(0) && ci_owner(CI_PA) == a && ci_owner(CI_PB) == b && ci_owner(CI_PC) == c
		 && !parts_try_get(CI_PX), "owners after PE_Load %d %d %d %d, want %d %d %d %d",
		 ci_owner(CI_P0), ci_owner(CI_PA), ci_owner(CI_PB), ci_owner(CI_PC), CI_ID(0), a, b, c);
	ci_drawn("after PE_Load", 4, (int[]){ CI_P0, CI_PA, CI_PC, CI_PB });
	CI_CHECK(ci_z(CI_PA) == 1 && ci_z(CI_PC) == 2 && ci_z(CI_PB) == 3, "sprite layers after PE_Load %d %d %d",
		 ci_z(CI_PA), ci_z(CI_PC), ci_z(CI_PB));
	/* the IDs the script kept still work */
	ci_erase(c);
	ci_layers("EraseLayer(C) after PE_Load", 3, (int[]){ CI_ID(0), a, b });
	CI_CHECK(!parts_try_get(CI_PC) && parts_try_get(CI_PB), "EraseLayer(C) after PE_Load released the wrong parts");

	/* A save as it was written before (no ID section: positions only) gives
	 * every layer the ID of its position. The section is the save's tail;
	 * the parts and the hidden list before it are what an old save has. */
	int n = 4, tail = 1 + 1 + n + 1;
	struct page *old = copy_page(saved);
	old->nr_vars -= tail;
	CI_CHECK(PE_Load(&old), "PE_Load of a save without the section failed");
	ci_layers("old save", 4, (int[]){ CI_ID(0), CI_ID(1), CI_ID(2), CI_ID(3) });
	CI_CHECK(ci_active() == CI_ID(2), "active %d from an old save, want position 2's %d", ci_active(), CI_ID(2));
	CI_CHECK(ci_owner(CI_P0) == CI_ID(0) && ci_owner(CI_PA) == CI_ID(1) && ci_owner(CI_PC) == CI_ID(2) && ci_owner(CI_PB) == CI_ID(3),
		 "owners from an old save %d %d %d %d, want the positions' IDs", ci_owner(CI_P0),
		 ci_owner(CI_PA), ci_owner(CI_PC), ci_owner(CI_PB));
	ci_drawn("old save", 4, (int[]){ CI_P0, CI_PA, CI_PC, CI_PB });
	old->nr_vars += tail;
	ca_free_buffer(old);

	/* The same two saves as version 7 wrote them (no list of sub colour
	 * parts): with the section the layers keep their IDs, without it they
	 * get the positions'. */
	struct page *v7 = copy_page(saved);
	int cut = ci_to_v7(v7, n);
	CI_CHECK(cut == 1 && ca_save_version(v7) == 7 && ci_id_section(v7, n) == tail,
		 "could not rewrite the save as version 7 (cut %d, version %d, section %d)", cut, ca_save_version(v7), ci_id_section(v7, n));
	ci_part(CI_PX);
	CI_CHECK(PE_Load(&v7), "PE_Load of a version 7 save with the section failed");
	ci_layers("version 7 save with the section", 4, (int[]){ CI_ID(0), a, c, b });
	CI_CHECK(ci_active() == c && ci_owner(CI_P0) == CI_ID(0) && ci_owner(CI_PA) == a && ci_owner(CI_PB) == b
		 && ci_owner(CI_PC) == c && !parts_try_get(CI_PX),
		 "version 7 save with the section: active %d, owners %d %d %d %d; want %d, %d %d %d %d", ci_active(),
		 ci_owner(CI_P0), ci_owner(CI_PA), ci_owner(CI_PB), ci_owner(CI_PC), c, CI_ID(0), a, b, c);
	ci_drawn("version 7 save with the section", 4, (int[]){ CI_P0, CI_PA, CI_PC, CI_PB });
	/* It has no list of sub colour parts: the section's words are not read as one. */
	CI_CHECK(!ci_any_sub(), "a parts or a root is in 減算色モード after loading a version 7 save with the section");
	v7->nr_vars -= tail;
	CI_CHECK(PE_Load(&v7), "PE_Load of a version 7 save without the section failed");
	ci_layers("version 7 save without the section", 4, (int[]){ CI_ID(0), CI_ID(1), CI_ID(2), CI_ID(3) });
	CI_CHECK(ci_active() == CI_ID(2) && ci_owner(CI_P0) == CI_ID(0) && ci_owner(CI_PA) == CI_ID(1)
		 && ci_owner(CI_PC) == CI_ID(2) && ci_owner(CI_PB) == CI_ID(3),
		 "version 7 save without the section: active %d, owners %d %d %d %d; want the positions' IDs", ci_active(),
		 ci_owner(CI_P0), ci_owner(CI_PA), ci_owner(CI_PC), ci_owner(CI_PB));
	ci_drawn("version 7 save without the section", 4, (int[]){ CI_P0, CI_PA, CI_PC, CI_PB });
	CI_CHECK(!ci_any_sub(), "a parts or a root is in 減算色モード after loading a version 7 save without the section");
	v7->nr_vars += tail + cut;
	ca_free_buffer(v7);
	ca_free_buffer(saved);

	/* none designated survives a save as well */
	ci_set_active(4711);
	saved = NULL;
	CI_CHECK(PE_Save(&saved) && PE_Load(&saved), "second save and load failed");
	int len = ci_len(), top = ci_id_at(len - 1), below = ci_id_at(len - 2);
	CI_CHECK(ci_active() == top, "active %d after loading with none designated, want the top layer %d", ci_active(), top);
	ci_move(top, 0);			/* still none designated: the new top layer acts */
	CI_CHECK(ci_active() == below, "active %d after moving the top layer away, want the new top layer %d",
		 ci_active(), below);
	/* without its section that save names the top position as the active one */
	saved->nr_vars -= 1 + 1 + len + 1;
	CI_CHECK(PE_Load(&saved), "PE_Load of the second save without the section failed");
	CI_CHECK(ci_len() == len && ci_active() == CI_ID(len - 1), "old save with none designated: %d layers, active %d; want %d, %d",
		 ci_len(), ci_active(), len, CI_ID(len - 1));
	ci_move(CI_ID(len - 1), 0);
	CI_CHECK(ci_active() == CI_ID(len - 1), "active %d after moving the designated layer, want it to stay %d", ci_active(), CI_ID(len - 1));
	saved->nr_vars += 1 + 1 + len + 1;
	ca_free_buffer(saved);
	printf("CI11 save and load: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI12 --- */

static void ci_hit_order(void)
{
	ci_setup();
	int type, no;
	int a = ci_add(-1);
	ci_part(CI_PA);
	int b = ci_add(-1);
	ci_part(CI_PB);				/* the same rectangle, a layer above */
	int n = br_click(150, 140, &type, &no);
	CI_CHECK(n == 1 && type == 4 && no == CI_PB, "click with B on top: %d messages, type %d parts %d; want 1, 4, %d",
		 n, type, no, CI_PB);
	ci_move(a, 99);
	n = br_click(150, 140, &type, &no);
	CI_CHECK(n == 1 && type == 4 && no == CI_PA, "click after moving A on top: %d messages, type %d parts %d; want 1, 4, %d",
		 n, type, no, CI_PA);
	int c = ci_add(CI_ID(0));			/* [0, c, b, a] */
	ci_part(CI_PC);
	n = br_click(150, 140, &type, &no);
	CI_CHECK(n == 1 && type == 4 && no == CI_PA, "click with a new layer at the bottom: %d messages, type %d parts %d; want 1, 4, %d",
		 n, type, no, CI_PA);
	ci_erase(a);
	ci_erase(b);
	n = br_click(150, 140, &type, &no);
	CI_CHECK(n == 1 && type == 4 && no == CI_PC, "click with only the inserted layer left: %d messages, type %d parts %d; want 1, 4, %d",
		 n, type, no, CI_PC);
	(void)c;
	printf("CI12 hit order: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI13 --- */

static void ci_overlay(void)
{
	ci_setup();
	int ov = ci_call("parts::detail::GetSystemOverlayLayer", 0, NULL);
	CI_CHECK(ov == CI_OVERLAY, "the system overlay layer is %d, want %d", ov, CI_OVERLAY);
	ci_part(CI_P0);
	int a = ci_add(-1);
	ci_part(CI_PA);
	int prev = ci_set_active(ov);
	CI_CHECK(prev == a && ci_active() == ov, "SetActiveLayer(overlay): previous %d, active %d; want %d, %d",
		 prev, ci_active(), a, ov);
	ci_part(CI_PO);
	CI_CHECK(ci_owner(CI_PO) == ov && ci_z(CI_PO) == CI_OVERLAY, "an overlay parts: layer %d, sprite layer %d; want %d twice",
		 ci_owner(CI_PO), ci_z(CI_PO), ov);
	CI_CHECK(ci_len() == 2, "%d layers, want 2 (the overlay is not in the stack)", ci_len());
	int b = ci_add(-1);			/* the overlay is not in the stack: on top */
	ci_layers("AddLayer(-1) with the overlay active", 3, (int[]){ CI_ID(0), a, b });
	CI_CHECK(ci_active() == b, "active %d, want the new layer %d", ci_active(), b);
	ci_part(CI_PB);
	ci_drawn("overlay above every layer", 4, (int[]){ CI_P0, CI_PA, CI_PB, CI_PO });
	ci_erase(b);
	ci_erase(a);
	CI_CHECK(parts_try_get(CI_PO) && ci_owner(CI_PO) == ov && ci_z(CI_PO) == CI_OVERLAY,
		 "the overlay parts after erasing every layer: %s, layer %d", parts_try_get(CI_PO) ? "there" : "gone",
		 ci_owner(CI_PO));
	ci_layers("layers erased", 1, (int[]){ CI_ID(0) });
	printf("CI13 system overlay: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI14 (guard) --- */

static unsigned ci_hash(struct page *buffer)
{
	unsigned h = 2166136261u;
	for (int i = 0; buffer && i < buffer->nr_vars; i++) {
		h ^= (unsigned)buffer->values[i].i;
		h *= 16777619u;
	}
	return h;
}

static void ci_push_pop(void)
{
	ci_setup();
	ci_part(CI_P0);
	int a = ci_add(-1);
	ci_part(CI_PA);
	int b = ci_add(-1);
	ci_part(CI_PB);
	int c = ci_add(-1);
	CI_CHECK(a == CI_ID(1) && b == CI_ID(2) && c == CI_ID(3), "AddLayer(-1) three times returned %d %d %d, want the IDs of positions 1 2 3", a, b, c);
	ci_ids("three pushes", 4, (int[]){ CI_ID(0), CI_ID(1), CI_ID(2), CI_ID(3) });
	CI_CHECK(ci_active() == CI_ID(3), "active %d, want %d", ci_active(), CI_ID(3));
	CI_CHECK(ci_z(CI_P0) == 0 && ci_z(CI_PA) == 1 && ci_z(CI_PB) == 2, "sprite layers %d %d %d, want 0 1 2",
		 ci_z(CI_P0), ci_z(CI_PA), ci_z(CI_PB));
	struct page *saved = NULL;
	CI_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	/* header 4, version, no numeral fonts, active, layers, parts count */
	CI_CHECK(ca_save_version(saved) == 8 && saved->values[6].i == 3 && saved->values[7].i == 4
		 && saved->values[8].i == 3, "save header: version %d, active %d, layers %d, parts %d; want 8 3 4 3",
		 ca_save_version(saved), saved->values[6].i, saved->values[7].i, saved->values[8].i);
	/* The words before the ID section are what a save of this stack always
	 * was, ending with the (empty) hidden list, and since version 8 the
	 * (empty) list of sub colour parts after it. (Since a layer's ID is its
	 * root's number every v14 save has the section; layer-root LR8 pins
	 * that. Before, a pushed-only stack had none.) The words and the hash
	 * printed are those of the version 7 form, which is what they were
	 * before the IDs and before version 8. */
	int section = ci_id_section(saved, 4);
	saved->nr_vars -= section;
	CI_CHECK(saved->values[saved->nr_vars - 1].i == 0 && saved->values[saved->nr_vars - 2].i == 0,
		 "the save of a pushed-only stack ends with %d %d before its ID section, want the 0 of the hidden list and of the sub colour list",
		 saved->values[saved->nr_vars - 2].i, saved->values[saved->nr_vars - 1].i);
	printf("  CI14 pushed-only save (version 8): %d words, hash %08x\n", ci_words(saved), ci_hash(saved));
	saved->nr_vars += section;
	struct page *v7 = copy_page(saved);
	int cut = ci_to_v7(v7, 4);
	CI_CHECK(cut == 1, "could not rewrite the pushed-only save as version 7");
	v7->nr_vars -= section;
	printf("  CI14 pushed-only save: %d words, hash %08x\n", ci_words(v7), ci_hash(v7));
	CI_CHECK(ci_words(v7) == 337 && ci_hash(v7) == 0x70d03dcau,
		 "the version 7 form of the pushed-only save is %d words, hash %08x; it was 337, 70d03dca",
		 ci_words(v7), ci_hash(v7));
	v7->nr_vars += section + cut;
	ca_free_buffer(v7);
	ci_erase(c);
	CI_CHECK(ci_active() == CI_ID(2), "active %d after popping, want %d", ci_active(), CI_ID(2));
	ci_erase(-1);
	ci_ids("two pops", 2, (int[]){ CI_ID(0), CI_ID(1) });
	CI_CHECK(ci_active() == CI_ID(1) && !parts_try_get(CI_PB) && parts_try_get(CI_PA), "after two pops: active %d", ci_active());
	CI_CHECK(ci_add(-1) == CI_ID(2), "the next layer is not position 2's ID");
	CI_CHECK(PE_Load(&saved), "PE_Load failed");
	ci_ids("loaded", 4, (int[]){ CI_ID(0), CI_ID(1), CI_ID(2), CI_ID(3) });
	CI_CHECK(ci_active() == CI_ID(3) && ci_owner(CI_PB) == CI_ID(2), "loaded: active %d, B's parts on %d; want %d, %d",
		 ci_active(), ci_owner(CI_PB), CI_ID(3), CI_ID(2));
	ca_free_buffer(saved);
	printf("CI14 push and pop as before: %s\n", ci_failures ? "FAIL" : "ok");
}

/* --- CI15 (guard) --- */

/* Runs fn in a child and returns whether it ended in a VM error naming `text`. */
static bool ci_dies(void (*fn)(void), const char *text)
{
	int fd[2];
	if (pipe(fd))
		return false;
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		close(fd[0]);
		/* the error report names the current instruction */
		instr_ptr = ain->functions[ci_fn("AFL_Parts_AddLayer")].address;
		fn();
		_exit(0);
	}
	close(fd[1]);
	char buf[4096];
	size_t len = 0;
	for (ssize_t n; len < sizeof(buf) - 1 && (n = read(fd[0], buf + len, sizeof(buf) - 1 - len)) > 0;)
		len += n;
	buf[len] = '\0';
	close(fd[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 1 && strstr(buf, text);
}

static void ci_legacy_add_id(void) { PE_AddController(1); }
static void ci_legacy_add_below(void) { PE_set_active_controller(0); PE_AddController(-1); }
static void ci_legacy_active(void) { PE_set_active_controller(7); }
static void ci_legacy_remove(void) { struct page *l = NULL; PE_RemoveController(&l, 9); }

static void ci_legacy(void)
{
	ci_setup();
	ain->version = 13;
	/* The v14 default layer is a root parts number; a v13 engine's is
	 * position 0. Make the stack anew as that engine would. */
	probe_init_parts_table();
	ci_part(CI_P0);
	int a = PE_AddController(-1);
	ci_part(CI_PA);
	int b = PE_AddController(-1);
	ci_part(CI_PB);
	int c = PE_AddController(-1);
	CI_CHECK(a == 1 && b == 2 && c == 3, "AddController(-1) three times returned %d %d %d, want 1 2 3", a, b, c);
	CI_CHECK(PE_get_active_controller() == 3, "active %d, want 3", PE_get_active_controller());
	CI_CHECK(ci_owner(CI_PA) == 1 && ci_owner(CI_PB) == 2 && ci_z(CI_PA) == 1 && ci_z(CI_PB) == 2,
		 "owners %d %d, sprite layers %d %d; want 1 2 1 2", ci_owner(CI_PA), ci_owner(CI_PB), ci_z(CI_PA), ci_z(CI_PB));
	CI_CHECK(ci_dies(ci_legacy_add_id, "index != -1 not supported (got 1)"), "AddController(1) did not stop with its error");
	CI_CHECK(ci_dies(ci_legacy_add_below, "active controller is not at the top of the stack"),
		 "AddController(-1) below the top did not stop with its error");
	CI_CHECK(ci_dies(ci_legacy_active, "Invalid controller number: 7"), "SetActive(7) did not stop with its error");
	CI_CHECK(ci_dies(ci_legacy_remove, "invalid controller index 9"), "RemoveController(9) did not stop with its error");
	struct page *saved = NULL;
	CI_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	CI_CHECK(ca_save_version(saved) == 3 && saved->values[6].i == 3 && saved->values[7].i == 4,
		 "save header: version %d, active %d, layers %d; want 3 3 4", ca_save_version(saved),
		 saved->values[6].i, saved->values[7].i);
	printf("  CI15 legacy save: %d words, hash %08x\n", ci_words(saved), ci_hash(saved));
	/* a position, not an ID: removing the middle one renumbers the one above */
	struct page *list = NULL;
	PE_RemoveController(&list, 1);
	CI_CHECK(!parts_try_get(CI_PA) && ci_owner(CI_PB) == 1 && PE_get_active_controller() == 2,
		 "RemoveController(1): A's parts %s, B's parts on %d, active %d; want went, 1, 2",
		 parts_try_get(CI_PA) ? "stayed" : "went", ci_owner(CI_PB), PE_get_active_controller());
	PE_RemoveController(&list, -1);
	PE_RemoveController(&list, -1);
	CI_CHECK(PE_get_active_controller() == 0 && !parts_try_get(CI_PB) && parts_try_get(CI_P0),
		 "after popping: active %d", PE_get_active_controller());
	/* the last one is removed as well and a new default one takes its place */
	PE_RemoveController(&list, -1);
	CI_CHECK(!parts_try_get(CI_P0) && PE_get_active_controller() == 0 && PE_AddController(-1) == 1,
		 "removing the last controller: its parts %s", parts_try_get(CI_P0) ? "stayed" : "went");
	CI_CHECK(PE_Load(&saved), "PE_Load failed");
	CI_CHECK(PE_get_active_controller() == 3 && ci_owner(CI_PA) == 1 && ci_owner(CI_PB) == 2
		 && PE_AddController(-1) == 4, "loaded: active %d, owners %d %d", PE_get_active_controller(),
		 ci_owner(CI_PA), ci_owner(CI_PB));
	ca_free_buffer(saved);
	if (list) {
		delete_page_vars(list);
		free_page(list);
	}
	ain->version = 14;
	printf("CI15 engines before v14: %s\n", ci_failures ? "FAIL" : "ok");
}

static int ci_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		ci_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(ci_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_controller_id(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "CI1", ci_add_top_layer }, { "CI2", ci_parts_layer }, { "CI3", ci_backlog_shape },
		{ "CI4", ci_insert_middle }, { "CI5", ci_erase_middle }, { "CI6", ci_erase_active },
		{ "CI7", ci_add_above_active }, { "CI8", ci_active_missing }, { "CI9", ci_erase_edges },
		{ "CI10", ci_queries }, { "CI11", ci_save_load }, { "CI12", ci_hit_order },
		{ "CI13", ci_overlay }, { "CI14", ci_push_pop }, { "CI15", ci_legacy },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += ci_fork(cases[i].name, cases[i].fn);
	printf("CONTROLLER-ID %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
