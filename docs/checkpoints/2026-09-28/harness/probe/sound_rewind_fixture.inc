/* sound-rewind (G1b, 2026-10-25): where a v14 group channel is after its
 * sound has played to its natural end, so that Play without a new Prepare
 * plays it again. It builds on sound_groups_fixture.inc's writers and setup
 * and writes its own data.
 *
 * Original evidence (static disassembly of the one CN executable; private
 * research notes have the listings):
 *  - KiwiSoundEngine's Play (dispatch entry 11, 0x4a16e0 -> 0x4a3640) finds
 *    the channel of the ID and calls the Play of its sound buffer (vtable
 *    +0x10); Stop, IsPlay, GetPos and GetLoopCount are +0x14, +0x18, +0x24
 *    and +0x30 of the same object (0x4a3700, 0x4a3790, 0x4a3c80, 0x4a3820).
 *    Prepare's bStreaming picks the buffer (0x4a0040 -> 0x700270): zero the
 *    static one (vtable 0x8128d8), nonzero the streaming one (0x812820).
 *  - The static buffer plays in a thread (Play 0x6fc030 starts it when it
 *    does not run, 0x6fc430 -> 0x6fc4a0). When the sound has played its
 *    laps the thread stops the DirectSound buffer, sets its position to 0
 *    (0x6fc990) and ends; so does Stop (0x6fc1a0). (The calls into the
 *    DirectSound buffer are read as IDirectSoundBuffer's standard vtable,
 *    Stop, SetCurrentPosition and Play: an inference, not traced into
 *    DirectSound.) Then IsPlay (0x6fc480:
 *    the thread runs) is false and GetPos (0x6fc3f0) is 0, and a later Play
 *    plays from the start with its laps counted anew (the lap counter is the
 *    thread's own, 0x6fc540 / 0x6fc760; the count +0x2c is not used up).
 *    Play while the thread runs does nothing.
 *  - The streaming buffer's IsPlay and GetPos (0x6f7dd0, 0x6f7e10) are
 *    false and 0 when its thread does not run.
 * The game's scripts prepare a channel on the same path before each of
 * their Play calls (sound effects, voices and system sounds with bStreaming
 * 0, music with 1): Play after a natural end without a new Prepare was not
 * found in them. They do read GetPos of a voice's channel after it may have
 * ended, without an IsPlay before it in the same function (the auto mode's
 * timer); so after an end every case reads GetPos before IsPlay, and a port
 * that rewinds an ended channel only when IsPlay sees it fails.
 *
 * Port strategies, checked here as such and not claimed as original
 * behavior: every group channel, streaming or not, is back at frame 0 when
 * its sound has ended (the original's streaming buffer leaves its data at
 * the loop's end then, inferred from 0x6f8af0 and 0x6f8770; not followed);
 * it plays again from frame 0 also when its loop begins later (the static
 * buffer ignores loop points: its +0x34 returns false); after n laps it
 * plays again one lap or n (SR3 takes either: the port uses its count up,
 * the original does not); Play while a sound plays goes on (as the static
 * buffer; the streaming one starts over). The legacy mixers (every other
 * game) keep their own ends, exactly as before (SR7): a sound played once
 * loses the chunk in which it ends and goes back to its loop begin, which
 * the next Play, and a Stop then Play, start from.
 *
 * Not covered: GetLoopCount after laps, the original's timing of the end
 * (here one chunk after the last frame), Pause and Restart, a timed fade
 * still running when the sound ends, the decoder stopping before the end of
 * the file (the grouped refill's own branch for it; a synthetic WAV never
 * does), the movie's sound, PlaySe (it prepares every time), the BGM
 * archive's pool in group mode (this game has none), a sound at 22.05 kHz,
 * a port that fakes GetPos 0 for an ended channel and rewinds it only at
 * the next Play (the same through this API), the order of the rewind and
 * the end across the audio thread and the main thread (the probe calls
 * the audio callback itself, on the one thread), real audio output.
 *
 * Cases (each in forked children): SR0 the synthetic data reads back; SR1
 * played once to the end, then Play without Prepare, twice, for eight
 * sounds in their own children (6000 frames; 700, which ends inside a
 * chunk; 2048, which ends at a chunk's edge; a voice; a file without a
 * timemap row; a streaming one; a stereo one, with bStreaming 0 and 1):
 * every frame from 0 in order, then silence, GetPos 0 (read first) and
 * IsPlay false after each end, and for the five of at least 4000 frames
 * IsPlay true and GetPos inside the sound while it plays again; SR2 a loop
 * that begins at 1000, played once, plays again from 0; SR3 two laps, then
 * again from 0, one or two laps, then silence; SR4 Stop after the end, then
 * Play from 0; SR5 the positions that were right before the change (Stop
 * while playing, Fade with stop timed and at once, Seek(0) after the end;
 * each then Play from 0; Seek(50 ms) after Prepare, and after the end then
 * Stop: Play from frame 2205); SR6 Play while it plays goes on; SR7 the
 * legacy mixers (a sound in v13 and in v14 with the SJIS rule; a BGM with a
 * BGI loop in v13, v14 SJIS, v15, v14 with the GB18030 flag only, v14 GBK
 * without the table and v13 with INI mixers, played again and stopped then
 * played after its end); SR8 Stop, Fade(0, stop) or Seek(0) in the chunk in
 * which the sound ends (IsPlay still true), then Play from 0.
 *
 * Data: synthetic, written here into a fresh mkdtemp directory
 * (xs4-sound-rewind-*, under $XS4_SAVE_TMP or $TMPDIR, refused inside
 * XS4_SRC or XS4_MASTER_GAME, removed at the end): sound-groups' main EX
 * (its group table and the internal volume keys at 1.0), Sound and Voice
 * archives of PCM16 ramps at 44.1 kHz, mono but for one stereo sound
 * (frame k holds k+1, a stereo one's right channel -(k+1), so an output
 * sample at unity gain names its frame) with timemaps, and for SR7
 * sound-groups' SG10 BGM archive, BGI and WAI (with that main EX, and with
 * sound-groups' main EX without the table). Names are neutral; the only
 * game-defined bytes are the schema keys the engine has to recognize (GBK).
 * The samples come from the engine's SDL callback (probe_audio_pull) with
 * every group at 100 (unity).
 */

