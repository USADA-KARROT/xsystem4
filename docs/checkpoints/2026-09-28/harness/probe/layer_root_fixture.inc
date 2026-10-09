/* layer-root: a v14 controller (the script's "layer") has an ID root parts.
 *
 * The original controller object (0x53d220 -> 0x53bd70) holds two root
 * parts; the one at +4 gives the controller its ID (every lookup compares
 * [[ctrl+4]+0x40]). When the parts table makes a new parts it tells the
 * manager (0x578cb0 -> 0x53d1d0), which hangs the parts under the ID root of
 * the active controller (the top one while none is designated): 0x53afb0
 * takes it out of its old parent's list and writes the root's number to its
 * parent field +0x13c (= [[parts+0x6c]+0xb8], what Parts_GetParentPartsNumber
 * 0x58f110 reads), then 0x550d90 adds the number to the root's child list
 * +0xbc (what NumofChild 0x58f500 and GetChild 0x58f640 read). The roots
 * themselves have parent 0 (the listener is off while 0x53d220 makes them).
 * Parts_SetParentPartsNumber(x, 0) (0x58f060 -> 0x53ab60) puts x back under
 * the root of its controller. RemoveController (0x53d500 -> 0x5386c0) lists
 * the root's descendants, not the root, and the controller's destructor
 * 0x53bea0 releases both roots with the subtree.
 *
 * The script relies on it: AFL_Parts_GetLayerIDByParts walks Parent::get up
 * to 0 and returns the last number; CMessageWindow@OnErasingLayer releases
 * the message window only when that equals EraseLayer's ID;
 * AdvMessageWindow@SearchMessageBoxWindow lists GetChild of the active
 * layer's ID. Before: an activity root had no parent, the walk returned the
 * 9000xx number, the window was never released by the script while
 * RemoveController had released its parts, and the next ADV wrote into a
 * blank parts parts_get had silently made (no CG, text area 0, size 16).
 *
 *   LR1 the default layer's root: IsExist, no parent, no children; a new
 *       parts has the layer as its parent and GetLayerIDByParts; a child
 *       of it walks to the layer; SetParent(0) puts it back under the
 *       root; positions, alpha and show are those of a parts without a
 *       parent, also after SetGlobalPos.
 *   LR2 AddLayer makes a root, EraseLayer releases it with the layer's
 *       parts; a freed ID is taken again (lowest free); a number some
 *       parts took (Wrap of an erased layer's ID) is skipped.
 *   LR3 an activity loaded on a layer: its root walks to the layer; after
 *       EraseLayer its parts and the layer's root are gone and the default
 *       layer is active, but the activity table still has the name
 *       (IsExistActivity true: the original's RemoveController 0x53d500 does
 *       not touch the table 0x58a7a0 looks up), so that the script's
 *       activity::detail::GetFreeName, run here, does not hand the name out
 *       again while its owner has not released it; ReleaseActivity then
 *       succeeds and frees the name.
 *   LR4 real bytecode: CMessageWindow@Create on a layer, GetRootPartsNumber
 *       walks to the layer, AFL_Parts_EraseLayer makes the script release
 *       the window (IsExist false, parts gone); Create on another layer
 *       gives a new root.
 *   LR10 real bytecode: two CMessageWindow on one layer (the ADV scene's
 *       _event and _main), both released by AFL_Parts_EraseLayer. The first
 *       OnErasingLayer removes itself from g_dgPartsErasingLayerEvent while
 *       CallErasingLayerEvent iterates it; the second must still be called
 *       (the iteration cursor follows the erased entry, page.c).
 *   LR5 writing to a released activity parts number re-creates it as
 *       before and says so once (WARNING "stale activity parts").
 *   LR8 PE_Save leaves the roots out and records no parent for a
 *       top-level parts; PE_Load puts the parts back under their layers'
 *       roots. A v14 save always ends with the ID section; without it the
 *       layers get the IDs of their positions and a warning says that the
 *       save's IDs are not known. A plain parts whose number is the ID a
 *       position would have by default comes back as a plain parts.
 *   LR11 a parts hung under a parts of another layer belongs to that layer
 *       with its subtree (0x53afb0 only moves it between child lists; the
 *       layer is the tree it hangs on): GetLayerIDByParts, the drawing
 *       order, EraseLayer of either layer, SetParent(0) (0x53ab60: the root
 *       at the top of its chain).
 * Guards, which pass before the change as well:
 *   LR6 an engine before v14: no roots, a top-level parts has no parent,
 *       IDs are positions.
 *   LR7 the system overlay layer keeps its constant and has no root.
 *   LR9 layers pushed with -1 and popped: IDs in order, the active one on
 *       top, the saved bytes before the ID section end with the hidden list.
 * The activity of LR3-LR5 is a synthetic pactex (a layout box root with one
 * 信息窗口 child named as the script asks) fed to the production loader by
 * a probe-only ReadActivityFile. No game assets or GL are used.
 */

static int lr_failures;
#define LR_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); lr_failures++; } } while (0)
#ifndef PARTS_CONTROLLER_ID_BASE	/* before the change (before-check builds) */
#define PARTS_CONTROLLER_ID_BASE 1000000000
#endif
#define LR_ID(i) (PARTS_CONTROLLER_ID_BASE + (i))
#define LR_MW_TYPE "\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda"       /* 信息窗口 */
#define LR_MW_NAME "SYS_" LR_MW_TYPE                     /* what GetMessageWindowParts asks for */

static int lr_parent(int no) { return BU_PE("Parts_GetParentPartsNumber", BU_I(no)).i; }
static int lr_nchild(int no) { return BU_PE("NumofChild", BU_I(no)).i; }
static int lr_child(int no, int i) { return BU_PE("GetChild", BU_I(no), BU_I(i)).i; }
static bool lr_exists(int no) { return BU_PE("IsExist", BU_I(no)).i; }
static void lr_set_parent(int no, int parent) { BU_PE("Parts_SetParentPartsNumber", BU_I(no), BU_I(parent)); }
static int lr_layer_of(int no) { return ci_call("AFL_Parts_GetLayerIDByParts", 1, (int[]){ no }); }

