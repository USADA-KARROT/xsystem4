/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "asset_manager.h"
#include "audio.h"
#include "id_pool.h"
#include "mixer.h"
#include "system4/archive.h"
#include "xsystem4.h"

/*
 * Common audio interface for SACT2/KiwiSoundEngine.
 * This is just a wrapper over the generic audio interface in audio_mixer.c.
 */

static struct id_pool wav;
static struct id_pool bgm;

// Anonymous channels for playing sounds in-engine (the parts' cursor and click
// sounds, motion and flash sounds). audio_update must be called periodically
// to clean up channels that have finished playing.
//
// They are channels of their own, never IDs of the wav or bgm pool: a
// script's IDs (KiwiSoundEngine's Prepare, Play, IsPlay and the rest, and
// the SE IDs of PlaySe while it uses the wav pool) are fixed numbers, which an
// anonymous channel taking the pool's lowest unused ID could be given while
// the script had not prepared it yet. The script's Prepare of that ID then
// replaced the engine's sound, and audio_update closed the script's channel
// when it was not playing yet (prepared, its Play to come). The original
// keeps them apart as well: the parts engine's CSound plays through the
// sound engine's SE table (PlaySe(3, name), 0x5ba830 -> 0x4a3340), whose IDs
// are apart from the general IDs (two tables, 0x4a2f00), so no general
// function reaches it.
#define NR_ANONYMOUS_CHANNELS 64
static struct channel *anonymous_channels[NR_ANONYMOUS_CHANNELS];

void audio_init(void)
{
	static bool audio_initialized = false;
	if (audio_initialized)
		return;

	id_pool_init(&wav, 0);
	id_pool_init(&bgm, 0);

	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		anonymous_channels[i] = NULL;
	}

	mixer_init();
	audio_initialized = true;
}

// Trace (XSYS4_STAGE2_TRACE): the channels a reset closes, so that a run's
// log shows what a restart (system.Reset) found open or still playing.
static bool audio_trace(void)
{
	static int enabled = -1;
	if (enabled < 0) {
		const char *v = getenv("XSYS4_STAGE2_TRACE");
		enabled = v && *v && strcmp(v, "0");
	}
	return enabled;
}

void audio_reset(void)
{
	bool trace = audio_trace();
	int nr_wav = 0, nr_bgm = 0;
	for (int i = id_pool_get_first(&wav); i >= 0; i = id_pool_get_next(&wav, i)) {
		if (trace)
			NOTICE("S2 sound reset wav id=%d playing=%d", i, wav_is_playing(i));
		nr_wav++;
		wav_unprepare(i);
	}
	for (int i = id_pool_get_first(&bgm); i >= 0; i = id_pool_get_next(&bgm, i)) {
		if (trace)
			NOTICE("S2 sound reset bgm id=%d playing=%d", i, bgm_is_playing(i));
		nr_bgm++;
		bgm_unprepare(i);
	}
	int nr_anonymous = 0;
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (anonymous_channels[i]) {
			nr_anonymous++;
			channel_close(anonymous_channels[i]);
		}
		anonymous_channels[i] = NULL;
	}
	if (trace) {
		NOTICE("S2 sound reset closed wav=%d bgm=%d", nr_wav, nr_bgm);
		if (nr_anonymous)
			NOTICE("S2 sound reset closed anonymous=%d", nr_anonymous);
	}
}

void audio_update(void)
{
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (anonymous_channels[i] && !channel_is_playing(anonymous_channels[i])) {
			channel_close(anonymous_channels[i]);
			anonymous_channels[i] = NULL;
		}
	}
}

// An anonymous channel plays ch (opened, not playing); false, and ch closed,
// when no channel is free or it does not play.
static bool audio_play_anonymous(struct channel *ch)
{
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (!anonymous_channels[i]) {
			if (!channel_play(ch)) {
				channel_close(ch);
				return false;
			}
			anonymous_channels[i] = ch;
			return true;
		}
	}
	WARNING("Failed to allocate anonymous audio channel");
	channel_close(ch);
	return false;
}

static bool audio_anonymous_free(void)
{
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (!anonymous_channels[i])
			return true;
	}
	WARNING("Failed to allocate anonymous audio channel");
	return false;
}