/* The sounds (sound archive, archive order). */
static const struct sg_sound sr_sounds[] = {
	{ "sr_once", SG_RAMP, 6000, 0, true, 2, 0, 6000 },
	{ "sr_tiny", SG_RAMP, 700, 0, true, 2, 0, 700 },
	{ "sr_edge", SG_RAMP, 2048, 0, true, 2, 0, 2048 },
	{ "sr_mid", SG_RAMP, 6000, 0, true, 2, 1000, 3000 },
	{ "sr_stream", SG_RAMP, 6000, 0, true, 1, 0, 6000 },
	{ "sr_norow", SG_RAMP, 3000, 0, false, 0, 0, 0 },
	{ "sr_stereo", SG_RAMP_STEREO, 6000, 0, true, 2, 0, 6000 },
};
static const char *const sr_sound_rows[] = { "sr_mid", "sr_once", "sr_stream", "sr_edge", "sr_tiny", "sr_stereo" };
static const struct sg_sound sr_voices[] = {
	{ "sr_voice", SG_RAMP, 4000, 0, true, 6, 0, 4000 },
};
static const char *const sr_voice_rows[] = { "sr_voice" };
#define SR_SOUND_TM_POS 2
#define SR_VOICE_TM_POS 1

/* groups: the group table; legacy: the table too, with the BGM archive, BGI
 * and WAI (for the versions and charsets that keep the legacy mixers);
 * notable: the same without the table (v14 GBK keeps the legacy mixers). */
enum { SR_DS_GROUPS, SR_DS_LEGACY, SR_DS_NOTABLE, SR_NR_DS };
static const char *const sr_ds_names[SR_NR_DS] = { "groups", "legacy", "notable" };
static char sr_root_dir[PATH_MAX];

static bool sr_make_data(void)
{
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !*base)
		base = getenv("TMPDIR");
	if (!base || !*base)
		base = "/tmp";
	snprintf(sr_root_dir, sizeof sr_root_dir, "%s/xs4-sound-rewind-XXXXXX", base);
	if (!mkdtemp(sr_root_dir)) {
		printf("  FAIL: mkdtemp %s: %s\n", sr_root_dir, strerror(errno));
		sr_root_dir[0] = '\0';
		return false;
	}
	if (sg_inside(sr_root_dir, getenv("XS4_SRC")) || sg_inside(sr_root_dir, getenv("XS4_MASTER_GAME"))) {
		printf("  FAIL: refusing a data directory inside the source tree or the original game: %s\n", sr_root_dir);
		rmdir(sr_root_dir);
		sr_root_dir[0] = '\0';
		return false;
	}
	/* sg_setup's data root and data sets (sound_groups_fixture.inc) */
	snprintf(sg_root_dir, sizeof sg_root_dir, "%s", sr_root_dir);
	bool ok = true;
	for (int ds = 0; ok && ds < SR_NR_DS; ds++) {
		snprintf(sg_ds_dir[ds], sizeof sg_ds_dir[ds], "%s/%s", sr_root_dir, sr_ds_names[ds]);
		ok = !mkdir(sg_ds_dir[ds], 0700);
		struct sg_buf ex = sg_main_ex(ds == SR_DS_NOTABLE ? SG_DS_NOTABLE : SG_DS_VALID);
		ok = ok && sg_write(sg_ds_dir[ds], "sgprobeEx.ex", &ex);
		free(ex.p);
		ok = ok && sg_archive(sg_ds_dir[ds], "sgprobeSound.afa", sr_sounds, SG_COUNT(sr_sounds), "timemap_sound.ex",
				      SR_SOUND_TM_POS, sr_sound_rows, SG_COUNT(sr_sound_rows), false);
		ok = ok && sg_archive(sg_ds_dir[ds], "sgprobeVoice.afa", sr_voices, SG_COUNT(sr_voices), "timemap_voice.ex",
				      SR_VOICE_TM_POS, sr_voice_rows, SG_COUNT(sr_voice_rows), false);
		if (ok && ds != SR_DS_GROUPS)
			ok = sg_legacy_meta_files(sg_ds_dir[ds]);
	}
	if (!ok)
		printf("  FAIL: cannot write the synthetic game under %s\n", sr_root_dir);
	return ok;
}

