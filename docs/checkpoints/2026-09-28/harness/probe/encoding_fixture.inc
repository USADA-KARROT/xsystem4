/* encoding_fixture.inc — which character encoding a game is run with, and
 * the crossfade mosaic transition (2026-11-08).
 * Modes: encoding-select (EN), mosaic-effect (MZ).
 *
 * encoding-select. Two switches decide how game strings are read:
 * ain_is_gb18030 (the GB18030 text paths: glyph lookup, character stepping
 * in the renderers, the GBK file-name fallbacks) and libsys4's String
 * character rule (sys4_set_string_charset). game_charset_is_gbk() is the
 * two together. Asserted here:
 *   - With no encoding given, both are on only when the strict GB18030
 *     check (gb18030_detect_strings' verdict) holds. A Shift-JIS table on
 *     which only the old loose score fires (the score of the first 100
 *     strings with a byte A1..DF followed by a byte >= 0x40; Japanese text
 *     hits it with the trail bytes of two-byte characters) keeps both off,
 *     for every AIN version (synthetic tables of v5 and v14; the strict
 *     check passes on the harness's own AIN, gbk-detect).
 *   - encoding "gbk" turns both on whatever the table; "sjis" keeps both off
 *     whatever the table, the strict verdict included.
 *   - The value comes from --encoding (as --encoding=<v> or as two
 *     arguments; the last one given wins), from the `encoding` key of the
 *     global .xsys4rc ($XSYSTEM4_HOME) or of the game folder's .xsys4rc.
 *     The game folder's file is read after the global one and the command
 *     line wins over both. Values are compared without regard to case (as
 *     upstream's 71dbfda does); any other value, the empty one included,
 *     is warned about (a line naming the key or option and the value, in
 *     double quotes when it is empty) and ignored, so that what was set
 *     before it stays.
 *   - XSYS4_STRING_CHARSET=sjis|gbk keeps its meaning: after all of the
 *     above it sets the character rule alone and never ain_is_gb18030;
 *     any other value is ignored.
 *   - The startup log has no AINCHECK line, and no "Invalid value" line
 *     unless the case gives an invalid value. It says which encoding is
 *     used: "encoding=gbk" / "encoding=sjis" when one is given, and with
 *     none on a table where the strict check holds the two lines the
 *     engine has always printed ("Detected GB18030 encoding in AIN",
 *     "GBK string rules enabled").
 *   - --help lists --encoding.
 * Each case writes its AIN (VERS, empty FUNC and STRT, and STR0, ciphered
 * as the engine reads it) and its .xsys4rc files in a temporary folder (mkdtemp
 * under $XS4_SAVE_TMP or $TMPDIR, refused inside XS4_SRC or
 * XS4_MASTER_GAME), checks with libsys4 and gb18030_detect_strings that the
 * file reads back as meant (an environment failure otherwise), and runs the
 * engine's own main (system4.c, renamed xsystem4_original_main by
 * build_probe.py) with --audit, which returns after the encoding is
 * decided, in a forked process that starts from the state of a new
 * process (both switches off, no XSYS4_STRING_CHARSET unless the case sets
 * it, XSYSTEM4_HOME set to the case's folder). Its stdout and stderr go to
 * files; the switches are read after main returns. A main that does not
 * return (a usage error, the engine's ERROR) fails the case. The tables
 * are synthetic (generic UI words in GBK, Shift-JIS and UTF-8, and ASCII),
 * except EN33/EN34, which copy the string table of the AIN the harness
 * already loads into the temporary AIN.
 *
 * mosaic-effect (EFFECT_CROSSFADE_MOSAIC, 6), as in the original engine
 * (read statically; upstream's 4233c7d moved the grid's origin to the
 * centre). At progress p the chunk size is the odd number
 * c = 2 * floor(48 * (1 - |2p - 1|)) + 1: 1 at p = 0 and 1, 97 at p = 0.5,
 * and 1 for a progress outside 0..1 (a script can give one). The chunk grid
 * is anchored at the centre of the screen in whole pixels, (floor(w / 2),
 * floor(h / 2)): a chunk edge passes through it on both axes, every edge is
 * c pixels from the next one, and the partial chunks at the screen edges
 * are what is left on each side. Each chunk, the partial ones included, is
 * drawn with the crossfade of the old and the new scene sampled at the
 * chunk's first pixel: its lowest x and y, the top left one (row 0 of the
 * pictures is the top of the screen). With c = 1 (p near 0 or 1) the
 * effect is the per-pixel crossfade, and at p = 0 and p = 1 exactly the old
 * and the new scene. Each case runs in its own fork, which starts this
 * probe binary again with posix_spawn in the helper mode mosaic-render: on
 * macOS a process made by fork() alone was measured not to compile a
 * shader that is not in the system's shader cache yet (the draw is
 * dropped), and a changed shader never is. The helper makes a windowless CGL context, runs the engine's GL
 * setup (probe_gl_initialize; the shaders are read from XS4_SRC), sets
 * config.view_width/height (the shader's resolution), draws
 * effect_update_texture(6, dst, old, new, p) into a blank texture and
 * writes the frames to a file; the case reads and checks them. Old and new carry 2x in red and 2y in green (mod 256:
 * even values, so that a sample half-way between two pixels is a whole
 * number, 1 away from both) and differ in blue (0 and 255). The sample
 * cases check red and green exactly: a sample at a pixel's centre is that
 * pixel's value. A plain crossfade (effect 1) of the same
 * pictures at 0, 0.5 and 1, drawn by the same helper, must reproduce its
 * inputs, or the case is an environment failure.
 *
 * Not covered: the save folder that game_charset_is_gbk() picks after the
 * encoding is decided (main reaches it only after --audit has returned);
 * an `encoding` value that is not a string in .xsys4rc (the ini reader
 * stops the engine for every key read as a string); the mosaic on the
 * engine's real screen surface and in a running game; in the per-pixel
 * cases (c = 1) a sample up to half a texel away from the pixel's centre
 * (red and green are checked within 1 there; the sample cases check them
 * exactly); the colour of the original's crossfade, which weighs the two
 * scenes in whole steps of 1/255 (here the GPU's mix at p; a model of a
 * 2000 ms effect finds a channel up to 2 or 3 off, by how the original's
 * tables round); the chunk size wherever the original, counting whole
 * milliseconds of the length, and the shader's progress round apart (an
 * odd length, or a progress not exact in binary at an edge between sizes).
 */
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <OpenGL/OpenGL.h>
#include "effect.h"

