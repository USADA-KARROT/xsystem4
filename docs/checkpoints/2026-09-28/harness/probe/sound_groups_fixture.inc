/* sound-groups (G1, 2026-10-24): the v14 sound groups.
 *
 * The original engine builds its mixer groups from a table in the main EX
 * file, keeps every group's default and current volume as integers, attaches
 * each sound to the group its archive's timemap gives it (and takes the loop
 * points from there), multiplies the gains up the parent chain, mutes on lost
 * focus unless the game allows global focus, and keeps the volumes in
 * Volume.sav. This mode checks those through the engine's own entry points:
 * the AIN's KiwiSoundEngine declarations called through ffi, the module
 * init/fini, the asset manager reading real archive files, the SDL event pump,
 * the exit path, and the engine's SDL audio callback for the samples.
 *
 * Original evidence (static disassembly of the one CN executable; private
 * research notes have the listings): group count 0x4a1d50 (first index without
 * a default), parent 0x4a1e50 (<=5 -> 0, >5 -> 3 when the table's voice-group
 * column is negative, else 5), defaults 0x4a20d0/0x49fe90 (float x 100.0,
 * truncated, default and current kept apart), current clamp 0x49fc90, the
 * HLL wrappers of GetMixerVolume/GetMixerDefaultVolume/SetMixerVolume
 * (0x4a66e0/0x4a6720/0x4a67e0) refuse a group outside 0..count-1, the chain
 * 0x4a5e70/0x4a5fa5/0x4a5fe6 (a muted node gives 0, the root counted once),
 * timemap names 0x4a0a20/0x4a0e00/0x4195b0 (A-Z folded, the byte after a lead
 * byte 0x81..0xFE kept), file query fallback 2 0x4a1a38 (it looks up maps keyed
 * by name, not the archive, 0x4a45a0), GetGroupNum 0x4a1951 -> 0x4a3da0 (the channel's group, 0
 * without a channel), prepare fallback from the Prepare(..., bStreaming)
 * argument (the HLL dispatch's jump table 0x4a1c34 follows the AIN's
 * declaration order, entry 9 is Prepare at 0x4a1640, which passes its fourth
 * argument on: 0x4a169c -> 0x4a3430 -> 0x49a130 -> 0x4a0040 -> channel +0x10;
 * nonzero -> 1, zero -> 2), both loop points handed to the backend when
 * neither is negative 0x4a357b, focus 0x4a1438/0x4a11b0/0x4a1a7c (gain 0 when
 * GlobalFocus is off and +0x31 is 0; inferred to be the window's activation
 * from its writes 0x4bdc14/0x4be5e7, which have the shape of WM_ACTIVATE),
 * Volume.sav 0x49f780/0x49f980 (00, count, (volume, mute, 00) x count; read
 * after the EX defaults, written on shutdown and restart).
 *
 * Port strategies, checked here as such and not claimed as original behavior:
 * the groups exist as soon as KiwiSoundEngine's init returns (it runs before
 * MainEXFile's, and the audio callback may run from then on); the loop end is
 * exclusive and is used only when 0 <= begin < end; an end of 0, an end at or
 * before the begin, or a negative point loops the whole file as before; a
 * sound played once is not cut short; Volume.sav is written only on
 * shutdown/restart (not while volumes change) and without leftovers; a restart
 * keeps the volumes in memory; a damaged Volume.sav never crashes and never
 * touches groups it does not reach; a timemap of another shape is ignored for
 * its archive (its files fall back as if they had no row); a gain change
 * takes effect from the next chunk the callback mixes (sg_level reads the
 * last 512 frames of five chunks; the original's smoothing, if any, is not
 * known); a loop's last lap plays on to the end of the file (SG7's play-once
 * cases accept that or a stop at the loop's end); of two timemap rows of one
 * name the first stays (SG5's vx_alpha).
 *
 * The v14 shapes are bound through the engine's declaration-shape hook
 * (hll_shape_select_function, called by link_static_library): SG10 links
 * declarations of other shapes the same way and wants the library table's
 * own function back. A _PreLink that replaced the table's functions for the
 * real AIN would be right for this game and still fail there.
 *
 * Only exactly v14 with the GBK string rule and a recognized table uses the
 * groups; v13, v15, SJIS, the GB18030 flag without the GBK rule, no table and
 * tables of another shape keep the legacy mixers (SG10, which passes on the
 * engine before the change). Unknown declaration shapes keep the binding of
 * the library table.
 *
 * Cases (each in forked children): SG0 the synthetic files read back with
 * libsys4; SG1 gate, init order (Kiwi before/after MainEXFile, no device,
 * dummy device), repeated init, restart; SG2 tree, parents, defaults, root
 * once, mute chain; SG3 integer read back 0..100, clamp, bad groups; SG4 the
 * samples through the chain while playing; SG5 name-based identity (archive
 * index vs name vs number, both archives, the timemap member in the middle of
 * its archive, case, double-byte names under the GBK lead rule, rows without
 * files, group 0, fallbacks of the file query and of Prepare); SG6 a missing
 * or odd timemap; SG7 loop points from the samples (mono, stereo, short loop,
 * chunk edges, ends at the file's last frame, at its end and past it, a
 * 22.05 kHz file, voice, play once, unusable points); SG8 focus with
 * GlobalFocus off and on, user values kept, position and fade go on; SG9
 * Volume.sav bytes, reopen, damaged files, restart, no write before quitting,
 * nothing else left in the save folder; SG10 the legacy mixers for every
 * other game (with a v13 game's BGI loop and WAI groups) and unknown
 * declaration shapes; SG11 the real audio thread under changes and a restart;
 * SG12 the game's own list filter, the voice-group mute, the file groups the
 * BGM cap reads.
 *
 * A restart (sg_restart) runs what system.Reset runs: every library's
 * _ModuleFini, the VM state and heap anew, then _PreLink, _PostLink and
 * _ModuleInit; it is not the real Reset bytecode (system-reset covers that).
 *
 * Data: everything is synthetic and written here (an EX writer, an AFA writer,
 * PCM16 WAV at 44.1 and 22.05 kHz, a BGI and a WAI) into a fresh mkdtemp
 * directory under $XS4_SAVE_TMP or
 * $TMPDIR that is removed at the end; it is refused inside XS4_SRC or
 * XS4_MASTER_GAME. Names are neutral. The only game-defined bytes are the
 * schema keys the engine has to recognize (table and column names, GBK), as in
 * base_ui_fixture.inc. config.save_dir always points into that directory.
 *
 * Samples: build_probe.py appends probe_audio_pull() to a copy of
 * src/audio_mixer.c; it runs the engine's SDL callback (audio_callback) on the
 * calling thread. The cases that pull open no audio device (an unavailable
 * driver), so the callback never runs on another thread there. SG11 uses the
 * dummy driver and its real callback thread instead. ASan does not see data
 * races: the lock contract is for review. The focus events SG8 pushes carry
 * no window ID (the port does not filter them by window).
 *
 * Not covered: the original's ducking curve and timing, its real background
 * sound routes, the internal volumes and the movie sound (G2), the system
 * sound table (G11), non-contiguous group keys, more than 64 groups, an
 * archive overlay order, Rance 9's Sound_Seek binding (this AIN does not
 * declare it), and real audio output.
 */
#include <dlfcn.h>
#include <errno.h>
#include <ftw.h>
#include <math.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>
#include <SDL.h>
#include "system4/afa.h"
#include "system4/ex.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "audio.h"
#include "mixer.h"
#include "xsystem4.h"

extern void probe_audio_pull(float *out, int frames);
extern bool libraries_initialized;

/* Schema keys of the main EX table (GBK): the names the engine has to
 * recognize for the table, its indexed group number, the voice-group column
 * (default -1) and the default volume column (default 0.8); the same keys as
 * src/audio_mixer.c's GROUP_*. */
#define SG_KEY_TABLE   "\xa3\xc5\xa3\xdf\xc2\x95\xd2\xf4\xc8\xba\xbd\x4d\xd4\x4f\xb6\xa8"
#define SG_KEY_GROUP   "\xc8\xba\xbd\x4d\xbe\x8e\xcc\x96"
#define SG_KEY_VOICE   "\x8c\x9d\x8f\xea\xd5\x5a\xd2\xf4\xc8\xba\xbd\x4d\xbe\x8e\xcc\x96"
#define SG_KEY_DEFAULT "\xc4\xac\xd5\x4a\xa5\xdc\xa5\xea\xa5\xe5\xa9\x60\xa5\xe0"
/* Two names that differ only in a double-byte character's trail byte, one
 * in A-Z: an ASCII case fold must not touch it. */
#define SG_DB_UPPER "\x81\x41"
#define SG_DB_LOWER "\x81\x61"
/* The same with a lead byte the GBK rule has and the SJIS one does not
 * (0xBD; trail 'M' / 'm'): the original's rule takes 0x81..0xFE as lead bytes
 * (0x4195b0). Timemap rows without files (the archive's own name index folds
 * by the SJIS rule and cannot hold both). */
#define SG_GBK_UPPER "\xbd\x4d"
#define SG_GBK_LOWER "\xbd\x6d"

static int sg_failures, sg_printed;
#define SG_CHECK(c, ...) do { if (!(c)) { sg_failures++; if (sg_printed++ < 14) { printf("  FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)
#define SG_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define SG_NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 2e-4f)

/* ---- byte buffers, EX, AFA and WAV writers ------------------------------------- */

struct sg_buf { uint8_t *p; size_t n, cap; };

static void sgb_put(struct sg_buf *b, const void *d, size_t n)
{
	if (b->n + n > b->cap) {
		b->cap = (b->n + n) * 2 + 64;
		b->p = xrealloc(b->p, b->cap);
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
}
static void sgb_u32(struct sg_buf *b, uint32_t v)
{
	uint8_t x[4] = { v, v >> 8, v >> 16, v >> 24 };
	sgb_put(b, x, 4);
}
static void sgb_u16(struct sg_buf *b, uint16_t v)
{
	uint8_t x[2] = { v, v >> 8 };
	sgb_put(b, x, 2);
}
static void sgb_f32(struct sg_buf *b, float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	sgb_u32(b, u);
}
static void sgb_patch(struct sg_buf *b, size_t at, uint32_t v)
{
	uint8_t x[4] = { v, v >> 8, v >> 16, v >> 24 };
	memcpy(b->p + at, x, 4);
}
/* Length-prefixed, NUL padded to four bytes (at least one NUL). */
static void sgb_str(struct sg_buf *b, const char *s)
{
	size_t n = strlen(s), padded = (n + 4) & ~(size_t)3;
	static const uint8_t zero[4];
	sgb_u32(b, padded);
	sgb_put(b, s, n);
	sgb_put(b, zero, padded - n);
}

enum { SG_V_NONE, SG_V_INT, SG_V_FLOAT, SG_V_STRING };
struct sg_val { int kind; int i; float f; const char *s; };
#define SGI(v) ((struct sg_val){ SG_V_INT, (v), 0, NULL })
#define SGF(v) ((struct sg_val){ SG_V_FLOAT, 0, (v), NULL })
#define SGS(v) ((struct sg_val){ SG_V_STRING, 0, 0, (v) })
#define SGNONE ((struct sg_val){ SG_V_NONE, 0, 0, NULL })

static int sg_ex_type(int kind) { return kind == SG_V_INT ? EX_INT : kind == SG_V_FLOAT ? EX_FLOAT : EX_STRING; }
static void sgx_value(struct sg_buf *b, struct sg_val v)
{
	if (v.kind == SG_V_INT) sgb_u32(b, (uint32_t)v.i);
	else if (v.kind == SG_V_FLOAT) sgb_f32(b, v.f);
	else sgb_str(b, v.s);
}

struct sg_field { int kind; const char *name; bool index; struct sg_val dflt; };

/* A table block: fields, then the row and column counts (rows first, as the
 * game's tables are), then typed values. */
static void sgx_table(struct sg_buf *b, const char *name, const struct sg_field *f, int nf, const struct sg_val *v, int rows)
{
	sgb_u32(b, EX_TABLE);
	size_t size_at = b->n;
	sgb_u32(b, 0);
	size_t start = b->n;
	sgb_str(b, name);
	sgb_u32(b, nf);
	for (int i = 0; i < nf; i++) {
		sgb_u32(b, sg_ex_type(f[i].kind));
		sgb_str(b, f[i].name);
		sgb_u32(b, f[i].dflt.kind != SG_V_NONE);
		sgb_u32(b, f[i].index);
		if (f[i].dflt.kind != SG_V_NONE)
			sgx_value(b, f[i].dflt);
	}
	sgb_u32(b, rows);
	sgb_u32(b, nf);
	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < nf; c++) {
			sgb_u32(b, sg_ex_type(v[r * nf + c].kind));
			sgx_value(b, v[r * nf + c]);
		}
	}
	sgb_patch(b, size_at, b->n - start);
}
static void sgx_int(struct sg_buf *b, const char *name, int value)
{
	sgb_u32(b, EX_INT);
	size_t size_at = b->n;
	sgb_u32(b, 0);
	size_t start = b->n;
	sgb_str(b, name);
	sgb_u32(b, value);
	sgb_patch(b, size_at, b->n - start);
}
static void sgx_int_list(struct sg_buf *b, const char *name, int value)
{
	sgb_u32(b, EX_LIST);
	size_t size_at = b->n;
	sgb_u32(b, 0);
	size_t start = b->n;
	sgb_str(b, name);
	sgb_u32(b, 1);
	sgb_u32(b, EX_INT);
	sgb_u32(b, 4);
	sgb_u32(b, value);
	sgb_patch(b, size_at, b->n - start);
}
/* HEAD / EXTF / block count / DATA, zlib, then the EX byte encoding. */
static void sgx_file(struct sg_buf *out, const struct sg_buf *blocks, int nr_blocks)
{
	uLongf clen = compressBound(blocks->n);
	uint8_t *c = xmalloc(clen);
	int rc = compress2(c, &clen, blocks->p, blocks->n, 9);
	assert(rc == Z_OK);
	ex_encode(c, clen);
	sgb_put(out, "HEAD", 4); sgb_u32(out, 12);
	sgb_put(out, "EXTF", 4); sgb_u32(out, 1);
	sgb_u32(out, nr_blocks);
	sgb_put(out, "DATA", 4); sgb_u32(out, clen); sgb_u32(out, blocks->n);
	sgb_put(out, c, clen);
	free(c);
}