// The sound is opened as wav_prepare opens it (the sound archive, bStreaming
// 0, so a v14 group from its timemap row), in a channel of its own.
bool audio_play_sound(int sound_no)
{
	if (!audio_anonymous_free())
		return false;
	struct channel *ch = channel_open_streaming(ASSET_SOUND, sound_no, false);
	return ch && audio_play_anonymous(ch);
}

bool audio_play_archive_data(struct archive_data *dfile)
{
	if (!audio_anonymous_free()) {
		archive_free_data(dfile);
		return false;
	}
	struct channel *ch = channel_open_archive_data(dfile);
	return ch && audio_play_anonymous(ch);
}

bool wav_exists(int no) { return asset_exists(ASSET_SOUND, no); }
bool bgm_exists(int no) { return asset_exists(ASSET_BGM, no); }

static int audio_prepare_streaming(struct id_pool *pool, int id, enum asset_type type, int no, bool streaming)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open_streaming(type, no, streaming);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

static int audio_prepare(struct id_pool *pool, int id, enum asset_type type, int no)
{
	return audio_prepare_streaming(pool, id, type, no, false);
}

int wav_prepare(int id, int no) { return audio_prepare(&wav, id, ASSET_SOUND, no); }
int wav_prepare_voice(int id, int no) { return audio_prepare(&wav, id, ASSET_VOICE, no); }
int bgm_prepare(int id, int no) { return audio_prepare(&bgm, id, ASSET_BGM, no); }

int wav_prepare_streaming(int id, enum asset_type type, int no, bool streaming)
{
	return audio_prepare_streaming(&wav, id, type, no, streaming);
}

int bgm_prepare_streaming(int id, int no, bool streaming)
{
	return audio_prepare_streaming(&bgm, id, ASSET_BGM, no, streaming);
}

int audio_get_channel_group(int id)
{
	struct channel *ch = id >= 0 ? id_pool_get(&wav, id) : NULL;
	if (!ch && id >= 0)
		ch = id_pool_get(&bgm, id);
	return ch ? channel_get_group(ch) : -1;
}

/*
 * v14 sound groups: KiwiSoundEngine's system sound (SE) table.
 *
 * Original (static disassembly of the CN executable): the sound engine keeps
 * two tables keyed by an int ID (both built by 0x4a2f00; hash maps, 0x442fc0):
 * the general IDs' (+0xc8: Prepare, Play, Stop, IsPlay, Unprepare,
 * GetGroupNum, GetFreeID) and the SE IDs' (+0xf8: the six SE functions), so
 * the two ID spaces are apart and no general function reaches an SE channel.
 * An SE entry (0x499780, got or made; a new one takes the table's defaults,
 * PlaySpan 0 and bMultiPlay 1, 0x4a2fa5 and 0x4a2fac) has PlaySpan,
 * bMultiPlay and its channels by the sound name PlaySe was given (a
 * multimap, compared byte for byte, 0x49bbd0; channels of one name in the
 * order they came). Its channels have bStreaming 0 (0x4a0040(0) at 0x49b8fc),
 * so they take the timemap's group of the file, else 2, and that group's
 * internal volume, as a Prepare without bStreaming does (0x4a3460, 0x4a11b0).
 *
 * Here an entry holds channels of its own (never in the wav or bgm pool),
 * and the table is used on the main thread only: by the HLL functions and by
 * KiwiSoundEngine's _ModuleFini. A channel is freed with channel_close, which
 * stops it under the device lock before freeing it, as every channel is. The
 * entry's second table (+0x14: the engine's own sounds from memory, as the
 * parts engine's CSound plays them) is not here: those sounds play through
 * audio_play_sound (G4).
 */
struct se_channel {
	char *name;
	struct channel *ch;	// NULL only inside audio_se_play
};

struct se_entry {
	int id;
	int play_span;
	bool multi_play;
	struct se_channel *channels;
	int nr_channels;
};

static struct se_entry *se_entries;
static int se_nr_entries;

static struct se_entry *se_find(int id)
{
	for (int i = 0; i < se_nr_entries; i++) {
		if (se_entries[i].id == id)
			return &se_entries[i];
	}
	return NULL;
}

// 0x499780: the entry of the ID, made with the table's defaults when there is
// none. The pointer lasts until the next entry is made.
static struct se_entry *se_get(int id)
{
	struct se_entry *e = se_find(id);
	if (e)
		return e;
	se_entries = xrealloc_array(se_entries, se_nr_entries, se_nr_entries + 1, sizeof(struct se_entry));
	e = &se_entries[se_nr_entries++];
	*e = (struct se_entry){ .id = id, .play_span = 0, .multi_play = true };
	return e;
}

