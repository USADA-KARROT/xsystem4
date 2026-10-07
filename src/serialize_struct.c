/* Native "serialize_struct" save files (system.SerializeStruct family, AIN v14).
 *
 * The original engine writes version 9 (0x65f160 / 0x65f8f0) and reads
 * versions 7..9 (0x65c910 / 0x65cb20). Container: "GD" 01 01, u32 raw size,
 * zlib level 1, no encryption (0x6768e0 / 0x676790). Body:
 *
 *   cstring key = "serialize_struct"
 *   int32 h[16]            (v8/v9: 0x40 bytes; v7: 0x38)
 *   cstring group = "serialize_struct"
 *   records  @h4  x h5 : int32 sd (-1 = globals), int32 n, int32 idx[n]
 *   globals  @h6  x h7 : int32 type, int32 type2, int32 value, int32 x, cstring name
 *   strings  @h8  x h9 : cstring
 *   arrays   @h10 x h11: int32 rank, int32 dims[rank], int32 nflat,
 *                        nflat x {int32 n, int32 elem_type, int32 v[n]}
 *   keyvals  @h12 x h13: int32
 *   structdefs         : int32 nsd, nsd x {cstring name, int32 nfield,
 *                        nfield x {int32 type, int32 type2, cstring name2}}
 *   [comment @h14, flag h15]: int32 len, byte[len]
 *
 * h0 = 1000, h1 = version, h2 = header size, h3 = number of roots,
 * h14 = end of the struct definitions. Offsets are from the start of the body.
 *
 * Loading is in place (0x65d9e0): fields are matched by member name2 plus
 * (type, value type); "<vtable>" and unnamed fields are skipped; struct names
 * are not compared. Strings are rewritten in place (0x65d480). Arrays are
 * rebuilt from the file dimensions with the destination's element type
 * (0x65d5b0). Differences from the original are listed in
 * docs/checkpoints/2026-09-28/research/save-persistence/README.md.
 */

#define VM_PRIVATE
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"

#include "serialize_struct.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define SS_KEY           "serialize_struct"
#define SS_MAGIC         1000
#define SS_VERSION       9
#define SS_HDR_V9        0x40
#define SS_EMPTY_STRING  0x7fffffff
#define SS_MAX_RAW       (64u << 20)
#define SS_MAX_DEPTH     64
#define SS_MAX_RECORDS   (1 << 20)
#define SS_WARN_LIMIT    8

#define SS_WARN(...) do {                             \
		static int ss_w_;                     \
		if (ss_w_ < SS_WARN_LIMIT) {          \
			ss_w_++;                      \
			WARNING(__VA_ARGS__);         \
		}                                     \
	} while (0)