int xsystem4_original_main(int argc, char *argv[]);
extern void probe_gl_initialize(void);

/* ---- encoding-select ---- */

enum en_table { EN_T_GBK, EN_T_SJIS, EN_T_UTF8, EN_T_ASCII, EN_T_EMPTY, EN_T_REAL };

/* Generic UI words: GBK (simplified Chinese), Shift-JIS and UTF-8
 * (Japanese), ASCII. */
static const char *const en_gbk_words[] = {
	"\xC9\xE8\xD6\xC3", "\xB6\xC1\xC8\xA1\xBD\xF8\xB6\xC8", "\xB1\xA3\xB4\xE6\xBD\xF8\xB6\xC8",
	"\xC8\xB7\xB6\xA8", "\xC8\xA1\xCF\xFB", "\xB7\xB5\xBB\xD8\xB1\xEA\xCC\xE2",
	"\xBF\xAA\xCA\xBC\xD3\xCE\xCF\xB7", "\xBD\xE1\xCA\xF8\xD3\xCE\xCF\xB7", "\xD1\xA1\xCF\xEE",
	"\xD2\xF4\xC1\xBF", "\xCE\xC4\xD7\xD6\xCB\xD9\xB6\xC8", "\xB4\xB0\xBF\xDA\xC4\xA3\xCA\xBD",
	"\xC8\xAB\xC6\xC1\xCF\xD4\xCA\xBE", "\xBB\xAD\xC3\xE6\xD0\xA7\xB9\xFB", "\xC9\xF9\xD2\xF4\xC9\xE8\xB6\xA8",
	"\xCC\xF8\xB9\xFD\xD2\xD1\xB6\xC1", "\xD7\xD4\xB6\xAF\xB2\xA5\xB7\xC5", "\xC0\xFA\xCA\xB7\xBC\xC7\xC2\xBC",
	"\xCD\xCB\xB3\xF6", "\xBC\xCC\xD0\xF8",
};
static const char *const en_sjis_words[] = {
	"\x82\xB9\x82\xC1\x82\xC4\x82\xA2", "\x83\x8D\x81\x5B\x83\x68", "\x83\x5A\x81\x5B\x83\x75",
	"\x82\xCD\x82\xA2", "\x82\xA2\x82\xA2\x82\xA6", "\x82\xA8\x82\xED\x82\xE8", "\x82\xC2\x82\xC3\x82\xAB",
	"\x83\x5E\x83\x43\x83\x67\x83\x8B", "\x83\x49\x83\x76\x83\x56\x83\x87\x83\x93",
	"\x82\xA8\x82\xF1\x82\xE8\x82\xE5\x82\xA4", "\x82\xE0\x82\xB6\x82\xBB\x82\xAD\x82\xC7",
	"\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45", "\x82\xBA\x82\xF1\x82\xAA\x82\xDF\x82\xF1",
	"\x83\x58\x83\x4C\x83\x62\x83\x76", "\x83\x49\x81\x5B\x83\x67", "\x82\xE8\x82\xEA\x82\xAB",
};
static const char *const en_utf8_words[] = {
	"\xE3\x81\x9B\xE3\x81\xA3\xE3\x81\xA6\xE3\x81\x84", "\xE3\x83\xAD\xE3\x83\xBC\xE3\x83\x89",
	"\xE3\x82\xBB\xE3\x83\xBC\xE3\x83\x96", "\xE3\x81\xAF\xE3\x81\x84", "\xE3\x81\x84\xE3\x81\x84\xE3\x81\x88",
	"\xE3\x81\x8A\xE3\x82\x8F\xE3\x82\x8A", "\xE3\x81\xA4\xE3\x81\xA5\xE3\x81\x8D",
	"\xE3\x82\xBF\xE3\x82\xA4\xE3\x83\x88\xE3\x83\xAB",
	"\xE3\x82\xAA\xE3\x83\x97\xE3\x82\xB7\xE3\x83\xA7\xE3\x83\xB3",
	"\xE3\x81\x8A\xE3\x82\x93\xE3\x82\x8A\xE3\x82\x87\xE3\x81\x86",
	"\xE3\x82\x82\xE3\x81\x98\xE3\x81\x9D\xE3\x81\x8F\xE3\x81\xA9",
	"\xE3\x82\xA6\xE3\x82\xA3\xE3\x83\xB3\xE3\x83\x89\xE3\x82\xA6",
	"\xE3\x81\x9C\xE3\x82\x93\xE3\x81\x8C\xE3\x82\x81\xE3\x82\x93",
	"\xE3\x82\xB9\xE3\x82\xAD\xE3\x83\x83\xE3\x83\x97", "\xE3\x82\xAA\xE3\x83\xBC\xE3\x83\x88",
	"\xE3\x82\x8A\xE3\x82\x8C\xE3\x81\x8D",
};
static const char *const en_ascii_words[] = { "menu", "ok", "cancel", "load", "save", "title", "option", "volume" };

struct en_def {
	const char *id, *what;
	enum en_table table;
	int version;
	const char *args[3];      /* before the AIN path */
	const char *home_rc;      /* the global .xsys4rc, or NULL */
	const char *game_rc;      /* the game folder's .xsys4rc, or NULL */
	const char *env_charset;  /* XSYS4_STRING_CHARSET, or NULL (unset) */
	int want_ain, want_gbk_rule;
	const char *warn_key, *warn_value; /* a stderr line must name both */
	const char *want_stdout;  /* --help: a stdout line must contain it */
	bool no_aincheck;         /* the startup log must have no AINCHECK line */
	const char *want_log[2];  /* stderr must contain each (NULL: none) */
};

#define RC_GBK "encoding = \"gbk\"\n"
#define RC_SJIS "encoding = \"sjis\"\n"