static void se_remove(struct se_entry *e, int i)
{
	if (e->channels[i].ch)
		channel_close(e->channels[i].ch);
	free(e->channels[i].name);
	memmove(&e->channels[i], &e->channels[i + 1], (size_t)(e->nr_channels - i - 1) * sizeof(struct se_channel));
	e->nr_channels--;
}

static void se_clear(struct se_entry *e)
{
	while (e->nr_channels)
		se_remove(e, e->nr_channels - 1);
	free(e->channels);
	e->channels = NULL;
}

// 0x49ab10: frees the entry's channels that do not play, but `keep`.
static void se_collect(struct se_entry *e, const struct channel *keep)
{
	for (int i = 0; i < e->nr_channels; ) {
		if (e->channels[i].ch != keep && !channel_is_playing(e->channels[i].ch))
			se_remove(e, i);
		else
			i++;
	}
}

// SetSeParam (0x4a6550): makes the entry and sets both; true.
bool audio_se_set_param(int se_id, int play_span, bool multi_play)
{
	struct se_entry *e = se_get(se_id);
	e->play_span = play_span;
	e->multi_play = multi_play;
	return true;
}

// The file of a name as PlaySe opens it, with bStreaming 0: in the sound
// archive, then the voice archive (as before this table: port strategy; the
// original's order in 0x4a4050 was not traced).
static struct channel *se_open(const char *name)
{
	static const enum asset_type types[] = { ASSET_SOUND, ASSET_VOICE };
	for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		int no;
		if (asset_exists_by_name(types[i], name, &no))
			return channel_open_streaming(types[i], no, false);
	}
	return NULL;
}

/*
 * PlaySe (0x4a3340: the entry, got or made, then 0x49b790 picks the channel).
 * When the name has channels: with PlaySpan > 0, one of them that plays at a
 * position below PlaySpan (unsigned, 0x49b85a) drops the call: false, nothing
 * plays; else without bMultiPlay the first of them is stopped and prepared
 * again. Otherwise, and when the name has none, a new channel. Each time the
 * entry's channels that do not play are freed first, but the one taken again
 * (0x49ab10). The channel is then prepared from the name (0x4a3460: false
 * when there is no file) and played.
 *
 * Port strategies: the position is the port's own (channel_get_pos: the
 * frames the channel's refill has read, which counts from the chunk after the
 * zeroed first one in steps of a chunk; the original reads its buffer's
 * GetPos, inferred to be the time played, 0x6fc3f0), so the game's 40 ms end
 * at the second refill, 46 to 70 ms after the call with the device's
 * 1024-frame callbacks; a channel taken again is closed and opened anew from
 * the name in its place (its first chunk is a zeroed buffer, as after every
 * Play); a name without a file leaves no channel (the original's stays, not
 * playing, until the entry's next collection, which no function here can
 * tell apart).
 */
bool audio_se_play(int se_id, const char *name)
{
	struct se_entry *e = se_get(se_id);
	struct channel *first = NULL;
	for (int i = 0; i < e->nr_channels; i++) {
		struct channel *ch = e->channels[i].ch;
		if (strcmp(e->channels[i].name, name))
			continue;
		if (!first)
			first = ch;
		if (e->play_span > 0 && channel_is_playing(ch)
		    && (unsigned)channel_get_pos(ch) < (unsigned)e->play_span) {
			se_collect(e, NULL);
			return false;
		}
	}
	int slot;
	if (first && !e->multi_play) {
		se_collect(e, first);
		for (slot = 0; e->channels[slot].ch != first; slot++)
			;
		channel_close(first);
		e->channels[slot].ch = NULL;
	} else {
		se_collect(e, NULL);
		e->channels = xrealloc_array(e->channels, e->nr_channels, e->nr_channels + 1, sizeof(struct se_channel));
		slot = e->nr_channels++;
		e->channels[slot] = (struct se_channel){ .name = xstrdup(name), .ch = NULL };
	}
	struct channel *ch = se_open(name);
	if (!ch) {
		se_remove(e, slot);
		return false;
	}
	e->channels[slot].ch = ch;
	return channel_play(ch);
}