void ss_trace(const char *fmt, ...)
{
	static int enabled = -1, lines;
	if (enabled < 0) {
		const char *e = getenv("XSYS4_TRACE_SAVE");
		enabled = e && !strcmp(e, "1");
	}
	if (!enabled || lines >= 200)
		return;
	lines++;
	va_list ap;
	va_start(ap, fmt);
	fputs("SAVE ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	fflush(stderr);
	va_end(ap);
}

/* --- AIN type helpers --------------------------------------------------- */

/* 0x653660: wrap<T> -> reference type code; -1 when T is not in the table. */
static int ss_wrap_ref_code(const struct ain_type *in)
{
	switch (in ? (int)in->data : -1) {
	case AIN_INT:        return AIN_REF_INT;
	case AIN_FLOAT:      return AIN_REF_FLOAT;
	case AIN_STRING:     return AIN_REF_STRING;
	case AIN_STRUCT:     return AIN_REF_STRUCT;
	case AIN_BOOL:       return AIN_REF_BOOL;
	case AIN_DELEGATE:   return AIN_REF_DELEGATE;
	case AIN_ARRAY:      return AIN_REF_ARRAY;
	case AIN_WRAP:       return AIN_WRAP;
	case AIN_OPTION:     return AIN_UNKNOWN_TYPE_87;
	case AIN_ENUM:       return AIN_REF_ENUM;
	case AIN_IFACE_WRAP: return AIN_IFACE;
	default:             return -1;
	}
}

static const struct ain_type *ss_option_inner(const struct ain_type *t)
{
	while (t && (t->data == AIN_OPTION || t->data == AIN_UNKNOWN_TYPE_87))
		t = t->array_type;
	return t;
}

/* 0x6535e0: value type (struct definition type2, option payload, array elem_type). */
static int ss_valuetype(const struct ain_type *t)
{
	if (!t)
		return -1;
	if (t->data == AIN_OPTION || t->data == AIN_UNKNOWN_TYPE_87) {
		t = ss_option_inner(t);
		if (!t)
			return -1;
	}
	if (t->data == AIN_WRAP)
		return ss_wrap_ref_code(t->array_type);
	return t->data;
}

/* 0x653420 / 0x6537e0 / 0x653610: slots per value. */
static int ss_slot_count(const struct ain_type *t)
{
	if (!t)
		return 1;
	switch (t->data) {
	case AIN_REF_INT: case AIN_REF_FLOAT: case AIN_REF_BOOL: case AIN_UNKNOWN_TYPE_87:
	case AIN_IFACE: case AIN_REF_ENUM:
		return 2;
	case AIN_WRAP:
		switch (t->array_type ? (int)t->array_type->data : -1) {
		case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_WRAP: case AIN_OPTION:
		case AIN_ENUM: case AIN_IFACE_WRAP:
			return 2;
		default:
			return 1;
		}
	case AIN_OPTION: {
		/* 0x653452 strips every option layer and adds one slot. */
		const struct ain_type *in = ss_option_inner(t);
		return in ? ss_slot_count(in) + 1 : 2;
	}
	default:
		return 1;
	}
}

int ss_type_slot_count(const struct ain_type *t)
{
	return ss_slot_count(t);
}

enum ss_kind { SS_ZERO, SS_RAW, SS_STR, SS_STRUCT, SS_ARRAY, SS_NULLREF, SS_BAD };

/* Shared classification of the writer (0x660070) and the loader (0x65d390). */
static enum ss_kind ss_kind(int code)
{
	switch (code) {
	case AIN_VOID: case AIN_DELEGATE:
		return SS_ZERO;
	case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_ENUM:
		return SS_RAW;
	case AIN_STRING:
		return SS_STR;
	case AIN_STRUCT:
		return SS_STRUCT;
	case AIN_ARRAY:
		return SS_ARRAY;
	case AIN_REF_INT: case AIN_REF_FLOAT: case AIN_REF_STRING: case AIN_REF_STRUCT:
	case AIN_REF_BOOL: case AIN_REF_DELEGATE: case AIN_REF_ARRAY: case AIN_WRAP:
	case AIN_UNKNOWN_TYPE_87: case AIN_IFACE: case AIN_REF_ENUM:
		return SS_NULLREF;
	default:
		return SS_BAD;
	}
}

static bool ss_is_nested_array(const struct ain_type *elem)
{
	return elem && (elem->data == AIN_ARRAY || elem->data == AIN_REF_ARRAY);
}

static bool ss_is_option(const struct ain_type *t)
{
	return t && (t->data == AIN_OPTION || t->data == AIN_UNKNOWN_TYPE_87);
}

/* Does an array element of this declared type own a heap slot in xsystem4? */
static bool ss_elem_owns_slot(const struct ain_type *elem)
{
	if (!elem || ss_slot_count(elem) != 1)
		return false;
	switch (ss_kind(ss_valuetype(elem))) {
	case SS_STR: case SS_STRUCT: case SS_ARRAY: case SS_NULLREF:
		return true;
	case SS_ZERO:
		return elem->data == AIN_DELEGATE;
	default:
		return false;
	}
}

static const char *ss_struct_name(int no)
{
	return no >= 0 && no < ain->nr_structures && ain->structures[no].name
		? ain->structures[no].name : "?";
}

static const char *ss_member_name2(const struct ain_variable *m)
{
	return m->name2 ? m->name2 : "";
}

int ss_struct_slot(int s)
{
	if (s <= 1 || !page_index_valid(s) || !heap[s].page)
		return -1;
	struct page *p = heap[s].page;
	if (p->type == STRUCT_PAGE && p->index == -1 && p->nr_vars >= 1) {
		/* xsystem4 wrap box; the original engine has no such layer. */
		s = p->values[0].i;
		if (s <= 1 || !page_index_valid(s) || !heap[s].page)
			return -1;
		p = heap[s].page;
	}
	return p->type == STRUCT_PAGE && p->index >= 0 && p->index < ain->nr_structures ? s : -1;
}

/* --- byte buffers ------------------------------------------------------- */

struct ss_bytes {
	uint8_t *p;
	size_t n, cap;
};

static void bb_put(struct ss_bytes *b, const void *d, size_t n)
{
	if (b->n + n > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;
		while (cap < b->n + n)
			cap *= 2;
		b->p = xrealloc(b->p, cap);
		b->cap = cap;
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
}

static void le32_store(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;
	p[0] = u & 0xff; p[1] = (u >> 8) & 0xff; p[2] = (u >> 16) & 0xff; p[3] = (u >> 24) & 0xff;
}

static int32_t le32_load(const uint8_t *p)
{
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void bb_i32(struct ss_bytes *b, int32_t v)
{
	uint8_t x[4];
	le32_store(x, v);
	bb_put(b, x, 4);
}

static void bb_cstr(struct ss_bytes *b, const char *s, size_t n)
{
	bb_put(b, s, n);
	bb_put(b, "", 1);
}

struct ss_ints {
	int32_t *v;
	int n, cap;
};

static void ib_push(struct ss_ints *b, int32_t x)
{
	if (b->n == b->cap) {
		b->cap = b->cap ? b->cap * 2 : 64;
		b->v = xrealloc(b->v, sizeof(int32_t) * b->cap);
	}
	b->v[b->n++] = x;
}

/* --- container I/O ------------------------------------------------------ */

static bool ss_read_container(const char *path, const char *label, uint8_t **raw, size_t *len)
{
	*raw = NULL;
	*len = 0;
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp) {
		if (errno != ENOENT)
			SS_WARN("serialize_struct '%s': cannot open: %s", label, strerror(errno));
		return false;
	}
	uint8_t hdr[8];
	bool ok = fread(hdr, sizeof hdr, 1, fp) == 1;
	long size = -1;
	if (ok && fseek(fp, 0, SEEK_END) == 0)
		size = ftell(fp);
	fclose(fp);
	if (!ok || size < 10 || (unsigned long)size > SS_MAX_RAW || memcmp(hdr, "GD\x01\x01", 4)) {
		SS_WARN("serialize_struct '%s': not a GD container", label);
		return false;
	}
	uint32_t raw_size = (uint32_t)le32_load(hdr + 4);
	if (raw_size == 0 || raw_size > SS_MAX_RAW) {
		SS_WARN("serialize_struct '%s': raw size %u out of range", label, raw_size);
		return false;
	}
	enum savefile_error error;
	struct savefile *sf = savefile_read(path, &error);
	if (!sf) {
		SS_WARN("serialize_struct '%s': %s", label, savefile_strerror(error));
		return false;
	}
	*raw = sf->buf;
	*len = sf->len;
	free(sf);
	return true;
}

#ifdef _WIN32
static wchar_t *ss_widen(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (n <= 0)
		return NULL;
	wchar_t *w = xmalloc(sizeof(wchar_t) * n);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
	return w;
}
#endif

/* Write to "<path>.xs4tmp", flush and sync it, then replace <path>. The
 * original engine overwrites in place (CREATE_ALWAYS); a failed write here
 * never leaves a truncated save file behind. */
static bool ss_write_container(const char *path, const char *label, const uint8_t *raw, size_t len)
{
	size_t tmp_len = strlen(path) + 8;
	char *tmp = xmalloc(tmp_len);
	snprintf(tmp, tmp_len, "%s.xs4tmp", path);
	FILE *fp = file_open_utf8(tmp, "wb");
	if (!fp) {
		SS_WARN("serialize_struct '%s': cannot create temporary file: %s", label, strerror(errno));
		free(tmp);
		return false;
	}
	struct savefile sf = { .buf = (uint8_t *)raw, .len = len, .encrypted = false,
			       .compression_level = Z_BEST_SPEED };
	bool ok = savefile_write(&sf, fp) == SAVEFILE_SUCCESS;
	ok = ok && fflush(fp) == 0;
#ifdef _WIN32
	ok = ok && _commit(_fileno(fp)) == 0;
#else
	ok = ok && fsync(fileno(fp)) == 0;
#endif
	if (fclose(fp) != 0)
		ok = false;
	if (!ok) {
		SS_WARN("serialize_struct '%s': write failed", label);
		remove_utf8(tmp);
		free(tmp);
		return false;
	}
#ifdef _WIN32
	wchar_t *wtmp = ss_widen(tmp), *wpath = ss_widen(path);
	ok = wtmp && wpath && MoveFileExW(wtmp, wpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	free(wtmp);
	free(wpath);
	if (!ok) /* keep the temporary file: the previous save stays intact */
		SS_WARN("serialize_struct '%s': cannot replace the save file", label);
#else
	ok = rename(tmp, path) == 0;
	if (!ok) {
		SS_WARN("serialize_struct '%s': cannot replace the save file: %s", label, strerror(errno));
		remove_utf8(tmp);
	}
#endif
	free(tmp);
	return ok;
}

/* --- writer (0x65f160 / 0x660070 / 0x6605c0 / 0x660190) ------------------ */

struct ss_wrec { int sd; int n; int32_t *idx; }; /* idx NULL: globals record, 0..n-1 */
struct ss_wstr { const char *p; size_t len; };
struct ss_warr { int n; int elem_type; int v0; };

struct ss_writer {
	struct ss_wrec *rec; int nrec, caprec;  /* rec.sd: AIN struct index until assembly */
	struct ss_wstr *str; int nstr, capstr;
	struct ss_warr *arr; int narr, caparr;
	struct ss_ints kv, pool;
	int *sd_of;          /* AIN struct index -> struct definition index, -1 unused */
	struct ss_ints sd_list;
	char why[200];
};

#define W_PUSH(arr, n, cap, val) do {                                   \
		if ((n) == (cap)) {                                     \
			(cap) = (cap) ? (cap) * 2 : 64;                 \
			(arr) = xrealloc((arr), sizeof(*(arr)) * (cap)); \
		}                                                       \
		(arr)[(n)++] = (val);                                   \
	} while (0)

static bool w_fail(struct ss_writer *W, const char *fmt, ...)
{
	if (!W->why[0]) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(W->why, sizeof W->why, fmt, ap);
		va_end(ap);
	}
	return false;
}

static int w_struct(struct ss_writer *W, int slot, int depth);
static int w_array(struct ss_writer *W, int slot, const struct ain_type *decl, int depth);

static bool w_value(struct ss_writer *W, int code, const struct ain_type *decl, int32_t v, int depth,
		    int32_t *out)
{
	switch (ss_kind(code)) {
	case SS_ZERO:
		*out = 0;
		return true;
	case SS_RAW:
		*out = v;
		return true;
	case SS_NULLREF:
		*out = -1;
		return true;
	case SS_STR: {
		if (v <= 0) {
			/* xsystem4 also uses 0 (the guard slot) as a null string */
			*out = -1;
			return true;
		}
		if (!string_index_valid(v))
			return w_fail(W, "invalid string handle %d", v);
		struct string *s = heap[v].s;
		size_t len = s ? strnlen(s->text, (size_t)s->size) : 0;
		if (len == 0) {
			*out = SS_EMPTY_STRING;
			return true;
		}
		struct ss_wstr e = { s->text, len };
		W_PUSH(W->str, W->nstr, W->capstr, e);
		*out = W->nstr - 1;
		return true;
	}
	case SS_STRUCT: {
		if (v <= 0) {
			*out = -1;
			return true;
		}
		int r = w_struct(W, v, depth + 1);
		if (r < 0)
			return false;
		*out = r;
		return true;
	}
	case SS_ARRAY: {
		if (v <= 0) {
			*out = -1;
			return true;
		}
		if (!page_index_valid(v))
			return w_fail(W, "invalid array handle %d", v);
		int r = w_array(W, v, decl, depth + 1);
		if (r < 0)
			return false;
		*out = r;
		return true;
	}
	default:
		return w_fail(W, "unsupported type %d", code);
	}
}

/* The original writer keeps struct definitions in a container ordered by
 * struct index (original files list them in ascending AIN index). */
static void w_number_sds(struct ss_writer *W)
{
	for (int no = 0; no < ain->nr_structures; no++) {
		if (W->sd_of[no] >= 0) {
			W->sd_of[no] = W->sd_list.n;
			ib_push(&W->sd_list, no);
		}
	}
}

static int w_struct(struct ss_writer *W, int slot, int depth)
{
	if (depth > SS_MAX_DEPTH)
		return w_fail(W, "nesting deeper than %d", SS_MAX_DEPTH), -1;
	if (W->nrec >= SS_MAX_RECORDS)
		return w_fail(W, "more than %d records", SS_MAX_RECORDS), -1;
	int s = ss_struct_slot(slot);
	if (s < 0)
		return w_fail(W, "handle %d is not a struct", slot), -1;
	int no = heap[s].page->index;
	struct ain_struct *st = &ain->structures[no];
	if (heap[s].page->nr_vars < st->nr_members)
		return w_fail(W, "struct %s has %d slots for %d members", st->name,
			      heap[s].page->nr_vars, st->nr_members), -1;
	int n = st->nr_members;
	int32_t *idx = xmalloc(sizeof(int32_t) * (n ? n : 1));
	/* Fields in order; each keyval is appended right after its value is
	 * encoded, so nested records, strings and arrays come first (the order
	 * original files show). */
	for (int i = 0; i < n; i++) {
		const struct ain_type *t = &st->members[i].type;
		int32_t v = heap[s].page->values[i].i;
		if (i == 0 || st->members[i - 1].type.data != AIN_OPTION) {
			bool opt = t->data == AIN_OPTION;
			int code = opt ? ss_valuetype(t) : (int)t->data;
			const struct ain_type *decl = opt ? ss_option_inner(t) : t;
			if (!w_value(W, code, decl, v, depth, &v)) {
				size_t used = strlen(W->why);
				if (used + 4 < sizeof W->why)
					snprintf(W->why + used, sizeof W->why - used, " <- %s.%s", st->name,
						 ss_member_name2(&st->members[i]));
				free(idx);
				return -1;
			}
		} /* else 0x6606be: the option flag slot is stored raw */
		idx[i] = W->kv.n;
		ib_push(&W->kv, v);
	}
	W->sd_of[no] = 0; /* used; numbered in w_number_sds */
	struct ss_wrec r = { no, n, idx };
	W_PUSH(W->rec, W->nrec, W->caprec, r);
	return W->nrec - 1;
}

static int w_array(struct ss_writer *W, int slot, const struct ain_type *decl, int depth)
{
	if (depth > SS_MAX_DEPTH)
		return w_fail(W, "nesting deeper than %d", SS_MAX_DEPTH), -1;
	const struct ain_type *elem = decl ? decl->array_type : NULL;
	if (!elem)
		return w_fail(W, "array without element type"), -1;
	struct page *p = heap[slot].page;
	if (p && p->type != ARRAY_PAGE)
		return w_fail(W, "handle %d is not an array", slot), -1;
	int ecode = ss_valuetype(elem);
	int slots = ss_slot_count(elem);
	/* xsystem4 stores two-slot elements either packed (struct_type 2) or as a
	 * single slot (X_A_INIT operand 0); every such element is written as -1. */
	int stride = slots == 2 && p && (p->a_type == AIN_ARRAY || p->a_type == AIN_REF_ARRAY)
		&& p->array.struct_type == 2 && p->nr_vars % 2 == 0 ? 2 : 1;
	int nn = p ? p->nr_vars / stride : 0;
	if (nn > 0 && (ss_is_nested_array(elem) || ss_is_option(elem)))
		return w_fail(W, "array element type %d is not supported", elem->data), -1;
	if (slots == 2 && ss_kind(ecode) != SS_NULLREF && nn > 0)
		return w_fail(W, "two-slot array element type %d", ecode), -1;
	int32_t *vals = nn ? xmalloc(sizeof(int32_t) * nn) : NULL;
	for (int k = 0; k < nn; k++) {
		int32_t v = heap[slot].page->values[k * stride].i;
		if (!w_value(W, ecode, elem, v, depth, &vals[k])) {
			free(vals);
			return -1;
		}
	}
	int v0 = W->pool.n;
	for (int k = 0; k < nn; k++)
		ib_push(&W->pool, vals[k]);
	free(vals);
	struct ss_warr a = { nn, ecode, v0 };
	W_PUSH(W->arr, W->narr, W->caparr, a);
	return W->narr - 1;
}

static void w_free(struct ss_writer *W)
{
	for (int i = 0; i < W->nrec; i++)
		free(W->rec[i].idx);
	free(W->rec);
	free(W->str);
	free(W->arr);
	free(W->kv.v);
	free(W->pool.v);
	free(W->sd_of);
	free(W->sd_list.v);
}

static void w_assemble(struct ss_writer *W, const int *root_rec, int n, struct ss_bytes *b)
{
	int32_t h[16] = { 0 };
	w_number_sds(W);
	bb_cstr(b, SS_KEY, strlen(SS_KEY));
	size_t hdr = b->n;
	uint8_t zero[SS_HDR_V9] = { 0 };
	bb_put(b, zero, sizeof zero);
	bb_cstr(b, SS_KEY, strlen(SS_KEY));

	h[4] = (int32_t)b->n;
	h[5] = W->nrec;
	for (int r = 0; r < W->nrec; r++) {
		bb_i32(b, W->rec[r].sd < 0 ? -1 : W->sd_of[W->rec[r].sd]);
		bb_i32(b, W->rec[r].n);
		for (int i = 0; i < W->rec[r].n; i++)
			bb_i32(b, W->rec[r].idx ? W->rec[r].idx[i] : i);
	}
	h[6] = (int32_t)b->n;
	h[7] = n + 1;
	for (int i = 0; i <= n; i++) {
		bool last = i == n;
		bb_i32(b, last ? 0 : AIN_STRUCT);
		bb_i32(b, last ? 0 : AIN_STRUCT);
		bb_i32(b, last ? 0 : root_rec[i]);
		bb_i32(b, 0);
		bb_cstr(b, "", 0);
	}
	h[8] = (int32_t)b->n;
	h[9] = W->nstr;
	for (int i = 0; i < W->nstr; i++)
		bb_cstr(b, W->str[i].p, W->str[i].len);
	h[10] = (int32_t)b->n;
	h[11] = W->narr;
	for (int i = 0; i < W->narr; i++) {
		bb_i32(b, 1);
		bb_i32(b, W->arr[i].n);
		bb_i32(b, 1);
		bb_i32(b, W->arr[i].n);
		bb_i32(b, W->arr[i].elem_type);
		for (int k = 0; k < W->arr[i].n; k++)
			bb_i32(b, W->pool.v[W->arr[i].v0 + k]);
	}
	h[12] = (int32_t)b->n;
	h[13] = W->kv.n;
	for (int i = 0; i < W->kv.n; i++)
		bb_i32(b, W->kv.v[i]);
	bb_i32(b, W->sd_list.n);
	for (int i = 0; i < W->sd_list.n; i++) {
		struct ain_struct *st = &ain->structures[W->sd_list.v[i]];
		bb_cstr(b, st->name, strlen(st->name));
		bb_i32(b, st->nr_members);
		for (int m = 0; m < st->nr_members; m++) {
			const char *name2 = ss_member_name2(&st->members[m]);
			bb_i32(b, st->members[m].type.data);
			bb_i32(b, ss_valuetype(&st->members[m].type));
			bb_cstr(b, name2, strlen(name2));
		}
	}
	h[0] = SS_MAGIC;
	h[1] = SS_VERSION;
	h[2] = SS_HDR_V9;
	h[3] = n;
	h[14] = (int32_t)b->n;
	h[15] = 0;
	for (int i = 0; i < 16; i++)
		le32_store(b->p + hdr + 4 * i, h[i]);
}

bool ss_serialize_file(const char *path, const char *label, const int *roots, int n)
{
	struct ss_writer W = { 0 };
	W.sd_of = xmalloc(sizeof(int) * ain->nr_structures);
	for (int i = 0; i < ain->nr_structures; i++)
		W.sd_of[i] = -1;
	struct ss_wrec globals = { -1, n, NULL };
	W_PUSH(W.rec, W.nrec, W.caprec, globals);
	int *root_rec = xmalloc(sizeof(int) * n);
	bool ok = true;
	for (int i = 0; i < n && ok; i++) {
		root_rec[i] = w_struct(&W, roots[i], 0);
		ok = root_rec[i] >= 0;
	}
	struct ss_bytes b = { 0 };
	if (ok) {
		w_assemble(&W, root_rec, n, &b);
		ok = ss_write_container(path, label, b.p, b.n);
		if (!ok)
			snprintf(W.why, sizeof W.why, "write failed");
	} else {
		SS_WARN("system.SerializeStruct('%s'): %s; nothing written", display_game0(label), display_game1(W.why));
	}
	ss_trace("ser %s roots=%d recs=%d strs=%d arrs=%d bytes=%zu %s%s%s", label, n, W.nrec, W.nstr, W.narr,
		 b.n, ok ? "ok" : "fail(", ok ? "" : W.why, ok ? "" : ")");
	free(b.p);
	free(root_rec);
	w_free(&W);
	return ok;
}

/* --- reader (0x65cb20) -------------------------------------------------- */

struct ss_rd {
	const uint8_t *b;
	size_t len, off;
	bool err;
};

static int32_t rd_i32(struct ss_rd *r)
{
	if (r->err || r->off > r->len || r->len - r->off < 4) {
		r->err = true;
		return 0;
	}
	int32_t v = le32_load(r->b + r->off);
	r->off += 4;
	return v;
}

static const char *rd_cstr(struct ss_rd *r, size_t *lenp)
{
	if (lenp)
		*lenp = 0;
	if (r->err || r->off >= r->len) {
		r->err = true;
		return "";
	}
	const uint8_t *z = memchr(r->b + r->off, 0, r->len - r->off);
	if (!z) {
		r->err = true;
		return "";
	}
	const char *s = (const char *)(r->b + r->off);
	size_t l = (size_t)(z - (r->b + r->off));
	r->off += l + 1;
	if (lenp)
		*lenp = l;
	return s;
}

/* 0x65ce00 family: follow a section offset only when it lies in the body. */
static void rd_seek(struct ss_rd *r, int32_t off)
{
	if (off >= 0 && (size_t)off < r->len)
		r->off = (size_t)off;
}

static bool rd_count_ok(struct ss_rd *r, int32_t count, size_t min)
{
	if (r->err || count < 0 || r->off > r->len)
		return false;
	return (size_t)count <= (r->len - r->off) / min;
}

struct ss_rec { int sd, n; int32_t *idx; };
struct ss_glob { int type, type2, value, x; };
struct ss_str { const char *p; size_t len; };
struct ss_arr { int rank, dims0, n, elem_type; int32_t *v; bool usable; };
struct ss_field { int type, type2; const char *name; };
struct ss_sd { const char *name; int nfield; struct ss_field *f; };

struct ss_file {
	uint8_t *raw;
	size_t len;
	int32_t h[16];
	int ver;
	int nrec; struct ss_rec *rec;
	int nglob; struct ss_glob *glob;
	int nstr; struct ss_str *str;
	int narr; struct ss_arr *arr;
	int nkv; int32_t *kv;
	int nsd; struct ss_sd *sd;
	int *sd_ain;
	long long flat_total;
};

static void ss_file_free(struct ss_file *f)
{
	for (int i = 0; i < f->nrec; i++)
		free(f->rec[i].idx);
	for (int i = 0; i < f->narr; i++)
		free(f->arr[i].v);
	for (int i = 0; f->sd && i < f->nsd; i++)
		free(f->sd[i].f);
	free(f->rec);
	free(f->glob);
	free(f->str);
	free(f->arr);
	free(f->kv);
	free(f->sd);
	free(f->sd_ain);
	free(f->raw);
	memset(f, 0, sizeof *f);
}

/* Parse key, header and group. Returns the header size, or 0 (why is set). */
static size_t ss_parse_head(struct ss_file *f, struct ss_rd *r, const char *label, const char **why)
{
	const char *key = rd_cstr(r, NULL);
	if (r->err || strcmp(key, SS_KEY)) {
		SS_WARN("serialize_struct '%s': key mismatch", label);
		*why = "key";
		return 0;
	}
	f->h[0] = rd_i32(r);
	f->h[1] = rd_i32(r);
	if (r->err || f->h[0] != SS_MAGIC) {
		*why = "magic";
		return 0;
	}
	f->ver = f->h[1];
	if (f->ver < 7 || f->ver > 9) {
		SS_WARN("serialize_struct '%s': unsupported version %d", label, f->ver);
		*why = "version";
		return 0;
	}
	size_t hdr = 0x38 + 8 * (f->ver > 7);
	for (size_t i = 2; i < hdr / 4; i++)
		f->h[i] = rd_i32(r);
	if (r->err) {
		*why = "header";
		return 0;
	}
	return hdr;
}

static bool ss_parse(struct ss_file *f, const char *label, const char **why)
{
	struct ss_rd r = { f->raw, f->len, 0, false };
	if (!ss_parse_head(f, &r, label, why))
		return false;
	const char *group = rd_cstr(&r, NULL);
	if (r.err || strcmp(group, SS_KEY)) {
		*why = "group";
		return false;
	}

	rd_seek(&r, f->h[4]);
	if (!rd_count_ok(&r, f->h[5], 8))
		return *why = "records", false;
	f->nrec = f->h[5];
	f->rec = xcalloc(f->nrec ? f->nrec : 1, sizeof *f->rec);
	for (int i = 0; i < f->nrec; i++) {
		f->rec[i].sd = rd_i32(&r);
		int n = rd_i32(&r);
		if (!rd_count_ok(&r, n, 4))
			return *why = "record fields", false;
		f->rec[i].n = n;
		f->rec[i].idx = xmalloc(sizeof(int32_t) * (n ? n : 1));
		for (int k = 0; k < n; k++)
			f->rec[i].idx[k] = rd_i32(&r);
	}

	rd_seek(&r, f->h[6]);
	if (!rd_count_ok(&r, f->h[7], 17))
		return *why = "globals", false;
	f->nglob = f->h[7];
	f->glob = xcalloc(f->nglob ? f->nglob : 1, sizeof *f->glob);
	for (int i = 0; i < f->nglob; i++) {
		f->glob[i].type = rd_i32(&r);
		f->glob[i].type2 = rd_i32(&r);
		f->glob[i].value = rd_i32(&r);
		f->glob[i].x = rd_i32(&r);
		rd_cstr(&r, NULL);
	}

	rd_seek(&r, f->h[8]);
	if (!rd_count_ok(&r, f->h[9], 1))
		return *why = "strings", false;
	f->nstr = f->h[9];
	f->str = xcalloc(f->nstr ? f->nstr : 1, sizeof *f->str);
	for (int i = 0; i < f->nstr; i++)
		f->str[i].p = rd_cstr(&r, &f->str[i].len);

	rd_seek(&r, f->h[10]);
	if (!rd_count_ok(&r, f->h[11], 12))
		return *why = "arrays", false;
	f->narr = f->h[11];
	f->arr = xcalloc(f->narr ? f->narr : 1, sizeof *f->arr);
	for (int i = 0; i < f->narr; i++) {
		struct ss_arr *a = &f->arr[i];
		a->rank = rd_i32(&r);
		if (a->rank > 0 && !rd_count_ok(&r, a->rank, 4))
			return *why = "array rank", false;
		int32_t dims0 = 0;
		for (int k = 0; k < a->rank; k++) {
			int32_t d = rd_i32(&r);
			if (k == 0)
				dims0 = d;
		}
		int nflat = rd_i32(&r);
		if (!rd_count_ok(&r, nflat, 8))
			return *why = "array flats", false;
		for (int k = 0; k < nflat; k++) {
			int n = rd_i32(&r);
			int et = rd_i32(&r);
			if (!rd_count_ok(&r, n, 4))
				return *why = "array values", false;
			bool keep = k == 0 && a->rank == 1 && nflat == 1;
			if (keep) {
				a->n = n;
				a->elem_type = et;
				a->v = xmalloc(sizeof(int32_t) * (n ? n : 1));
				f->flat_total += n;
			}
			for (int j = 0; j < n; j++) {
				int32_t v = rd_i32(&r);
				if (keep)
					a->v[j] = v;
			}
		}
		if (a->rank <= 0) {
			a->usable = true; /* 0x65d5b0: rank <= 0 keeps an empty array */
			a->n = 0;
		} else if (a->rank == 1) {
			if (nflat != 1 || dims0 < 0 || a->n != dims0)
				return *why = "array shape", false;
			a->dims0 = dims0;
			a->usable = true;
		} else {
			a->usable = false; /* rank >= 2: nested arrays, not supported */
		}
	}

	rd_seek(&r, f->h[12]);
	if (!rd_count_ok(&r, f->h[13], 4))
		return *why = "keyvals", false;
	f->nkv = f->h[13];
	f->kv = xmalloc(sizeof(int32_t) * (f->nkv ? f->nkv : 1));
	for (int i = 0; i < f->nkv; i++)
		f->kv[i] = rd_i32(&r);

	/* struct definitions follow the keyvals without an offset field */
	int32_t nsd = rd_i32(&r);
	if (!rd_count_ok(&r, nsd, 5))
		return *why = "struct definitions", false;
	f->nsd = nsd;
	f->sd = xcalloc(f->nsd ? f->nsd : 1, sizeof *f->sd);
	for (int i = 0; i < f->nsd; i++) {
		f->sd[i].name = rd_cstr(&r, NULL);
		int nf = rd_i32(&r);
		if (!rd_count_ok(&r, nf, 9))
			return *why = "struct fields", false;
		f->sd[i].nfield = nf;
		f->sd[i].f = xcalloc(nf ? nf : 1, sizeof *f->sd[i].f);
		for (int k = 0; k < nf; k++) {
			f->sd[i].f[k].type = rd_i32(&r);
			f->sd[i].f[k].type2 = rd_i32(&r);
			f->sd[i].f[k].name = rd_cstr(&r, NULL);
		}
	}
	if (r.err)
		return *why = "truncated", false;
	return true;
}

/* A value the file declares with value type `code` refers to an existing table entry. */
static bool ss_ref_ok(const struct ss_file *f, int code, int32_t v)
{
	switch (ss_kind(code)) {
	case SS_STR:
		return v == -1 || v == SS_EMPTY_STRING || (v >= 0 && v < f->nstr);
	case SS_STRUCT:
		return v == -1 || (v >= 1 && v < f->nrec && f->rec[v].sd >= 0);
	case SS_ARRAY:
		return v < 0 || v < f->narr;
	default:
		return true;
	}
}

/* Checks done before anything is written to the destination. The original
 * engine checks the same ranges while applying; failing early keeps the
 * destination untouched for corrupt files. */
static bool ss_validate(struct ss_file *f, int nroots, const char **why)
{
	if (f->nrec < 1)
		return *why = "no records", false;
	for (int r = 1; r < f->nrec; r++) {
		struct ss_rec *rec = &f->rec[r];
		if (rec->sd < -1 || rec->sd >= f->nsd)
			return *why = "record struct index", false;
		if (rec->sd >= 0 && f->sd[rec->sd].nfield != rec->n)
			return *why = "record field count", false;
		for (int i = 0; i < rec->n; i++) {
			if (rec->idx[i] < 0 || rec->idx[i] >= f->nkv)
				return *why = "record keyval index", false;
			if (rec->sd >= 0 && !ss_ref_ok(f, f->sd[rec->sd].f[i].type2, f->kv[rec->idx[i]]))
				return *why = "record value reference", false;
		}
	}
	for (int i = 0; i < f->narr; i++) {
		struct ss_arr *a = &f->arr[i];
		if (!a->usable || !a->v)
			continue;
		for (int k = 0; k < a->n; k++)
			if (!ss_ref_ok(f, a->elem_type, a->v[k]))
				return *why = "array value reference", false;
	}
	if (f->nglob < nroots)
		return *why = "fewer globals than roots", false;
	for (int i = 0; i < nroots; i++) {
		struct ss_glob *g = &f->glob[i];
		if (g->type2 != AIN_STRUCT || g->value < 1 || g->value >= f->nrec || f->rec[g->value].sd < 0)
			return *why = "root record", false;
	}
	f->sd_ain = xmalloc(sizeof(int) * (f->nsd ? f->nsd : 1));
	for (int i = 0; i < f->nsd; i++)
		f->sd_ain[i] = ain_get_struct(ain, (char *)f->sd[i].name);
	return true;
}

static bool ss_load_file(const char *path, const char *label, struct ss_file *f, const char **why)
{
	memset(f, 0, sizeof *f);
	if (!ss_read_container(path, label, &f->raw, &f->len)) {
		*why = "read";
		return false;
	}
	return true;
}

/* --- loader (0x65c910 / 0x65d390 / 0x65d9e0 / 0x65d480 / 0x65d5b0) -------- */

struct ss_loader {
	struct ss_file *f;
	const char *label;
	long long struct_budget, elem_budget;
	int new_structs, new_strings, arrays, shared_strings, kept_arrays;
	char why[200];
};

/* A value location: a member of a struct (re-read after every VM re-entry)
 * or a slot of a page that is not attached to the heap yet. */
struct ss_loc {
	int owner, idx;
	union vm_value *direct;
};

static union vm_value *loc_ptr(const struct ss_loc *l)
{
	return l->direct ? l->direct : &heap[l->owner].page->values[l->idx];
}

static bool l_fail(struct ss_loader *L, const char *fmt, ...)
{
	if (!L->why[0]) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(L->why, sizeof L->why, fmt, ap);
		va_end(ap);
	}
	return false;
}