static void sr_remove_data(void)
{
	const char *slash = strrchr(sr_root_dir, '/');
	if (sr_root_dir[0] && slash && !strncmp(slash, "/xs4-sound-rewind-", 18))
		nftw(sr_root_dir, sg_rm, 16, FTW_DEPTH | FTW_PHYS);
}

/* ---- SR0: the synthetic files read back with libsys4 ------------------------------- */

static void sr0_archive(int ds, const char *file, const char *tm, int tm_pos, const struct sg_sound *v, int n,
			const char *const *rows, int nr_rows)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sg_ds_dir[ds], file);
	int err = 0;
	struct afa_archive *ar = afa_open(path, 0, &err);
	SG_CHECK(ar && !ar->has_number && ar->nr_files == (uint32_t)n + 1, "%s/%s: open %d, %u files", sr_ds_names[ds], file, err,
		 ar ? ar->nr_files : 0);
	if (!ar)
		return;
	for (int i = 0; i < n; i++) {
		int no = -1, want = i < tm_pos ? i : i + 1;
		SG_CHECK(archive_exists_by_basename(&ar->ar, v[i].name, &no) && no == want, "%s/%s: %s at %d, want %d",
			 sr_ds_names[ds], file, v[i].name, no, want);
		struct archive_data *d = archive_get_by_basename(&ar->ar, v[i].name);
		int nch = v[i].kind == SG_RAMP_STEREO ? 2 : 1;
		bool ramp = d && d->size == 44 + 2 * (size_t)nch * v[i].frames && d->data[22] == nch && d->data[23] == 0;
		for (int k = 0; ramp && k < v[i].frames; k += v[i].frames - 1 > 0 ? v[i].frames - 1 : 1) {
			const uint8_t *f = d->data + 44 + 2 * nch * k;
			ramp = (int16_t)(f[0] | f[1] << 8) == k + 1 && (nch == 1 || (int16_t)(f[2] | f[3] << 8) == -(k + 1));
		}
		SG_CHECK(ramp, "%s/%s: %s is not a %s ramp of %d frames", sr_ds_names[ds], file, v[i].name, nch == 2 ? "stereo" : "mono",
			 v[i].frames);
		if (d)
			archive_free_data(d);
	}
	struct archive_data *d = archive_get_by_basename(&ar->ar, tm);
	struct ex *t = d ? ex_read(d->data, d->size) : NULL;
	struct ex_table *st = t ? ex_get_table(t, "SoundInfo") : NULL;
	SG_CHECK(st && st->nr_rows == (uint32_t)nr_rows && st->nr_fields == 4, "%s/%s: %s SoundInfo shape", sr_ds_names[ds], file, tm);
	for (int r = 0; st && r < nr_rows && r < (int)st->nr_rows; r++) {
		const struct sg_sound *s = sg_find(v, n, rows[r]);
		SG_CHECK(s && st->rows[r][0].s && !strcmp(st->rows[r][0].s->text, rows[r]) && st->rows[r][1].i == s->group
			 && st->rows[r][2].i == s->begin && st->rows[r][3].i == s->end, "%s/%s: timemap row %d (%s)", sr_ds_names[ds], file,
			 r, rows[r]);
	}
	if (d)
		archive_free_data(d);
}

static void sr0_data(void)
{
	static const char *const in_keys[4] = { SG_KEY_IN_MOVIE, SG_KEY_IN_BGM, SG_KEY_IN_SE, SG_KEY_IN_VOICE };
	for (int ds = 0; ds < SR_NR_DS; ds++) {
		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/sgprobeEx.ex", sg_ds_dir[ds]);
		struct ex *ex = ex_read_file(path);
		struct ex_table *t = ex ? ex_get_table(ex, SG_KEY_TABLE) : NULL;
		if (ds == SR_DS_NOTABLE)
			SG_CHECK(ex && !t, "%s: main EX %s", sr_ds_names[ds], ex ? "has a group table" : "does not read");
		else
			SG_CHECK(t && t->nr_rows == SG_NR_GROUPS && t->nr_fields == 3, "%s: main EX group table", sr_ds_names[ds]);
		for (int k = 0; ex && k < 4; k++) {
			struct ex_value *iv = ex_get(ex, in_keys[k]);
			SG_CHECK(iv && iv->type == EX_FLOAT && iv->f == 1.0f, "%s: internal volume key %d is not 1.0", sr_ds_names[ds], k);
		}
		sr0_archive(ds, "sgprobeSound.afa", "timemap_sound", SR_SOUND_TM_POS, sr_sounds, SG_COUNT(sr_sounds), sr_sound_rows,
			    SG_COUNT(sr_sound_rows));
		sr0_archive(ds, "sgprobeVoice.afa", "timemap_voice", SR_VOICE_TM_POS, sr_voices, SG_COUNT(sr_voices), sr_voice_rows,
			    SG_COUNT(sr_voice_rows));
		snprintf(path, sizeof path, "%s/sgprobe.bgi", sg_ds_dir[ds]);
		bool bgi = access(path, R_OK) == 0;
		SG_CHECK(bgi == (ds != SR_DS_GROUPS), "%s: a BGI %s", sr_ds_names[ds], bgi ? "where none belongs" : "is missing");
		snprintf(path, sizeof path, "%s/sgprobeBA.afa", sg_ds_dir[ds]);
		int err = 0;
		struct afa_archive *ba = ds != SR_DS_GROUPS ? afa_open(path, 0, &err) : NULL;
		SG_CHECK(ds == SR_DS_GROUPS || (ba && archive_exists_by_basename(&ba->ar, SG_LEGACY_BGM, NULL)), "%s: no BGM %s",
			 sr_ds_names[ds], SG_LEGACY_BGM);
		if (ba)
			archive_free(&ba->ar);
	}
	printf("  SR0 synthetic EX, AFA, timemap and BGI files read back with libsys4\n");
}