// StopSe (0x4998c0 -> 0x49a9b0): frees every channel of the entry and keeps
// the entry and its parameters; false without an entry.
bool audio_se_stop(int se_id)
{
	struct se_entry *e = se_find(se_id);
	if (!e)
		return false;
	se_clear(e);
	return true;
}

// IsPlaySe (0x499920 -> 0x49ad40): a channel of the entry plays.
bool audio_se_is_playing(int se_id)
{
	struct se_entry *e = se_find(se_id);
	for (int i = 0; e && i < e->nr_channels; i++) {
		if (channel_is_playing(e->channels[i].ch))
			return true;
	}
	return false;
}

/*
 * GetFreeSeID (0x499700): the first ID from min on that has no entry, max not
 * included; 0 when there is none, and for min == max. As in the original the
 * IDs go up in 32 bits and wrap, so with min > max the search goes on past
 * INT_MAX; it stops within one ID more than there are entries.
 */
int audio_se_get_free_id(int min_id, int max_id)
{
	for (uint32_t id = (uint32_t)min_id; id != (uint32_t)max_id; id++) {
		if (!se_find((int)id))
			return (int)id;
	}
	return 0;
}

int audio_se_reset(void)
{
	int n = 0;
	for (int i = 0; i < se_nr_entries; i++) {
		n += se_entries[i].nr_channels;
		se_clear(&se_entries[i]);
	}
	free(se_entries);
	se_entries = NULL;
	se_nr_entries = 0;
	return n;
}

static int audio_unprepare(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_release(pool, id);
	if (!ch)
		return 0;
	channel_close(ch);
	return 1;
}

int wav_unprepare(int id) { return audio_unprepare(&wav, id); }
int bgm_unprepare(int id) { return audio_unprepare(&bgm, id); }

static int audio_play(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch) {
		// NOTE: SACT2 brings up error GUI here
		WARNING("Tried to play from unprepared audio channel");
		return 0;
	}
	return channel_play(ch);
}
int wav_play(int id) { return audio_play(&wav, id); }
int bgm_play(int id) { return audio_play(&bgm, id); }

static int audio_stop(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch) {
		return 0;
	}
	return channel_stop(ch);
}
int wav_stop(int id) { return audio_stop(&wav, id); }
int bgm_stop(int id) { return audio_stop(&bgm, id); }

static int audio_is_playing(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	return ch && channel_is_playing(ch);
}
bool wav_is_playing(int id) { return audio_is_playing(&wav, id); }
bool bgm_is_playing(int id) { return audio_is_playing(&bgm, id); }

static int audio_set_loop_count(struct id_pool *pool, int id, int count)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_count(ch, count);
}
int wav_set_loop_count(int id, int count) { return audio_set_loop_count(&wav, id, count); }
int bgm_set_loop_count(int id, int count) { return audio_set_loop_count(&bgm, id, count); }

static int audio_get_loop_count(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_loop_count(ch);
}
int wav_get_loop_count(int id) { return audio_get_loop_count(&wav, id); }
int bgm_get_loop_count(int id) { return audio_get_loop_count(&bgm, id); }

static int audio_set_loop_start_pos(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_start_pos(ch, pos);
}
int wav_set_loop_start_pos(int id, int pos) { return audio_set_loop_start_pos(&wav, id, pos); }
int bgm_set_loop_start_pos(int id, int pos) { return audio_set_loop_start_pos(&bgm, id, pos); }

static int audio_set_loop_end_pos(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_end_pos(ch, pos);
}
int wav_set_loop_end_pos(int id, int pos) { return audio_set_loop_end_pos(&wav, id, pos); }
int bgm_set_loop_end_pos(int id, int pos) { return audio_set_loop_end_pos(&bgm, id, pos); }

static int audio_fade(struct id_pool *pool, int id, int time, int volume, bool stop)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_fade(ch, time, volume, stop);
}
int wav_fade(int id, int time, int volume, bool stop) { return audio_fade(&wav, id, time, volume, stop); }
int bgm_fade(int id, int time, int volume, bool stop) { return audio_fade(&bgm, id, time, volume, stop); }

static int audio_stop_fade(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_stop_fade(ch);
}
int wav_stop_fade(int id) { return audio_stop_fade(&wav, id); }
int bgm_stop_fade(int id) { return audio_stop_fade(&bgm, id); }

