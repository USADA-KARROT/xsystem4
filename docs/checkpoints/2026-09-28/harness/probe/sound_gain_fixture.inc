/* sound-gain (G2, 2026-10-25): the internal volumes, the movie sound's group
 * and GetLengthFromFile, on top of the v14 sound groups (sound-groups).
 *
 * Original evidence (static disassembly of the one CN executable; private
 * research notes have the listings):
 *  - A channel's gain is 0x4a11b0: the group chain (0x4a5e70; 0 under the
 *    focus mute) x the channel's own volume (+0x18: 1.0, or Fade's fVolume,
 *    0x4a3adb) x an internal volume; 0x4a5050 clamps it to 0..1. Every
 *    caller of 0x4a11b0 hands the result to 0x4a5050, and a slider change
 *    walks every channel (0x4a10c0 -> 0x4a12b0), so changes reach what is
 *    playing, the movie's channel too.
 *  - The internal volume comes from the sound config object (vtable
 *    0x80ddb4, which also reads the group table): by group (0x4a1f60: group
 *    0 -> 1.0 without the EX; 1 -> the BGM key, 2 -> the sound-effect key,
 *    any other -> the voice key; a missing key gives 0.5, 0.5, 1.0), or the
 *    movie key (0x4a21d0; missing 0.5) for a channel whose name is empty
 *    (+0x2c, the length of the std::string at +0x1c) and whose bStreaming
 *    flag (+0x10) is set. A file's Prepare names its channel (0x4a4b20); a
 *    channel prepared from memory has no name (0x4a4b70).
 *  - GetGroupVolume (0x4a1a44) is the chain only.
 *  - CreatePartsMovie(Number, FileName, SoundID, SoundGroup, ...) (0x58912d
 *    -> 0x59b090 -> 0x569d90 -> 0x575870 -> 0x449f50) gives the movie's
 *    sound to 0x4a3140(SoundID, data, size, SoundGroup): the channel SoundID
 *    made with bStreaming 1 (0x49a130), prepared from memory, its group
 *    SoundGroup (a negative one falls back to 1), then SetLoopCount(SoundID,
 *    1) (0x4a2aa0). So the movie's gain is chain(SoundGroup) x the movie key,
 *    and GetGroupNum (0x4a3da0) reads SoundGroup for SoundID.
 *  - GetLengthFromFile (0x4a19c7 -> 0x4a42a0) finds the file as Prepare does
 *    (0x4a4050), picks the decoder by the first bytes (0x4a2ca0: Ogg, or
 *    RIFF/WAVE) and returns the PCM bytes x 1000 / the bytes per second in
 *    unsigned 64-bit arithmetic: milliseconds, truncated. A missing file or
 *    one of neither kind gives 0.
 * The game's scripts make the opening movie with SoundID 1000 and
 * SoundGroup 1; their AFL_Sound_GetLength wraps GetLengthFromFile (the
 * ending waits its value plus a join time after each voice, another caller
 * divides it by 1000.0 for seconds).
 *
 * Port strategies, checked here as such and not claimed as original
 * behavior: the groups are the nested mixers of sound-groups, so the
 * internal volume multiplies a channel's (or the movie stream's) own gain,
 * never a group's, while it plays and while it fades alike; the internal
 * volumes exist only with the groups (every other game keeps its gains, even
 * with the keys in its main EX or a group table of another shape);
 * GetLengthFromFile is the decoder's frames x 1000 / rate, truncated (the
 * same as the original except for a WAV data chunk that does not hold whole
 * frames, or one whose header's bytes per second disagree with its rate,
 * which no case has), looks the name up as Prepare does (so a numeric name
 * is a name, not an archive number) and stays 0 without the groups; the
 * movie's sound is not a channel of the scripts' pool (IsPlay and Stop do
 * not reach it), only GetGroupNum knows its SoundID, the latest movie made
 * with an ID owns it, and after that movie's release GetGroupNum gives 0 for
 * the ID (the original removes the channel by ID, 0x49a1c0; inferred from
 * its first half); a muted group or one at 0 still pulls the movie's sound,
 * so its clock goes on. GetLengthFromFile's new function is bound by the
 * declaration-shape hook (as sound-groups' wrappers are): the library
 * table's own function stays what it was (GN8 calls it directly).
 *
 * The scripts of this game never call GetGroupNum (and never call Kiwi with
 * the movie's SoundID): GN5's GetGroupNum checks are fidelity to 0x4a3140 and
 * 0x4a3da0 only. The original's Prepare keeps a channel's volume (0x4a3460
 * does not write +0x18; only Fade does, 0x4a3ad6); the port's Prepare opens
 * the channel anew at 1.0, so GN3 sets the volume back to 1.0 with Fade
 * before it prepares a channel again and depends on neither.
 *
 * Not covered: an internal volume over 1.0 (the original clamps the whole
 * product; nested mixers can only clamp a channel's part; the game's four
 * values are at most 1.0), keys of another type, a SoundGroup outside the
 * table, a SoundID a script uses too, the focus mute on the movie
 * (sound-groups silences the whole output), the system sound table, the
 * parts sounds, PlaySe, real audio output and the original's loudness.
 *
 * Cases (each in forked children): GN0 the synthetic data reads back; GN1
 * the gain by group (0, 1, 2, 3, 5, 6, 7; a BGM-style streaming file; a file
 * without a timemap row in either fallback group; two at once; changes while
 * playing; GetGroupVolume without it); GN2 missing keys; GN3 the channel
 * volume (Fade, also while a fade runs), a channel prepared again, a
 * restart; GN4 the movie sound in group 1 (follows the root and group 1, not
 * group 2 or the voice group; its clock goes on while group 1 is muted or at
 * 0); GN5 SoundGroup and SoundID are the arguments (2; -1 falls back to 1;
 * GetGroupNum, 0 for an unrelated ID and after the release; the groups as
 * before after the release; the same SoundID again in another group, and
 * two movies with one SoundID at once); GN6 the
 * movie key missing, or the only one with the sound-effect key; GN7
 * GetLengthFromFile (also numeric voice names that are not their archive
 * numbers, and 0 for an AIFF file, which libsndfile reads and the original's
 * decoder choice does not); GN8 the legacy mixers for every other game (v13, v15, SJIS, the
 * GB18030 flag only, keys without the table, keys with a table of another
 * shape, INI mixers; the movie on the root) and unknown declaration shapes.
 *
 * Data: synthetic, written here into a fresh mkdtemp directory
 * (xs4-sound-gain-*, under $XS4_SAVE_TMP or $TMPDIR, refused inside XS4_SRC
 * or XS4_MASTER_GAME, removed at the end): per data set a main EX (the
 * group table of sound_groups_fixture.inc and the internal volume keys at
 * made-up, exactly representable values 0.25, 0.875, 0.625 and 0.75, so
 * that every factor shows and none is the game's), Sound and Voice archives
 * with timemaps (PCM16 WAV, one AIFF; the movie fixtures' Vorbis long-mono.ogg
 * as a voice), and a copy of the movie fixtures' audio-only synthetic.apeg (its
 * sound is long-stereo.ogg). The only game-defined bytes are the schema
 * keys the engine has to recognize (GBK). File names are sgprobe* because
 * sg_setup names the AIN so.
 *
 * Levels: a constant mono WAV gives its gain exactly (sg_level). The movie's
 * level is the RMS of each output channel over 4096 frames against
 * long-stereo.ogg decoded here by libsndfile (a constant sine; Vorbis is
 * lossy; the engine before the change reads within 0.5% of its gain), within
 * 3%; right and wrong differ by 14% or more (0.75 against 0.875). The movie
 * sound is 1.2 s long: a case pulls at most eight levels of one movie. The
 * movie cases need a windowless GL context for the movie's surface, as
 * apeg-video.
 */
#include <sndfile.h>

/* The internal volume keys of the main EX (GBK; sound_groups_fixture.inc's
 * SG_KEY_IN_*): movie / BGM / sound effects / voice. */
#define GN_KEY_MOVIE SG_KEY_IN_MOVIE
#define GN_KEY_BGM   SG_KEY_IN_BGM
#define GN_KEY_SE    SG_KEY_IN_SE
#define GN_KEY_VOICE SG_KEY_IN_VOICE

/* Made-up values (not the game's). */
#define GN_IN_BGM   0.25f
#define GN_IN_SE    0.875f
#define GN_IN_VOICE 0.625f
#define GN_IN_MOVIE 0.75f

/* badtable: the group table with an int default column (sound-groups' intdef
 * shape, which keeps the legacy mixers), and every key. */
