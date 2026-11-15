/* array-enum-member: an array member whose declared element is a value
 * (int, bool, enum, float) gets a page of that value type wherever the VM
 * makes or restores it, so that its elements are never taken for heap slots.
 *
 * Original (static disassembly of the unpacked executable):
 *   - An array object is made only by 0x679350, from the element type
 *     description the declaration gives (+0x2c..: code +0x30, struct or enum
 *     number +0x34; +0x48 is the slot count 0x653420 derives from it). A
 *     struct member gets it from its declaration (0x656970, array branch
 *     0x6569dd), X_A_INIT from the variable's declaration (0x66e579).
 *   - Releasing an element (0x680270 -> 0x656c10) does nothing for int,
 *     float, bool and enum (10, 11, 47, 92). Copying (0x67ffb0) moves them.
 *   - DeserializeStruct restores an array in place (0x65d5b0): the
 *     destination object and its element type stay.
 * This VM (before the change) gave an array<enum> member of an object made
 * without its constructor a generic page (page.c init_struct_slot), whose
 * elements its teardown, copies and A_REF copies take for heap slots.
 * system.DeserializeStruct took the type of the page it replaced, and a VM
 * image brings back what an older engine wrote.
 *
 * The element values below are slot numbers of live objects, so that a value
 * taken for a slot shows as a changed reference count.
 *
 * Cases:
 *   AE1  BattleBonusCalculator made without its constructor (alloc_struct):
 *        m_bonuses is an int array; two values stored the way the script
 *        does (Array.PushBack, operand 1); releasing the array leaves the
 *        objects whose slot numbers they are alone.
 *   AE2  BattleContext made the way DeserializeStruct makes a member or an
 *        element (vm_construct_struct, no STRT constructor): the
 *        BattleBonusCalculator down its member chain (BattleResult) has an
 *        int array in m_bonuses; its release leaves the objects alone.
 *   AE3  DeserializeStruct into a BattleBonusCalculator whose m_bonuses page
 *        is generic (as an older engine left it): the new page is an int
 *        array with the two values; releasing it leaves the objects alone.
 *   AE4  A VM image (rsm, then json) holding a struct member declared
 *        array<enum> whose page is a one-dimensional generic page: after the
 *        load the page is an int array with its values, and releasing it
 *        leaves the objects alone. A generic page of a member declared as an
 *        array of wraps comes back unchanged (type, struct_type, layout,
 *        rank, values) and still releases its element.
 *   AE5  Every struct member declared as an array: the page the object gets
 *        without a constructor (alloc_struct) and the page X_A_INIT 0 of
 *        size 0 makes for the same member agree on whether the elements are
 *        values, and on the page type when they are. One "AE5 row" line per
 *        member (a_type/struct_type/elem_slots/rank of each), compared across
 *        builds outside the probe.
 *   AE6  A copy of a BattleBonusCalculator made without its constructor
 *        (copy_page, as a struct assignment does) and an A_REF copy of its
 *        m_bonuses (v14_array_value_copy) keep the two values and leave the
 *        objects' reference counts alone.
 *   AE7  DeserializeStruct into a BattleBonusCalculator whose m_bonuses slot
 *        has no page: the new page is an int array with struct_type -1, as
 *        before the change (guard).
 *   AE8  As AE3, but the generic page being replaced holds two values that
 *        are slot numbers of live objects: the new page is an int array with
 *        the file's values, and neither the replaced values nor the restored
 *        ones lose or gain a reference, at the load or at the release.
 *   AE9  DeserializeStruct into an object (the first struct, in AIN order,
 *        with a member declared as an array of single-slot wraps) whose page
 *        for that member is generic: the elements are references, so the new
 *        page keeps the replaced page's type, struct_type, layout and rank.
 *        The replaced page holds one empty element (-1) and the file none, so
 *        the case also shows that the member got a new page: another page
 *        object, empty, and no longer the generic one the probe put there.
 *   AE10 VM images (rsm, then json) whose generic pages the load must leave
 *        alone, each in its own case: a two-dimensional page and a page of
 *        two-slot elements under a member declared array<enum>, and generic
 *        pages no struct member names (one held only by the probe, one held
 *        only by another generic array). Type, struct_type, layout, rank,
 *        values and reference counts come back as they were written.
 *   AE11 VM images (rsm, then json) where m_bonuses (declared array<enum>)
 *        holds a one-dimensional page of another concrete type, a float
 *        array and then a string array: the load changes only generic pages,
 *        so type, struct_type, layout, rank and values come back unchanged.
 */

static int ae_bad;

#define AE_EXPECT(c, ...) do {                                                  \
		if (!(c)) {                                                     \
			printf("  FAIL %s:%d: ", __func__, __LINE__);            \
			printf(__VA_ARGS__);                                    \
			printf("\n");                                           \
			fflush(stdout);                                         \
			ae_bad = 1;                                             \
		}                                                               \
	} while (0)