static const struct en_def en_defs[] = {
	/* no encoding given */
	{ "EN1", "auto, GBK table v14: both on", EN_T_GBK, 14, {0}, 0, 0, 0, 1, 1,
	  .want_log = { "Detected GB18030 encoding in AIN", "GBK string rules enabled" } },
	{ "EN2", "auto, GBK table v5: both on", EN_T_GBK, 5, {0}, 0, 0, 0, 1, 1 },
	{ "EN3", "auto, SJIS table v5 (only the loose score fires): both off", EN_T_SJIS, 5, {0}, 0, 0, 0, 0, 0 },
	{ "EN4", "auto, SJIS table v14 (only the loose score fires): both off", EN_T_SJIS, 14, {0}, 0, 0, 0, 0, 0 },
	{ "EN5", "auto, UTF-8 table v14 (loose score fires, strict does not): both off", EN_T_UTF8, 14, {0}, 0, 0, 0, 0, 0 },
	{ "EN6", "auto, ASCII table v5: both off", EN_T_ASCII, 5, {0}, 0, 0, 0, 0, 0 },
	{ "EN7", "auto, no strings v14: both off", EN_T_EMPTY, 14, {0}, 0, 0, 0, 0, 0 },
	/* --encoding */
	{ "EN8", "--encoding=gbk, ASCII table: both on", EN_T_ASCII, 5, {"--encoding=gbk"}, 0, 0, 0, 1, 1,
	  .want_log = { "encoding=gbk" } },
	{ "EN9", "--encoding=gbk, SJIS table: both on", EN_T_SJIS, 5, {"--encoding=gbk"}, 0, 0, 0, 1, 1 },
	{ "EN10", "--encoding=gbk, no strings: both on", EN_T_EMPTY, 14, {"--encoding=gbk"}, 0, 0, 0, 1, 1 },
	{ "EN11", "--encoding=gbk, GBK table: both on", EN_T_GBK, 14, {"--encoding=gbk"}, 0, 0, 0, 1, 1 },
	{ "EN12", "--encoding=sjis, GBK table (strict holds): both off", EN_T_GBK, 14, {"--encoding=sjis"}, 0, 0, 0, 0, 0,
	  .want_log = { "encoding=sjis" } },
	{ "EN13", "--encoding=sjis, SJIS table: both off", EN_T_SJIS, 5, {"--encoding=sjis"}, 0, 0, 0, 0, 0 },
	{ "EN14", "--encoding gbk (two arguments), ASCII table: both on", EN_T_ASCII, 5, {"--encoding", "gbk"}, 0, 0, 0, 1, 1 },
	{ "EN15", "--encoding=GBK (case), ASCII table: both on", EN_T_ASCII, 5, {"--encoding=GBK"}, 0, 0, 0, 1, 1 },
	{ "EN16", "--encoding=Sjis (case), GBK table: both off", EN_T_GBK, 14, {"--encoding=Sjis"}, 0, 0, 0, 0, 0 },
	{ "EN17", "--encoding=foo, GBK table: ignored with a warning (auto: on)", EN_T_GBK, 14, {"--encoding=foo"}, 0, 0, 0, 1, 1,
	  "encoding", "foo" },
	{ "EN18", "global rc gbk, --encoding=foo, ASCII table: the rc value stays (on)", EN_T_ASCII, 5, {"--encoding=foo"}, RC_GBK, 0, 0,
	  1, 1, "encoding", "foo" },
	/* .xsys4rc */
	{ "EN19", "global rc gbk, ASCII table: both on", EN_T_ASCII, 5, {0}, RC_GBK, 0, 0, 1, 1 },
	{ "EN20", "game rc sjis, GBK table: both off", EN_T_GBK, 14, {0}, 0, RC_SJIS, 0, 0, 0 },
	{ "EN21", "global rc sjis, game rc gbk, ASCII table: the game rc wins (on)", EN_T_ASCII, 5, {0}, RC_SJIS, RC_GBK, 0, 1, 1 },
	{ "EN22", "global rc gbk, game rc sjis, GBK table: the game rc wins (off)", EN_T_GBK, 14, {0}, RC_GBK, RC_SJIS, 0, 0, 0 },
	{ "EN23", "global rc gbk, --encoding=sjis, GBK table: the command line wins (off)", EN_T_GBK, 14, {"--encoding=sjis"}, RC_GBK, 0, 0,
	  0, 0 },
	{ "EN24", "game rc sjis, --encoding=gbk, ASCII table: the command line wins (on)", EN_T_ASCII, 5, {"--encoding=gbk"}, 0, RC_SJIS, 0,
	  1, 1 },
	{ "EN25", "global rc gbk, game rc bar, ASCII table: the global value stays (on)", EN_T_ASCII, 5, {0}, RC_GBK,
	  "encoding = \"bar\"\n", 0, 1, 1, "encoding", "bar" },
	{ "EN26", "game rc SJIS (case), GBK table: both off", EN_T_GBK, 14, {0}, 0, "encoding = \"SJIS\"\n", 0, 0, 0 },
	/* XSYS4_STRING_CHARSET */
	{ "EN27", "XSYS4_STRING_CHARSET=sjis, GBK table: the rule alone off", EN_T_GBK, 14, {0}, 0, 0, "sjis", 1, 0 },
	{ "EN28", "XSYS4_STRING_CHARSET=gbk, ASCII table: the rule alone on", EN_T_ASCII, 5, {0}, 0, 0, "gbk", 0, 1 },
	{ "EN29", "XSYS4_STRING_CHARSET=gbk, SJIS table: the rule alone on", EN_T_SJIS, 5, {0}, 0, 0, "gbk", 0, 1 },
	{ "EN30", "XSYS4_STRING_CHARSET=bogus, GBK table: ignored with a warning (both on)", EN_T_GBK, 14, {0}, 0, 0, "bogus", 1, 1,
	  "xsys4_string_charset", "bogus" },
	{ "EN31", "XSYS4_STRING_CHARSET=gbk, --encoding=sjis, GBK table: ain off, rule on", EN_T_GBK, 14, {"--encoding=sjis"}, 0, 0,
	  "gbk", 0, 1 },
	{ "EN32", "XSYS4_STRING_CHARSET=sjis, --encoding=gbk, ASCII table: ain on, rule off", EN_T_ASCII, 5, {"--encoding=gbk"}, 0, 0,
	  "sjis", 1, 0 },
	/* the harness's own AIN table */
	{ "EN33", "auto, the harness AIN's string table: both on", EN_T_REAL, 14, {0}, 0, 0, 0, 1, 1 },
	{ "EN34", "--encoding=sjis, the harness AIN's string table: both off", EN_T_REAL, 14, {"--encoding=sjis"}, 0, 0, 0, 0, 0 },
	/* --help */
	{ "EN35", "--help lists --encoding", EN_T_ASCII, 5, {"--help"}, 0, 0, 0, -1, -1, 0, 0, "--encoding" },
	/* the startup log */
	{ "EN36", "auto, SJIS table v5: no AINCHECK line", EN_T_SJIS, 5, {0}, 0, 0, 0, -1, -1, 0, 0, 0, 1 },
	{ "EN37", "auto, GBK table v14: no AINCHECK line", EN_T_GBK, 14, {0}, 0, 0, 0, -1, -1, 0, 0, 0, 1 },
	/* more values */
	{ "EN38", "global rc gbk, --encoding= (empty), ASCII table: the rc value stays (on)", EN_T_ASCII, 5, {"--encoding="},
	  RC_GBK, 0, 0, 1, 1, "encoding", "\"\"" },
	{ "EN39", "--encoding=gbk then --encoding=sjis, GBK table: the last one wins (off)", EN_T_GBK, 14,
	  {"--encoding=gbk", "--encoding=sjis"}, 0, 0, 0, 0, 0 },
	{ "EN40", "global rc baz, ASCII table: ignored with a warning (auto: off)", EN_T_ASCII, 5, {0}, "encoding = \"baz\"\n", 0,
	  0, 0, 0, "encoding", "baz" },
};
#define EN_N (int)(sizeof en_defs / sizeof en_defs[0])