enum { GN_DS_KEYS, GN_DS_NOKEYS, GN_DS_PARTIAL, GN_DS_NOTABLE, GN_DS_BADTABLE, GN_NR_DS };
static const char *const gn_ds_names[GN_NR_DS] = { "keys", "nokeys", "partial", "notable", "badtable" };
/* The keys a data set has: 1 BGM, 2 sound effects, 4 voice, 8 movie. */
enum { GN_K_BGM = 1, GN_K_SE = 2, GN_K_VOICE = 4, GN_K_MOVIE = 8 };
static const int gn_ds_keys[GN_NR_DS] = { 15, 0, GN_K_SE | GN_K_MOVIE, 15, 15 };

static char gn_root_dir[PATH_MAX];
static char gn_ref_path[PATH_MAX];
static double gn_ref_rms[2];

/* The internal volume of a group (0x4a1f60; the original's defaults for a missing key). */
static float gn_internal(int ds, int g)
{
	int k = gn_ds_keys[ds];
	if (g == 0)
		return 1.0f;
	if (g == 1)
		return k & GN_K_BGM ? GN_IN_BGM : 0.5f;
	if (g == 2)
		return k & GN_K_SE ? GN_IN_SE : 0.5f;
	return k & GN_K_VOICE ? GN_IN_VOICE : 1.0f;
}
/* The movie's (0x4a21d0). */
static float gn_movie_internal(int ds) { return gn_ds_keys[ds] & GN_K_MOVIE ? GN_IN_MOVIE : 0.5f; }

/* ---- data ---------------------------------------------------------------------- */

/* Sounds through the groups: a constant (8192 = 0.25; gn_g0 4096 = 0.125). */
static const struct sg_sound gn_sounds[] = {
	{ "gn_g0", SG_CONST, 8192, 4096, true, 0, 0, 8192 },
	{ "gn_g1", SG_CONST, 8192, 8192, true, 1, 0, 8192 },
	{ "gn_g2", SG_CONST, 8192, 8192, true, 2, 0, 8192 },
	{ "gn_g5", SG_CONST, 8192, 8192, true, 5, 0, 8192 },
};
static const char *const gn_sound_rows[] = { "gn_g2", "gn_g0", "gn_g5", "gn_g1" };
static const struct sg_sound gn_voices[] = {
	{ "v3", SG_CONST, 8192, 8192, true, 3, 0, 8192 },
	{ "v6", SG_CONST, 8192, 8192, true, 6, 0, 8192 },
	{ "v7", SG_CONST, 8192, 8192, true, 7, 0, 8192 },
};
static const char *const gn_voice_rows[] = { "v7", "v3", "v6" };
#define GN_OGG_VOICE "vl_ogg"		/* the movie fixtures' long-mono.ogg: 52920 frames at 44.1 kHz */
#define GN_OGG_MS 1200
#define GN_MOVIE "gn_movie.apeg"

/* Sound-archive files for GetLengthFromFile (no timemap rows; gn_len_m is
 * also GN1's file without a row, at 1000/32768). */
struct gn_length { const char *name; int channels, rate, frames, ms; };
static const struct gn_length gn_lengths[] = {
	{ "gn_len_m", 1, 44100, 66150, 1500 },
	{ "gn_len_s", 2, 44100, 44144, 1000 },	/* 1000.997 ms: truncated, not rounded */
	{ "gn_len_h", 1, 22050, 33075, 1500 },	/* the file's rate, not the output's */
	{ "gn_len_z", 1, 44100, 0, 0 },
};
/* Voice-archive files with numeric names, as the ending's voices have
 * (dictionary order, after v3, v6, the timemap, v7 and the Vorbis voice: the
 * archive numbers are 5, 6, 7, or 6, 7, 8 counted from 1). A name read as a
 * number names another file: "2" is the timemap's archive index and v6's
 * number counted from 1; "10" and "100" are past the archive. */
static const struct gn_length gn_voice_lengths[] = {
	{ "10", 1, 44100, 30870, 700 },
	{ "100", 1, 44100, 39690, 900 },
	{ "2", 1, 44100, 22050, 500 },
};

static struct sg_buf gn_wav(int channels, uint32_t rate, int frames, int16_t value)
{
	struct sg_buf b = {0};
	uint32_t bytes = (uint32_t)frames * channels * 2;
	sgb_put(&b, "RIFF", 4); sgb_u32(&b, 36 + bytes); sgb_put(&b, "WAVE", 4);
	sgb_put(&b, "fmt ", 4); sgb_u32(&b, 16); sgb_u16(&b, 1); sgb_u16(&b, channels);
	sgb_u32(&b, rate); sgb_u32(&b, rate * channels * 2); sgb_u16(&b, channels * 2); sgb_u16(&b, 16);
	sgb_put(&b, "data", 4); sgb_u32(&b, bytes);
	for (int k = 0; k < frames * channels; k++)
		sgb_u16(&b, (uint16_t)value);
	return b;
}

/* An AIFF file (16-bit mono at 44.1 kHz): libsndfile reads it, the
 * original's decoder choice (0x4a2ca0) has nothing for it. */
#define GN_AIFF "gn_aiff"
#define GN_AIFF_FRAMES 22050
static void gnb_be32(struct sg_buf *b, uint32_t v)
{
	uint8_t x[4] = { v >> 24, v >> 16, v >> 8, v };
	sgb_put(b, x, 4);
}
static struct sg_buf gn_aiff(int frames, int16_t value)
{
	struct sg_buf b = {0};
	uint32_t bytes = (uint32_t)frames * 2;
	static const uint8_t rate_44100[10] = { 0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0 };
	sgb_put(&b, "FORM", 4); gnb_be32(&b, 4 + 26 + 16 + bytes); sgb_put(&b, "AIFF", 4);
	sgb_put(&b, "COMM", 4); gnb_be32(&b, 18);
	uint8_t comm[8] = { 0, 1, frames >> 24, frames >> 16, frames >> 8, frames, 0, 16 };
	sgb_put(&b, comm, 8); sgb_put(&b, rate_44100, 10);
	sgb_put(&b, "SSND", 4); gnb_be32(&b, 8 + bytes); gnb_be32(&b, 0); gnb_be32(&b, 0);
	for (int k = 0; k < frames; k++) {
		uint8_t s[2] = { (uint16_t)value >> 8, (uint16_t)value };
		sgb_put(&b, s, 2);
	}
	return b;
}

/* libsndfile on a buffer: its frames (-1 when it cannot open it). */
struct gn_mem { const uint8_t *p; sf_count_t n, at; };
static sf_count_t gn_mem_len(void *u) { return ((struct gn_mem *)u)->n; }
static sf_count_t gn_mem_seek(sf_count_t off, int whence, void *u)
{
	struct gn_mem *m = u;
	sf_count_t at = whence == SEEK_SET ? off : whence == SEEK_CUR ? m->at + off : m->n + off;
	m->at = at < 0 ? 0 : at > m->n ? m->n : at;
	return m->at;
}
static sf_count_t gn_mem_read(void *dst, sf_count_t count, void *u)
{
	struct gn_mem *m = u;
	sf_count_t c = count < m->n - m->at ? count : m->n - m->at;
	memcpy(dst, m->p + m->at, c);
	m->at += c;
	return c;
}
static sf_count_t gn_mem_write(const void *src, sf_count_t count, void *u) { (void)src; (void)count; (void)u; return 0; }
static sf_count_t gn_mem_tell(void *u) { return ((struct gn_mem *)u)->at; }
static long long gn_mem_frames(const uint8_t *p, size_t n)
{
	SF_VIRTUAL_IO io = { gn_mem_len, gn_mem_seek, gn_mem_read, gn_mem_write, gn_mem_tell };
	struct gn_mem m = { p, (sf_count_t)n, 0 };
	SF_INFO info = {0};
	SNDFILE *f = sf_open_virtual(&io, SFM_READ, &info, &m);
	if (!f)
		return -1;
	sf_close(f);
	return info.frames;
}

static void gn_float_block(struct sg_buf *b, const char *name, float value)
{
	sgx_float(b, name, value);
}

/* The main EX: unrelated blocks around the group table of sound-groups and
 * the data set's internal volume keys (in the game's order). */