struct sg_member { const char *name; struct sg_buf data; };

/* AFA v2 without per-entry numbers: the archive index is the entry's place. */
static void sga_file(struct sg_buf *out, struct sg_member *m, int n)
{
	struct sg_buf table = {0};
	uint32_t off = 8;
	for (int i = 0; i < n; i++) {
		size_t len = strlen(m[i].name), padded = (len + 4) & ~(size_t)3;
		static const uint8_t zero[4];
		sgb_u32(&table, len);
		sgb_u32(&table, padded);
		sgb_put(&table, m[i].name, len);
		sgb_put(&table, zero, padded - len);
		sgb_u32(&table, 0);
		sgb_u32(&table, 0);
		sgb_u32(&table, off);
		sgb_u32(&table, m[i].data.n);
		off += m[i].data.n;
	}
	uLongf clen = compressBound(table.n);
	uint8_t *c = xmalloc(clen);
	int rc = compress2(c, &clen, table.p, table.n, 9);
	assert(rc == Z_OK);
	sgb_put(out, "AFAH", 4); sgb_u32(out, 0x1c);
	sgb_put(out, "AlicArch", 8); sgb_u32(out, 2); sgb_u32(out, 1);
	sgb_u32(out, 44 + clen);
	sgb_put(out, "INFO", 4); sgb_u32(out, clen + 16); sgb_u32(out, table.n); sgb_u32(out, n);
	sgb_put(out, c, clen);
	sgb_put(out, "DATA", 4); sgb_u32(out, off);
	for (int i = 0; i < n; i++)
		sgb_put(out, m[i].data.p, m[i].data.n);
	free(c);
	free(table.p);
}

/* PCM16 at 44.1 kHz (SG_RAMP_22K: 22.05 kHz). A ramp holds k+1 at frame k
 * (the right channel of a stereo ramp -(k+1)), so an output sample at unity
 * gain names its frame. */
enum { SG_CONST, SG_RAMP, SG_RAMP_STEREO, SG_RAMP_22K };
static struct sg_buf sg_wav(int kind, int frames, int value)
{
	struct sg_buf b = {0};
	int ch = kind == SG_RAMP_STEREO ? 2 : 1;
	uint32_t rate = kind == SG_RAMP_22K ? 22050 : 44100;
	uint32_t bytes = frames * ch * 2;
	sgb_put(&b, "RIFF", 4); sgb_u32(&b, 36 + bytes); sgb_put(&b, "WAVE", 4);
	sgb_put(&b, "fmt ", 4); sgb_u32(&b, 16); sgb_u16(&b, 1); sgb_u16(&b, ch);
	sgb_u32(&b, rate); sgb_u32(&b, rate * ch * 2); sgb_u16(&b, ch * 2); sgb_u16(&b, 16);
	sgb_put(&b, "data", 4); sgb_u32(&b, bytes);
	for (int k = 0; k < frames; k++) {
		if (kind == SG_CONST) {
			sgb_u16(&b, (uint16_t)(int16_t)value);
		} else {
			sgb_u16(&b, (uint16_t)(int16_t)(k + 1));
			if (ch == 2)
				sgb_u16(&b, (uint16_t)(int16_t)-(k + 1));
		}
	}
	return b;
}

/* ---- the synthetic game -------------------------------------------------------- */

struct sg_sound { const char *name; int kind, frames, value; bool row; int group, begin, end; };

/* Sound archive, in archive order. Index 0 is a group-1 sound; the voice
 * archive's index 0 is a group-6 voice (the same data number, two groups). */
static const struct sg_sound sg_sounds[] = {
	{ "sg_tone_b", SG_CONST, 8192, 4096, true, 1, 0, 8192 },
	{ "sg_tone_a", SG_CONST, 88200, 8192, true, 2, 0, 88200 },	/* 2 s: GetPos does not wrap in SG8 */
	{ "sg_root", SG_CONST, 8192, 2048, true, 0, 0, 8192 },
	{ "sg_ramp_m", SG_RAMP, 6000, 0, true, 2, 1000, 3000 },
	{ "sg_ramp_s", SG_RAMP_STEREO, 6000, 0, true, 2, 1000, 3000 },
	{ "sg_short", SG_RAMP, 3000, 0, true, 2, 500, 600 },
	{ "sg_edge_a", SG_RAMP, 5000, 0, true, 2, 0, 2048 },
	{ "sg_edge_b", SG_RAMP, 5000, 0, true, 2, 0, 2049 },
	{ "sg_once", SG_RAMP, 6000, 0, true, 2, 0, 6000 },
	{ "sg_end0", SG_RAMP, 4000, 0, true, 2, 100, 0 },
	{ "sg_rev", SG_RAMP, 4000, 0, true, 2, 3000, 1000 },
	{ "sg_neg", SG_RAMP, 4000, 0, true, 2, -1, 500 },
	/* the game's own loops mostly end at the file's length - 1 or at its
	 * length; an end past the file */
	{ "sg_tail_a", SG_RAMP, 6000, 0, true, 2, 1000, 5999 },
	{ "sg_tail_b", SG_RAMP, 6000, 0, true, 2, 1000, 6000 },
	{ "sg_tail_c", SG_RAMP, 6000, 0, true, 2, 1000, 9000 },
	/* 22.05 kHz: the loop points are frames of the file, not of the output */
	{ "sg_slow", SG_RAMP_22K, 3000, 0, true, 2, 500, 1500 },
	{ "sg_orphan_s", SG_CONST, 8192, 8192, false, 0, 0, 0 },
};
/* The timemap rows in another order than the archive, plus a row without a file. */
static const char *const sg_sound_rows[] = { "sg_rev", "sg_tone_a", "sg_ramp_s", "sg_tail_b", "sg_end0", "sg_root", "sg_short",
	"sg_edge_b", "sg_ramp_m", "sg_slow", "sg_neg", "sg_once", "sg_tail_c", "sg_tone_b", "sg_edge_a", "sg_tail_a", "sg_ghost_s" };

/* Voice archive in dictionary order (1, 10, 100, 2): the names are not the
 * archive ordinals and not numbers to parse. */
static const struct sg_sound sg_voices[] = {
	{ "1", SG_CONST, 8192, 8192, true, 6, 0, 8192 },
	{ "10", SG_CONST, 8192, 4096, true, 7, 0, 8192 },
	{ "100", SG_CONST, 8192, 2048, true, 3, 0, 8192 },
	{ "2", SG_CONST, 8192, 1024, true, 5, 0, 8192 },
	{ "vx_alpha", SG_CONST, 8192, 8192, true, 6, 0, 8192 },
	{ SG_DB_UPPER, SG_CONST, 8192, 8192, true, 6, 0, 8192 },
	{ SG_DB_LOWER, SG_CONST, 8192, 8192, true, 7, 0, 8192 },
	{ "vx_loop", SG_RAMP, 4000, 0, true, 6, 200, 1200 },
	{ "vx_orphan", SG_CONST, 8192, 8192, false, 0, 0, 0 },
};
static const char *const sg_voice_rows[] = { "2", "vx_loop", "100", SG_DB_LOWER, SG_GBK_UPPER, "1", "sg_ghost_v", "vx_alpha", "10",
	SG_GBK_LOWER, SG_DB_UPPER, "VX_ALPHA" };

/* Rows without a file (the group they get). */
static const struct { const char *name; int group; } sg_ghosts[] = {
	{ "sg_ghost_s", 7 }, { "sg_ghost_v", 7 }, { SG_GBK_UPPER, 6 }, { SG_GBK_LOWER, 7 },
	/* a second row of vx_alpha's name, after its first (port strategy: the
	 * first row stays) */
	{ "VX_ALPHA", 7 },
};

/* The timemap member is not the last one of its archive: an archive index
 * counts it like any file. */
#define SG_SOUND_TM_POS 3
#define SG_VOICE_TM_POS 2

static const struct sg_sound *sg_find(const struct sg_sound *v, int n, const char *name)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(v[i].name, name))
			return &v[i];
	return NULL;
}

/* Group table: 8 groups. Row 2 has a voice-group column >= 0 but is <= 5
 * (parent 0 anyway); row 6 -1 (parent 3); row 7 0 (parent 5). 0.537 -> 53
 * (truncated, not 54); group 3 is 1.0 -> 100; group 1 0.25 -> 25. */
#define SG_NR_GROUPS 8
static const float sg_defaults_f[SG_NR_GROUPS] = { 0.8f, 0.25f, 0.537f, 1.0f, 0.8f, 0.8f, 0.8f, 0.8f };
static const int sg_defaults[SG_NR_GROUPS] = { 80, 25, 53, 100, 80, 80, 80, 80 };
static const int sg_voice_col[SG_NR_GROUPS] = { -1, -1, 4, -1, -1, -1, -1, 0 };
static const int sg_parent[SG_NR_GROUPS] = { -1, 0, 0, 0, 0, 0, 3, 5 };

enum { SG_DS_VALID, SG_DS_NOTABLE, SG_DS_INTDEF, SG_DS_NOINDEX, SG_DS_NOROWS, SG_DS_LISTBLOCK,
       SG_DS_NOVOICETM, SG_DS_BADVOICETM, SG_DS_LEGACYMETA, SG_NR_DS };
static const char *const sg_ds_names[SG_NR_DS] = { "valid", "notable", "intdef", "noindex", "norows", "listblock",
	"novoicetm", "badvoicetm", "legacymeta" };
static char sg_root_dir[PATH_MAX];
static char sg_ds_dir[SG_NR_DS][PATH_MAX];

static bool sg_write(const char *dir, const char *name, const struct sg_buf *b)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(b->p, 1, b->n, f) == b->n;
	return !fclose(f) && ok;
}

/* SG10's legacy metadata: a BGM archive with one ramp, a BGI giving it the
 * loop 1000..3000 for ever (the old line format, each byte's nibbles
 * swapped), and a WAI (version 3) that puts the sound archive's data number 1
 * (sg_tone_b) in mixer 1 and every other one in mixer 0. */
#define SG_LEGACY_BGM "sg_bgm_ramp"
static bool sg_legacy_meta_files(const char *dir)
{
	struct sg_member m = { SG_LEGACY_BGM ".wav", sg_wav(SG_RAMP, 6000, 0) };
	struct sg_buf bga = {0};
	sga_file(&bga, &m, 1);
	bool ok = sg_write(dir, "sgprobeBA.afa", &bga);
	free(m.data.p);
	free(bga.p);
	struct sg_buf bgi = {0};
	const char *line = " 1, 0, 1000, 3000, -1\n";
	for (const char *c = line; *c; c++) {
		uint8_t x = (uint8_t)*c;
		x = x >> 4 | (x & 0xf) << 4;
		sgb_put(&bgi, &x, 1);
	}
	ok = ok && sg_write(dir, "sgprobe.bgi", &bgi);
	free(bgi.p);
	int records = SG_COUNT(sg_sounds) + 1;
	struct sg_buf wai = {0};
	sgb_put(&wai, "XI2\0", 4);
	sgb_u32(&wai, 0);
	sgb_u32(&wai, records + 1);
	sgb_u32(&wai, 3);
	for (int i = 16; i < 36; i += 4)
		sgb_u32(&wai, 0);
	for (int i = 0; i < records; i++) {
		sgb_u32(&wai, 0);
		sgb_u32(&wai, 0);
		sgb_u32(&wai, i == 0 ? 1 : 0);
	}
	ok = ok && sg_write(dir, "sgprobeWA.wai", &wai);
	free(wai.p);
	return ok;
}