static struct page *ae_arr(int slot)
{
	return da_live_page(slot) && heap[slot].page->type == ARRAY_PAGE ? heap[slot].page : NULL;
}

static void ae_meta(struct page *p, char *buf, size_t n)
{
	if (!p)
		snprintf(buf, n, "none");
	else
		snprintf(buf, n, "%d/%d/%d/%d", p->a_type, p->array.struct_type, p->array.elem_slots, p->array.rank);
}

/* The values of an array page are exactly a and b. */
static bool ae_holds(int slot, int a, int b)
{
	struct page *p = ae_arr(slot);
	return p && p->nr_vars == 2 && p->values[0].i == a && p->values[1].i == b;
}

/* Store a value the way the script stores a BattleBonusType (Array.PushBack
 * with operand 1: a plain value). */
static void ae_push(int arr, int value)
{
	int args[2] = { arr, value };
	oa_hll("PushBack", 0, 1, 2, args, NULL, 0);
}

/* The member forgets its array and the array's last reference goes. */
static void ae_drop_member(int obj, const char *member)
{
	int a = sp_get(obj, member);
	sp_at(obj, member)->i = -1;
	if (a > 1 && heap_index_valid(a))
		heap_unref(a);
}

/* Replace the page of an array slot by an empty one-dimensional generic page,
 * as init_struct_slot made it before the change (struct_type the member's
 * struc, here the enum's number). */
static void ae_make_generic(int slot, int struct_type, int n, const int *vals)
{
	struct page *g = alloc_page(ARRAY_PAGE, AIN_ARRAY, n);
	g->array.rank = 1;
	g->array.struct_type = struct_type;
	for (int i = 0; i < n; i++)
		g->values[i].i = vals[i];
	struct page *old = heap[slot].page;
	heap[slot].page = g;
	if (old)
		free_page(old);
}

static int ae_member_struc(const char *sname, const char *member)
{
	int s = sp_sno(sname);
	return ain->structures[s].members[sp_mem(s, member)].type.struc;
}

/* AE1 */
static int ae1_eager(void)
{
	ae_bad = 0;
	int obj = alloc_struct(sp_sno("BattleBonusCalculator"));
	int arr = sp_get(obj, "m_bonuses");
	char m[64];
	ae_meta(ae_arr(arr), m, sizeof m);
	printf("AE1 m_bonuses without the constructor: %s\n", m);
	AE_EXPECT(ae_arr(arr) && ae_arr(arr)->a_type == AIN_ARRAY_INT, "m_bonuses page %s, want a_type %d", m, AIN_ARRAY_INT);
	int w0 = oa_obj(), w1 = oa_obj();
	ae_push(arr, w0);
	ae_push(arr, w1);
	AE_EXPECT(ae_holds(arr, w0, w1), "PushBack did not store the two values");
	AE_EXPECT(ae_arr(arr) && ae_arr(arr)->a_type == AIN_ARRAY_INT, "after PushBack the page type is %d",
		  ae_arr(arr) ? ae_arr(arr)->a_type : -1);
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "storing the values changed the counts to %d %d",
		  HEAP_REF(w0), HEAP_REF(w1));
	ae_drop_member(obj, "m_bonuses");
	printf("AE1 after the release: objects %d %d reference(s)\n", HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "releasing m_bonuses changed the objects' counts to %d %d",
		  HEAP_REF(w0), HEAP_REF(w1));
	return ae_bad;
}

/* Depth-first through members declared struct or wrap, for a page of the
 * struct `want`. */
static int ae_find_struct(int slot, int want, int depth, int *seen, int *nseen)
{
	if (depth > 8 || !da_live_page(slot) || heap[slot].page->type != STRUCT_PAGE)
		return -1;
	for (int i = 0; i < *nseen; i++)
		if (seen[i] == slot)
			return -1;
	if (*nseen < 4096)
		seen[(*nseen)++] = slot;
	struct page *p = heap[slot].page;
	if (p->index == want)
		return slot;
	for (int i = 0; i < p->nr_vars; i++) {
		const struct ain_type *t = variable_decltype(p, i);
		if (!t || (t->data != AIN_STRUCT && t->data != AIN_WRAP))
			continue;
		int r = ae_find_struct(p->values[i].i, want, depth + 1, seen, nseen);
		if (r > 0)
			return r;
		p = heap[slot].page;
	}
	return -1;
}