static struct sg_buf gn_main_ex(int ds)
{
	struct sg_buf blocks = {0}, out = {0};
	int nb = 0;
	sgx_int(&blocks, "gn_unrelated_int", 7); nb++;
	if (ds != GN_DS_NOTABLE) {
		bool bad = ds == GN_DS_BADTABLE;
		struct sg_field f[3] = {
			{ SG_V_INT, SG_KEY_GROUP, true, SGNONE },
			{ SG_V_INT, SG_KEY_VOICE, false, SGI(-1) },
			bad ? (struct sg_field){ SG_V_INT, SG_KEY_DEFAULT, false, SGI(80) }
			    : (struct sg_field){ SG_V_FLOAT, SG_KEY_DEFAULT, false, SGF(0.8f) },
		};
		struct sg_val v[SG_NR_GROUPS * 3];
		for (int g = 0; g < SG_NR_GROUPS; g++) {
			v[g * 3] = SGI(g);
			v[g * 3 + 1] = SGI(sg_voice_col[g]);
			v[g * 3 + 2] = bad ? SGI(sg_defaults[g]) : SGF(sg_defaults_f[g]);
		}
		sgx_table(&blocks, SG_KEY_TABLE, f, 3, v, SG_NR_GROUPS); nb++;
	}
	gn_float_block(&blocks, "gn_unrelated_float", 0.125f); nb++;
	int k = gn_ds_keys[ds];
	if (k & GN_K_MOVIE) { gn_float_block(&blocks, GN_KEY_MOVIE, GN_IN_MOVIE); nb++; }
	if (k & GN_K_BGM) { gn_float_block(&blocks, GN_KEY_BGM, GN_IN_BGM); nb++; }
	if (k & GN_K_SE) { gn_float_block(&blocks, GN_KEY_SE, GN_IN_SE); nb++; }
	if (k & GN_K_VOICE) { gn_float_block(&blocks, GN_KEY_VOICE, GN_IN_VOICE); nb++; }
	sgx_int(&blocks, "gn_unrelated_after", 3); nb++;
	sgx_file(&out, &blocks, nb);
	free(blocks.p);
	return out;
}

/* An archive: the sounds (the timemap member in the middle), then the Vorbis
 * voice (voice archive) and the length files. */
#define GN_TM_POS 2
static bool gn_archive(const char *dir, const char *file, const struct sg_sound *v, int n, const char *tm_name,
		       const char *const *rows, int nr_rows, const struct gn_length *lens, int nr_lens, const struct sg_buf *ogg)
{
	struct sg_member m[24];
	static char names[24][64];
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (i == GN_TM_POS) {
			snprintf(names[k], sizeof names[k], "%s", tm_name);
			m[k].name = names[k];
			m[k].data = sg_timemap(v, n, rows, nr_rows, false);
			k++;
		}
		snprintf(names[k], sizeof names[k], "%s.wav", v[i].name);
		m[k].name = names[k];
		m[k].data = sg_wav(v[i].kind, v[i].frames, v[i].value);
		k++;
	}
	if (ogg) {
		snprintf(names[k], sizeof names[k], "%s.ogg", GN_OGG_VOICE);
		m[k].name = names[k];
		m[k].data = (struct sg_buf){0};
		sgb_put(&m[k].data, ogg->p, ogg->n);
		k++;
	}
	for (int i = 0; i < nr_lens; i++) {
		snprintf(names[k], sizeof names[k], "%s.wav", lens[i].name);
		m[k].name = names[k];
		m[k].data = gn_wav(lens[i].channels, lens[i].rate, lens[i].frames, 1000);
		k++;
	}
	if (!ogg) {
		snprintf(names[k], sizeof names[k], "%s.aiff", GN_AIFF);
		m[k].name = names[k];
		m[k].data = gn_aiff(GN_AIFF_FRAMES, 1000);
		k++;
	}
	struct sg_buf out = {0};
	sga_file(&out, m, k);
	bool ok = sg_write(dir, file, &out);
	for (int i = 0; i < k; i++)
		free(m[i].data.p);
	free(out.p);
	return ok;
}
#define GN_SOUND_FILES (SG_COUNT(gn_sounds) + 1 + SG_COUNT(gn_lengths) + 1)
#define GN_VOICE_FILES (SG_COUNT(gn_voices) + 1 + 1 + SG_COUNT(gn_voice_lengths))

static bool gn_slurp(const char *path, struct sg_buf *b)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	uint8_t chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
		sgb_put(b, chunk, n);
	fclose(f);
	return b->n > 0;
}

/* RMS of each channel of the movie's sound, decoded by libsndfile, over
 * 32768 frames (some 330 periods of the 440 Hz sine). */
static bool gn_reference(void)
{
	SF_INFO info = {0};
	SNDFILE *f = sf_open(gn_ref_path, SFM_READ, &info);
	if (!f || info.channels != 2 || info.frames < 4096 + 32768) {
		printf("  FAIL: cannot decode the reference %s (%s)\n", "long-stereo.ogg", f ? "shape" : sf_strerror(NULL));
		if (f)
			sf_close(f);
		return false;
	}
	float *buf = xmalloc(sizeof(float) * 2 * info.frames);
	sf_count_t got = sf_readf_float(f, buf, info.frames);
	sf_close(f);
	double s[2] = { 0, 0 };
	for (sf_count_t i = 4096; i < 4096 + 32768 && i < got; i++)
		for (int c = 0; c < 2; c++)
			s[c] += (double)buf[2 * i + c] * buf[2 * i + c];
	free(buf);
	for (int c = 0; c < 2; c++)
		gn_ref_rms[c] = sqrt(s[c] / 32768);
	return got == info.frames;
}

static bool gn_make_data(void)
{
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !*base)
		base = getenv("TMPDIR");
	if (!base || !*base)
		base = "/tmp";
	snprintf(gn_root_dir, sizeof gn_root_dir, "%s/xs4-sound-gain-XXXXXX", base);
	if (!mkdtemp(gn_root_dir)) {
		printf("  FAIL: mkdtemp %s: %s\n", gn_root_dir, strerror(errno));
		return false;
	}
	if (sg_inside(gn_root_dir, getenv("XS4_SRC")) || sg_inside(gn_root_dir, getenv("XS4_MASTER_GAME"))) {
		printf("  FAIL: refusing a data directory inside the source tree or the original game: %s\n", gn_root_dir);
		rmdir(gn_root_dir);
		gn_root_dir[0] = '\0';
		return false;
	}
	/* sg_setup's data root and data sets (sound_groups_fixture.inc) */
	snprintf(sg_root_dir, sizeof sg_root_dir, "%s", gn_root_dir);
	snprintf(gn_ref_path, sizeof gn_ref_path, "%s/long-stereo.ogg", XS4_MOVIE_FIXTURE_DIR);
	struct sg_buf ogg = {0}, apeg = {0};
	if (!gn_slurp(XS4_MOVIE_FIXTURE_DIR "/long-mono.ogg", &ogg) || !gn_slurp(XS4_MOVIE_FIXTURE_DIR "/synthetic.apeg", &apeg)) {
		printf("  FAIL: the movie fixtures (long-mono.ogg, synthetic.apeg) are missing\n");
		return false;
	}
	bool ok = true;
	for (int ds = 0; ok && ds < GN_NR_DS; ds++) {
		snprintf(sg_ds_dir[ds], sizeof sg_ds_dir[ds], "%s/%s", gn_root_dir, gn_ds_names[ds]);
		ok = !mkdir(sg_ds_dir[ds], 0700);
		struct sg_buf ex = gn_main_ex(ds);
		ok = ok && sg_write(sg_ds_dir[ds], "sgprobeEx.ex", &ex);
		free(ex.p);
		ok = ok && gn_archive(sg_ds_dir[ds], "sgprobeSound.afa", gn_sounds, SG_COUNT(gn_sounds), "timemap_sound.ex",
				      gn_sound_rows, SG_COUNT(gn_sound_rows), gn_lengths, SG_COUNT(gn_lengths), NULL);
		ok = ok && gn_archive(sg_ds_dir[ds], "sgprobeVoice.afa", gn_voices, SG_COUNT(gn_voices), "timemap_voice.ex",
				      gn_voice_rows, SG_COUNT(gn_voice_rows), gn_voice_lengths, SG_COUNT(gn_voice_lengths), &ogg);
		ok = ok && sg_write(sg_ds_dir[ds], GN_MOVIE, &apeg);
	}
	free(ogg.p);
	free(apeg.p);
	if (!ok) {
		printf("  FAIL: cannot write the synthetic game under %s\n", gn_root_dir);
		return false;
	}
	return gn_reference();
}

static void gn_remove_data(void)
{
	const char *slash = strrchr(gn_root_dir, '/');
	if (gn_root_dir[0] && slash && !strncmp(slash, "/xs4-sound-gain-", 16))
		nftw(gn_root_dir, sg_rm, 16, FTW_DEPTH | FTW_PHYS);
}