static struct sg_buf sg_main_ex(int ds)
{
	struct sg_buf blocks = {0}, out = {0};
	int nb = 0;
	sgx_int(&blocks, "sg_unrelated_int", 7); nb++;
	static const struct sg_field of[] = { { SG_V_INT, "ID", true, SGNONE }, { SG_V_FLOAT, "Value", false, SGNONE } };
	struct sg_val ov[] = { SGI(0), SGF(0.1f), SGI(1), SGF(0.2f) };
	sgx_table(&blocks, "sg_unrelated_table", of, 2, ov, 2); nb++;
	struct sg_field f[3] = {
		{ SG_V_INT, SG_KEY_GROUP, true, SGNONE },
		{ SG_V_INT, SG_KEY_VOICE, false, SGI(-1) },
		{ SG_V_FLOAT, SG_KEY_DEFAULT, false, SGF(0.8f) },
	};
	if (ds == SG_DS_INTDEF)
		f[2] = (struct sg_field){ SG_V_INT, SG_KEY_DEFAULT, false, SGI(80) };
	if (ds == SG_DS_NOINDEX)
		f[0].index = false;
	struct sg_val v[SG_NR_GROUPS * 3];
	for (int g = 0; g < SG_NR_GROUPS; g++) {
		v[g * 3] = SGI(g);
		v[g * 3 + 1] = SGI(sg_voice_col[g]);
		v[g * 3 + 2] = ds == SG_DS_INTDEF ? SGI(sg_defaults[g]) : SGF(sg_defaults_f[g]);
	}
	if (ds == SG_DS_LISTBLOCK) {
		sgx_int_list(&blocks, SG_KEY_TABLE, 1); nb++;
	} else if (ds != SG_DS_NOTABLE) {
		sgx_table(&blocks, SG_KEY_TABLE, f, 3, v, ds == SG_DS_NOROWS ? 0 : SG_NR_GROUPS); nb++;
	}
	sgx_int(&blocks, "sg_unrelated_after", 3); nb++;
	sgx_file(&out, &blocks, nb);
	free(blocks.p);
	return out;
}

static struct sg_buf sg_timemap(const struct sg_sound *v, int n, const char *const *rows, int nr_rows, bool bad_group)
{
	struct sg_buf blocks = {0}, out = {0};
	struct sg_field f[4] = {
		{ SG_V_STRING, "Name", true, SGNONE },
		{ bad_group ? SG_V_STRING : SG_V_INT, "Group", false, SGNONE },
		{ SG_V_INT, "LoopBeginSample", false, SGI(0) },
		{ SG_V_INT, "LoopEndSample", false, SGI(0) },
	};
	struct sg_val *vals = xcalloc(nr_rows * 4, sizeof *vals);
	static char group_text[64][8];
	for (int r = 0; r < nr_rows; r++) {
		const struct sg_sound *s = sg_find(v, n, rows[r]);
		int group = 7, begin = s ? s->begin : 0, end = s ? s->end : 100;
		if (s)
			group = s->group;
		for (int g = 0; !s && g < SG_COUNT(sg_ghosts); g++)
			if (!strcmp(sg_ghosts[g].name, rows[r]))
				group = sg_ghosts[g].group;
		vals[r * 4] = SGS(rows[r]);
		snprintf(group_text[r], sizeof group_text[r], "%d", group);
		vals[r * 4 + 1] = bad_group ? SGS(group_text[r]) : SGI(group);
		vals[r * 4 + 2] = SGI(begin);
		vals[r * 4 + 3] = SGI(end);
	}
	sgx_table(&blocks, "SoundInfo", f, 4, vals, nr_rows);
	sgx_int(&blocks, "Version", 1);
	sgx_file(&out, &blocks, 2);
	free(vals);
	free(blocks.p);
	return out;
}

static bool sg_archive(const char *dir, const char *file, const struct sg_sound *v, int n,
		       const char *timemap, int tm_pos, const char *const *rows, int nr_rows, bool bad_group)
{
	struct sg_member *m = xcalloc(n + 1, sizeof *m);
	char (*names)[64] = xcalloc(n + 1, sizeof *names);
	int k = 0;
	for (int i = 0; i <= n; i++) {
		if (i == tm_pos && timemap) {
			snprintf(names[k], sizeof names[k], "%s", timemap);
			m[k].name = names[k];
			m[k].data = sg_timemap(v, n, rows, nr_rows, bad_group);
			k++;
		}
		if (i == n)
			break;
		snprintf(names[k], sizeof names[k], "%s.wav", v[i].name);
		m[k].name = names[k];
		m[k].data = sg_wav(v[i].kind, v[i].frames, v[i].value);
		k++;
	}
	struct sg_buf out = {0};
	sga_file(&out, m, k);
	bool ok = sg_write(dir, file, &out);
	for (int i = 0; i < k; i++)
		free(m[i].data.p);
	free(m);
	free(names);
	free(out.p);
	return ok;
}

static bool sg_inside(const char *path, const char *dir)
{
	char a[PATH_MAX], b[PATH_MAX];
	if (!dir || !*dir || !realpath(path, a) || !realpath(dir, b))
		return false;
	size_t n = strlen(b);
	return !strncmp(a, b, n) && (a[n] == '/' || a[n] == '\0');
}

static bool sg_make_data(void)
{
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !*base)
		base = getenv("TMPDIR");
	if (!base || !*base)
		base = "/tmp";
	snprintf(sg_root_dir, sizeof sg_root_dir, "%s/xs4-sound-groups-XXXXXX", base);
	if (!mkdtemp(sg_root_dir)) {
		printf("  FAIL: mkdtemp %s: %s\n", sg_root_dir, strerror(errno));
		return false;
	}
	if (sg_inside(sg_root_dir, getenv("XS4_SRC")) || sg_inside(sg_root_dir, getenv("XS4_MASTER_GAME"))) {
		printf("  FAIL: refusing a data directory inside the source tree or the original game: %s\n", sg_root_dir);
		rmdir(sg_root_dir);
		return false;
	}
	for (int ds = 0; ds < SG_NR_DS; ds++) {
		snprintf(sg_ds_dir[ds], sizeof sg_ds_dir[ds], "%s/%s", sg_root_dir, sg_ds_names[ds]);
		if (mkdir(sg_ds_dir[ds], 0700))
			return false;
		struct sg_buf ex = sg_main_ex(ds);
		bool ok = sg_write(sg_ds_dir[ds], "sgprobeEx.ex", &ex);
		free(ex.p);
		ok = ok && sg_archive(sg_ds_dir[ds], "sgprobeSound.afa", sg_sounds, SG_COUNT(sg_sounds), "timemap_sound.ex",
				      SG_SOUND_TM_POS, sg_sound_rows, SG_COUNT(sg_sound_rows), false);
		ok = ok && sg_archive(sg_ds_dir[ds], "sgprobeVoice.afa", sg_voices, SG_COUNT(sg_voices),
				      ds == SG_DS_NOVOICETM ? NULL : "timemap_voice.ex", SG_VOICE_TM_POS, sg_voice_rows,
				      SG_COUNT(sg_voice_rows), ds == SG_DS_BADVOICETM);
		if (ok && ds == SG_DS_LEGACYMETA)
			ok = sg_legacy_meta_files(sg_ds_dir[ds]);
		if (!ok) {
			printf("  FAIL: cannot write the synthetic game %s\n", sg_ds_dir[ds]);
			return false;
		}
	}
	return true;
}

static int sg_rm(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st; (void)flag; (void)ftw;
	return remove(path);
}
static void sg_remove_data(void)
{
	if (sg_root_dir[0] && !strncmp(strrchr(sg_root_dir, '/') ? strrchr(sg_root_dir, '/') : sg_root_dir, "/xs4-sound-groups-", 18))
		nftw(sg_root_dir, sg_rm, 16, FTW_DEPTH | FTW_PHYS);
}

/* ---- engine setup in a child ----------------------------------------------------- */

enum { SG_GBK, SG_SJIS, SG_FLAG_ONLY };
struct sg_opts {
	int ds;
	int version;
	int charset;
	bool dummy_audio;
	bool mainex_first;
	bool ini_mixers;
	const char *save;	/* a directory name under the data root; NULL: "save-<pid>" */
	bool kiwi_only;		/* stop after KiwiSoundEngine's init (MainEXFile's not run yet) */
};

static int sg_lib = -1;
static char sg_save_dir[PATH_MAX];
static void *sg_orig_fun[256];
static const char *sg_orig_name[256];
static int sg_nr_orig;

static void sg_link_one(struct static_library *lib, const char *name)
{
	int l = ain_get_library(ain, name);
	assert(l >= 0);
	libraries[l] = link_static_library(&ain->libraries[l], lib);
}

static void sg_setup(const struct sg_opts *o)
{
	extern struct static_library lib_KiwiSoundEngine, lib_MainEXFile;
	/* The library table's own entries, before any _PreLink replaces one. */
	sg_nr_orig = 0;
	for (int i = 0; lib_KiwiSoundEngine.functions[i].name && sg_nr_orig < 256; i++) {
		sg_orig_name[sg_nr_orig] = lib_KiwiSoundEngine.functions[i].name;
		sg_orig_fun[sg_nr_orig++] = lib_KiwiSoundEngine.functions[i].fun;
	}
	ain->version = o->version;
	if (o->charset == SG_GBK) {
		ain_is_gb18030 = true;
		sys4_set_string_charset(SYS4_CHARSET_GBK);
	} else if (o->charset == SG_FLAG_ONLY) {
		ain_is_gb18030 = true;
		sys4_set_string_charset(SYS4_CHARSET_SJIS);
	} else {
		ain_is_gb18030 = false;
		sys4_set_string_charset(SYS4_CHARSET_SJIS);
	}
	char save[PATH_MAX];
	if (o->save)
		snprintf(save, sizeof save, "%s/%s", sg_root_dir, o->save);
	else
		snprintf(save, sizeof save, "%s/save-%d", sg_root_dir, (int)getpid());
	mkdir(save, 0700);
	snprintf(sg_save_dir, sizeof sg_save_dir, "%s", save);
	config.game_dir = strdup(sg_ds_dir[o->ds]);
	config.ain_filename = strdup("sgprobe.ain");
	config.save_dir = strdup(sg_save_dir);
	config.ex_path = config.bgi_path = config.wai_path = config.fnl_path = NULL;
	config.default_volume = 100;
	config.mixer_nr_channels = 0;
	if (o->ini_mixers) {
		static char *names[] = { "Master", "BGM", "Voice", "VoiceSub" };
		static int volumes[] = { 100, 50, 25, 75 };
		config.mixer_nr_channels = 4;
		config.mixer_channels = names;
		config.mixer_volumes = volumes;
	}
	SDL_setenv("SDL_AUDIODRIVER", o->dummy_audio ? "dummy" : "sound-groups-no-device", 1);
	SDL_setenv("SDL_VIDEODRIVER", "sound-groups-no-video", 1);
	asset_manager_init();
	/* As init_libraries does: _PreLink, link, _PostLink, then _ModuleInit in
	 * the AIN's library order (KiwiSoundEngine before MainEXFile). */
	library_run(&lib_KiwiSoundEngine, "_PreLink");
	library_run(&lib_MainEXFile, "_PreLink");
	sg_link_one(&lib_KiwiSoundEngine, "KiwiSoundEngine");
	sg_link_one(&lib_MainEXFile, "MainEXFile");
	library_run(&lib_KiwiSoundEngine, "_PostLink");
	library_run(&lib_MainEXFile, "_PostLink");
	sg_lib = ain_get_library(ain, "KiwiSoundEngine");
	if (o->mainex_first) {
		library_run(&lib_MainEXFile, "_ModuleInit");
		library_run(&lib_KiwiSoundEngine, "_ModuleInit");
	} else {
		library_run(&lib_KiwiSoundEngine, "_ModuleInit");
		if (!o->kiwi_only)
			library_run(&lib_MainEXFile, "_ModuleInit");
	}
}