static bool l_struct(struct ss_loader *L, struct ss_loc loc, int32_t value, int decl_struct, int depth);
static bool l_array(struct ss_loader *L, struct ss_loc loc, int32_t value, const struct ain_type *decl, int depth);

static void l_release(int v)
{
	if (v > 1 && heap_index_valid(v))
		heap_unref(v);
}

static bool l_string(struct ss_loader *L, struct ss_loc loc, int32_t value)
{
	struct ss_file *f = L->f;
	if (value == -1) {
		int old = loc_ptr(&loc)->i;
		loc_ptr(&loc)->i = -1;
		if (string_index_valid(old))
			heap_unref(old);
		return true;
	}
	struct string *s;
	if (value == SS_EMPTY_STRING)
		s = make_string("", 0);
	else if (value >= 0 && value < f->nstr)
		s = make_string(f->str[value].p, (int)f->str[value].len);
	else
		return l_fail(L, "string index %d", value);
	int cur = loc_ptr(&loc)->i;
	if (string_index_valid(cur)) {
		/* 0x65d480 rewrites the string object; the handle is kept */
		if (HEAP_REF(cur) > 1)
			L->shared_strings++;
		heap_string_assign(cur, s);
		free_string(s);
	} else {
		loc_ptr(&loc)->i = heap_alloc_string(s);
		L->new_strings++;
	}
	return true;
}