/* ---- helpers ---------------------------------------------------------------------- */

static const struct sg_opts gn_group_opts = { GN_DS_KEYS, 14, SG_GBK, false, false, false, NULL, false };

/* Distinct volumes, so that each factor shows: root 90, 1 50, 2 60, 3 70, 4 80, 5 40, 6 80, 7 30. */
static const int gn_vol[SG_NR_GROUPS] = { 90, 50, 60, 70, 80, 40, 80, 30 };
static void gn_set_volumes(int *vol, int *mute)
{
	for (int g = 0; g < SG_NR_GROUPS; g++) {
		SG_CHECK(sg_setvol(g, gn_vol[g]), "SetMixerVolume(%d, %d) failed", g, gn_vol[g]);
		vol[g] = gn_vol[g];
		mute[g] = 0;
	}
}

/* Prepare (with bStreaming), loop, play, then the level (sg_level). */
static float gn_play_level(int id, const char *name, bool streaming)
{
	if (!(sg_prepare(id, name, streaming) && sg_i2("SetLoopCount", id, 0) && sg_i1("Play", id))) {
		SG_CHECK(false, "cannot play %s on %d", name, id);
		return -1.0f;
	}
	return sg_level();
}
static void gn_stop(int id)
{
	sg_i1("Stop", id);
	sg_i1("Unprepare", id);
	sg_level();
}
static bool gn_fade(int id, int ms, float volume)
{
	stack_push(id); stack_push(ms); stack_push(volume); stack_push(0); stack_push(0);
	return sg_call("Fade").i;
}

/* RMS of each output channel over 4 chunks, after 2 (a change applies from
 * the next chunk the callback mixes). */
static void gn_rms(double out[2])
{
	static float buf[4 * SG_CHUNK * 2];
	sg_pull(buf, 2 * SG_CHUNK);
	sg_pull(buf, 4 * SG_CHUNK);
	double s[2] = { 0, 0 };
	for (int i = 0; i < 4 * SG_CHUNK; i++)
		for (int c = 0; c < 2; c++)
			s[c] += (double)buf[2 * i + c] * buf[2 * i + c];
	for (int c = 0; c < 2; c++)
		out[c] = sqrt(s[c] / (4 * SG_CHUNK));
}
static void gn_movie_level(const char *what, float gain)
{
	double rms[2];
	gn_rms(rms);
	printf("  movie %-58s L %.4f R %.4f of the reference (want %.4f)\n", what, rms[0] / gn_ref_rms[0], rms[1] / gn_ref_rms[1], gain);
	for (int c = 0; c < 2; c++) {
		double want = gn_ref_rms[c] * gain;
		bool ok = gain == 0.0f ? rms[c] == 0.0 : fabs(rms[c] - want) <= 0.03 * want;
		SG_CHECK(ok, "%s: the movie's %s channel at %.4f of the reference, want %.4f", what, c ? "right" : "left",
			 rms[c] / gn_ref_rms[c], gain);
	}
}

static bool gn_movie_create(int number, int sound_id, int sound_group)
{
	int s = heap_alloc_string(make_string(GN_MOVIE, strlen(GN_MOVIE)));
	union vm_value args[8] = { BU_I(number), BU_I(s), BU_I(sound_id), BU_I(sound_group), BU_I(0), BU_I(128), BU_I(128), BU_I(1) };
	bool r = bu_call("PartsEngine", "CreatePartsMovie", 8, 0, 8, args).i;
	heap_unref(s);
	return r;
}
static bool gn_movie_play(int number)
{
	return BU_PE("PlayPartsMovie", BU_I(number), BU_I(0), BU_I(1)).i;
}
static void gn_movie_release(int number)
{
	BU_PE("Release", BU_I(number));
}
/* GL for the movie's surface (apeg-video's way), then the sound engine, then
 * the parts engine. */
static CGLContextObj gn_movie_setup(const struct sg_opts *o)
{
	CGLContextObj ctx = ma_gl_setup();
	if (!ctx) {
		SG_CHECK(false, "no windowless GL context for the movie's surface");
		return NULL;
	}
	sg_setup(o);
	ri_postlink();
	return ctx;
}
static void gn_movie_teardown(CGLContextObj ctx)
{
	if (ctx) {
		CGLSetCurrentContext(NULL);
		CGLDestroyContext(ctx);
	}
}

/* AFL_Sound_GetLength(string), the scripts' own wrapper, through the VM. */
static int gn_script_length(const char *name)
{
	int fno = ain_get_function(ain, "AFL_Sound_GetLength");
	if (fno < 0) {
		SG_CHECK(false, "no AFL_Sound_GetLength in the AIN");
		return -1;
	}
	int s = sg_str(name), sp = stack_ptr;
	stack_push(s);
	vm_call(fno, -1);
	int r = stack_ptr > sp ? stack_pop().i : -1;
	stack_ptr = sp;
	heap_unref(s);
	return r;
}

/* ---- GN0: the synthetic data reads back -------------------------------------------- */

static void gn0_data(void)
{
	const char *keys[4] = { GN_KEY_BGM, GN_KEY_SE, GN_KEY_VOICE, GN_KEY_MOVIE };
	const float vals[4] = { GN_IN_BGM, GN_IN_SE, GN_IN_VOICE, GN_IN_MOVIE };
	for (int ds = 0; ds < GN_NR_DS; ds++) {
		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/sgprobeEx.ex", sg_ds_dir[ds]);
		struct ex *ex = ex_read_file(path);
		SG_CHECK(ex, "%s: the main EX does not read back", gn_ds_names[ds]);
		if (!ex)
			continue;
		struct ex_table *t = ex_get_table(ex, SG_KEY_TABLE);
		SG_CHECK(ds == GN_DS_NOTABLE ? !t : t && t->nr_rows == SG_NR_GROUPS, "%s: the group table", gn_ds_names[ds]);
		for (int k = 0; k < 4; k++) {
			struct ex_value *v = ex_get(ex, keys[k]);
			bool want = gn_ds_keys[ds] & (1 << k);
			SG_CHECK(want ? v && v->type == EX_FLOAT && v->f == vals[k] : !v, "%s: internal volume key %d %s", gn_ds_names[ds], k,
				 v ? "present" : "missing");
		}
		SG_CHECK(ex_get_float(ex, "gn_unrelated_float", -1.0f) == 0.125f, "%s: the unrelated float", gn_ds_names[ds]);
		ex_free(ex);
	}
	const char *arcs[2] = { "sgprobeSound.afa", "sgprobeVoice.afa" };
	const int counts[2] = { GN_SOUND_FILES, GN_VOICE_FILES };
	for (int a = 0; a < 2; a++) {
		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/%s", sg_ds_dir[GN_DS_KEYS], arcs[a]);
		int err = 0;
		struct afa_archive *ar = afa_open(path, 0, &err);
		SG_CHECK(ar && ar->nr_files == (uint32_t)counts[a], "%s: open %d, %u files, want %d", arcs[a], err, ar ? ar->nr_files : 0,
			 counts[a]);
		if (!ar)
			continue;
		const char *names[GN_SOUND_FILES > GN_VOICE_FILES ? GN_SOUND_FILES : GN_VOICE_FILES];
		int n = 0;
		if (a == 0) {
			for (int i = 0; i < SG_COUNT(gn_sounds); i++) names[n++] = gn_sounds[i].name;
			for (int i = 0; i < SG_COUNT(gn_lengths); i++) names[n++] = gn_lengths[i].name;
			names[n++] = "timemap_sound";
			names[n++] = GN_AIFF;
		} else {
			for (int i = 0; i < SG_COUNT(gn_voices); i++) names[n++] = gn_voices[i].name;
			names[n++] = GN_OGG_VOICE;
			names[n++] = "timemap_voice";
			for (int i = 0; i < SG_COUNT(gn_voice_lengths); i++) names[n++] = gn_voice_lengths[i].name;
		}
		for (int i = 0; i < n; i++)
			SG_CHECK(archive_exists_by_basename(&ar->ar, names[i], NULL), "%s: no %s", arcs[a], names[i]);
		if (a == 0) {
			/* GN7's premise: libsndfile reads the AIFF file (a length that
			 * does not check the first bytes would give one). */
			struct archive_data *d = archive_get_by_basename(&ar->ar, GN_AIFF);
			long long fr = d ? gn_mem_frames(d->data, d->size) : -2;
			SG_CHECK(fr == GN_AIFF_FRAMES, "%s: libsndfile reads %s as %lld frames, want %d", arcs[a], GN_AIFF, fr, GN_AIFF_FRAMES);
			if (d)
				archive_free_data(d);
		}
		if (a == 1) {
			/* GN7's premise: the numeric names are not their archive numbers. */
			static const struct { const char *name; int no; } at[] = { { "v6", 1 }, { "timemap_voice", 2 }, { "10", 5 }, { "100", 6 }, { "2", 7 } };
			for (int i = 0; i < SG_COUNT(at); i++) {
				int no = -1;
				SG_CHECK(archive_exists_by_basename(&ar->ar, at[i].name, &no) && no == at[i].no, "%s: %s at %d, want %d", arcs[a],
					 at[i].name, no, at[i].no);
			}
		}
		archive_free(&ar->ar);
	}
	SF_INFO info = {0};
	SNDFILE *f = sf_open(XS4_MOVIE_FIXTURE_DIR "/long-mono.ogg", SFM_READ, &info);
	SG_CHECK(f && info.frames == 52920 && info.samplerate == 44100 && info.channels == 1,
		 "the Vorbis voice: %lld frames at %d Hz, %d channel(s); want 52920 at 44100, 1", f ? (long long)info.frames : -1LL,
		 info.samplerate, info.channels);
	if (f)
		sf_close(f);
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sg_ds_dir[GN_DS_KEYS], GN_MOVIE);
	struct sg_buf apeg = {0};
	bool sond = gn_slurp(path, &apeg) && apeg.n > 64 && !memcmp(apeg.p, "APEG", 4) && memmem(apeg.p, apeg.n, "SOND", 4);
	SG_CHECK(sond, "the movie copy is not an APEG with a SOND chunk");
	free(apeg.p);
	for (int c = 0; c < 2; c++)
		SG_CHECK(gn_ref_rms[c] > 0.95 * 0.25 / sqrt(2.0) && gn_ref_rms[c] < 1.05 * 0.25 / sqrt(2.0),
			 "the reference's %s RMS is %.5f, want about %.5f (a 0.25 sine)", c ? "right" : "left", gn_ref_rms[c], 0.25 / sqrt(2.0));
	printf("  GN0 synthetic EX, archives and movie read back; reference RMS L %.5f R %.5f\n", gn_ref_rms[0], gn_ref_rms[1]);
}