static const struct sg_opts sg_group_opts = { SG_DS_VALID, 14, SG_GBK, false, false, false, NULL, false };

/* A restart as system.Reset makes it: vm_free -> exit_libraries (every
 * library's _ModuleFini in the AIN's order), then vm_execute_ain again: the
 * VM state and the heap anew (system-reset's sr_restart_vm) and
 * init_libraries, that is _PreLink, link (done already: libraries is set),
 * _PostLink and _ModuleInit, here for the two modules in the AIN's order. */
static void sg_restart(void)
{
	extern struct static_library lib_KiwiSoundEngine, lib_MainEXFile;
	libraries_initialized = true;
	exit_libraries();
	libraries_initialized = false;
	sr_restart_vm();
	library_run(&lib_KiwiSoundEngine, "_PreLink");
	library_run(&lib_MainEXFile, "_PreLink");
	library_run(&lib_KiwiSoundEngine, "_PostLink");
	library_run(&lib_MainEXFile, "_PostLink");
	library_run(&lib_KiwiSoundEngine, "_ModuleInit");
	library_run(&lib_MainEXFile, "_ModuleInit");
}

/* ---- KiwiSoundEngine through the AIN declarations --------------------------------- */

static int sg_fno(const char *name)
{
	for (int i = 0; i < ain->libraries[sg_lib].nr_functions; i++)
		if (!strcmp(ain->libraries[sg_lib].functions[i].name, name))
			return i;
	printf("  FAIL: no KiwiSoundEngine.%s in the AIN\n", name);
	exit(1);
}

static union vm_value sg_call(const char *name)
{
	int f = sg_fno(name), sp = stack_ptr;
	struct ain_hll_function *d = &ain->libraries[sg_lib].functions[f];
	int slots = 0;
	for (int i = 0; i < d->nr_arguments; i++) {
		enum ain_data_type t = d->arguments[i].type.data;
		enum ain_data_type in = t == AIN_WRAP && d->arguments[i].type.array_type ? d->arguments[i].type.array_type->data : AIN_VOID;
		slots += t == AIN_WRAP && (in == AIN_INT || in == AIN_BOOL || in == AIN_FLOAT) ? 2 : 1;
	}
	hll_call(sg_lib, f, 0);
	int ret = d->return_type.data != AIN_VOID;
	SG_CHECK(stack_ptr == sp - slots + ret, "%s left the stack at %d, want %d", name, stack_ptr - sp, ret - slots);
	union vm_value r = { .i = 0 };
	if (ret)
		r = stack_pop();
	stack_ptr = sp - slots;
	return r;
}
static int sg_str(const char *s) { return heap_alloc_string(make_string(s, strlen(s))); }

static int sg_i0(const char *fn) { return sg_call(fn).i; }
static int sg_i1(const char *fn, int a) { stack_push(a); return sg_call(fn).i; }
static int sg_i2(const char *fn, int a, int b) { stack_push(a); stack_push(b); return sg_call(fn).i; }
static float sg_f1(const char *fn, int a) { stack_push(a); return sg_call(fn).f; }
static int sg_is(const char *fn, const char *s) { stack_push(sg_str(s)); return sg_call(fn).i; }
static void sg_vb(const char *fn, bool b) { stack_push((int)b); sg_call(fn); }

/* bool F(int, wrap<int|bool>): the value is written through the wrap. */
static bool sg_get_wrap(const char *fn, int n, int *out, int sentinel)
{
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 1);
	p->array.rank = 1;
	p->values[0].i = sentinel;
	int pg = heap_alloc_page(p);
	stack_push(n);
	stack_push(pg);
	stack_push(0);
	bool r = sg_call(fn).i;
	*out = heap[pg].page->values[0].i;
	heap_unref(pg);
	return r;
}
static int sg_vol(int g) { int v; return sg_get_wrap("GetMixerVolume", g, &v, -777) ? v : -1; }
static int sg_def(int g) { int v; return sg_get_wrap("GetMixerDefaultVolume", g, &v, -777) ? v : -1; }
/* Sentinel 0: a bool written as one byte must not read as set. */
static int sg_mute(int g) { int v; return sg_get_wrap("GetMixerMute", g, &v, 0) ? !!v : -1; }
static bool sg_setvol(int g, int v) { return sg_i2("SetMixerVolume", g, v); }
static bool sg_setmute(int g, bool m) { return sg_i2("SetMixerMute", g, m); }
static float sg_gv(int g) { return sg_f1("GetGroupVolume", g); }
static int sg_numof(void) { return sg_i0("GetMixerNumof"); }
static int sg_file_group(const char *name) { return sg_is("GetGroupNumFromFile", name); }

static bool sg_prepare(int id, const char *name, bool streaming)
{
	stack_push(id);
	stack_push(sg_str(name));
	stack_push(sg_str(""));
	stack_push((int)streaming);
	return sg_call("Prepare").i;
}
static bool sg_play_loop(int id, const char *name, int loops)
{
	return sg_prepare(id, name, false) && sg_i2("SetLoopCount", id, loops) && sg_i1("Play", id);
}

/* ---- samples ----------------------------------------------------------------------- */

#define SG_CHUNK 1024
static void sg_pull(float *buf, int frames)
{
	for (int off = 0; off < frames; off += SG_CHUNK)
		probe_audio_pull(buf + 2 * off, frames - off < SG_CHUNK ? frames - off : SG_CHUNK);
}
/* The mean left sample over the last 512 frames after 5 chunks; left and right agree. */
static float sg_level(void)
{
	static float buf[5 * SG_CHUNK * 2];
	sg_pull(buf, 5 * SG_CHUNK);
	double sum = 0;
	bool lr = true;
	for (int i = 5 * SG_CHUNK - 512; i < 5 * SG_CHUNK; i++) {
		sum += buf[2 * i];
		lr = lr && buf[2 * i] == buf[2 * i + 1];
	}
	SG_CHECK(lr, "left and right differ for a mono source");
	return sum / 512;
}

static bool sg_group_mode_ok(void)
{
	int n = sg_numof(), d3 = sg_def(3), d1 = sg_def(1);
	bool ok = n == SG_NR_GROUPS && d3 == 100 && d1 == 25;
	SG_CHECK(ok, "the group graph is not there: GetMixerNumof=%d (want %d), GetMixerDefaultVolume(1)=%d (want 25), (3)=%d (want 100)",
		 n, SG_NR_GROUPS, d1, d3);
	return ok;
}

/* The expected GetGroupVolume from the current integer volumes and mutes. */
static float sg_chain(const int *vol, const int *mute, int g)
{
	float gain = 1.0f;
	for (int k = g, guard = 0; k >= 0 && guard < 16; k = sg_parent[k], guard++)
		gain *= mute[k] ? 0.0f : vol[k] / 100.0f;
	return gain;
}

/* ---- SG0: the synthetic files read back with libsys4 -------------------------------- */

static void sg0_data(void)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/sgprobeEx.ex", sg_ds_dir[SG_DS_VALID]);
	struct ex *ex = ex_read_file(path);
	SG_CHECK(ex && ex->nr_blocks == 4, "main EX: %u blocks", ex ? ex->nr_blocks : 0);
	struct ex_table *t = ex ? ex_get_table(ex, SG_KEY_TABLE) : NULL;
	SG_CHECK(t && t->nr_rows == SG_NR_GROUPS && t->nr_fields == 3 && t->fields[0].is_index
		 && t->fields[2].type == EX_FLOAT && t->fields[1].has_value && t->fields[1].value.i == -1,
		 "main EX group table has the wrong shape");
	if (t && t->nr_rows == SG_NR_GROUPS)
		for (int g = 0; g < SG_NR_GROUPS; g++)
			SG_CHECK(t->rows[g][0].i == g && t->rows[g][1].i == sg_voice_col[g] && t->rows[g][2].f == sg_defaults_f[g],
				 "main EX group row %d", g);
	const char *arcs[2] = { "sgprobeSound.afa", "sgprobeVoice.afa" };
	const char *tms[2] = { "timemap_sound", "timemap_voice" };
	const int tm_pos[2] = { SG_SOUND_TM_POS, SG_VOICE_TM_POS };
	int counts[2] = { SG_COUNT(sg_sounds), SG_COUNT(sg_voices) }, rows[2] = { SG_COUNT(sg_sound_rows), SG_COUNT(sg_voice_rows) };
	const struct sg_sound *lists[2] = { sg_sounds, sg_voices };
	for (int a = 0; a < 2; a++) {
		snprintf(path, sizeof path, "%s/%s", sg_ds_dir[SG_DS_VALID], arcs[a]);
		int err = 0;
		struct afa_archive *ar = afa_open(path, 0, &err);
		SG_CHECK(ar && !ar->has_number && ar->nr_files == (uint32_t)counts[a] + 1, "%s: open %d files %u", arcs[a], err,
			 ar ? ar->nr_files : 0);
		if (!ar)
			continue;
		for (int i = 0; i < counts[a]; i++) {
			int no = -1, want = i < tm_pos[a] ? i : i + 1;
			SG_CHECK(archive_exists_by_basename(&ar->ar, lists[a][i].name, &no) && no == want, "%s: %s at %d, want %d",
				 arcs[a], lists[a][i].name, no, want);
		}
		int tno = -1;
		SG_CHECK(archive_exists_by_basename(&ar->ar, tms[a], &tno) && tno == tm_pos[a], "%s: %s at %d, want %d", arcs[a], tms[a],
			 tno, tm_pos[a]);
		struct archive_data *d = archive_get_by_basename(&ar->ar, tms[a]);
		SG_CHECK(d, "%s: no %s", arcs[a], tms[a]);
		if (d) {
			struct ex *tm = ex_read(d->data, d->size);
			struct ex_table *st = tm ? ex_get_table(tm, "SoundInfo") : NULL;
			SG_CHECK(st && st->nr_rows == (uint32_t)rows[a] && st->nr_fields == 4 && st->fields[0].is_index,
				 "%s: SoundInfo shape", tms[a]);
			SG_CHECK(tm && ex_get_int(tm, "Version", -1) == 1, "%s: Version", tms[a]);
			archive_free_data(d);
		}
	}
	printf("  SG0 synthetic EX, AFA and timemap files read back with libsys4\n");
}

/* ---- SG1: gate and lifecycle --------------------------------------------------------- */

static void sg1_expect_defaults(const char *when)
{
	SG_CHECK(sg_numof() == SG_NR_GROUPS, "%s: GetMixerNumof=%d, want %d (the table's row count, not 3 or 9)", when, sg_numof(), SG_NR_GROUPS);
	for (int g = 0; g < SG_NR_GROUPS; g++) {
		SG_CHECK(sg_def(g) == sg_defaults[g], "%s: GetMixerDefaultVolume(%d)=%d, want %d", when, g, sg_def(g), sg_defaults[g]);
		SG_CHECK(sg_vol(g) == sg_defaults[g], "%s: GetMixerVolume(%d)=%d, want %d", when, g, sg_vol(g), sg_defaults[g]);
		SG_CHECK(sg_mute(g) == 0, "%s: GetMixerMute(%d)=%d, want 0", when, g, sg_mute(g));
	}
}