/* Runs fn with stderr captured into buf. */
static void lr_capture_stderr(void (*fn)(void *), void *arg, char *buf, size_t size)
{
	char path[] = "/tmp/xs4-layer-root-XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0) { printf("LAYER-ROOT mkstemp failed\n"); exit(2); }
	fflush(stderr);
	int saved = dup(2);
	dup2(fd, 2);
	fn(arg);
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	lseek(fd, 0, SEEK_SET);
	ssize_t n = read(fd, buf, size - 1);
	buf[n > 0 ? n : 0] = '\0';
	close(fd);
	unlink(path);
}

/* A layer root: numbered with its own controller's ID (only fields that
 * exist before the change, for the before-check builds). */
static bool lr_is_root(int no)
{
	struct parts *p = parts_try_get(no);
	return p && p->no >= PARTS_CONTROLLER_ID_BASE && p->no == p->controller_no;
}

/* Calls a method with one-slot arguments on an object; returns its one-slot
 * result (0 for void). The struct page goes below the arguments
 * (PUSHSTRUCTPAGE first, as CALLMETHOD expects). */
static int lr_method(const char *name, int obj, int nargs, const int *args)
{
	int fno = ci_fn(name);
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	stack_push(obj);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	method_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	int nret = ain_return_slots_type(&ain->functions[fno].return_type);
	assert(call_stack_ptr == csp && stack_ptr == sp + nret);
	int r = nret ? stack[sp].i : 0;
	stack_ptr = sp;
	return r;
}

/* The probe's ReadActivityFile: a layout box root with one message window
 * child, through the production pactex loader. */
static bool lr_read_activity_file(struct string *name, possibly_unused struct string *filename,
		possibly_unused bool edit)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("LayerRootActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("LayerRootActivity", 1);
	e->blocks[0].val.tree = top;
	/* The CN pactex names its root ルート部件, which CMessageWindow@GetRootParts
	 * asks for by name (the loader's ルートパーツ alias is the JP spelling). */
	top->children[0] = bu_comp("\xa5\xeb\xa9\x60\xa5\xc8\xb2\xbf\xbc\xfe", BU_LAYOUT, 0, 1);
	top->children[0].children[1].children[0] = bu_comp(LR_MW_NAME, LR_MW_TYPE, 0, 0);
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

/* The probe has no EX file: the script's EX_Int/EX_Float/EX_String get their
 * defaults (CMessageWindow@Create reads the window's default alpha, z and
 * CG name through them). */
static bool lr_ex_exists(possibly_unused struct string *name, possibly_unused int id) { return false; }
static bool lr_ex_aexists(possibly_unused struct string *name, possibly_unused int index, possibly_unused int id) { return false; }
static int lr_ex_type(possibly_unused struct string *name, possibly_unused int id) { return 0; }
static int lr_ex_int(possibly_unused struct string *name, int dflt, possibly_unused int id) { return dflt; }
static float lr_ex_float(possibly_unused struct string *name, float dflt, possibly_unused int id) { return dflt; }
static struct string *lr_ex_string(possibly_unused struct string *name, struct string *dflt, possibly_unused int id)
{
	return dflt ? string_ref(dflt) : string_ref(&EMPTY_STRING);
}

/* Replaces a function in the runtime library table (after the links). */
static void lr_replace_hll(const char *libname, const char *fn, void *ptr)
{
	int lib = bu_lib(libname);
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++) {
		if (!strcmp(ain->libraries[lib].functions[j].name, fn)) {
			libraries[lib][j].fun = ptr;
			return;
		}
	}
	printf("LAYER-ROOT %s.%s not declared\n", libname, fn);
	exit(2);
}

static void lr_setup(void)
{
	ci_setup();
	/* bu_postlink registered the activity API into the runtime table; put
	 * the probe's reader in its place. */
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)lr_read_activity_file);
	lr_replace_hll("MainEXFile", "Exists", (void *)lr_ex_exists);
	lr_replace_hll("MainEXFile", "AExists", (void *)lr_ex_aexists);
	lr_replace_hll("MainEXFile", "Type", (void *)lr_ex_type);
	lr_replace_hll("MainEXFile", "Int", (void *)lr_ex_int);
	lr_replace_hll("MainEXFile", "Float", (void *)lr_ex_float);
	lr_replace_hll("MainEXFile", "String", (void *)lr_ex_string);
}

/* Loads the synthetic activity under `name` through the HLL as the script
 * does and returns its root parts number. */
static int lr_load_activity(const char *name)
{
	int n = sf_str(name, -1), f = sf_str("LayerRoot", -1), empty = sf_str("", 0);
	bool ok = BU_PE("ReadActivityFile", BU_I(n), BU_I(f), BU_I(0)).i;
	int root = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(empty)).i;
	LR_CHECK(ok && root >= 900000 && parts_try_get(root), "activity %s: loaded %d, root %d", name, ok, root);
	return root;
}

static bool lr_activity_exists(const char *name)
{
	int n = sf_str(name, -1);
	return BU_PE("IsExistActivity", BU_I(n)).i;
}

/* The script's activity::detail::GetFreeName (real bytecode): the first of
 * name, name:0, name:1... that IsExistActivity does not know. */
static const char *lr_free_name(const char *name)
{
	static char buf[128];
	int r = ci_call("activity::detail::GetFreeName", 1, (int[]){ sf_str(name, -1) });
	struct string *s = heap_get_string(r);
	snprintf(buf, sizeof(buf), "%s", s ? s->text : "(null)");
	heap_unref(r);
	return buf;
}

/* --- LR1 --- */

