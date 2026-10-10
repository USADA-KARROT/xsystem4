/* msgskip_msk_fixture.inc — the read-message table in the original file
 * format (2026-10-11). Mode: msgskip-msk.
 *
 * AIN v14 declares MsgSkip as SetFlag, GetFlag, SetAdvFlag and GetAdvFlag
 * only; the engine owns the table and keeps it in MsgSkip.msk. Original
 * (static disassembly): init 0x4c7650, reader 0x4c7b50, writer 0x4c7840,
 * SetFlag 0x4c8700, GetFlag 0x4c7700, SetAdvFlag 0x4c7760, GetAdvFlag
 * 0x4c8750, dispatch 0x4c7230. The writer's only caller is 0x4c1b60, reached
 * from the window close (0x4be9b0) and from the first step of a reset
 * (0x4b5bc3 -> 0x4c12e0, before the object is released at 0x4b30f0).
 *
 * Real AIN and real ffi (hll_call); the library is started and stopped the
 * way init_libraries / exit_libraries do (_PreLink, _PostLink, _ModuleFini).
 * Every case runs in a forked child with a private save folder (the
 * save-persist one: mkdtemp under $XS4_SAVE_TMP or $TMPDIR, refused inside
 * XS4_SRC or XS4_MASTER_GAME, removed at the end). All file contents are
 * synthetic and built here with independent arithmetic. Symbols that only
 * exist after the change are looked up with dlsym so that before-check.sh
 * can build this file against older sources.
 *
 * M1  a new table is written byte for byte: header, bit order, ADV count;
 *     SetFlag ignores numbers outside the table and writes nothing itself.
 * M2  a synthetic file is read, written back unchanged (also when it was
 *     deleted meanwhile: the writer does not look for changes), and one more
 *     flag changes exactly one bit after a reload. A second _ModuleFini in a
 *     row does nothing.
 * M3  files that must not be loaded: magic, version, zero or oversized table,
 *     truncation, negative count, a table for more messages. The original
 *     gives up on each of them except the table size -1, where it fails to
 *     allocate that many bytes; this port gives up there too.
 * M4  size mismatches that still load: fewer messages (whole bytes are
 *     copied), more messages in the same number of bytes, and a table shorter
 *     than its count (the original reads past its buffer there; the missing
 *     bytes are zero here).
 * M5  partial reads: the second string is kept once the version is accepted,
 *     but not when the file ends before its terminator; a cut ADV list (in
 *     an entry's step, or in a name without terminator) keeps the table and
 *     the complete entries; the first of two entries with one name wins.
 * M6  ADV flags: add, same version keeps the larger step, another version
 *     replaces, queries, file round trip, a name with high bytes.
 * M7  msgskip_get_flag / msgskip_get_adv_flag answer like the HLL functions.
 * M8  a write that cannot be completed keeps the previous file.
 * M9  guard, AIN v13: the old MsgSkip.asd format is read and written, the
 *     .msk file is ignored and left alone, the ADV functions stay unlinked.
 *     A second start keeps the table without reading the file again, and
 *     the real vm_reset writes the old format too.
 * M10 guard, a library that declares Init: nothing is set up before the game
 *     calls Init; the file it names is written in the old format.
 * M11 vm_exit writes the file (the whole exit path, in the child); it
 *     leaves an existing file alone when the libraries were never started.
 * M12 vm_reset writes the file, drops the table and reads it back on restart.
 * M13 guard: SetAdvFlag / GetAdvFlag are linked only for the declaration the
 *     original dispatches; another argument type, return type or argument
 *     count stays unlinked.
 * M14 system.Reset through ffi writes the file and keeps the table (the
 *     original's reset writes before it releases anything); nothing is
 *     written once the library has stopped, or in the old format.
 *
 * Not covered: the file name prefix of the original (always empty here),
 * the order the original's unordered_map writes ADV entries in, the Windows
 * branch of the writer, and a restart through vm_execute_ain after a reset
 * (M12 runs _PreLink itself).
 *
 * Optional mode msgskip-msk-file (not in XS4_MODES): XS4_MSK_COPY=<a copy of
 * a MsgSkip.msk written by the original engine>. The copy is only read; it
 * is loaded from the private folder, the runs of read messages are printed,
 * and the file written back must equal the copy byte for byte.
 */
#include <setjmp.h>

extern struct static_library lib_MsgSkip;

static int mk_failed_cases, mk_cases;

#define MK_CHECK(c, ...) do {                                                   \
		if (!(c)) {                                                     \
			printf("  FAIL %s:%d: ", __func__, __LINE__);            \
			printf(__VA_ARGS__);                                    \
			printf("\n");                                           \
			fflush(stdout);                                         \
			return 1;                                               \
		}                                                               \
	} while (0)

#define MK_MSK "MsgSkip.msk"
#define MK_ASD "MsgSkip.asd"

/* ---- files -------------------------------------------------------------- */

struct mk_buf {
	uint8_t b[32768];
	size_t n;
};

static void mk_bytes(struct mk_buf *m, const void *p, size_t n)
{
	assert(m->n + n <= sizeof m->b);
	memcpy(m->b + m->n, p, n);
	m->n += n;
}

static void mk_i32(struct mk_buf *m, int32_t v)
{
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	mk_bytes(m, b, 4);
}

static void mk_str(struct mk_buf *m, const char *s) { mk_bytes(m, s, strlen(s) + 1); }

static void mk_adv(struct mk_buf *m, const char *name, int version, int step)
{
	mk_str(m, name);
	mk_i32(m, version);
	mk_i32(m, step);
}

/* Everything up to and including the bit table. */
static void mk_head(struct mk_buf *m, const char *tag, int nr_msgs, int nr_bytes,
		    const uint8_t *bits, size_t bits_len)
{
	m->n = 0;
	mk_str(m, "MSK");
	mk_i32(m, 0);
	mk_str(m, tag);
	mk_i32(m, nr_msgs);
	mk_i32(m, nr_bytes);
	mk_bytes(m, bits, bits_len);
}

