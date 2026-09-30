/* mojibake: game strings of the GBK release are decoded as GB18030, not SJIS
 * (window title, file and folder names, the save folder, logs).
 *
 *   MJ1 game_to_utf8 / utf8_to_game / display_sjis0 / unix_path. With the
 *       SJIS rule (probe default, and ain_is_gb18030 alone) they stay
 *       sjis2utf / utf2sjis byte for byte. With the GBK rule GameName
 *       (B6 E0 C4 C8 ...) decodes to 多娜多娜繁中版, a GBK trail byte 0x5C
 *       (運 = DF 5C) is not taken for a path separator, and a name outside
 *       SJIS (零氪战士.txt) round-trips through utf8_to_game and unix_path.
 *       Before: sjis2utf half-width katakana (ｶ狷ﾈ...).
 *   MJ2 save_dir_for_game_charset: only the old sjis2utf root exists -> it is
 *       renamed with its contents; both exist -> the new one is used and the
 *       old one is left alone; neither -> the new path, nothing created; the
 *       new name is a file -> the old save folder is kept.
 *   MJ3 savedir_path: a save file created under its sjis2utf name is renamed
 *       to the decoded name the first time the game uses it.
 *   MJ4 migrate_legacy_save_files: a file in the save folder whose name is
 *       the sjis2utf form of a game string (the dump files of
 *       debug::detail::UpdateDumpData) is renamed; other names, the old name
 *       plus a suffix, and strings with a path separator are not.
 *   MJ5 FileOperation.GetFileList returns names in GB18030 (utf2sjis turned
 *       零氪战士 into 零??士), and TextFile.OpenReader opens the path built
 *       from that name. Before: the name did not match and fopen failed.
 * Each case runs in its own fork. New functions are looked up with dlsym, so
 * before-check.sh builds against the old tree, where those cases fail.
 */

static int mj_failures;
#define MJ_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); mj_failures++; } } while (0)

#define MJ_NAME "\xb6\xe0\xc4\xc8\xb6\xe0\xc4\xc8\xb7\xb1\xd6\xd0\xb0\xe6"	/* 多娜多娜繁中版 */
#define MJ_NAME_UTF8 "多娜多娜繁中版"
#define MJ_DUMP "\xa5\xc0\xa5\xf3\xa5\xd7\xce\xc4\xbc\xfe\x5f\xa5\xb9\xa5\xc8\xa5\xec\xa9\x60\xa5\xb8.asd"	/* ダンプ文件_ストレージ.asd */
#define MJ_DUMP_UTF8 "ダンプ文件_ストレージ.asd"
#define MJ_MEM "<memory>\xa5\xc0\xa5\xf3\xa5\xd7\xce\xc4\xbc\xfe\x5f\xa5\xe1\xa5\xe2\xa5\xea"	/* <memory>ダンプ文件_メモリ */
#define MJ_MEM_UTF8 "<memory>ダンプ文件_メモリ"
#define MJ_USER_GB "\xc1\xe3\xeb\xb4\xd5\xbd\xca\xbf.txt"	/* 零氪战士.txt */
#define MJ_USER_UTF8 "零氪战士.txt"
#define MJ_SEP "\xce\xc4\xbc\xfe.txt"	/* 文件.txt: its old name as a file ... */
#define MJ_SEP_PATH "User\\\xce\xc4\xbc\xfe.txt"	/* ... but the AIN string is User\文件.txt */

typedef char *(*mj_conv_fn)(const char *, size_t);

static char mj_dir[1024];

static void mj_mkdtemp(void)
{
	const char *base = getenv("TMPDIR");
	if (!base || !base[0])
		base = "/tmp";
	snprintf(mj_dir, sizeof mj_dir, "%s/xsys4-mojibake-XXXXXX", base);
	if (!mkdtemp(mj_dir)) {
		printf("MOJIBAKE cannot create a temporary folder under %s\n", base);
		exit(2);
	}
	if (sp_under(mj_dir, "XS4_SRC") || sp_under(mj_dir, "XS4_MASTER_GAME")) {
		printf("MOJIBAKE refused: temporary folder is inside XS4_SRC or XS4_MASTER_GAME\n");
		rmdir(mj_dir);
		exit(2);
	}
}