/* ---- GN1: the gain by group ------------------------------------------------------------- */

static void gn1_by_group(void)
{
	sg_setup(&gn_group_opts);
	if (!sg_group_mode_ok())
		return;
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	const int ds = GN_DS_KEYS;
	struct { const char *name; int group; float amp; bool streaming; } s[] = {
		{ "gn_g0", 0, 0.125f, false }, { "gn_g1", 1, 0.25f, false },
		/* a file prepared with bStreaming (the BGM's way) is named: the BGM key, not the movie key */
		{ "gn_g1", 1, 0.25f, true },
		{ "gn_g2", 2, 0.25f, false }, { "gn_g5", 5, 0.25f, false },
		{ "v3", 3, 0.25f, false }, { "v6", 6, 0.25f, false }, { "v7", 7, 0.25f, false },
	};
	for (int i = 0; i < SG_COUNT(s); i++) {
		float got = gn_play_level(20, s[i].name, s[i].streaming);
		float chain = sg_chain(vol, mute, s[i].group), in = gn_internal(ds, s[i].group);
		float want = s[i].amp * chain * in;
		SG_CHECK(SG_NEAR(got, want), "%s (group %d%s): %.5f, want %.5f (sound %.3f x chain %.4f x internal %.4f)", s[i].name,
			 s[i].group, s[i].streaming ? ", bStreaming" : "", got, want, s[i].amp, chain, in);
		gn_stop(20);
	}
	/* A file without a timemap row: Prepare's fallback group (bStreaming 0 ->
	 * 2, 1 -> 1, 0x4a3568) and that group's internal volume (0x4a11b0 reads
	 * the channel's final group). */
	for (int st = 0; st < 2; st++) {
		int g = st ? 1 : 2;
		float got = gn_play_level(20, "gn_len_m", st);
		float want = 1000 / 32768.0f * sg_chain(vol, mute, g) * gn_internal(ds, g);
		int gg = sg_i1("GetGroupNum", 20);
		SG_CHECK(SG_NEAR(got, want) && gg == g, "gn_len_m without a row, bStreaming %d: %.5f, want %.5f (group %d x internal %.4f), GetGroupNum %d",
			 st, got, want, g, gn_internal(ds, g), gg);
		gn_stop(20);
	}
	/* Two at once: each channel by its own group. */
	SG_CHECK(sg_play_loop(20, "gn_g1", 0) && sg_play_loop(21, "v6", 0), "cannot play two");
	float got = sg_level();
	float want = 0.25f * sg_chain(vol, mute, 1) * gn_internal(ds, 1) + 0.25f * sg_chain(vol, mute, 6) * gn_internal(ds, 6);
	SG_CHECK(SG_NEAR(got, want), "group 1 + group 6 at once: %.5f, want %.5f", got, want);
	gn_stop(20);
	gn_stop(21);
	/* Changes reach a playing sound, the internal volume stays. */
	SG_CHECK(sg_play_loop(20, "gn_g2", 0), "cannot play gn_g2");
	sg_level();
	sg_setvol(2, 30); vol[2] = 30;
	got = sg_level(); want = 0.25f * sg_chain(vol, mute, 2) * gn_internal(ds, 2);
	SG_CHECK(SG_NEAR(got, want), "group 2 at 30 while playing: %.5f, want %.5f", got, want);
	sg_setvol(0, 50); vol[0] = 50;
	got = sg_level(); want = 0.25f * sg_chain(vol, mute, 2) * gn_internal(ds, 2);
	SG_CHECK(SG_NEAR(got, want), "the root at 50 while playing: %.5f, want %.5f", got, want);
	sg_setmute(2, true); mute[2] = 1;
	got = sg_level();
	SG_CHECK(got == 0.0f, "group 2 muted: %.5f, want 0", got);
	sg_setmute(2, false); mute[2] = 0;
	gn_stop(20);
	/* GetGroupVolume is the chain only (0x4a1a44); it was so before. */
	for (int g = 0; g < SG_NR_GROUPS; g++)
		SG_CHECK(SG_NEAR(sg_gv(g), sg_chain(vol, mute, g)), "GetGroupVolume(%d)=%.4f, want the chain %.4f (without the internal volume)",
			 g, sg_gv(g), sg_chain(vol, mute, g));
	printf("  GN1 gains by group checked\n");
}

/* ---- GN2: missing keys ------------------------------------------------------------------ */

static void gn2_missing(int ds)
{
	struct sg_opts o = gn_group_opts;
	o.ds = ds;
	sg_setup(&o);
	if (!sg_group_mode_ok())
		return;
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	struct { const char *name; int group; float amp; } s[] = {
		{ "gn_g0", 0, 0.125f }, { "gn_g1", 1, 0.25f }, { "gn_g2", 2, 0.25f }, { "gn_g5", 5, 0.25f },
		{ "v3", 3, 0.25f }, { "v6", 6, 0.25f }, { "v7", 7, 0.25f },
	};
	for (int i = 0; i < SG_COUNT(s); i++) {
		float got = gn_play_level(20, s[i].name, false);
		float chain = sg_chain(vol, mute, s[i].group), in = gn_internal(ds, s[i].group);
		float want = s[i].amp * chain * in;
		SG_CHECK(SG_NEAR(got, want), "%s: %s (group %d): %.5f, want %.5f (chain %.4f x internal %.4f)", gn_ds_names[ds], s[i].name,
			 s[i].group, got, want, chain, in);
		gn_stop(20);
	}
	printf("  GN2 %s checked\n", gn_ds_names[ds]);
}
static void gn2_nokeys(void) { gn2_missing(GN_DS_NOKEYS); }
static void gn2_partial(void) { gn2_missing(GN_DS_PARTIAL); }

/* ---- GN3: the channel volume, a channel prepared again, a restart ------------------------- */