static int mk_nr(void) { return ain->nr_messages; }
static int mk_nb(void) { return (ain->nr_messages + 7) / 8; }

static void mk_bit(uint8_t *table, int n) { table[n / 8] |= (uint8_t)(0x80 >> (n % 8)); }

static const char *mk_path(const char *name)
{
	static char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/%s", sp_dir, name);
	return p;
}

static bool mk_put(const char *name, const void *data, size_t len)
{
	FILE *f = fopen(mk_path(name), "wb");
	if (!f)
		return false;
	bool ok = !len || fwrite(data, len, 1, f) == 1;
	return fclose(f) == 0 && ok;
}

static bool mk_exists(const char *name) { return access(mk_path(name), F_OK) == 0; }

/* 0 when <name> holds exactly these bytes. */
static int mk_expect(const char *name, const void *want, size_t len)
{
	FILE *f = fopen(mk_path(name), "rb");
	MK_CHECK(f, "%s was not written", name);
	static uint8_t got[65536];
	size_t n = fread(got, 1, sizeof got, f);
	fclose(f);
	MK_CHECK(n == len, "%s is %zu bytes, want %zu", name, n, len);
	const uint8_t *w = want;
	for (size_t i = 0; i < len; i++)
		MK_CHECK(got[i] == w[i], "%s differs at offset %zu: 0x%02x, want 0x%02x", name, i, got[i], w[i]);
	return 0;
}

/* 0 when the save folder holds only the named files (no temporary left). */
static int mk_only(const char *a, const char *b)
{
	DIR *d = opendir(sp_dir);
	MK_CHECK(d, "cannot list the save folder");
	struct dirent *e;
	char stray[256] = "";
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if ((a && !strcmp(e->d_name, a)) || (b && !strcmp(e->d_name, b)))
			continue;
		snprintf(stray, sizeof stray, "%s", e->d_name);
	}
	closedir(d);
	MK_CHECK(!stray[0], "unexpected file in the save folder: %s", stray);
	return 0;
}

/* ---- the library ------------------------------------------------------ */

static void mk_start(void)
{
	library_run(&lib_MsgSkip, "_PreLink");
	library_run(&lib_MsgSkip, "_PostLink");
}

static void mk_stop(void) { library_run(&lib_MsgSkip, "_ModuleFini"); }

static int mk_lib(void) { return sp_lib("MsgSkip"); }

static int mk_get(int n)
{
	int lib = mk_lib(), f = sp_libfn(lib, "GetFlag"), sp = stack_ptr;
	stack_push(n);
	hll_call(lib, f, 0);
	assert(stack_ptr == sp + 1);
	return stack_pop().i;
}

static void mk_set(int n)
{
	int lib = mk_lib(), f = sp_libfn(lib, "SetFlag"), sp = stack_ptr;
	stack_push(n);
	hll_call(lib, f, 0);
	assert(stack_ptr == sp);
}

static void mk_set_adv(const char *name, int version, int step)
{
	int lib = mk_lib(), f = sp_libfn(lib, "SetAdvFlag"), sp = stack_ptr;
	stack_push(sp_str(name));
	stack_push(version);
	stack_push(step);
	hll_call(lib, f, 0);
	assert(stack_ptr == sp);
}

static int mk_get_adv(const char *name, int version, int step)
{
	int lib = mk_lib(), f = sp_libfn(lib, "GetAdvFlag"), sp = stack_ptr;
	stack_push(sp_str(name));
	stack_push(version);
	stack_push(step);
	hll_call(lib, f, 0);
	assert(stack_ptr == sp + 1);
	return stack_pop().i;
}

static void mk_system_reset(void)
{
	int lib = sp_lib("system"), f = sp_libfn(lib, "Reset"), sp = stack_ptr;
	hll_call(lib, f, 0);
	assert(stack_ptr == sp);
}

/* Number of set flags among 0..limit-1, through GetFlag. */
static int mk_count(int limit)
{
	int c = 0;
	for (int i = 0; i < limit; i++)
		c += mk_get(i) ? 1 : 0;
	return c;
}

static void mk_run(const char *name, int (*fn)(void))
{
	printf("== %s\n", name);
	fflush(stdout);
	mk_cases++;
	pid_t pid = fork();
	if (pid == 0) {
		alarm(120);
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
		printf("%-52s FAIL (signal %d)\n", name, WTERMSIG(st));
		mk_failed_cases++;
	} else if (WEXITSTATUS(st)) {
		printf("%-52s FAIL (exit %d)\n", name, WEXITSTATUS(st));
		mk_failed_cases++;
	} else {
		printf("%-52s PASS\n", name);
	}
	fflush(stdout);
}

/* ---- M1: a new table, byte for byte ------------------------------------ */

static int mk1_new_file(void)
{
	int nr = mk_nr(), nb = mk_nb();
	MK_CHECK(nr > 2000 && nb < 16000, "unexpected message count %d", nr);
	mk_start();
	MK_CHECK(!mk_get(0) && !mk_get(7) && !mk_get(8) && !mk_get(nr - 1), "a new table has flags set");
	mk_set(0);
	mk_set(7);
	mk_set(8);
	mk_set(nr - 1);
	mk_set(-1);
	mk_set(nr);
	mk_set(INT_MAX);
	mk_set(INT_MIN);
	MK_CHECK(mk_get(0) && mk_get(7) && mk_get(8) && mk_get(nr - 1), "SetFlag did not set its flag");
	MK_CHECK(!mk_get(1) && !mk_get(6) && !mk_get(9) && !mk_get(nr - 2), "SetFlag set a neighbour");
	MK_CHECK(!mk_get(-1) && !mk_get(nr) && !mk_get(INT_MAX) && !mk_get(INT_MIN),
		 "GetFlag answered for a number outside the table");
	MK_CHECK(mk_count(2000) == 3, "%d flags set below 2000, want 3", mk_count(2000));
	MK_CHECK(!mk_exists(MK_MSK), "SetFlag wrote the file");
	mk_stop();

	static uint8_t bits[16000];
	bits[0] = 0x81; /* message 0 is the top bit, message 7 the bottom one */
	bits[1] = 0x80;
	mk_bit(bits, nr - 1);
	static struct mk_buf want;
	mk_head(&want, "", nr, nb, bits, nb);
	mk_i32(&want, 0);
	MK_CHECK(want.n == (size_t)(17 + nb + 4), "fixture arithmetic");
	static const uint8_t header[9] = { 'M', 'S', 'K', 0, 0, 0, 0, 0, 0 };
	MK_CHECK(!memcmp(want.b, header, 9), "fixture header");
	if (mk_expect(MK_MSK, want.b, want.n) || mk_only(MK_MSK, NULL))
		return 1;
	printf("  %zu bytes: 17 header + %d table + 4 ADV count\n", want.n, nb);
	return 0;
}