/* AE2 */
static int ae2_construct_chain(void)
{
	ae_bad = 0;
	int ctx = vm_construct_struct(sp_sno("BattleContext"));
	SP_CHECK(ctx > 0, "vm_construct_struct(BattleContext) gave %d", ctx);
	static int seen[4096];
	int nseen = 0;
	int bbc = ae_find_struct(ctx, sp_sno("BattleBonusCalculator"), 0, seen, &nseen);
	SP_CHECK(bbc > 0, "no BattleBonusCalculator down BattleContext's members (%d pages seen)", nseen);
	int arr = sp_get(bbc, "m_bonuses");
	char m[64];
	ae_meta(ae_arr(arr), m, sizeof m);
	printf("AE2 BattleContext -> BattleBonusCalculator slot %d (%d pages seen), m_bonuses %s\n", bbc, nseen, m);
	AE_EXPECT(ae_arr(arr) && ae_arr(arr)->a_type == AIN_ARRAY_INT, "m_bonuses page %s, want a_type %d", m, AIN_ARRAY_INT);
	int w0 = oa_obj(), w1 = oa_obj();
	ae_push(arr, w0);
	ae_push(arr, w1);
	AE_EXPECT(ae_holds(arr, w0, w1), "PushBack did not store the two values");
	ae_drop_member(bbc, "m_bonuses");
	printf("AE2 after the release: objects %d %d reference(s)\n", HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "releasing m_bonuses changed the objects' counts to %d %d",
		  HEAP_REF(w0), HEAP_REF(w1));
	return ae_bad;
}

/* AE3 */
static int ae3_deserialize_generic(void)
{
	ae_bad = 0;
	int src = sp_new("BattleBonusCalculator");
	int w0 = oa_obj(), w1 = oa_obj();
	ae_push(sp_get(src, "m_bonuses"), w0);
	ae_push(sp_get(src, "m_bonuses"), w1);
	SP_CHECK(ae_holds(sp_get(src, "m_bonuses"), w0, w1), "the source does not hold the two values");
	SP_CHECK(sp_sys("SerializeStruct", "T_AE3.asd", src) == 1, "SerializeStruct failed");
	int dst = alloc_struct(sp_sno("BattleBonusCalculator"));
	int arr = sp_get(dst, "m_bonuses");
	ae_make_generic(arr, ae_member_struc("BattleBonusCalculator", "m_bonuses"), 0, NULL);
	int r = sp_sys("DeserializeStruct", "T_AE3.asd", dst);
	SP_CHECK(r == 1, "DeserializeStruct returned %d", r);
	int arr2 = sp_get(dst, "m_bonuses");
	char m[64];
	ae_meta(ae_arr(arr2), m, sizeof m);
	printf("AE3 restored into a generic page: slot %d -> %d, page %s\n", arr, arr2, m);
	AE_EXPECT(ae_holds(arr2, w0, w1), "the restored array does not hold the two values");
	AE_EXPECT(ae_arr(arr2) && ae_arr(arr2)->a_type == AIN_ARRAY_INT, "restored page %s, want a_type %d", m, AIN_ARRAY_INT);
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "after the load the objects have %d %d reference(s)",
		  HEAP_REF(w0), HEAP_REF(w1));
	ae_drop_member(dst, "m_bonuses");
	ae_drop_member(src, "m_bonuses");
	printf("AE3 after both releases: objects %d %d reference(s)\n", HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "releasing the arrays changed the objects' counts to %d %d",
		  HEAP_REF(w0), HEAP_REF(w1));
	return ae_bad;
}

/* The first struct with a member declared array<wrap<struct>> (one slot per
 * element, a reference): its member index in *member. */
static int ae_wrap_array_struct(int *member)
{
	for (int s = 0; s < ain->nr_structures; s++) {
		struct ain_struct *as = &ain->structures[s];
		for (int i = 0; i < as->nr_members; i++) {
			struct ain_type *t = &as->members[i].type;
			if (t->data == AIN_ARRAY && t->array_type && t->array_type->data == AIN_WRAP
			    && ss_type_slot_count(t->array_type) == 1) {
				*member = i;
				return s;
			}
		}
	}
	printf("ARRAY-ENUM-MEMBER no struct has an array<wrap> member\n");
	exit(2);
}