static void sg1_gate(void)
{
	extern struct static_library lib_MainEXFile;
	struct sg_opts o = sg_group_opts;
	o.kiwi_only = true;
	sg_setup(&o);
	/* The graph is complete when the first audio init returns: the audio
	 * callback may run from then on, and the scripts' MainEXFile is later. */
	sg1_expect_defaults("right after KiwiSoundEngine's init (before MainEXFile's)");
	library_run(&lib_MainEXFile, "_ModuleInit");
	sg1_expect_defaults("Kiwi init before MainEXFile init, no audio device");
	/* A repeated module init keeps the state. */
	SG_CHECK(sg_setvol(2, 37), "SetMixerVolume(2, 37) failed");
	extern struct static_library lib_KiwiSoundEngine;
	for (int i = 0; i < 2; i++)
		library_run(&lib_KiwiSoundEngine, "_ModuleInit");
	SG_CHECK(sg_vol(2) == 37, "a repeated KiwiSoundEngine init changed group 2 to %d (want 37)", sg_vol(2));
	SG_CHECK(sg_numof() == SG_NR_GROUPS, "a repeated init changed the group count to %d", sg_numof());
	/* A restart keeps the current volume and the defaults; a playing sound
	 * is closed by the sprite/parts engines' fini after Kiwi's. */
	SG_CHECK(sg_play_loop(20, "sg_tone_a", 0), "cannot play before the restart");
	sg_restart();
	SG_CHECK(sg_vol(2) == 37, "the restart put group 2 back to %d (want 37: the restart keeps the volumes)", sg_vol(2));
	SG_CHECK(sg_def(2) == 53, "after the restart GetMixerDefaultVolume(2)=%d, want 53", sg_def(2));
	SG_CHECK(sg_numof() == SG_NR_GROUPS, "the restart changed the group count to %d", sg_numof());
	SG_CHECK(config.default_volume == 100, "config.default_volume changed to %d (it is global)", config.default_volume);
	printf("  SG1 gate, init order and restart checked\n");
}
static void sg1_mainex_first(void)
{
	struct sg_opts o = sg_group_opts;
	o.mainex_first = true;
	sg_setup(&o);
	sg1_expect_defaults("MainEXFile init before Kiwi init");
}
static void sg1_dummy_device(void)
{
	struct sg_opts o = sg_group_opts;
	o.dummy_audio = true;
	sg_setup(&o);
	sg1_expect_defaults("dummy audio device");
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

/* ---- SG2: tree and defaults ----------------------------------------------------------- */

static void sg2_tree(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS] = {0};
	memcpy(vol, sg_defaults, sizeof vol);
	for (int g = 0; g < SG_NR_GROUPS; g++)
		SG_CHECK(SG_NEAR(sg_gv(g), sg_chain(vol, mute, g)), "defaults: GetGroupVolume(%d)=%.4f, want %.4f", g, sg_gv(g), sg_chain(vol, mute, g));
	/* Distinct volumes make each parent visible: 7 hangs under 5 (its column
	 * is 0), 6 under 3 (-1), 2 under 0 although its column is 4. */
	static const int set[SG_NR_GROUPS] = { 90, 70, 60, 50, 40, 30, 20, 10 };
	for (int g = 0; g < SG_NR_GROUPS; g++) {
		SG_CHECK(sg_setvol(g, set[g]), "SetMixerVolume(%d) failed", g);
		vol[g] = set[g];
	}
	for (int g = 0; g < SG_NR_GROUPS; g++)
		SG_CHECK(SG_NEAR(sg_gv(g), sg_chain(vol, mute, g)), "GetGroupVolume(%d)=%.4f, want %.4f (parent %d)", g, sg_gv(g),
			 sg_chain(vol, mute, g), sg_parent[g]);
	SG_CHECK(SG_NEAR(sg_gv(0), 0.9f), "the root counts once: GetGroupVolume(0)=%.4f, want 0.9", sg_gv(0));
	/* Current and default are apart. */
	for (int g = 0; g < SG_NR_GROUPS; g++)
		SG_CHECK(sg_def(g) == sg_defaults[g], "SetMixerVolume changed the default of %d to %d", g, sg_def(g));
	/* Mute along the chain. */
	SG_CHECK(sg_setmute(3, true), "SetMixerMute(3) failed");
	mute[3] = 1;
	SG_CHECK(sg_gv(6) == 0.0f && sg_gv(3) == 0.0f, "muting 3 left GetGroupVolume(6)=%.4f (3)=%.4f", sg_gv(6), sg_gv(3));
	SG_CHECK(SG_NEAR(sg_gv(7), sg_chain(vol, mute, 7)), "muting 3 changed group 7 (parent 5): %.4f", sg_gv(7));
	SG_CHECK(sg_mute(3) == 1 && sg_mute(6) == 0, "GetMixerMute(3)=%d (6)=%d: a parent's mute is not the child's own", sg_mute(3), sg_mute(6));
	printf("  SG2 group tree and defaults checked\n");
}

/* ---- SG3: integers --------------------------------------------------------------------- */

static void sg3_integers(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	for (int v = 0; v <= 100; v++) {
		sg_setvol(2, v);
		int got = sg_vol(2);
		SG_CHECK(got == v, "SetMixerVolume(2, %d) reads back %d", v, got);
	}
	sg_setvol(4, 53);
	for (int i = 0; i < 60; i++)
		sg_setvol(4, sg_vol(4));
	SG_CHECK(sg_vol(4) == 53, "60 read/write rounds of 53 drifted to %d", sg_vol(4));
	sg_setvol(4, -1);
	SG_CHECK(sg_vol(4) == 0, "SetMixerVolume(4, -1) reads back %d, want 0", sg_vol(4));
	sg_setvol(4, 101);
	SG_CHECK(sg_vol(4) == 100, "SetMixerVolume(4, 101) reads back %d, want 100", sg_vol(4));
	int out;
	SG_CHECK(!sg_get_wrap("GetMixerVolume", SG_NR_GROUPS, &out, -777) && out == -777,
		 "GetMixerVolume(%d) (no such group) returned true or wrote %d", SG_NR_GROUPS, out);
	SG_CHECK(!sg_get_wrap("GetMixerVolume", -1, &out, -777) && out == -777, "GetMixerVolume(-1) returned true or wrote %d", out);
	SG_CHECK(!sg_setvol(SG_NR_GROUPS, 10) && !sg_setvol(-1, 10), "SetMixerVolume accepted a group that does not exist");
	for (int g = 0; g < SG_NR_GROUPS; g++)
		if (g != 2 && g != 4)
			SG_CHECK(sg_vol(g) == sg_defaults[g], "another group changed: %d is %d", g, sg_vol(g));
	printf("  SG3 integer volumes checked\n");
}

/* ---- SG4: real output ------------------------------------------------------------------- */

static void sg4_output(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS] = {0};
	memcpy(vol, sg_defaults, sizeof vol);
	const float a = 8192 / 32768.0f, b = 4096 / 32768.0f, r = 2048 / 32768.0f;
	SG_CHECK(sg_play_loop(20, "sg_tone_a", 0), "cannot play sg_tone_a");	/* group 2 */
	float got = sg_level(), want = a * sg_chain(vol, mute, 2);
	SG_CHECK(SG_NEAR(got, want), "group 2 alone: %.5f, want %.5f (sound x 0.53 x root 0.8)", got, want);
	SG_CHECK(SG_NEAR(got, a * sg_gv(2)), "output %.5f does not match GetGroupVolume(2)=%.4f", got, sg_gv(2));
	SG_CHECK(sg_play_loop(21, "10", 0), "cannot play voice 10");	/* group 7 under 5 */
	got = sg_level(); want = a * sg_chain(vol, mute, 2) + b * sg_chain(vol, mute, 7);
	SG_CHECK(SG_NEAR(got, want), "groups 2 + 7: %.5f, want %.5f", got, want);
	/* Changes reach what is already playing. */
	sg_setvol(0, 50); vol[0] = 50;
	got = sg_level(); want = a * sg_chain(vol, mute, 2) + b * sg_chain(vol, mute, 7);
	SG_CHECK(SG_NEAR(got, want), "root 50 while playing: %.5f, want %.5f", got, want);
	sg_setmute(5, true); mute[5] = 1;
	got = sg_level(); want = a * sg_chain(vol, mute, 2);
	SG_CHECK(SG_NEAR(got, want), "parent 5 muted: %.5f, want %.5f (group 7 silent, group 2 as before)", got, want);
	sg_setmute(5, false); mute[5] = 0;
	sg_setvol(5, 25); vol[5] = 25;
	got = sg_level(); want = a * sg_chain(vol, mute, 2) + b * sg_chain(vol, mute, 7);
	SG_CHECK(SG_NEAR(got, want), "parent 5 at 25: %.5f, want %.5f", got, want);
	sg_setmute(0, true); mute[0] = 1;
	got = sg_level();
	SG_CHECK(got == 0.0f, "root muted: %.5f, want 0", got);
	sg_setmute(0, false); mute[0] = 0;
	sg_i1("Stop", 20); sg_i1("Stop", 21);
	sg_level();
	/* A group-0 sound is scaled by the root once. */
	SG_CHECK(sg_play_loop(22, "sg_root", 0), "cannot play sg_root");
	got = sg_level(); want = r * vol[0] / 100.0f;
	SG_CHECK(SG_NEAR(got, want), "group 0: %.5f, want %.5f (the root once)", got, want);
	printf("  SG4 output through the group chain checked\n");
}

/* ---- SG5: which file is in which group --------------------------------------------------- */

static void sg5_identity(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	struct { const char *name; int group; } q[] = {
		{ "sg_tone_b", 1 }, { "sg_tone_a", 2 }, { "sg_root", 0 }, { "1", 6 }, { "10", 7 }, { "100", 3 }, { "2", 5 },
		{ "vx_alpha", 6 }, { "VX_ALPHA", 6 }, { SG_DB_UPPER, 6 }, { SG_DB_LOWER, 7 },
		{ "sg_orphan_s", 2 }, { "vx_orphan", 2 }, { "sg_no_such_sound", 2 },
		/* GBK lead bytes outside the SJIS range keep their trail byte */
		{ SG_GBK_UPPER, 6 }, { SG_GBK_LOWER, 7 },
		/* a row without a file counts: the query looks up the map only
		 * (0x4a45a0; inferred, the map's other sources were not traced) */
		{ "sg_ghost_s", 7 }, { "sg_ghost_v", 7 },
	};
	for (unsigned i = 0; i < SG_COUNT(q); i++) {
		int g = sg_file_group(q[i].name);
		SG_CHECK(g == q[i].group, "GetGroupNumFromFile(%s)=%d, want %d", gk_hex(q[i].name, strlen(q[i].name)), g, q[i].group);
	}
	/* The group of a prepared channel is its file's. Archive index 0 of
	 * both archives (sg_tone_b, 1) are different files in different groups. */
	struct { const char *name; bool streaming; int group; } p[] = {
		{ "sg_tone_b", false, 1 }, { "1", false, 6 }, { "10", true, 7 }, { "100", false, 3 }, { "sg_root", true, 0 },
		{ SG_DB_LOWER, false, 7 },
		/* no row: the Prepare argument decides (nonzero 1, zero 2) */
		{ "sg_orphan_s", true, 1 }, { "sg_orphan_s", false, 2 }, { "vx_orphan", true, 1 }, { "vx_orphan", false, 2 },
	};
	for (unsigned i = 0; i < SG_COUNT(p); i++) {
		int id = 30 + i;
		SG_CHECK(sg_prepare(id, p[i].name, p[i].streaming), "Prepare(%d, %s) failed", id, gk_hex(p[i].name, strlen(p[i].name)));
		int g = sg_i1("GetGroupNum", id);
		SG_CHECK(g == p[i].group, "Prepare(%s, bStreaming=%d) then GetGroupNum=%d, want %d",
			 gk_hex(p[i].name, strlen(p[i].name)), p[i].streaming, g, p[i].group);
	}
	/* A channel prepared again with another file takes that file's group. */
	sg_prepare(30, "2", false);
	SG_CHECK(sg_i1("GetGroupNum", 30) == 5, "re-prepared channel 30 kept group %d (want 5)", sg_i1("GetGroupNum", 30));
	printf("  SG5 file identity checked\n");
}

/* ---- SG6: missing and bad metadata --------------------------------------------------------- */

static void sg6_missing(int ds)
{
	struct sg_opts o = sg_group_opts;
	o.ds = ds;
	sg_setup(&o);
	sg_group_mode_ok();
	SG_CHECK(sg_file_group("1") == 2 && sg_file_group("10") == 2, "%s: voices without usable metadata: %d %d, want 2 2",
		 sg_ds_names[ds], sg_file_group("1"), sg_file_group("10"));
	SG_CHECK(sg_file_group("sg_tone_b") == 1 && sg_file_group("sg_root") == 0, "%s: the sound archive's metadata was lost: %d %d",
		 sg_ds_names[ds], sg_file_group("sg_tone_b"), sg_file_group("sg_root"));
	SG_CHECK(sg_prepare(40, "1", true) && sg_i1("GetGroupNum", 40) == 1, "%s: Prepare(1, bStreaming) group %d, want 1",
		 sg_ds_names[ds], sg_i1("GetGroupNum", 40));
}
static void sg6_metadata(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	SG_CHECK(!sg_prepare(41, "timemap_sound", false), "the timemap file was prepared as a sound");
	SG_CHECK(!sg_prepare(42, "timemap_voice", false), "the timemap file was prepared as a sound");
	printf("  SG6 timemap files are not sounds\n");
}
static void sg6_novoice(void) { sg6_missing(SG_DS_NOVOICETM); }
static void sg6_badvoice(void) { sg6_missing(SG_DS_BADVOICETM); }

/* ---- SG7: loop points -------------------------------------------------------------------- */

static int sg_frame_of(float v) { return (int)lrintf(v * 32768.0f) - 1; }

/* The frames a sound plays: 0.. then [begin, end) again and again
 * (loops == 0), or once: 0..last, then (tail) optionally on to the file's
 * end, then silence; never back. Earlier output is flushed first. */