static bool l_value(struct ss_loader *L, int code, const struct ain_type *mt, struct ss_loc loc,
		    int32_t kv, int depth)
{
	switch (ss_kind(code)) {
	case SS_ZERO:
	case SS_NULLREF:
		return true; /* 0x65d390: kept as is */
	case SS_RAW:
		loc_ptr(&loc)->i = kv;
		return true;
	case SS_STR:
		return l_string(L, loc, kv);
	case SS_STRUCT: {
		const struct ain_type *in = ss_option_inner(mt);
		return l_struct(L, loc, kv, in ? in->struc : -1, depth);
	}
	case SS_ARRAY:
		return l_array(L, loc, kv, ss_is_option(mt) ? ss_option_inner(mt) : mt, depth);
	default:
		return l_fail(L, "unsupported type %d", code);
	}
}

static int l_member_by_name2(struct ain_struct *st, const char *name)
{
	if (!name[0])
		return -1; /* 0x67e7c0 */
	for (int i = 0; i < st->nr_members; i++)
		if (!strcmp(ss_member_name2(&st->members[i]), name))
			return i;
	return -1;
}

static bool l_fields(struct ss_loader *L, int slot, struct ss_rec *rec, struct ss_sd *sd, int depth)
{
	struct ss_file *f = L->f;
	struct ain_struct *st = &ain->structures[heap[slot].page->index];
	for (int i = 0; i < rec->n; i++) {
		struct ss_field *fd = &sd->f[i];
		if (!strcmp(fd->name, "<vtable>"))
			continue; /* 0x65db1e */
		int m = l_member_by_name2(st, fd->name);
		if (m < 0)
			continue;
		const struct ain_type *mt = &st->members[m].type;
		if ((int)mt->data != fd->type || ss_valuetype(mt) != fd->type2)
			continue; /* 0x65db60 / 0x65db72 */
		if (!heap[slot].page || m >= heap[slot].page->nr_vars)
			return l_fail(L, "%s.%s: slot %d out of range", st->name, fd->name, m);
		struct ss_loc loc = { slot, m, NULL };
		if (!l_value(L, fd->type2, mt, loc, f->kv[rec->idx[i]], depth + 1)) {
			size_t used = strlen(L->why);
			if (used + 4 < sizeof L->why)
				snprintf(L->why + used, sizeof L->why - used, " <- %s.%s", st->name, fd->name);
			return false;
		}
		if (mt->data != AIN_OPTION)
			continue;
		/* 0x65dbd4: the option flag follows the value, stored raw */
		if (i + 1 >= rec->n || !heap[slot].page || m + 1 >= heap[slot].page->nr_vars)
			return l_fail(L, "%s.%s: option flag missing", st->name, fd->name);
		int32_t flag = f->kv[rec->idx[i + 1]];
		int payload = heap[slot].page->values[m].i;
		if (flag == 0 && ss_kind(fd->type2) == SS_NULLREF && !(payload > 1 && heap_index_valid(payload))) {
			/* The file cannot carry reference payloads (written as -1). Setting
			 * "some" here would leave some(-1); keep the destination instead. */
			SS_WARN("system.DeserializeStruct('%s'): %s.%s keeps its value (reference option)",
				display_game0(L->label), display_game1(st->name), display_game2(fd->name));
			continue;
		}
		heap[slot].page->values[m + 1].i = flag;
	}
	return true;
}