static char en_dir[PATH_MAX];

static bool en_under(const char *path, const char *root_env)
{
	const char *root = getenv(root_env);
	char a[PATH_MAX], b[PATH_MAX];
	if (!root || !root[0] || !realpath(path, a) || !realpath(root, b))
		return false;
	size_t n = strlen(b);
	return !strncmp(a, b, n) && (a[n] == '/' || a[n] == 0);
}

static void en_path(char *out, size_t size, const char *name)
{
	snprintf(out, size, "%s/%s", en_dir, name);
}

static void en_write_file(const char *name, const void *data, size_t len)
{
	char p[PATH_MAX];
	en_path(p, sizeof p, name);
	FILE *f = fopen(p, "wb");
	if (!f || fwrite(data, 1, len, f) != len || fclose(f))
		xc_env_fail("cannot write a fixture file");
}

static char *en_read_file(const char *name)
{
	char p[PATH_MAX];
	en_path(p, sizeof p, name);
	FILE *f = fopen(p, "rb");
	if (!f)
		return xstrdup("");
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *s = xmalloc(n + 1);
	size_t got = fread(s, 1, n, f);
	s[got] = 0;
	fclose(f);
	return s;
}

static void en_remove(const char *name)
{
	char p[PATH_MAX];
	en_path(p, sizeof p, name);
	unlink(p);
}

static void en_cleanup(void)
{
	static const char *const files[] = { "game/game.ain", "game/.xsys4rc", "home/.xsys4rc", "out.txt", "err.txt" };
	for (size_t i = 0; i < sizeof files / sizeof *files; i++)
		en_remove(files[i]);
	char p[PATH_MAX];
	en_path(p, sizeof p, "game");
	rmdir(p);
	en_path(p, sizeof p, "home");
	rmdir(p);
	rmdir(en_dir);
}

static void en_setup_dir(void)
{
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !base[0])
		base = getenv("TMPDIR");
	if (!base || !base[0])
		base = "/tmp";
	/* no "encoding" in the name: the warning checks look for that word */
	snprintf(en_dir, sizeof en_dir, "%s/xsys4-enctest-XXXXXX", base);
	if (!mkdtemp(en_dir))
		xc_env_fail("cannot create a temporary folder");
	if (en_under(en_dir, "XS4_SRC") || en_under(en_dir, "XS4_MASTER_GAME")) {
		rmdir(en_dir);
		xc_env_fail("the temporary folder is inside XS4_SRC or XS4_MASTER_GAME");
	}
	char p[PATH_MAX];
	en_path(p, sizeof p, "game");
	if (mkdir(p, 0700))
		xc_env_fail("cannot create the game folder");
	en_path(p, sizeof p, "home");
	if (mkdir(p, 0700))
		xc_env_fail("cannot create the home folder");
}

/* The table of a case: n strings (copies of the word lists, each list
 * repeated so that the first 100 strings are all from it). */
static int en_table_strings(enum en_table t, const char ***out)
{
	const char *const *words = NULL;
	int nw = 0, times = 0;
	switch (t) {
	case EN_T_GBK: words = en_gbk_words; nw = 20; times = 5; break;
	case EN_T_SJIS: words = en_sjis_words; nw = 16; times = 7; break;
	case EN_T_UTF8: words = en_utf8_words; nw = 16; times = 7; break;
	case EN_T_ASCII: words = en_ascii_words; nw = 8; times = 13; break;
	case EN_T_EMPTY: *out = NULL; return 0;
	case EN_T_REAL: {
		/* the AIN the harness loaded (main() of the probe) */
		const char **s = xcalloc(ain->nr_strings ? ain->nr_strings : 1, sizeof *s);
		for (int i = 0; i < ain->nr_strings; i++)
			s[i] = ain->strings[i] ? ain->strings[i]->text : "";
		*out = s;
		return ain->nr_strings;
	}
	}
	const char **s = xcalloc(nw * times, sizeof *s);
	for (int i = 0; i < nw * times; i++)
		s[i] = words[i % nw];
	*out = s;
	return nw * times;
}

/* An AIN of a VERS, an empty FUNC and STRT (ain_free needs their indexes)
 * and a STR0 section, ciphered like the engine's files (ain_decrypt is the
 * cipher's own inverse). */
static void en_write_ain(int version, const char **strs, int n)
{
	size_t len = 32;
	for (int i = 0; i < n; i++)
		len += strlen(strs[i]) + 1;
	uint8_t *buf = xcalloc(1, len), *p = buf;
	int32_t v = version, count = n;
	int32_t zero = 0;
	memcpy(p, "VERS", 4); memcpy(p + 4, &v, 4); p += 8;
	memcpy(p, "FUNC", 4); memcpy(p + 4, &zero, 4); p += 8;
	memcpy(p, "STRT", 4); memcpy(p + 4, &zero, 4); p += 8;
	memcpy(p, "STR0", 4); memcpy(p + 4, &count, 4); p += 8;
	for (int i = 0; i < n; i++) {
		size_t l = strlen(strs[i]) + 1;
		memcpy(p, strs[i], l);
		p += l;
	}
	ain_decrypt(buf, len);
	en_write_file("game/game.ain", buf, len);
	free(buf);
}

/* The fixture file reads back as meant: the same strings, and the scores
 * the case relies on. */
