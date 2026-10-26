/* sound-se (G11, 2026-10-25): KiwiSoundEngine's system sound table (PlaySe,
 * StopSe, IsPlaySe, GetFreeSeID, IsExistSeID, SetSeParam) on top of the v14
 * sound groups. It builds on sound_groups_fixture.inc's writers and setup,
 * sound_gain_fixture.inc's main EX with made-up internal volumes and
 * sound_rewind_fixture.inc's unity helper, and writes its own data.
 *
 * Original evidence (static disassembly of the one CN executable; private
 * research notes have the listings):
 *  - The HLL dispatch (0x4a13d0, jump table 0x4a1c34 in the AIN's order)
 *    sends GetFreeSeID and IsExistSeID to 0x499700 on the sound engine's
 *    table at +0xf8, SetSeParam to 0x4a6550, PlaySe to 0x4a65b0 -> 0x4a3340,
 *    StopSe to 0x4a65e0 -> 0x4998c0 and IsPlaySe to 0x4a6610 -> 0x499920,
 *    all on that table. Prepare, Play, Stop, IsPlay, Unprepare, GetGroupNum
 *    and GetFreeID use another one at +0xc8 (0x4a3430, 0x4a3700, 0x4a3790,
 *    0x4a6640, 0x4a3da0, 0x4a15ca): the two ID spaces are apart. Both are
 *    hash maps keyed by the int ID (0x442fc0).
 *  - An SE entry (0x499780, get or create) holds PlaySpan (+4), bMultiPlay
 *    (+8) and the entry's channels by sound name (+0xc; std::string order,
 *    0x49bbd0: the bytes up to the shorter length, unsigned and without case
 *    folding, 0x41b480, then the lengths, 0x49bc18, so two names are one
 *    only when they are equal byte for byte and in length) and by data
 *    (+0x14, the engine's own sounds from memory, 0x4a3250 -> 0x499860 ->
 *    0x49b980). A new entry takes the table's defaults, PlaySpan 0 and
 *    bMultiPlay 1 (the constructor 0x4a2f00 writes them at +0x128 and
 *    +0x12c). SetSeParam creates the entry and sets both on it alone
 *    (0x4a6583, 0x4a6590, 0x4a6593), true whenever the engine is there.
 *    PlaySe makes the entry too, before it looks at the name (0x4a3340 ->
 *    0x499780), and picks the channel among that entry's alone (0x4a3376).
 *    StopSe frees every channel of the entry (0x49a9b0 -> 0x4a01e0, which
 *    writes neither PlaySpan nor bMultiPlay) and keeps the entry (false for
 *    an ID without one); IsPlaySe is true while any of them plays (0x49ad40
 *    walks them all). Both only look the ID up (0x4998c0, 0x499920) and act
 *    on that entry alone: they make no entry, and another ID's sounds are
 *    neither stopped nor seen.
 *  - PlaySe picks the channel in 0x49b790: with channels of the same name,
 *    when PlaySpan > 0 and one of them plays at a position (vtable +0x24, ms)
 *    below PlaySpan, none (0x49b85a: so 0x4a3460 returns false and nothing
 *    plays). It walks them all (0x49b7f6..0x49b8a9, lower_bound to
 *    upper_bound of the name, 0x49bbd0 and 0x49bb60: the walk is the name's
 *    own range, a channel of another name is never in it) and goes on past
 *    one that does not play (vtable +0x18 false: 0x49b826 to the next one at
 *    0x49b860), it does not stop there. Else without bMultiPlay the first of
 *    them, stopped and prepared again, whether it plays or has ended (no
 *    IsPlay on that path, 0x49b8b2..0x49b8e4; the collection keeps it);
 *    else, and when the name has no channel, a new one (0x4a0040(0):
 *    bStreaming 0). Each time, after the walk (also for a dropped call,
 *    0x49b8e6), the entry's channels that do not play are freed (0x49ab10),
 *    except the one reused: one that has ended stays in the entry, and in
 *    the next call's walk, until then. Then the channel is prepared from
 *    the name as Prepare does (0x4a3460: the timemap's group, else 2) and
 *    played.
 *  - GetFreeSeID(Min, Max) is the first ID in [Min, Max) without an entry,
 *    0 when there is none (0x499700; the ID goes up until it equals Max,
 *    0x499754 jne, so with Min > Max from Min on); IsExistSeID(ID) is
 *    GetFreeSeID(ID, ID) != ID (0x4a14c5), and an empty range gives 0, so
 *    it is true for every ID but 0, with or without an entry.
 *  - A PlaySe channel has bStreaming 0, so its internal volume is its
 *    group's key, never the movie key (0x4a11b0 takes the movie key only for
 *    an unnamed channel with bStreaming set; sound-gain's G2 question).
 * The game's scripts: InitSystemSE sets SetSeParam(5 + ID, span, multi) from
 * the main EX's system sound table (three rows: 40 ms with, 40 ms with and
 * 40 ms without multiple play); PlaySystemSE (FUNC 19234) plays
 * PlaySe(5 + seID, ...): cursor, click and cancel on 5, the ADV marks and
 * battle sounds on 6, battle voices on 7 (VoiceRouter stops the type's
 * previous one first); their looped sounds use general IDs such as 5..8 at
 * the same time (the audit's traces), which before this change shared one
 * pool with the SE IDs. Nothing in the scripts calls GetFreeSeID or
 * IsExistSeID.
 *
 * Port strategies, checked here as such and not claimed as original
 * behavior: the table exists only with the groups (every other game keeps
 * the shared pool as before, also its IsExistSeID, SE9); PlaySpan is
 * measured on the port's own position of the playing sound (channel_get_pos,
 * the frames its refill has read; the original reads its buffer's GetPos,
 * inferred to be the time played, so a measure of wall-clock time since
 * the start fails here), so the cases stay far from the window's edge: with
 * PlaySpan 40 a second call at once or one chunk (23 ms) after the first
 * (also at once after another name's), or at least five chunks (116 ms)
 * after it; with PlaySpan 200 five chunks or twelve (278 ms) after it, or
 * one chunk after a second call, or seven chunks (162 ms) after a second
 * call when the first sound has ended a chunk or more before; a restart
 * empties the table and stops its sounds (SE13).
 *
 * Not covered: the engine's own SE users (the parts engine's CSound plays
 * through the table, 0x5ba830; a sound from memory on SE 4 with 200 ms,
 * 0x5feac0; G4), PlaySe's filter argument, negative or huge SeIDs, PlaySpan
 * near its edge, a negative PlaySpan (0x49b7ee compares it signed: no walk),
 * bMultiPlay changed between two calls of an ID (the game sets each ID
 * once), SetSeParam while the ID's sounds play (the game calls it only
 * while it initializes), one ID's PlaySpan told apart from another's (every
 * ID the game plays is set to 40), names that differ only in case
 * (0x41b480 compares bytes, so they are two names; the port's archive
 * lookup folds case, and neither the game's archives nor its string
 * constants spell a sound two ways), GetFreeSeID from 0 (no script calls
 * GetFreeSeID), how many sounds may overlap (the port mixes at most
 * STS_MIXER_VOICES per group), the release of finished channels (not visible
 * through the API: when they go, a stopped one kept in its entry, how many
 * pile up before the next collection), the scripts' BGM cap that waits on
 * IsPlaySe, real audio output.
 *
 * Cases (each in forked children): SE0 the synthetic data reads back; SE1 a
 * general channel's loop goes on, sample for sample, while PlaySe of the
 * same ID plays there (IDs 5, 6 and 7, with the game's parameters and
 * without, a voice on 7); SE2 the two ID spaces (StopSe and IsPlaySe leave
 * the general channel, Stop, IsPlay, GetGroupNum and Unprepare leave the SE,
 * an ID without an entry); SE3 the defaults: the same name twice at once and
 * again later both play, StopSe stops both, a missing name; SE4 PlaySpan 40
 * with multiple play: the same name at once is dropped, later it plays too,
 * another name at once plays (IsPlaySe while only the first still plays);
 * PlaySpan 200 and a sound that has ended before it, PlaySpan 200 counted in
 * ms (116 ms later dropped, 278 ms later not), PlaySpan 200 with two sounds
 * of the name playing (the second, within it, drops a third), PlaySpan 200
 * when the first of two has ended but is not freed yet (the walk goes on
 * past it, the second still drops a third), PlaySpan 40 with another name's
 * sound before the name's in the entry (the same name at once is still
 * dropped), PlaySpan 40 with a name that is a prefix of another (another
 * name: on SE 5 and 6, in either order, both play at once), the game's
 * three rows set in InitSystemSE's order on a fresh table (SE 5 keeps
 * multiple play, SE 7 its own single play, a PlaySe on SE 9 without an
 * entry plays beside SE 5's sound of the name, StopSe of SE 8 without an
 * entry stops nothing), and the three rows playing at once (StopSe of
 * SE 7, then of SE 6, leave the entries made before and after them); SE5
 * PlaySpan 40
 * without it: at once dropped, later the same sound starts over, another
 * name plays alongside, StopSe then another name, the same name after its
 * sound has ended (that channel taken again, the sound once more), StopSe
 * of SE 7 beside a sound of SE 6 (that one plays on, IsPlaySe of 7 and of
 * an ID without an entry false while it does, 7 keeps PlaySpan 40); SE6
 * the game's table and a battle's sequence over two
 * looping general channels on IDs 5 and 7; SE7 GetFreeSeID and IsExistSeID
 * (also the entry that PlaySe of a missing name makes, none that IsPlaySe or
 * StopSe makes, Min > Max); SE8 a PlaySe sound's group and internal volume
 * (guard: it was so before); SE9 every other game keeps the shared pool
 * (v13, v14 SJIS, v15, the GB18030 flag only, v14 GBK without the group
 * table or with one of no rows, v13 with INI mixers); SE10 unknown
 * declaration shapes keep the library table's functions; SE11 the real audio
 * thread under PlaySe, StopSe and general channels on the same IDs; SE12 a
 * system sound is on no general ID (IDs 0..15, general channels prepared on
 * 0..3 while it plays); SE13 a restart empties the table and stops its
 * sounds (port strategy).
 *
 * Data: synthetic, written here into a fresh mkdtemp directory
 * (xs4-sound-se-*, under $XS4_SAVE_TMP or $TMPDIR, refused inside XS4_SRC
 * or XS4_MASTER_GAME, removed at the end): sound-groups' main EX (internal
 * volume keys at 1.0) and sound-gain's (made-up keys), and the same Sound
 * and Voice archives with timemaps for each: PCM16 at 44.1 kHz, mono; a
 * ramp (frame k holds k+1) for the looping general channel, a constant
 * loop, constants of distinct powers of two for the system sounds, so that
 * the output minus the loop's known frames names the sounds that play.
 * Names are neutral; the only game-defined bytes are the schema keys the
 * engine has to recognize (GBK). Samples come from the engine's SDL
 * callback (probe_audio_pull) with every group at 100 (unity) and, but in
 * SE8, every internal volume at 1.0.
 */

/* The sounds (sound archive, archive order). */
#define SS_LOOP_FRAMES 6000
static const struct sg_sound ss_sounds[] = {
	{ "ss_loop", SG_RAMP, SS_LOOP_FRAMES, 0, true, 2, 0, SS_LOOP_FRAMES },	/* a general channel's loop */
	{ "ss_hum", SG_CONST, 4096, 64, true, 2, 0, 4096 },			/* a second general loop */
	{ "ss_se", SG_CONST, 3000, 8192, true, 2, 0, 3000 },
	{ "ss_se_b", SG_CONST, 3000, 2048, true, 2, 0, 3000 },
	{ "ss_se_c", SG_CONST, 3000, 16384, true, 2, 0, 3000 },
	{ "ss_long", SG_CONST, 16384, 1024, true, 2, 0, 16384 },
	{ "ss_ramp", SG_RAMP, 8000, 0, true, 2, 0, 8000 },
	{ "ss_g1", SG_CONST, 16384, 8192, true, 1, 0, 16384 },
	{ "ss_g2", SG_CONST, 16384, 8192, true, 2, 0, 16384 },
	{ "ss_norow", SG_CONST, 16384, 8192, false, 0, 0, 0 },
	{ "ss_short", SG_CONST, 500, 4096, true, 2, 0, 500 },			/* shorter than a PlaySpan of 200 ms */
	{ "ss_lon", SG_CONST, 3000, 512, true, 2, 0, 3000 },			/* its name is a prefix of ss_long's */
};
static const char *const ss_sound_rows[] = { "ss_se_c", "ss_long", "ss_loop", "ss_g1", "ss_se", "ss_ramp", "ss_hum", "ss_g2",
	"ss_se_b", "ss_short", "ss_lon" };