static bool l_struct(struct ss_loader *L, struct ss_loc loc, int32_t value, int decl_struct, int depth)
{
	struct ss_file *f = L->f;
	if (value == -1) {
		/* 0x65da02: release the current object and store -1 */
		int old = loc_ptr(&loc)->i;
		loc_ptr(&loc)->i = -1;
		if (old > 1 && page_index_valid(old))
			heap_unref(old);
		return true;
	}
	if (depth > SS_MAX_DEPTH)
		return l_fail(L, "nesting deeper than %d", SS_MAX_DEPTH);
	if (--L->struct_budget < 0)
		return l_fail(L, "record references exceed the file size");
	if (value < 1 || value >= f->nrec)
		return l_fail(L, "record %d out of range", value);
	struct ss_rec *rec = &f->rec[value];
	if (rec->sd < 0 || rec->sd >= f->nsd || f->sd[rec->sd].nfield != rec->n)
		return l_fail(L, "record %d has no struct definition", value);
	struct ss_sd *sd = &f->sd[rec->sd];

	int slot = ss_struct_slot(loc_ptr(&loc)->i);
	if (slot < 0) {
		/* 0x679d60: create the struct named in the file, with its constructor */
		int no = f->sd_ain ? f->sd_ain[rec->sd] : -1;
		if (no < 0)
			return l_fail(L, "unknown struct '%s'", sd->name);
		if (decl_struct >= 0 && no != decl_struct)
			SS_WARN("system.DeserializeStruct('%s'): file struct '%s' for a '%s' member",
				display_game0(L->label), display_game1(sd->name), display_game2(ss_struct_name(decl_struct)));
		int s = vm_construct_struct(no);
		if (s <= 0)
			return l_fail(L, "cannot construct '%s'", sd->name);
		L->new_structs++;
		loc_ptr(&loc)->i = s;
		slot = s;
	}
	heap_ref(slot);
	bool ok = l_fields(L, slot, rec, sd, depth);
	heap_unref(slot);
	return ok;
}