static void mj_rmtree(void)
{
	char cmd[1200];
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", mj_dir);
	if (system(cmd))
		printf("  (could not remove %s)\n", mj_dir);
}

static char *mj_join(const char *a, const char *b)
{
	char *p = xmalloc(strlen(a) + strlen(b) + 2);
	sprintf(p, "%s/%s", a, b);
	return p;
}

static bool mj_exists(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0;
}

static void mj_touch(const char *p)
{
	FILE *f = fopen(p, "w");
	if (f) {
		fputs("x", f);
		fclose(f);
	}
}

static char *mj_legacy_unix(const char *s)
{
	char *u = sjis2utf(s, 0);
	for (char *c = u; *c; c++)
		if (*c == '\\')
			*c = '/';
	return u;
}

static void mj1_conversions(void)
{
	mj_conv_fn g2u = (mj_conv_fn)gk_sym("game_to_utf8");
	mj_conv_fn u2g = (mj_conv_fn)gk_sym("utf8_to_game");
	MJ_CHECK(g2u && u2g, "game_to_utf8/utf8_to_game missing");
	const char *path = "\xdf\x5c\\a.txt";	/* 運\a.txt */
	for (int cfg = 0; cfg < 3; cfg++) {
		gk_force_sjis();
		if (cfg == 1)
			ain_is_gb18030 = true;
		if (cfg == 2)
			gk_enable();
		const char *tag = cfg == 0 ? "SJIS" : cfg == 1 ? "legacy flag" : "GBK";
		char *ref = sjis2utf(MJ_NAME, 0);
		char *legacy_path = mj_legacy_unix(path);
		const char *disp = display_sjis0(MJ_NAME);
		char *up = unix_path(path);
		if (cfg < 2) {
			MJ_CHECK(!strcmp(disp, ref), "%s: display_sjis0 changed", tag);
			MJ_CHECK(!strcmp(up, legacy_path), "%s: unix_path changed ('%s')", tag, up);
			if (g2u && u2g) {
				char *a = g2u(MJ_NAME, 0), *b = u2g("ランス.txt", 0), *c = utf2sjis("ランス.txt", 0);
				MJ_CHECK(!strcmp(a, ref), "%s: game_to_utf8 is not sjis2utf", tag);
				MJ_CHECK(!strcmp(b, c), "%s: utf8_to_game is not utf2sjis", tag);
				free(a); free(b); free(c);
			}
		} else {
			MJ_CHECK(!strcmp(disp, MJ_NAME_UTF8), "GBK: display_sjis0(GameName)='%s'", disp);
			MJ_CHECK(!strcmp(up, "運/a.txt"), "GBK: unix_path(運\\a.txt)='%s'", up);
			if (g2u && u2g) {
				char *a = g2u(MJ_NAME, 0), *b = u2g(MJ_USER_UTF8, 0), *c = u2g("SaveData/x.asd", 0);
				MJ_CHECK(!strcmp(a, MJ_NAME_UTF8), "GBK: game_to_utf8(GameName)='%s'", a);
				MJ_CHECK(!strcmp(b, MJ_USER_GB), "GBK: utf8_to_game(零氪战士.txt) is not GB18030");
				MJ_CHECK(!strcmp(c, "SaveData/x.asd"), "GBK: ASCII changed");
				char *back = unix_path(b);
				MJ_CHECK(!strcmp(back, MJ_USER_UTF8), "GBK: unix_path round trip '%s'", back);
				char *bad = g2u("\x81", 0);	/* lone lead byte: falls back to sjis2utf */
				char *bad_ref = sjis2utf("\x81", 0);
				MJ_CHECK(!strcmp(bad, bad_ref), "GBK: invalid GBK does not fall back");
				free(a); free(b); free(c); free(back); free(bad); free(bad_ref);
			}
		}
		printf("  MJ1 %s: display='%s' unix_path='%s'\n", tag, disp, up);
		free(ref); free(legacy_path); free(up);
	}
	gk_force_sjis();
}

typedef char *(*mj_savedir_fn)(const char *, const char *, const char *, const char *);