static void en_check_fixture(const struct en_def *d, const char **strs, int n)
{
	char p[PATH_MAX];
	en_path(p, sizeof p, "game/game.ain");
	int err = 0;
	struct ain *a = ain_open(p, &err);
	if (!a || a->version != d->version || a->nr_strings != n)
		xc_env_fail("the fixture AIN does not read back");
	for (int i = 0; i < n; i++)
		if (!a->strings[i] || strcmp(a->strings[i]->text, strs[i]))
			xc_env_fail("a fixture string does not read back");
	struct gb18030_scores sc;
	memset(&sc, 0, sizeof sc);
	bool strict = n ? gb18030_detect_strings(a->strings, a->nr_strings, &sc) : false;
	printf("  %s table: n=%d legacy=%d boundary=%d sjis_invalid=%d nonascii=%d utf8_valid=%d strict=%d\n", d->id, n,
	       sc.legacy, sc.boundary, sc.sjis_invalid, sc.nonascii, sc.utf8_valid, strict);
	bool ok = true;
	switch (d->table) {
	case EN_T_GBK: case EN_T_REAL: ok = strict && sc.legacy > 5; break;
	case EN_T_SJIS: ok = !strict && sc.legacy > 5 && sc.boundary == 0 && sc.sjis_invalid == 0; break;
	case EN_T_UTF8: ok = !strict && sc.legacy > 5 && sc.utf8_valid * 2 >= sc.nonascii; break;
	case EN_T_ASCII: ok = !strict && sc.legacy == 0 && sc.nonascii == 0; break;
	case EN_T_EMPTY: ok = n == 0; break;
	}
	if (!ok)
		xc_env_fail("the fixture table does not have the scores the case needs");
	ain_free(a);
}

/* In a forked process: main from the state of a new process. Writes
 * "R <rc> <ain_is_gb18030> <rule> <game_charset_is_gbk>" to fd once main
 * returned. */
static _Noreturn void en_run_main(const struct en_def *d, int fd)
{
	alarm(30);
	ain_is_gb18030 = false;
	sys4_set_string_charset(SYS4_CHARSET_SJIS);
	char home[PATH_MAX], game[PATH_MAX], out[PATH_MAX], errp[PATH_MAX];
	en_path(home, sizeof home, "home");
	en_path(game, sizeof game, "game/game.ain");
	en_path(out, sizeof out, "out.txt");
	en_path(errp, sizeof errp, "err.txt");
	setenv("XSYSTEM4_HOME", home, 1);
	if (d->env_charset)
		setenv("XSYS4_STRING_CHARSET", d->env_charset, 1);
	else
		unsetenv("XSYS4_STRING_CHARSET");
	fflush(NULL);
	int o = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600), e = open(errp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (o < 0 || e < 0 || dup2(o, 1) < 0 || dup2(e, 2) < 0)
		_exit(XC_ENV_EXIT);
	char *argv[8];
	static char prog[] = "xsystem4", audit[] = "--audit";
	int argc = 0;
	argv[argc++] = prog;
	if (!d->want_stdout)
		argv[argc++] = audit;
	for (int i = 0; i < 3 && d->args[i]; i++)
		argv[argc++] = (char *)d->args[i];
	argv[argc++] = game;
	argv[argc] = NULL;
	optind = 1;
	optreset = 1;
	opterr = 1;
	int rc = xsystem4_original_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	char line[96];
	int l = snprintf(line, sizeof line, "R %d %d %d %d\n", rc, ain_is_gb18030 ? 1 : 0,
			 sys4_get_string_charset() == SYS4_CHARSET_GBK ? 1 : 0, game_charset_is_gbk() ? 1 : 0);
	_exit(write(fd, line, l) == l ? 0 : XC_ENV_EXIT);
}

/* A stderr line that names both (case-insensitively for the key). */
static bool en_line_names(const char *text, const char *key, const char *value)
{
	const char *p = text;
	while (*p) {
		const char *end = strchr(p, '\n');
		size_t n = end ? (size_t)(end - p) : strlen(p);
		char *line = strndup(p, n);
		bool has_value = strstr(line, value) != NULL;
		for (char *c = line; *c; c++)
			*c = tolower((unsigned char)*c);
		bool has_key = strstr(line, key) != NULL;
		free(line);
		if (has_key && has_value)
			return true;
		if (!end)
			break;
		p = end + 1;
	}
	return false;
}

static void en_run_case(const struct en_def *d)
{
	printf("  %s: %s\n", d->id, d->what);
	en_setup_dir();
	const char **strs = NULL;
	int n = en_table_strings(d->table, &strs);
	en_write_ain(d->version, strs, n);
	if (d->home_rc)
		en_write_file("home/.xsys4rc", d->home_rc, strlen(d->home_rc));
	if (d->game_rc)
		en_write_file("game/.xsys4rc", d->game_rc, strlen(d->game_rc));
	en_check_fixture(d, strs, n);
	free(strs);
	int fd[2];
	if (pipe(fd))
		xc_env_fail("pipe");
	xc_ready();
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0)
		xc_env_fail("fork");
	if (pid == 0) {
		close(fd[0]);
		en_run_main(d, fd[1]);
	}
	close(fd[1]);
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	char got[96] = "";
	ssize_t nr = read(fd[0], got, sizeof got - 1);
	close(fd[0]);
	if (nr > 0)
		got[nr] = 0;
	char *err = en_read_file("err.txt"), *out = en_read_file("out.txt");
	int rc = -1, a = -1, rule = -1, both = -1;
	bool returned = nr > 0 && sscanf(got, "R %d %d %d %d", &rc, &a, &rule, &both) == 4;
	XC_CHECK(returned, "%s: main did not return (wait status %d); its last stderr: %.300s", d->id, st,
		 strlen(err) > 300 ? err + strlen(err) - 300 : err);
	if (returned) {
		printf("  %s got rc=%d ain_is_gb18030=%d rule=%s game_charset_is_gbk=%d\n", d->id, rc, a, rule ? "GBK" : "SJIS",
		       both);
		XC_CHECK(rc == 0, "%s: main returned %d", d->id, rc);
		if (d->want_ain >= 0) {
			XC_CHECK(a == d->want_ain, "%s: ain_is_gb18030=%d, want %d", d->id, a, d->want_ain);
			XC_CHECK(rule == d->want_gbk_rule, "%s: the character rule is %s, want %s", d->id, rule ? "GBK" : "SJIS",
				 d->want_gbk_rule ? "GBK" : "SJIS");
			XC_CHECK(both == (d->want_ain && d->want_gbk_rule), "%s: game_charset_is_gbk=%d", d->id, both);
		}
	}
	/* a valid value (or none) is not warned about */
	if (!d->warn_value)
		XC_CHECK(!en_line_names(err, "invalid value", ""), "%s: the startup log has an \"Invalid value\" line", d->id);
	for (int k = 0; k < 2 && d->want_log[k]; k++)
		XC_CHECK(strstr(err, d->want_log[k]) != NULL, "%s: the startup log has no \"%s\"", d->id, d->want_log[k]);
	if (d->no_aincheck)
		XC_CHECK(!strstr(err, "AINCHECK"), "%s: the startup log has an AINCHECK line", d->id);
	if (d->warn_value)
		XC_CHECK(en_line_names(err, d->warn_key, d->warn_value), "%s: no warning line names %s and \"%s\"", d->id,
			 d->warn_key, d->warn_value);
	if (d->want_stdout)
		XC_CHECK(strstr(out, d->want_stdout) != NULL, "%s: the usage text does not list %s", d->id, d->want_stdout);
	free(err);
	free(out);
	en_cleanup();
}