static int ae4_image(const char *format)
{
	ae_bad = 0;
	int bbc = alloc_struct(sp_sno("BattleBonusCalculator"));
	int arr = sp_get(bbc, "m_bonuses");
	int w0 = oa_obj(), w1 = oa_obj(), w2 = oa_obj();
	int vals[2] = { w0, w1 };
	ae_make_generic(arr, ae_member_struc("BattleBonusCalculator", "m_bonuses"), 2, vals);
	/* the control: a generic page of a member whose elements are references */
	int cm = -1, cs = ae_wrap_array_struct(&cm);
	int ctl = alloc_struct(cs);
	int carr = heap[ctl].page->values[cm].i;
	SP_CHECK(ae_arr(carr), "the control member %s.%s has no page", ain->structures[cs].name,
		 ain->structures[cs].members[cm].name);
	heap_ref(w2);
	ae_make_generic(carr, ae_arr(carr)->array.struct_type, 1, &w2);
	char before[64], cbefore[64];
	ae_meta(ae_arr(arr), before, sizeof before);
	ae_meta(ae_arr(carr), cbefore, sizeof cbefore);

	char file[64];
	snprintf(file, sizeof file, "ae4-%s.dat", format);
	int csp = call_stack_ptr;
	function_call(da_fn("BattleActionPanel::PanelType::GetNameList"), VM_RETURN);
	stack_push(0);
	stack_push(0);
	int ok = vm_save_image("array-enum-member", file);
	stack_pop();
	stack_pop();
	SP_CHECK(call_stack_ptr == csp + 1, "call stack %d before the image", call_stack_ptr);
	SP_CHECK(ok, "the %s image was not written", format);
	vm_load_image("array-enum-member", file);

	char after[64], cafter[64];
	ae_meta(ae_arr(arr), after, sizeof after);
	ae_meta(ae_arr(carr), cafter, sizeof cafter);
	printf("AE4 %s image: m_bonuses %s -> %s; control %s.%s %s -> %s\n", format, before, after,
	       ain->structures[cs].name, ain->structures[cs].members[cm].name, cbefore, cafter);
	AE_EXPECT(da_live_page(bbc) && heap[bbc].page->values[sp_mem(sp_sno("BattleBonusCalculator"), "m_bonuses")].i == arr,
		  "%s: the object did not come back with its array", format);
	AE_EXPECT(ae_holds(arr, w0, w1), "%s: m_bonuses did not come back with its values", format);
	AE_EXPECT(ae_arr(arr) && ae_arr(arr)->a_type == AIN_ARRAY_INT, "%s: m_bonuses came back as %s, want a_type %d",
		  format, after, AIN_ARRAY_INT);
	AE_EXPECT(!strcmp(cbefore, cafter), "%s: the control page came back as %s, was %s", format, cafter, cbefore);
	struct page *cp = ae_arr(carr);
	AE_EXPECT(cp && cp->nr_vars == 1 && cp->values[0].i == w2, "%s: the control page lost its element", format);
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1 && HEAP_REF(w2) == 2,
		  "%s: after the load the objects have %d %d %d reference(s), want 1 1 2", format, HEAP_REF(w0),
		  HEAP_REF(w1), HEAP_REF(w2));
	ae_drop_member(bbc, "m_bonuses");
	heap[ctl].page->values[cm].i = -1;
	heap_unref(carr);
	printf("AE4 %s after the releases: objects %d %d (m_bonuses) %d (control)\n", format, HEAP_REF(w0), HEAP_REF(w1),
	       HEAP_REF(w2));
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "%s: releasing m_bonuses changed the objects' counts to %d %d",
		  format, HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(HEAP_REF(w2) == 1, "%s: releasing the control page left its element %d reference(s), want 1", format,
		  HEAP_REF(w2));
	return ae_bad;
}

static int ae4_image_rsm(void)
{
	config.save_format = SAVE_FORMAT_RSM;
	return ae4_image("rsm");
}

static int ae4_image_json(void)
{
	config.save_format = SAVE_FORMAT_JSON;
	return ae4_image("json");
}

static bool ae_value_page_type(enum ain_data_type t)
{
	return t == AIN_ARRAY_INT || t == AIN_ARRAY_FLOAT;
}

/* AE5 */
static int ae5_table(void)
{
	ae_bad = 0;
	uint32_t at = oa_find_any(X_A_INIT, 0);
	int rows = 0, both = 0, mismatch = 0, enums = 0, skipped = 0;
	for (int s = 0; s < ain->nr_structures; s++) {
		struct ain_struct *as = &ain->structures[s];
		bool any = false;
		for (int i = 0; i < as->nr_members; i++)
			if (as->members[i].type.data == AIN_ARRAY)
				any = true;
		if (!any)
			continue;
		int obj = alloc_struct(s);
		if (!da_live_page(obj) || heap[obj].page->nr_vars < as->nr_members) {
			for (int i = 0; i < as->nr_members; i++)
				if (as->members[i].type.data == AIN_ARRAY) {
					printf("AE5 row %d %s.%s skipped: alloc_struct gave %d slot(s) of %d\n", s, as->name,
					       as->members[i].name, da_live_page(obj) ? heap[obj].page->nr_vars : -1,
					       as->nr_members);
					skipped++;
				}
			continue;
		}
		for (int i = 0; i < as->nr_members; i++) {
			struct ain_type *t = &as->members[i].type;
			if (t->data != AIN_ARRAY)
				continue;
			rows++;
			int elem = t->array_type ? t->array_type->data : -1;
			if (elem == AIN_ENUM)
				enums++;
			struct page *ip = ae_arr(heap[obj].page->values[i].i);
			char im[64], xm[64];
			ae_meta(ip, im, sizeof im);
			size_t ipt = instr_ptr;
			int sp = stack_ptr;
			stack_push(obj);
			stack_push(i);
			stack_push(0);
			instr_ptr = at;
			execute_instruction(X_A_INIT);
			instr_ptr = ipt;
			int xs = stack_pop().i;
			stack_ptr = sp;
			struct page *xp = ae_arr(xs);
			ae_meta(xp, xm, sizeof xm);
			printf("AE5 row %d %s.%s elem=%d isl=%s xai=%s\n", s, as->name, as->members[i].name, elem, im, xm);
			if (!ip || !xp)
				continue;
			both++;
			bool iv = ae_value_page_type(ip->a_type), xv = ae_value_page_type(xp->a_type);
			if (iv != xv || (xv && ip->a_type != xp->a_type)) {
				mismatch++;
				printf("  AE5 mismatch %s.%s: alloc_struct %s, X_A_INIT %s\n", as->name, as->members[i].name, im, xm);
			}
		}
	}
	printf("AE5 %d array member(s), %d with both pages, %d array<enum>, %d skipped, %d disagreement(s) on value "
	       "elements\n", rows, both, enums, skipped, mismatch);
	AE_EXPECT(rows > 0 && both == rows, "%d of %d members have both pages", both, rows);
	AE_EXPECT(skipped == 0, "%d member(s) skipped", skipped);
	AE_EXPECT(mismatch == 0, "%d member(s) where alloc_struct and X_A_INIT disagree on value elements", mismatch);
	return ae_bad;
}