static void mj2_save_root(void)
{
	mj_savedir_fn f = (mj_savedir_fn)gk_sym("save_dir_for_game_charset");
	MJ_CHECK(f, "save_dir_for_game_charset missing");
	if (!f)
		return;
	gk_enable();
	mj_mkdtemp();
	char *old_name = sjis2utf(MJ_NAME, 0);
	const char *cases[] = { "only-old", "both", "neither", "new-is-file" };
	for (int i = 0; i < 4; i++) {
		char *home = mj_join(mj_dir, cases[i]);
		mkdir(home, 0755);
		char *old_root = mj_join(home, old_name), *new_root = mj_join(home, MJ_NAME_UTF8);
		char *old_save = mj_join(old_root, "SaveData"), *new_save = mj_join(new_root, "SaveData");
		char *old_file = mj_join(old_save, "AFConfig.asd"), *new_file = mj_join(new_save, "AFConfig.asd");
		if (i != 2) {
			mkdir(old_root, 0755);
			mkdir(old_save, 0755);
			mj_touch(old_file);
		}
		if (i == 1)
			mkdir(new_root, 0755);
		if (i == 3)
			mj_touch(new_root);
		char *r = f(home, MJ_NAME, "SaveData", old_save);
		switch (i) {
		case 0:
			MJ_CHECK(!strcmp(r, new_save), "only-old: result '%s'", r);
			MJ_CHECK(mj_exists(new_file) && !mj_exists(old_root), "only-old: not moved");
			break;
		case 1:
			MJ_CHECK(!strcmp(r, new_save), "both: result '%s'", r);
			MJ_CHECK(mj_exists(old_file), "both: old folder touched");
			break;
		case 2:
			MJ_CHECK(!strcmp(r, new_save), "neither: result '%s'", r);
			MJ_CHECK(!mj_exists(new_root) && !mj_exists(old_root), "neither: created a folder");
			break;
		case 3:
			MJ_CHECK(!strcmp(r, old_save), "new-is-file: result '%s'", r);
			MJ_CHECK(mj_exists(old_file), "new-is-file: old folder touched");
			break;
		}
		printf("  MJ2 %s -> %s\n", cases[i], r + strlen(home) + 1);
		free(r); free(home); free(old_root); free(new_root); free(old_save); free(new_save);
		free(old_file); free(new_file);
	}
	free(old_name);
	mj_rmtree();
	gk_force_sjis();
}

static void mj3_savedir_path(void)
{
	gk_enable();
	mj_mkdtemp();
	char *saved = config.save_dir;
	config.save_dir = strdup(mj_dir);
	char *legacy_rel = sjis2utf(MJ_DUMP, 0);
	char *legacy = mj_join(mj_dir, legacy_rel), *want = mj_join(mj_dir, MJ_DUMP_UTF8);
	mj_touch(legacy);
	char *p = savedir_path(MJ_DUMP);
	MJ_CHECK(!strcmp(p, want), "savedir_path='%s'", p);
	MJ_CHECK(mj_exists(want) && !mj_exists(legacy), "old file not renamed");
	char *q = savedir_path("SaveData1.asd"), *q_want = mj_join(mj_dir, "SaveData1.asd");
	MJ_CHECK(!strcmp(q, q_want), "ASCII name changed: '%s'", q);
	printf("  MJ3 %s -> %s\n", legacy_rel, p + strlen(mj_dir) + 1);
	free(p); free(q); free(q_want); free(legacy_rel); free(legacy); free(want);
	free(config.save_dir);
	config.save_dir = saved;
	mj_rmtree();
	gk_force_sjis();
}