/* ---- helpers ------------------------------------------------------------------------ */

static const struct sg_opts sr_group_opts = { SR_DS_GROUPS, 14, SG_GBK, false, false, false, NULL, false };

/* Every group at 100, so that an output sample names its source frame. */
static void sr_unity(void)
{
	for (int g = 0; g < SG_NR_GROUPS; g++)
		SG_CHECK(sg_setvol(g, 100), "SetMixerVolume(%d, 100) failed", g);
}

static bool sr_prepare(int id, const char *name, bool streaming, int loops)
{
	bool ok = sg_prepare(id, name, streaming) && sg_i2("SetLoopCount", id, loops);
	SG_CHECK(ok, "cannot prepare %s on %d (bStreaming %d, %d laps)", name, id, streaming, loops);
	return ok;
}

static bool sr_fade(int id, int ms, float volume, bool stop)
{
	stack_push(id); stack_push(ms); stack_push(volume); stack_push((int)stop); stack_push(0);
	return sg_call("Fade").i;
}

static int sr_ms(int frames) { return (int)((int64_t)frames * 1000 / 44100); }

/* What a take must hold after its leading silence: the frames from..to in
 * order, laps_lo..laps_hi times (each lap after the first from lap_from),
 * then, when tail_to > to, either nothing more or every frame on to tail_to
 * (ending anywhere between is a failure), then silence to the end of the
 * take. With stereo the right channel holds the negated frame (a stereo
 * ramp), else the left one's. */
struct sr_want { int from, to, lap_from, laps_lo, laps_hi, tail_to; bool stereo; };

enum { SR_FRAMES = 24 * SG_CHUNK, SR_MID = 4 * SG_CHUNK };
static float sr_buf[SR_FRAMES * 2];
static float sr_scratch[4 * SG_CHUNK * 2];

static bool sr_check(const char *what, struct sr_want w)
{
	int s = -1;
	for (int i = 0; i < SR_FRAMES && s < 0; i++)
		if (fabsf(sr_buf[2 * i]) > 0.5f / 32768 || fabsf(sr_buf[2 * i + 1]) > 0.5f / 32768)
			s = i;
	if (s < 0) {
		SG_CHECK(false, "%s: no sound in %d frames", what, SR_FRAMES);
		return false;
	}
	enum { PLAY, TAIL, END } phase = PLAY;
	int want = w.from, laps = 0, end_at = -1;
	for (int i = s; i < SR_FRAMES; i++) {
		float l = sr_buf[2 * i], r = w.stereo ? -sr_buf[2 * i + 1] : sr_buf[2 * i + 1];
		bool silent = l == 0.0f && r == 0.0f;
		int got = sg_frame_of(l);
		if (phase == TAIL && silent) {
			if (want != w.to + 1) {
				SG_CHECK(false, "%s: output frame %d is silence inside the tail (after source frame %d), want it to end at %d or go on to %d",
					 what, i - s, want - 1, w.to, w.tail_to);
				return false;
			}
			phase = END;
			end_at = i;
		}
		if (phase == END) {
			if (!silent) {
				SG_CHECK(false, "%s: output frame %d after the end is source frame %d, want silence", what, i - s, got);
				return false;
			}
			continue;
		}
		if (silent || got != want || l != r) {
			SG_CHECK(false, "%s: output frame %d is %s %d (right %d), want %d (lap %d)", what, i - s,
				 silent ? "silence, not frame" : "source frame", silent ? want : got, sg_frame_of(r), want, laps + 1);
			return false;
		}
		want++;
		if (phase == TAIL) {
			if (want > w.tail_to) {
				phase = END;
				end_at = i + 1;
			}
			continue;
		}
		if (want <= w.to)
			continue;
		laps++;
		bool next_lap = i + 1 < SR_FRAMES && sr_buf[2 * (i + 1)] != 0.0f && sg_frame_of(sr_buf[2 * (i + 1)]) == w.lap_from;
		if (laps < w.laps_lo || (laps < w.laps_hi && next_lap)) {
			want = w.lap_from;
		} else if (w.tail_to > w.to) {
			phase = TAIL;
		} else {
			phase = END;
			end_at = i + 1;
		}
	}
	if (phase == PLAY) {
		SG_CHECK(false, "%s: the sound did not end in %d frames (at source frame %d, lap %d)", what, SR_FRAMES - s, want, laps + 1);
		return false;
	}
	if (phase == END && SR_FRAMES - end_at < 2 * SG_CHUNK) {
		SG_CHECK(false, "%s: only %d frames after the end (the take is too short to see silence)", what, SR_FRAMES - end_at);
		return false;
	}
	return true;
}