/* AE6 */
static int ae6_copies(void)
{
	ae_bad = 0;
	int obj = alloc_struct(sp_sno("BattleBonusCalculator"));
	int arr = sp_get(obj, "m_bonuses");
	int w0 = oa_obj(), w1 = oa_obj();
	ae_push(arr, w0);
	ae_push(arr, w1);
	SP_CHECK(ae_holds(arr, w0, w1), "PushBack did not store the two values");
	int copy = heap_alloc_page(copy_page(heap[obj].page));
	int carr = sp_get(copy, "m_bonuses");
	struct page *cp = ae_arr(carr);
	printf("AE6 struct copy: m_bonuses slot %d -> %d, values %d %d (were %d %d); objects %d %d reference(s)\n", arr,
	       carr, cp && cp->nr_vars > 0 ? cp->values[0].i : -1, cp && cp->nr_vars > 1 ? cp->values[1].i : -1, w0, w1,
	       HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(carr != arr && ae_holds(carr, w0, w1), "the struct copy's m_bonuses does not hold the two values");
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "the struct copy changed the objects' counts to %d %d",
		  HEAP_REF(w0), HEAP_REF(w1));
	int aref = v14_array_value_copy(heap[arr].page);
	printf("AE6 A_REF copy: slot %d, objects %d %d reference(s)\n", aref, HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(aref > 0 && aref != arr && ae_holds(aref, w0, w1), "the A_REF copy does not hold the two values");
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "the A_REF copy changed the objects' counts to %d %d",
		  HEAP_REF(w0), HEAP_REF(w1));
	if (aref > 0)
		heap_unref(aref);
	ae_drop_member(copy, "m_bonuses");
	ae_drop_member(obj, "m_bonuses");
	printf("AE6 after the releases: objects %d %d reference(s)\n", HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "the releases changed the objects' counts to %d %d",
		  HEAP_REF(w0), HEAP_REF(w1));
	return ae_bad;
}

/* AE7 */
static int ae7_deserialize_no_page(void)
{
	ae_bad = 0;
	int src = sp_new("BattleBonusCalculator");
	ae_push(sp_get(src, "m_bonuses"), 3);
	ae_push(sp_get(src, "m_bonuses"), 8);
	SP_CHECK(sp_sys("SerializeStruct", "T_AE7.asd", src) == 1, "SerializeStruct failed");
	int dst = alloc_struct(sp_sno("BattleBonusCalculator"));
	int arr = sp_get(dst, "m_bonuses");
	struct page *old = heap[arr].page;
	heap[arr].page = NULL;
	if (old)
		free_page(old);
	int r = sp_sys("DeserializeStruct", "T_AE7.asd", dst);
	SP_CHECK(r == 1, "DeserializeStruct returned %d", r);
	int arr2 = sp_get(dst, "m_bonuses");
	char m[64];
	ae_meta(ae_arr(arr2), m, sizeof m);
	printf("AE7 restored into a slot without a page: %s\n", m);
	AE_EXPECT(ae_holds(arr2, 3, 8), "the restored array does not hold 3 and 8");
	AE_EXPECT(ae_arr(arr2) && ae_arr(arr2)->a_type == AIN_ARRAY_INT && ae_arr(arr2)->array.struct_type == -1
		  && ae_arr(arr2)->array.rank == 1 && ae_arr(arr2)->array.elem_slots == 0,
		  "restored page %s, want %d/-1/0/1", m, AIN_ARRAY_INT);
	return ae_bad;
}

