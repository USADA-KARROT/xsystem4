/* Synthetic v13 wrapper diagnostics, include after parts_image_fixture.inc.
 * The AIN and instruction table belong to v14; only its version is set to 13.
 * The loaded AIN provides the Save and Load declarations used here.
 * SaveWithoutHideParts is synthesized from Save with one argument.
 * This checks version dispatch; it does not establish real v13 ABI compatibility.
 * Each case runs in an sp_run child: a sanitizer failure must remain visible,
 * and must not prevent the other five cases from running. In particular the
 * 372c83e wrapper can free a page before releasing its old heap slot. Record
 * base and candidate output separately; do not treat a base crash as a pass.
 */

static bool pv13_wrap_int_array(const struct ain_type *t)
{
	if (t->data != AIN_WRAP || !t->array_type) return false;
	const struct ain_type *a = t->array_type;
	return a->data == AIN_ARRAY_INT || (a->data == AIN_ARRAY && a->array_type
		&& a->array_type->data == AIN_INT);
}

static int pv13_case(const char *name, bool filled)
{
	ci_setup();
	ain->version = 13;
	int lib = ain_get_library(ain, "PartsEngine"), ignored;
	int save_fn = ri_fn("Save", &ignored), load_fn = ri_fn("Load", &ignored);
	if (lib < 0 || save_fn < 0 || load_fn < 0) return 1;
	struct ain_library *l = &ain->libraries[lib];
	struct ain_hll_function *save = &l->functions[save_fn], *load = &l->functions[load_fn];
	if (save->nr_arguments != 2 || load->nr_arguments != 1
	    || !pv13_wrap_int_array(&save->arguments[0].type)
	    || !pv13_wrap_int_array(&load->arguments[0].type)) {
		puts("SYNTHETIC FFI precondition failed: unexpected declarations"); return 1;
	}
	/* Keep the declaration's wrap type and return type; only the name and
	 * declared argument count are hypothetical for the missing operation. */
	int n = l->nr_functions;
	struct ain_hll_function *copy = xcalloc(n + 1, sizeof *copy);
	memcpy(copy, l->functions, n * sizeof *copy);
	copy[n] = copy[save_fn];
	copy[n].name = "SaveWithoutHideParts";
	copy[n].nr_arguments = 1;
	l->functions = copy;
	l->nr_functions = n + 1;
	fp_prelink_parts_engine(); /* Select wrapper then build actual libffi CIF. */
	probe_init_parts_table();
	br_box(PI_P0, 10, 20, 100, 50, true);
	int box = variable_initval(AIN_WRAP).i;
	if (box <= 0 || !pi_page(box) || pi_page(box)->nr_vars != 1) return 1;
	heap_ref(box);
	if (filled) {
		struct page *page = NULL;
		if (!PE_Save(&page) || !page) return 1;
		int inner = heap_alloc_page(page);
		/* Match wrap_set_slot's ownership transfer without relying on the
		 * private inline helper being visible in this probe translation unit. */
		int old = heap[box].page->values[0].i;
		heap[box].page->values[0].i = inner;
		heap_ref(inner);
		if (old > 0) heap_unref(old);
		heap_unref(inner);
	}
	int before = pi_page(box)->values[0].i;
	printf("SYNTHETIC v13 %s box=%s before=%d\n", name, filled ? "valid save" : "empty", before);
	fflush(stdout);
	union vm_value result;
	if (!strcmp(name, "Save")) result = ri_hll(name, 2, (union vm_value[]){ RI_I(box), RI_I(0) });
	else result = ri_hll(name, 1, (union vm_value[]){ RI_I(box) });
	int after = pi_page(box) ? pi_page(box)->values[0].i : -999;
	struct page *p = pi_page(after);
	printf("SYNTHETIC RESULT %s %s return=%d box-inner=%d page=%s words=%d\n",
		name, filled ? "filled" : "empty", result.i, after,
		p ? "present" : "absent", p ? p->nr_vars : -1);
	/* These are baseline consequences for a single empty box or a valid save.
	 * Occupied saves may fail under ASan in either tree; never mask that. */
	if (!strcmp(name, "Load")) return result.i != (int)filled;
	return !result.i || !p || p->nr_vars <= 5
		|| p->values[0].i != 'X' || p->values[1].i != 'P' || p->values[2].i != 'E'
		|| p->values[3].i != 0 || p->values[4].i != 3;
}

static int pv13_save_empty(void) { return pv13_case("Save", false); }
static int pv13_save_filled(void) { return pv13_case("Save", true); }
static int pv13_without_empty(void) { return pv13_case("SaveWithoutHideParts", false); }
static int pv13_without_filled(void) { return pv13_case("SaveWithoutHideParts", true); }
static int pv13_load_empty(void) { return pv13_case("Load", false); }
static int pv13_load_filled(void) { return pv13_case("Load", true); }

static int test_parts_v13_wrap_ffi(void)
{
	puts("SYNTHETIC v13 version guard; SaveWithoutHideParts declaration is hypothetical");
	sp_setup_dir();
	sp_run("v13 FFI Save empty box", pv13_save_empty);
	sp_run("v13 FFI Save filled box", pv13_save_filled);
	sp_run("v13 FFI SaveWithoutHideParts empty box", pv13_without_empty);
	sp_run("v13 FFI SaveWithoutHideParts filled box", pv13_without_filled);
	sp_run("v13 FFI Load empty box", pv13_load_empty);
	sp_run("v13 FFI Load filled box", pv13_load_filled);
	sp_remove_dir();
	printf("PARTS-V13-WRAP-FFI summary: %d/%d failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