/* ---- M2: read, round trip, one more flag --------------------------------- */

static void mk2_file(struct mk_buf *m, uint8_t *bits)
{
	memset(bits, 0, mk_nb());
	for (int i = 3; i <= 20; i++)
		mk_bit(bits, i);
	mk_bit(bits, 100);
	for (int i = 777; i <= 800; i++)
		mk_bit(bits, i);
	mk_head(m, "t\x81g", mk_nr(), mk_nb(), bits, mk_nb());
	mk_i32(m, 2);
	mk_adv(m, "ADV_a", 3, 12);
	mk_adv(m, "\xb2\xe2\xca\xd4", -1, 0x01020304);
}

static int mk2_read_roundtrip(void)
{
	static uint8_t bits[16000];
	static struct mk_buf file;
	mk2_file(&file, bits);
	MK_CHECK(mk_put(MK_MSK, file.b, file.n), "cannot write the fixture file");

	mk_start();
	MK_CHECK(!mk_get(0) && !mk_get(2) && mk_get(3) && mk_get(20) && !mk_get(21), "flags 3..20 not read");
	MK_CHECK(!mk_get(99) && mk_get(100) && !mk_get(101), "flag 100 not read");
	MK_CHECK(!mk_get(776) && mk_get(777) && mk_get(800) && !mk_get(801), "flags 777..800 not read");
	MK_CHECK(mk_count(2000) == 18 + 1 + 24, "%d flags read, want 43", mk_count(2000));
	MK_CHECK(!mk_get(-1) && !mk_get(mk_nr()), "GetFlag answered outside the table");
	mk_stop();
	if (mk_expect(MK_MSK, file.b, file.n) || mk_only(MK_MSK, NULL))
		return 1;

	/* The writer writes every time, changed or not. */
	mk_start();
	MK_CHECK(unlink(mk_path(MK_MSK)) == 0, "cannot delete the fixture file");
	mk_stop();
	if (mk_expect(MK_MSK, file.b, file.n))
		return 1;

	mk_start();
	MK_CHECK(!mk_get(1000), "flag 1000 set before SetFlag");
	mk_set(1000);
	mk_set(100); /* already set */
	mk_stop();
	size_t table = 4 + 4 + 4 + 4 + 4; /* magic, version, "t\x81g\0", count, size */
	MK_CHECK(file.b[table + 125] == 0, "fixture arithmetic");
	file.b[table + 125] = 0x80; /* message 1000 = byte 125, top bit */
	if (mk_expect(MK_MSK, file.b, file.n))
		return 1;

	mk_start();
	MK_CHECK(mk_get(1000) && mk_get(3) && mk_get(800) && !mk_get(1001), "flags lost over a reload");
	MK_CHECK(mk_count(2000) == 44, "%d flags after the reload, want 44", mk_count(2000));
	mk_stop();
	mk_stop(); /* nothing is loaded any more: must not write an empty table */
	return mk_expect(MK_MSK, file.b, file.n);
}

/* ---- M3: files that must not be loaded ----------------------------------- */

/* The file is offered with every bit set; nothing may be loaded from it. The
 * next write replaces it with an empty table carrying want_tag. */
static int mk3_rejected(const char *label, const struct mk_buf *file, const char *want_tag)
{
	sp_clear_dir();
	MK_CHECK(mk_put(MK_MSK, file->b, file->n), "%s: cannot write the fixture file", label);
	mk_start();
	int set = mk_count(300) + mk_get(mk_nr() - 1);
	if (set) {
		printf("  FAIL %s: %d flags read from a file that must not be loaded\n", label, set);
		return 1;
	}
	mk_stop();
	static uint8_t zero[16000];
	static struct mk_buf want;
	mk_head(&want, want_tag, mk_nr(), mk_nb(), zero, mk_nb());
	mk_i32(&want, 0);
	if (mk_expect(MK_MSK, want.b, want.n)) {
		printf("  (%s)\n", label);
		return 1;
	}
	return 0;
}