static const struct sg_sound ss_voices[] = {
	{ "ss_v6", SG_CONST, 16384, 8192, true, 6, 0, 16384 },
	{ "ss_v7", SG_CONST, 6000, 4096, true, 7, 0, 6000 },
	{ "ss_v7b", SG_CONST, 3000, 8192, true, 7, 0, 3000 },
};
static const char *const ss_voice_rows[] = { "ss_v7b", "ss_v6", "ss_v7" };
#define SS_SOUND_TM_POS 4
#define SS_VOICE_TM_POS 1

/* groups: sound-groups' main EX (keys at 1.0); keys: sound-gain's (keys
 * 0.25, 0.875, 0.625, 0.75); notable: sound-groups' without the table;
 * norows: sound-groups' with a table of no rows (a shape the groups refuse). */
enum { SS_DS_GROUPS, SS_DS_KEYS, SS_DS_NOTABLE, SS_DS_NOROWS, SS_NR_DS };
static const char *const ss_ds_names[SS_NR_DS] = { "groups", "keys", "notable", "norows" };
static char ss_root_dir[PATH_MAX];

static bool ss_make_data(void)
{
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !*base)
		base = getenv("TMPDIR");
	if (!base || !*base)
		base = "/tmp";
	snprintf(ss_root_dir, sizeof ss_root_dir, "%s/xs4-sound-se-XXXXXX", base);
	if (!mkdtemp(ss_root_dir)) {
		printf("  FAIL: mkdtemp %s: %s\n", ss_root_dir, strerror(errno));
		ss_root_dir[0] = '\0';
		return false;
	}
	if (sg_inside(ss_root_dir, getenv("XS4_SRC")) || sg_inside(ss_root_dir, getenv("XS4_MASTER_GAME"))) {
		printf("  FAIL: refusing a data directory inside the source tree or the original game: %s\n", ss_root_dir);
		rmdir(ss_root_dir);
		ss_root_dir[0] = '\0';
		return false;
	}
	/* sg_setup's data root and data sets (sound_groups_fixture.inc) */
	snprintf(sg_root_dir, sizeof sg_root_dir, "%s", ss_root_dir);
	bool ok = true;
	for (int ds = 0; ok && ds < SS_NR_DS; ds++) {
		snprintf(sg_ds_dir[ds], sizeof sg_ds_dir[ds], "%s/%s", ss_root_dir, ss_ds_names[ds]);
		ok = !mkdir(sg_ds_dir[ds], 0700);
		struct sg_buf ex = ds == SS_DS_KEYS ? gn_main_ex(GN_DS_KEYS)
			: sg_main_ex(ds == SS_DS_NOTABLE ? SG_DS_NOTABLE : ds == SS_DS_NOROWS ? SG_DS_NOROWS : SG_DS_VALID);
		ok = ok && sg_write(sg_ds_dir[ds], "sgprobeEx.ex", &ex);
		free(ex.p);
		ok = ok && sg_archive(sg_ds_dir[ds], "sgprobeSound.afa", ss_sounds, SG_COUNT(ss_sounds), "timemap_sound.ex",
				      SS_SOUND_TM_POS, ss_sound_rows, SG_COUNT(ss_sound_rows), false);
		ok = ok && sg_archive(sg_ds_dir[ds], "sgprobeVoice.afa", ss_voices, SG_COUNT(ss_voices), "timemap_voice.ex",
				      SS_VOICE_TM_POS, ss_voice_rows, SG_COUNT(ss_voice_rows), false);
	}
	if (!ok)
		printf("  FAIL: cannot write the synthetic game under %s\n", ss_root_dir);
	return ok;
}

static void ss_remove_data(void)
{
	const char *slash = strrchr(ss_root_dir, '/');
	if (ss_root_dir[0] && slash && !strncmp(slash, "/xs4-sound-se-", 14))
		nftw(ss_root_dir, sg_rm, 16, FTW_DEPTH | FTW_PHYS);
}

/* ---- SE0: the synthetic files read back with libsys4 ------------------------------- */

static void se0_archive(int ds, const char *file, const char *tm, int tm_pos, const struct sg_sound *v, int n,
			const char *const *rows, int nr_rows)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sg_ds_dir[ds], file);
	int err = 0;
	struct afa_archive *ar = afa_open(path, 0, &err);
	SG_CHECK(ar && !ar->has_number && ar->nr_files == (uint32_t)n + 1, "%s/%s: open %d, %u files", ss_ds_names[ds], file, err,
		 ar ? ar->nr_files : 0);
	if (!ar)
		return;
	for (int i = 0; i < n; i++) {
		int no = -1, want = i < tm_pos ? i : i + 1;
		SG_CHECK(archive_exists_by_basename(&ar->ar, v[i].name, &no) && no == want, "%s/%s: %s at %d, want %d",
			 ss_ds_names[ds], file, v[i].name, no, want);
		struct archive_data *d = archive_get_by_basename(&ar->ar, v[i].name);
		bool ok = d && d->size == 44 + 2 * (size_t)v[i].frames && d->data[22] == 1 && d->data[23] == 0;
		for (int k = 0; ok && k < v[i].frames; k += v[i].frames - 1) {
			const uint8_t *f = d->data + 44 + 2 * k;
			int want_v = v[i].kind == SG_RAMP ? k + 1 : v[i].value;
			ok = (int16_t)(f[0] | f[1] << 8) == want_v;
		}
		SG_CHECK(ok, "%s/%s: %s is not the mono sound of %d frames it should be", ss_ds_names[ds], file, v[i].name, v[i].frames);
		if (d)
			archive_free_data(d);
	}
	struct archive_data *d = archive_get_by_basename(&ar->ar, tm);
	struct ex *t = d ? ex_read(d->data, d->size) : NULL;
	struct ex_table *st = t ? ex_get_table(t, "SoundInfo") : NULL;
	SG_CHECK(st && st->nr_rows == (uint32_t)nr_rows && st->nr_fields == 4, "%s/%s: %s SoundInfo shape", ss_ds_names[ds], file, tm);
	for (int r = 0; st && r < nr_rows && r < (int)st->nr_rows; r++) {
		const struct sg_sound *s = sg_find(v, n, rows[r]);
		SG_CHECK(s && st->rows[r][0].s && !strcmp(st->rows[r][0].s->text, rows[r]) && st->rows[r][1].i == s->group
			 && st->rows[r][2].i == s->begin && st->rows[r][3].i == s->end, "%s/%s: timemap row %d (%s)", ss_ds_names[ds], file,
			 r, rows[r]);
	}
	if (d)
		archive_free_data(d);
	archive_free(&ar->ar);
}

static void se0_data(void)
{
	static const char *const in_keys[4] = { SG_KEY_IN_MOVIE, SG_KEY_IN_BGM, SG_KEY_IN_SE, SG_KEY_IN_VOICE };
	static const float key_values[4] = { GN_IN_MOVIE, GN_IN_BGM, GN_IN_SE, GN_IN_VOICE };
	for (int ds = 0; ds < SS_NR_DS; ds++) {
		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/sgprobeEx.ex", sg_ds_dir[ds]);
		struct ex *ex = ex_read_file(path);
		struct ex_table *t = ex ? ex_get_table(ex, SG_KEY_TABLE) : NULL;
		if (ds == SS_DS_NOTABLE)
			SG_CHECK(ex && !t, "%s: main EX %s", ss_ds_names[ds], ex ? "has a group table" : "does not read");
		else if (ds == SS_DS_NOROWS)
			SG_CHECK(t && t->nr_rows == 0 && t->nr_fields == 3, "%s: main EX group table without rows", ss_ds_names[ds]);
		else
			SG_CHECK(t && t->nr_rows == SG_NR_GROUPS && t->nr_fields == 3, "%s: main EX group table", ss_ds_names[ds]);
		for (int k = 0; ex && k < 4; k++) {
			struct ex_value *iv = ex_get(ex, in_keys[k]);
			float want = ds == SS_DS_KEYS ? key_values[k] : 1.0f;
			SG_CHECK(iv && iv->type == EX_FLOAT && iv->f == want, "%s: internal volume key %d is not %.3f", ss_ds_names[ds], k, want);
		}
		se0_archive(ds, "sgprobeSound.afa", "timemap_sound", SS_SOUND_TM_POS, ss_sounds, SG_COUNT(ss_sounds), ss_sound_rows,
			    SG_COUNT(ss_sound_rows));
		se0_archive(ds, "sgprobeVoice.afa", "timemap_voice", SS_VOICE_TM_POS, ss_voices, SG_COUNT(ss_voices), ss_voice_rows,
			    SG_COUNT(ss_voice_rows));
	}
	printf("  SE0 synthetic EX, AFA and timemap files read back with libsys4\n");
}

/* ---- helpers ------------------------------------------------------------------------ */

static const struct sg_opts ss_group_opts = { SS_DS_GROUPS, 14, SG_GBK, false, false, false, NULL, false };

static bool ss_playse(int id, const char *name)
{
	stack_push(id);
	stack_push(sg_str(name));
	stack_push(sg_str(""));
	return sg_call("PlaySe").i;
}
static bool ss_param(int id, int span, bool multi)
{
	stack_push(id);
	stack_push(span);
	stack_push((int)multi);
	return sg_call("SetSeParam").i;
}
static bool ss_stopse(int id) { return sg_i1("StopSe", id); }
static bool ss_isplayse(int id) { return sg_i1("IsPlaySe", id); }
static int ss_free(int lo, int hi) { return sg_i2("GetFreeSeID", lo, hi); }
static bool ss_exist(int id) { return sg_i1("IsExistSeID", id); }

/* A general channel that loops for ever. */
static bool ss_loop_on(int id, const char *name)
{
	bool ok = sg_play_loop(id, name, 0);
	SG_CHECK(ok, "cannot loop %s on the general channel %d", name, id);
	return ok;
}

/* The game's InitSystemSE: SE 5, 6 and 7 from the three rows of its table. */
static void ss_game_params(void)
{
	SG_CHECK(ss_param(5, 40, true) && ss_param(6, 40, true) && ss_param(7, 40, false), "SetSeParam for 5, 6 and 7 returned false");
}

/* A take: pulled chunk by chunk into ss_buf, the calls of a case between. */
enum { SS_TAKE = 40 * SG_CHUNK };
static float ss_buf[SS_TAKE * 2];
static float ss_scratch[4 * SG_CHUNK * 2];
static int ss_at;

static void ss_begin_take(void)
{
	sg_pull(ss_scratch, 3 * SG_CHUNK);
	memset(ss_buf, 0, sizeof ss_buf);
	ss_at = 0;
}
static void ss_pull(int chunks)
{
	if (ss_at + chunks * SG_CHUNK > SS_TAKE)
		chunks = (SS_TAKE - ss_at) / SG_CHUNK;
	sg_pull(ss_buf + 2 * ss_at, chunks * SG_CHUNK);
	ss_at += chunks * SG_CHUNK;
}

/* What the general channels add to a frame of the take: the ramp loop (its
 * phase at frame 0; -1 without), a constant loop, and a ramp played once from
 * a frame of the take (-1 without). */
struct ss_model { int loop_p0; int hum; int ramp_start, ramp_len; };
static const struct ss_model ss_none = { -1, 0, -1, 0 };
static int ss_expect(const struct ss_model *m, int i)
{
	int v = m->hum;
	if (m->loop_p0 >= 0)
		v += (m->loop_p0 + i) % SS_LOOP_FRAMES + 1;
	if (m->ramp_start >= 0 && i >= m->ramp_start && i < m->ramp_start + m->ramp_len)
		v += i - m->ramp_start + 1;
	return v;
}

/* The left sample in 1/32768; *whole is cleared when it is not a whole number
 * or right differs (every sound here is mono). */
static int ss_q(int i, bool *whole)
{
	float l = ss_buf[2 * i], r = ss_buf[2 * i + 1];
	float x = l * 32768.0f;
	int q = (int)lrintf(x);
	if (fabsf(x - q) > 1e-3f || l != r)
		*whole = false;
	return q;
}

/* The phase of the ramp loop at frame 0 of the take (plus a constant loop of
 * hum); -1 (and a failure) when frames 0 and 1 are not two frames of it in
 * order. */
static int ss_loop_phase(const char *what, int hum)
{
	bool whole = true;
	int a = ss_q(0, &whole) - hum, b = ss_q(1, &whole) - hum;
	if (!whole || a < 1 || a > SS_LOOP_FRAMES || b != a % SS_LOOP_FRAMES + 1) {
		SG_CHECK(false, "%s: the take does not begin inside the general channel's loop (frames 0 and 1: %d, %d)", what, a, b);
		return -1;
	}
	return a - 1;
}