/* xsystem4 array type for a new array whose destination had no page (X_A_INIT). */
static void l_new_array_meta(const struct ain_type *elem, struct page *np)
{
	np->array.rank = 1;
	switch (elem->data) {
	case AIN_INT: case AIN_BOOL: case AIN_ENUM:
		np->a_type = AIN_ARRAY_INT;
		np->array.struct_type = -1;
		break;
	case AIN_FLOAT:
		np->a_type = AIN_ARRAY_FLOAT;
		np->array.struct_type = -1;
		break;
	case AIN_STRING:
		np->a_type = AIN_ARRAY_STRING;
		np->array.struct_type = -1;
		break;
	case AIN_STRUCT:
		np->a_type = AIN_ARRAY_STRUCT;
		np->array.struct_type = elem->struc;
		break;
	default: {
		/* element slot count, as X_A_INIT stores it; l_array keeps the
		 * destination of a multi-slot element array and never comes here
		 * with one, so this is 1 */
		int slots = ss_slot_count(elem);
		np->a_type = AIN_ARRAY;
		np->array.struct_type = slots;
		np->array.elem_slots = slots > 1 ? slots : 0;
		break;
	}
	}
}

static void l_release_elements(struct page *p, int count, const struct ain_type *elem)
{
	if (!p || !ss_elem_owns_slot(elem))
		return;
	for (int k = 0; k < count && k < p->nr_vars; k++)
		l_release(p->values[k].i);
}