/* AE8 */
static int ae8_deserialize_over_values(void)
{
	ae_bad = 0;
	int src = sp_new("BattleBonusCalculator");
	int w0 = oa_obj(), w1 = oa_obj(), x0 = oa_obj(), x1 = oa_obj();
	ae_push(sp_get(src, "m_bonuses"), w0);
	ae_push(sp_get(src, "m_bonuses"), w1);
	SP_CHECK(ae_holds(sp_get(src, "m_bonuses"), w0, w1), "the source does not hold the two values");
	SP_CHECK(sp_sys("SerializeStruct", "T_AE8.asd", src) == 1, "SerializeStruct failed");
	int dst = alloc_struct(sp_sno("BattleBonusCalculator"));
	int arr = sp_get(dst, "m_bonuses");
	int old[2] = { x0, x1 };
	ae_make_generic(arr, ae_member_struc("BattleBonusCalculator", "m_bonuses"), 2, old);
	SP_CHECK(ae_holds(arr, x0, x1), "the generic page does not hold the two old values");
	int r = sp_sys("DeserializeStruct", "T_AE8.asd", dst);
	SP_CHECK(r == 1, "DeserializeStruct returned %d", r);
	int arr2 = sp_get(dst, "m_bonuses");
	char m[64];
	ae_meta(ae_arr(arr2), m, sizeof m);
	printf("AE8 restored over a generic page of values: slot %d -> %d, page %s; old values %d %d, new %d %d "
	       "reference(s)\n", arr, arr2, m, HEAP_REF(x0), HEAP_REF(x1), HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(ae_holds(arr2, w0, w1), "the restored array does not hold the file's two values");
	AE_EXPECT(ae_arr(arr2) && ae_arr(arr2)->a_type == AIN_ARRAY_INT, "restored page %s, want a_type %d", m, AIN_ARRAY_INT);
	AE_EXPECT(HEAP_REF(x0) == 1 && HEAP_REF(x1) == 1, "replacing the generic page changed its values' counts to %d %d",
		  HEAP_REF(x0), HEAP_REF(x1));
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "after the load the file's values have %d %d reference(s)",
		  HEAP_REF(w0), HEAP_REF(w1));
	ae_drop_member(dst, "m_bonuses");
	ae_drop_member(src, "m_bonuses");
	printf("AE8 after both releases: old values %d %d, new %d %d reference(s)\n", HEAP_REF(x0), HEAP_REF(x1),
	       HEAP_REF(w0), HEAP_REF(w1));
	AE_EXPECT(HEAP_REF(x0) == 1 && HEAP_REF(x1) == 1 && HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1,
		  "the releases changed the counts to %d %d %d %d", HEAP_REF(x0), HEAP_REF(x1), HEAP_REF(w0), HEAP_REF(w1));
	return ae_bad;
}

/* AE9 */
static int ae9_deserialize_ref_generic(void)
{
	ae_bad = 0;
	int cm = -1, cs = ae_wrap_array_struct(&cm);
	const char *sn = ain->structures[cs].name, *mn = ain->structures[cs].members[cm].name;
	int src = alloc_struct(cs);
	SP_CHECK(ae_arr(heap[src].page->values[cm].i), "the source member %s.%s has no page", sn, mn);
	SP_CHECK(sp_sys("SerializeStruct", "T_AE9.asd", src) == 1, "SerializeStruct of %s failed", sn);
	int dst = alloc_struct(cs);
	int arr = heap[dst].page->values[cm].i;
	SP_CHECK(ae_arr(arr), "the member %s.%s has no page", sn, mn);
	int st = ae_arr(arr)->array.struct_type;
	/* one empty element (-1, which no release touches), so that a page
	 * left in place would still show it */
	int empty = -1;
	ae_make_generic(arr, st, 1, &empty);
	struct page *gp = heap[arr].page;
	char before[64];
	ae_meta(ae_arr(arr), before, sizeof before);
	/* a page made from the declaration alone would carry the element slot
	 * count (1) in struct_type; the replaced page must differ from it */
	SP_CHECK(st != ss_type_slot_count(ain->structures[cs].members[cm].type.array_type),
		 "%s.%s: the generic page's struct_type %d cannot tell a kept page from a new one", sn, mn, st);
	int r = sp_sys("DeserializeStruct", "T_AE9.asd", dst);
	SP_CHECK(r == 1, "DeserializeStruct of %s returned %d", sn, r);
	int arr2 = heap[dst].page->values[cm].i;
	char after[64];
	ae_meta(ae_arr(arr2), after, sizeof after);
	bool replaced = ae_arr(arr2) && heap[arr2].page != gp;
	printf("AE9 %s.%s restored over a generic page of references: slot %d -> %d, page %s -> %s, "
	       "elements 1 -> %d, page object replaced: %s\n", sn, mn, arr, arr2, before, after,
	       ae_arr(arr2) ? ae_arr(arr2)->nr_vars : -1, replaced ? "yes" : "no");
	AE_EXPECT(replaced, "%s.%s: the member still holds the generic page the probe gave it", sn, mn);
	AE_EXPECT(ae_arr(arr2) && ae_arr(arr2)->nr_vars == 0, "%s.%s: the restored array is not empty", sn, mn);
	AE_EXPECT(!strcmp(before, after), "%s.%s: the restored page is %s, the replaced one was %s", sn, mn, after, before);
	return ae_bad;
}