static void gn3_channel(void)
{
	sg_setup(&gn_group_opts);
	if (!sg_group_mode_ok())
		return;
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	const int ds = GN_DS_KEYS;
	const float c2 = sg_chain(vol, mute, 2), in2 = gn_internal(ds, 2), c1 = sg_chain(vol, mute, 1), in1 = gn_internal(ds, 1);
	float got = gn_play_level(20, "gn_g2", false), want = 0.25f * c2 * in2;
	SG_CHECK(SG_NEAR(got, want), "gn_g2 at the channel volume 1.0: %.5f, want %.5f", got, want);
	/* Fade with 0 ms sets the channel's volume (0x4a3adb: +0x18 = fVolume). */
	SG_CHECK(gn_fade(20, 0, 0.5f), "Fade(20, 0, 0.5) failed");
	got = sg_level(); want = 0.25f * c2 * 0.5f * in2;
	SG_CHECK(SG_NEAR(got, want), "after Fade(0 ms, 0.5): %.5f, want %.5f (x the internal volume %.4f)", got, want, in2);
	SG_CHECK(gn_fade(20, 100, 0.2f), "Fade(20, 100, 0.2) failed");
	sg_level();
	got = sg_level(); want = 0.25f * c2 * 0.2f * in2;
	SG_CHECK(SG_NEAR(got, want) && !sg_i1("IsFade", 20), "after Fade(100 ms, 0.2): %.5f, want %.5f, fading %d", got, want, sg_i1("IsFade", 20));
	/* The same channel prepared with a group-1 file takes group 1's; its
	 * volume is set back to 1.0 first (the original's Prepare keeps it, the
	 * port's opens the channel at 1.0; see the header). */
	SG_CHECK(gn_fade(20, 0, 1.0f), "Fade(20, 0, 1.0) failed");
	got = gn_play_level(20, "gn_g1", false); want = 0.25f * c1 * in1;
	SG_CHECK(SG_NEAR(got, want), "channel 20 back at 1.0, prepared again with a group-1 file: %.5f, want %.5f", got, want);
	gn_stop(20);
	/* While a fade runs (0x4a3ad6 writes fVolume, 0x4a11b0 gives the target
	 * with the internal volume and 0x4a5050 fades to it from the gain applied
	 * before, which has it too): a 3 s fade to the same volume, so its gain
	 * stays 1.0 all the way. */
	struct { const char *name; int group; } fs[] = { { "gn_g1", 1 }, { "gn_g2", 2 }, { "v6", 6 } };
	for (int i = 0; i < SG_COUNT(fs); i++) {
		gn_play_level(20, fs[i].name, false);
		SG_CHECK(gn_fade(20, 3000, 1.0f), "Fade(20, 3000, 1.0) failed");
		got = sg_level();
		want = 0.25f * sg_chain(vol, mute, fs[i].group) * gn_internal(ds, fs[i].group);
		int fading = sg_i1("IsFade", 20);
		SG_CHECK(fading && SG_NEAR(got, want), "%s while a 3 s fade to 1.0 runs: %.5f, want %.5f (x the internal volume %.4f), fading %d",
			 fs[i].name, got, want, gn_internal(ds, fs[i].group), fading);
		gn_stop(20);
	}
	/* A restart (the volumes stay, sound-groups) keeps the internal volumes. */
	sg_restart();
	SG_CHECK(sg_numof() == SG_NR_GROUPS, "after the restart GetMixerNumof=%d", sg_numof());
	got = gn_play_level(21, "gn_g1", false); want = 0.25f * c1 * in1;
	SG_CHECK(SG_NEAR(got, want), "gn_g1 after a restart: %.5f, want %.5f", got, want);
	gn_stop(21);
	printf("  GN3 channel volume, re-prepare and restart checked\n");
}

/* ---- GN4: the movie sound in group 1 ------------------------------------------------------- */

static void gn4_movie(void)
{
	CGLContextObj ctx = gn_movie_setup(&gn_group_opts);
	if (!ctx)
		return;
	if (!sg_group_mode_ok()) {
		gn_movie_teardown(ctx);
		return;
	}
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	const float in = gn_movie_internal(GN_DS_KEYS);
	const int N = 7101;
	bool made = gn_movie_create(N, 1000, 1);
	SG_CHECK(made, "CreatePartsMovie(%d, movie, 1000, 1) failed", N);
	if (!made) {
		gn_movie_teardown(ctx);
		return;
	}
	gn_movie_level("before PlayPartsMovie", 0.0f);
	SG_CHECK(gn_movie_play(N), "PlayPartsMovie failed");
	gn_movie_level("SoundGroup 1 (root 90, group 1 50)", sg_chain(vol, mute, 1) * in);
	sg_setvol(2, 0); vol[2] = 0;
	sg_setmute(3, true); mute[3] = 1;
	gn_movie_level("sound effects at 0, the voice group muted", sg_chain(vol, mute, 1) * in);
	sg_setvol(1, 25); vol[1] = 25;
	gn_movie_level("group 1 at 25", sg_chain(vol, mute, 1) * in);
	sg_setvol(0, 45); vol[0] = 45;
	gn_movie_level("the root at 45", sg_chain(vol, mute, 1) * in);
	sg_setmute(1, true); mute[1] = 1;
	gn_movie_level("group 1 muted", 0.0f);
	sg_setmute(1, false); mute[1] = 0;
	sg_setmute(0, true); mute[0] = 1;
	gn_movie_level("the root muted", 0.0f);
	sg_setmute(0, false); mute[0] = 0;
	gn_movie_level("unmuted", sg_chain(vol, mute, 1) * in);
	gn_movie_release(N);
	gn_movie_teardown(ctx);
	printf("  GN4 movie sound in group 1 checked\n");
}
/* The movie's clock (its sound's refills) goes on while group 1 is muted or
 * at 0: the opening's pictures follow it. */
extern int movie_get_position(struct movie_context *mc);
static void gn4_movie_clock(void)
{
	CGLContextObj ctx = gn_movie_setup(&gn_group_opts);
	if (!ctx)
		return;
	if (!sg_group_mode_ok()) {
		gn_movie_teardown(ctx);
		return;
	}
	const int N = 7108;
	SG_CHECK(gn_movie_create(N, 1000, 1) && gn_movie_play(N), "CreatePartsMovie(%d, movie, 1000, 1) and play failed", N);
	struct parts *p = parts_try_get(N);
	struct movie_context *mc = p && p->states[0].type == PARTS_MOVIE ? p->states[0].movie.mc : NULL;
	SG_CHECK(mc, "no movie context on parts %d", N);
	if (mc) {
		static float buf[6 * SG_CHUNK * 2];
		sg_pull(buf, 2 * SG_CHUNK);
		for (int k = 0; k < 2; k++) {
			if (k == 0) {
				sg_setmute(1, true);
			} else {
				sg_setmute(1, false);
				sg_setvol(1, 0);
			}
			int a = movie_get_position(mc);
			sg_pull(buf, 6 * SG_CHUNK);
			int b = movie_get_position(mc);
			SG_CHECK(b - a >= 100, "group 1 %s: the movie's clock moved %d ms over 6144 frames (139 ms), want 100 or more",
				 k ? "at 0" : "muted", b - a);
		}
	}
	gn_movie_release(N);
	gn_movie_teardown(ctx);
	printf("  GN4 movie clock checked\n");
}

/* ---- GN5: SoundGroup and SoundID are the arguments ---------------------------------------- */

static void gn5_movie_args(void)
{
	CGLContextObj ctx = gn_movie_setup(&gn_group_opts);
	if (!ctx)
		return;
	if (!sg_group_mode_ok()) {
		gn_movie_teardown(ctx);
		return;
	}
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	const float in = gn_movie_internal(GN_DS_KEYS);
	SG_CHECK(gn_movie_create(7102, 1000, 2) && gn_movie_play(7102), "CreatePartsMovie(7102, movie, 1000, 2) and play failed");
	gn_movie_level("SoundGroup 2 (root 90, group 2 60)", sg_chain(vol, mute, 2) * in);
	int g = sg_i1("GetGroupNum", 1000);
	SG_CHECK(g == 2, "GetGroupNum(1000)=%d while the movie with SoundID 1000, SoundGroup 2 plays, want 2", g);
	sg_setvol(1, 25); vol[1] = 25;
	gn_movie_level("group 1 at 25 (the movie is in group 2)", sg_chain(vol, mute, 2) * in);
	sg_setvol(2, 30); vol[2] = 30;
	gn_movie_level("group 2 at 30", sg_chain(vol, mute, 2) * in);
	gn_movie_release(7102);
	gn_movie_level("released", 0.0f);
	g = sg_i1("GetGroupNum", 1000);
	SG_CHECK(g == 0, "GetGroupNum(1000)=%d after the movie's release, want 0", g);
	sg_setvol(1, 50); vol[1] = 50;
	sg_setvol(2, 60); vol[2] = 60;
	SG_CHECK(gn_movie_create(7103, 1234, -1) && gn_movie_play(7103), "CreatePartsMovie(7103, movie, 1234, -1) and play failed");
	gn_movie_level("SoundGroup -1 (falls back to group 1)", sg_chain(vol, mute, 1) * in);
	g = sg_i1("GetGroupNum", 1234);
	SG_CHECK(g == 1, "GetGroupNum(1234)=%d while the movie with SoundID 1234, SoundGroup -1 plays, want 1", g);
	sg_setvol(2, 0); vol[2] = 0;
	gn_movie_level("sound effects at 0 (the movie is in group 1)", sg_chain(vol, mute, 1) * in);
	gn_movie_release(7103);
	gn_movie_teardown(ctx);
	printf("  GN5 movie SoundGroup and SoundID checked\n");
}
/* An unrelated ID; the groups after a release (nothing of the movie is left
 * in them); the same SoundID again in another group (the game's movies all
 * take 1000). */