/* Flush what played before (unless flush is false), Play (already
 * prepared), pull a take and check it; with mid, IsPlay and GetPos also
 * while it plays (sounds of at least 4000 frames). The window of mid relies
 * on how a stream starts here (sts_mixer_play_stream at position 0 mixes the
 * channel's zeroed buffer first, channel_play clears it): after SR_MID
 * frames the channel has refilled three times (frame 3072), so a sound of
 * 4000 frames has 928 frames to spare; an engine that filled the buffer at
 * Play would need the window moved. */
static bool sr_take_flush(const char *what, int id, int frames, struct sr_want w, bool mid, bool flush)
{
	if (flush)
		sg_pull(sr_scratch, 4 * SG_CHUNK);
	if (!sg_i1("Play", id)) {
		SG_CHECK(false, "%s: Play(%d) returned false", what, id);
		return false;
	}
	int at = 0;
	if (mid) {
		sg_pull(sr_buf, SR_MID);
		at = SR_MID;
		int pos = sg_i1("GetPos", id), len = sr_ms(frames);
		SG_CHECK(sg_i1("IsPlay", id), "%s: IsPlay is false while it should play", what);
		SG_CHECK(pos > 0 && pos < len, "%s: GetPos %d while it plays, want 1..%d", what, pos, len - 1);
	}
	sg_pull(sr_buf + 2 * at, SR_FRAMES - at);
	return sr_check(what, w);
}
static bool sr_take(const char *what, int id, int frames, struct sr_want w, bool mid)
{
	return sr_take_flush(what, id, frames, w, mid, true);
}

/* After an end: with pos, GetPos 0, and not playing. GetPos comes first,
 * before any IsPlay after the end: the scripts' auto mode timer reads
 * GetPos of the voice channel without an IsPlay before it, so an engine
 * that rewinds an ended channel only when IsPlay sees it must fail here.
 * pos is false for a BGM-archive channel (Kiwi's GetPos does not reach
 * that pool) and for the last takes of SR5 and SR6, whose own natural end
 * is SR1's to check. */
static void sr_after_end(const char *what, int id, bool pos)
{
	int p = sg_i1("GetPos", id);
	SG_CHECK(!pos || p == 0, "%s: GetPos is %d after the end (read before IsPlay), want 0", what, p);
	SG_CHECK(!sg_i1("IsPlay", id), "%s: IsPlay is still true after the end", what);
}

/* ---- SR1: played to the end, then Play without Prepare ------------------------------ */

/* The stereo ones: the rewind is by frames, whatever the channels (the
 * game's music is stereo). */
static const struct { const char *what, *name; bool streaming; int frames; bool stereo; } sr1_cases[] = {
	{ "6000 frames", "sr_once", false, 6000, false },
	{ "700 frames, ending inside a chunk", "sr_tiny", false, 700, false },
	{ "2048 frames, ending at a chunk's edge", "sr_edge", false, 2048, false },
	{ "a voice", "sr_voice", false, 4000, false },
	{ "a file without a timemap row", "sr_norow", false, 3000, false },
	{ "a streaming one (bStreaming 1)", "sr_stream", true, 6000, false },
	{ "a stereo one", "sr_stereo", false, 6000, true },
	{ "a stereo streaming one (bStreaming 1)", "sr_stereo", true, 6000, true },
};
static int sr1_which;

static void sr1_replay(void)
{
	sg_setup(&sr_group_opts);
	sg_group_mode_ok();
	sr_unity();
	int id = 20, n = sr1_cases[sr1_which].frames;
	bool mid = n >= 4000;
	if (!sr_prepare(id, sr1_cases[sr1_which].name, sr1_cases[sr1_which].streaming, 1))
		return;
	struct sr_want w = { 0, n - 1, 0, 1, 1, 0, sr1_cases[sr1_which].stereo };
	char what[128];
	for (int k = 1; k <= 3; k++) {
		snprintf(what, sizeof what, "%s, %s", sr1_cases[sr1_which].what,
			 k == 1 ? "the first Play" : k == 2 ? "Play again without Prepare" : "a third Play");
		if (!sr_take(what, id, n, w, mid))
			return;
		sr_after_end(what, id, true);
	}
	sg_i1("Unprepare", id);
}

/* ---- SR2: a loop that begins later ---------------------------------------------------- */

static void sr2_loop_begin(void)
{
	sg_setup(&sr_group_opts);
	sg_group_mode_ok();
	sr_unity();
	int id = 21;
	if (!sr_prepare(id, "sr_mid", false, 1))
		return;
	/* Once: 0..2999 and (sound-groups' play-once strategy) maybe on to the
	 * file's end; again from 0, not from the loop's begin 1000. */
	struct sr_want w = { 0, 2999, 0, 1, 1, 5999 };
	if (sr_take("loop 1000..3000 once, the first Play", id, 6000, w, true)) {
		sr_after_end("loop 1000..3000 once, the first Play", id, true);
		if (sr_take("loop 1000..3000 once, Play again without Prepare", id, 6000, w, true))
			sr_after_end("loop 1000..3000 once, Play again without Prepare", id, true);
	}
	printf("  SR2 a loop that begins at 1000 checked\n");
}