/* The residual of each frame (the output minus the model) in runs. */
struct ss_run { int value, start, len; };
#define SS_MAX_RUNS 512
static struct ss_run ss_runs[SS_MAX_RUNS];
static int ss_nr_runs;

static void ss_print_runs(const char *what)
{
	char line[600];
	int n = 0;
	line[0] = '\0';
	for (int r = 0; r < ss_nr_runs && r < 12 && n < (int)sizeof line - 40; r++)
		n += snprintf(line + n, sizeof line - n, "%s%dx%d", r ? ", " : "", ss_runs[r].value, ss_runs[r].len);
	printf("    %s: residual runs (value x frames): %s%s\n", what, line, ss_nr_runs > 12 ? ", ..." : "");
}

/* Splits the take into runs and checks every value is one of allowed. The
 * first frame that is not names what broke (a loop of a general channel
 * that stopped or started over gives values outside the set). */
static bool ss_split(const char *what, const struct ss_model *m, const int *allowed, int nr_allowed)
{
	ss_nr_runs = 0;
	bool whole = true;
	for (int i = 0; i < ss_at; i++) {
		int got = ss_q(i, &whole), want = ss_expect(m, i), v = got - want;
		if (!whole) {
			SG_CHECK(false, "%s: take frame %d is not a whole sum of the sounds (left %.8f, right %.8f)", what, i, ss_buf[2 * i],
				 ss_buf[2 * i + 1]);
			return false;
		}
		bool ok = false;
		for (int k = 0; k < nr_allowed && !ok; k++)
			ok = v == allowed[k];
		if (!ok) {
			SG_CHECK(false, "%s: take frame %d is %d, the general channels' %d plus %d, which is no sum of the system sounds there "
				 "(a general channel's sound stopped, started over or the system sounds are not what they should be)",
				 what, i, got, want, v);
			ss_print_runs(what);
			return false;
		}
		if (ss_nr_runs && ss_runs[ss_nr_runs - 1].value == v) {
			ss_runs[ss_nr_runs - 1].len++;
		} else if (ss_nr_runs < SS_MAX_RUNS) {
			ss_runs[ss_nr_runs++] = (struct ss_run){ v, i, 1 };
		} else {
			SG_CHECK(false, "%s: more than %d runs", what, SS_MAX_RUNS);
			return false;
		}
	}
	return true;
}

/* The runs must be these, in order; an entry with lo 0 may be missing (no
 * run of its value there). hi < 0: no upper bound. */
struct ss_pat { int value, lo, hi; };
static bool ss_match(const char *what, const struct ss_pat *p, int n)
{
	int r = 0;
	for (int k = 0; k < n; k++) {
		if (r < ss_nr_runs && ss_runs[r].value == p[k].value && ss_runs[r].len >= p[k].lo && (p[k].hi < 0 || ss_runs[r].len <= p[k].hi)) {
			r++;
			continue;
		}
		if (p[k].lo == 0)
			continue;
		SG_CHECK(false, "%s: run %d is %s, want %d for %d..%d frames", what, r,
			 r < ss_nr_runs ? "another" : "missing", p[k].value, p[k].lo, p[k].hi);
		ss_print_runs(what);
		return false;
	}
	if (r != ss_nr_runs) {
		SG_CHECK(false, "%s: %d more runs than wanted", what, ss_nr_runs - r);
		ss_print_runs(what);
		return false;
	}
	return true;
}

/* The lead before a sound called at take frame 1024: where the port starts it
 * (the chunk after the call; the first chunk of a new stream is its zeroed
 * buffer), up to three chunks late. */
#define SS_LEAD_LO SG_CHUNK
#define SS_LEAD_HI (4 * SG_CHUNK)

/* ---- SE1: a general channel's loop goes on while PlaySe of its ID plays ------------- */

static int se1_which;
static const struct { const char *what; int id, span, multi; const char *se; int value, frames; } se1_cases[] = {
	{ "ID 5 without SetSeParam", 5, -1, 0, "ss_se", 8192, 3000 },
	{ "ID 5 at 40 ms with multiple play (the game's row 0)", 5, 40, 1, "ss_se", 8192, 3000 },
	{ "ID 6 at 40 ms with multiple play (row 1)", 6, 40, 1, "ss_se_b", 2048, 3000 },
	{ "ID 7 at 40 ms without multiple play (row 2)", 7, 40, 0, "ss_se", 8192, 3000 },
	{ "ID 7 at 40 ms without multiple play, a voice", 7, 40, 0, "ss_v7", 4096, 6000 },
};

static void se1_loop_goes_on(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = se1_cases[se1_which].what;
	int id = se1_cases[se1_which].id, value = se1_cases[se1_which].value, frames = se1_cases[se1_which].frames;
	if (!ss_loop_on(id, "ss_loop"))
		return;
	if (se1_cases[se1_which].span >= 0)
		SG_CHECK(ss_param(id, se1_cases[se1_which].span, se1_cases[se1_which].multi), "%s: SetSeParam returned false", what);
	ss_begin_take();
	ss_pull(1);
	SG_CHECK(ss_playse(id, se1_cases[se1_which].se), "%s: PlaySe(%d, %s) returned false", what, id, se1_cases[se1_which].se);
	SG_CHECK(ss_isplayse(id), "%s: IsPlaySe(%d) is false right after PlaySe", what, id);
	SG_CHECK(sg_i1("IsPlay", id), "%s: the general channel %d does not play after PlaySe of its ID", what, id);
	ss_pull(14);
	struct ss_model m = ss_none;
	m.loop_p0 = ss_loop_phase(what, 0);
	if (m.loop_p0 < 0)
		return;
	int allowed[] = { 0, value };
	if (ss_split(what, &m, allowed, 2)) {
		struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { value, frames, frames }, { 0, 2 * SG_CHUNK, -1 } };
		ss_match(what, p, 3);
	}
	SG_CHECK(sg_i1("IsPlay", id), "%s: the general channel %d stopped", what, id);
	SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe(%d) is still true after the system sound ended", what, id);
	SG_CHECK(sg_i1("GetGroupNum", id) == 2, "%s: GetGroupNum(%d)=%d, want the general channel's group 2", what, id,
		 sg_i1("GetGroupNum", id));
}

/* ---- SE2: the two ID spaces ----------------------------------------------------------- */

static int se2_which;
static const char *const se2_names[] = {
	"StopSe and IsPlaySe leave the general channel of the ID",
	"Stop and IsPlay leave the system sound of the ID",
	"an ID with a general channel and no SE entry",
	"an ID with a system sound and no general channel",
};

static void se2_spaces(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = se2_names[se2_which];
	struct ss_model m = ss_none;
	if (se2_which == 0) {
		if (!ss_loop_on(7, "ss_loop"))
			return;
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(7, "ss_long"), "%s: PlaySe(7) returned false", what);
		ss_pull(3);
		SG_CHECK(ss_isplayse(7) && sg_i1("IsPlay", 7), "%s: IsPlaySe(7) %d, IsPlay(7) %d while both play, want both true", what,
			 ss_isplayse(7), sg_i1("IsPlay", 7));
		SG_CHECK(ss_stopse(7), "%s: StopSe(7) returned false", what);
		SG_CHECK(!ss_isplayse(7), "%s: IsPlaySe(7) is true after StopSe", what);
		SG_CHECK(sg_i1("IsPlay", 7), "%s: StopSe(7) stopped the general channel 7", what);
		ss_pull(8);
		m.loop_p0 = ss_loop_phase(what, 0);
		int allowed[] = { 0, 1024 };
		if (m.loop_p0 >= 0 && ss_split(what, &m, allowed, 2)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 1024, 1, 5 * SG_CHUNK }, { 0, 4 * SG_CHUNK, -1 } };
			ss_match(what, p, 3);
		}
		SG_CHECK(sg_i1("IsPlay", 7), "%s: the general channel 7 stopped", what);
	} else if (se2_which == 1) {
		if (!ss_loop_on(7, "ss_loop"))
			return;
		sg_pull(ss_scratch, SG_CHUNK);
		SG_CHECK(ss_playse(7, "ss_long"), "%s: PlaySe(7) returned false", what);
		sg_pull(ss_scratch, 2 * SG_CHUNK);
		SG_CHECK(sg_i1("Stop", 7), "%s: Stop(7) returned false", what);
		SG_CHECK(!sg_i1("IsPlay", 7), "%s: IsPlay(7) is true after Stop", what);
		SG_CHECK(ss_isplayse(7), "%s: Stop(7) stopped the system sound of SE 7", what);
		ss_begin_take();
		ss_pull(6);
		/* the system sound alone, from the first frame of the take on */
		int allowed[] = { 0, 1024 };
		if (ss_split(what, &m, allowed, 2)) {
			struct ss_pat p[] = { { 1024, 6 * SG_CHUNK, 6 * SG_CHUNK } };
			ss_match(what, p, 1);
		}
		ss_pull(12);
		SG_CHECK(!ss_isplayse(7), "%s: IsPlaySe(7) is still true after the system sound ended", what);
	} else if (se2_which == 2) {
		if (!ss_loop_on(9, "ss_loop"))
			return;
		sg_pull(ss_scratch, 2 * SG_CHUNK);
		SG_CHECK(!ss_isplayse(9), "%s: IsPlaySe(9) is true for the general channel 9", what);
		SG_CHECK(!ss_stopse(9), "%s: StopSe(9) returned true without an SE entry 9", what);
		SG_CHECK(sg_i1("IsPlay", 9), "%s: StopSe(9) stopped the general channel 9", what);
		ss_begin_take();
		ss_pull(4);
		m.loop_p0 = ss_loop_phase(what, 0);
		int allowed[] = { 0 };
		if (m.loop_p0 >= 0)
			ss_split(what, &m, allowed, 1);
	} else {
		SG_CHECK(ss_playse(11, "ss_long"), "%s: PlaySe(11) returned false", what);
		sg_pull(ss_scratch, 2 * SG_CHUNK);
		SG_CHECK(!sg_i1("IsPlay", 11), "%s: IsPlay(11) is true without a general channel 11", what);
		SG_CHECK(sg_i1("GetGroupNum", 11) == 0, "%s: GetGroupNum(11)=%d without a general channel 11, want 0", what,
			 sg_i1("GetGroupNum", 11));
		SG_CHECK(!sg_i1("Stop", 11), "%s: Stop(11) returned true without a general channel 11", what);
		SG_CHECK(!sg_i1("Unprepare", 11), "%s: Unprepare(11) returned true without a general channel 11", what);
		SG_CHECK(ss_isplayse(11), "%s: the system sound of SE 11 stopped (Stop or Unprepare of the general ID)", what);
		ss_begin_take();
		ss_pull(4);
		int allowed[] = { 0, 1024 };
		if (ss_split(what, &m, allowed, 2)) {
			struct ss_pat p[] = { { 1024, 4 * SG_CHUNK, 4 * SG_CHUNK } };
			ss_match(what, p, 1);
		}
	}
}

/* ---- SE3: the defaults (PlaySpan 0, multiple play) ------------------------------------- */

static int se3_which;
static const char *const se3_names[] = {
	"the same name twice at once: both play",
	"the same name again five chunks later: both play",
	"StopSe stops both; a missing name plays nothing and stops nothing",
};

