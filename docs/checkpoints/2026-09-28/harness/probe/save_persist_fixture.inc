/* save_persist_fixture.inc — system.SerializeStruct family (2026-09-29).
 *
 * Modes: save-list (Array.SYSTEMONLY_GetStructPageList), save-roundtrip
 * (SerializeStruct / DeserializeStruct, ffi re-entry, option release),
 * save-comment (Write/ReadSerializeStructComment) and the manual helper
 * save-seed (GUI seed files, not in XS4_MODES).
 *
 * Real AIN, real ffi (hll_call) and the real AFL_GameSave_* bytecode
 * wrappers; every case runs in a forked child. Save files go to a private
 * mkdtemp folder under $XS4_SAVE_TMP or $TMPDIR (refused inside XS4_SRC or
 * XS4_MASTER_GAME) that is removed at the end. Symbols that only exist after
 * the fix are looked up with dlsym so that before-check.sh can build this
 * file against older sources. Only ASCII sentinels are used.
 */
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

extern int hll_current_arg3;
void *array_select_function(const struct ain_hll_function *f, void *fallback);
void *hll_shape_select_function(const char *lib, const struct ain_hll_function *f, void *dflt);

static char sp_dir[PATH_MAX];
static int sp_failed_cases, sp_cases;

#define SP_CHECK(c, ...) do {                                                   \
		if (!(c)) {                                                     \
			printf("  FAIL %s:%d: ", __func__, __LINE__);            \
			printf(__VA_ARGS__);                                    \
			printf("\n");                                           \
			fflush(stdout);                                         \
			return 1;                                               \
		}                                                               \
	} while (0)

/* ---- environment -------------------------------------------------------- */

static bool sp_under(const char *path, const char *root_env)
{
	const char *root = getenv(root_env);
	char a[PATH_MAX], b[PATH_MAX];
	if (!root || !root[0] || !realpath(path, a) || !realpath(root, b))
		return false;
	size_t n = strlen(b);
	return !strncmp(a, b, n) && (a[n] == '/' || a[n] == 0);
}

static void sp_setup_dir(void)
{
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !base[0])
		base = getenv("TMPDIR");
	if (!base || !base[0])
		base = "/tmp";
	snprintf(sp_dir, sizeof sp_dir, "%s/xsys4-save-XXXXXX", base);
	if (!mkdtemp(sp_dir)) {
		printf("SAVE cannot create a temporary folder under %s\n", base);
		exit(2);
	}
	if (sp_under(sp_dir, "XS4_SRC") || sp_under(sp_dir, "XS4_MASTER_GAME")) {
		printf("SAVE refused: temporary folder is inside XS4_SRC or XS4_MASTER_GAME\n");
		rmdir(sp_dir);
		exit(2);
	}
	free(config.save_dir);
	config.save_dir = strdup(sp_dir);
}

static void sp_clear_dir(void)
{
	DIR *d = opendir(sp_dir);
	if (!d)
		return;
	struct dirent *e;
	char p[PATH_MAX];
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		snprintf(p, sizeof p, "%s/%s", sp_dir, e->d_name);
		unlink(p);
	}
	closedir(d);
}

static void sp_remove_dir(void)
{
	chmod(sp_dir, 0700);
	sp_clear_dir();
	rmdir(sp_dir);
}