#define EN_FN(k) static void en_case_##k(void) { en_run_case(&en_defs[k]); }
EN_FN(0) EN_FN(1) EN_FN(2) EN_FN(3) EN_FN(4) EN_FN(5) EN_FN(6) EN_FN(7) EN_FN(8) EN_FN(9)
EN_FN(10) EN_FN(11) EN_FN(12) EN_FN(13) EN_FN(14) EN_FN(15) EN_FN(16) EN_FN(17) EN_FN(18) EN_FN(19)
EN_FN(20) EN_FN(21) EN_FN(22) EN_FN(23) EN_FN(24) EN_FN(25) EN_FN(26) EN_FN(27) EN_FN(28) EN_FN(29)
EN_FN(30) EN_FN(31) EN_FN(32) EN_FN(33) EN_FN(34) EN_FN(35) EN_FN(36) EN_FN(37) EN_FN(38) EN_FN(39)

static int test_encoding_select(void)
{
	static void (*const fns[])(void) = {
		en_case_0, en_case_1, en_case_2, en_case_3, en_case_4, en_case_5, en_case_6, en_case_7, en_case_8,
		en_case_9, en_case_10, en_case_11, en_case_12, en_case_13, en_case_14, en_case_15, en_case_16,
		en_case_17, en_case_18, en_case_19, en_case_20, en_case_21, en_case_22, en_case_23, en_case_24,
		en_case_25, en_case_26, en_case_27, en_case_28, en_case_29, en_case_30, en_case_31, en_case_32,
		en_case_33, en_case_34, en_case_35, en_case_36, en_case_37, en_case_38, en_case_39,
	};
	_Static_assert(sizeof fns / sizeof *fns == sizeof en_defs / sizeof *en_defs, "one function per case");
	struct xc_case cases[sizeof en_defs / sizeof *en_defs];
	for (int i = 0; i < EN_N; i++)
		cases[i] = (struct xc_case){ en_defs[i].id, fns[i] };
	return xc_run("ENCODING-SELECT", cases, EN_N, 60);
}

/* ---- mosaic-effect ---- */

/* The rendering runs in a process of its own, started with posix_spawn
 * (this probe binary in the helper mode mosaic-render): on macOS a process
 * made by fork() alone was measured not to compile a shader that is not in
 * the system's shader cache yet (the GL draw is dropped, the driver logs
 * "buildPipelineState failed"), and a changed shader never is. The helper writes the three control frames and
 * the mosaic frame to a file; the case reads and checks them. */
#include <mach-o/dyld.h>
#include <spawn.h>
extern char **environ;

struct mz_pic { int w, h; uint8_t *ctl[3], *px; };
static const float mz_ctl_p[3] = { 0.0f, 0.5f, 1.0f };

static uint8_t *mz_pattern(int w, int h, uint8_t blue)
{
	uint8_t *px = xmalloc((size_t)w * h * 4);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8_t *p = px + ((size_t)y * w + x) * 4;
			p[0] = (2 * x) & 0xFF; p[1] = (2 * y) & 0xFF; p[2] = blue; p[3] = 255;
		}
	}
	return px;
}

/* mosaic-render (helper, run by the mosaic-effect cases): MZ_RENDER="w h p
 * file". Exit 3: no GL; 0: the file holds "MZR1", w, h and four w*h RGBA
 * frames (crossfade at 0, 0.5, 1; mosaic at p). An engine ERROR (a shader
 * that does not compile) exits 1. */
static int test_mosaic_render(void)
{
	const char *spec = getenv("MZ_RENDER");
	int w = 0, h = 0;
	float p = 0;
	char path[PATH_MAX];
	if (!spec || sscanf(spec, "%d %d %f %1023s", &w, &h, &p, path) != 4 || w <= 0 || h <= 0)
		return 3;
	CGLPixelFormatAttribute attrs[] = {
		kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
		kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
		kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8, 0 };
	CGLPixelFormatObj pf = NULL;
	CGLContextObj ctx = NULL;
	GLint count = 0;
	if (CGLChoosePixelFormat(attrs, &pf, &count) || !pf || CGLCreateContext(pf, NULL, &ctx) || CGLSetCurrentContext(ctx))
		return 3;
	CGLDestroyPixelFormat(pf);
	glewExperimental = GL_TRUE;
	const char *src = getenv("XS4_SRC");
	if (glewInit() != GLEW_OK || !src || chdir(src))
		return 3;
	while (glGetError() != GL_NO_ERROR) {}
	probe_gl_initialize();
	config.view_width = w;
	config.view_height = h;
	Texture old = { 0 }, new = { 0 }, dst = { 0 };
	uint8_t *a = mz_pattern(w, h, 0), *b = mz_pattern(w, h, 255);
	gfx_init_texture_with_pixels(&old, w, h, a);
	gfx_init_texture_with_pixels(&new, w, h, b);
	gfx_init_texture_blank(&dst, w, h);
	FILE *f = fopen(path, "wb");
	if (!f)
		return 3;
	int32_t hdr[2] = { w, h };
	fwrite("MZR1", 1, 4, f);
	fwrite(hdr, sizeof hdr, 1, f);
	for (int k = 0; k < 4; k++) {
		/* the blank target before each frame, so that a dropped draw shows */
		gfx_delete_texture(&dst);
		gfx_init_texture_blank(&dst, w, h);
		if (k < 3)
			effect_update_texture(EFFECT_CROSSFADE, &dst, &old, &new, mz_ctl_p[k]);
		else
			effect_update_texture(EFFECT_CROSSFADE_MOSAIC, &dst, &old, &new, p);
		uint8_t *px = gfx_get_pixels(&dst);
		fwrite(px, 4, (size_t)w * h, f);
		free(px);
	}
	if (fclose(f))
		return 3;
	printf("MOSAIC-RENDER %dx%d p=%.6f done\n", w, h, p);
	return 0;
}