static void se3_defaults(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = se3_names[se3_which];
	struct ss_model m = ss_none;
	int id = 8;
	ss_begin_take();
	ss_pull(1);
	if (se3_which == 0) {
		SG_CHECK(ss_playse(id, "ss_se") && ss_playse(id, "ss_se"), "%s: PlaySe returned false", what);
		ss_pull(8);
		int allowed[] = { 0, 8192, 16384 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 16384, 3000, 3000 }, { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 3);
		}
	} else if (se3_which == 1) {
		SG_CHECK(ss_playse(id, "ss_long"), "%s: the first PlaySe returned false", what);
		ss_pull(5);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: the second PlaySe returned false", what);
		ss_pull(27);
		int allowed[] = { 0, 1024, 2048 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 1024, 4 * SG_CHUNK, 6 * SG_CHUNK }, { 2048, 16384 - 6 * SG_CHUNK, 16384 - 4 * SG_CHUNK },
					      { 1024, 4 * SG_CHUNK, 6 * SG_CHUNK }, { 0, SG_CHUNK, -1 } };
			if (ss_match(what, p, 5))
				SG_CHECK(ss_runs[1].len == ss_runs[3].len && ss_runs[1].len + ss_runs[2].len == 16384,
					 "%s: the two sounds do not both last 16384 frames (%d + %d, then %d)", what, ss_runs[1].len, ss_runs[2].len,
					 ss_runs[3].len);
		}
	} else {
		SG_CHECK(ss_playse(id, "ss_long") && ss_playse(id, "ss_long"), "%s: PlaySe returned false", what);
		ss_pull(3);
		SG_CHECK(!ss_playse(id, "ss_no_such_sound"), "%s: PlaySe of a missing name returned true", what);
		SG_CHECK(ss_isplayse(id), "%s: IsPlaySe is false after PlaySe of a missing name", what);
		ss_pull(2);
		SG_CHECK(ss_stopse(id), "%s: StopSe returned false", what);
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is true after StopSe", what);
		ss_pull(4);
		int allowed[] = { 0, 1024, 2048 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 2048, 2 * SG_CHUNK, 6 * SG_CHUNK }, { 0, 3 * SG_CHUNK, -1 } };
			ss_match(what, p, 3);
		}
		/* the entry stays: StopSe again is true, with nothing to stop */
		SG_CHECK(ss_stopse(id), "%s: a second StopSe returned false (the entry should stay)", what);
	}
	SG_CHECK(!ss_isplayse(id) || se3_which == 1, "%s: IsPlaySe is still true at the end", what);
}

/* ---- SE4: PlaySpan 40 with multiple play (SE 5 and 6) ------------------------------------ */

static int se4_which;
static const char *const se4_names[] = {
	"the same name at once is dropped",
	"the same name five chunks later plays too",
	"another name at once plays",
	"PlaySpan 200 counts a sound only while it plays",
	"PlaySpan 200: the same name 116 ms later is dropped, 278 ms later it plays too",
	"PlaySpan 200: every playing sound of the name counts, the second of two drops a third",
	"PlaySpan 200: a sound of the name that has ended does not stop the walk, the second still drops a third",
	"PlaySpan 40: a sound of another name before the name's in the entry does not end the walk, the same name at once is dropped",
	"PlaySpan 40: a name that is a prefix of another is another name, both play at once in either order",
	"the game's three rows in InitSystemSE's order: each ID keeps its own parameters and channels, StopSe of an ID without an entry stops nothing",
	"the game's three rows: StopSe and IsPlaySe of each ID leave the IDs whose entries came before and after it",
};

static void se4_span_multi(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = se4_names[se4_which];
	struct ss_model m = ss_none;
	int id = 5;
	if (se4_which == 3) {
		/* 0x49b790 looks at the position of a sound of the name only while
		 * it plays (vtable +0x18 first): a sound shorter than PlaySpan that
		 * has ended does not drop the next one, however soon it comes. */
		SG_CHECK(ss_param(id, 200, true), "%s: SetSeParam returned false", what);
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(id, "ss_short"), "%s: the first PlaySe returned false", what);
		ss_pull(4);
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is true after the 500 frames of the sound", what);
		SG_CHECK(ss_playse(id, "ss_short"), "%s: PlaySe of the same name after it ended (93 ms after the first, PlaySpan 200) "
			 "returned false", what);
		SG_CHECK(!ss_playse(id, "ss_short"), "%s: PlaySe of the same name while it plays (within 200 ms) returned true", what);
		ss_pull(6);
		int allowed[] = { 0, 4096, 8192 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 4096, 500, 500 }, { 0, SG_CHUNK, -1 }, { 4096, 500, 500 },
					      { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 5);
		}
		return;
	}
	if (se4_which == 4) {
		/* PlaySpan is in ms (the original engine sets 200 itself, 0x5feac0):
		 * five chunks (116 ms) after the first call it still drops the same
		 * name, twelve chunks (278 ms) after it does not; both far from the
		 * window's edge, also for the port's position (a chunk behind). */
		SG_CHECK(ss_param(id, 200, true), "%s: SetSeParam returned false", what);
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: the first PlaySe returned false", what);
		ss_pull(5);
		SG_CHECK(!ss_playse(id, "ss_long"), "%s: PlaySe of the same name 116 ms later (PlaySpan 200) returned true", what);
		ss_pull(7);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: PlaySe of the same name 278 ms later (PlaySpan 200) returned false", what);
		ss_pull(27);
		int allowed[] = { 0, 1024, 2048 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 1024, 11 * SG_CHUNK, 13 * SG_CHUNK },
					      { 2048, 16384 - 13 * SG_CHUNK, 16384 - 11 * SG_CHUNK }, { 1024, 11 * SG_CHUNK, 13 * SG_CHUNK },
					      { 0, SG_CHUNK, -1 } };
			if (ss_match(what, p, 5))
				SG_CHECK(ss_runs[1].len == ss_runs[3].len && ss_runs[1].len + ss_runs[2].len == 16384,
					 "%s: the two sounds do not both last 16384 frames (%d + %d, then %d)", what, ss_runs[1].len, ss_runs[2].len,
					 ss_runs[3].len);
		}
		return;
	}
	if (se4_which == 5) {
		/* 0x49b790 walks every channel of the name (0x49b7f6..0x49b8a9,
		 * lower_bound to upper_bound): any one that plays below PlaySpan
		 * drops the call (0x49b85a), not only the first. The second call
		 * comes twelve chunks (278 ms) after the first and plays beside it;
		 * the third one chunk later, when the first is past PlaySpan and
		 * the second is not (0 or 23 ms): dropped. Each call a chunk or
		 * more from the window's edge. Sample for sample: the two sounds
		 * overlap, no third one. */
		SG_CHECK(ss_param(id, 200, true), "%s: SetSeParam returned false", what);
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: the first PlaySe returned false", what);
		ss_pull(12);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: PlaySe of the same name 278 ms after the first (PlaySpan 200) returned false", what);
		ss_pull(1);
		SG_CHECK(!ss_playse(id, "ss_long"), "%s: PlaySe of the same name 23 ms after the second (PlaySpan 200; the first is past "
			 "it) returned true", what);
		ss_pull(26);
		int allowed[] = { 0, 1024, 2048 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 1024, 11 * SG_CHUNK, 13 * SG_CHUNK },
					      { 2048, 16384 - 13 * SG_CHUNK, 16384 - 11 * SG_CHUNK }, { 1024, 11 * SG_CHUNK, 13 * SG_CHUNK },
					      { 0, SG_CHUNK, -1 } };
			if (ss_match(what, p, 5))
				SG_CHECK(ss_runs[1].len == ss_runs[3].len && ss_runs[1].len + ss_runs[2].len == 16384,
					 "%s: the two sounds do not both last 16384 frames (%d + %d, then %d)", what, ss_runs[1].len, ss_runs[2].len,
					 ss_runs[3].len);
		}
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is still true at the end of the take", what);
		return;
	}
	if (se4_which == 6) {
		/* The walk goes on past a channel of the name that does not play
		 * (0x49b826 to the next one), it does not stop there. The first
		 * sound has ended and is not freed yet (nothing frees between the
		 * calls, 0x49ab10 runs after the walk); the second plays within
		 * PlaySpan: the third call is dropped. Twelve chunks (278 ms) after
		 * the first call the second; seven chunks (162 ms) after that the
		 * third, when the first has ended two chunks before with the usual
		 * lead of two chunks (checked below: a chunk or more before, which
		 * is enough for the mixer to have seen its end, so a lead up to a
		 * chunk longer passes too) and the second is 5 to 7 chunks (116 to
		 * 162 ms) in, 1.6 chunks or more from 200 ms. Sample for sample:
		 * the two sounds whole, no third one. */
		SG_CHECK(ss_param(id, 200, true), "%s: SetSeParam returned false", what);
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: the first PlaySe returned false", what);
		ss_pull(12);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: the second PlaySe (278 ms after the first) returned false", what);
		ss_pull(7);
		SG_CHECK(ss_isplayse(id), "%s: IsPlaySe is false while the second plays", what);
		SG_CHECK(!ss_playse(id, "ss_long"), "%s: PlaySe of the same name 162 ms after the second (the first has ended) returned true",
			 what);
		ss_pull(20);
		int allowed[] = { 0, 1024, 2048 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 1024, 11 * SG_CHUNK, 13 * SG_CHUNK },
					      { 2048, 16384 - 13 * SG_CHUNK, 16384 - 11 * SG_CHUNK }, { 1024, 11 * SG_CHUNK, 13 * SG_CHUNK },
					      { 0, SG_CHUNK, -1 } };
			if (ss_match(what, p, 5)) {
				SG_CHECK(ss_runs[1].len == ss_runs[3].len && ss_runs[1].len + ss_runs[2].len == 16384,
					 "%s: the two sounds do not both last 16384 frames (%d + %d, then %d)", what, ss_runs[1].len, ss_runs[2].len,
					 ss_runs[3].len);
				SG_CHECK(ss_runs[2].start + ss_runs[2].len <= 20 * SG_CHUNK - SG_CHUNK,
					 "%s: the first sound ends at take frame %d, not a chunk or more before the third call (%d)", what,
					 ss_runs[2].start + ss_runs[2].len, 20 * SG_CHUNK);
			}
		}
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is still true at the end of the take", what);
		return;
	}
	if (se4_which == 7) {
		/* The walk is over the name's own range of the entry (0x49b7cf and
		 * 0x49b7db: the first channel of the name and the one past its
		 * last, 0x49bbd0 and 0x49bb60, by the byte-wise name; the loop
		 * 0x49b7f6..0x49b8a9 runs between them), so a channel of another
		 * name is never in it, wherever it came. The port keeps an entry's
		 * channels in one list in the order they came and skips the other
		 * names': here another name first, then the name, then the name
		 * again at once (0 ms, PlaySpan 40): dropped. Sample for sample:
		 * the two sounds, no third one. */
		SG_CHECK(ss_param(id, 40, true), "%s: SetSeParam returned false", what);
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(id, "ss_se_b"), "%s: PlaySe of another name returned false", what);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: the first PlaySe of the name returned false", what);
		SG_CHECK(!ss_playse(id, "ss_long"), "%s: PlaySe of the name at once, another name's sound first in the entry, returned true",
			 what);
		ss_pull(19);
		int allowed[] = { 0, 1024, 3072 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 3072, 3000, 3000 }, { 1024, 16384 - 3000, 16384 - 3000 }, { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 4);
		}
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is still true at the end of the take", what);
		return;
	}
	if (se4_which == 8) {
		/* The name's range is found by the whole name, byte for byte:
		 * 0x49bbd0 and 0x49bb60 compare the bytes up to the shorter length
		 * with 0x41b480 (unsigned bytes, no case folding), then the lengths
		 * (0x49bc18), so a name that is a prefix of another is another
		 * name and neither drops the other. On SE 5 the prefix first, then
		 * the whole name; on SE 6 the whole name first, then the prefix;
		 * all at once (0 ms, PlaySpan 40: 1.7 chunks from its edge). Then
		 * each of the four again at once: dropped by its own sound. Sample
		 * for sample: the two sounds on each ID, no fifth one. */
		SG_CHECK(ss_param(id, 40, true) && ss_param(6, 40, true), "%s: SetSeParam returned false", what);
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(id, "ss_lon"), "%s: PlaySe(5) of the prefix returned false", what);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: PlaySe(5) of the whole name after a sound of its prefix returned false", what);
		SG_CHECK(ss_playse(6, "ss_long"), "%s: PlaySe(6) of the whole name returned false", what);
		SG_CHECK(ss_playse(6, "ss_lon"), "%s: PlaySe(6) of the prefix after a sound of the whole name returned false", what);
		bool again[4];
		again[0] = ss_playse(id, "ss_lon");
		again[1] = ss_playse(id, "ss_long");
		again[2] = ss_playse(6, "ss_long");
		again[3] = ss_playse(6, "ss_lon");
		SG_CHECK(!again[0] && !again[1] && !again[2] && !again[3],
			 "%s: PlaySe of each name again at once returned %d %d (SE 5), %d %d (SE 6), want all false (PlaySpan 40)", what,
			 again[0], again[1], again[2], again[3]);
		ss_pull(19);
		int allowed[] = { 0, 2048, 3072 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 3072, 3000, 3000 }, { 2048, 16384 - 3000, 16384 - 3000 }, { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 4);
		}
		SG_CHECK(!ss_isplayse(id) && !ss_isplayse(6), "%s: IsPlaySe(5) %d, IsPlaySe(6) %d at the end of the take, want both false", what,
			 ss_isplayse(id), ss_isplayse(6));
		return;
	}
	if (se4_which == 9) {
		/* Each SE function works on its own ID's entry: SetSeParam gets or
		 * makes that entry and writes PlaySpan and bMultiPlay there alone
		 * (0x4a6583, 0x4a6590, 0x4a6593); PlaySe walks and reuses among that
		 * entry's channels alone, with its bMultiPlay (0x4a3362 -> 0x499780,
		 * 0x4a3376 -> 0x49b790, 0x49b8b2); StopSe of an ID without an entry
		 * is false and frees nothing (0x4998ea). The game's three rows in
		 * InitSystemSE's order (5 and 6 with multiple play, then 7
		 * without), on a fresh table. At once: a long sound on SE 5, a
		 * voice on SE 7, and the long sound on SE 9, which has no entry yet
		 * (its walk is its own: it plays although SE 5's sound of the name
		 * is at 0 ms). Five chunks (116 ms) later, past PlaySpan by three
		 * chunks: the long sound on SE 5 again (alongside the first) and
		 * the voice on SE 7 again (the first started over); then StopSe(8),
		 * an ID without an entry. Sample for sample: the residual is
		 * 1024 x (long sounds playing) + 4096 x (voices playing); three
		 * whole long sounds, three at a time at the most, never two
		 * voices, the first voice cut, the second whole. */
		ss_game_params();
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(5, "ss_long") && ss_playse(7, "ss_v7"), "%s: PlaySe of SE 5 or SE 7 returned false", what);
		SG_CHECK(ss_playse(9, "ss_long"), "%s: PlaySe(9) of the name SE 5 plays at 0 ms returned false (SE 9 had no entry)", what);
		ss_pull(5);
		SG_CHECK(ss_playse(5, "ss_long"), "%s: PlaySe(5) of the same name 116 ms later returned false", what);
		SG_CHECK(ss_playse(7, "ss_v7"), "%s: PlaySe(7) of the same voice 116 ms later returned false", what);
		ss_pull(2);
		SG_CHECK(!ss_stopse(8) && !ss_isplayse(8), "%s: StopSe(8) or IsPlaySe(8) is true without an entry 8", what);
		SG_CHECK(ss_isplayse(5) && ss_isplayse(7) && ss_isplayse(9), "%s: IsPlaySe(5) %d, (7) %d, (9) %d after StopSe(8), want all true",
			 what, ss_isplayse(5), ss_isplayse(7), ss_isplayse(9));
		ss_pull(19);
		int allowed[8];
		for (int k = 0; k < 8; k++)
			allowed[k] = (k & 3) * 1024 + (k >> 2) * 4096;
		if (ss_split(what, &m, allowed, 8)) {
			int sum = 0, most = 0, voices = 0, voice_len[4] = { 0 }, prev = 0;
			for (int r = 0; r < ss_nr_runs; r++) {
				int longs = (ss_runs[r].value % 4096) / 1024, has = ss_runs[r].value >= 4096;
				sum += longs * ss_runs[r].len;
				if (longs > most)
					most = longs;
				if (has && !prev && voices < 4)
					voices++;
				if (has)
					voice_len[voices - 1] += ss_runs[r].len;
				prev = has;
			}
			SG_CHECK(sum == 3 * 16384 && most == 3, "%s: the long sounds sum to %d frames, at most %d at a time; want three whole "
				 "(%d), three at a time (SE 5's two alongside, SE 9's)", what, sum, most, 3 * 16384);
			SG_CHECK(voices == 2 && voice_len[0] >= 1 && voice_len[0] <= 5 * SG_CHUNK && voice_len[1] == 6000,
				 "%s: the voice on SE 7 plays %d time(s) (%d, %d frames), want it cut by the second PlaySe, then 6000 frames once",
				 what, voices, voice_len[0], voice_len[1]);
			if (sum != 3 * 16384 || most != 3 || voices != 2)
				ss_print_runs(what);
		}
		SG_CHECK(!ss_isplayse(5) && !ss_isplayse(7) && !ss_isplayse(9), "%s: IsPlaySe(5) %d, (7) %d, (9) %d at the end, want all false",
			 what, ss_isplayse(5), ss_isplayse(7), ss_isplayse(9));
		return;
	}
	if (se4_which == 10) {
		/* StopSe and IsPlaySe look the ID up and act on that entry alone
		 * (0x4998c0, 0x499920): the entries made before and after it are
		 * neither stopped nor seen. The game's three rows in InitSystemSE's
		 * order (SE 5's entry first); at once a long sound on SE 5, another
		 * on SE 6 and a voice on SE 7. Two chunks later StopSe(7) (SE 5 and 6
		 * play on, IsPlaySe(7) false) and the voice again; two chunks later
		 * StopSe(6) (SE 5 and the voice play on, IsPlaySe(6) false). Sample
		 * for sample: SE 5's sound whole, SE 6's cut, the voice cut, then
		 * whole once. */
		ss_game_params();
		ss_begin_take();
		ss_pull(1);
		SG_CHECK(ss_playse(5, "ss_long") && ss_playse(6, "ss_g2") && ss_playse(7, "ss_v7"), "%s: PlaySe of SE 5, 6 or 7 returned false",
			 what);
		ss_pull(2);
		SG_CHECK(ss_stopse(7), "%s: StopSe(7) returned false", what);
		SG_CHECK(ss_isplayse(5) && ss_isplayse(6) && !ss_isplayse(7), "%s: after StopSe(7) IsPlaySe(5) %d, (6) %d, (7) %d, want 1 1 0",
			 what, ss_isplayse(5), ss_isplayse(6), ss_isplayse(7));
		SG_CHECK(ss_playse(7, "ss_v7"), "%s: PlaySe(7) after StopSe(7) returned false", what);
		ss_pull(2);
		SG_CHECK(ss_stopse(6), "%s: StopSe(6) returned false", what);
		SG_CHECK(ss_isplayse(5) && !ss_isplayse(6) && ss_isplayse(7), "%s: after StopSe(6) IsPlaySe(5) %d, (6) %d, (7) %d, want 1 0 1",
			 what, ss_isplayse(5), ss_isplayse(6), ss_isplayse(7));
		ss_pull(17);
		int allowed[8];
		for (int k = 0; k < 8; k++)
			allowed[k] = (k & 1 ? 1024 : 0) + (k & 2 ? 4096 : 0) + (k & 4 ? 8192 : 0);
		if (ss_split(what, &m, allowed, 8)) {
			static const int bits[] = { 1024, 8192, 4096 };
			int segs[3] = { 0 }, len[3][4] = { { 0 } };
			for (int b = 0; b < 3; b++) {
				int prev = 0;
				for (int r = 0; r < ss_nr_runs; r++) {
					int has = (ss_runs[r].value & bits[b]) != 0;
					if (has && !prev && segs[b] < 4)
						segs[b]++;
					if (has)
						len[b][segs[b] - 1] += ss_runs[r].len;
					prev = has;
				}
			}
			SG_CHECK(segs[0] == 1 && len[0][0] == 16384, "%s: SE 5's sound plays %d time(s), first %d frames; want once, whole (16384)",
				 what, segs[0], len[0][0]);
			SG_CHECK(segs[1] == 1 && len[1][0] >= 1 && len[1][0] <= 4 * SG_CHUNK, "%s: SE 6's sound plays %d time(s), first %d frames; "
				 "want once, cut by StopSe(6)", what, segs[1], len[1][0]);
			SG_CHECK(segs[2] == 2 && len[2][0] >= 1 && len[2][0] <= 2 * SG_CHUNK && len[2][1] == 6000,
				 "%s: the voice on SE 7 plays %d time(s) (%d, %d frames), want it cut by StopSe(7), then 6000 frames once", what,
				 segs[2], len[2][0], len[2][1]);
			if (segs[0] != 1 || segs[1] != 1 || segs[2] != 2)
				ss_print_runs(what);
		}
		SG_CHECK(!ss_isplayse(5) && !ss_isplayse(6) && !ss_isplayse(7), "%s: IsPlaySe(5) %d, (6) %d, (7) %d at the end, want all false",
			 what, ss_isplayse(5), ss_isplayse(6), ss_isplayse(7));
		return;
	}
	SG_CHECK(ss_param(id, 40, true), "%s: SetSeParam returned false", what);
	ss_begin_take();
	ss_pull(1);
	SG_CHECK(ss_playse(id, "ss_long"), "%s: the first PlaySe returned false", what);
	if (se4_which == 0) {
		SG_CHECK(!ss_playse(id, "ss_long"), "%s: PlaySe of the same name within 40 ms returned true", what);
		ss_pull(19);
		int allowed[] = { 0, 1024, 2048 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 1024, 16384, 16384 }, { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 3);
		}
	} else if (se4_which == 1) {
		ss_pull(5);
		SG_CHECK(ss_playse(id, "ss_long"), "%s: PlaySe of the same name 116 ms later returned false", what);
		ss_pull(27);
		int allowed[] = { 0, 1024, 2048 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 1024, 4 * SG_CHUNK, 6 * SG_CHUNK }, { 2048, 16384 - 6 * SG_CHUNK, 16384 - 4 * SG_CHUNK },
					      { 1024, 4 * SG_CHUNK, 6 * SG_CHUNK }, { 0, SG_CHUNK, -1 } };
			if (ss_match(what, p, 5))
				SG_CHECK(ss_runs[1].len == ss_runs[3].len && ss_runs[1].len + ss_runs[2].len == 16384,
					 "%s: the two sounds do not both last 16384 frames (%d + %d, then %d)", what, ss_runs[1].len, ss_runs[2].len,
					 ss_runs[3].len);
		}
	} else {
		SG_CHECK(ss_playse(id, "ss_se_b"), "%s: PlaySe of another name within 40 ms returned false", what);
		ss_pull(8);
		/* IsPlaySe looks at every channel of the entry (0x49ad40 walks
		 * them all): the other name's sound has ended a chunk or more
		 * before (3000 frames from a lead of at most four chunks), the
		 * first still plays (16384 frames). */
		SG_CHECK(ss_isplayse(id), "%s: IsPlaySe is false while the first sound plays, after the other name's has ended", what);
		ss_pull(11);
		int allowed[] = { 0, 1024, 3072 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 3072, 3000, 3000 }, { 1024, 16384 - 3000, 16384 - 3000 }, { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 4);
		}
	}
}