/* ---- SR3: laps ------------------------------------------------------------------------ */

static void sr3_laps(void)
{
	sg_setup(&sr_group_opts);
	sg_group_mode_ok();
	sr_unity();
	int id = 22;
	if (!sr_prepare(id, "sr_once", false, 2))
		return;
	struct sr_want two = { 0, 5999, 0, 2, 2, 0 }, again = { 0, 5999, 0, 1, 2, 0 };
	if (sr_take("two laps, the first Play", id, 12000, two, false)) {
		sr_after_end("two laps, the first Play", id, true);
		if (sr_take("two laps, Play again without Prepare", id, 12000, again, false))
			sr_after_end("two laps, Play again without Prepare", id, true);
	}
	printf("  SR3 laps checked\n");
}

/* ---- SR4: Stop after the end ----------------------------------------------------------- */

static void sr4_stop_after_end(void)
{
	sg_setup(&sr_group_opts);
	sg_group_mode_ok();
	sr_unity();
	int id = 23;
	if (!sr_prepare(id, "sr_once", false, 1))
		return;
	struct sr_want w = { 0, 5999, 0, 1, 1, 0 };
	if (!sr_take("Stop after the end, the first Play", id, 6000, w, true))
		return;
	SG_CHECK(sg_i1("Stop", id), "Stop after the end returned false");
	sr_after_end("Stop after the end", id, true);
	if (sr_take("Stop after the end, then Play", id, 6000, w, true))
		sr_after_end("Stop after the end, then Play", id, true);
	printf("  SR4 Stop after the end checked\n");
}

/* ---- SR5: what was right before -------------------------------------------------------- */

/* Seek(50 ms) is frame 2205 (44.1 kHz): a later Play starts there, also
 * after the end and a Stop, as before the change (the static buffer's Play
 * starts its DirectSound buffer where it is, 0x6fc5a0, and its Stop sets no
 * position itself, 0x6fc1a0; what Seek does there was not traced). So the
 * end's rewind must not come from Play or Stop. */
static int sr5_which;
static const char *const sr5_names[] = { "Stop while playing", "Fade(20 ms, 1.0, stop) while playing",
	"Fade(0, 1.0, stop) while playing", "Seek(0) after the end", "Seek(50 ms) after Prepare",
	"Seek(50 ms) after the end, then Stop" };

static void sr5_before(void)
{
	sg_setup(&sr_group_opts);
	sg_group_mode_ok();
	sr_unity();
	int id = 24;
	const char *what = sr5_names[sr5_which];
	if (!sr_prepare(id, "sr_once", false, 1))
		return;
	struct sr_want w = { 0, 5999, 0, 1, 1, 0 };
	if (sr5_which == 3 || sr5_which == 5) {
		if (!sr_take(sr5_which == 3 ? "before Seek(0), the first Play" : "before Seek(50 ms), the first Play", id, 6000, w, false))
			return;
		int ms = sr5_which == 3 ? 0 : 50;
		SG_CHECK(sg_i2("Seek", id, ms), "Seek(%d, %d) returned false", id, ms);
		if (sr5_which == 5)
			SG_CHECK(sg_i1("Stop", id), "Stop after the end and Seek returned false");
	} else if (sr5_which == 4) {
		SG_CHECK(sg_i2("Seek", id, 50), "Seek(%d, 50) after Prepare returned false", id);
	} else {
		sg_pull(sr_scratch, 4 * SG_CHUNK);
		SG_CHECK(sg_i1("Play", id), "%s: Play returned false", what);
		sg_pull(sr_scratch, 3 * SG_CHUNK);
		bool ok = sr5_which == 0 ? sg_i1("Stop", id) : sr_fade(id, sr5_which == 1 ? 20 : 0, 1.0f, true);
		SG_CHECK(ok, "%s returned false", what);
		sg_pull(sr_scratch, 4 * SG_CHUNK);
		sr_after_end(what, id, true);
	}
	/* The position after this take's own end is SR1's. */
	char take[128];
	snprintf(take, sizeof take, "%s, then Play", what);
	if (sr5_which >= 4)
		w.from = 2205;
	if (sr_take(take, id, 6000, w, true))
		sr_after_end(take, id, false);
}

/* ---- SR6: Play while it plays ------------------------------------------------------------ */

static void sr6_play_while_playing(void)
{
	sg_setup(&sr_group_opts);
	sg_group_mode_ok();
	sr_unity();
	int id = 25;
	if (!sr_prepare(id, "sr_once", false, 1))
		return;
	sg_pull(sr_scratch, 4 * SG_CHUNK);
	SG_CHECK(sg_i1("Play", id), "Play returned false");
	sg_pull(sr_buf, 3 * SG_CHUNK);
	SG_CHECK(sg_i1("IsPlay", id) && sg_i1("Play", id), "Play while it plays: IsPlay or Play false");
	sg_pull(sr_buf + 2 * 3 * SG_CHUNK, SR_FRAMES - 3 * SG_CHUNK);
	struct sr_want w = { 0, 5999, 0, 1, 1, 0 };
	if (sr_check("Play while it plays", w))
		sr_after_end("Play while it plays", id, false);
	printf("  SR6 Play while it plays checked\n");
}