static int mk3_bad_files(void)
{
	int nr = mk_nr(), nb = mk_nb();
	static uint8_t ones[16000];
	memset(ones, 0xff, sizeof ones);
	static struct mk_buf f;

	mk_head(&f, "", nr, nb, ones, nb);
	mk_i32(&f, 0);
	f.b[2] = 'X';
	if (mk3_rejected("magic MSX", &f, ""))
		return 1;

	mk_head(&f, "", nr, nb, ones, nb);
	mk_i32(&f, 0);
	f.b[3] = 'K'; /* the magic string runs on into the version: "MSKK" */
	if (mk3_rejected("magic MSKK", &f, ""))
		return 1;

	f.n = 0;
	mk_bytes(&f, "MSK", 3);
	if (mk3_rejected("three bytes", &f, ""))
		return 1;

	mk_head(&f, "", nr, nb, ones, nb);
	mk_i32(&f, 0);
	f.b[4] = 1;
	if (mk3_rejected("version 1", &f, ""))
		return 1;

	mk_head(&f, "", nr, 0, ones, 0);
	mk_i32(&f, 1);
	mk_adv(&f, "unreached", 1, 1); /* the reader stops before the ADV list */
	if (mk3_rejected("table size 0", &f, ""))
		return 1;

	mk_head(&f, "", nr, nb, ones, nb - 1);
	if (mk3_rejected("table cut short", &f, ""))
		return 1;

	/* The original does not give up here: it tries to allocate 0xffffffff
	 * bytes for the table and fails. Giving up is this port's choice. */
	mk_head(&f, "", nr, -1, ones, nb);
	mk_i32(&f, 0);
	if (mk3_rejected("table size -1", &f, ""))
		return 1;

	mk_head(&f, "", nr, nb, ones, 0);
	f.n -= 2; /* ends inside the table size */
	if (mk3_rejected("header cut short", &f, ""))
		return 1;

	mk_head(&f, "", -1, nb, ones, nb);
	mk_i32(&f, 0);
	if (mk3_rejected("message count -1", &f, ""))
		return 1;

	mk_head(&f, "", 0, nb, ones, nb);
	mk_i32(&f, 0);
	if (mk3_rejected("message count 0", &f, ""))
		return 1;

	mk_head(&f, "", INT_MIN, nb, ones, nb);
	mk_i32(&f, 0);
	if (mk3_rejected("message count INT_MIN", &f, ""))
		return 1;

	/* A table for more messages than the AIN has is not loaded at all. */
	mk_head(&f, "", nr + 8, nb + 1, ones, nb + 1);
	mk_i32(&f, 0);
	if (mk3_rejected("more messages", &f, ""))
		return 1;

	/* Fewer messages, but a table larger than they need. */
	mk_head(&f, "", 100, 20, ones, 20);
	mk_i32(&f, 0);
	if (mk3_rejected("table larger than its count", &f, ""))
		return 1;

	/* An empty file counts as unreadable. */
	f.n = 0;
	if (mk3_rejected("empty file", &f, ""))
		return 1;
	printf("  14 files not loaded and replaced by an empty table\n");
	return 0;
}

/* ---- M4: size mismatches that still load ---------------------------------- */

static int mk4_size_mismatch(void)
{
	int nr = mk_nr(), nb = mk_nb();
	static uint8_t ones[16000];
	memset(ones, 0xff, sizeof ones);
	static struct mk_buf f;

	/* 100 messages in 13 bytes: the 13 bytes are copied whole, so the four
	 * spare bits of the last byte come along (0x4c7e62 copies bytes). */
	mk_head(&f, "", 100, 13, ones, 13);
	mk_i32(&f, 0);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(0) && mk_get(99) && mk_get(103), "the leading part of a smaller table was not read");
	MK_CHECK(!mk_get(104) && !mk_get(nr - 1), "flags beyond a smaller table are set");
	MK_CHECK(mk_count(300) == 104, "%d flags, want 104", mk_count(300));
	mk_stop();
	static uint8_t bits[16000];
	memset(bits, 0, sizeof bits);
	memset(bits, 0xff, 13);
	static struct mk_buf want;
	mk_head(&want, "", nr, nb, bits, nb);
	mk_i32(&want, 0);
	if (mk_expect(MK_MSK, want.b, want.n))
		return 1;

	/* More messages that still fit in the same number of bytes: loaded.
	 * Needs spare bits in the last byte, which this AIN has. */
	MK_CHECK(nr % 8 != 0, "this AIN has no spare bits; the case needs another count");
	sp_clear_dir();
	memset(bits, 0, sizeof bits);
	mk_bit(bits, 5);
	mk_bit(bits, nr - 1);
	mk_head(&f, "", nb * 8, nb, bits, nb);
	mk_i32(&f, 0);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(5) && mk_get(nr - 1) && !mk_get(6), "a table with the same byte count was not read");
	mk_stop();
	mk_head(&want, "", nr, nb, bits, nb);
	mk_i32(&want, 0);
	if (mk_expect(MK_MSK, want.b, want.n))
		return 1;

	/* 100 messages but only 5 bytes of table. The original copies 13 bytes
	 * from the 5-byte buffer; here the missing ones are zero. */
	sp_clear_dir();
	mk_head(&f, "", 100, 5, ones, 5);
	mk_i32(&f, 0);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(0) && mk_get(39), "a short table was not read");
	MK_CHECK(mk_count(300) == 40, "%d flags from a 5-byte table, want 40", mk_count(300));
	mk_stop();
	return 0;
}

/* ---- M5: partial reads ------------------------------------------------- */