/* ---- SE5: PlaySpan 40 without multiple play (SE 7) ----------------------------------------- */

/* The take as segments of the ramp sound (ss_ramp: frame k holds k+1): each
 * a run of frames from 0 in order, silence before, between (at most two
 * chunks) and after. */
static int ss_seg_len[8], ss_nr_segs;
static bool ss_ramp_segments(const char *what)
{
	bool whole = true;
	ss_nr_segs = 0;
	int want = 0, gap = 0;
	for (int i = 0; i < ss_at; i++) {
		int q = ss_q(i, &whole);
		if (!whole) {
			SG_CHECK(false, "%s: take frame %d is not a whole sample", what, i);
			return false;
		}
		if (q == 0) {
			if (want > 0 && ss_nr_segs < 8)
				ss_seg_len[ss_nr_segs++] = want;
			want = 0;
			gap++;
			continue;
		}
		if (q == 1 && want > 0 && ss_nr_segs < 8) {
			/* starts over without a gap */
			ss_seg_len[ss_nr_segs++] = want;
			want = 0;
		}
		if (q != want + 1) {
			SG_CHECK(false, "%s: take frame %d is %d, want ramp frame %d (%s)", what, i, q - 1, want,
				 q > want + 1 ? "a sum: two of it at once" : "it went back");
			return false;
		}
		if (want == 0 && ss_nr_segs > 0 && gap > 2 * SG_CHUNK) {
			SG_CHECK(false, "%s: %d silent frames before it starts over", what, gap);
			return false;
		}
		gap = 0;
		want++;
	}
	if (want > 0 && ss_nr_segs < 8)
		ss_seg_len[ss_nr_segs++] = want;
	return true;
}

static int se5_which;
static const char *const se5_names[] = {
	"the same name at once is dropped, the first not started over",
	"the same name five chunks later starts the sound over (not twice at once)",
	"another name plays alongside",
	"StopSe, then another name (a battle voice after the previous one)",
	"the same name after its sound has ended: that channel is taken again and plays it once more",
	"StopSe of the ID leaves another ID's sound and its own parameters",
};