static void sg7_sequence_rep(const char *name, int id, int loops, int n, int lb, int le, bool stereo, int last, bool tail, int rep)
{
	enum { FRAMES = 22 * SG_CHUNK };
	static float buf[FRAMES * 2];
	sg_pull(buf, 4 * SG_CHUNK);
	if (!sg_play_loop(id, name, loops)) {
		SG_CHECK(false, "%s: cannot play", name);
		return;
	}
	sg_pull(buf, FRAMES);
	sg_i1("Stop", id);
	sg_i1("Unprepare", id);
	int s = -1;
	for (int i = 0; i < FRAMES && s < 0; i++)
		if (fabsf(buf[2 * i]) > 0.5f / 32768)
			s = i;
	if (s < 0) {
		SG_CHECK(false, "%s: no sound in %d frames", name, FRAMES);
		return;
	}
	enum { PLAY, TAIL, END } phase = PLAY;
	int want = 0, laps = 0, played = 0, seen = 0;
	for (int i = s; i < FRAMES; i++) {
		float l = buf[2 * i], r = buf[2 * i + 1];
		int got = sg_frame_of(l), got_r = stereo ? sg_frame_of(-r) : sg_frame_of(r);
		bool silent = l == 0.0f && r == 0.0f;
		if (phase == TAIL && silent)
			phase = END;
		if (phase == END) {
			if (!silent) {
				SG_CHECK(false, "%s: output frame %d after the end is source frame %d, want silence", name, i - s, got);
				return;
			}
			continue;
		}
		if (got != want || got_r != want) {
			SG_CHECK(false, "%s: output frame %d is %s %d (right %d), want %d (loop %d..%d, lap %d)", name, i - s,
				 silent ? "silence, frame" : "source frame", silent ? -1 : got, got_r, want, lb, le, laps);
			return;
		}
		/* a file at half the output rate gives every frame twice */
		if (++seen < rep)
			continue;
		seen = 0;
		played++;
		want++;
		if (loops == 0 && want == le) {
			want = lb;
			laps++;
		} else if (loops != 0 && phase == PLAY && want > last) {
			phase = tail ? TAIL : END;
		}
		if (phase == TAIL && want >= n)
			phase = END;
	}
	if (loops == 0)
		SG_CHECK(laps >= 2, "%s: only %d laps in %d frames", name, laps, FRAMES - s);
	else
		SG_CHECK(phase != PLAY, "%s: played once only %d frames, want at least %d", name, played, last + 1);
}
static void sg7_sequence(const char *name, int id, int loops, int n, int lb, int le, bool stereo, int last, bool tail)
{
	sg7_sequence_rep(name, id, loops, n, lb, le, stereo, last, tail, 1);
}

static void sg7_loops(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	/* Unity gain on the chains used, so a sample names its frame. */
	for (int g = 0; g < SG_NR_GROUPS; g++)
		sg_setvol(g, 100);
	sg7_sequence("sg_ramp_m", 50, 0, 6000, 1000, 3000, false, 0, false);
	sg7_sequence("sg_ramp_s", 51, 0, 6000, 1000, 3000, true, 0, false);
	sg7_sequence("sg_short", 52, 0, 3000, 500, 600, false, 0, false);
	sg7_sequence("sg_edge_a", 53, 0, 5000, 0, 2048, false, 0, false);
	sg7_sequence("sg_edge_b", 54, 0, 5000, 0, 2049, false, 0, false);
	sg7_sequence("vx_loop", 55, 0, 4000, 200, 1200, false, 0, false);
	/* Played once: every frame, nothing cut at the end, no loop back. With
	 * an end before the file's end, the rest may play or not (unknown). */
	sg7_sequence("sg_once", 56, 1, 6000, 0, 6000, false, 5999, false);
	sg7_sequence("sg_ramp_m", 57, 1, 6000, 1000, 3000, false, 2999, true);
	/* Port strategy: unusable loop points loop the whole file. */
	sg7_sequence("sg_end0", 58, 0, 4000, 0, 4000, false, 0, false);
	sg7_sequence("sg_rev", 59, 0, 4000, 0, 4000, false, 0, false);
	sg7_sequence("sg_neg", 60, 0, 4000, 0, 4000, false, 0, false);
	/* Loops that end at the file's last frame or at its end (most of the
	 * game's own), and past it (port strategy: as at the end). */
	sg7_sequence("sg_tail_a", 61, 0, 6000, 1000, 5999, false, 0, false);
	sg7_sequence("sg_tail_b", 62, 0, 6000, 1000, 6000, false, 0, false);
	sg7_sequence("sg_tail_c", 63, 0, 6000, 1000, 6000, false, 0, false);
	/* 22.05 kHz: the loop goes back to the file's frame 500, every frame
	 * twice in the 44.1 kHz output. */
	sg7_sequence_rep("sg_slow", 64, 0, 3000, 500, 1500, false, 0, false, 2);
	printf("  SG7 loop points checked\n");
}

/* ---- SG8: focus ----------------------------------------------------------------------------- */

static void sg_focus_event(bool gained)
{
	SDL_Event e = { .type = SDL_WINDOWEVENT };
	e.window.event = gained ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
	SDL_PushEvent(&e);
	SDL_Delay(3);
	handle_events();
}

static void sg8_focus(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	SG_CHECK(!SDL_InitSubSystem(SDL_INIT_EVENTS), "SDL events: %s", SDL_GetError());
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS] = {0};
	memcpy(vol, sg_defaults, sizeof vol);
	const float a = 8192 / 32768.0f, full = a * sg_chain(vol, mute, 2);
	sg_setmute(4, true);
	SG_CHECK(sg_play_loop(20, "sg_tone_a", 0), "cannot play");
	/* GlobalFocus off: the game is silent while another window is active. */
	sg_vb("SetGlobalFocus", false);
	float got = sg_level();
	SG_CHECK(SG_NEAR(got, full), "focused, GlobalFocus off: %.5f, want %.5f", got, full);
	int pos0 = sg_i1("GetPos", 20);
	sg_focus_event(false);
	got = sg_level();
	SG_CHECK(got == 0.0f, "unfocused, GlobalFocus off: %.5f, want 0", got);
	SG_CHECK(sg_gv(2) == 0.0f, "unfocused, GlobalFocus off: GetGroupVolume(2)=%.4f, want 0", sg_gv(2));
	SG_CHECK(sg_vol(2) == 53 && sg_mute(2) == 0 && sg_mute(4) == 1 && sg_mute(0) == 0 && sg_vol(0) == 80,
		 "focus touched the user's values: vol %d mute %d mute4 %d root mute %d root vol %d",
		 sg_vol(2), sg_mute(2), sg_mute(4), sg_mute(0), sg_vol(0));
	SG_CHECK(sg_i1("GetPos", 20) > pos0, "the muted sound did not go on playing (pos %d -> %d)", pos0, sg_i1("GetPos", 20));
	sg_focus_event(true);
	got = sg_level();
	SG_CHECK(SG_NEAR(got, full), "focus back: %.5f, want %.5f", got, full);
	SG_CHECK(sg_vol(2) == 53 && sg_mute(4) == 1, "focus back reset the user's values: vol %d mute4 %d", sg_vol(2), sg_mute(4));
	/* GlobalFocus on: unfocused keeps playing. */
	sg_vb("SetGlobalFocus", true);
	sg_focus_event(false);
	got = sg_level();
	SG_CHECK(SG_NEAR(got, full), "unfocused, GlobalFocus on: %.5f, want %.5f", got, full);
	SG_CHECK(SG_NEAR(sg_gv(2), sg_chain(vol, mute, 2)), "unfocused, GlobalFocus on: GetGroupVolume(2)=%.4f", sg_gv(2));
	/* Turned off while unfocused: silent at once. */
	sg_vb("SetGlobalFocus", false);
	got = sg_level();
	SG_CHECK(got == 0.0f, "GlobalFocus turned off while unfocused: %.5f, want 0", got);
	/* A fade goes on while muted. */
	stack_push(20); stack_push(200); stack_push(0.0f); stack_push(0); stack_push(0);
	SG_CHECK(sg_call("Fade").i, "Fade failed");
	sg_level(); sg_level(); sg_level();
	sg_focus_event(true);
	got = sg_level();
	SG_CHECK(got == 0.0f && !sg_i1("IsFade", 20), "a 200 ms fade to 0 did not finish while muted: %.5f, fading %d", got, sg_i1("IsFade", 20));
	printf("  SG8 focus checked\n");
}

/* ---- SG9: Volume.sav ------------------------------------------------------------------------- */