/* SaveImage and LoadImage as AE4 does them; false when no image was written. */
static bool ae_image_cycle(const char *file)
{
	int csp = call_stack_ptr;
	function_call(da_fn("BattleActionPanel::PanelType::GetNameList"), VM_RETURN);
	stack_push(0);
	stack_push(0);
	int ok = vm_save_image("array-enum-member", file);
	stack_pop();
	stack_pop();
	if (call_stack_ptr != csp + 1) {
		printf("  call stack %d before the image\n", call_stack_ptr);
		return false;
	}
	if (!ok)
		return false;
	vm_load_image("array-enum-member", file);
	return true;
}

/* A generic page with values a and b, not attached to anything. */
static int ae_generic_slot(int struct_type, int a, int b)
{
	struct page *g = alloc_page(ARRAY_PAGE, AIN_ARRAY, 2);
	g->array.rank = 1;
	g->array.struct_type = struct_type;
	g->values[0].i = a;
	g->values[1].i = b;
	return heap_alloc_page(g);
}

/* AE10: kind 'r' a two-dimensional page and 'e' a page of two-slot elements
 * under m_bonuses; 'n' generic pages no struct member names. */
static int ae10_image(const char *format, char kind)
{
	ae_bad = 0;
	int st = ae_member_struc("BattleBonusCalculator", "m_bonuses");
	int w0 = oa_obj(), w1 = oa_obj();
	int pages[3], np = 0, outer = -1;
	if (kind == 'n') {
		pages[np++] = ae_generic_slot(st, w0, w1);
		int inner = ae_generic_slot(st, w0, w1);
		struct page *o = alloc_page(ARRAY_PAGE, AIN_ARRAY, 1);
		o->array.rank = 1;
		o->array.struct_type = st;
		o->values[0].i = inner;
		outer = heap_alloc_page(o);
		pages[np++] = inner;
		pages[np++] = outer;
	} else {
		int bbc = alloc_struct(sp_sno("BattleBonusCalculator"));
		int arr = sp_get(bbc, "m_bonuses");
		int vals[2] = { w0, w1 };
		ae_make_generic(arr, st, 2, vals);
		if (kind == 'r')
			heap[arr].page->array.rank = 2;
		else
			heap[arr].page->array.elem_slots = 2;
		pages[np++] = arr;
	}
	char before[3][64], after[3][64];
	int refs[3];
	for (int i = 0; i < np; i++) {
		ae_meta(ae_arr(pages[i]), before[i], sizeof before[i]);
		refs[i] = HEAP_REF(pages[i]);
	}
	char file[64];
	snprintf(file, sizeof file, "ae10-%s-%c.dat", format, kind);
	SP_CHECK(ae_image_cycle(file), "the %s image was not written", format);
	for (int i = 0; i < np; i++) {
		ae_meta(ae_arr(pages[i]), after[i], sizeof after[i]);
		printf("AE10 %s image, kind %c: page %d %s -> %s, %d -> %d reference(s)\n", format, kind, pages[i], before[i],
		       after[i], refs[i], HEAP_REF(pages[i]));
		AE_EXPECT(!strcmp(before[i], after[i]), "%s: page %d came back as %s, was %s", format, pages[i], after[i],
			  before[i]);
		AE_EXPECT(HEAP_REF(pages[i]) == refs[i], "%s: page %d came back with %d reference(s), was %d", format,
			  pages[i], HEAP_REF(pages[i]), refs[i]);
		if (pages[i] != outer)
			AE_EXPECT(ae_holds(pages[i], w0, w1), "%s: page %d did not come back with its values", format, pages[i]);
	}
	if (outer > 0) {
		struct page *o = ae_arr(outer);
		AE_EXPECT(o && o->nr_vars == 1 && o->values[0].i == pages[1], "%s: the outer array lost its element", format);
	}
	AE_EXPECT(HEAP_REF(w0) == 1 && HEAP_REF(w1) == 1, "%s: after the load the objects have %d %d reference(s)", format,
		  HEAP_REF(w0), HEAP_REF(w1));
	return ae_bad;
}

static int ae10_rank_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ae10_image("rsm", 'r'); }
static int ae10_rank_json(void) { config.save_format = SAVE_FORMAT_JSON; return ae10_image("json", 'r'); }
static int ae10_slots_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ae10_image("rsm", 'e'); }
static int ae10_slots_json(void) { config.save_format = SAVE_FORMAT_JSON; return ae10_image("json", 'e'); }
static int ae10_unnamed_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ae10_image("rsm", 'n'); }
static int ae10_unnamed_json(void) { config.save_format = SAVE_FORMAT_JSON; return ae10_image("json", 'n'); }