static void se5_span_single(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = se5_names[se5_which];
	int id = 7;
	SG_CHECK(ss_param(id, 40, false), "%s: SetSeParam returned false", what);
	ss_begin_take();
	ss_pull(1);
	if (se5_which == 0) {
		SG_CHECK(ss_playse(id, "ss_ramp"), "%s: the first PlaySe returned false", what);
		ss_pull(1);
		SG_CHECK(!ss_playse(id, "ss_ramp"), "%s: PlaySe of the same name within 40 ms returned true", what);
		ss_pull(12);
		if (ss_ramp_segments(what))
			SG_CHECK(ss_nr_segs == 1 && ss_seg_len[0] == 8000, "%s: %d segment(s), the first of %d frames; want it once, 8000 frames",
				 what, ss_nr_segs, ss_seg_len[0]);
	} else if (se5_which == 1) {
		SG_CHECK(ss_playse(id, "ss_ramp"), "%s: the first PlaySe returned false", what);
		ss_pull(5);
		SG_CHECK(ss_playse(id, "ss_ramp"), "%s: PlaySe of the same name 116 ms later returned false", what);
		ss_pull(14);
		if (ss_ramp_segments(what))
			SG_CHECK(ss_nr_segs == 2 && ss_seg_len[0] >= 3 * SG_CHUNK && ss_seg_len[0] < 8000 && ss_seg_len[1] == 8000,
				 "%s: %d segment(s) of %d and %d frames; want the first cut after 3072..7999 frames, then all 8000 again", what,
				 ss_nr_segs, ss_seg_len[0], ss_nr_segs > 1 ? ss_seg_len[1] : 0);
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is still true at the end", what);
	} else if (se5_which == 2) {
		SG_CHECK(ss_playse(id, "ss_ramp"), "%s: the first PlaySe returned false", what);
		ss_pull(2);
		SG_CHECK(ss_playse(id, "ss_se"), "%s: PlaySe of another name returned false", what);
		ss_pull(12);
		struct ss_model m = ss_none;
		bool whole = true;
		for (int i = 0; i < ss_at && m.ramp_start < 0; i++)
			if (ss_q(i, &whole) != 0)
				m.ramp_start = i;
		m.ramp_len = 8000;
		int allowed[] = { 0, 8192 };
		if (m.ramp_start < 0) {
			SG_CHECK(false, "%s: no sound in the take", what);
		} else if (ss_split(what, &m, allowed, 2)) {
			struct ss_pat p[] = { { 0, SG_CHUNK, -1 }, { 8192, 3000, 3000 }, { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 3);
		}
	} else if (se5_which == 3) {
		SG_CHECK(ss_playse(id, "ss_v7"), "%s: the first voice's PlaySe returned false", what);
		ss_pull(2);
		SG_CHECK(ss_stopse(id), "%s: StopSe returned false", what);
		SG_CHECK(ss_playse(id, "ss_v7b"), "%s: the second voice's PlaySe returned false", what);
		ss_pull(8);
		struct ss_model m = ss_none;
		int allowed[] = { 0, 4096, 8192 };
		if (ss_split(what, &m, allowed, 3)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 4096, 1, 3 * SG_CHUNK }, { 0, 0, 2 * SG_CHUNK }, { 8192, 3000, 3000 },
					      { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 5);
		}
	} else if (se5_which == 4) {
		/* Without bMultiPlay the name's first channel is taken again
		 * whether it plays or has ended (0x49b8b2..0x49b8e4: no IsPlay on
		 * that path; the collection keeps it, 0x49b8b8 pushes it as the one
		 * not to free, and it is stopped and prepared again). Nothing frees
		 * the ended channel between the calls (0x49ab10 runs in PlaySe), so
		 * the second call takes it again. The first sound (3000 frames)
		 * has ended a chunk or more before the second call, two with the
		 * usual lead (seven chunks after the first, PlaySpan 40 long past).
		 * Sample for sample: the sound twice, whole, silence between, never
		 * two at once. */
		SG_CHECK(ss_playse(id, "ss_se"), "%s: the first PlaySe returned false", what);
		ss_pull(7);
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is true seven chunks after PlaySe of a sound of 3000 frames", what);
		SG_CHECK(ss_playse(id, "ss_se"), "%s: PlaySe of the same name after its sound ended returned false", what);
		ss_pull(7);
		struct ss_model m = ss_none;
		int allowed[] = { 0, 8192 };
		if (ss_split(what, &m, allowed, 2)) {
			struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 8192, 3000, 3000 }, { 0, SG_CHUNK, -1 }, { 8192, 3000, 3000 },
					      { 0, SG_CHUNK, -1 } };
			ss_match(what, p, 5);
		}
		SG_CHECK(!ss_isplayse(id), "%s: IsPlaySe is still true at the end", what);
	} else {
		/* StopSe looks the ID up and frees that entry's channels only
		 * (0x4998c0 -> 0x49a9b0, which writes neither PlaySpan nor
		 * bMultiPlay): a sound of another ID plays on, IsPlaySe of the
		 * stopped ID and of an ID without an entry are false while it
		 * does, and the stopped ID keeps its PlaySpan (the same name at
		 * once is still dropped). SE 6 plays a long sound, SE 7 a short
		 * one beside it; StopSe(7) two chunks later. Sample for sample:
		 * the long sound whole, the short one cut, then once more. */
		SG_CHECK(ss_param(6, 40, true), "%s: SetSeParam(6) returned false", what);
		SG_CHECK(ss_playse(6, "ss_long") && ss_playse(id, "ss_se"), "%s: PlaySe of SE 6 or 7 returned false", what);
		ss_pull(2);
		SG_CHECK(ss_stopse(id), "%s: StopSe(7) returned false", what);
		SG_CHECK(ss_isplayse(6), "%s: IsPlaySe(6) is false after StopSe(7)", what);
		SG_CHECK(!ss_isplayse(id) && !ss_isplayse(9), "%s: IsPlaySe(7) %d, IsPlaySe(9) %d while only SE 6 plays, want both false",
			 what, ss_isplayse(id), ss_isplayse(9));
		SG_CHECK(ss_playse(id, "ss_se"), "%s: PlaySe(7) after StopSe(7) returned false", what);
		SG_CHECK(!ss_playse(id, "ss_se"), "%s: PlaySe(7) of the same name at once after StopSe(7) returned true (PlaySpan 40 stays)",
			 what);
		ss_pull(19);
		struct ss_model m = ss_none;
		int allowed[] = { 0, 1024, 8192, 9216 };
		if (ss_split(what, &m, allowed, 4)) {
			int on = -1, end = -1, gaps = 0, segs = 0, seg_len[4] = { 0 }, prev = 0;
			for (int r = 0; r < ss_nr_runs; r++) {
				if (ss_runs[r].value & 1024) {
					if (on < 0)
						on = ss_runs[r].start;
					else if (end != ss_runs[r].start)
						gaps++;
					end = ss_runs[r].start + ss_runs[r].len;
				}
				int has = (ss_runs[r].value & 8192) != 0;
				if (has && !prev && segs < 4)
					segs++;
				if (has && segs <= 4)
					seg_len[segs - 1] += ss_runs[r].len;
				prev = has;
			}
			SG_CHECK(on >= 0 && !gaps && end - on == 16384, "%s: SE 6's sound is not whole (frames %d to %d, %d gap(s))", what, on,
				 end, gaps);
			SG_CHECK(segs == 2 && seg_len[0] >= 1 && seg_len[0] <= 2 * SG_CHUNK && seg_len[1] == 3000,
				 "%s: SE 7's sound plays %d time(s) (%d, %d frames), want it cut by StopSe, then 3000 frames once", what, segs,
				 seg_len[0], seg_len[1]);
			if (gaps || segs != 2)
				ss_print_runs(what);
		}
		SG_CHECK(!ss_isplayse(6) && !ss_isplayse(id), "%s: IsPlaySe(6) %d, IsPlaySe(7) %d at the end, want both false", what,
			 ss_isplayse(6), ss_isplayse(id));
	}
}

/* ---- SE6: the game's table, a battle's sequence over two looping general channels ----------- */

static void se6_battle(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = "the game's table and a battle's sequence";
	ss_game_params();
	if (!ss_loop_on(5, "ss_hum") || !ss_loop_on(7, "ss_loop"))
		return;
	ss_begin_take();
	ss_pull(1);
	SG_CHECK(ss_playse(5, "ss_se_b"), "%s: the cursor sound on SE 5 returned false", what);
	ss_pull(1);
	SG_CHECK(ss_playse(7, "ss_v7"), "%s: a voice on SE 7 returned false", what);
	ss_pull(3);
	SG_CHECK(ss_stopse(7) && ss_playse(7, "ss_v7b"), "%s: StopSe(7), then the next voice on SE 7 returned false", what);
	ss_pull(2);
	SG_CHECK(ss_playse(5, "ss_se_b"), "%s: the cursor sound on SE 5 again returned false", what);
	ss_pull(2);
	SG_CHECK(ss_playse(6, "ss_se_c"), "%s: a mark sound on SE 6 returned false", what);
	ss_pull(9);
	/* The residual must be a sum of the four system sounds' values. */
	struct ss_model m = ss_none;
	m.hum = 64;
	m.loop_p0 = ss_loop_phase(what, 64);
	if (m.loop_p0 < 0)
		return;
	int allowed[16];
	for (int k = 0; k < 16; k++)
		allowed[k] = (k & 1 ? 2048 : 0) + (k & 2 ? 4096 : 0) + (k & 4 ? 8192 : 0) + (k & 8 ? 16384 : 0);
	if (ss_split(what, &m, allowed, 16)) {
		/* Each sound's own runs. */
		static const struct { int bit, nr; const char *name; } sounds[] = {
			{ 2048, 2, "the cursor sound" }, { 4096, 1, "the first voice" }, { 8192, 1, "the second voice" }, { 16384, 1, "the mark sound" },
		};
		int voice_end = -1;
		for (int s = 0; s < SG_COUNT(sounds); s++) {
			int starts[4], lens[4], n = 0, on = -1;
			for (int r = 0; r <= ss_nr_runs; r++) {
				bool has = r < ss_nr_runs && (ss_runs[r].value & sounds[s].bit);
				if (has && on < 0)
					on = ss_runs[r].start;
				if (!has && on >= 0) {
					int end = r < ss_nr_runs ? ss_runs[r].start : ss_at;
					if (n < 4) {
						starts[n] = on;
						lens[n] = end - on;
					}
					n++;
					on = -1;
				}
			}
			if (n != sounds[s].nr) {
				SG_CHECK(false, "%s: %s plays %d time(s), want %d", what, sounds[s].name, n, sounds[s].nr);
				ss_print_runs(what);
				continue;
			}
			for (int k = 0; k < n && k < 4; k++) {
				if (sounds[s].bit == 4096) {
					SG_CHECK(lens[k] >= SG_CHUNK && lens[k] <= 4 * SG_CHUNK, "%s: the first voice lasts %d frames, want it cut by StopSe "
						 "after 1024..4096", what, lens[k]);
					voice_end = starts[k] + lens[k];
				} else {
					SG_CHECK(lens[k] == 3000, "%s: %s (%d) lasts %d frames, want 3000", what, sounds[s].name, k + 1, lens[k]);
				}
				if (sounds[s].bit == 8192)
					SG_CHECK(voice_end >= 0 && starts[k] >= voice_end && starts[k] <= voice_end + 2 * SG_CHUNK,
						 "%s: the second voice starts at %d, want at the first's end %d or up to two chunks after", what,
						 starts[k], voice_end);
			}
		}
	}
	SG_CHECK(sg_i1("IsPlay", 5) && sg_i1("IsPlay", 7), "%s: a looping general channel stopped (IsPlay(5) %d, IsPlay(7) %d)", what,
		 sg_i1("IsPlay", 5), sg_i1("IsPlay", 7));
	SG_CHECK(!ss_isplayse(5) && !ss_isplayse(6) && !ss_isplayse(7), "%s: IsPlaySe(5/6/7) %d/%d/%d at the end, want all false", what,
		 ss_isplayse(5), ss_isplayse(6), ss_isplayse(7));
	printf("  SE6 the game's table and a battle's sequence checked\n");
}

/* ---- SE7: GetFreeSeID and IsExistSeID ---------------------------------------------------- */