static void gn5_movie_reuse(void)
{
	CGLContextObj ctx = gn_movie_setup(&gn_group_opts);
	if (!ctx)
		return;
	if (!sg_group_mode_ok()) {
		gn_movie_teardown(ctx);
		return;
	}
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	const int ds = GN_DS_KEYS;
	const float in = gn_movie_internal(ds);
	SG_CHECK(gn_movie_create(7106, 1000, 1) && gn_movie_play(7106), "CreatePartsMovie(7106, movie, 1000, 1) and play failed");
	gn_movie_level("reuse: SoundGroup 1", sg_chain(vol, mute, 1) * in);
	int g = sg_i1("GetGroupNum", 4321);
	SG_CHECK(g == 0, "GetGroupNum(4321)=%d while only the movie's SoundID 1000 exists, want 0", g);
	gn_movie_release(7106);
	gn_movie_level("reuse: released", 0.0f);
	struct { const char *name; int group; } s[] = { { "gn_g1", 1 }, { "gn_g2", 2 } };
	for (int i = 0; i < SG_COUNT(s); i++) {
		float got = gn_play_level(20, s[i].name, false), want = 0.25f * sg_chain(vol, mute, s[i].group) * gn_internal(ds, s[i].group);
		SG_CHECK(SG_NEAR(got, want), "after the movie's release %s (group %d): %.5f, want %.5f", s[i].name, s[i].group, got, want);
		gn_stop(20);
	}
	SG_CHECK(gn_movie_create(7107, 1000, 2) && gn_movie_play(7107), "CreatePartsMovie(7107, movie, 1000, 2) and play failed");
	gn_movie_level("reuse: SoundID 1000 again, SoundGroup 2", sg_chain(vol, mute, 2) * in);
	g = sg_i1("GetGroupNum", 1000);
	SG_CHECK(g == 2, "GetGroupNum(1000)=%d for the second movie (SoundGroup 2), want 2", g);
	sg_setvol(1, 25); vol[1] = 25;
	gn_movie_level("reuse: group 1 at 25 (the second movie is in group 2)", sg_chain(vol, mute, 2) * in);
	gn_movie_release(7107);
	/* Two movies with one SoundID at once (port strategy: the latest made
	 * owns the ID; the other's release leaves it). */
	SG_CHECK(gn_movie_create(7109, 1000, 1) && gn_movie_create(7110, 1000, 2), "two movies with SoundID 1000 failed");
	g = sg_i1("GetGroupNum", 1000);
	SG_CHECK(g == 2, "GetGroupNum(1000)=%d with movies of SoundGroup 1 then 2, want 2 (the latest)", g);
	gn_movie_release(7109);
	g = sg_i1("GetGroupNum", 1000);
	SG_CHECK(g == 2, "GetGroupNum(1000)=%d after the earlier movie's release, want 2", g);
	gn_movie_release(7110);
	g = sg_i1("GetGroupNum", 1000);
	SG_CHECK(g == 0, "GetGroupNum(1000)=%d after both releases, want 0", g);
	gn_movie_teardown(ctx);
	printf("  GN5 movie SoundID reuse checked\n");
}

/* ---- GN6: the movie key missing, or present without the BGM key ------------------------- */

static void gn6_movie_key(int ds)
{
	struct sg_opts o = gn_group_opts;
	o.ds = ds;
	CGLContextObj ctx = gn_movie_setup(&o);
	if (!ctx)
		return;
	if (!sg_group_mode_ok()) {
		gn_movie_teardown(ctx);
		return;
	}
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	SG_CHECK(gn_movie_create(7104, 1000, 1) && gn_movie_play(7104), "%s: CreatePartsMovie and play failed", gn_ds_names[ds]);
	char what[80];
	snprintf(what, sizeof what, "%s: SoundGroup 1, movie internal %.4f", gn_ds_names[ds], gn_movie_internal(ds));
	gn_movie_level(what, sg_chain(vol, mute, 1) * gn_movie_internal(ds));
	gn_movie_release(7104);
	gn_movie_teardown(ctx);
}
static void gn6_nokeys(void) { gn6_movie_key(GN_DS_NOKEYS); }
static void gn6_partial(void) { gn6_movie_key(GN_DS_PARTIAL); }

/* ---- GN7: GetLengthFromFile ---------------------------------------------------------------- */

static void gn7_length(void)
{
	sg_setup(&gn_group_opts);
	if (!sg_group_mode_ok())
		return;
	for (int i = 0; i < SG_COUNT(gn_lengths); i++) {
		int got = sg_is("GetLengthFromFile", gn_lengths[i].name);
		SG_CHECK(got == gn_lengths[i].ms, "GetLengthFromFile(%s)=%d, want %d (%d frames, %d channel(s), %d Hz: ms, truncated)",
			 gn_lengths[i].name, got, gn_lengths[i].ms, gn_lengths[i].frames, gn_lengths[i].channels, gn_lengths[i].rate);
	}
	/* The ending asks for its voices by numeric names (a string): by name,
	 * not by the archive number a name would read as. */
	for (int i = 0; i < SG_COUNT(gn_voice_lengths); i++) {
		int got = sg_is("GetLengthFromFile", gn_voice_lengths[i].name), via = gn_script_length(gn_voice_lengths[i].name);
		SG_CHECK(got == gn_voice_lengths[i].ms && via == gn_voice_lengths[i].ms,
			 "voice \"%s\": GetLengthFromFile %d, AFL_Sound_GetLength %d, want %d (%d frames)", gn_voice_lengths[i].name, got, via,
			 gn_voice_lengths[i].ms, gn_voice_lengths[i].frames);
	}
	int got = sg_is("GetLengthFromFile", "gn_g1");
	SG_CHECK(got == 185, "GetLengthFromFile(gn_g1)=%d, want 185 (8192 frames at 44.1 kHz)", got);
	got = sg_is("GetLengthFromFile", "v6");
	SG_CHECK(got == 185, "GetLengthFromFile(v6)=%d, want 185 (a voice, 8192 frames at 44.1 kHz)", got);
	got = sg_is("GetLengthFromFile", GN_OGG_VOICE);
	SG_CHECK(got == GN_OGG_MS, "GetLengthFromFile(%s)=%d, want %d (a Vorbis voice)", GN_OGG_VOICE, got, GN_OGG_MS);
	/* No decoder: neither a WAV nor Ogg (0x4a2ca0), though libsndfile reads the AIFF one. */
	const char *none[] = { "gn_no_such_sound", "", "timemap_sound", "timemap_voice", GN_AIFF };
	for (int i = 0; i < SG_COUNT(none); i++) {
		got = sg_is("GetLengthFromFile", none[i]);
		SG_CHECK(got == 0, "GetLengthFromFile(\"%s\")=%d, want 0 (no sound file)", none[i], got);
	}
	got = gn_script_length("gn_len_s");
	SG_CHECK(got == 1000, "AFL_Sound_GetLength(gn_len_s)=%d, want 1000", got);
	got = gn_script_length(GN_OGG_VOICE);
	SG_CHECK(got == GN_OGG_MS, "AFL_Sound_GetLength(%s)=%d, want %d", GN_OGG_VOICE, got, GN_OGG_MS);
	got = gn_script_length("gn_no_such_sound");
	SG_CHECK(got == 0, "AFL_Sound_GetLength(gn_no_such_sound)=%d, want 0", got);
	printf("  GN7 GetLengthFromFile checked\n");
}

/* ---- GN8: legacy kept ------------------------------------------------------------------------ */