static int sg_read_file(const char *dir, const char *name, uint8_t *buf, int cap)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	int n = fread(buf, 1, cap, f);
	fclose(f);
	return n;
}
static void sg_write_file(const char *dir, const char *name, const uint8_t *b, int n)
{
	char path[PATH_MAX];
	mkdir(dir, 0700);
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "wb");
	assert(f && fwrite(b, 1, n, f) == (size_t)n);
	fclose(f);
}
/* Entries of the save folder other than Volume.sav (a temporary file left). */
static int sg_others(const char *dir)
{
	DIR *d = opendir(dir);
	int n = 0;
	struct dirent *e;
	while (d && (e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && strcmp(e->d_name, "Volume.sav")) {
			printf("  (left in the save folder: %s)\n", e->d_name);
			n++;
		}
	if (d)
		closedir(d);
	return n;
}

static const uint8_t sg9_expected[] = { 0, SG_NR_GROUPS, 80, 0, 0, 47, 0, 0, 53, 0, 0, 100, 0, 0, 80, 1, 0, 80, 0, 0, 80, 0, 0, 0, 0, 0 };

/* A run that changes three groups and quits the engine's way (vm_exit runs
 * every library's _ModuleFini; this process ends there). */
static void sg9_write_and_exit(void)
{
	struct sg_opts o = sg_group_opts;
	o.save = "save-sg9";
	sg_setup(&o);
	sg_group_mode_ok();
	sg_setvol(1, 47);
	sg_setmute(4, true);
	sg_setvol(7, 0);
	for (int i = 0; i < 30; i++) {	/* the settings page writes every frame */
		sg_setvol(1, sg_vol(1));
		sg_setmute(4, sg_mute(4));
	}
	uint8_t buf[256];
	SG_CHECK(sg_read_file(sg_save_dir, "Volume.sav", buf, sizeof buf) < 0, "Volume.sav was written before the engine quit");
	/* Still playing at the exit: the parts engine's fini (after Kiwi's in
	 * the AIN's order) closes it, so the mixers must still be there. */
	SG_CHECK(sg_play_loop(20, "sg_tone_a", 0), "cannot play");
	float lv[2 * SG_CHUNK];
	sg_pull(lv, SG_CHUNK);
	fflush(NULL);
	if (sg_failures)
		_exit(1);
	libraries_initialized = true;
	vm_exit(0);
}
static void sg9_check_written(void)
{
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/save-sg9", sg_root_dir);
	uint8_t buf[256];
	int n = sg_read_file(dir, "Volume.sav", buf, sizeof buf);
	SG_CHECK(n == (int)sizeof sg9_expected, "Volume.sav after quitting: %d bytes, want %zu", n, sizeof sg9_expected);
	if (n == (int)sizeof sg9_expected)
		for (int i = 0; i < n; i++)
			SG_CHECK(buf[i] == sg9_expected[i], "Volume.sav byte %d is %d, want %d", i, buf[i], sg9_expected[i]);
	SG_CHECK(sg_others(dir) == 0, "the save folder has more than Volume.sav");
}
static void sg9_reopen(void)
{
	struct sg_opts o = sg_group_opts;
	o.save = "save-sg9";
	sg_setup(&o);
	sg_group_mode_ok();
	int want[SG_NR_GROUPS];
	memcpy(want, sg_defaults, sizeof want);
	want[1] = 47; want[7] = 0;
	for (int g = 0; g < SG_NR_GROUPS; g++) {
		SG_CHECK(sg_vol(g) == want[g], "reopened: group %d volume %d, want %d", g, sg_vol(g), want[g]);
		SG_CHECK(sg_mute(g) == (g == 4), "reopened: group %d mute %d", g, sg_mute(g));
		SG_CHECK(sg_def(g) == sg_defaults[g], "reopened: group %d default %d, want the EX's %d", g, sg_def(g), sg_defaults[g]);
	}
}

/* Damaged or unusual files: never a crash; what each reaches applies. */
struct sg9_file { const char *name; uint8_t bytes[240]; int n; int vol[SG_NR_GROUPS]; int alt[SG_NR_GROUPS]; int mute0; };
static struct sg9_file sg9_files[] = {
	{ "one byte", { 0 }, 1, { 80, 25, 53, 100, 80, 80, 80, 80 }, { -1, -1, -1, -1, -1, -1, -1, -1 }, 0 },
	{ "count 0", { 0, 0 }, 2, { 80, 25, 53, 100, 80, 80, 80, 80 }, { -1, -1, -1, -1, -1, -1, -1, -1 }, 0 },
	{ "count 3", { 0, 3, 10, 1, 0, 20, 0, 0, 30, 0, 0 }, 11, { 10, 20, 30, 100, 80, 80, 80, 80 }, { -1, -1, -1, -1, -1, -1, -1, -1 }, 1 },
	{ "reserved bytes set, trailing bytes", { 7, 2, 40, 1, 9, 41, 0, 9, 1, 2, 3, 4, 5 }, 13, { 40, 41, 53, 100, 80, 80, 80, 80 },
	  { -1, -1, -1, -1, -1, -1, -1, -1 }, 1 },
	{ "volume 150", { 0, 1, 150, 0, 0 }, 5, { 100, 25, 53, 100, 80, 80, 80, 80 }, { -1, -1, -1, -1, -1, -1, -1, -1 }, 0 },
	{ "cut in a triplet", { 0, 8, 33, 0, 0, 34, 0 }, 7, { 33, 34, 53, 100, 80, 80, 80, 80 }, { 80, 25, -1, -1, -1, -1, -1, -1 }, 0 },
	{ "count 70", { 0 }, 0, { 30, 30, 30, 30, 30, 30, 30, 30 }, { -1, -1, -1, -1, -1, -1, -1, -1 }, 0 },
};
static int sg9_which;
static void sg9_damaged(void)
{
	struct sg9_file *f = &sg9_files[sg9_which];
	char save[64], dir[PATH_MAX];
	snprintf(save, sizeof save, "save-sg9-file%d", sg9_which);
	snprintf(dir, sizeof dir, "%s/%s", sg_root_dir, save);
	if (!f->n) {	/* count 70: more entries than groups */
		f->bytes[0] = 0; f->bytes[1] = 70;
		for (int i = 0; i < 70; i++) { f->bytes[2 + 3 * i] = 30; f->bytes[3 + 3 * i] = 0; f->bytes[4 + 3 * i] = 0; }
		f->n = 2 + 3 * 70;
	}
	sg_write_file(dir, "Volume.sav", f->bytes, f->n);
	struct sg_opts o = sg_group_opts;
	o.save = save;
	sg_setup(&o);
	sg_group_mode_ok();
	for (int g = 0; g < SG_NR_GROUPS; g++) {
		int v = sg_vol(g);
		SG_CHECK(v == f->vol[g] || v == f->alt[g], "Volume.sav %s: group %d is %d, want %d%s", f->name, g, v, f->vol[g],
			 f->alt[g] >= 0 ? " (or unchanged)" : "");
		SG_CHECK(sg_def(g) == sg_defaults[g], "Volume.sav %s changed the default of %d", f->name, g);
	}
	SG_CHECK(sg_mute(0) == f->mute0, "Volume.sav %s: group 0 mute %d, want %d", f->name, sg_mute(0), f->mute0);
}

/* A restart writes the file and keeps the volumes; quitting afterwards writes the latest. */
static void sg9_restart_then_exit(void)
{
	struct sg_opts o = sg_group_opts;
	o.save = "save-sg9-restart";
	sg_setup(&o);
	sg_group_mode_ok();
	sg_setvol(2, 61);
	sg_restart();
	SG_CHECK(sg_vol(2) == 61, "after the restart group 2 is %d, want 61", sg_vol(2));
	uint8_t buf[256];
	int n = sg_read_file(sg_save_dir, "Volume.sav", buf, sizeof buf);
	SG_CHECK(n == 2 + 3 * SG_NR_GROUPS && buf[2 + 3 * 2] == 61, "the restart did not write Volume.sav (%d bytes, group 2 %d)", n,
		 n > 8 ? buf[8] : -1);
	sg_restart();
	sg_setvol(2, 62);
	fflush(NULL);
	if (sg_failures)
		_exit(1);
	libraries_initialized = true;
	vm_exit(0);
}
static void sg9_check_restart(void)
{
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/save-sg9-restart", sg_root_dir);
	uint8_t buf[256];
	int n = sg_read_file(dir, "Volume.sav", buf, sizeof buf);
	SG_CHECK(n == 2 + 3 * SG_NR_GROUPS && buf[8] == 62, "quitting after two restarts: %d bytes, group 2 %d (want 62)", n, n > 8 ? buf[8] : -1);
}

/* ---- SG10: legacy and declaration shapes ------------------------------------------------------ */

/* What the engine without groups answers (the base revision's behavior):
 * three mixers Music/Sound/Master at 100, the default always 100, the file
 * group 0, sounds in mixer 1, 53 read back as 52, and no Volume.sav. */
static void sg10_legacy_fingerprint(const char *what, bool ini)
{
	int n = sg_numof();
	SG_CHECK(n == (ini ? 4 : 3), "%s: GetMixerNumof=%d, want %d", what, n, ini ? 4 : 3);
	static const int ini_vol[4] = { 100, 50, 25, 75 };
	for (int g = -1; g <= 4; g++) {
		int out;
		bool ok = sg_get_wrap("GetMixerVolume", g, &out, -777);
		bool exists = g >= 0 && g < n;
		/* the INI's volume (an engine with more mixers must not index past it) */
		int iv = ini && g >= 0 && g < 4 ? ini_vol[g] : 100;
		SG_CHECK(ok == exists && (exists ? out == iv : out == -777), "%s: GetMixerVolume(%d) %d/%d", what, g, ok, out);
		ok = sg_get_wrap("GetMixerDefaultVolume", g, &out, -777);
		SG_CHECK(ok && out == 100, "%s: GetMixerDefaultVolume(%d) %d/%d, want 1/100", what, g, ok, out);
		ok = sg_get_wrap("GetMixerMute", g, &out, -777);
		SG_CHECK(ok == exists && (exists ? out == 0 : out == -777), "%s: GetMixerMute(%d) %d/%d", what, g, ok, out);
		float gv = sg_gv(g);
		SG_CHECK(SG_NEAR(gv, exists && ini ? iv / 100.0f : 1.0f), "%s: GetGroupVolume(%d)=%.4f", what, g, gv);
	}
	SG_CHECK(sg_file_group("sg_tone_a") == 0 && sg_file_group("1") == 0 && sg_file_group("sg_no_such_sound") == 0,
		 "%s: GetGroupNumFromFile %d/%d/%d, want 0/0/0", what, sg_file_group("sg_tone_a"), sg_file_group("1"), sg_file_group("sg_no_such_sound"));
	SG_CHECK(sg_prepare(20, "sg_tone_a", true) && sg_i1("GetGroupNum", 20) == 0, "%s: GetGroupNum after Prepare %d", what, sg_i1("GetGroupNum", 20));
	SG_CHECK(sg_i2("SetLoopCount", 20, 0) && sg_i1("Play", 20), "%s: cannot play", what);
	float got = sg_level(), want = 0.25f * (ini ? 0.5f : 1.0f);
	SG_CHECK(SG_NEAR(got, want), "%s: a sound plays at %.5f, want %.5f (mixer 1)", what, got, want);
	sg_setvol(1, 53);
	SG_CHECK(sg_vol(1) == 52, "%s: legacy 53 reads back %d (the old float getter gives 52; unchanged)", what, sg_vol(1));
	SG_CHECK(config.default_volume == 100, "%s: config.default_volume %d", what, config.default_volume);
}
static int sg10_which;
static struct { const char *what; struct sg_opts o; } sg10_cases[] = {
	{ "v13, valid table", { SG_DS_VALID, 13, SG_GBK, false, false, false, NULL } },
	{ "v15, valid table", { SG_DS_VALID, 15, SG_GBK, false, false, false, NULL } },
	{ "v14 SJIS, valid table", { SG_DS_VALID, 14, SG_SJIS, false, false, false, NULL } },
	{ "v14 GB18030 flag with the SJIS rule", { SG_DS_VALID, 14, SG_FLAG_ONLY, false, false, false, NULL } },
	{ "v14 GBK, no table", { SG_DS_NOTABLE, 14, SG_GBK, false, false, false, NULL } },
	{ "v14 GBK, int default column", { SG_DS_INTDEF, 14, SG_GBK, false, false, false, NULL } },
	{ "v14 GBK, no indexed column", { SG_DS_NOINDEX, 14, SG_GBK, false, false, false, NULL } },
	{ "v14 GBK, no rows", { SG_DS_NOROWS, 14, SG_GBK, false, false, false, NULL } },
	{ "v14 GBK, list block of that name", { SG_DS_LISTBLOCK, 14, SG_GBK, false, false, false, NULL } },
	{ "v13 with INI mixers", { SG_DS_VALID, 13, SG_GBK, false, false, true, NULL } },
};
static void sg10_legacy(void)
{
	sg_setup(&sg10_cases[sg10_which].o);
	sg10_legacy_fingerprint(sg10_cases[sg10_which].what, sg10_cases[sg10_which].o.ini_mixers);
	fflush(NULL);
	if (sg_failures)
		_exit(1);
	libraries_initialized = true;
	vm_exit(0);
}
/* v13 with a BGI and a WAI: the BGM loops at the BGI's points (the end
 * exclusive), the WAI gives a sound's group and mixer; as on the engine
 * before the change. Quits through vm_exit (no Volume.sav, see below). */
static void sg10_legacy_meta(void)
{
	struct sg_opts o = { SG_DS_LEGACYMETA, 13, SG_GBK, false, false, false, "save-legacy-meta" };
	sg_setup(&o);
	SG_CHECK(sg_numof() == 3, "v13 with BGI/WAI: GetMixerNumof=%d, want 3", sg_numof());
	/* The legacy query looks a voice's data number up in the sound WAI
	 * (voice "1" is data number 1, which the WAI puts in 1): kept as it is. */
	SG_CHECK(sg_file_group("sg_tone_b") == 1 && sg_file_group("sg_tone_a") == 0 && sg_file_group("1") == 1,
		 "v13 with BGI/WAI: GetGroupNumFromFile %d/%d/%d, want the WAI's 1/0/1",
		 sg_file_group("sg_tone_b"), sg_file_group("sg_tone_a"), sg_file_group("1"));
	/* the WAI puts sg_tone_a in mixer 0, not 1 */
	sg_setvol(0, 50);
	sg_setvol(1, 20);
	SG_CHECK(sg_play_loop(20, "sg_tone_a", 0), "v13 with BGI/WAI: cannot play sg_tone_a");
	float got = sg_level(), want = 0.25f * 0.5f;
	SG_CHECK(SG_NEAR(got, want), "v13 with BGI/WAI: a WAI mixer-0 sound plays at %.5f, want %.5f (mixer 0 at 50)", got, want);
	sg_i1("Stop", 20);
	sg_i1("Unprepare", 20);
	sg_setvol(0, 100);
	sg_setvol(1, 100);
	sg7_sequence(SG_LEGACY_BGM, 21, 0, 6000, 1000, 3000, false, 0, false);
	printf("  SG10 v13 BGI loop and WAI groups checked\n");
	fflush(NULL);
	if (sg_failures)
		_exit(1);
	libraries_initialized = true;
	vm_exit(0);
}

static void sg10_check_legacy_files(void)
{
	/* A legacy run quits without a Volume.sav (sg10_legacy's save dirs). */
	DIR *d = opendir(sg_root_dir);
	struct dirent *e;
	int found = 0;
	while (d && (e = readdir(d))) {
		if (strncmp(e->d_name, "save-legacy", 11))
			continue;
		char dir[PATH_MAX];
		uint8_t buf[8];
		snprintf(dir, sizeof dir, "%s/%s", sg_root_dir, e->d_name);
		if (sg_read_file(dir, "Volume.sav", buf, sizeof buf) >= 0)
			found++;
	}
	if (d)
		closedir(d);
	SG_CHECK(found == 0, "%d legacy run(s) wrote a Volume.sav", found);
}

/* wrap<int>, wrap<bool>, wrap<string> through ffi; unknown shapes keep the
 * library table's function. */
static void *sg_orig(const char *name)
{
	for (int i = 0; i < sg_nr_orig; i++)
		if (!strcmp(sg_orig_name[i], name))
			return sg_orig_fun[i];
	return NULL;
}
static void sg10_shapes(void)
{
	extern struct static_library lib_KiwiSoundEngine;
	sg_setup(&sg_group_opts);
	int out;
	SG_CHECK(sg_get_wrap("GetMixerVolume", 1, &out, -777) ? out >= 0 && out <= 100 : out == -777, "wrap<int> out %d", out);
	SG_CHECK(sg_get_wrap("GetMixerMute", 1, &out, -777) ? out == 0 || out == 1 : out == -777, "wrap<bool> out %d", out);
	int name = sg_str("unchanged");
	stack_push(1);
	stack_push(name);
	bool r = sg_call("GetMixerName").i;
	SG_CHECK(heap_index_valid(name) && heap[name].type == VM_STRING && heap[name].s && (r || !strcmp(heap[name].s->text, "unchanged")),
		 "GetMixerName through wrap<string>: ret %d, slot %s", r, heap[name].s ? heap[name].s->text : "(none)");
	heap_unref(name);
	/* Declarations of an unknown shape: copies of the AIN's with one type changed. */
	struct ain_library *kiwi = &ain->libraries[sg_lib];
	static struct ain_type t_int = { .data = AIN_INT, .struc = -1 }, t_float = { .data = AIN_FLOAT, .struc = -1 };
	struct { const char *fn; int arg; int how; } u[] = {
		{ "GetMixerName", 1, 0 },	/* wrap<int> where the AIN has wrap<string> */
		{ "GetMixerVolume", 1, 1 },	/* wrap<float> */
		{ "GetGroupVolume", -1, 2 },	/* returns int */
		{ "GetMixerDefaultVolume", -1, 3 },	/* one argument fewer */
		{ "SetMixerVolume", 1, 4 },	/* float volume */
	};
	for (unsigned i = 0; i < SG_COUNT(u); i++) {
		int f = sg_fno(u[i].fn);
		struct ain_hll_function decl = kiwi->functions[f];
		struct ain_hll_argument args[8];
		memcpy(args, decl.arguments, sizeof(args[0]) * decl.nr_arguments);
		decl.arguments = args;
		if (u[i].how == 0)
			args[1].type.array_type = &t_int;
		else if (u[i].how == 1)
			args[1].type.array_type = &t_float;
		else if (u[i].how == 2)
			decl.return_type = t_int;
		else if (u[i].how == 3)
			decl.nr_arguments--;
		else
			args[1].type = t_float;
		struct ain_library fake = { .name = kiwi->name, .nr_functions = 1, .functions = &decl };
		struct hll_function *linked = link_static_library(&fake, &lib_KiwiSoundEngine);
		SG_CHECK(linked[0].fun == sg_orig(u[i].fn), "an unknown shape of %s is not bound to the library table's function", u[i].fn);
	}
	printf("  SG10 declaration shapes checked\n");
}

/* ---- SG11: the audio thread ---------------------------------------------------------------------- */

static void sg11_thread(void)
{
	struct sg_opts o = sg_group_opts;
	o.dummy_audio = true;
	sg_setup(&o);
	bool groups = sg_group_mode_ok();
	SG_CHECK(SDL_WasInit(SDL_INIT_AUDIO), "the dummy audio device did not start");
	SG_CHECK(sg_play_loop(20, "sg_tone_a", 0) && sg_play_loop(21, "10", 0) && sg_play_loop(22, "sg_short", 0), "cannot play");
	int pos0 = sg_i1("GetPos", 20);
	srand(7);
	for (int i = 0; i < 400; i++) {
		int g = rand() % SG_NR_GROUPS;
		sg_setvol(g, rand() % 101);
		sg_setmute(g, rand() & 1);
		if (i % 7 == 0)
			sg_vb("SetGlobalFocus", i & 8);
		if (i % 11 == 0) {
			sg_i1("Unprepare", 21);
			sg_play_loop(21, i & 16 ? "1" : "10", 0);
		}
		if (i % 13 == 0)
			sg_gv(g);
		if (i == 200)
			sg_restart();
		if (i == 201)
			SG_CHECK(sg_play_loop(20, "sg_tone_a", 0), "cannot play after the restart");
		SDL_Delay(1);
	}
	SDL_Delay(60);
	int pos1 = sg_i1("GetPos", 20);
	SG_CHECK(pos1 > 0, "the audio thread did not consume the channel (pos %d, before %d)", pos1, pos0);
	for (int id = 20; id <= 22; id++)
		sg_i1("Unprepare", id);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	printf("  SG11 %d group changes against the audio thread (groups %s)\n", 400, groups ? "on" : "off");
}

/* ---- SG12: what the scripts build on ---------------------------------------------------------------- */

static void sg12_scripts(void)
{
	sg_setup(&sg_group_opts);
	sg_group_mode_ok();
	/* The game's own filter for a background sound list
	 * (sound::detail::IsExistSoundList, type 2 = sound effects): files of the
	 * list's group stay. Before the groups every file was group 0, and the
	 * list came out empty. */
	int fno = ain_get_function(ain, "sound::detail::IsExistSoundList");
	SG_CHECK(fno >= 0, "no sound::detail::IsExistSoundList in the AIN");
	if (fno >= 0) {
		/* group 2, group 1, group 2, no such file, group 2 */
		const char *names[] = { "sg_tone_a", "sg_tone_b", "sg_ramp_m", "sg_no_such_sound", "sg_short" };
		struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, SG_COUNT(names));
		a->array.rank = 1;
		a->array.struct_type = -1;
		for (int i = 0; i < SG_COUNT(names); i++)
			a->values[i].i = sg_str(names[i]);
		int list = heap_alloc_page(a);
		int sp = stack_ptr;
		stack_push(2);
		stack_push(list);
		vm_call(fno, -1);
		int ret = stack_ptr > sp ? stack_pop().i : -1;
		stack_ptr = sp;
		struct page *after = heap[list].page;
		int n = after ? after->nr_vars : 0;	/* an emptied array has no page */
		bool same = n == 3;
		for (int i = 0; same && i < 3; i++) {
			int slot = after->values[i].i;
			const char *want = i == 0 ? "sg_tone_a" : i == 1 ? "sg_ramp_m" : "sg_short";
			same = heap_index_valid(slot) && heap[slot].s && !strcmp(heap[slot].s->text, want);
		}
		/* the function's result says whether it met an empty name */
		SG_CHECK(same && ret == 0, "the group-2 list has %d of 5 files left, want the 3 of group 2 (returned %d, want 0)", n, ret);
		heap_unref(list);
	}
	/* Muting the voice group silences the voices under it (6 under 3), not
	 * the ones under 5. */
	SG_CHECK(sg_play_loop(20, "1", 0) && sg_play_loop(21, "10", 0), "cannot play the voices");
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS] = {0};
	memcpy(vol, sg_defaults, sizeof vol);
	const float a6 = 8192 / 32768.0f, a7 = 4096 / 32768.0f;
	sg_setmute(3, true); mute[3] = 1;
	float got = sg_level(), want = a7 * sg_chain(vol, mute, 7);
	SG_CHECK(SG_NEAR(got, want), "voice group 3 muted: %.5f, want %.5f (6 silent, 7 plays)", got, want);
	SG_CHECK(sg_mute(3) == 1, "GetMixerMute(3)=%d", sg_mute(3));
	sg_setmute(3, false); mute[3] = 0;
	got = sg_level(); want = a6 * sg_chain(vol, mute, 6) + a7 * sg_chain(vol, mute, 7);
	SG_CHECK(SG_NEAR(got, want), "voice group 3 unmuted: %.5f, want %.5f", got, want);
	/* The BGM cap reacts to the voice groups of the files (3, 6, 7, 8). */
	SG_CHECK(sg_file_group("1") == 6 && sg_file_group("10") == 7 && sg_file_group("sg_tone_b") == 1,
		 "file groups for the BGM cap: %d %d %d", sg_file_group("1"), sg_file_group("10"), sg_file_group("sg_tone_b"));
	printf("  SG12 script-facing groups checked\n");
}