static void se7_free_exist(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	/* General channels 0..3 are not SE entries. */
	for (int id = 0; id <= 3; id++)
		SG_CHECK(sg_prepare(id, "ss_hum", false), "cannot prepare the general channel %d", id);
	SG_CHECK(sg_i2("SetLoopCount", 1, 0) && sg_i1("Play", 1), "cannot loop the general channel 1");
	SG_CHECK(ss_free(1, 10) == 1, "GetFreeSeID(1, 10)=%d with general channels 0..3 only, want 1", ss_free(1, 10));
	SG_CHECK(ss_free(4, 4) == 0, "GetFreeSeID(4, 4)=%d, want 0 (an empty range)", ss_free(4, 4));
	/* SetSeParam makes an entry, PlaySe makes one, StopSe keeps it. */
	SG_CHECK(ss_param(1, 40, true), "SetSeParam(1) returned false");
	SG_CHECK(ss_free(1, 10) == 2, "GetFreeSeID(1, 10)=%d after SetSeParam(1), want 2", ss_free(1, 10));
	SG_CHECK(ss_playse(2, "ss_se"), "PlaySe(2) returned false");
	SG_CHECK(ss_free(1, 10) == 3, "GetFreeSeID(1, 10)=%d after PlaySe(2), want 3", ss_free(1, 10));
	sg_pull(ss_scratch, 4 * SG_CHUNK);
	sg_pull(ss_scratch, 4 * SG_CHUNK);
	SG_CHECK(!ss_isplayse(2), "IsPlaySe(2) is true after its sound ended");
	SG_CHECK(ss_stopse(2), "StopSe(2) returned false for an entry with nothing playing");
	SG_CHECK(ss_free(1, 10) == 3, "GetFreeSeID(1, 10)=%d after StopSe(2), want 3 (the entry stays)", ss_free(1, 10));
	SG_CHECK(ss_free(1, 3) == 0, "GetFreeSeID(1, 3)=%d with entries 1 and 2, want 0", ss_free(1, 3));
	SG_CHECK(ss_free(2, 5) == 3, "GetFreeSeID(2, 5)=%d, want 3", ss_free(2, 5));
	SG_CHECK(ss_free(3, 4) == 3, "GetFreeSeID(3, 4)=%d, want 3", ss_free(3, 4));
	/* PlaySe of a missing name still makes the entry: 0x4a3340 gets or makes
	 * it (0x499780) before 0x4a3460 fails on the name. */
	SG_CHECK(ss_free(5, 6) == 5, "GetFreeSeID(5, 6)=%d before any call on 5, want 5", ss_free(5, 6));
	SG_CHECK(!ss_playse(5, "ss_no_such_sound"), "PlaySe(5) of a missing name returned true");
	SG_CHECK(ss_free(5, 6) == 0, "GetFreeSeID(5, 6)=%d after PlaySe(5) of a missing name, want 0 (the entry is made first)",
		 ss_free(5, 6));
	SG_CHECK(!ss_isplayse(5) && ss_stopse(5), "after PlaySe(5) of a missing name: IsPlaySe %d, StopSe %d, want false and true",
		 ss_isplayse(5), ss_stopse(5));
	/* IsPlaySe and StopSe only look the ID up (0x499920 and 0x4998c0:
	 * false when it has no entry): they make none. */
	SG_CHECK(!ss_isplayse(6) && !ss_stopse(6), "IsPlaySe(6) or StopSe(6) is true without an entry 6");
	SG_CHECK(ss_free(6, 7) == 6, "GetFreeSeID(6, 7)=%d after IsPlaySe(6) and StopSe(6) without an entry, want 6 (they make none)",
		 ss_free(6, 7));
	/* The search compares the ID with Max for equality (0x499754 jne), so
	 * with Min > Max it goes up from Min: the first ID without an entry. */
	SG_CHECK(ss_free(10, 5) == 10, "GetFreeSeID(10, 5)=%d without an entry 10, want 10 (the ID goes up from Min until it is Max)",
		 ss_free(10, 5));
	/* IsExistSeID is GetFreeSeID(ID, ID) != ID: true but for 0 (0x4a14c5). */
	SG_CHECK(ss_exist(1) && ss_exist(2), "IsExistSeID(1) %d, (2) %d, want true", ss_exist(1), ss_exist(2));
	SG_CHECK(ss_exist(3) && ss_exist(9), "IsExistSeID(3) %d, (9) %d without entries, want true (the original's arithmetic)", ss_exist(3),
		 ss_exist(9));
	SG_CHECK(!ss_exist(0), "IsExistSeID(0) is true, want false");
	SG_CHECK(sg_i1("IsPlay", 1), "the general channel 1 stopped");
	printf("  SE7 GetFreeSeID and IsExistSeID checked\n");
}

/* ---- SE8: a PlaySe sound's group and internal volume (guard) ----------------------------- */

static void se8_gain(void)
{
	struct sg_opts o = ss_group_opts;
	o.ds = SS_DS_KEYS;
	sg_setup(&o);
	if (!sg_group_mode_ok())
		return;
	int vol[SG_NR_GROUPS], mute[SG_NR_GROUPS];
	gn_set_volumes(vol, mute);
	struct { const char *name; int group; } s[] = {
		{ "ss_g2", 2 }, { "ss_g1", 1 }, { "ss_norow", 2 }, { "ss_v6", 6 },
	};
	for (int i = 0; i < SG_COUNT(s); i++) {
		int id = 20 + i;
		SG_CHECK(ss_playse(id, s[i].name), "PlaySe(%d, %s) returned false", id, s[i].name);
		float got = sg_level(), in = gn_internal(GN_DS_KEYS, s[i].group);
		float want = 0.25f * sg_chain(vol, mute, s[i].group) * in;
		SG_CHECK(SG_NEAR(got, want), "PlaySe of %s (group %d): %.5f, want %.5f (chain %.4f x its group's key %.4f; the movie key %.4f gives %.5f)",
			 s[i].name, s[i].group, got, want, sg_chain(vol, mute, s[i].group), in, GN_IN_MOVIE,
			 0.25f * sg_chain(vol, mute, s[i].group) * GN_IN_MOVIE);
		ss_stopse(id);
	}
	SG_CHECK(ss_playse(30, "ss_g2"), "PlaySe(30, ss_g2) returned false");
	sg_level();
	sg_setvol(2, 30); vol[2] = 30;
	float got = sg_level(), want = 0.25f * sg_chain(vol, mute, 2) * gn_internal(GN_DS_KEYS, 2);
	SG_CHECK(SG_NEAR(got, want), "group 2 at 30 while a PlaySe sound plays: %.5f, want %.5f", got, want);
	sg_setmute(2, true);
	got = sg_level();
	SG_CHECK(got == 0.0f, "group 2 muted while a PlaySe sound plays: %.5f, want 0", got);
	sg_setmute(2, false);
	ss_stopse(30);
	printf("  SE8 groups and internal volumes of PlaySe sounds checked\n");
}

/* ---- SE9: every other game keeps the shared pool ------------------------------------------- */

static int se9_which;
static const struct { const char *what; struct sg_opts o; } se9_cases[] = {
	{ "v13", { SS_DS_GROUPS, 13, SG_GBK, false, false, false, NULL, false } },
	{ "v14 with the SJIS rule", { SS_DS_GROUPS, 14, SG_SJIS, false, false, false, NULL, false } },
	{ "v15", { SS_DS_GROUPS, 15, SG_GBK, false, false, false, NULL, false } },
	{ "v14 with the GB18030 flag only", { SS_DS_GROUPS, 14, SG_FLAG_ONLY, false, false, false, NULL, false } },
	{ "v14 GBK without the group table", { SS_DS_NOTABLE, 14, SG_GBK, false, false, false, NULL, false } },
	{ "v14 GBK with a group table of no rows", { SS_DS_NOROWS, 14, SG_GBK, false, false, false, NULL, false } },
	{ "v13 with INI mixers", { SS_DS_GROUPS, 13, SG_GBK, false, false, true, NULL, false } },
};

/* Before this change PlaySe prepared the general channel of its ID: it took
 * the place of what played there, IsPlaySe, IsExistSeID and StopSe were the
 * general channel's, SetSeParam did nothing and GetFreeSeID was GetFreeID.
 * Kept. */
static void se9_legacy(void)
{
	sg_setup(&se9_cases[se9_which].o);
	const char *v = se9_cases[se9_which].what;
	int nr = se9_cases[se9_which].o.ini_mixers ? 4 : 3;
	SG_CHECK(sg_numof() == nr, "%s: GetMixerNumof=%d, want the %d legacy mixers", v, sg_numof(), nr);
	/* the INI's mixers at 100 too (the INI gives 50 to the one sounds play in) */
	for (int g = 0; g < nr; g++)
		SG_CHECK(sg_setvol(g, 100), "%s: SetMixerVolume(%d, 100) failed", v, g);
	if (!ss_loop_on(7, "ss_loop"))
		return;
	ss_begin_take();
	ss_pull(1);
	SG_CHECK(ss_playse(7, "ss_se"), "%s: PlaySe(7) returned false", v);
	ss_pull(9);
	/* the loop is gone: the system sound alone, then silence */
	bool whole = true;
	int alone = 0, best = 0;
	for (int i = SG_CHUNK; i < ss_at; i++) {
		alone = ss_q(i, &whole) == 8192 ? alone + 1 : 0;
		best = alone > best ? alone : best;
	}
	SG_CHECK(best >= SG_CHUNK, "%s: no chunk of the system sound alone (longest %d frames): the general loop 7 should have given way", v, best);
	for (int i = ss_at - 2 * SG_CHUNK; i < ss_at; i++)
		if (ss_q(i, &whole) != 0) {
			SG_CHECK(false, "%s: take frame %d is %d, want silence: the general loop 7 should be gone", v, i, ss_q(i, &whole));
			break;
		}
	SG_CHECK(!sg_i1("IsPlay", 7), "%s: IsPlay(7) is true, want the general channel taken by the system sound and ended", v);
	if (!ss_loop_on(9, "ss_hum"))
		return;
	SG_CHECK(ss_isplayse(9), "%s: IsPlaySe(9) is false while the general channel 9 plays (the shared pool)", v);
	SG_CHECK(ss_exist(9), "%s: IsExistSeID(9) is false while the general channel 9 plays (the shared pool)", v);
	SG_CHECK(!ss_exist(12), "%s: IsExistSeID(12) is true with nothing on 12 (the shared pool's IsPlay)", v);
	SG_CHECK(ss_stopse(9) && !sg_i1("IsPlay", 9), "%s: StopSe(9) did not stop the general channel 9", v);
	SG_CHECK(ss_free(1, 10) == sg_i2("GetFreeID", 1, 10), "%s: GetFreeSeID(1, 10)=%d, GetFreeID(1, 10)=%d, want the same", v,
		 ss_free(1, 10), sg_i2("GetFreeID", 1, 10));
	SG_CHECK(ss_param(5, 40, false), "%s: SetSeParam returned false", v);
	ss_begin_take();
	SG_CHECK(ss_playse(5, "ss_long") && ss_playse(5, "ss_long"), "%s: PlaySe(5) twice returned false", v);
	ss_pull(6);
	int top = 0;
	for (int i = 0; i < ss_at; i++)
		top = ss_q(i, &whole) > top ? ss_q(i, &whole) : top;
	SG_CHECK(top == 1024, "%s: the same sound twice on 5 peaks at %d, want 1024 (the second takes the first's place)", v, top);
	ss_stopse(5);
}

/* ---- SE10: unknown declaration shapes keep the library table's functions ------------------ */

static void se10_shapes(void)
{
	extern struct static_library lib_KiwiSoundEngine;
	sg_setup(&ss_group_opts);
	struct ain_library *kiwi = &ain->libraries[sg_lib];
	static struct ain_type t_int = { .data = AIN_INT, .struc = -1 }, t_float = { .data = AIN_FLOAT, .struc = -1 },
			       t_string = { .data = AIN_STRING, .struc = -1 };
	struct { const char *fn; int how; } u[] = {
		{ "PlaySe", 0 },	/* one argument fewer */
		{ "SetSeParam", 1 },	/* float PlaySpan */
		{ "IsPlaySe", 2 },	/* returns int */
		{ "StopSe", 3 },	/* a string SeID */
		{ "GetFreeSeID", 0 },	/* one argument fewer */
		{ "IsExistSeID", 2 },	/* returns int */
	};
	for (unsigned i = 0; i < SG_COUNT(u); i++) {
		int f = sg_fno(u[i].fn);
		struct ain_hll_function decl = kiwi->functions[f];
		struct ain_hll_argument args[8];
		memcpy(args, decl.arguments, sizeof(args[0]) * decl.nr_arguments);
		decl.arguments = args;
		if (u[i].how == 0)
			decl.nr_arguments--;
		else if (u[i].how == 1)
			args[1].type = t_float;
		else if (u[i].how == 2)
			decl.return_type = t_int;
		else
			args[0].type = t_string;
		struct ain_library fake = { .name = kiwi->name, .nr_functions = 1, .functions = &decl };
		struct hll_function *linked = link_static_library(&fake, &lib_KiwiSoundEngine);
		SG_CHECK(linked[0].fun == sg_orig(u[i].fn), "an unknown shape of %s is not bound to the library table's function", u[i].fn);
	}
	printf("  SE10 declaration shapes checked\n");
}