/* ---- SR7: the legacy mixers --------------------------------------------------------------- */

/* The old refill (cb_read_frames) stops in the refill that reaches the
 * end: that chunk is not mixed, so a sound played once ends at the last
 * whole chunk, and the channel is back at its loop begin. Kept as it is. */
static int sr7_which;
static const struct { const char *what; struct sg_opts o; } sr7_cases[] = {
	{ "v13", { SR_DS_LEGACY, 13, SG_GBK, false, false, false, NULL, false } },
	{ "v14 with the SJIS rule", { SR_DS_LEGACY, 14, SG_SJIS, false, false, false, NULL, false } },
};

static void sr7_legacy_sound(void)
{
	sg_setup(&sr7_cases[sr7_which].o);
	const char *v = sr7_cases[sr7_which].what;
	SG_CHECK(sg_numof() == 3, "%s: GetMixerNumof=%d, want the 3 legacy mixers", v, sg_numof());
	int id = 26;
	if (!sr_prepare(id, "sr_once", false, 1))
		return;
	struct sr_want w = { 0, 5119, 0, 1, 1, 0 };
	char what[128];
	for (int k = 1; k <= 3; k++) {
		snprintf(what, sizeof what, "%s, %s", v, k == 1 ? "the first Play" : k == 2 ? "Play again without Prepare" : "Stop, then Play");
		if (k == 3)
			SG_CHECK(sg_i1("Stop", id), "%s: Stop after the end returned false", v);
		if (!sr_take(what, id, 6000, w, true))
			return;
		sr_after_end(what, id, true);
	}
}

/* A BGM with the BGI's loop 1000..3000, once: 0..2047; then Play again,
 * and Stop then Play, from its loop begin: 1000..2023. The loop begin is
 * not 0, so a rewind to 0 shows: in every configuration that keeps the
 * legacy mixers (the gate is the groups', not the version), and after a
 * Stop (which leaves a channel that does not play where it is). The BGI
 * gives the BGM mixer 0 (unity in both mixer sets). */
static int sr7_bgm_which;
static const struct { const char *what; struct sg_opts o; } sr7_bgm_cases[] = {
	{ "v13", { SR_DS_LEGACY, 13, SG_GBK, false, false, false, NULL, false } },
	{ "v14 with the SJIS rule", { SR_DS_LEGACY, 14, SG_SJIS, false, false, false, NULL, false } },
	{ "v15", { SR_DS_LEGACY, 15, SG_GBK, false, false, false, NULL, false } },
	{ "v14 with the GB18030 flag only", { SR_DS_LEGACY, 14, SG_FLAG_ONLY, false, false, false, NULL, false } },
	{ "v14 GBK without the group table", { SR_DS_NOTABLE, 14, SG_GBK, false, false, false, NULL, false } },
	{ "v13 with INI mixers", { SR_DS_LEGACY, 13, SG_GBK, false, false, true, NULL, false } },
};

static void sr7_legacy_bgm(void)
{
	const struct sg_opts *o = &sr7_bgm_cases[sr7_bgm_which].o;
	sg_setup(o);
	const char *v = sr7_bgm_cases[sr7_bgm_which].what;
	int want_mixers = o->ini_mixers ? 4 : 3;
	SG_CHECK(sg_numof() == want_mixers, "%s BGM: GetMixerNumof=%d, want the %d legacy mixers", v, sg_numof(), want_mixers);
	int id = 27;
	if (!sr_prepare(id, SG_LEGACY_BGM, false, 1))
		return;
	struct sr_want first = { 0, 2047, 0, 1, 1, 0 }, again = { 1000, 2023, 1000, 1, 1, 0 };
	char what[160];
	for (int k = 1; k <= 3; k++) {
		snprintf(what, sizeof what, "%s BGM with a BGI loop, %s", v,
			 k == 1 ? "the first Play" : k == 2 ? "Play again without Prepare" : "Stop after the end, then Play");
		if (k == 3)
			SG_CHECK(sg_i1("Stop", id), "%s BGM: Stop after the end returned false", v);
		if (!sr_take(what, id, 6000, k == 1 ? first : again, false))
			return;
		sr_after_end(what, id, false);
	}
}

/* ---- SR8: in the chunk in which the sound ends -------------------------------------------- */

/* 700 frames: after Play the first chunk is the channel's zeroed buffer, the
 * second holds the whole sound, and the refill that ends the stream comes at
 * the third. Between the second and the third the channel still plays (IsPlay
 * true) while nothing of the sound is left: a Stop, a Fade(0, stop) or a
 * Seek(0) there, then Play, plays the sound again from frame 0 (as before the
 * change: a stop rewinds, a seek moves a playing stream). Seek leaves it
 * playing, so its Play goes on (SR6) and the take is not flushed first. The
 * position after the take's own end is SR1's. */
static int sr8_which;
static const char *const sr8_names[] = { "Stop", "Fade(0, 1.0, stop)", "Seek(0)" };