static int mk5_partial(void)
{
	int nr = mk_nr(), nb = mk_nb();
	static uint8_t ones[16000], bits[16000];
	memset(ones, 0xff, sizeof ones);
	static struct mk_buf f, want;

	/* The second string is stored as soon as the version is accepted
	 * (0x4c7dab), before the size check that fails here. */
	mk_head(&f, "kept", nr, 0, ones, nb);
	mk_i32(&f, 0);
	if (mk3_rejected("tag before a zero table size", &f, "kept"))
		return 1;

	/* The file ends inside the second string: nothing is stored
	 * (0x72c8b0 fails at the end of the data, 0x4c7da5 skips the assign).
	 * file_read keeps a NUL after the data, so only this check tells a
	 * reader that runs past the end. */
	f.n = 0;
	mk_str(&f, "MSK");
	mk_i32(&f, 0);
	mk_bytes(&f, "kept", 4);
	if (mk3_rejected("second string without its terminator", &f, ""))
		return 1;

	/* The ADV list ends after one complete entry: table and entry stay. */
	sp_clear_dir();
	memset(bits, 0, sizeof bits);
	mk_bit(bits, 42);
	mk_head(&f, "", nr, nb, bits, nb);
	mk_i32(&f, 3);
	mk_adv(&f, "first", 1, 4);
	mk_str(&f, "second");
	mk_i32(&f, 9); /* its step is missing */
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(42) && mk_count(300) == 1, "the table before a cut ADV list was dropped");
	MK_CHECK(mk_get_adv("first", 1, 4), "the complete ADV entry was dropped");
	MK_CHECK(!mk_get_adv("second", 9, 0), "an incomplete ADV entry was kept");
	mk_stop();
	mk_head(&want, "", nr, nb, bits, nb);
	mk_i32(&want, 1);
	mk_adv(&want, "first", 1, 4);
	if (mk_expect(MK_MSK, want.b, want.n))
		return 1;

	/* The second entry's name runs to the end of the file. */
	sp_clear_dir();
	mk_head(&f, "", nr, nb, bits, nb);
	mk_i32(&f, 2);
	mk_adv(&f, "first", 1, 4);
	mk_bytes(&f, "second", 6);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(42) && mk_count(300) == 1, "the table before an unterminated ADV name was dropped");
	MK_CHECK(mk_get_adv("first", 1, 4), "the entry before an unterminated ADV name was dropped");
	MK_CHECK(!mk_get_adv("second", 0, 0), "an ADV entry without a terminated name was kept");
	mk_stop();
	if (mk_expect(MK_MSK, want.b, want.n)) /* still: table, 1 entry, "first" */
		return 1;

	/* No ADV count at all: the table stays. */
	sp_clear_dir();
	mk_head(&f, "", nr, nb, bits, nb);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(42) && mk_count(300) == 1, "the table of a file without an ADV count was dropped");
	mk_stop();

	/* A negative or absurd ADV count reads nothing more and does not crash. */
	sp_clear_dir();
	mk_head(&f, "", nr, nb, bits, nb);
	mk_i32(&f, -5);
	mk_adv(&f, "ignored", 1, 1);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(42) && !mk_get_adv("ignored", 1, 1), "a negative ADV count read entries");
	mk_stop();
	sp_clear_dir();
	mk_head(&f, "", nr, nb, bits, nb);
	mk_i32(&f, INT_MAX);
	mk_adv(&f, "one", 1, 1);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(42) && mk_get_adv("one", 1, 1), "an oversized ADV count lost the entries present");
	mk_stop();

	/* Two entries with one name: the first is kept (map insert). */
	sp_clear_dir();
	mk_head(&f, "", nr, nb, bits, nb);
	mk_i32(&f, 2);
	mk_adv(&f, "dup", 1, 7);
	mk_adv(&f, "dup", 2, 9);
	MK_CHECK(mk_put(MK_MSK, f.b, f.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get_adv("dup", 1, 7) && !mk_get_adv("dup", 2, 9), "the later of two entries with one name won");
	mk_stop();
	mk_head(&want, "", nr, nb, bits, nb);
	mk_i32(&want, 1);
	mk_adv(&want, "dup", 1, 7);
	return mk_expect(MK_MSK, want.b, want.n);
}

/* ---- M6: ADV flags ------------------------------------------------------- */

static int mk6_adv(void)
{
	int nr = mk_nr(), nb = mk_nb();
	static const char *high = "\xb2\xe2\xca\xd4";
	mk_start();
	MK_CHECK(!mk_get_adv("a", 1, 0), "an unknown name is read");
	mk_set_adv("a", 1, 5);
	MK_CHECK(mk_get_adv("a", 1, 5) && mk_get_adv("a", 1, 4) && mk_get_adv("a", 1, -3),
		 "steps up to the recorded one are not read");
	MK_CHECK(!mk_get_adv("a", 1, 6), "a step beyond the recorded one is read");
	MK_CHECK(!mk_get_adv("a", 2, 0) && !mk_get_adv("a", 0, 0), "another version is read");
	MK_CHECK(!mk_get_adv("A", 1, 0) && !mk_get_adv("", 1, 0), "another name is read");

	mk_set_adv("a", 1, 3); /* same version: the step only grows */
	MK_CHECK(mk_get_adv("a", 1, 5), "a smaller step lowered the recorded one");
	mk_set_adv("a", 1, 9);
	MK_CHECK(mk_get_adv("a", 1, 9) && !mk_get_adv("a", 1, 10), "a larger step was not recorded");

	mk_set_adv("a", 2, 1); /* another version replaces version and step */
	MK_CHECK(!mk_get_adv("a", 1, 0), "the old version is still read");
	MK_CHECK(mk_get_adv("a", 2, 1) && !mk_get_adv("a", 2, 2), "the new version's step is wrong");

	mk_set_adv(high, 7, 7);
	mk_set_adv("", -2, INT_MIN);
	MK_CHECK(mk_get_adv(high, 7, 7) && mk_get_adv("", -2, INT_MIN) && !mk_get_adv("", -2, INT_MIN + 1),
		 "names with high bytes or the empty name are not kept");
	MK_CHECK(!mk_exists(MK_MSK), "SetAdvFlag wrote the file");
	mk_stop();

	static uint8_t zero[16000];
	static struct mk_buf want;
	mk_head(&want, "", nr, nb, zero, nb);
	mk_i32(&want, 3);
	mk_adv(&want, "a", 2, 1);
	mk_adv(&want, high, 7, 7);
	mk_adv(&want, "", -2, INT_MIN);
	if (mk_expect(MK_MSK, want.b, want.n))
		return 1;

	mk_start();
	MK_CHECK(mk_get_adv("a", 2, 1) && !mk_get_adv("a", 2, 2) && !mk_get_adv("a", 1, 0)
		 && mk_get_adv(high, 7, 0) && mk_get_adv("", -2, INT_MIN),
		 "ADV flags changed over a reload");
	mk_stop();
	return mk_expect(MK_MSK, want.b, want.n);
}

/* ---- M7: accessors for engine code --------------------------------------- */

static int mk7_accessors(void)
{
	bool (*get_flag)(int) = sp_sym("msgskip_get_flag");
	bool (*get_adv)(const char *, int, int) = sp_sym("msgskip_get_adv_flag");
	MK_CHECK(get_flag && get_adv, "msgskip_get_flag / msgskip_get_adv_flag are missing");
	MK_CHECK(!get_flag(5) && !get_adv("n", 1, 1), "accessors answer before the library starts");
	mk_start();
	mk_set(5);
	mk_set_adv("n", 4, 6);
	for (int i = -2; i < 12; i++)
		MK_CHECK(get_flag(i) == !!mk_get(i), "msgskip_get_flag(%d) differs from GetFlag", i);
	MK_CHECK(get_flag(5) && !get_flag(mk_nr()), "msgskip_get_flag is wrong");
	MK_CHECK(get_adv("n", 4, 6) && !get_adv("n", 4, 7) && !get_adv("n", 3, 0) && !get_adv("m", 4, 0),
		 "msgskip_get_adv_flag is wrong");
	mk_stop();
	MK_CHECK(!get_flag(5) && !get_adv("n", 4, 6), "accessors answer after the library stopped");
	return 0;
}