static bool audio_is_fading(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_is_fading(ch);
}
bool wav_is_fading(int id) { return audio_is_fading(&wav, id); }
bool bgm_is_fading(int id) { return audio_is_fading(&bgm, id); }

static int audio_pause(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_pause(ch);
}
int wav_pause(int id) { return audio_pause(&wav, id); }
int bgm_pause(int id) { return audio_pause(&bgm, id); }

static int audio_restart(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_restart(ch);
}
int wav_restart(int id) { return audio_restart(&wav, id); }
int bgm_restart(int id) { return audio_restart(&bgm, id); }

static bool audio_is_paused(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_is_paused(ch);
}
bool wav_is_paused(int id) { return audio_is_paused(&wav, id); }
bool bgm_is_paused(int id) { return audio_is_paused(&bgm, id); }

static int audio_get_pos(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_pos(ch);
}
int wav_get_pos(int id) { return audio_get_pos(&wav, id); }
int bgm_get_pos(int id) { return audio_get_pos(&bgm, id); }

static int audio_get_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_length(ch);
}
int wav_get_length(int id) { return audio_get_length(&wav, id); }
int bgm_get_length(int id) { return audio_get_length(&bgm, id); }

static int audio_get_sample_pos(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_sample_pos(ch);
}
int wav_get_sample_pos(int id) { return audio_get_sample_pos(&wav, id); }
int bgm_get_sample_pos(int id) { return audio_get_sample_pos(&bgm, id); }

static int audio_get_sample_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_sample_length(ch);
}
int wav_get_sample_length(int id) { return audio_get_sample_length(&wav, id); }
int bgm_get_sample_length(int id) { return audio_get_sample_length(&bgm, id); }

static int audio_seek(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_seek(ch, pos);
}
int wav_seek(int id, int pos) { return audio_seek(&wav, id, pos); }
int bgm_seek(int id, int pos) { return audio_seek(&bgm, id, pos); }

int wav_get_unused_channel(void) { return id_pool_get_unused(&wav); }
int bgm_get_unused_channel(void) { return id_pool_get_unused(&bgm); }

static int audio_reverse_LR(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_reverse_LR(ch);
}
int wav_reverse_LR(int id) { return audio_reverse_LR(&wav, id); }
int bgm_reverse_LR(int id) { return audio_reverse_LR(&bgm, id); }

static int audio_get_volume(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_volume(ch);
}
int wav_get_volume(int id) { return audio_get_volume(&wav, id); }
int bgm_get_volume(int id) { return audio_get_volume(&bgm, id); }

static int audio_get_time_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_time_length(ch);
}
int wav_get_time_length(int id) { return audio_get_time_length(&wav, id); }
int bgm_get_time_length(int id) { return audio_get_time_length(&bgm, id); }

static int audio_get_data_length(enum asset_type type, int no) {
	struct channel *ch = channel_open(type, no);
	if (!ch)
		return 0;
	int len = channel_get_time_length(ch);
	channel_close(ch);
	return len;
}

int wav_get_data_length(int no) { return audio_get_data_length(ASSET_SOUND, no); }
int bgm_get_data_length(int no) { return audio_get_data_length(ASSET_BGM, no); }

static int audio_get_data_no(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return -1;
	return channel_get_data_no(ch);
}

int wav_get_group_num(int id)
{
	int no = audio_get_data_no(&wav, id);
	if (no < 0)
		return 0;
	return wav_get_group_num_from_data_num(no);
}
//int bgm_get_group_num(int channel);

static int audio_prepare_from_file(struct id_pool *pool, int id, char *filename)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open_file(filename);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare_from_file(int id, char *filename) { return audio_prepare_from_file(&wav, id, filename); }
int bgm_prepare_from_file(int id, char *filename) { return audio_prepare_from_file(&bgm, id, filename); }

static int audio_prepare_from_archive_data(struct id_pool *pool, int id, struct archive_data *dfile)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open_archive_data(dfile);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare_from_archive_data(int id, struct archive_data *dfile) {
	return audio_prepare_from_archive_data(&wav, id, dfile);
}

int bgm_prepare_from_archive_data(int id, struct archive_data *dfile) {
	return audio_prepare_from_archive_data(&bgm, id, dfile);
}

int wav_get_group_num_from_data_num(int no)
{
	struct wai *wai = wai_get(no);
	return wai ? wai->channel : 0;
}