static bool l_element(struct ss_loader *L, struct page *np, int k, const struct ain_type *elem,
		      int32_t kv, int depth)
{
	union vm_value *v = &np->values[k];
	int code = ss_valuetype(elem);
	switch (ss_kind(code)) {
	case SS_RAW:
		v->i = kv;
		return true;
	case SS_STR: {
		v->i = -1;
		struct ss_loc loc = { -1, -1, v };
		return l_string(L, loc, kv);
	}
	case SS_STRUCT: {
		v->i = -1;
		if (kv == -1)
			return true;
		/* 0x67fe20(n, 1) -> 0x656970: elements are constructed with the declared
		 * struct type, then loaded in place */
		if (elem->struc >= 0 && elem->struc < ain->nr_structures) {
			int s = vm_construct_struct(elem->struc);
			if (s <= 0)
				return l_fail(L, "cannot construct '%s'", ss_struct_name(elem->struc));
			L->new_structs++;
			v->i = s;
		}
		struct ss_loc loc = { -1, -1, v };
		return l_struct(L, loc, kv, elem->struc, depth);
	}
	case SS_ZERO:
		/* 0x656970: a delegate element starts as a new empty delegate */
		v->i = elem->data == AIN_DELEGATE ? variable_initval(AIN_DELEGATE).i : 0;
		return true;
	case SS_NULLREF:
		v->i = -1; /* reference payloads are not in the file */
		return true;
	case SS_ARRAY:
	default:
		return l_fail(L, "array element type %d is not supported", code);
	}
}