/* ---- M8: a failed write keeps the previous file --------------------------- */

static int mk8_failed_write(void)
{
	static uint8_t bits[16000];
	static struct mk_buf file;
	mk_bit(bits, 5);
	mk_head(&file, "", mk_nr(), mk_nb(), bits, mk_nb());
	mk_i32(&file, 0);
	MK_CHECK(mk_put(MK_MSK, file.b, file.n), "cannot write the fixture file");
	mk_start();
	MK_CHECK(mk_get(5), "the fixture file was not read");
	mk_set(6);
	MK_CHECK(chmod(sp_dir, 0500) == 0, "cannot make the save folder read-only");
	mk_stop(); /* warns; must not stop the process */
	MK_CHECK(chmod(sp_dir, 0700) == 0, "cannot restore the save folder");
	if (mk_expect(MK_MSK, file.b, file.n) || mk_only(MK_MSK, NULL))
		return 1;
	/* The library is usable again afterwards. */
	mk_start();
	MK_CHECK(mk_get(5) && !mk_get(6), "the table after a failed write is not the file's");
	mk_stop();
	return 0;
}

/* exit_libraries() stops every library of the AIN. Keep only MsgSkip in the
 * list so that no window, parts or audio module is torn down in this
 * headless child. */
static void mk_only_msgskip(void)
{
	static struct ain_library one;
	one = ain->libraries[mk_lib()];
	ain->libraries = &one;
	ain->nr_libraries = 1;
	libraries_initialized = true;
}

/* ---- M9: AIN v13 keeps the old format ------------------------------------- */

static void mk_asd(struct mk_buf *m, const uint8_t *bits)
{
	m->n = 0;
	mk_i32(m, mk_nr());
	mk_bytes(m, bits, mk_nb());
}

static int mk9_v13_guard(void)
{
	int nr = mk_nr(), nb = mk_nb();
	static uint8_t ones[16000], bits[16000];
	memset(ones, 0xff, sizeof ones);
	static struct mk_buf msk, asd;
	mk_head(&msk, "", nr, nb, ones, nb);
	mk_i32(&msk, 0);
	mk_bit(bits, 11);
	mk_bit(bits, 12);
	mk_asd(&asd, bits);
	MK_CHECK(mk_put(MK_MSK, msk.b, msk.n) && mk_put(MK_ASD, asd.b, asd.n), "cannot write the fixture files");

	ain->version = 13;
	mk_start();
	MK_CHECK(mk_get(11) && mk_get(12), "v13 did not read MsgSkip.asd");
	MK_CHECK(mk_count(300) == 2, "v13 read %d flags, want the 2 of MsgSkip.asd", mk_count(300));
	int lib = mk_lib();
	MK_CHECK(!libraries[lib][sp_libfn(lib, "SetAdvFlag")].fun && !libraries[lib][sp_libfn(lib, "GetAdvFlag")].fun,
		 "the ADV functions are linked outside the original format");
	mk_set_adv("x", 1, 1);
	MK_CHECK(!mk_get_adv("x", 1, 1), "GetAdvFlag answers outside the original format");
	mk_set(13);
	mk_stop();
	ain->version = 14;

	mk_bit(bits, 13);
	mk_asd(&asd, bits);
	MK_CHECK(asd.n == (size_t)(4 + nb), "fixture arithmetic");
	if (mk_expect(MK_ASD, asd.b, asd.n) || mk_expect(MK_MSK, msk.b, msk.n) || mk_only(MK_MSK, MK_ASD))
		return 1;
	/* The table stays in memory, as it did before there was a write here. */
	MK_CHECK(mk_get(13) && mk_get(11), "the v13 table was dropped by the write");

	/* What a reset is for this format: the next start (MsgSkip_Init) keeps
	 * the table and does not read the file, here replaced by an empty one. */
	static uint8_t zero[16000];
	static struct mk_buf empty;
	mk_asd(&empty, zero);
	MK_CHECK(mk_put(MK_ASD, empty.b, empty.n), "cannot replace the fixture file");
	ain->version = 13;
	mk_start();
	MK_CHECK(mk_get(11) && mk_get(12) && mk_get(13) && mk_count(300) == 3,
		 "a second start read the old-format file again");

	/* The real reset path writes the old format as well and keeps the table. */
	mk_set(14);
	struct ain_library *libs = ain->libraries;
	int nr_libs = ain->nr_libraries;
	static volatile int phase;
	mk_only_msgskip();
	if (!setjmp(reset_buf)) {
		phase = 1;
		vm_reset();
	}
	MK_CHECK(phase == 1, "vm_reset did not come back");
	ain->libraries = libs;
	ain->nr_libraries = nr_libs;
	mk_bit(bits, 14);
	mk_asd(&asd, bits);
	if (mk_expect(MK_ASD, asd.b, asd.n) || mk_expect(MK_MSK, msk.b, msk.n) || mk_only(MK_MSK, MK_ASD))
		return 1;
	MK_CHECK(mk_get(14) && mk_count(300) == 4, "the v13 table was dropped by vm_reset");
	ain->version = 14;
	return 0;
}

/* ---- M10: a library that declares Init -------------------------------------- */