/* Runs the helper for one picture; environment failure when it cannot run. */
static void mz_setup(struct mz_pic *m, int w, int h, float p)
{
	char exe[PATH_MAX], real[PATH_MAX], file[PATH_MAX], spec[PATH_MAX + 64];
	uint32_t size = sizeof exe;
	if (_NSGetExecutablePath(exe, &size) || !realpath(exe, real))
		xc_env_fail("cannot find the probe binary");
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !base[0])
		base = getenv("TMPDIR");
	if (!base || !base[0])
		base = "/tmp";
	snprintf(file, sizeof file, "%s/xsys4-mosaic-XXXXXX", base);
	int fd = mkstemp(file);
	if (fd < 0)
		xc_env_fail("cannot create the frame file");
	close(fd);
	snprintf(spec, sizeof spec, "%d %d %.9g %s", w, h, p, file);
	setenv("MZ_RENDER", spec, 1);
	char mode[] = "mosaic-render";
	char *args[] = { real, ain->ain_path, mode, NULL };
	xc_ready();
	fflush(NULL);
	pid_t pid;
	if (posix_spawn(&pid, real, NULL, NULL, args, environ))
		xc_env_fail("posix_spawn");
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	if (WIFEXITED(st) && WEXITSTATUS(st) == 3) {
		unlink(file);
		xc_env_fail("the render helper has no GL context or no source shaders");
	}
	m->w = w; m->h = h;
	m->px = NULL;
	for (int k = 0; k < 3; k++)
		m->ctl[k] = NULL;
	FILE *f = fopen(file, "rb");
	size_t n = (size_t)w * h * 4;
	char magic[4];
	int32_t hdr[2];
	bool ok = st == 0 && f && fread(magic, 1, 4, f) == 4 && !memcmp(magic, "MZR1", 4) && fread(hdr, sizeof hdr, 1, f) == 1
		&& hdr[0] == w && hdr[1] == h;
	for (int k = 0; ok && k < 4; k++) {
		uint8_t *buf = xmalloc(n);
		ok = fread(buf, 1, n, f) == n;
		if (k < 3) m->ctl[k] = buf; else m->px = buf;
		if (!ok) free(buf);
	}
	if (f)
		fclose(f);
	unlink(file);
	XC_CHECK(ok, "%dx%d p=%.6f: the render helper ended with status %d without the frames", w, h, p, st);
}

/* Pixels that differ from the per-pixel crossfade at p by more than 1 in a
 * channel (red and green: the pixel's own 2x and 2y). */
static int mz_blue(float p)
{
	/* a progress outside 0..1 mixes beyond the two scenes; the colour is clamped */
	return (int)lroundf(255.0f * fminf(fmaxf(p, 0.0f), 1.0f));
}

static int mz_crossfade_misses(const struct mz_pic *m, const uint8_t *px, float p, int *fx, int *fy, const uint8_t **fp)
{
	int miss = 0;
	int blue = mz_blue(p);
	for (int y = 0; y < m->h; y++) {
		for (int x = 0; x < m->w; x++) {
			const uint8_t *q = px + ((size_t)y * m->w + x) * 4;
			if (abs(q[0] - ((2 * x) & 0xFF)) > 1 || abs(q[1] - ((2 * y) & 0xFF)) > 1 || abs(q[2] - blue) > 1) {
				if (!miss) { *fx = x; *fy = y; *fp = q; }
				miss++;
			}
		}
	}
	return miss;
}

/* The control: a plain crossfade reproduces its inputs. Returns whether the
 * frames are there to check. */
static bool mz_control(struct mz_pic *m)
{
	if (!m->px)
		return false;
	for (int i = 0; i < 3; i++) {
		int fx = 0, fy = 0; const uint8_t *fp = NULL;
		int miss = mz_crossfade_misses(m, m->ctl[i], mz_ctl_p[i], &fx, &fy, &fp);
		if (miss) {
			printf("  ENVIRONMENT: crossfade p=%.3f misses %d pixels, first (%d,%d)\n", mz_ctl_p[i], miss, fx, fy);
			xc_env_fail("the effect pass does not reproduce its inputs");
		}
	}
	return true;
}

static void mz_free(struct mz_pic *m)
{
	for (int k = 0; k < 3; k++)
		free(m->ctl[k]);
	free(m->px);
}

/* The chunk size at p (worked out in float, as the shader's progress is). */
static int mz_chunk(float p)
{
	float c = floorf((1.0f - fabsf(p * 2.0f - 1.0f)) * 48.0f) * 2.0f + 1.0f;
	return c < 1.0f ? 1 : (int)c;
}