/* AE11: kind 'f' a float array and 's' a string array under m_bonuses. */
static bool ae11_values_ok(int slot, char kind)
{
	struct page *p = ae_arr(slot);
	if (!p || p->nr_vars != 2)
		return false;
	if (kind == 'f')
		return p->values[0].f == 1.5f && p->values[1].f == 2.5f;
	static const char *want[2] = { "a", "b" };
	for (int i = 0; i < 2; i++) {
		int s = p->values[i].i;
		if (!string_index_valid(s) || !heap[s].s || strcmp(heap[s].s->text, want[i]))
			return false;
	}
	return true;
}

static int ae11_image(const char *format, char kind)
{
	ae_bad = 0;
	int bbc = alloc_struct(sp_sno("BattleBonusCalculator"));
	int arr = sp_get(bbc, "m_bonuses");
	SP_CHECK(ae_arr(arr), "m_bonuses has no page");
	enum ain_data_type t = kind == 'f' ? AIN_ARRAY_FLOAT : AIN_ARRAY_STRING;
	struct page *c = alloc_page(ARRAY_PAGE, t, 2);
	c->array.rank = 1;
	c->array.struct_type = -1;
	if (kind == 'f') {
		c->values[0].f = 1.5f;
		c->values[1].f = 2.5f;
	} else {
		c->values[0].i = heap_alloc_string(make_string("a", 1));
		c->values[1].i = heap_alloc_string(make_string("b", 1));
	}
	struct page *old = heap[arr].page;
	heap[arr].page = c;
	free_page(old);
	SP_CHECK(ae11_values_ok(arr, kind), "the %c page does not hold its values", kind);
	char before[64], after[64];
	ae_meta(ae_arr(arr), before, sizeof before);
	int ref = HEAP_REF(arr);
	char file[64];
	snprintf(file, sizeof file, "ae11-%s-%c.dat", format, kind);
	SP_CHECK(ae_image_cycle(file), "the %s image was not written", format);
	ae_meta(ae_arr(arr), after, sizeof after);
	printf("AE11 %s image, kind %c: page %d %s -> %s, %d -> %d reference(s)\n", format, kind, arr, before, after, ref,
	       HEAP_REF(arr));
	AE_EXPECT(!strcmp(before, after), "%s: page %d came back as %s, was %s", format, arr, after, before);
	AE_EXPECT(HEAP_REF(arr) == ref, "%s: page %d came back with %d reference(s), was %d", format, arr, HEAP_REF(arr),
		  ref);
	AE_EXPECT(ae11_values_ok(arr, kind), "%s: page %d did not come back with its values", format, arr);
	return ae_bad;
}

static int ae11_float_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ae11_image("rsm", 'f'); }
static int ae11_float_json(void) { config.save_format = SAVE_FORMAT_JSON; return ae11_image("json", 'f'); }
static int ae11_string_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return ae11_image("rsm", 's'); }
static int ae11_string_json(void) { config.save_format = SAVE_FORMAT_JSON; return ae11_image("json", 's'); }

static int test_array_enum_member(void)
{
	sp_setup_dir();
	sp_run("AE1 member without its constructor", ae1_eager);
	sp_run("AE2 BattleContext member chain", ae2_construct_chain);
	sp_run("AE3 DeserializeStruct into a generic page", ae3_deserialize_generic);
	sp_run("AE4 rsm image with a generic value page", ae4_image_rsm);
	sp_run("AE4 json image with a generic value page", ae4_image_json);
	sp_run("AE5 alloc_struct and X_A_INIT agree", ae5_table);
	sp_run("AE6 struct copy and A_REF copy", ae6_copies);
	sp_run("AE7 DeserializeStruct without a page", ae7_deserialize_no_page);
	sp_run("AE8 DeserializeStruct over generic values", ae8_deserialize_over_values);
	sp_run("AE9 DeserializeStruct over generic refs", ae9_deserialize_ref_generic);
	sp_run("AE10 rsm image, two-dimensional page", ae10_rank_rsm);
	sp_run("AE10 json image, two-dimensional page", ae10_rank_json);
	sp_run("AE10 rsm image, two-slot element page", ae10_slots_rsm);
	sp_run("AE10 json image, two-slot element page", ae10_slots_json);
	sp_run("AE10 rsm image, pages no member names", ae10_unnamed_rsm);
	sp_run("AE10 json image, pages no member names", ae10_unnamed_json);
	sp_run("AE11 rsm image, float page of a value member", ae11_float_rsm);
	sp_run("AE11 json image, float page of a value member", ae11_float_json);
	sp_run("AE11 rsm image, string page of a value member", ae11_string_rsm);
	sp_run("AE11 json image, string page of a value member", ae11_string_json);
	sp_remove_dir();
	printf("ARRAY-ENUM-MEMBER summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