/* ---- SE11: the real audio thread ------------------------------------------------------------- */

static void se11_thread(void)
{
	struct sg_opts o = ss_group_opts;
	o.dummy_audio = true;
	sg_setup(&o);
	bool groups = sg_group_mode_ok();
	SG_CHECK(SDL_WasInit(SDL_INIT_AUDIO), "the dummy audio device did not start");
	if (!ss_loop_on(8, "ss_loop"))
		return;
	ss_game_params();
	static const char *const names[] = { "ss_se", "ss_se_b", "ss_long", "ss_v7", "ss_v7b", "ss_ramp" };
	srand(11);
	for (int i = 0; i < 300; i++) {
		int id = 5 + rand() % 3;
		switch (rand() % 9) {
		case 0: case 1: case 2: ss_playse(id, names[rand() % SG_COUNT(names)]); break;
		case 3: ss_stopse(id); break;
		case 4: ss_isplayse(id); ss_free(1, 20); break;
		case 5: ss_param(id, rand() % 2 ? 40 : 0, rand() & 1); break;
		case 6: sg_play_loop(id, "ss_hum", rand() % 3); break;
		case 7: sg_i1("Stop", id); break;
		default: sg_i1("IsPlay", id); break;
		}
		if (i % 4 == 0)
			SDL_Delay(1);
	}
	for (int id = 5; id <= 7; id++) {
		ss_stopse(id);
		sg_i1("Stop", id);
		SG_CHECK(!ss_isplayse(id) && !sg_i1("IsPlay", id), "SE/general %d still plays after StopSe and Stop", id);
	}
	SG_CHECK(sg_i1("IsPlay", 8), "the general loop 8 stopped");
	for (int id = 5; id <= 8; id++)
		sg_i1("Unprepare", id);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	printf("  SE11 300 calls against the audio thread (groups %s)\n", groups ? "on" : "off");
}

/* ---- SE12: a system sound is on no general ID, not only not on its own ------------------- */

/* The original's SE channels are in the SE table alone (0x49b790 makes them
 * with 0x4a0040, never through the general table at +0xc8): no general ID
 * reaches one, whatever its number. A port that kept them on spare IDs of the
 * general channels' pool would bring the clash of the two spaces back on
 * other numbers: there the general functions would see and stop the system
 * sound, and a script's Prepare of that ID would take its place. */
static void se12_no_general_id(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = "a system sound on SE 11, general channels 0..15";
	SG_CHECK(ss_playse(11, "ss_long"), "%s: PlaySe(11) returned false", what);
	sg_pull(ss_scratch, 4 * SG_CHUNK);
	for (int i = 0; i < 16; i++) {
		SG_CHECK(!sg_i1("IsPlay", i), "%s: IsPlay(%d) is true while only SE 11 plays", what, i);
		SG_CHECK(sg_i1("GetGroupNum", i) == 0, "%s: GetGroupNum(%d)=%d while only SE 11 plays, want 0", what, i,
			 sg_i1("GetGroupNum", i));
		SG_CHECK(!sg_i1("IsExistID", i), "%s: IsExistID(%d) is true while only SE 11 plays", what, i);
	}
	for (int i = 0; i < 16; i++) {
		SG_CHECK(!sg_i1("Stop", i), "%s: Stop(%d) returned true without a general channel %d", what, i, i);
		SG_CHECK(!sg_i1("Unprepare", i), "%s: Unprepare(%d) returned true without a general channel %d", what, i, i);
	}
	SG_CHECK(ss_isplayse(11), "%s: SE 11 stopped by Stop or Unprepare of a general ID", what);
	/* General channels on the lowest IDs, which a pool hands out first. */
	for (int i = 0; i < 4; i++)
		if (!ss_loop_on(i, "ss_hum"))
			return;
	memset(ss_buf, 0, sizeof ss_buf);
	ss_at = 0;
	ss_pull(6);
	SG_CHECK(ss_isplayse(11), "%s: SE 11 stopped when the general channels 0..3 were prepared and played", what);
	SG_CHECK(ss_stopse(11), "%s: StopSe(11) returned false", what);
	for (int i = 0; i < 4; i++)
		SG_CHECK(sg_i1("IsPlay", i), "%s: StopSe(11) stopped the general channel %d", what, i);
	ss_pull(3);
	/* SE 11 on every frame until StopSe, the four loops from their lead on, then the loops alone */
	struct ss_model m = ss_none;
	int allowed[] = { 256, 1024, 1280 };
	if (ss_split(what, &m, allowed, 3)) {
		struct ss_pat p[] = { { 1024, SS_LEAD_LO, SS_LEAD_HI }, { 1280, 2 * SG_CHUNK, -1 }, { 256, 2 * SG_CHUNK, -1 } };
		ss_match(what, p, 3);
	}
	for (int i = 0; i < 4; i++)
		SG_CHECK(sg_i1("IsPlay", i), "%s: the general channel %d stopped", what, i);
	printf("  SE12 a system sound and the general IDs 0..15 checked\n");
}

/* ---- SE13: a restart empties the table (port strategy) -------------------------------------- */

/* The original rebuilds its sound engine on a restart (inferred: its
 * release, 0x4a26f0, is what a restart runs), so the SE table is empty and
 * its sounds stop; the game's InitSystemSE sets the table again. Port
 * strategy: KiwiSoundEngine's _ModuleFini (vm_exit and a restart) frees
 * every SE channel and entry; the general channels are the parts engine's
 * audio_reset's, as before. */
static void se13_restart(void)
{
	sg_setup(&ss_group_opts);
	if (!sg_group_mode_ok())
		return;
	sr_unity();
	const char *what = "a restart while system sounds play";
	ss_game_params();
	SG_CHECK(ss_param(9, 0, false), "%s: SetSeParam(9) returned false", what);
	SG_CHECK(ss_playse(5, "ss_long") && ss_playse(6, "ss_long") && ss_playse(7, "ss_ramp"), "%s: PlaySe of 5, 6 or 7 returned false", what);
	sg_pull(ss_scratch, 2 * SG_CHUNK);
	SG_CHECK(ss_isplayse(5) && ss_isplayse(6) && ss_isplayse(7), "%s: IsPlaySe(5/6/7) %d/%d/%d before the restart, want all true", what,
		 ss_isplayse(5), ss_isplayse(6), ss_isplayse(7));
	sg_restart();
	SG_CHECK(sg_group_mode_ok(), "%s: no groups after the restart", what);
	SG_CHECK(!ss_isplayse(5) && !ss_isplayse(6) && !ss_isplayse(7), "%s: IsPlaySe(5/6/7) %d/%d/%d after the restart, want all false", what,
		 ss_isplayse(5), ss_isplayse(6), ss_isplayse(7));
	SG_CHECK(!ss_stopse(5) && !ss_stopse(9), "%s: StopSe(5) or StopSe(9) returned true after the restart (want no entries)", what);
	SG_CHECK(ss_free(5, 10) == 5, "%s: GetFreeSeID(5, 10)=%d after the restart, want 5 (no entries)", what, ss_free(5, 10));
	memset(ss_buf, 0, sizeof ss_buf);
	ss_at = 0;
	ss_pull(2);
	int silent[] = { 0 };
	struct ss_model m = ss_none;
	ss_split(what, &m, silent, 1);
	/* InitSystemSE again: the table works as before */
	ss_game_params();
	SG_CHECK(ss_free(5, 10) == 8, "%s: GetFreeSeID(5, 10)=%d after SetSeParam of 5, 6 and 7, want 8", what, ss_free(5, 10));
	ss_begin_take();
	ss_pull(1);
	SG_CHECK(ss_playse(5, "ss_se"), "%s: PlaySe(5) after the restart returned false", what);
	SG_CHECK(!ss_playse(5, "ss_se"), "%s: PlaySe(5) of the same name at once after the restart returned true", what);
	ss_pull(6);
	int allowed[] = { 0, 8192, 16384 };
	if (ss_split(what, &m, allowed, 3)) {
		struct ss_pat p[] = { { 0, SS_LEAD_LO, SS_LEAD_HI }, { 8192, 3000, 3000 }, { 0, SG_CHUNK, -1 } };
		ss_match(what, p, 3);
	}
	SG_CHECK(!ss_isplayse(5), "%s: IsPlaySe(5) is still true at the end", what);
	printf("  SE13 a restart checked\n");
}

/* ---- the mode ------------------------------------------------------------------------------- */

static int ss_run_many(const char *id, void (*fn)(void), int *which, int n, const char *const *names)
{
	int f = 0;
	for (*which = 0; *which < n; (*which)++) {
		int r = sg_fork(fn);
		printf("  %s %s: %s\n", id, names[*which], r ? "FAIL" : "ok");
		f += r;
	}
	return f;
}

static int test_sound_se(void)
{
	if (!ss_make_data()) {
		ss_remove_data();
		printf("SOUND-SE FAIL: no synthetic data\n");
		return 1;
	}
	int failed = 0, f;
	const char *names[16];
	sg_begin("SE0", "synthetic data reads back");
	failed += sg_report("SE0", "synthetic data reads back", sg_fork(se0_data));

	sg_begin("SE1", "a general channel's loop goes on while PlaySe of its ID plays");
	for (int i = 0; i < SG_COUNT(se1_cases); i++)
		names[i] = se1_cases[i].what;
	failed += sg_report("SE1", "a general channel's loop goes on while PlaySe of its ID plays",
			    ss_run_many("SE1", se1_loop_goes_on, &se1_which, SG_COUNT(se1_cases), names));

	sg_begin("SE2", "the general and the SE IDs are apart");
	failed += sg_report("SE2", "the general and the SE IDs are apart",
			    ss_run_many("SE2", se2_spaces, &se2_which, SG_COUNT(se2_names), se2_names));

	sg_begin("SE3", "defaults: the same name plays again alongside, StopSe stops all");
	failed += sg_report("SE3", "defaults: the same name plays again alongside, StopSe stops all",
			    ss_run_many("SE3", se3_defaults, &se3_which, SG_COUNT(se3_names), se3_names));

	sg_begin("SE4", "PlaySpan 40 with multiple play");
	failed += sg_report("SE4", "PlaySpan 40 with multiple play",
			    ss_run_many("SE4", se4_span_multi, &se4_which, SG_COUNT(se4_names), se4_names));

	sg_begin("SE5", "PlaySpan 40 without multiple play");
	failed += sg_report("SE5", "PlaySpan 40 without multiple play",
			    ss_run_many("SE5", se5_span_single, &se5_which, SG_COUNT(se5_names), se5_names));

	sg_begin("SE6", "the game's table and a battle's sequence over looping general channels");
	failed += sg_report("SE6", "the game's table and a battle's sequence over looping general channels", sg_fork(se6_battle));

	sg_begin("SE7", "GetFreeSeID and IsExistSeID");
	failed += sg_report("SE7", "GetFreeSeID and IsExistSeID", sg_fork(se7_free_exist));

	sg_begin("SE8", "a PlaySe sound's group and internal volume (guard)");
	failed += sg_report("SE8", "a PlaySe sound's group and internal volume (guard)", sg_fork(se8_gain));

	sg_begin("SE9", "every other game keeps the shared pool (v13, v14 SJIS, v15, flag only, no table, no rows, INI)");
	for (int i = 0; i < SG_COUNT(se9_cases); i++)
		names[i] = se9_cases[i].what;
	failed += sg_report("SE9", "every other game keeps the shared pool (v13, v14 SJIS, v15, flag only, no table, no rows, INI)",
			    ss_run_many("SE9", se9_legacy, &se9_which, SG_COUNT(se9_cases), names));

	sg_begin("SE10", "unknown declaration shapes keep the library table's functions");
	failed += sg_report("SE10", "unknown declaration shapes keep the library table's functions", sg_fork(se10_shapes));

	sg_begin("SE11", "the real audio thread under PlaySe, StopSe and general channels");
	f = sg_fork(se11_thread);
	failed += sg_report("SE11", "the real audio thread under PlaySe, StopSe and general channels", f);

	sg_begin("SE12", "a system sound is on no general ID");
	failed += sg_report("SE12", "a system sound is on no general ID", sg_fork(se12_no_general_id));

	sg_begin("SE13", "a restart empties the table (port strategy)");
	failed += sg_report("SE13", "a restart empties the table (port strategy)", sg_fork(se13_restart));

	ss_remove_data();
	printf("SOUND-SE %s: %d of 14 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