static int mk10_init_guard(void)
{
	int nr = mk_nr(), nb = mk_nb();
	static uint8_t ones[16000], bits[16000];
	memset(ones, 0xff, sizeof ones);
	static struct mk_buf msk, asd;
	mk_head(&msk, "", nr, nb, ones, nb);
	mk_i32(&msk, 0);
	MK_CHECK(mk_put(MK_MSK, msk.b, msk.n), "cannot write the fixture file");

	/* Rename one declaration so that this v14 library has an Init. */
	int lib = mk_lib(), f = sp_libfn(lib, "GetAdvFlag");
	ain->libraries[lib].functions[f].name = "Init";
	mk_start();
	MK_CHECK(!mk_count(300), "a library with Init was set up from MsgSkip.msk");
	mk_set(21); /* no table yet */
	MK_CHECK(!mk_get(21), "SetFlag worked before Init");
	mk_stop();
	if (mk_expect(MK_MSK, msk.b, msk.n) || mk_only(MK_MSK, NULL))
		return 1;

	int (*init)(struct string *) = NULL;
	for (int i = 0; lib_MsgSkip.functions[i].name; i++)
		if (!strcmp(lib_MsgSkip.functions[i].name, "Init"))
			init = (int (*)(struct string *))lib_MsgSkip.functions[i].fun;
	MK_CHECK(init, "MsgSkip.Init is not exported");
	struct string *name = make_string("SaveData\\Skip.asd", 17);
	MK_CHECK(init(name) == 1, "Init failed");
	free_string(name);
	MK_CHECK(!mk_count(300), "Init read flags from nowhere");
	mk_set(21);
	mk_stop();
	mk_bit(bits, 21);
	mk_asd(&asd, bits);
	if (mk_expect("Skip.asd", asd.b, asd.n) || mk_expect(MK_MSK, msk.b, msk.n) || mk_only(MK_MSK, "Skip.asd"))
		return 1;
	return 0;
}

/* ---- M11 / M12: the two moments the file is written ------------------------- */

static int mk_written_4242(void)
{
	static uint8_t bits[16000];
	static struct mk_buf want;
	memset(bits, 0, sizeof bits);
	mk_bit(bits, 4242);
	mk_head(&want, "", mk_nr(), mk_nb(), bits, mk_nb());
	mk_i32(&want, 0);
	return mk_expect(MK_MSK, want.b, want.n) || mk_only(MK_MSK, NULL);
}

static void mk11_child(void)
{
	mk_start();
	mk_set(4242);
	mk_only_msgskip();
	fflush(stdout);
	vm_exit(0);
}

static int mk11_vm_exit(void)
{
	MK_CHECK(!mk_exists(MK_MSK), "the save folder is not empty");
	pid_t pid = fork();
	if (pid == 0) {
		alarm(60);
		mk11_child();
		_exit(99);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	MK_CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "vm_exit(0) ended with status 0x%x", st);
	if (mk_written_4242())
		return 1;

	/* An exit before init_libraries finished (libraries_initialized is
	 * false in the probe) runs no _ModuleFini: the file on disk stays. */
	static uint8_t bits[16000];
	static struct mk_buf file;
	sp_clear_dir();
	memset(bits, 0, sizeof bits);
	mk_bit(bits, 7);
	mk_head(&file, "", mk_nr(), mk_nb(), bits, mk_nb());
	mk_i32(&file, 0);
	MK_CHECK(mk_put(MK_MSK, file.b, file.n), "cannot write the fixture file");
	MK_CHECK(!libraries_initialized, "the probe started the libraries");
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		alarm(60);
		mk_start();
		mk_set(8);
		fflush(stdout);
		vm_exit(0);
		_exit(99);
	}
	st = 0;
	waitpid(pid, &st, 0);
	MK_CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "vm_exit(0) ended with status 0x%x", st);
	return mk_expect(MK_MSK, file.b, file.n) || mk_only(MK_MSK, NULL);
}

static int mk12_vm_reset(void)
{
	mk_start();
	mk_set(4242);
	struct ain_library *libs = ain->libraries;
	int nr_libs = ain->nr_libraries;
	static volatile int phase;
	mk_only_msgskip();
	if (!setjmp(reset_buf)) {
		phase = 1;
		vm_reset(); /* vm_free, then back to the setjmp in vm_execute_ain */
	}
	MK_CHECK(phase == 1, "vm_reset did not come back");
	ain->libraries = libs;
	ain->nr_libraries = nr_libs;
	if (mk_written_4242())
		return 1;
	/* The original destroys the object after the write and reads the file
	 * again when it starts over (init_libraries runs _PreLink again). */
	MK_CHECK(!mk_get(4242), "the table is still loaded after the reset write");
	mk_start();
	MK_CHECK(mk_get(4242) && mk_count(300) == 0, "the table was not read back after the reset");
	mk_stop();
	return 0;
}

/* ---- M13: only the known declaration is linked -------------------------------- */

static int mk13_shape_guard(void)
{
	int lib = mk_lib(), set = sp_libfn(lib, "SetAdvFlag"), get = sp_libfn(lib, "GetAdvFlag");
	struct ain_hll_function *fs = &ain->libraries[lib].functions[set];
	struct ain_hll_function *fg = &ain->libraries[lib].functions[get];
	MK_CHECK(fs->nr_arguments == 3 && fg->nr_arguments == 3, "unexpected declarations in this AIN");
	MK_CHECK(!libraries[lib][set].fun && !libraries[lib][get].fun, "the ADV functions are linked by name");

	fs->arguments[1].type.data = AIN_FLOAT; /* (string, float, int) */
	fg->return_type.data = AIN_FLOAT;
	mk_start();
	MK_CHECK(!libraries[lib][set].fun, "SetAdvFlag linked to a declaration with a float argument");
	MK_CHECK(!libraries[lib][get].fun, "GetAdvFlag linked to a declaration returning float");
	mk_set(9);
	MK_CHECK(mk_get(9), "SetFlag / GetFlag stopped working");
	mk_stop();
	fs->arguments[1].type.data = AIN_INT;
	fg->return_type.data = AIN_INT;

	fs->nr_arguments = 2;
	fg->arguments[0].type.data = AIN_INT; /* (int, int, int) */
	mk_start();
	MK_CHECK(!libraries[lib][set].fun, "SetAdvFlag linked to a declaration with two arguments");
	MK_CHECK(!libraries[lib][get].fun, "GetAdvFlag linked to a declaration without the name");
	mk_stop();
	fs->nr_arguments = 3;
	fg->arguments[0].type.data = AIN_STRING;

	mk_start();
	MK_CHECK(libraries[lib][set].fun && libraries[lib][get].fun, "the original declarations were not linked");
	mk_set_adv("s", 1, 2);
	MK_CHECK(mk_get_adv("s", 1, 2), "the linked functions do not work");
	mk_stop();
	return 0;
}