static bool l_array(struct ss_loader *L, struct ss_loc loc, int32_t value, const struct ain_type *decl, int depth)
{
	struct ss_file *f = L->f;
	if (value < 0)
		return true; /* 0x65d5b0: unchanged */
	if (value >= f->narr)
		return l_fail(L, "WriteArray: array %d out of range", value);
	if (depth > SS_MAX_DEPTH)
		return l_fail(L, "nesting deeper than %d", SS_MAX_DEPTH);
	int slot = loc_ptr(&loc)->i;
	if (slot <= 1 || !page_index_valid(slot))
		return l_fail(L, "array destination %d is not an array", slot);
	struct page *old = heap[slot].page;
	if (old && old->type != ARRAY_PAGE)
		return l_fail(L, "array destination %d is not an array", slot);
	const struct ain_type *elem = decl ? decl->array_type : NULL;
	if (!elem)
		return l_fail(L, "array without element type");
	struct ss_arr *a = &f->arr[value];
	if (!a->usable)
		return l_fail(L, "array %d has rank %d (not supported)", value, a->rank);
	int nn = a->n;
	if (nn > 0 && (ss_is_nested_array(elem) || ss_is_option(elem)))
		return l_fail(L, "array element type %d is not supported", elem->data);
	if (ss_slot_count(elem) != 1) {
		/* Two-slot elements (interfaces, references) have no data in the file
		 * and their xsystem4 layout varies; keep the destination. */
		SS_WARN("system.DeserializeStruct('%s'): two-slot element array kept", L->label);
		L->kept_arrays++;
		return true;
	}
	L->elem_budget -= nn;
	if (L->elem_budget < 0)
		return l_fail(L, "array elements exceed the file size");

	struct page *np = alloc_page(ARRAY_PAGE, AIN_ARRAY, nn);
	if (old) {
		np->a_type = old->a_type;
		np->array = old->array;
		np->array.rank = 1;
	} else {
		l_new_array_meta(elem, np);
	}
	for (int k = 0; k < nn; k++)
		np->values[k].i = ss_kind(ss_valuetype(elem)) == SS_RAW ? 0 : -1;
	for (int k = 0; k < nn; k++) {
		if (!l_element(L, np, k, elem, a->v[k], depth + 1)) {
			/* the destination array is untouched on failure */
			l_release_elements(np, nn, elem);
			free_page(np);
			return false;
		}
	}
	/* Attach the new page first, then release the old elements, so that a
	 * destructor never sees a half-released page in the destination slot. */
	old = heap[slot].page;
	if (old && old->type != ARRAY_PAGE) {
		l_release_elements(np, nn, elem);
		free_page(np);
		return l_fail(L, "array destination %d changed while loading", slot);
	}
	heap_set_page(slot, np);
	L->arrays++;
	if (old) {
		int cnt = old->nr_vars;
		int *vals = cnt ? xmalloc(sizeof(int) * cnt) : NULL;
		for (int k = 0; k < cnt; k++)
			vals[k] = old->values[k].i;
		free_page(old);
		if (ss_elem_owns_slot(elem))
			for (int k = 0; k < cnt; k++)
				l_release(vals[k]);
		free(vals);
	}
	return true;
}

bool ss_deserialize_file(const char *path, const char *label, const int *roots, int n)
{
	struct ss_file f;
	const char *why = "";
	bool ok = ss_load_file(path, label, &f, &why) && ss_parse(&f, label, &why) && ss_validate(&f, n, &why);
	if (!ok) {
		if (strcmp(why, "read") && strcmp(why, "magic") && strcmp(why, "group"))
			SS_WARN("system.DeserializeStruct('%s'): invalid file (%s); nothing loaded", label, why);
		ss_trace("des %s roots=%d ver=%d fail(%s)", label, n, f.ver, why);
		ss_file_free(&f);
		return false;
	}
	struct ss_loader L = { .f = &f, .label = label };
	L.struct_budget = 2LL * f.nrec + 64;
	L.elem_budget = 2LL * f.flat_total + 64;
	heap_gc_inhibit();
	for (int i = 0; i < n && ok; i++) {
		int h = roots[i];
		heap_ref(h);
		union vm_value local = { .i = h }; /* 0x65c9d7: a copy of the handle */
		struct ss_loc loc = { -1, -1, &local };
		ok = l_struct(&L, loc, f.glob[i].value, -1, 0);
		heap_unref(h);
	}
	heap_gc_allow();
	if (!ok)
		SS_WARN("system.DeserializeStruct('%s'): %s; partially loaded", display_game0(label), display_game1(L.why));
	ss_trace("des %s roots=%d ver=%d %s%s%s new_structs=%d new_strings=%d arrays=%d kept2=%d shared_str=%d",
		 label, n, f.ver, ok ? "ok" : "fail(", ok ? "" : L.why, ok ? "" : ")", L.new_structs,
		 L.new_strings, L.arrays, L.kept_arrays, L.shared_strings);
	ss_file_free(&f);
	return ok;
}

/* --- comments (0x65f3b0 / 0x65c250) --------------------------------------- */

bool ss_write_comment_file(const char *path, const char *label, const char *bytes, size_t len)
{
	struct ss_file f;
	const char *why = "";
	bool ok = false;
	if (!len) {
		SS_WARN("system.WriteSerializeStructComment('%s'): empty comment", label);
		ss_trace("cmt-w %s len=0 fail(empty)", label);
		return false;
	}
	if (!ss_load_file(path, label, &f, &why)) {
		SS_WARN("system.WriteSerializeStructComment('%s'): cannot read the save file", label);
		goto out;
	}
	struct ss_rd r = { f.raw, f.len, 0, false };
	size_t hdr = ss_parse_head(&f, &r, label, &why);
	if (!hdr)
		goto out;
	if (f.ver <= 7) {
		SS_WARN("system.WriteSerializeStructComment('%s'): version %d has no comment", label, f.ver);
		why = "version";
		goto out;
	}
	size_t hdr_off = strlen(SS_KEY) + 1;
	int32_t end = f.h[14];
	if (end < (int32_t)(hdr_off + hdr) || (size_t)end > f.len) {
		why = "comment offset";
		goto out;
	}
	{
		struct ss_bytes b = { 0 };
		bb_put(&b, f.raw, (size_t)end);
		le32_store(b.p + hdr_off + 4 * 15, 1);
		bb_i32(&b, (int32_t)len);
		bb_put(&b, bytes, len);
		ok = ss_write_container(path, label, b.p, b.n);
		free(b.p);
	}
out:
	ss_trace("cmt-w %s len=%zu %s%s%s", label, len, ok ? "ok" : "fail(", ok ? "" : why, ok ? "" : ")");
	ss_file_free(&f);
	return ok;
}

int ss_read_comment_file(const char *path, const char *label, struct string **out)
{
	struct ss_file f;
	const char *why = "";
	int result = -1;
	*out = NULL;
	if (!ss_load_file(path, label, &f, &why))
		goto out;
	struct ss_rd r = { f.raw, f.len, 0, false };
	size_t hdr = ss_parse_head(&f, &r, label, &why);
	if (!hdr)
		goto out;
	if (f.ver <= 8) {
		why = "version";
		goto out;
	}
	if (f.h[15] == 0) {
		result = 0;
		goto out;
	}
	int32_t pos = f.h[14];
	if (pos < 0 || (size_t)pos > f.len || f.len - (size_t)pos < 4) {
		why = "comment offset";
		goto out;
	}
	int32_t n = le32_load(f.raw + pos);
	if (n == 0) {
		result = 0;
		goto out;
	}
	if (n < 0 || (size_t)n > f.len - (size_t)pos - 4) {
		why = "comment length";
		goto out;
	}
	const char *p = (const char *)f.raw + pos + 4;
	*out = make_string(p, (int)strnlen(p, (size_t)n));
	result = 1;
out:
	ss_trace("cmt-r %s r=%d%s%s%s", label, result, result < 0 ? " (" : "", result < 0 ? why : "",
		 result < 0 ? ")" : "");
	ss_file_free(&f);
	return result;
}