static void sr8_end_chunk(void)
{
	sg_setup(&sr_group_opts);
	sg_group_mode_ok();
	sr_unity();
	int id = 28;
	const char *what = sr8_names[sr8_which];
	if (!sr_prepare(id, "sr_tiny", false, 1))
		return;
	sg_pull(sr_scratch, 4 * SG_CHUNK);
	SG_CHECK(sg_i1("Play", id), "%s: Play returned false", what);
	sg_pull(sr_scratch, 2 * SG_CHUNK);
	if (!sg_i1("IsPlay", id)) {
		SG_CHECK(false, "%s: IsPlay is false two chunks after Play (not in the chunk in which the sound ends)", what);
		return;
	}
	bool ok = sr8_which == 0 ? sg_i1("Stop", id) : sr8_which == 1 ? sr_fade(id, 0, 1.0f, true) : sg_i2("Seek", id, 0);
	SG_CHECK(ok, "%s in the end chunk returned false", what);
	char take[128];
	snprintf(take, sizeof take, "%s in the end chunk, then Play", what);
	struct sr_want w = { 0, 699, 0, 1, 1, 0 };
	if (sr_take_flush(take, id, 700, w, false, sr8_which != 2))
		sr_after_end(take, id, false);
}

/* ---- the mode ------------------------------------------------------------------------------- */

static int test_sound_rewind(void)
{
	if (!sr_make_data()) {
		sr_remove_data();
		printf("SOUND-REWIND FAIL: no synthetic data\n");
		return 1;
	}
	int failed = 0, f;
	sg_begin("SR0", "synthetic data reads back");
	failed += sg_report("SR0", "synthetic data reads back", sg_fork(sr0_data));
	sg_begin("SR1", "played to the end, then Play without Prepare: from frame 0 (eight sounds)");
	f = 0;
	for (sr1_which = 0; sr1_which < (int)SG_COUNT(sr1_cases); sr1_which++) {
		int r = sg_fork(sr1_replay);
		printf("  SR1 %s: %s\n", sr1_cases[sr1_which].what, r ? "FAIL" : "ok");
		f += r;
	}
	failed += sg_report("SR1", "played to the end, then Play without Prepare: from frame 0 (eight sounds)", f);
	sg_begin("SR2", "a loop that begins later: again from frame 0");
	failed += sg_report("SR2", "a loop that begins later: again from frame 0", sg_fork(sr2_loop_begin));
	sg_begin("SR3", "two laps, then again from frame 0");
	failed += sg_report("SR3", "two laps, then again from frame 0", sg_fork(sr3_laps));
	sg_begin("SR4", "Stop after the end, then Play from frame 0");
	failed += sg_report("SR4", "Stop after the end, then Play from frame 0", sg_fork(sr4_stop_after_end));
	sg_begin("SR5", "positions that were right before (Stop, Fade with stop, Seek(0))");
	f = 0;
	for (sr5_which = 0; sr5_which < (int)SG_COUNT(sr5_names); sr5_which++) {
		int r = sg_fork(sr5_before);
		printf("  SR5 %s: %s\n", sr5_names[sr5_which], r ? "FAIL" : "ok");
		f += r;
	}
	failed += sg_report("SR5", "positions that were right before (Stop, Fade with stop, Seek(0))", f);
	sg_begin("SR6", "Play while it plays goes on");
	failed += sg_report("SR6", "Play while it plays goes on", sg_fork(sr6_play_while_playing));
	sg_begin("SR7", "legacy mixers unchanged (sounds: v13, v14 SJIS; a BGM with a BGI loop in six configurations)");
	f = 0;
	for (sr7_which = 0; sr7_which < (int)SG_COUNT(sr7_cases); sr7_which++) {
		int r = sg_fork(sr7_legacy_sound);
		printf("  SR7 %s: %s\n", sr7_cases[sr7_which].what, r ? "FAIL" : "ok");
		f += r;
	}
	for (sr7_bgm_which = 0; sr7_bgm_which < (int)SG_COUNT(sr7_bgm_cases); sr7_bgm_which++) {
		int r = sg_fork(sr7_legacy_bgm);
		printf("  SR7 %s BGM with a BGI loop: %s\n", sr7_bgm_cases[sr7_bgm_which].what, r ? "FAIL" : "ok");
		f += r;
	}
	failed += sg_report("SR7", "legacy mixers unchanged (sounds: v13, v14 SJIS; a BGM with a BGI loop in six configurations)", f);
	sg_begin("SR8", "Stop, Fade(0, stop) or Seek(0) in the chunk in which the sound ends, then Play from frame 0");
	f = 0;
	for (sr8_which = 0; sr8_which < (int)SG_COUNT(sr8_names); sr8_which++) {
		int r = sg_fork(sr8_end_chunk);
		printf("  SR8 %s: %s\n", sr8_names[sr8_which], r ? "FAIL" : "ok");
		f += r;
	}
	failed += sg_report("SR8", "Stop, Fade(0, stop) or Seek(0) in the chunk in which the sound ends, then Play from frame 0", f);
	sr_remove_data();
	printf("SOUND-REWIND %s: %d of 9 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