/* ---- the mode ---------------------------------------------------------------------------------------- */

/* Run fn in a child; returns 0 when it ended with 0 (every check passed or it
 * quit through vm_exit(0) after passing them). */
static int sg_fork(void (*fn)(void))
{
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0) {
		alarm(90);
		sg_failures = sg_printed = 0;
		fn();
		fflush(NULL);
		if (sg_failures > 14)
			printf("  ... %d failed checks in all\n", sg_failures);
		fflush(NULL);
		_exit(sg_failures ? 1 : 0);
	}
	int st = 0;
	pid_t w;
	do w = waitpid(pid, &st, 0); while (w < 0 && errno == EINTR);
	if (w == pid && WIFSIGNALED(st))
		printf("  FAIL: the child died with signal %d\n", WTERMSIG(st));
	return !(w == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

static void sg_begin(const char *id, const char *what)
{
	printf("-- %s %s\n", id, what);
	fflush(stdout);
}

static int sg_report(const char *id, const char *what, int failed)
{
	printf("%s %s %s\n", id, failed ? "FAIL:" : "ok", what);
	fflush(stdout);
	return !!failed;
}

static void sg9_check_written_child(void) { sg9_check_written(); }
static void sg9_check_restart_child(void) { sg9_check_restart(); }

static int test_sound_groups(void)
{
	if (!sg_make_data())
		return 1;
	int failed = 0, f;
	sg_begin("SG0", "synthetic data reads back");
	failed += sg_report("SG0", "synthetic data reads back", sg_fork(sg0_data));
	sg_begin("SG1", "gate, init order, repeated init, restart");
	f = sg_fork(sg1_gate) + sg_fork(sg1_mainex_first) + sg_fork(sg1_dummy_device);
	failed += sg_report("SG1", "gate, init order, repeated init, restart", f);
	sg_begin("SG2", "group tree, parents, defaults");
	failed += sg_report("SG2", "group tree, parents, defaults", sg_fork(sg2_tree));
	sg_begin("SG3", "integer volumes, clamp, bad groups");
	failed += sg_report("SG3", "integer volumes, clamp, bad groups", sg_fork(sg3_integers));
	sg_begin("SG4", "output through the group chain");
	failed += sg_report("SG4", "output through the group chain", sg_fork(sg4_output));
	sg_begin("SG5", "file identity and groups");
	failed += sg_report("SG5", "file identity and groups", sg_fork(sg5_identity));
	sg_begin("SG6", "missing and bad metadata");
	f = sg_fork(sg6_metadata) + sg_fork(sg6_novoice) + sg_fork(sg6_badvoice);
	failed += sg_report("SG6", "missing and bad metadata", f);
	sg_begin("SG7", "loop points and play-once");
	failed += sg_report("SG7", "loop points and play-once", sg_fork(sg7_loops));
	sg_begin("SG8", "focus mute");
	failed += sg_report("SG8", "focus mute", sg_fork(sg8_focus));
	sg_begin("SG9", "Volume.sav, restart and quit");
	f = sg_fork(sg9_write_and_exit);
	f += sg_fork(sg9_check_written_child);
	f += sg_fork(sg9_reopen);
	for (sg9_which = 0; sg9_which < (int)SG_COUNT(sg9_files); sg9_which++)
		f += sg_fork(sg9_damaged);
	f += sg_fork(sg9_restart_then_exit);
	f += sg_fork(sg9_check_restart_child);
	failed += sg_report("SG9", "Volume.sav, restart and quit", f);
	sg_begin("SG10", "legacy kept (v13, v15, SJIS, flag only, no/odd table, INI), shapes");
	f = 0;
	for (sg10_which = 0; sg10_which < (int)SG_COUNT(sg10_cases); sg10_which++) {
		/* each legacy run quits into its own save dir */
		static char save[32];
		snprintf(save, sizeof save, "save-legacy%d", sg10_which);
		sg10_cases[sg10_which].o.save = save;
		int r = sg_fork(sg10_legacy);
		if (r)
			printf("  SG10 legacy case failed: %s\n", sg10_cases[sg10_which].what);
		f += r;
	}
	{
		int r = sg_fork(sg10_legacy_meta);
		if (r)
			printf("  SG10 legacy case failed: v13 with BGI and WAI\n");
		f += r;
	}
	f += sg_fork(sg10_check_legacy_files);
	f += sg_fork(sg10_shapes);
	failed += sg_report("SG10", "legacy kept (v13, v15, SJIS, flag only, no/odd table, INI), shapes", f);
	sg_begin("SG11", "audio thread under group changes and a restart");
	failed += sg_report("SG11", "audio thread under group changes and a restart", sg_fork(sg11_thread));
	sg_begin("SG12", "script list filter, voice mute, cap groups");
	failed += sg_report("SG12", "script list filter, voice mute, cap groups", sg_fork(sg12_scripts));
	sg_remove_data();
	printf("SOUND-GROUPS %s: %d of 13 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