static bool mz_same(const uint8_t *a, const uint8_t *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

/* Edges along a row (axis 0) or a column (axis 1): positions i in 1..n-1
 * where the pixel differs from the one before. */
static int mz_edges(const struct mz_pic *m, const uint8_t *px, int axis, int line, int *out, int max)
{
	int n = axis ? m->h : m->w, k = 0;
	for (int i = 1; i < n; i++) {
		const uint8_t *a = axis ? px + ((size_t)(i - 1) * m->w + line) * 4 : px + ((size_t)line * m->w + i - 1) * 4;
		const uint8_t *b = axis ? px + ((size_t)i * m->w + line) * 4 : px + ((size_t)line * m->w + i) * 4;
		if (!mz_same(a, b) && k < max)
			out[k++] = i;
	}
	return k;
}

/* The edges of a grid of chunk size c anchored at the centre of n pixels. */
static int mz_want_edges(int n, int c, int *out, int max)
{
	int centre = n / 2, k = 0;
	int first = centre - (centre / c) * c;
	for (int e = first; e < n && k < max; e += c)
		if (e > 0)
			out[k++] = e;
	return k;
}

static void mz_print_edges(const char *what, const int *e, int n)
{
	printf("    %s:", what);
	for (int i = 0; i < n && i < 40; i++)
		printf(" %d", e[i]);
	printf("%s\n", n > 40 ? " ..." : "");
}

static void mz_grid_case(int w, int h, float p)
{
	struct mz_pic m;
	mz_setup(&m, w, h, p);
	if (!mz_control(&m))
		return;
	int c = mz_chunk(p);
	const uint8_t *px = m.px;
	for (int axis = 0; axis < 2; axis++) {
		int n = axis ? h : w;
		/* a line through the middle of the chunk after the centre on the other axis */
		int other = axis ? w : h;
		int line = other / 2 + (c > 1 ? c / 2 : 0);
		if (line >= other) line = other - 1;
		int got[1024], want[1024];
		int ng = mz_edges(&m, px, axis, line, got, 1024), nw = mz_want_edges(n, c, want, 1024);
		bool same = ng == nw && !memcmp(got, want, ng * sizeof *got);
		printf("  p=%.4f c=%d %s %d (%s %d)\n", p, c, axis ? "column" : "row", line, axis ? "height" : "width", n);
		mz_print_edges("chunk edges", got, ng);
		if (!same)
			mz_print_edges("want", want, nw);
		XC_CHECK(same, "%dx%d p=%.4f: the chunk edges along the %s are not a grid of %d anchored at the centre",
			 w, h, p, axis ? "column" : "row", c);
	}
	mz_free(&m);
}

/* The first pixel of the chunk that holds pixel i of n (the grid of
 * mz_want_edges: the partial chunk at the start begins at 0). */
static int mz_chunk_start(int i, int n, int c)
{
	int first = (n / 2) % c;
	return i < first ? 0 : i - (i - first) % c;
}

/* Each chunk, the partial ones at the screen edges included, is the
 * crossfade sampled at its first pixel: every pixel holds the red and the
 * green of the chunk's lowest x and y, exactly, and the crossfade's blue. */
static void mz_sample_case(int w, int h, float p)
{
	struct mz_pic m;
	mz_setup(&m, w, h, p);
	if (!mz_control(&m))
		return;
	int c = mz_chunk(p);
	const uint8_t *px = m.px;
	int bad = 0, blue = mz_blue(p);
	for (int y = 0; y < h; y++) {
		int y0 = mz_chunk_start(y, h, c);
		for (int x = 0; x < w; x++) {
			int x0 = mz_chunk_start(x, w, c);
			int r = (2 * x0) & 0xFF, g = (2 * y0) & 0xFF;
			const uint8_t *q = px + ((size_t)y * w + x) * 4;
			if (q[0] != r || q[1] != g || abs(q[2] - blue) > 1) {
				if (!bad)
					printf("    (%d,%d) in the chunk from (%d,%d) = (%d,%d,%d), want (%d,%d,%d)\n", x, y, x0, y0,
					       q[0], q[1], q[2], r, g, blue);
				bad++;
			}
		}
	}
	printf("  p=%.4f c=%d: %d pixels checked, %d off\n", p, c, w * h, bad);
	XC_CHECK(bad == 0, "%dx%d p=%.4f: %d pixels are not their chunk's first pixel sample", w, h, p, bad);
	mz_free(&m);
}

/* Chunk size 1: the per-pixel crossfade (and p = 0 / p = 1: old / new). */
static void mz_pixel_case(int w, int h, float p)
{
	struct mz_pic m;
	mz_setup(&m, w, h, p);
	if (!mz_control(&m))
		return;
	const uint8_t *px = m.px;
	int fx = 0, fy = 0; const uint8_t *fp = NULL;
	int miss = mz_crossfade_misses(&m, px, p, &fx, &fy, &fp);
	printf("  p=%.6f (chunk %d): %d of %d pixels differ from the per-pixel crossfade\n", p, mz_chunk(p), miss, w * h);
	if (miss)
		printf("    first (%d,%d) = (%d,%d,%d)\n", fx, fy, fp[0], fp[1], fp[2]);
	XC_CHECK(miss == 0, "%dx%d p=%.6f: not the per-pixel crossfade", w, h, p);
	mz_free(&m);
}

static void mz1(void) { mz_grid_case(120, 100, 0.125f); }
static void mz2(void) { mz_grid_case(120, 100, 0.375f); }
static void mz3(void) { mz_grid_case(120, 100, 0.1f); }
static void mz4(void) { mz_grid_case(120, 100, 0.5f); }
static void mz5(void) { mz_grid_case(800, 600, 0.125f); }
static void mz6(void) { mz_grid_case(800, 600, 0.75f); }
static void mz7(void) { mz_sample_case(120, 100, 0.125f); }
static void mz8(void) { mz_sample_case(120, 100, 0.1f); }
static void mz9(void) { mz_pixel_case(120, 100, 0.0f); }
static void mz10(void) { mz_pixel_case(120, 100, 1.0f); }
static void mz11(void) { mz_pixel_case(120, 100, 1.0f / 512.0f); }
static void mz12(void) { mz_pixel_case(120, 100, 511.0f / 512.0f); }
/* a small chunk (3) with many chunks between the screen edge and the centre */
static void mz13(void) { mz_grid_case(120, 100, 0.0125f); }
/* chunk size 1 as a grid: an edge between every two pixels (the per-pixel
 * check alone accepts chunks of 2, red and green being checked within 1) */
static void mz14(void) { mz_grid_case(120, 100, 1.0f / 512.0f); }
/* a 1280x720 screen with chunks of 91 (p = 15/32): the grid stays exact on
 * a wide screen */
static void mz15(void) { mz_grid_case(1280, 720, 0.46875f); }
/* an odd screen size: the centre is the pixel at floor(w / 2), floor(h / 2) */
static void mz16(void) { mz_sample_case(121, 101, 0.125f); }
/* a progress outside 0..1: chunks of 1 */
static void mz17(void) { mz_pixel_case(120, 100, -0.0625f); }
static void mz18(void) { mz_pixel_case(120, 100, 1.0625f); }
/* a 1024x768 screen half-way (the largest chunks, 97): the first pixel of
 * every chunk where red and green wrap around */
static void mz19(void) { mz_sample_case(1024, 768, 0.5f); }

static int test_mosaic_effect(void)
{
	static const struct xc_case cases[] = {
		{ "MZ1", mz1 }, { "MZ2", mz2 }, { "MZ3", mz3 }, { "MZ4", mz4 }, { "MZ5", mz5 }, { "MZ6", mz6 },
		{ "MZ7", mz7 }, { "MZ8", mz8 }, { "MZ9", mz9 }, { "MZ10", mz10 }, { "MZ11", mz11 }, { "MZ12", mz12 },
		{ "MZ13", mz13 }, { "MZ14", mz14 }, { "MZ15", mz15 }, { "MZ16", mz16 }, { "MZ17", mz17 }, { "MZ18", mz18 },
		{ "MZ19", mz19 },
	};
	return xc_run("MOSAIC-EFFECT", cases, sizeof cases / sizeof cases[0], 60);
}