static int gn8_which;
static const struct { const char *what; struct sg_opts o; } gn8_cases[] = {
	{ "v13, keys and table", { GN_DS_KEYS, 13, SG_GBK, false, false, false, NULL, false } },
	{ "v15, keys and table", { GN_DS_KEYS, 15, SG_GBK, false, false, false, NULL, false } },
	{ "v14 SJIS, keys and table", { GN_DS_KEYS, 14, SG_SJIS, false, false, false, NULL, false } },
	{ "v14 GB18030 flag with the SJIS rule", { GN_DS_KEYS, 14, SG_FLAG_ONLY, false, false, false, NULL, false } },
	{ "v14 GBK, keys without the table", { GN_DS_NOTABLE, 14, SG_GBK, false, false, false, NULL, false } },
	{ "v13 with INI mixers", { GN_DS_KEYS, 13, SG_GBK, false, false, true, NULL, false } },
	{ "v14 GBK, keys with a table of another shape", { GN_DS_BADTABLE, 14, SG_GBK, false, false, false, NULL, false } },
};
/* The base revision's answers: the legacy mixers (Music/Sound/Master, or the
 * INI's), every sound in mixer 1, no internal volume, GetLengthFromFile 0. */
static void gn8_legacy(void)
{
	const char *what = gn8_cases[gn8_which].what;
	bool ini = gn8_cases[gn8_which].o.ini_mixers;
	sg_setup(&gn8_cases[gn8_which].o);
	SG_CHECK(sg_numof() == (ini ? 4 : 3), "%s: GetMixerNumof=%d, want %d (the legacy mixers)", what, sg_numof(), ini ? 4 : 3);
	const float want = 0.25f * (ini ? 0.5f : 1.0f);
	struct { const char *name; bool streaming; } s[] = { { "gn_g2", false }, { "gn_g1", true }, { "v6", false } };
	for (int i = 0; i < SG_COUNT(s); i++) {
		float got = gn_play_level(20 + i, s[i].name, s[i].streaming);
		SG_CHECK(SG_NEAR(got, want), "%s: %s plays at %.5f, want %.5f (mixer 1, no internal volume)", what, s[i].name, got, want);
		gn_stop(20 + i);
	}
	int a = sg_is("GetLengthFromFile", "gn_len_m"), b = sg_is("GetLengthFromFile", GN_OGG_VOICE), c = gn_script_length("gn_len_s");
	SG_CHECK(a == 0 && b == 0 && c == 0, "%s: GetLengthFromFile %d/%d, AFL_Sound_GetLength %d, want 0 as before", what, a, b, c);
}
/* The movie stays on the root (legacy mixer 2, Master) at 100. */
static void gn8_legacy_movie(int which)
{
	const char *what = gn8_cases[which].what;
	CGLContextObj ctx = gn_movie_setup(&gn8_cases[which].o);
	if (!ctx)
		return;
	SG_CHECK(sg_numof() == 3, "%s: GetMixerNumof=%d, want 3", what, sg_numof());
	SG_CHECK(gn_movie_create(7105, 1000, 1) && gn_movie_play(7105), "%s: CreatePartsMovie and play failed", what);
	char m[96];
	snprintf(m, sizeof m, "%s: on the root", what);
	gn_movie_level(m, 1.0f);
	sg_setvol(0, 30);
	sg_setvol(1, 30);
	snprintf(m, sizeof m, "%s: Music and Sound at 30", what);
	gn_movie_level(m, 1.0f);
	sg_setvol(2, 50);
	snprintf(m, sizeof m, "%s: Master at 50", what);
	gn_movie_level(m, 0.5f);
	gn_movie_release(7105);
	gn_movie_teardown(ctx);
}
/* Every legacy case but the INI mixers' (whose master is not mixer 2). */
static void gn8_movie_v13(void) { gn8_legacy_movie(0); }
static void gn8_movie_v15(void) { gn8_legacy_movie(1); }
static void gn8_movie_sjis(void) { gn8_legacy_movie(2); }
static void gn8_movie_flag(void) { gn8_legacy_movie(3); }
static void gn8_movie_notable(void) { gn8_legacy_movie(4); }
static void gn8_movie_badtable(void) { gn8_legacy_movie(6); }
/* Declarations of GetLengthFromFile of another shape keep the library table's
 * function, and that function is what it was (0, the name unread). */
static void gn8_shapes(void)
{
	extern struct static_library lib_KiwiSoundEngine;
	sg_setup(&gn_group_opts);
	if (!sg_group_mode_ok())
		return;
	int (*table_fn)(struct string *) = (int (*)(struct string *))sg_orig("GetLengthFromFile");
	struct string *s = make_string("gn_len_m", 8);
	int direct = table_fn ? table_fn(s) : -1;
	free_string(s);
	SG_CHECK(direct == 0, "the library table's GetLengthFromFile gives %d with the groups, want 0 (the v14 function is bound by shape)", direct);
	struct ain_library *kiwi = &ain->libraries[sg_lib];
	static struct ain_type t_int = { .data = AIN_INT, .struc = -1 }, t_float = { .data = AIN_FLOAT, .struc = -1 };
	int f = sg_fno("GetLengthFromFile");
	for (int how = 0; how < 3; how++) {
		struct ain_hll_function decl = kiwi->functions[f];
		struct ain_hll_argument args[4];
		memcpy(args, decl.arguments, sizeof(args[0]) * decl.nr_arguments);
		decl.arguments = args;
		if (how == 0)
			decl.return_type = t_float;	/* returns float */
		else if (how == 1)
			args[0].type = t_int;		/* takes an int */
		else
			decl.nr_arguments = 0;		/* takes nothing */
		struct ain_library fake = { .name = kiwi->name, .nr_functions = 1, .functions = &decl };
		struct hll_function *linked = link_static_library(&fake, &lib_KiwiSoundEngine);
		SG_CHECK(linked[0].fun == sg_orig("GetLengthFromFile"), "GetLengthFromFile of shape %d is not bound to the library table's function", how);
	}
	printf("  GN8 declaration shapes checked\n");
}

/* ---- the mode ------------------------------------------------------------------------------- */

static int test_sound_gain(void)
{
	if (!gn_make_data()) {
		gn_remove_data();
		printf("SOUND-GAIN FAIL: no synthetic data\n");
		return 1;
	}
	int failed = 0, f;
	sg_begin("GN0", "synthetic data reads back");
	failed += sg_report("GN0", "synthetic data reads back", sg_fork(gn0_data));
	sg_begin("GN1", "gain by group: chain x channel volume x internal volume");
	failed += sg_report("GN1", "gain by group: chain x channel volume x internal volume", sg_fork(gn1_by_group));
	sg_begin("GN2", "missing internal volume keys");
	f = sg_fork(gn2_nokeys) + sg_fork(gn2_partial);
	failed += sg_report("GN2", "missing internal volume keys", f);
	sg_begin("GN3", "channel volume (Fade, also mid-fade), re-prepare, restart");
	failed += sg_report("GN3", "channel volume (Fade, also mid-fade), re-prepare, restart", sg_fork(gn3_channel));
	sg_begin("GN4", "movie sound in group 1 x the movie internal volume");
	f = sg_fork(gn4_movie) + sg_fork(gn4_movie_clock);
	failed += sg_report("GN4", "movie sound in group 1 x the movie internal volume", f);
	sg_begin("GN5", "movie SoundGroup and SoundID are the arguments");
	f = sg_fork(gn5_movie_args) + sg_fork(gn5_movie_reuse);
	failed += sg_report("GN5", "movie SoundGroup and SoundID are the arguments", f);
	sg_begin("GN6", "movie internal volume key missing or alone");
	f = sg_fork(gn6_nokeys) + sg_fork(gn6_partial);
	failed += sg_report("GN6", "movie internal volume key missing or alone", f);
	sg_begin("GN7", "GetLengthFromFile in ms");
	failed += sg_report("GN7", "GetLengthFromFile in ms", sg_fork(gn7_length));
	sg_begin("GN8", "legacy kept (v13, v15, SJIS, flag only, keys without table or with another shape, INI), movie on the root, shapes");
	f = 0;
	for (gn8_which = 0; gn8_which < (int)SG_COUNT(gn8_cases); gn8_which++) {
		int r = sg_fork(gn8_legacy);
		if (r)
			printf("  GN8 legacy case failed: %s\n", gn8_cases[gn8_which].what);
		f += r;
	}
	f += sg_fork(gn8_movie_v13) + sg_fork(gn8_movie_v15) + sg_fork(gn8_movie_sjis) + sg_fork(gn8_movie_flag)
		+ sg_fork(gn8_movie_notable) + sg_fork(gn8_movie_badtable) + sg_fork(gn8_shapes);
	failed += sg_report("GN8", "legacy kept (v13, v15, SJIS, flag only, keys without table or with another shape, INI), movie on the root, shapes", f);
	gn_remove_data();
	printf("SOUND-GAIN %s: %d of 9 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