static void mj4_migrate_files(void)
{
	void (*f)(struct string **, int) = (void (*)(struct string **, int))gk_sym("migrate_legacy_save_files");
	MJ_CHECK(f, "migrate_legacy_save_files missing");
	if (!f)
		return;
	gk_enable();
	mj_mkdtemp();
	char *saved = config.save_dir;
	config.save_dir = strdup(mj_dir);
	char *mem_legacy = sjis2utf(MJ_MEM, 0), *dump_legacy = sjis2utf(MJ_DUMP, 0);
	char *p_mem = mj_join(mj_dir, mem_legacy), *p_dump = mj_join(mj_dir, dump_legacy);
	char *p_user = mj_join(mj_dir, MJ_USER_UTF8), *p_ascii = mj_join(mj_dir, "AFConfig.asd");
	/* near misses: the old name plus a suffix, and the old name of a string
	 * with a path separator (skipped) must stay */
	char *near_rel = xmalloc(strlen(dump_legacy) + 5);
	sprintf(near_rel, "%s.bak", dump_legacy);
	char *p_near = mj_join(mj_dir, near_rel);
	char *sep_legacy = sjis2utf(MJ_SEP, 0);
	char *p_sep = mj_join(mj_dir, sep_legacy);
	mj_touch(p_mem);
	mj_touch(p_dump);
	mj_touch(p_user);
	mj_touch(p_ascii);
	mj_touch(p_near);
	mj_touch(p_sep);
	struct string *strs[] = { make_string("AFConfig.asd", 12), make_string(MJ_MEM, strlen(MJ_MEM)),
				  make_string(MJ_DUMP, strlen(MJ_DUMP)), make_string(MJ_SEP_PATH, strlen(MJ_SEP_PATH)) };
	f(strs, 4);
	char *w_mem = mj_join(mj_dir, MJ_MEM_UTF8), *w_dump = mj_join(mj_dir, MJ_DUMP_UTF8);
	MJ_CHECK(mj_exists(w_mem) && !mj_exists(p_mem), "<memory> dump not renamed");
	MJ_CHECK(mj_exists(w_dump) && !mj_exists(p_dump), "storage dump not renamed");
	MJ_CHECK(mj_exists(p_user) && mj_exists(p_ascii), "unrelated file renamed");
	MJ_CHECK(mj_exists(p_near), "old name + suffix renamed");
	MJ_CHECK(mj_exists(p_sep), "name of a string with a path separator renamed");
	printf("  MJ4 renamed %s, %s\n", MJ_MEM_UTF8, MJ_DUMP_UTF8);
	for (int i = 0; i < 4; i++)
		free_string(strs[i]);
	free(near_rel); free(p_near); free(sep_legacy); free(p_sep);
	free(mem_legacy); free(dump_legacy); free(p_mem); free(p_dump); free(p_user); free(p_ascii);
	free(w_mem); free(w_dump);
	free(config.save_dir);
	config.save_dir = saved;
	mj_rmtree();
	gk_force_sjis();
}

static void mj5_file_list(void)
{
	gk_enable();
	mj_mkdtemp();
	char *user = mj_join(mj_dir, MJ_USER_UTF8);
	mj_touch(user);
	int lib, fl = cf_fn("FileOperation", "GetFileList", 0, &lib);
	stack_push(cf_str_slot(mj_dir));
	hll_call(lib, fl, 0);
	int r = stack_pop().i;
	MJ_CHECK(r > 1 && heap[r].page && heap[r].page->nr_vars == 1, "GetFileList ret %d", r);
	const char *got = (r > 1 && heap[r].page && heap[r].page->nr_vars == 1)
		? heap[heap[r].page->values[0].i].s->text : "";
	MJ_CHECK(!strcmp(got, MJ_USER_GB), "GetFileList name is not GB18030 零氪战士.txt");
	char path[2048];
	snprintf(path, sizeof path, "%s/%s", mj_dir, got);
	int tlib, fo = cf_fn("TextFile", "OpenReader", 0, &tlib), fc = cf_fn("TextFile", "Close", 0, &tlib);
	stack_push(cf_str_slot(path));
	hll_call(tlib, fo, 0);
	int h = stack_pop().i;
	MJ_CHECK(h >= 0, "TextFile.OpenReader(dir + listed name) failed");
	if (h >= 0) {
		stack_push(h);
		hll_call(tlib, fc, 0);
		stack_pop();
	}
	printf("  MJ5 GetFileList -> %d bytes, OpenReader -> %d\n", (int)strlen(got), h);
	free(user);
	mj_rmtree();
	gk_force_sjis();
}

static int test_mojibake(void)
{
	void (*cases[])(void) = { mj1_conversions, mj2_save_root, mj3_savedir_path, mj4_migrate_files, mj5_file_list };
	const char *names[] = { "MJ1", "MJ2", "MJ3", "MJ4", "MJ5" };
	int failed = 0;
	for (int i = 0; i < 5; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			mj_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(mj_failures ? 1 : 0);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("MOJIBAKE %s: %d of 5 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