/* ---- M14: system.Reset writes the file and keeps the table ---------------------- */

static int mk14_system_reset(void)
{
	int nr = mk_nr(), nb = mk_nb();
	static uint8_t bits[16000];
	static struct mk_buf want, asd;

	mk_start();
	mk_set(4242);
	MK_CHECK(!mk_exists(MK_MSK), "the file exists before the reset");
	mk_system_reset();
	if (mk_written_4242())
		return 1;
	/* 0x4c7840 only writes: the table is released later in the reset
	 * (0x4b30f0), which is not implemented here. */
	MK_CHECK(mk_get(4242) && !mk_get(4243) && mk_count(300) == 0, "system.Reset changed the table");
	mk_set(4243);
	mk_set_adv("r", 2, 3);
	mk_system_reset();
	mk_bit(bits, 4242);
	mk_bit(bits, 4243);
	mk_head(&want, "", nr, nb, bits, nb);
	mk_i32(&want, 1);
	mk_adv(&want, "r", 2, 3);
	if (mk_expect(MK_MSK, want.b, want.n) || mk_only(MK_MSK, NULL))
		return 1;
	MK_CHECK(mk_get(4243) && mk_get_adv("r", 2, 3), "the second system.Reset changed the table");
	mk_stop();
	if (mk_expect(MK_MSK, want.b, want.n))
		return 1;

	/* Nothing to write once the library has stopped. */
	MK_CHECK(unlink(mk_path(MK_MSK)) == 0, "cannot delete the file");
	mk_system_reset();
	MK_CHECK(!mk_exists(MK_MSK), "system.Reset wrote a file for a stopped library");

	/* Guard: the old format is not written from system.Reset. */
	memset(bits, 0, sizeof bits);
	mk_bit(bits, 11);
	mk_asd(&asd, bits);
	MK_CHECK(mk_put(MK_ASD, asd.b, asd.n), "cannot write the fixture file");
	ain->version = 13;
	mk_start();
	MK_CHECK(mk_get(11) && mk_count(300) == 1, "v13 did not read MsgSkip.asd");
	mk_set(12);
	mk_system_reset();
	ain->version = 14;
	if (mk_expect(MK_ASD, asd.b, asd.n) || mk_only(MK_ASD, NULL))
		return 1;
	MK_CHECK(mk_get(12), "system.Reset dropped the v13 table");
	return 0;
}

/* ---- msgskip-msk-file (optional, needs XS4_MSK_COPY) ------------------------ */

static int mk_file_child(void)
{
	static uint8_t data[65536];
	FILE *in = fopen(getenv("XS4_MSK_COPY"), "rb");
	MK_CHECK(in, "cannot open XS4_MSK_COPY");
	size_t len = fread(data, 1, sizeof data, in);
	bool whole = feof(in);
	fclose(in);
	MK_CHECK(whole && len > 0, "XS4_MSK_COPY is empty or larger than %zu bytes", sizeof data);
	MK_CHECK(mk_put(MK_MSK, data, len), "cannot copy the file into the private folder");

	mk_start();
	int total = 0, start = -1, runs = 0;
	printf("  read messages:");
	for (int i = 0; i <= mk_nr(); i++) {
		bool on = i < mk_nr() && mk_get(i);
		total += on;
		if (on && start < 0)
			start = i;
		if (!on && start >= 0) {
			printf(" %d-%d", start, i - 1);
			start = -1;
			runs++;
		}
	}
	printf("\n  %d read of %d messages in %d run(s); file %zu bytes\n", total, mk_nr(), runs, len);
	mk_stop();
	if (mk_expect(MK_MSK, data, len) || mk_only(MK_MSK, NULL))
		return 1;
	printf("  written back byte for byte\n");
	return 0;
}

static int test_msgskip_msk_file(void)
{
	const char *src = getenv("XS4_MSK_COPY");
	if (!src || !src[0]) {
		printf("MSGSKIP-MSK-FILE skipped: set XS4_MSK_COPY to a copy of an original MsgSkip.msk\n");
		return 2;
	}
	sp_setup_dir();
	mk_run("F1 load a copy of an original file and write it back", mk_file_child);
	sp_remove_dir();
	printf("MSGSKIP-MSK-FILE %s\n", mk_failed_cases ? "FAIL" : "PASS");
	return mk_failed_cases ? 1 : 0;
}

static int test_msgskip_msk(void)
{
	sp_setup_dir();
	mk_run("M1 new table byte for byte", mk1_new_file);
	mk_run("M2 read, round trip, one more flag", mk2_read_roundtrip);
	mk_run("M3 files that must not be loaded", mk3_bad_files);
	mk_run("M4 size mismatches that still load", mk4_size_mismatch);
	mk_run("M5 partial reads", mk5_partial);
	mk_run("M6 ADV flags", mk6_adv);
	mk_run("M7 accessors for engine code", mk7_accessors);
	mk_run("M8 a failed write keeps the previous file", mk8_failed_write);
	mk_run("M9 guard: AIN v13 keeps the old format", mk9_v13_guard);
	mk_run("M10 guard: a library that declares Init", mk10_init_guard);
	mk_run("M11 vm_exit writes the file", mk11_vm_exit);
	mk_run("M12 vm_reset writes and reloads", mk12_vm_reset);
	mk_run("M13 guard: only the known declaration is linked", mk13_shape_guard);
	mk_run("M14 system.Reset writes and keeps the table", mk14_system_reset);
	sp_remove_dir();
	printf("MSGSKIP-MSK %s: %d of %d cases failed\n", mk_failed_cases ? "FAIL" : "PASS", mk_failed_cases, mk_cases);
	return mk_failed_cases ? 1 : 0;
}