static void sp_run(const char *name, int (*fn)(void))
{
	printf("== %s\n", name);
	fflush(stdout);
	sp_cases++;
	pid_t pid = fork();
	if (pid == 0) {
		int rc = fn();
		fflush(stdout);
		fflush(stderr);
		_exit(rc);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	chmod(sp_dir, 0700);
	sp_clear_dir();
	if (WIFSIGNALED(st)) {
		printf("%-44s FAIL (signal %d)\n", name, WTERMSIG(st));
		sp_failed_cases++;
	} else if (WEXITSTATUS(st)) {
		printf("%-44s FAIL (exit %d)\n", name, WEXITSTATUS(st));
		sp_failed_cases++;
	} else {
		printf("%-44s PASS\n", name);
	}
	fflush(stdout);
}

/* ---- VM helpers ------------------------------------------------------- */

static void *sp_sym(const char *name) { return dlsym(RTLD_DEFAULT, name); }

static int sp_lib(const char *name)
{
	int l = ain_get_library(ain, name);
	assert(l >= 0);
	return l;
}

static int sp_libfn(int lib, const char *name)
{
	int f = ain_get_library_function(ain, lib, name);
	assert(f >= 0);
	return f;
}

static int sp_str(const char *s) { return heap_alloc_string(make_string(s, strlen(s))); }

/* A string argument for a bytecode call, owned by the stack like S_PUSH's
 * (the temp flag hands the reference to the callee's local page). */
static int sp_arg_str(const char *s)
{
	int slot = sp_str(s);
	heap[slot].ref |= HEAP_TEMP_FLAG;
	return slot;
}

static int sp_sno(const char *name)
{
	int s = ain_get_struct(ain, (char *)name);
	if (s < 0) {
		printf("SAVE struct %s not found\n", name);
		exit(2);
	}
	return s;
}

static int sp_mem(int sno, const char *name)
{
	for (int i = 0; i < ain->structures[sno].nr_members; i++)
		if (!strcmp(ain->structures[sno].members[i].name, name))
			return i;
	printf("SAVE member %s.%s not found\n", ain->structures[sno].name, name);
	exit(2);
}

/* alloc_struct + no-argument STRT constructor (what bytecode NEW does). */
static int sp_new(const char *name)
{
	int s = sp_sno(name);
	int obj = alloc_struct(s);
	int ctor = ain->structures[s].constructor;
	if (ctor > 0 && ain->functions[ctor].nr_args == 0)
		call_method0(ctor, obj);
	return obj;
}

static union vm_value *sp_at(int obj, const char *member)
{
	return &heap[obj].page->values[sp_mem(heap[obj].page->index, member)];
}

static int sp_get(int obj, const char *member) { return sp_at(obj, member)->i; }

/* Replace an array's page keeping its metadata. Elements are taken over;
 * the old elements are released when release_old is set. */
static void sp_arr_set(int slot, const int *v, int n, bool release_old)
{
	struct page *old = heap[slot].page;
	struct page *np = alloc_page(ARRAY_PAGE, old ? old->a_type : AIN_ARRAY_INT, n);
	if (old)
		np->array = old->array;
	else
		np->array.struct_type = -1;
	np->array.rank = 1;
	for (int i = 0; i < n; i++)
		np->values[i].i = v[i];
	heap[slot].page = np;
	if (old) {
		int cnt = old->nr_vars;
		int *vals = xmalloc(sizeof(int) * (cnt ? cnt : 1));
		for (int i = 0; i < cnt; i++)
			vals[i] = old->values[i].i;
		free_page(old);
		for (int i = 0; release_old && i < cnt; i++)
			if (vals[i] > 1 && heap_index_valid(vals[i]))
				heap_unref(vals[i]);
		free(vals);
	}
}

static int sp_arr_len(int slot) { return heap[slot].page ? heap[slot].page->nr_vars : 0; }
static int sp_arr_at(int slot, int i) { return heap[slot].page->values[i].i; }

static const char *sp_text(int slot)
{
	return string_index_valid(slot) && heap[slot].s ? heap[slot].s->text : "(null)";
}

static int sp_list(const int *objs, int n)
{
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	p->array.rank = 1;
	p->array.struct_type = -1;
	for (int i = 0; i < n; i++)
		p->values[i].i = objs[i];
	return heap_alloc_page(p);
}

/* system.<name>(fileName, list, true) through ffi; the list is consumed. */
static int sp_sys_list(const char *name, const char *file, int list)
{
	int lib = sp_lib("system"), f = sp_libfn(lib, name);
	int sp = stack_ptr;
	stack_push(sp_str(file));
	stack_push(list);
	stack_push(1);
	hll_call(lib, f, 0);
	assert(stack_ptr == sp + 1);
	return stack_pop().i;
}

static int sp_sys(const char *name, const char *file, int obj)
{
	return sp_sys_list(name, file, sp_list(&obj, 1));
}

/* AFL_GameSave_* wrapper, pushed the way Collection@Save does (S_PUSH file,
 * PUSHSTRUCTPAGE, PUSH 0). Returns the first result slot. */
static int sp_wrapper(const char *fname, const char *file, int obj, int nret)
{
	int fno = ain_get_function(ain, (char *)fname);
	assert(fno >= 0);
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t saved_ip = instr_ptr;
	stack_push(sp_arg_str(file));
	if (obj != INT_MIN) {
		stack_push(obj);
		stack_push(0);
	}
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = saved_ip;
	assert(call_stack_ptr == csp && stack_ptr == sp + nret);
	int r = stack[sp].i;
	return r;
}

static int sp_save(const char *file, int obj)
{
	int r = sp_wrapper("AFL_GameSave_StructSave", file, obj, 1);
	stack_pop();
	return r;
}

static int sp_load(const char *file, int obj)
{
	int r = sp_wrapper("AFL_GameSave_StructLoad", file, obj, 1);
	stack_pop();
	return r;
}

static size_t sp_nonzero_slots(void)
{
	size_t n = 0;
	for (size_t i = 2; i < heap_size; i++)
		if (heap[i].ref != 0)
			n++;
	return n;
}

static char *sp_path(const char *file)
{
	char *p = xmalloc(strlen(sp_dir) + strlen(file) + 2);
	sprintf(p, "%s/%s", sp_dir, file);
	return p;
}

static bool sp_exists(const char *file)
{
	char *p = sp_path(file);
	struct stat st;
	bool ok = stat(p, &st) == 0;
	free(p);
	return ok;
}

/* ---- independent v9 container/body reader (checks only) ------------------ */

static uint8_t *sp_read_bytes(const char *file, size_t *len)
{
	char *p = sp_path(file);
	FILE *fp = fopen(p, "rb");
	free(p);
	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *b = xmalloc(n ? n : 1);
	*len = fread(b, 1, n, fp);
	fclose(fp);
	return b;
}

static void sp_write_bytes(const char *file, const uint8_t *b, size_t len)
{
	char *p = sp_path(file);
	FILE *fp = fopen(p, "wb");
	free(p);
	assert(fp);
	fwrite(b, 1, len, fp);
	fclose(fp);
}

static int32_t sp_le(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static void sp_put_le(uint8_t *p, int32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* decompressed body; *zhdr gets the first two bytes of the zlib stream */
static uint8_t *sp_read_raw(const char *file, size_t *len, uint8_t zhdr[2])
{
	size_t n;
	uint8_t *b = sp_read_bytes(file, &n);
	if (!b || n < 10 || memcmp(b, "GD\x01\x01", 4)) {
		free(b);
		return NULL;
	}
	uLongf raw = (uint32_t)sp_le(b + 4);
	uint8_t *out = xmalloc(raw ? raw : 1);
	if (uncompress(out, &raw, b + 8, n - 8) != Z_OK) {
		free(b);
		free(out);
		return NULL;
	}
	if (zhdr) {
		zhdr[0] = b[8];
		zhdr[1] = b[9];
	}
	free(b);
	*len = raw;
	return out;
}

static void sp_write_raw(const char *file, const uint8_t *raw, size_t len)
{
	uLongf cl = compressBound(len);
	uint8_t *b = xmalloc(cl + 8);
	memcpy(b, "GD\x01\x01", 4);
	sp_put_le(b + 4, (int32_t)len);
	assert(compress2(b + 8, &cl, raw, len, 1) == Z_OK);
	sp_write_bytes(file, b, cl + 8);
	free(b);
}

#define SP_HDR_OFF 17 /* "serialize_struct\0" */

struct sp_body {
	uint8_t *raw;
	size_t len;
	int32_t h[16];
	size_t rec_off[64];      /* offsets of the first records */
	int nrec;
	size_t sd_off;           /* offset of nsd */
	size_t glob_off;
};

static bool sp_body_parse(struct sp_body *b)
{
	if (b->len < SP_HDR_OFF + 64 || memcmp(b->raw, "serialize_struct", 17))
		return false;
	for (int i = 0; i < 16; i++)
		b->h[i] = sp_le(b->raw + SP_HDR_OFF + 4 * i);
	if (b->h[1] != 9 || b->h[2] != 0x40 || b->h[5] < 1 || b->h[5] > 64 || b->h[4] < 0 || b->h[6] < 0
	    || b->h[12] < 0 || b->h[13] < 0 || (size_t)b->h[6] + 34 > b->len)
		return false; /* only small native v9 bodies are inspected here */
	size_t off = (size_t)b->h[4];
	b->nrec = b->h[5];
	for (int r = 0; r < b->nrec; r++) {
		if (off + 8 > b->len)
			return false;
		if (r < 64)
			b->rec_off[r] = off;
		off += 8 + 4 * (size_t)sp_le(b->raw + off + 4);
	}
	b->glob_off = (size_t)b->h[6];
	/* keyvals end = struct definitions */
	b->sd_off = (size_t)b->h[12] + 4 * (size_t)b->h[13];
	return b->sd_off + 4 <= b->len;
}

/* struct definition <sd>: field <k> (type, type2, name) */
static bool sp_sd_field(struct sp_body *b, int sd, int k, int *type, int *type2, const char **name)
{
	size_t off = b->sd_off;
	int nsd = sp_le(b->raw + off);
	off += 4;
	for (int i = 0; i < nsd; i++) {
		off += strlen((char *)b->raw + off) + 1;
		int nf = sp_le(b->raw + off);
		off += 4;
		for (int j = 0; j < nf; j++) {
			int t = sp_le(b->raw + off), t2 = sp_le(b->raw + off + 4);
			const char *nm = (char *)b->raw + off + 8;
			if (i == sd && j == k) {
				*type = t;
				*type2 = t2;
				*name = nm;
				return true;
			}
			off += 8 + strlen(nm) + 1;
		}
	}
	return false;
}

static const char *sp_sd_name(struct sp_body *b, int sd)
{
	size_t off = b->sd_off + 4;
	for (int i = 0; i < sd; i++) {
		off += strlen((char *)b->raw + off) + 1;
		int nf = sp_le(b->raw + off);
		off += 4;
		for (int j = 0; j < nf; j++)
			off += 8 + strlen((char *)b->raw + off + 8) + 1;
	}
	return (char *)b->raw + off;
}

static int sp_rec_sd(struct sp_body *b, int r) { return r >= 0 && r < b->nrec ? sp_le(b->raw + b->rec_off[r]) : -999; }
static int sp_rec_n(struct sp_body *b, int r) { return r >= 0 && r < b->nrec ? sp_le(b->raw + b->rec_off[r] + 4) : -999; }
static int sp_rec_idx(struct sp_body *b, int r, int i)
{
	return r >= 0 && r < b->nrec && i >= 0 && i < sp_rec_n(b, r) ? sp_le(b->raw + b->rec_off[r] + 8 + 4 * i) : -999;
}
static size_t sp_kv_off(struct sp_body *b, int idx)
{
	return idx >= 0 && idx < b->h[13] ? (size_t)b->h[12] + 4 * (size_t)idx : (size_t)b->h[12];
}

/* ---- save-list -------------------------------------------------------- */

static int sp_gspl_lib, sp_gspl_fn;

static int sp_gspl(int list_slot, int arg3)
{
	int sp = stack_ptr;
	stack_push(list_slot);
	hll_call(sp_gspl_lib, sp_gspl_fn, arg3);
	if (stack_ptr != sp + 1) {
		printf("  stack delta %d\n", stack_ptr - sp);
		exit(1);
	}
	return stack_pop().i;
}

static bool sp_valid_list(int r, int n)
{
	return r > 1 && (size_t)r < heap_size && page_index_valid(r) && heap[r].page
		&& heap[r].page->type == ARRAY_PAGE && heap[r].page->nr_vars == n;
}

static int sp_generic_list(const int *v, int n, int struct_type)
{
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY, n);
	p->array.struct_type = struct_type;
	p->array.rank = 1;
	for (int i = 0; i < n; i++)
		p->values[i].i = v[i];
	return heap_alloc_page(p);
}

static void sp_gspl_init(void)
{
	sp_gspl_lib = sp_lib("Array");
	sp_gspl_fn = sp_libfn(sp_gspl_lib, "SYSTEMONLY_GetStructPageList");
}

static int sp_l1_binding(void)
{
	sp_gspl_init();
	void *want = sp_sym("Array_SYSTEMONLY_GetStructPageList_v14");
	printf("  bound=%p v14=%p\n", (void *)libraries[sp_gspl_lib][sp_gspl_fn].fun, want);
	SP_CHECK(want && libraries[sp_gspl_lib][sp_gspl_fn].fun == want, "Array[83] is not the v14 implementation");
	/* F7: the v14 list is only used when both system consumers have the v14 shape */
	int sl = sp_lib("system"), sf = sp_libfn(sl, "SerializeStruct");
	struct ain_hll_function *decl = &ain->libraries[sp_gspl_lib].functions[sp_gspl_fn];
	SP_CHECK(array_select_function(decl, (void *)0x1234) == want, "selector with v14 consumers");
	ain->libraries[sl].functions[sf].arguments[1].type.data = AIN_REF_STRUCT;
	SP_CHECK(array_select_function(decl, (void *)0x1234) == (void *)0x1234,
		 "selector must keep the fallback when system.SerializeStruct has another shape");
	return 0;
}

static int sp_l2_real_shape(void)
{
	sp_gspl_init();
	size_t base = live_slots();
	int obj = sp_new("CASCommonData");
	int ref0 = HEAP_REF(obj);
	int list = sp_generic_list(&obj, 1, 1); /* X_A_INIT 0 layout: one slot */
	int r = sp_gspl(list, 0x10003);
	SP_CHECK(sp_valid_list(r, 1), "result %d is not a one-element array", r);
	struct page *p = heap[r].page;
	SP_CHECK(p->a_type == AIN_ARRAY_INT && p->array.rank == 1 && p->array.struct_type == -1,
		 "a_type=%d rank=%d struct_type=%d", p->a_type, p->array.rank, p->array.struct_type);
	SP_CHECK(p->values[0].i == obj && HEAP_REF(r) == 1 && HEAP_REF(obj) == ref0,
		 "v0=%d obj=%d ref(r)=%d ref(obj)=%d/%d", p->values[0].i, obj, HEAP_REF(r), HEAP_REF(obj), ref0);
	exit_unref(r);
	SP_CHECK(HEAP_REF(obj) == ref0 && heap[obj].page, "releasing the list touched the struct");
	heap_unref(list);
	heap_unref(obj);
	SP_CHECK(live_slots() == base, "live %zu != %zu", live_slots(), base);
	return 0;
}

static int sp_l3_stride2(void)
{
	sp_gspl_init();
	int obj = sp_new("CASCommonData");
	int v[2] = { obj, 0 };
	int r = sp_gspl(sp_generic_list(v, 2, 2), 0x10003);
	SP_CHECK(sp_valid_list(r, 1) && heap[r].page->values[0].i == obj, "stride-2 list");
	return 0;
}

static int sp_l4_invalid(void)
{
	sp_gspl_init();
	int str = sp_str("probe");
	int freed = sp_str("gone");
	heap_unref(freed);
	int cases[4] = { -1, str, freed, 0 };
	for (int i = 0; i < 4; i++) {
		int r = sp_gspl(sp_generic_list(&cases[i], 1, 1), 0x10003);
		SP_CHECK(sp_valid_list(r, 0), "element %d must give an empty list", cases[i]);
	}
	return 0;
}

static int sp_l5_partial(void)
{
	sp_gspl_init();
	int v[2] = { sp_new("CASCommonData"), -1 };
	int r = sp_gspl(sp_generic_list(v, 2, 1), 0x10003);
	SP_CHECK(sp_valid_list(r, 0), "one invalid element must empty the whole list");
	return 0;
}

static int sp_l6_null(void)
{
	sp_gspl_init();
	int r = sp_gspl(-1, 0x10003);
	SP_CHECK(sp_valid_list(r, 0), "null self");
	return 0;
}

static int sp_l7_wrap_box(void)
{
	sp_gspl_init();
	int obj = sp_new("CASCommonData");
	struct page *box = alloc_page(STRUCT_PAGE, -1, 1);
	box->values[0].i = obj;
	int b = heap_alloc_page(box);
	int r = sp_gspl(sp_generic_list(&b, 1, 1), 0x10003);
	SP_CHECK(sp_valid_list(r, 1) && heap[r].page->values[0].i == obj, "wrap box");
	return 0;
}

static int sp_l8_cached_page(void)
{
	sp_gspl_init();
	int obj = sp_new("CASCommonData");
	int ref0 = HEAP_REF(obj);
	struct page *junk = alloc_page(ARRAY_PAGE, AIN_ARRAY, 1);
	junk->array.rank = 2;
	junk->array.struct_type = 77;
	free_page(junk); /* next one-slot page comes from the cache */
	int r = sp_gspl(sp_generic_list(&obj, 1, 1), 0x10003);
	SP_CHECK(sp_valid_list(r, 1), "list");
	SP_CHECK(heap[r].page->array.rank == 1 && heap[r].page->array.struct_type == -1, "stale metadata");
	exit_unref(r);
	SP_CHECK(HEAP_REF(obj) == ref0 && heap[obj].page, "struct released with the list");
	return 0;
}

static int sp_l9_nr, sp_l9_atype, sp_l9_v0;
static bool sp_l9_hook(struct string *fileName, struct page *list, bool saveFolder)
{
	(void)fileName;
	(void)saveFolder;
	sp_l9_nr = list ? list->nr_vars : -1;
	sp_l9_atype = list ? (int)list->a_type : -1;
	sp_l9_v0 = list && list->nr_vars > 0 ? list->values[0].i : -1;
	return true;
}

static int sp_l9_bytecode(void)
{
	int sl = sp_lib("system"), sf = sp_libfn(sl, "SerializeStruct");
	libraries[sl][sf].fun = (void *)sp_l9_hook;
	int obj = sp_new("CASCommonData");
	int r = sp_save("L9.asd", obj);
	printf("  list nr_vars=%d a_type=%d v0=%d obj=%d ret=%d\n", sp_l9_nr, sp_l9_atype, sp_l9_v0, obj, r);
	SP_CHECK(sp_l9_nr == 1 && sp_l9_atype == AIN_ARRAY_INT && sp_l9_v0 == obj,
		 "AFL_GameSave_StructSave did not pass [obj] to SerializeStruct");
	return 0;
}

static int test_save_list(void)
{
	sp_setup_dir();
	sp_run("L1 binding and consumer check", sp_l1_binding);
	sp_run("L2 X_A_INIT list", sp_l2_real_shape);
	sp_run("L3 two-slot list", sp_l3_stride2);
	sp_run("L4 invalid elements", sp_l4_invalid);
	sp_run("L5 one invalid element empties", sp_l5_partial);
	sp_run("L6 null self", sp_l6_null);
	sp_run("L7 wrap box", sp_l7_wrap_box);
	sp_run("L8 cached page metadata", sp_l8_cached_page);
	sp_run("L9 AFL_GameSave_StructSave list", sp_l9_bytecode);
	sp_remove_dir();
	printf("SAVE-LIST summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 1 : 0;
}

/* ---- save-roundtrip --------------------------------------------------- */

static int sp_r1_afcommon_format(void)
{
	int obj = sp_new("CASCommonData");
	heap[obj].page->values[1].i = 0x5A17;
	SP_CHECK(sp_save("T_AFCommon.asd", obj) == 1, "StructSave returned false");
	size_t len;
	uint8_t z[2];
	struct sp_body b = { 0 };
	b.raw = sp_read_raw("T_AFCommon.asd", &len, z);
	SP_CHECK(b.raw, "file missing or not a GD container");
	b.len = len;
	SP_CHECK(z[0] == 0x78 && z[1] == 0x01, "zlib level 1 header, got %02x %02x", z[0], z[1]);
	SP_CHECK(sp_body_parse(&b), "body");
	SP_CHECK(!strcmp((char *)b.raw + SP_HDR_OFF + 64, "serialize_struct"), "group");
	SP_CHECK(b.h[0] == 1000 && b.h[1] == 9 && b.h[2] == 0x40 && b.h[3] == 1, "h0..h3 = %d %d %d %d",
		 b.h[0], b.h[1], b.h[2], b.h[3]);
	SP_CHECK(b.h[15] == 0 && (size_t)b.h[14] == len, "h14=%d len=%zu h15=%d", b.h[14], len, b.h[15]);
	SP_CHECK(b.h[4] == 98, "records at %d", b.h[4]);
	SP_CHECK(b.h[7] == 2, "globals %d", b.h[7]);
	const uint8_t *g = b.raw + b.h[6];
	int root = sp_le(g + 8);
	SP_CHECK(sp_le(g) == 13 && sp_le(g + 4) == 13 && sp_le(g + 12) == 0 && g[16] == 0, "global 0");
	SP_CHECK(sp_le(g + 17) == 0 && sp_le(g + 21) == 0 && sp_le(g + 25) == 0 && sp_le(g + 29) == 0 && g[33] == 0,
		 "global 1 must be all zero");
	SP_CHECK(sp_rec_sd(&b, 0) == -1 && sp_rec_n(&b, 0) == 1 && sp_rec_idx(&b, 0, 0) == 0, "record 0");
	SP_CHECK(root == b.nrec - 1 && sp_rec_n(&b, root) == 2, "root %d nrec %d", root, b.nrec);
	int t, t2;
	const char *nm;
	int sd = sp_rec_sd(&b, root);
	SP_CHECK(!strcmp(sp_sd_name(&b, sd), "CASCommonData"), "sd name %s", sp_sd_name(&b, sd));
	SP_CHECK(sp_sd_field(&b, sd, 0, &t, &t2, &nm) && t == 79 && t2 == 79 && !strcmp(nm, "<vtable>"), "field 0");
	const char *name2 = ain->structures[heap[obj].page->index].members[1].name2;
	SP_CHECK(sp_sd_field(&b, sd, 1, &t, &t2, &nm) && t == 10 && t2 == 10 && !strcmp(nm, name2),
		 "field 1 must carry the raw name2 bytes");
	SP_CHECK(sp_le(b.raw + sp_kv_off(&b, sp_rec_idx(&b, root, 1))) == 0x5A17, "value");
	return 0;
}

static int sp_r2_afcommon_load(void)
{
	size_t base = live_slots(), base_raw = sp_nonzero_slots();
	int obj = sp_new("CASCommonData");
	heap[obj].page->values[1].i = 0x5A17;
	SP_CHECK(sp_save("T_AFCommon.asd", obj) == 1, "save");
	heap[obj].page->values[1].i = 7;
	struct page *pg = heap[obj].page;
	int vt = heap[obj].page->values[0].i;
	SP_CHECK(sp_load("T_AFCommon.asd", obj) == 1, "StructLoad returned false");
	SP_CHECK(heap[obj].page->values[1].i == 0x5A17, "value %d", heap[obj].page->values[1].i);
	SP_CHECK(heap[obj].page == pg && heap[obj].page->values[0].i == vt, "not loaded in place");
	heap_unref(obj);
	printf("  live %zu -> %zu, nonzero-ref slots %zu -> %zu\n", base, live_slots(), base_raw, sp_nonzero_slots());
	SP_CHECK(live_slots() == base, "live slots");
	return 0;
}

static void sp_clear_enum_array(int slot)
{
	/* Pre-existing (R2): an enum array typed AIN_ARRAY releases its values as
	 * slots on deletion. Empty such arrays before teardown. */
	if (heap[slot].page && heap[slot].page->a_type == AIN_ARRAY)
		sp_arr_set(slot, NULL, 0, false);
}

static int sp_config_new(int *gc, int *nsfw, int *ds_list, int *shortcut, int *joy)
{
	int obj = sp_new("CASConfigData");
	*gc = sp_get(obj, "m_gameConfig");
	*nsfw = sp_get(*gc, "m_nsfwConfig");
	int ds = sp_get(*gc, "m_dialogState");
	*ds_list = sp_get(sp_get(ds, "m_dontShowAgain"), "m_list");
	*shortcut = sp_get(*gc, "m_shortcutButton");
	*joy = sp_get(obj, "m_JoystickButtonAssignmentTable");
	return obj;
}

static void sp_config_free(int obj, int ds_list, int shortcut)
{
	sp_clear_enum_array(ds_list);
	sp_clear_enum_array(shortcut);
	heap_unref(obj);
}

static int sp_r3_afconfig(void)
{
	size_t base = live_slots();
	int gc, nsfw, ds_list, shortcut, joy;
	int obj = sp_config_new(&gc, &nsfw, &ds_list, &shortcut, &joy);
	printf("  joy a_type=%d n=%d shortcut a_type=%d dialog a_type=%d\n", heap[joy].page ? heap[joy].page->a_type : -1,
	       sp_arr_len(joy), heap[shortcut].page ? heap[shortcut].page->a_type : -1,
	       heap[ds_list].page ? heap[ds_list].page->a_type : -1);
	sp_at(obj, "MessageSpeedRate")->f = 0.25f;
	int jv[50];
	int jn = sp_arr_len(joy);
	if (jn <= 0 || jn > 50)
		jn = 50;
	for (int i = 0; i < jn; i++)
		jv[i] = 100 + 3 * i;
	sp_arr_set(joy, jv, jn, false);
	int sc[3] = { 17, 27, 31 }, dl[4] = { 0, 1, 2, 4 };
	sp_arr_set(shortcut, sc, 3, false);
	sp_arr_set(ds_list, dl, 4, false);
	sp_at(nsfw, "<ConfigVoiceMutedByNsfw>")->i = 1;
	sp_at(gc, "m_gameSpeedRate")->f = 1.5f;
	SP_CHECK(sp_save("T_AFConfig.asd", obj) == 1, "save");

	int meta_joy = heap[joy].page->a_type, meta_sc = heap[shortcut].page->a_type;
	int st_joy = heap[joy].page->array.struct_type, st_sc = heap[shortcut].page->array.struct_type;
	sp_at(obj, "MessageSpeedRate")->f = 0.75f;
	int z[1] = { 5 };
	sp_arr_set(joy, z, 1, false);
	sp_arr_set(shortcut, z, 1, false);
	sp_arr_set(ds_list, NULL, 0, false);
	sp_at(nsfw, "<ConfigVoiceMutedByNsfw>")->i = 0;
	sp_at(gc, "m_gameSpeedRate")->f = 9.0f;
	size_t mid = live_slots();

	SP_CHECK(sp_load("T_AFConfig.asd", obj) == 1, "load");
	SP_CHECK(sp_at(obj, "MessageSpeedRate")->f == 0.25f, "MessageSpeedRate");
	SP_CHECK(sp_get(obj, "m_gameConfig") == gc && sp_get(gc, "m_nsfwConfig") == nsfw, "sub-structs replaced");
	SP_CHECK(sp_get(obj, "m_JoystickButtonAssignmentTable") == joy && sp_get(gc, "m_shortcutButton") == shortcut,
		 "array slots replaced");
	SP_CHECK(sp_arr_len(joy) == jn, "joy length %d", sp_arr_len(joy));
	for (int i = 0; i < jn; i++)
		SP_CHECK(sp_arr_at(joy, i) == 100 + 3 * i, "joy[%d]", i);
	SP_CHECK(sp_arr_len(shortcut) == 3 && sp_arr_at(shortcut, 0) == 17 && sp_arr_at(shortcut, 2) == 31, "shortcut");
	SP_CHECK(sp_arr_len(ds_list) == 4 && sp_arr_at(ds_list, 3) == 4, "dialog list");
	SP_CHECK(heap[joy].page->a_type == meta_joy && heap[shortcut].page->a_type == meta_sc
		 && heap[joy].page->array.struct_type == st_joy && heap[shortcut].page->array.struct_type == st_sc,
		 "array metadata changed");
	SP_CHECK(sp_get(nsfw, "<ConfigVoiceMutedByNsfw>") == 1 && sp_at(gc, "m_gameSpeedRate")->f == 1.5f, "nested");
	SP_CHECK(live_slots() == mid, "load changed live slots %zu -> %zu", mid, live_slots());
	sp_config_free(obj, ds_list, shortcut);
	SP_CHECK(live_slots() == base, "teardown live %zu != %zu", live_slots(), base);
	return 0;
}

static int sp_achievement_new(int *state, int *list, int *ach)
{
	int obj = alloc_struct(sp_sno("AchievementCollection")); /* its constructor reads EX data */
	*state = sp_new("IdArray<string, AchievementStateContainer>");
	sp_at(obj, "m_state")->i = *state;
	*list = sp_get(*state, "m_list");
	*ach = sp_get(obj, "m_achievements");
	return obj;
}

static int sp_state_entry(const char *id, int state)
{
	int e = alloc_struct(sp_sno("AchievementStateContainer"));
	sp_at(e, "<Id>")->i = sp_str(id);
	sp_at(e, "<State>")->i = state;
	return e;
}

static int sp_r4_achievement(void)
{
	size_t base = live_slots();
	int state, list, ach;
	int obj = sp_achievement_new(&state, &list, &ach);
	int two[2] = { -1, -1 };
	sp_arr_set(ach, two, 2, false);
	int ev[2] = { sp_state_entry("probe01", 2), sp_state_entry("probe02", 1) };
	sp_arr_set(list, ev, 2, true);
	SP_CHECK(sp_save("T_Achievement.asd", obj) == 1, "save");
	size_t len;
	struct sp_body b = { 0 };
	b.raw = sp_read_raw("T_Achievement.asd", &len, NULL);
	b.len = len;
	SP_CHECK(b.raw && sp_body_parse(&b), "file");
	int root = sp_le(b.raw + b.h[6] + 8), t, t2;
	const char *nm;
	SP_CHECK(sp_sd_field(&b, sp_rec_sd(&b, root), 1, &t, &t2, &nm) && t == 79 && t2 == 79 && !nm[0],
		 "m_achievements must be an unnamed (79,79) field");
	int ai = sp_le(b.raw + sp_kv_off(&b, sp_rec_idx(&b, root, 1)));
	size_t ao = (size_t)b.h[10];
	for (int i = 0; i < ai; i++)
		ao += 20 + 4 * (size_t)sp_le(b.raw + ao + 12);
	SP_CHECK(sp_le(b.raw + ao) == 1 && sp_le(b.raw + ao + 4) == 2 && sp_le(b.raw + ao + 16) == 89
		 && sp_le(b.raw + ao + 20) == -1 && sp_le(b.raw + ao + 24) == -1, "m_achievements array entry");

	struct page *ach_page = heap[ach].page;
	sp_arr_set(list, NULL, 0, true);
	SP_CHECK(sp_load("T_Achievement.asd", obj) == 1, "load");
	SP_CHECK(sp_get(state, "m_list") == list && sp_arr_len(list) == 2, "m_state list");
	int e0 = sp_arr_at(list, 0), e1 = sp_arr_at(list, 1);
	SP_CHECK(page_index_valid(e0) && heap[e0].page->index == sp_sno("AchievementStateContainer"), "element type");
	SP_CHECK(!strcmp(sp_text(sp_get(e0, "<Id>")), "probe01") && sp_get(e0, "<State>") == 2, "element 0");
	SP_CHECK(!strcmp(sp_text(sp_get(e1, "<Id>")), "probe02") && sp_get(e1, "<State>") == 1, "element 1");
	SP_CHECK(sp_get(obj, "m_achievements") == ach && heap[ach].page == ach_page && sp_arr_len(ach) == 2,
		 "m_achievements must stay untouched");
	sp_arr_set(ach, NULL, 0, false);
	heap_unref(obj);
	SP_CHECK(live_slots() == base, "teardown live %zu != %zu", live_slots(), base);
	return 0;
}

static int sp_reached(int id, int st)
{
	int r = alloc_struct(sp_sno("ReachedInfo"));
	sp_at(r, "Id")->i = id;
	sp_at(r, "m_state")->i = st;
	return r;
}

static int sp_r5_collection(void)
{
	size_t base = live_slots();
	int obj = sp_new("Collection");
	int ev_list = sp_get(sp_get(sp_get(obj, "m_eventInfo"), "m_event"), "m_list");
	int mr_list = sp_get(sp_get(obj, "m_reachedInfos"), "m_list");
	int strs[3] = { sp_str("ev-a"), sp_str("ev-b"), sp_str("") };
	sp_arr_set(ev_list, strs, 3, true);
	int mri = alloc_struct(sp_sno("MapReachedInfo"));
	sp_at(mri, "Id")->i = sp_str("m1");
	int nodes = sp_new("IdArray<int, ReachedInfo>"), edges = sp_new("IdArray<int, ReachedInfo>");
	int rv = sp_reached(3, 1);
	sp_arr_set(sp_get(nodes, "m_list"), &rv, 1, true);
	sp_at(mri, "m_nodes")->i = nodes;
	sp_at(mri, "m_edges")->i = edges;
	sp_arr_set(mr_list, &mri, 1, true);
	SP_CHECK(sp_save("T_Collection.asd", obj) == 1, "save");
	sp_arr_set(ev_list, NULL, 0, true);
	sp_arr_set(mr_list, NULL, 0, true);
	/* direct ffi call: the load constructs IdArray<int, ReachedInfo> (a VM
	 * constructor runs inside the HLL call) */
	int list = sp_list(&obj, 1), fname = sp_str("T_Collection.asd");
	int lib = sp_lib("system"), f = sp_libfn(lib, "DeserializeStruct");
	int sp = stack_ptr;
	stack_push(fname);
	stack_push(list);
	stack_push(1);
	hll_call(lib, f, 0);
	SP_CHECK(stack_ptr == sp + 1 && stack_pop().i == 1, "DeserializeStruct");
	SP_CHECK(HEAP_REF(fname) == 0 && HEAP_REF(list) == 0, "ffi argument release: fileName ref %d list ref %d",
		 HEAP_REF(fname), HEAP_REF(list));
	SP_CHECK(sp_arr_len(ev_list) == 3 && !strcmp(sp_text(sp_arr_at(ev_list, 1)), "ev-b")
		 && !strcmp(sp_text(sp_arr_at(ev_list, 2)), ""), "event list");
	SP_CHECK(sp_arr_len(mr_list) == 1, "reached list");
	int m = sp_arr_at(mr_list, 0);
	SP_CHECK(page_index_valid(m) && !strcmp(sp_text(sp_get(m, "Id")), "m1"), "MapReachedInfo");
	int n2 = sp_get(m, "m_nodes"), e2 = sp_get(m, "m_edges");
	SP_CHECK(page_index_valid(n2) && page_index_valid(e2) && n2 != e2 && HEAP_REF(n2) == 1 && HEAP_REF(e2) == 1,
		 "constructed IdArray members: %d(ref %d) %d(ref %d)", n2, HEAP_REF(n2), e2, HEAP_REF(e2));
	int nl = sp_get(n2, "m_list");
	SP_CHECK(sp_arr_len(nl) == 1 && sp_get(sp_arr_at(nl, 0), "Id") == 3 && sp_get(sp_arr_at(nl, 0), "m_state") == 1,
		 "ReachedInfo");
	SP_CHECK(sp_arr_len(sp_get(e2, "m_list")) == 0, "edges");
	heap_unref(obj);
	SP_CHECK(live_slots() == base, "teardown live %zu != %zu", live_slots(), base);
	return 0;
}

static int sp_r6_option_delegate(void)
{
	size_t base = live_slots();
	/* option<int>, option<string> */
	int wh = sp_new("WorkerHistory");
	heap[wh].page->values[0].i = 9800;
	heap[wh].page->values[1].i = 0;  /* some */
	heap[wh].page->values[2].i = -1;
	heap[wh].page->values[3].i = 1;  /* none */
	sp_at(wh, "EntryTurn")->i = 12;
	SP_CHECK(sp_sys("SerializeStruct", "T_History.asd", wh) == 1, "save WorkerHistory");
	int wh2 = sp_new("WorkerHistory");
	heap[wh2].page->values[0].i = -1;
	heap[wh2].page->values[1].i = 1;
	heap[wh2].page->values[2].i = sp_str("old");
	heap[wh2].page->values[3].i = 0;
	SP_CHECK(sp_sys("DeserializeStruct", "T_History.asd", wh2) == 1, "load WorkerHistory");
	SP_CHECK(heap[wh2].page->values[0].i == 9800 && heap[wh2].page->values[1].i == 0, "option<int> some");
	SP_CHECK(heap[wh2].page->values[2].i == -1 && heap[wh2].page->values[3].i == 1, "option<string> none");
	SP_CHECK(sp_get(wh2, "EntryTurn") == 12, "EntryTurn");
	size_t len;
	struct sp_body b = { 0 };
	b.raw = sp_read_raw("T_History.asd", &len, NULL);
	b.len = len;
	SP_CHECK(b.raw && sp_body_parse(&b), "file");
	int root = sp_le(b.raw + b.h[6] + 8), sd = sp_rec_sd(&b, root), t, t2;
	const char *nm;
	SP_CHECK(sp_sd_field(&b, sd, 0, &t, &t2, &nm) && t == 86 && t2 == 10, "(86,10)");
	SP_CHECK(sp_sd_field(&b, sd, 1, &t, &t2, &nm) && t == 0 && t2 == 0 && !nm[0], "(0,0,'')");
	SP_CHECK(sp_sd_field(&b, sd, 2, &t, &t2, &nm) && t == 86 && t2 == 12, "(86,12)");
	SP_CHECK(sp_le(b.raw + sp_kv_off(&b, sp_rec_idx(&b, root, 1))) == 0, "flag stored raw");
	free(b.raw);
	heap_unref(wh);
	heap_unref(wh2);

	/* option<struct>: the payload is constructed with the file's struct */
	int ce = sp_new("CustomerEffect");
	int per = alloc_struct(sp_sno("Personality"));
	sp_at(per, "Id")->i = sp_str("p1");
	heap[ce].page->values[2].i = per;
	heap[ce].page->values[3].i = 0;
	SP_CHECK(sp_sys("SerializeStruct", "T_Effect.asd", ce) == 1, "save CustomerEffect");
	int ce2 = sp_new("CustomerEffect");
	heap[ce2].page->values[2].i = -1;
	heap[ce2].page->values[3].i = 1;
	SP_CHECK(sp_sys("DeserializeStruct", "T_Effect.asd", ce2) == 1, "load CustomerEffect");
	int p2 = heap[ce2].page->values[2].i;
	SP_CHECK(page_index_valid(p2) && heap[p2].page->index == sp_sno("Personality") && HEAP_REF(p2) == 1
		 && !strcmp(sp_text(sp_get(p2, "Id")), "p1") && heap[ce2].page->values[3].i == 0, "option<struct> some");
	b.raw = sp_read_raw("T_Effect.asd", &len, NULL);
	b.len = len;
	SP_CHECK(b.raw && sp_body_parse(&b), "file");
	root = sp_le(b.raw + b.h[6] + 8);
	SP_CHECK(sp_sd_field(&b, sp_rec_sd(&b, root), 2, &t, &t2, &nm) && t == 86 && t2 == 13, "(86,13)");
	free(b.raw);
	heap_unref(ce);
	heap_unref(ce2);

	/* delegate: kept */
	int ls = sp_new("LocalSave");
	int dg = heap[ls].page->values[0].i;
	struct page *dgp = page_index_valid(dg) ? heap[dg].page : NULL;
	SP_CHECK(sp_sys("SerializeStruct", "T_LocalSave.asd", ls) == 1, "save LocalSave");
	SP_CHECK(sp_sys("DeserializeStruct", "T_LocalSave.asd", ls) == 1, "load LocalSave");
	SP_CHECK(heap[ls].page->values[0].i == dg && (!dgp || heap[dg].page == dgp), "delegate replaced");
	b.raw = sp_read_raw("T_LocalSave.asd", &len, NULL);
	b.len = len;
	SP_CHECK(b.raw && sp_body_parse(&b), "file");
	root = sp_le(b.raw + b.h[6] + 8);
	SP_CHECK(sp_sd_field(&b, sp_rec_sd(&b, root), 0, &t, &t2, &nm) && t == 63 && t2 == 63 && !nm[0], "(63,63,'')");
	free(b.raw);
	heap_unref(ls);
	SP_CHECK(live_slots() == base, "teardown live %zu != %zu", live_slots(), base);
	return 0;
}

static int sp_r7_null_members(void)
{
	int hs = alloc_struct(sp_sno("DungeonHotSpot")); /* string member -1 */
	SP_CHECK(sp_sys("SerializeStruct", "T_Spot.asd", hs) == 1, "save with a -1 string");
	int hs2 = alloc_struct(sp_sno("DungeonHotSpot"));
	int s = sp_str("keep?");
	heap_ref(s);
	sp_at(hs2, "m_dungeonId")->i = s;
	SP_CHECK(sp_sys("DeserializeStruct", "T_Spot.asd", hs2) == 1, "load");
	SP_CHECK(sp_get(hs2, "m_dungeonId") == -1 && HEAP_REF(s) == 1, "string member must be released and -1");
	int cfg = alloc_struct(sp_sno("CASConfigData")); /* m_gameConfig -1 */
	SP_CHECK(sp_sys("SerializeStruct", "T_NullCfg.asd", cfg) == 1, "save with a -1 struct");
	int gc, nsfw, dl, sc, joy;
	int cfg2 = sp_config_new(&gc, &nsfw, &dl, &sc, &joy);
	heap_ref(gc);
	SP_CHECK(sp_sys("DeserializeStruct", "T_NullCfg.asd", cfg2) == 1, "load");
	SP_CHECK(sp_get(cfg2, "m_gameConfig") == -1 && HEAP_REF(gc) == 1, "struct member must be released and -1");
	sp_clear_enum_array(dl);
	sp_clear_enum_array(sc);
	return 0;
}

static int sp_r8_empty_list(void)
{
	SP_CHECK(sp_sys_list("SerializeStruct", "T_Empty.asd", sp_list(NULL, 0)) == 0, "Serialize(empty) must be false");
	SP_CHECK(!sp_exists("T_Empty.asd"), "Serialize(empty) wrote a file");
	SP_CHECK(sp_sys_list("SerializeStruct", "T_Empty.asd", -1) == 0, "Serialize(null) must be false");
	int obj = sp_new("CASCommonData");
	SP_CHECK(sp_save("T_Empty.asd", obj) == 1, "save");
	SP_CHECK(sp_sys_list("DeserializeStruct", "T_Empty.asd", sp_list(NULL, 0)) == 0, "Deserialize(empty)");
	SP_CHECK(sp_sys_list("DeserializeStruct", "T_Empty.asd", -1) == 0, "Deserialize(null)");
	return 0;
}

/* Variants of a valid file; each load must return false and leave the destination untouched. */
static int sp_r9_try(const char *what, int dest_kind)
{
	int gc, nsfw, dl, sc, joy;
	int obj = dest_kind ? sp_config_new(&gc, &nsfw, &dl, &sc, &joy) : sp_new("CASCommonData");
	if (dest_kind) {
		sp_at(obj, "MessageSpeedRate")->f = 0.5f;
		sp_at(gc, "m_gameSpeedRate")->f = 3.0f;
	} else {
		heap[obj].page->values[1].i = 0x1234;
	}
	size_t live = live_slots();
	int r = sp_sys("DeserializeStruct", "T_Bad.asd", obj);
	bool untouched = dest_kind
		? sp_at(obj, "MessageSpeedRate")->f == 0.5f && sp_get(obj, "m_gameConfig") == gc
			&& sp_at(gc, "m_gameSpeedRate")->f == 3.0f
		: heap[obj].page->values[1].i == 0x1234;
	printf("  %-34s ret=%d untouched=%d live %+ld\n", what, r, untouched, (long)live_slots() - (long)live);
	SP_CHECK(r == 0 && untouched && live_slots() == live, "%s", what);
	if (dest_kind) {
		sp_clear_enum_array(dl);
		sp_clear_enum_array(sc);
	}
	return 0;
}

static int sp_r9_corrupt(void)
{
	int obj = sp_new("CASCommonData");
	heap[obj].page->values[1].i = 0x5A17;
	SP_CHECK(sp_save("T_Good.asd", obj) == 1, "save");
	size_t len, flen;
	uint8_t *raw = sp_read_raw("T_Good.asd", &len, NULL);
	uint8_t *file = sp_read_bytes("T_Good.asd", &flen);
	SP_CHECK(raw && file, "files");
	struct sp_body b = { .raw = raw, .len = len };
	SP_CHECK(sp_body_parse(&b), "parse");
	uint8_t *m = xmalloc(len + 64);
	size_t cuts[5] = { 1, 8, 20, 97, (size_t)b.h[14] - 1 };
	char label[64];
	for (int i = 0; i < 5; i++) {
		sp_write_raw("T_Bad.asd", raw, cuts[i]);
		snprintf(label, sizeof label, "body truncated to %zu", cuts[i]);
		if (sp_r9_try(label, 0))
			return 1;
	}
	memcpy(m, file, flen);
	m[0] = 'X';
	sp_write_bytes("T_Bad.asd", m, flen);
	if (sp_r9_try("container signature", 0))
		return 1;
	memcpy(m, file, flen);
	sp_put_le(m + 4, 0x7fffffff);
	sp_write_bytes("T_Bad.asd", m, flen);
	if (sp_r9_try("raw size 0x7fffffff", 0))
		return 1;
	sp_write_bytes("T_Bad.asd", file, 12);
	if (sp_r9_try("container truncated", 0))
		return 1;
	struct { const char *what; size_t off; int32_t v; bool byte; } edits[] = {
		{ "key", 0, 'x', true },
		{ "version 6", SP_HDR_OFF + 4, 6, false },
		{ "version 10", SP_HDR_OFF + 4, 10, false },
		{ "magic 999", SP_HDR_OFF, 999, false },
		{ "group", SP_HDR_OFF + 64, 'x', true },
		{ "record index", b.rec_off[b.nrec - 1] + 8, 99999, false },
		{ "root record out of range", (size_t)b.h[6] + 8, 99, false },
		{ "globals fewer than roots", SP_HDR_OFF + 28, 0, false },
		{ "array shape (flat n != dims)", (size_t)b.h[10] + 12, sp_le(raw + b.h[10] + 4) + 1, false },
	};
	for (size_t i = 0; i < sizeof edits / sizeof edits[0]; i++) {
		memcpy(m, raw, len);
		if (edits[i].byte)
			m[edits[i].off] = (uint8_t)edits[i].v;
		else
			sp_put_le(m + edits[i].off, edits[i].v);
		sp_write_raw("T_Bad.asd", m, len);
		if (sp_r9_try(edits[i].what, 0))
			return 1;
	}
	free(raw);
	free(file);

	/* F2: nested struct references in a two-level file */
	int gc, nsfw, dl, sc, joy;
	int cfg = sp_config_new(&gc, &nsfw, &dl, &sc, &joy);
	SP_CHECK(sp_save("T_GoodCfg.asd", cfg) == 1, "save config");
	raw = sp_read_raw("T_GoodCfg.asd", &len, NULL);
	struct sp_body c = { .raw = raw, .len = len };
	SP_CHECK(raw && sp_body_parse(&c), "parse config");
	int root = sp_le(raw + c.h[6] + 8);
	int mgc = sp_mem(sp_sno("CASConfigData"), "m_gameConfig");
	size_t kvo = sp_kv_off(&c, sp_rec_idx(&c, root, mgc));
	int32_t nested[4] = { 0, c.nrec, -2, 0x7fffffff };
	m = xrealloc(m, len + 64);
	for (int i = 0; i < 4; i++) {
		memcpy(m, raw, len);
		sp_put_le(m + kvo, nested[i]);
		sp_write_raw("T_Bad.asd", m, len);
		snprintf(label, sizeof label, "nested struct reference %d", nested[i]);
		if (sp_r9_try(label, 1))
			return 1;
	}
	sp_clear_enum_array(dl);
	sp_clear_enum_array(sc);
	heap_unref(cfg);

	/* F5: an apply-time failure (unknown struct name when the member must be
	 * created) returns false without leaking references. The original engine
	 * does not roll back: root fields before the failure stay loaded. */
	size_t base = live_slots();
	int cfg2 = sp_config_new(&gc, &nsfw, &dl, &sc, &joy);
	sp_at(cfg2, "MessageSpeedRate")->f = 0.25f;
	SP_CHECK(sp_save("T_GoodCfg.asd", cfg2) == 1, "save config");
	free(raw);
	raw = sp_read_raw("T_GoodCfg.asd", &len, NULL);
	uint8_t *hit = memmem(raw, len, "GameConfig", 11); /* includes the NUL: the struct definition name */
	SP_CHECK(hit, "GameConfig definition");
	hit[9] = 'X';
	sp_write_raw("T_Bad.asd", raw, len);
	sp_at(cfg2, "MessageSpeedRate")->f = 0.75f;
	sp_at(cfg2, "m_gameConfig")->i = -1;
	sp_clear_enum_array(sc);
	sp_clear_enum_array(dl);
	heap_unref(gc);
	size_t live = live_slots();
	int r = sp_sys("DeserializeStruct", "T_Bad.asd", cfg2);
	printf("  unknown struct during apply ret=%d MessageSpeedRate=%g m_gameConfig=%d live %+ld\n", r,
	       sp_at(cfg2, "MessageSpeedRate")->f, sp_get(cfg2, "m_gameConfig"), (long)live_slots() - (long)live);
	SP_CHECK(r == 0 && sp_get(cfg2, "m_gameConfig") == -1 && live_slots() == live, "apply-time failure");
	heap_unref(cfg2);
	SP_CHECK(live_slots() == base, "teardown live %zu != %zu", live_slots(), base);
	free(raw);
	free(m);
	return 0;
}

static int sp_r10_other_struct(void)
{
	int obj = sp_new("CASCommonData");
	heap[obj].page->values[1].i = 0x5A17;
	SP_CHECK(sp_save("T_AFCommon.asd", obj) == 1, "save");
	int info = sp_new("_system::detail::CAFInfo");
	for (int i = 1; i < 8; i++)
		heap[info].page->values[i].i = 40 + i;
	SP_CHECK(sp_sys("DeserializeStruct", "T_AFCommon.asd", info) == 1, "struct names are not compared");
	for (int i = 1; i < 8; i++)
		SP_CHECK(heap[info].page->values[i].i == 40 + i, "unmatched field %d changed", i);
	return 0;
}

static int sp_r11_list_longer(void)
{
	int obj = sp_new("CASCommonData");
	heap[obj].page->values[1].i = 0x5A17;
	SP_CHECK(sp_save("T_AFCommon.asd", obj) == 1, "save");
	int a = sp_new("CASCommonData"), c = sp_new("CASCommonData");
	heap[a].page->values[1].i = 1;
	heap[c].page->values[1].i = 2;
	int two[2] = { a, c };
	SP_CHECK(sp_sys_list("DeserializeStruct", "T_AFCommon.asd", sp_list(two, 2)) == 0, "must be false");
	SP_CHECK(heap[a].page->values[1].i == 1 && heap[c].page->values[1].i == 2, "destination changed");
	return 0;
}

static int sp_r12_shared(void)
{
	int mri = alloc_struct(sp_sno("MapReachedInfo"));
	sp_at(mri, "Id")->i = sp_str("shared");
	int ia = sp_new("IdArray<int, ReachedInfo>");
	int rv = sp_reached(7, 2);
	sp_arr_set(sp_get(ia, "m_list"), &rv, 1, true);
	sp_at(mri, "m_nodes")->i = ia;
	sp_at(mri, "m_edges")->i = ia;
	heap_ref(ia);
	SP_CHECK(sp_sys("SerializeStruct", "T_Shared.asd", mri) == 1, "save");
	size_t len;
	struct sp_body b = { 0 };
	b.raw = sp_read_raw("T_Shared.asd", &len, NULL);
	b.len = len;
	SP_CHECK(b.raw && sp_body_parse(&b) && b.nrec == 1 + 1 + 2 + 2, "a shared object is written once per reference (records %d)",
		 b.nrec);
	sp_arr_set(sp_get(ia, "m_list"), NULL, 0, true);
	SP_CHECK(sp_sys("DeserializeStruct", "T_Shared.asd", mri) == 1, "load");
	SP_CHECK(sp_get(mri, "m_nodes") == ia && sp_get(mri, "m_edges") == ia, "struct members stay shared");
	int l = sp_get(ia, "m_list");
	SP_CHECK(sp_arr_len(l) == 1 && sp_get(sp_arr_at(l, 0), "Id") == 7, "contents");
	return 0;
}

static int sp_r13_atomic(void)
{
	int obj = sp_new("CASCommonData");
	heap[obj].page->values[1].i = 1;
	SP_CHECK(sp_sys("SerializeStruct", "T_Atomic.asd", obj) == 1, "first save");
	size_t n1, n2;
	uint8_t *before = sp_read_bytes("T_Atomic.asd", &n1);
	chmod(sp_dir, 0500);
	heap[obj].page->values[1].i = 2;
	int r = sp_sys("SerializeStruct", "T_Atomic.asd", obj);
	chmod(sp_dir, 0700);
	uint8_t *after = sp_read_bytes("T_Atomic.asd", &n2);
	SP_CHECK(r == 0, "save into a read-only folder must fail");
	SP_CHECK(after && n1 == n2 && !memcmp(before, after, n1), "the previous file changed");
	SP_CHECK(!sp_exists("T_Atomic.asd.xs4tmp"), "temporary file left behind");
	return 0;
}

static int sp_r14_selector(void)
{
	int sl = sp_lib("system");
	const char *names[4] = { "SerializeStruct", "DeserializeStruct", "WriteSerializeStructComment",
				 "ReadSerializeStructComment" };
	for (int i = 0; i < 4; i++) {
		char sym[80];
		snprintf(sym, sizeof sym, "system_%s_v14", names[i]);
		void *want = sp_sym(sym);
		int f = sp_libfn(sl, names[i]);
		SP_CHECK(want && libraries[sl][f].fun == want, "system.%s is not bound to %s", names[i], sym);
	}
	struct ain_hll_function copy = ain->libraries[sl].functions[sp_libfn(sl, "SerializeStruct")];
	struct ain_hll_argument args[3];
	memcpy(args, copy.arguments, sizeof args);
	copy.arguments = args;
	void *dflt = (void *)0x1234;
	args[1].type.data = AIN_REF_STRUCT;
	SP_CHECK(hll_shape_select_function("system", &copy, dflt) == dflt, "(string, ref struct, bool) must keep dflt");
	memcpy(args, ain->libraries[sl].functions[sp_libfn(sl, "SerializeStruct")].arguments, sizeof args);
	copy.return_type.data = AIN_VOID;
	SP_CHECK(hll_shape_select_function("system", &copy, dflt) == dflt, "void return must keep dflt");
	return 0;
}

/* R15 (optional): copies of original-engine files named by XS4_SAVE_ORIG_COPY
 * (a folder with copies, never the original folder). */
static int sp_r15_original(void)
{
	const char *src = getenv("XS4_SAVE_ORIG_COPY");
	if (!src || !src[0]) {
		printf("  SKIP: XS4_SAVE_ORIG_COPY not set\n");
		return 0;
	}
	const char *files[5] = { "AFConfig.asd", "Collection.asd", "Achievement.asd", "AFCommon.asd", "AFInfo.asd" };
	for (int i = 0; i < 5; i++) {
		char p[PATH_MAX];
		snprintf(p, sizeof p, "%s/%s", src, files[i]);
		FILE *in = fopen(p, "rb");
		if (!in) {
			printf("  %s: not in the copy folder\n", files[i]);
			continue;
		}
		uint8_t buf[1 << 16];
		size_t n;
		char *dst = sp_path(files[i]);
		FILE *out = fopen(dst, "wb");
		free(dst);
		while ((n = fread(buf, 1, sizeof buf, in)) > 0)
			fwrite(buf, 1, n, out);
		fclose(in);
		fclose(out);
		int obj, extra = -1, extra2 = -1;
		if (i == 0) {
			int gc, nsfw, dl, sc, joy;
			obj = sp_config_new(&gc, &nsfw, &dl, &sc, &joy);
			extra = dl;
			extra2 = sc;
		} else if (i == 1) {
			obj = sp_new("Collection");
		} else if (i == 2) {
			int st, l, a;
			obj = sp_achievement_new(&st, &l, &a);
		} else {
			obj = sp_new(i == 3 ? "CASCommonData" : "_system::detail::CAFInfo");
		}
		int r = sp_sys("DeserializeStruct", files[i], obj);
		printf("  %s ret=%d\n", files[i], r);
		SP_CHECK(r == 1, "original %s", files[i]);
		/* informational: write the loaded object back and compare bodies */
		char rt[64];
		snprintf(rt, sizeof rt, "RT_%s", files[i]);
		SP_CHECK(sp_sys("SerializeStruct", rt, obj) == 1, "re-save %s", files[i]);
		size_t la, lb;
		uint8_t *ra = sp_read_raw(files[i], &la, NULL), *rb = sp_read_raw(rt, &lb, NULL);
		size_t d = 0;
		while (ra && rb && d < la && d < lb && ra[d] == rb[d])
			d++;
		printf("    re-saved body: original %zu bytes, xsystem4 %zu bytes, %s", la, lb,
		       ra && rb && la == lb && d == la ? "identical" : "first difference at ");
		if (!(ra && rb && la == lb && d == la))
			printf("%zu", d);
		printf("\n");
		free(ra);
		free(rb);
		if (i == 0) {
			int gc = sp_get(obj, "m_gameConfig");
			printf("    MessageSpeedRate=%g MoveMouseCursorSpeedID=%d joy=%d shortcut=%d nsfw.muted=%d\n",
			       sp_at(obj, "MessageSpeedRate")->f, sp_get(obj, "MoveMouseCursorSpeedID"),
			       sp_arr_len(sp_get(obj, "m_JoystickButtonAssignmentTable")),
			       sp_arr_len(sp_get(gc, "m_shortcutButton")),
			       sp_get(sp_get(gc, "m_nsfwConfig"), "<ConfigVoiceMutedByNsfw>"));
			sp_clear_enum_array(extra);
			sp_clear_enum_array(extra2);
		} else if (i == 1) {
			int ev = sp_get(sp_get(sp_get(obj, "m_eventInfo"), "m_event"), "m_list");
			int mr = sp_get(sp_get(obj, "m_reachedInfos"), "m_list");
			printf("    events=%d reached=%d\n", sp_arr_len(ev), sp_arr_len(mr));
		} else if (i == 2) {
			int l = sp_get(sp_get(obj, "m_state"), "m_list");
			printf("    achievement states=%d\n", sp_arr_len(l));
			for (int k = 0; k < sp_arr_len(l) && k < 3; k++)
				printf("    [%d] %s state=%d\n", k, sp_text(sp_get(sp_arr_at(l, k), "<Id>")),
				       sp_get(sp_arr_at(l, k), "<State>"));
		}
	}
	return 0;
}

/* ffi re-entry (F1): an HLL function that runs a VM constructor must not
 * disturb the release of its by-value arguments. */
static int sp_r16_no, sp_r16_new = -1;
static bool sp_r16_hook(struct string *fileName, struct page *list, bool saveFolder)
{
	(void)fileName;
	(void)list;
	(void)saveFolder;
	int s = alloc_struct(sp_r16_no);
	heap_ref(s);
	vm_call(ain->structures[sp_r16_no].constructor, s);
	if (HEAP_REF(s) > 1)
		heap_unref(s);
	sp_r16_new = s;
	return true;
}

static int sp_r16_reentry(void)
{
	sp_r16_no = sp_sno("CASCommonData");
	int lib = sp_lib("system"), f = sp_libfn(lib, "DeserializeStruct");
	libraries[lib][f].fun = (void *)sp_r16_hook;
	int dest = sp_new("CASCommonData");
	int fname = sp_str("probe.asd"), list = sp_list(&dest, 1);
	int sp = stack_ptr;
	stack_push(fname);
	stack_push(list);
	stack_push(1);
	hll_call(lib, f, 0);
	SP_CHECK(stack_ptr == sp + 1, "stack");
	stack_pop();
	printf("  fileName ref=%d list ref=%d dest ref=%d new ref=%d\n", HEAP_REF(fname), HEAP_REF(list),
	       HEAP_REF(dest), HEAP_REF(sp_r16_new));
	SP_CHECK(HEAP_REF(fname) == 0 && HEAP_REF(list) == 0 && HEAP_REF(dest) == 1 && HEAP_REF(sp_r16_new) == 1,
		 "arguments released from overwritten stack slots");
	return 0;
}

/* R1: deleting a struct must not release an option<int> payload as a slot. */
static int sp_r17_option_int_delete(void)
{
	int victim = sp_str("victim");
	int wh = alloc_struct(sp_sno("WorkerHistory"));
	heap[wh].page->values[0].i = victim; /* some(<victim's slot number>) as a plain int */
	heap[wh].page->values[1].i = 0;
	heap_unref(wh);
	SP_CHECK(HEAP_REF(victim) == 1, "option<int> payload released as a slot (ref %d)", HEAP_REF(victim));
	return 0;
}

/* option<wrap<struct>>: the file stores -1 for the payload and the flag raw.
 * A "some" flag is applied only when the destination already holds a payload
 * (the original engine would leave some(-1) otherwise). */
static int sp_r18_option_wrap(void)
{
	int sno = sp_sno("PlayerAction");
	int a = alloc_struct(sno), target = alloc_struct(sp_sno("ActionTargetCollection"));
	heap[a].page->values[0].i = target;
	heap[a].page->values[1].i = 0; /* some */
	SP_CHECK(sp_sys("SerializeStruct", "T_Action.asd", a) == 1, "save");
	size_t len;
	struct sp_body b = { 0 };
	b.raw = sp_read_raw("T_Action.asd", &len, NULL);
	b.len = len;
	SP_CHECK(b.raw && sp_body_parse(&b), "file");
	int root = sp_le(b.raw + b.h[6] + 8), t, t2;
	const char *nm;
	SP_CHECK(sp_sd_field(&b, sp_rec_sd(&b, root), 0, &t, &t2, &nm) && t == 86 && t2 == AIN_REF_STRUCT, "(86,21)");
	SP_CHECK(sp_le(b.raw + sp_kv_off(&b, sp_rec_idx(&b, root, 0))) == -1
		 && sp_le(b.raw + sp_kv_off(&b, sp_rec_idx(&b, root, 1))) == 0, "payload -1, flag 0");
	free(b.raw);
	int none = alloc_struct(sno);
	heap[none].page->values[0].i = -1;
	heap[none].page->values[1].i = 1;
	SP_CHECK(sp_sys("DeserializeStruct", "T_Action.asd", none) == 1, "load into none");
	SP_CHECK(heap[none].page->values[0].i == -1 && heap[none].page->values[1].i == 1, "none must stay none, got [%d, %d]",
		 heap[none].page->values[0].i, heap[none].page->values[1].i);
	int some = alloc_struct(sno), x = alloc_struct(sp_sno("ActionTargetCollection"));
	heap[some].page->values[0].i = x;
	heap[some].page->values[1].i = 0;
	SP_CHECK(sp_sys("DeserializeStruct", "T_Action.asd", some) == 1, "load into some");
	SP_CHECK(heap[some].page->values[0].i == x && heap[some].page->values[1].i == 0 && HEAP_REF(x) == 1,
		 "some(x) must keep x");
	return 0;
}

static int test_save_roundtrip(void)
{
	setenv("XSYS4_TRACE_SAVE", "1", 1);
	sp_setup_dir();
	sp_run("R1 AFCommon v9 format", sp_r1_afcommon_format);
	sp_run("R2 AFCommon in-place load", sp_r2_afcommon_load);
	sp_run("R3 AFConfig nested round trip", sp_r3_afconfig);
	sp_run("R4 Achievement round trip", sp_r4_achievement);
	sp_run("R5 Collection with constructed members", sp_r5_collection);
	sp_run("R6 option and delegate fields", sp_r6_option_delegate);
	sp_run("R7 -1 members", sp_r7_null_members);
	sp_run("R8 empty and null lists", sp_r8_empty_list);
	sp_run("R9 corrupt files", sp_r9_corrupt);
	sp_run("R10 other struct type", sp_r10_other_struct);
	sp_run("R11 list longer than globals", sp_r11_list_longer);
	sp_run("R12 shared sub-object", sp_r12_shared);
	sp_run("R13 failed write keeps the old file", sp_r13_atomic);
	sp_run("R14 declaration-shape selection", sp_r14_selector);
	sp_run("R15 original-engine copies (optional)", sp_r15_original);
	sp_run("R16 ffi argument release after re-entry", sp_r16_reentry);
	sp_run("R17 option<int> payload on delete", sp_r17_option_int_delete);
	sp_run("R18 option<wrap<struct>> flag", sp_r18_option_wrap);
	sp_remove_dir();
	printf("SAVE-ROUNDTRIP summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 1 : 0;
}

/* ---- save-comment ----------------------------------------------------- */

#define SP_COMMENT "0|2|1|true|2026/09/29 00:00:00|probe|ep"

static int sp_write_comment(const char *file, const char *text)
{
	int fno = ain_get_function(ain, "AFL_GameSave_WriteStructSaveComment");
	assert(fno >= 0);
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t saved_ip = instr_ptr;
	stack_push(sp_arg_str(file));
	stack_push(sp_arg_str(text));
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = saved_ip;
	assert(call_stack_ptr == csp && stack_ptr == sp + 1);
	return stack_pop().i;
}

/* option<string>: returns the flag (0 = some), *text the string slot */
static int sp_read_comment(const char *file, int *text)
{
	int r = sp_wrapper("AFL_GameSave_ReadStructSaveComment", file, INT_MIN, 2);
	(void)r;
	int flag = stack_pop().i;
	*text = stack_pop().i;
	return flag;
}

static int sp_c1_write_read(void)
{
	int obj = sp_new("CASCommonData");
	SP_CHECK(sp_save("T_Cmt.asd", obj) == 1, "save");
	size_t l0, l1;
	uint8_t *raw0 = sp_read_raw("T_Cmt.asd", &l0, NULL);
	SP_CHECK(sp_write_comment("T_Cmt.asd", SP_COMMENT) == 1, "WriteStructSaveComment returned false");
	uint8_t *raw1 = sp_read_raw("T_Cmt.asd", &l1, NULL);
	size_t n = strlen(SP_COMMENT);
	SP_CHECK(raw0 && raw1 && l1 == l0 + 4 + n, "length %zu -> %zu", l0, l1);
	size_t h15 = SP_HDR_OFF + 60;
	SP_CHECK(sp_le(raw0 + h15) == 0 && sp_le(raw1 + h15) == 1, "h15 flag");
	SP_CHECK(!memcmp(raw0, raw1, h15) && !memcmp(raw0 + h15 + 4, raw1 + h15 + 4, l0 - h15 - 4),
		 "bytes before the trailer changed (other than h15)");
	SP_CHECK(sp_le(raw1 + l0) == (int32_t)n && !memcmp(raw1 + l0 + 4, SP_COMMENT, n), "trailer");
	int text;
	int flag = sp_read_comment("T_Cmt.asd", &text);
	SP_CHECK(flag == 0 && !strcmp(sp_text(text), SP_COMMENT), "read flag=%d text=%s", flag, sp_text(text));
	return 0;
}

static int sp_c2_no_comment(void)
{
	int obj = sp_new("CASCommonData");
	SP_CHECK(sp_save("T_Cmt.asd", obj) == 1, "save");
	int text;
	int flag = sp_read_comment("T_Cmt.asd", &text);
	SP_CHECK(flag == 0 && !strcmp(sp_text(text), ""), "file without comment: flag=%d text=%s", flag, sp_text(text));
	return 0;
}

static int sp_c3_missing(void)
{
	int text;
	int flag = sp_read_comment("T_None.asd", &text);
	SP_CHECK(flag == 1 && text == -1, "missing file must give none (flag=%d text=%d)", flag, text);
	return 0;
}

static int sp_c4_resave_clears(void)
{
	int obj = sp_new("CASCommonData");
	SP_CHECK(sp_save("T_Cmt.asd", obj) == 1 && sp_write_comment("T_Cmt.asd", SP_COMMENT) == 1, "save+comment");
	SP_CHECK(sp_save("T_Cmt.asd", obj) == 1, "save again");
	size_t l;
	uint8_t *raw = sp_read_raw("T_Cmt.asd", &l, NULL);
	SP_CHECK(raw && sp_le(raw + SP_HDR_OFF + 60) == 0 && (size_t)sp_le(raw + SP_HDR_OFF + 56) == l,
		 "a new save must drop the comment");
	int text;
	int flag = sp_read_comment("T_Cmt.asd", &text);
	SP_CHECK(flag == 0 && !strcmp(sp_text(text), ""), "flag=%d text=%s", flag, sp_text(text));
	return 0;
}

static int sp_c5_errors(void)
{
	int obj = sp_new("CASCommonData");
	SP_CHECK(sp_save("T_Cmt.asd", obj) == 1, "save");
	SP_CHECK(sp_write_comment("T_Cmt.asd", "") == 0, "empty comment must be refused");
	SP_CHECK(sp_write_comment("T_None.asd", SP_COMMENT) == 0, "missing file must be refused");
	SP_CHECK(!sp_exists("T_None.asd"), "a comment write created a file");
	size_t l;
	uint8_t *raw = sp_read_raw("T_Cmt.asd", &l, NULL);
	sp_put_le(raw + SP_HDR_OFF + 4, 7);
	sp_write_raw("T_V7.asd", raw, l);
	SP_CHECK(sp_write_comment("T_V7.asd", SP_COMMENT) == 0, "version 7 has no comment");
	sp_put_le(raw + SP_HDR_OFF + 4, 8);
	sp_put_le(raw + SP_HDR_OFF + 60, 1);
	sp_write_raw("T_V8.asd", raw, l);
	int text;
	SP_CHECK(sp_read_comment("T_V8.asd", &text) == 1, "version 8 comments are not read (0x65c458)");
	return 0;
}

static int sp_c6_cif(void)
{
	int sl = sp_lib("system"), f = sp_libfn(sl, "ReadSerializeStructComment");
	void *want = sp_sym("system_ReadSerializeStructComment_v14");
	SP_CHECK(libraries[sl][f].args[1] == &ffi_type_sint32, "wrap<string> CIF");
	SP_CHECK(want && libraries[sl][f].fun == want, "ReadSerializeStructComment binding");
	return 0;
}

static int test_save_comment(void)
{
	setenv("XSYS4_TRACE_SAVE", "1", 1);
	sp_setup_dir();
	sp_run("C1 write and read a comment", sp_c1_write_read);
	sp_run("C2 file without a comment", sp_c2_no_comment);
	sp_run("C3 missing file", sp_c3_missing);
	sp_run("C4 a new save drops the comment", sp_c4_resave_clears);
	sp_run("C5 refused writes and versions", sp_c5_errors);
	sp_run("C6 binding and CIF", sp_c6_cif);
	sp_remove_dir();
	printf("SAVE-COMMENT summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 1 : 0;
}

/* ---- save-localgame (optional, needs XS4_SAVE_ORIG_COPY) ------------------ */

/* Load copies of original SaveData<N>.asd files into a constructed LocalGame,
 * write the object back and compare bodies; then release it. */
static int sp_localgame_file(const char *name)
{
	const char *src = getenv("XS4_SAVE_ORIG_COPY");
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/%s", src, name);
	FILE *in = fopen(p, "rb");
	if (!in) {
		printf("  %s: not in the copy folder\n", name);
		return 0;
	}
	char *dst = sp_path(name);
	FILE *out = fopen(dst, "wb");
	free(dst);
	uint8_t buf[1 << 16];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
	size_t base = live_slots();
	int lg = alloc_struct(sp_sno("LocalGame")); /* its constructor reads EX data */
	size_t constructed = live_slots();
	int r = sp_sys("DeserializeStruct", name, lg);
	printf("  %s load=%d live %zu -> %zu (constructed) -> %zu (loaded)\n", name, r, base, constructed, live_slots());
	SP_CHECK(r == 1, "load %s", name);
	char rt[64];
	snprintf(rt, sizeof rt, "RT_%s", name);
	SP_CHECK(sp_sys("SerializeStruct", rt, lg) == 1, "re-save");
	size_t la, lb;
	uint8_t *ra = sp_read_raw(name, &la, NULL), *rb = sp_read_raw(rt, &lb, NULL);
	size_t d = 0;
	while (ra && rb && d < la && d < lb && ra[d] == rb[d])
		d++;
	printf("  re-saved body: original %zu bytes, xsystem4 %zu bytes, %s%zu\n", la, lb,
	       ra && rb && la == lb && d == la ? "identical " : "first difference at ", d);
	free(ra);
	free(rb);
	heap_unref(lg);
	printf("  teardown live %zu (base %zu)\n", live_slots(), base);
	return 0;
}

static int sp_lg_1000(void) { return sp_localgame_file("SaveData1000.asd"); }
static int sp_lg_5000(void) { return sp_localgame_file("SaveData5000.asd"); }

static int test_save_localgame(void)
{
	if (!getenv("XS4_SAVE_ORIG_COPY")) {
		printf("SAVE-LOCALGAME skipped: XS4_SAVE_ORIG_COPY not set\n");
		return 0;
	}
	setenv("XSYS4_TRACE_SAVE", "1", 1);
	/* LocalGame constructors read the main EX file: load the work copy's */
	const char *game = getenv("XS4_GAME");
	char ex_path[PATH_MAX];
	snprintf(ex_path, sizeof ex_path, "%s/dohnadohnaEx.ex", game ? game : ".");
	config.ex_path = strdup(ex_path);
	extern struct static_library lib_MainEXFile;
	library_run(&lib_MainEXFile, "_ModuleInit");
	sp_setup_dir();
	sp_run("G1 original SaveData1000 into LocalGame", sp_lg_1000);
	sp_run("G2 original SaveData5000 into LocalGame", sp_lg_5000);
	sp_remove_dir();
	printf("SAVE-LOCALGAME summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 1 : 0;
}

/* ---- save-seed (manual GUI helper) -------------------------------------- */

/* XS4_SAVE_SEED_DIR (under XS4_WORK): append "ZZ_PERSIST_PROBE" to the
 * event list of <dir>/Collection.asd and write <dir>/AFConfig.asd with
 * default settings and <ConfigVoiceMutedByNsfw> = 1. Uses the fixed build. */
static int test_save_seed(void)
{
	const char *dir = getenv("XS4_SAVE_SEED_DIR");
	if (!dir || !dir[0] || !sp_under(dir, "XS4_WORK") || sp_under(dir, "XS4_SRC") || sp_under(dir, "XS4_MASTER_GAME")) {
		printf("SAVE-SEED refused: XS4_SAVE_SEED_DIR must be inside XS4_WORK\n");
		return 2;
	}
	free(config.save_dir);
	config.save_dir = strdup(dir);
	int col = sp_new("Collection");
	int r = sp_load("Collection.asd", col);
	int ev = sp_get(sp_get(sp_get(col, "m_eventInfo"), "m_event"), "m_list");
	int n = sp_arr_len(ev);
	printf("SAVE-SEED Collection load=%d events=%d\n", r, n);
	if (r != 1)
		return 1;
	/* UniqueArray<string> keeps m_list sorted (LowerBound/BinarySearch with
	 * S_LT/S_GT, i.e. strcmp): insert the marker at its sorted position. */
	const char *marker = "ZZ_PERSIST_PROBE";
	int *v = xmalloc(sizeof(int) * (n + 1));
	int k = 0, pos = n;
	for (int i = 0; i < n; i++) {
		int e = sp_arr_at(ev, i);
		if (pos == n && strcmp(sp_text(e), marker) > 0)
			pos = k++;
		v[k++] = e;
		heap_ref(e);
	}
	v[pos] = sp_str(marker);
	sp_arr_set(ev, v, n + 1, true);
	r = sp_save("Collection.asd", col);
	printf("SAVE-SEED Collection save=%d events=%d marker at %d\n", r, n + 1, pos);
	int gc, nsfw, dl, sc, joy;
	int cfg = sp_config_new(&gc, &nsfw, &dl, &sc, &joy);
	sp_at(nsfw, "<ConfigVoiceMutedByNsfw>")->i = 1;
	int r2 = sp_save("AFConfig.asd", cfg);
	printf("SAVE-SEED AFConfig save=%d ConfigVoiceMutedByNsfw=1\n", r2);
	return r == 1 && r2 == 1 ? 0 : 1;
}