static void lr_roots(void)
{
	lr_setup();
	int id = ci_active();
	LR_CHECK(id == LR_ID(0), "the default layer's ID is %d, want %d", id, LR_ID(0));
	LR_CHECK(lr_is_root(id), "no root parts numbered %d", id);
	struct parts *root = parts_try_get(id);
	LR_CHECK(root && root->states[PARTS_STATE_DEFAULT].type == PARTS_UNINITIALIZED && !root->want_save
		 && root->global.show && root->global.alpha == 255 && root->global.z == 0,
		 "the root: state type %d, want_save %d, show %d, alpha %d, z %d; want 0 0 1 255 0",
		 root ? root->states[PARTS_STATE_DEFAULT].type : -1, root ? root->want_save : -1,
		 root ? root->global.show : -1, root ? root->global.alpha : -1, root ? root->global.z : -1);
	LR_CHECK(lr_exists(id) && lr_parent(id) == 0 && lr_nchild(id) == 0,
		 "root: IsExist %d, parent %d, children %d; want 1, 0, 0", lr_exists(id), lr_parent(id), lr_nchild(id));
	LR_CHECK(lr_layer_of(id) == id, "GetLayerIDByParts(root) is %d, want %d", lr_layer_of(id), id);
	ci_part(CI_PA);
	LR_CHECK(lr_parent(CI_PA) == id, "a new parts' parent is %d, want the layer %d", lr_parent(CI_PA), id);
	LR_CHECK(lr_nchild(id) == 1 && lr_child(id, 0) == CI_PA && BU_PE("IsExistChild", BU_I(id), BU_I(CI_PA)).i,
		 "the root's children: %d, first %d; want 1, %d", lr_nchild(id), lr_child(id, 0), CI_PA);
	LR_CHECK(lr_layer_of(CI_PA) == id, "GetLayerIDByParts(a top-level parts) is %d, want %d", lr_layer_of(CI_PA), id);
	ci_part(CI_PB);
	lr_set_parent(CI_PB, CI_PA);
	LR_CHECK(lr_parent(CI_PB) == CI_PA && lr_nchild(id) == 1 && lr_layer_of(CI_PB) == id,
		 "a child: parent %d, the root's children %d, layer %d; want %d, 1, %d",
		 lr_parent(CI_PB), lr_nchild(id), lr_layer_of(CI_PB), CI_PA, id);
	lr_set_parent(CI_PB, 0);		/* 0x53ab60: back under the layer's root */
	LR_CHECK(lr_parent(CI_PB) == id && lr_nchild(id) == 2 && lr_child(id, 1) == CI_PB && lr_nchild(CI_PA) == 0,
		 "SetParent(0): parent %d, the root's children %d (second %d), A's children %d; want %d, 2, %d, 0",
		 lr_parent(CI_PB), lr_nchild(id), lr_child(id, 1), lr_nchild(CI_PA), id, CI_PB);
	lr_set_parent(id, CI_PA);		/* a root is left alone (0x53ab93) */
	LR_CHECK(lr_parent(id) == 0 && lr_nchild(CI_PA) == 0, "SetParent of the root: parent %d, A's children %d; want 0, 0",
		 lr_parent(id), lr_nchild(CI_PA));
	/* The root is neutral: a top-level parts is placed as one without a
	 * parent, also when SetGlobalPos moves everything. */
	PE_UpdateComponent(0);
	struct parts *pa = parts_try_get(CI_PA);
	LR_CHECK(pa->global.pos.x == 100 && pa->global.pos.y == 100 && pa->global.alpha == 255 && pa->global.show
		 && pa->global.z == pa->local.z, "a top-level parts under the root: global (%d,%d) alpha %d show %d",
		 pa->global.pos.x, pa->global.pos.y, pa->global.alpha, pa->global.show);
	parts_set_global_pos((Point){ 7, 9 });
	PE_UpdateComponent(0);
	LR_CHECK(pa->global.pos.x == 107 && pa->global.pos.y == 109, "after SetGlobalPos(7,9): global (%d,%d), want (107,109)",
		 pa->global.pos.x, pa->global.pos.y);
	PE_SetPos(CI_PA, 110, 120);
	PE_UpdateComponent(0);
	LR_CHECK(pa->global.pos.x == 117 && pa->global.pos.y == 129, "moved after SetGlobalPos: global (%d,%d), want (117,129)",
		 pa->global.pos.x, pa->global.pos.y);
	parts_set_global_pos((Point){ 0, 0 });
	PE_UpdateComponent(0);
	LR_CHECK(pa->global.pos.x == 110 && pa->global.pos.y == 120, "SetGlobalPos back: global (%d,%d), want (110,120)",
		 pa->global.pos.x, pa->global.pos.y);
	/* Hiding the root hides the layer (CBackSceneView@HideAllFrontScene). */
	PE_SetShow(id, false);
	PE_UpdateComponent(0);
	LR_CHECK(!pa->global.show && parts_try_get(CI_PB) && !parts_try_get(CI_PB)->global.show,
		 "the layer's parts still show with the root hidden");
	PE_SetShow(id, true);
	PE_UpdateComponent(0);
	LR_CHECK(pa->global.show, "the layer's parts do not show again");
	/* A root is not hung anywhere (0x53b018), also not under a parts that
	 * is not below it: one on another layer, one on the overlay. */
	int other = ci_add(-1);
	ci_part(CI_PC);
	lr_set_parent(id, CI_PC);
	LR_CHECK(lr_parent(id) == 0 && lr_nchild(CI_PC) == 0 && lr_layer_of(id) == id && lr_is_root(id),
		 "the root under another layer's parts: parent %d, that parts' children %d, layer %d; want 0, 0, %d",
		 lr_parent(id), lr_nchild(CI_PC), lr_layer_of(id), id);
	lr_set_parent(other, CI_PA);
	LR_CHECK(lr_parent(other) == 0 && lr_nchild(CI_PA) == 0, "another layer's root under a parts: parent %d, want 0", lr_parent(other));
	ci_set_active(PARTS_CONTROLLER_SYSTEM_OVERLAY);
	ci_part(CI_PO);
	lr_set_parent(id, CI_PO);
	LR_CHECK(lr_parent(id) == 0 && lr_nchild(CI_PO) == 0, "the root under an overlay parts: parent %d, want 0", lr_parent(id));
	LR_CHECK(lr_layer_of(CI_PA) == id && lr_parent(CI_PA) == id, "the layer's parts after that: parent %d, layer %d", lr_parent(CI_PA), lr_layer_of(CI_PA));
	printf("LR1 layer roots and parents: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR2 --- */

static void lr_add_erase(void)
{
	lr_setup();
	ci_part(CI_P0);
	int a = ci_add(-1);
	LR_CHECK(a == LR_ID(1) && lr_is_root(a) && lr_exists(a) && lr_parent(a) == 0,
		 "AddLayer(-1) gave %d (want %d); root %d, IsExist %d, parent %d", a, LR_ID(1), lr_is_root(a),
		 lr_exists(a), lr_parent(a));
	ci_part(CI_PA);
	LR_CHECK(lr_parent(CI_PA) == a && lr_layer_of(CI_PA) == a && lr_parent(CI_P0) == LR_ID(0),
		 "parents %d %d, layer of A's parts %d; want %d %d %d", lr_parent(CI_PA), lr_parent(CI_P0),
		 lr_layer_of(CI_PA), a, LR_ID(0), a);
	int b = ci_add(-1);
	ci_part(CI_PB);
	ci_erase(a);
	LR_CHECK(!parts_try_get(a) && !parts_try_get(CI_PA) && parts_try_get(CI_PB) && !lr_exists(a),
		 "EraseLayer(A): its root %s, its parts %s, B's parts %s", parts_try_get(a) ? "stayed" : "went",
		 parts_try_get(CI_PA) ? "stayed" : "went", parts_try_get(CI_PB) ? "stayed" : "went");
	LR_CHECK(lr_layer_of(CI_PB) == b && lr_layer_of(CI_P0) == LR_ID(0) && lr_parent(a) == 0 && lr_nchild(a) == 0,
		 "after EraseLayer(A): layers %d %d, the gone root's parent %d, children %d", lr_layer_of(CI_PB),
		 lr_layer_of(CI_P0), lr_parent(a), lr_nchild(a));
	/* the lowest free ID again, with a root again */
	int c = ci_add(-1);
	LR_CHECK(c == a && lr_is_root(c) && lr_nchild(c) == 0, "the next layer is %d (want %d again), root %d, children %d",
		 c, a, lr_is_root(c), lr_nchild(c));
	ci_part(CI_PC);
	LR_CHECK(lr_parent(CI_PC) == c && lr_layer_of(CI_PB) == b, "parents after the ID came back: %d, B's layer %d",
		 lr_parent(CI_PC), lr_layer_of(CI_PB));
	/* a parts made with the erased layer's number (Wrap after EraseLayer) */
	ci_erase(c);
	LR_CHECK(ci_active() == b, "active %d after erasing the top layer, want %d", ci_active(), b);
	parts_get(c);
	LR_CHECK(!lr_is_root(c) && lr_parent(c) == b, "a stray parts numbered %d: root %d, parent %d; want no, %d",
		 c, lr_is_root(c), lr_parent(c), b);
	int d = ci_add(-1);
	LR_CHECK(d != c && d == LR_ID(3) && lr_is_root(d), "the next layer is %d, want %d (the taken number %d skipped)",
		 d, LR_ID(3), c);
	printf("LR2 AddLayer and EraseLayer roots: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR3 --- */

static void lr_activity_on_layer(void)
{
	lr_setup();
	int layer = ci_add(-1);
	int root = lr_load_activity("LR3");
	int window = BU_PE("GetActivityPartsNumber", BU_I(sf_str("LR3", -1)), BU_I(sf_str(LR_MW_NAME, -1))).i;
	LR_CHECK(window > 0 && parts_try_get(window) && parts_try_get(window)->message_window,
		 "the activity's message window parts: %d", window);
	LR_CHECK(lr_parent(root) == layer && lr_layer_of(root) == layer && lr_layer_of(window) == layer,
		 "the activity root's parent %d, layer %d, the window's layer %d; want %d", lr_parent(root),
		 lr_layer_of(root), lr_layer_of(window), layer);
	LR_CHECK(lr_nchild(layer) == 1 && lr_child(layer, 0) == root, "the layer's children: %d, first %d; want 1, %d",
		 lr_nchild(layer), lr_child(layer, 0), root);
	LR_CHECK(lr_activity_exists("LR3"), "the activity does not exist after loading");
	LR_CHECK(!strcmp(lr_free_name("LR3"), "LR3:0") && !strcmp(lr_free_name("LR3x"), "LR3x"),
		 "GetFreeName with the activity loaded: %s", lr_free_name("LR3"));
	ci_erase(layer);
	LR_CHECK(!parts_try_get(root) && !parts_try_get(window) && !parts_try_get(layer),
		 "after EraseLayer: the activity root %s, its window %s, the layer root %s",
		 parts_try_get(root) ? "stayed" : "went", parts_try_get(window) ? "stayed" : "went",
		 parts_try_get(layer) ? "stayed" : "went");
	LR_CHECK(ci_active() == LR_ID(0) && ci_len() == 1, "active %d, %d layers; want %d, 1", ci_active(), ci_len(), LR_ID(0));
	/* The name stays taken until its owner releases it. */
	LR_CHECK(lr_activity_exists("LR3"), "the activity's name is free after EraseLayer (its owner has not released it)");
	LR_CHECK(!strcmp(lr_free_name("LR3"), "LR3:0"), "GetFreeName after EraseLayer: %s, want LR3:0", lr_free_name("LR3"));
	LR_CHECK(BU_PE("GetActivityPartsNumber", BU_I(sf_str("LR3", -1)), BU_I(sf_str(LR_MW_NAME, -1))).i == window,
		 "the table no longer names the window's number %d", window);
	int list = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0));
	heap[list].page->array.rank = 1;
	bool released = BU_PE("ReleaseActivity", BU_I(sf_str("LR3", -1)), BU_I(list)).i;
	LR_CHECK(released && !lr_activity_exists("LR3") && !strcmp(lr_free_name("LR3"), "LR3"),
		 "the owner's ReleaseActivity: %d, exists %d, the free name %s; want 1, 0, LR3", released,
		 lr_activity_exists("LR3"), lr_free_name("LR3"));
	LR_CHECK(!parts_try_get(root) && !parts_try_get(window), "ReleaseActivity made the gone parts again");
	printf("LR3 an activity on an erased layer: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR4 --- */

static void lr_message_window(void)
{
	lr_setup();
	int st = ain_get_struct(ain, "message::detail::CMessageWindow");
	if (st < 0) { printf("LAYER-ROOT struct CMessageWindow missing\n"); exit(2); }
	int layer = ci_add(-1);
	int win = vm_construct_struct(st);
	lr_method("message::detail::CMessageWindow@Create", win, 1,
		  (int[]){ sf_str("Scene/10_Adv/Main/AdvMessageWindow_event", -1) });
	bool exist = lr_method("message::detail::CMessageWindow@IsExist", win, 0, NULL);
	int root = lr_method("message::detail::CMessageWindow@GetRootPartsNumber", win, 0, NULL);
	LR_CHECK(exist && root >= 900000 && parts_try_get(root), "after Create: IsExist %d, root %d", exist, root);
	LR_CHECK(lr_parent(root) == layer && lr_layer_of(root) == layer,
		 "the window root's parent %d, GetLayerIDByParts %d; want the layer %d", lr_parent(root), lr_layer_of(root), layer);
	LR_CHECK(lr_activity_exists("Scene/10_Adv/Main/AdvMessageWindow_event"), "the window's activity does not exist");
	/* SceneContext@1: AFL_Parts_EraseLayer -> CallErasingLayerEvent ->
	 * CMessageWindow@OnErasingLayer -> Release -> ReleaseActivity */
	ci_erase(layer);
	exist = lr_method("message::detail::CMessageWindow@IsExist", win, 0, NULL);
	LR_CHECK(!exist && !parts_try_get(root) && !lr_activity_exists("Scene/10_Adv/Main/AdvMessageWindow_event"),
		 "after EraseLayer: IsExist %d, the root %s, the activity %s; want 0, gone, gone", exist,
		 parts_try_get(root) ? "stayed" : "went",
		 lr_activity_exists("Scene/10_Adv/Main/AdvMessageWindow_event") ? "there" : "gone");
	/* CMessageWindowManager@Create makes the window anew when !IsExist */
	int layer2 = ci_add(-1);
	lr_method("message::detail::CMessageWindow@Create", win, 1,
		  (int[]){ sf_str("Scene/10_Adv/Main/AdvMessageWindow_event", -1) });
	exist = lr_method("message::detail::CMessageWindow@IsExist", win, 0, NULL);
	int root2 = lr_method("message::detail::CMessageWindow@GetRootPartsNumber", win, 0, NULL);
	LR_CHECK(exist && root2 != root && parts_try_get(root2) && lr_layer_of(root2) == layer2,
		 "created again: IsExist %d, root %d (was %d), its layer %d (want %d)", exist, root2, root,
		 lr_layer_of(root2), layer2);
	printf("LR4 CMessageWindow over EraseLayer: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR10 --- */

static void lr_two_windows(void)
{
	lr_setup();
	int st = ain_get_struct(ain, "message::detail::CMessageWindow");
	int layer = ci_add(-1);
	int win[2], root[2];
	const char *names[2] = { "Scene/10_Adv/Main/AdvMessageWindow_event", "Scene/10_Adv/Main/AdvMessageWindow_main" };
	for (int i = 0; i < 2; i++) {
		win[i] = vm_construct_struct(st);
		lr_method("message::detail::CMessageWindow@Create", win[i], 1, (int[]){ sf_str(names[i], -1) });
		root[i] = lr_method("message::detail::CMessageWindow@GetRootPartsNumber", win[i], 0, NULL);
		LR_CHECK(root[i] >= 900000 && lr_layer_of(root[i]) == layer, "window %d: root %d, layer %d (want %d)",
			 i, root[i], lr_layer_of(root[i]), layer);
	}
	ci_erase(layer);
	for (int i = 0; i < 2; i++) {
		bool exist = lr_method("message::detail::CMessageWindow@IsExist", win[i], 0, NULL);
		LR_CHECK(!exist && !parts_try_get(root[i]) && !lr_activity_exists(names[i]),
			 "window %d after EraseLayer: IsExist %d, root %s, activity %s; want released", i, exist,
			 parts_try_get(root[i]) ? "stayed" : "went", lr_activity_exists(names[i]) ? "there" : "gone");
	}
	printf("LR10 two windows on one layer: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR5 --- */

static void lr_stale_warning(void)
{
	lr_setup();
	int layer = ci_add(-1);
	int root = lr_load_activity("LR5");
	ci_erase(layer);
	LR_CHECK(!parts_try_get(root), "the activity root stayed");
	char path[] = "/tmp/xs4-layer-root-XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0) { printf("LAYER-ROOT mkstemp failed\n"); exit(2); }
	fflush(stderr);
	int saved = dup(2);
	dup2(fd, 2);
	PE_SetShow(root, true);			/* what SetShow::set does for a kept number */
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	char buf[4096] = "";
	lseek(fd, 0, SEEK_SET);
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	buf[n > 0 ? n : 0] = '\0';
	close(fd);
	unlink(path);
	struct parts *p = parts_try_get(root);
	LR_CHECK(p && p->states[PARTS_STATE_DEFAULT].type == PARTS_UNINITIALIZED && !p->message,
		 "the stale number was not re-created as a blank parts");
	LR_CHECK(strstr(buf, "stale activity parts") && strstr(buf, "re-created"),
		 "no warning for the stale activity parts; stderr: %.200s", buf);
	printf("LR5 stale activity parts warning: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR8 --- */

static void lr_load_into(void *buffer)
{
	LR_CHECK(PE_Load((struct page **)buffer), "PE_Load failed");
}

static void lr_save_load(void)
{
	lr_setup();
	ci_part(CI_P0);
	/* A plain parts whose number happens to be in the roots' range (a
	 * GetFreeNumber parts is): not a root, saved like any other. */
	ci_part(LR_ID(500));
	LR_CHECK(!lr_is_root(LR_ID(500)) && lr_parent(LR_ID(500)) == LR_ID(0), "a big-numbered parts: root %d, parent %d",
		 lr_is_root(LR_ID(500)), lr_parent(LR_ID(500)));
	int a = ci_add(-1);
	ci_part(CI_PA);
	ci_part(CI_PB);
	lr_set_parent(CI_PB, CI_PA);
	struct page *saved = NULL;
	LR_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	/* header 4, version, no numeral fonts, active, layers, parts count */
	LR_CHECK(saved->values[8].i == 4, "the save holds %d parts, want 4 (no roots)", saved->values[8].i);
	/* A v14 save names its IDs, also when they are the default ones. */
	int section = ci_id_section(saved, 2);
	LR_CHECK(section == 5 && saved->values[saved->nr_vars - 3].i == LR_ID(0) && saved->values[saved->nr_vars - 2].i == a
		 && saved->values[saved->nr_vars - 1].i == a, "the ID section of a pushed-only stack: %d words", section);
	for (int i = 0; i < saved->nr_vars - section; i++)
		LR_CHECK(saved->values[i].i != LR_ID(0) && saved->values[i].i != a, "a root number %d is in the save at %d",
			 saved->values[i].i, i);
	ci_erase(a);
	LR_CHECK(PE_Load(&saved), "PE_Load failed");
	LR_CHECK(ci_len() == 2 && ci_active() == a && lr_is_root(LR_ID(0)) && lr_is_root(a),
		 "loaded: %d layers, active %d, roots %d %d", ci_len(), ci_active(), lr_is_root(LR_ID(0)), lr_is_root(a));
	LR_CHECK(parts_try_get(LR_ID(500)) && !lr_is_root(LR_ID(500)) && lr_parent(LR_ID(500)) == LR_ID(0),
		 "the big-numbered parts after PE_Load: %s, root %d, parent %d", parts_try_get(LR_ID(500)) ? "there" : "gone",
		 lr_is_root(LR_ID(500)), lr_parent(LR_ID(500)));
	LR_CHECK(lr_parent(CI_P0) == LR_ID(0) && lr_parent(CI_PA) == a && lr_layer_of(CI_PA) == a,
		 "loaded parents %d %d, A's layer %d; want %d %d %d", lr_parent(CI_P0), lr_parent(CI_PA), lr_layer_of(CI_PA),
		 LR_ID(0), a, a);
	PE_UpdateComponent(0);		/* the saved parent is attached by the dirty pass */
	LR_CHECK(lr_parent(CI_PB) == CI_PA && lr_layer_of(CI_PB) == a && lr_nchild(a) == 1,
		 "loaded child: parent %d, layer %d, the layer's children %d; want %d %d 1", lr_parent(CI_PB),
		 lr_layer_of(CI_PB), lr_nchild(a), CI_PA, a);
	/* a stack whose IDs are not the positions' (the ID section) */
	int c = ci_add(LR_ID(0));		/* [0, c, a] */
	ci_part(CI_PC);
	struct page *saved2 = NULL;
	LR_CHECK(PE_Save(&saved2) && saved2, "second PE_Save failed");
	ci_erase(c);
	ci_erase(a);
	LR_CHECK(PE_Load(&saved2), "second PE_Load failed");
	LR_CHECK(ci_len() == 3 && ci_id_at(1) == c && ci_id_at(2) == a && lr_is_root(c) && lr_is_root(a),
		 "loaded with the section: %d layers [%d %d %d], roots %d %d", ci_len(), ci_id_at(0), ci_id_at(1),
		 ci_id_at(2), lr_is_root(c), lr_is_root(a));
	LR_CHECK(lr_parent(CI_PC) == c && lr_parent(CI_PA) == a && lr_parent(CI_P0) == LR_ID(0),
		 "parents with the section %d %d %d; want %d %d %d", lr_parent(CI_PC), lr_parent(CI_PA), lr_parent(CI_P0),
		 c, a, LR_ID(0));
	LR_CHECK(!parts_try_get(LR_ID(3)) || !lr_is_root(LR_ID(3)), "a root is left for an ID that is not in the stack");
	/* IDs with a gap: [0, c] after erasing a (c is the second ID taken).
	 * Loading makes the roots of the positions' IDs first (0, 1); the
	 * section then names 0 and c: the root of 1 goes (it is not in the
	 * stack), c's comes. */
	ci_erase(a);
	LR_CHECK(ci_len() == 2 && ci_id_at(1) == c && c != LR_ID(1), "gap setup: %d layers [%d %d]", ci_len(), ci_id_at(0), ci_id_at(1));
	struct page *saved3 = NULL;
	LR_CHECK(PE_Save(&saved3) && saved3, "third PE_Save failed");
	ci_erase(c);
	LR_CHECK(PE_Load(&saved3), "third PE_Load failed");
	LR_CHECK(ci_len() == 2 && ci_id_at(1) == c && lr_is_root(c) && lr_parent(CI_PC) == c,
		 "loaded with a gap: %d layers [%d %d], c's root %d, C's parent %d", ci_len(), ci_id_at(0), ci_id_at(1),
		 lr_is_root(c), lr_parent(CI_PC));
	LR_CHECK(!parts_try_get(LR_ID(1)) && !lr_exists(LR_ID(1)), "the root of the ID that is not in the stack (%d) stayed", LR_ID(1));
	/* A plain parts numbered like the default ID of a position that the
	 * stack gives another ID (the script Wraps a layer ID it kept after
	 * erasing the layer, which makes a parts of that number): [0, c] and
	 * a parts numbered ID 1. It is loaded as what it was. */
	ci_part(LR_ID(1));
	PE_SetPos(LR_ID(1), 31, 32);
	LR_CHECK(!lr_is_root(LR_ID(1)) && lr_parent(LR_ID(1)) == c, "a parts numbered %d: root %d, parent %d; want no, %d",
		 LR_ID(1), lr_is_root(LR_ID(1)), lr_parent(LR_ID(1)), c);
	struct page *saved4 = NULL;
	LR_CHECK(PE_Save(&saved4) && saved4, "fourth PE_Save failed");
	ci_erase(c);
	LR_CHECK(PE_Load(&saved4), "fourth PE_Load failed");
	struct parts *stray = parts_try_get(LR_ID(1));
	LR_CHECK(ci_len() == 2 && ci_id_at(1) == c && lr_is_root(c) && stray && !lr_is_root(LR_ID(1))
		 && stray->local.pos.x == 31 && stray->local.pos.y == 32 && lr_parent(LR_ID(1)) == c && stray->controller_no == c,
		 "a plain parts numbered %d after PE_Load: %s, root %d, parent %d, layer %d; want there, no, %d, %d", LR_ID(1),
		 stray ? "there" : "gone", lr_is_root(LR_ID(1)), lr_parent(LR_ID(1)), stray ? stray->controller_no : -1, c, c);
	/* Without its section (a save from before the IDs were root numbers)
	 * the layers get the IDs of their positions, and a warning says so. */
	int tail = ci_id_section(saved, 2);
	saved->nr_vars -= tail;
	char err[4096];
	lr_capture_stderr(lr_load_into, &saved, err, sizeof(err));
	saved->nr_vars += tail;
	LR_CHECK(tail == 5 && ci_len() == 2 && ci_id_at(0) == LR_ID(0) && ci_id_at(1) == LR_ID(1) && ci_active() == LR_ID(1)
		 && lr_parent(CI_PA) == LR_ID(1) && lr_is_root(LR_ID(1)),
		 "a save without the section: %d layers [%d %d], active %d, A's parent %d", ci_len(), ci_id_at(0), ci_id_at(1),
		 ci_active(), lr_parent(CI_PA));
	LR_CHECK(strstr(err, "without controller IDs"), "no warning for a save without the ID section; stderr: %.200s", err);
	lr_capture_stderr(lr_load_into, &saved, err, sizeof(err));
	LR_CHECK(!strstr(err, "without controller IDs"), "a save with the ID section warned: %.200s", err);
	ca_free_buffer(saved);
	ca_free_buffer(saved2);
	ca_free_buffer(saved3);
	ca_free_buffer(saved4);
	printf("LR8 save and load: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR6 (guard) --- */

static void lr_legacy(void)
{
	lr_setup();
	ain->version = 13;
	probe_init_parts_table();
	ci_part(CI_P0);
	int a = PE_AddController(-1);
	ci_part(CI_PA);
	LR_CHECK(a == 1 && !parts_try_get(0) && !parts_try_get(1) && !parts_try_get(LR_ID(0)) && !parts_try_get(LR_ID(1)),
		 "v13: AddController gave %d; roots made", a);
	LR_CHECK(!parts_try_get(CI_PA)->parent && !parts_try_get(CI_P0)->parent
		 && PE_GetParentPartsNumber(CI_PA) == -1 && lr_parent(CI_PA) == 0,
		 "v13: a top-level parts has a parent (%d / %d)", PE_GetParentPartsNumber(CI_PA), lr_parent(CI_PA));
	lr_set_parent(CI_PA, 0);
	PE_UpdateComponent(0);
	LR_CHECK(!parts_try_get(CI_PA)->parent, "v13: SetParent(0) gave a parent");
	struct page *list = NULL;
	PE_RemoveController(&list, -1);
	LR_CHECK(!parts_try_get(CI_PA) && parts_try_get(CI_P0) && PE_get_active_controller() == 0,
		 "v13: pop released %s, active %d", parts_try_get(CI_PA) ? "nothing" : "A", PE_get_active_controller());
	ain->version = 14;
	printf("LR6 engines before v14: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR7 (guard) --- */

static void lr_overlay(void)
{
	lr_setup();
	int ov = ci_call("parts::detail::GetSystemOverlayLayer", 0, NULL);
	LR_CHECK(ov == PARTS_CONTROLLER_SYSTEM_OVERLAY, "the overlay layer is %d, want %d", ov, PARTS_CONTROLLER_SYSTEM_OVERLAY);
	ci_set_active(ov);
	ci_part(CI_PO);
	LR_CHECK(!parts_try_get(ov) && !parts_try_get(CI_PO)->parent && lr_parent(CI_PO) == 0
		 && lr_layer_of(CI_PO) == CI_PO && parts_try_get(CI_PO)->controller_no == ov,
		 "overlay: root %s, parent of its parts %d, layer walk %d", parts_try_get(ov) ? "made" : "none",
		 lr_parent(CI_PO), lr_layer_of(CI_PO));
	printf("LR7 the system overlay layer: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR9 (guard) --- */

static void lr_push_pop(void)
{
	lr_setup();
	ci_part(CI_P0);
	int a = ci_add(-1), b = ci_add(-1);
	ci_part(CI_PB);
	int bottom = ci_id_at(0);
	LR_CHECK(a != bottom && b != a && b != bottom && ci_active() == b && ci_id_at(1) == a && ci_id_at(2) == b,
		 "pushes: %d %d over %d, active %d", a, b, bottom, ci_active());
	struct page *saved = NULL;
	LR_CHECK(PE_Save(&saved) && saved, "PE_Save failed");
	int body = saved->nr_vars - ci_id_section(saved, 3);
	LR_CHECK(saved->values[6].i == 2 && saved->values[7].i == 3 && saved->values[8].i == 2
		 && saved->values[body - 1].i == 0,
		 "save header: active %d, layers %d, parts %d, last word before the ID section %d; want 2 3 2 0", saved->values[6].i,
		 saved->values[7].i, saved->values[8].i, saved->values[body - 1].i);
	ci_erase(-1);
	ci_erase(-1);
	LR_CHECK(ci_len() == 1 && ci_active() == bottom && !parts_try_get(CI_PB) && parts_try_get(CI_P0)
		 && (!parts_try_get(a) || !lr_is_root(a)) && (!parts_try_get(b) || !lr_is_root(b)),
		 "pops: %d layers, active %d", ci_len(), ci_active());
	LR_CHECK(ci_add(-1) == a, "the next layer is not %d", a);
	ca_free_buffer(saved);
	printf("LR9 push and pop: %s\n", lr_failures ? "FAIL" : "ok");
}

/* --- LR11 --- */

static void lr_across_layers(void)
{
	lr_setup();
	int l0 = LR_ID(0);
	ci_part(CI_P0);
	int a = ci_add(-1);
	ci_part(CI_PA);
	ci_part(CI_PB);
	lr_set_parent(CI_PB, CI_PA);		/* A with its child B on layer a */
	int b = ci_add(-1);
	ci_part(CI_PC);
	/* A goes under a parts of layer b: A and B are b's now */
	lr_set_parent(CI_PA, CI_PC);
	LR_CHECK(lr_parent(CI_PA) == CI_PC && lr_layer_of(CI_PA) == b && lr_layer_of(CI_PB) == b && lr_nchild(a) == 0,
		 "under another layer's parts: parent %d, layers %d %d, the old layer's children %d; want %d, %d %d, 0",
		 lr_parent(CI_PA), lr_layer_of(CI_PA), lr_layer_of(CI_PB), lr_nchild(a), CI_PC, b, b);
	LR_CHECK(ci_owner(CI_PA) == b && ci_owner(CI_PB) == b && ci_z(CI_PA) == 2 && ci_z(CI_PB) == 2,
		 "the moved parts' controllers %d %d (want %d), sprite layers %d %d (want 2)", ci_owner(CI_PA), ci_owner(CI_PB), b,
		 ci_z(CI_PA), ci_z(CI_PB));
	/* SetParent(0): the root at the top of the chain, layer b's */
	lr_set_parent(CI_PA, 0);
	LR_CHECK(lr_parent(CI_PA) == b && lr_layer_of(CI_PB) == b && lr_nchild(CI_PC) == 0,
		 "SetParent(0) after the move: parent %d, want the new layer %d", lr_parent(CI_PA), b);
	/* straight under another layer's root (what the script does with a layer ID) */
	lr_set_parent(CI_PA, l0);
	LR_CHECK(lr_parent(CI_PA) == l0 && ci_owner(CI_PA) == l0 && ci_owner(CI_PB) == l0 && ci_z(CI_PB) == 0 && lr_layer_of(CI_PB) == l0,
		 "under another layer's root: parent %d, controllers %d %d, B's sprite layer %d; want %d, %d %d, 0",
		 lr_parent(CI_PA), ci_owner(CI_PA), ci_owner(CI_PB), ci_z(CI_PB), l0, l0, l0);
	/* the layer it was made on goes: it stays; the layer it is on goes: it goes */
	lr_set_parent(CI_PA, CI_PC);
	ci_erase(a);
	LR_CHECK(parts_try_get(CI_PA) && parts_try_get(CI_PB) && lr_layer_of(CI_PB) == b,
		 "EraseLayer of the layer the parts were made on: A %s, B %s", parts_try_get(CI_PA) ? "stayed" : "went",
		 parts_try_get(CI_PB) ? "stayed" : "went");
	ci_erase(b);
	LR_CHECK(!parts_try_get(CI_PA) && !parts_try_get(CI_PB) && !parts_try_get(CI_PC) && parts_try_get(CI_P0),
		 "EraseLayer of the layer the parts hang on: A %s, B %s, C %s", parts_try_get(CI_PA) ? "stayed" : "went",
		 parts_try_get(CI_PB) ? "stayed" : "went", parts_try_get(CI_PC) ? "stayed" : "went");
	/* onto the overlay and back */
	ci_part(CI_PA);
	ci_set_active(PARTS_CONTROLLER_SYSTEM_OVERLAY);
	ci_part(CI_PO);
	lr_set_parent(CI_PA, CI_PO);
	LR_CHECK(ci_owner(CI_PA) == PARTS_CONTROLLER_SYSTEM_OVERLAY && lr_parent(CI_PA) == CI_PO,
		 "under an overlay parts: controller %d, parent %d", ci_owner(CI_PA), lr_parent(CI_PA));
	lr_set_parent(CI_PA, l0);
	LR_CHECK(ci_owner(CI_PA) == l0 && lr_parent(CI_PA) == l0 && lr_layer_of(CI_PA) == l0,
		 "back under the default layer's root: controller %d, parent %d", ci_owner(CI_PA), lr_parent(CI_PA));
	printf("LR11 parts moved between layers: %s\n", lr_failures ? "FAIL" : "ok");
}

static int lr_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		lr_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(lr_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_layer_root(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "LR1", lr_roots }, { "LR2", lr_add_erase }, { "LR3", lr_activity_on_layer },
		{ "LR4", lr_message_window }, { "LR5", lr_stale_warning }, { "LR6", lr_legacy },
		{ "LR7", lr_overlay }, { "LR8", lr_save_load }, { "LR9", lr_push_pop },
		{ "LR10", lr_two_windows }, { "LR11", lr_across_layers },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += lr_fork(cases[i].name, cases[i].fn);
	printf("LAYER-ROOT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
