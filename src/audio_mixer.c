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

#include <stdatomic.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

#include <sndfile.h>
#include <SDL.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "mixer.h"
#include "vm.h"
#include "xsystem4.h"

#define clamp(min_value, max_value, value) min(max_value, max(min_value, value))
#define muldiv(x, y, denom) ((int64_t)(x) * (int64_t)(y) / (int64_t)(denom))

/*
 * The actual mixer implementation is contained in this header.
 * The rest of this file implements a high level interface for managing mixer
 * channels (loading audio, starting/stopping, etc.)
 */
#define STS_MIXER_IMPLEMENTATION
#include "sts_mixer.h"

#define CHUNK_SIZE 1024

struct fade {
	atomic_bool fading;
	bool stop;
	uint_least32_t start_pos;
	uint_least32_t frames;
	uint_least32_t elapsed;
	float start_volume;
	float end_volume;
};

struct channel {
	// archive data
	struct archive_data *dfile;
	int no;
	int mixer_no;

	// audio file data
	SNDFILE *file;
	SF_INFO info;
	sf_count_t offset;

	// stream data
	atomic_int voice;
	sts_mixer_stream_t stream;
	float data[CHUNK_SIZE * 2];

	// main thread read-only
	atomic_uint_least32_t frame;

	atomic_uint volume;
	atomic_bool swapped;
	uint_least32_t loop_start;
	uint_least32_t loop_end;
	atomic_uint loop_count;
	struct fade fade;

	// v14 sound groups: refilled by cb_read_frames_grouped (see there)
	bool grouped;
};

struct mixer {
	sts_mixer_t mixer;
	sts_mixer_stream_t stream;
	int voice;
	atomic_bool muted;
	float data[CHUNK_SIZE * 2];
	char *name;

	struct mixer *parent;
	struct mixer **children;
	int nr_children;

	// v14 sound groups: the integer volumes the game reads back. `volume`
	// is the current one (0..100, `mixer.gain` is volume / 100); both are
	// written on the main thread only, `mixer.gain` under the device lock.
	int volume;
	int default_volume;
};

static struct mixer *master = NULL;
static struct mixer *mixers = NULL;
static int nr_mixers = 0;

static SDL_AudioDeviceID audio_device = 0;

// v14 sound groups (see mixer_init): set once before the device starts.
static bool groups_enabled = false;
// The game's SetGlobalFocus and the window's focus; with groups, the output
// is silent while the window is inactive and GlobalFocus is off.
static bool global_focus = false;
static bool window_active = true;
static atomic_bool focus_muted;

/*
 * The SDL2 audio callback.
 */
static void audio_callback(possibly_unused void *data, Uint8 *stream, int len)
{
	if (!master) {
		memset(stream, 0, len);
		return;
	}
	sts_mixer_mix_audio(&master->mixer, stream, len / (sizeof(float) * 2));
	if (master->muted || focus_muted) {
		memset(stream, 0, len);
	}
}

/*
 * Seek to the specified position in the stream.
 * Returns true if the seek succeeded, otherwise returns false.
 */
static bool cb_seek(struct channel *ch, uint_least32_t pos)
{
	if (pos > ch->info.frames) {
		pos = ch->info.frames;
	}
	sf_count_t r = sf_seek(ch->file, pos, SEEK_SET);
	if (r < 0) {
		WARNING("sf_seek failed");
		return false;
	}
	ch->frame = r;
	return true;
}

/*
 * Seek to loop start, if the stream should loop.
 * Returns true if the stream should loop, false if it should stop.
 */
static bool cb_loop(struct channel *ch)
{
	if (!cb_seek(ch, ch->loop_start) || ch->loop_count == 1) {
		return false;
	}
	if (ch->loop_count > 1) {
		ch->loop_count--;
	}
	return true;
}

/*
 * Callback to refill the stream's audio data.
 * Called from sys_mixer_mix_audio.
 */
static int cb_read_frames(struct channel *ch, float *out, sf_count_t frame_count, uint_least32_t *num_read)
{
	*num_read = 0;

	// handle case where chunk crosses loop point (seamless)
	// NOTE: it's assumed that the length of the loop is greater than the chunk length
	if (ch->frame + frame_count >= ch->loop_end) {
		// read frames up to loop_end
		*num_read = sf_readf_float(ch->file, out, ch->loop_end - ch->frame);
		// adjust parameters for later
		ch->frame += *num_read;
		out += *num_read;
		frame_count -= *num_read;
		// seek to loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	} else if (ch->frame >= ch->loop_end) {
		// seek to loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	}

	// read remaining data
	sf_count_t n = sf_readf_float(ch->file, out, frame_count);
	*num_read += n;
	ch->frame += n;
	out += *num_read;
	frame_count -= *num_read;

	// XXX: This *shouldn't* be necessary, but sometimes libsndfile seems to stop reading
	//      just before the end of file (i.e. ch->frame + frame_count is < ch->info.frames,
	//      yet sf_readf_float returns less that the requested amount of frames).
	//      Not sure if this is a bug in xsystem4 or libsndfile
	if (frame_count > 0) {
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
		*num_read += sf_readf_float(ch->file, out, frame_count);
	}

	return STS_STREAM_CONTINUE;
}

/*
 * The refill of a channel in the v14 sound groups (cb_read_frames is kept as it
 * is for every other channel). It fills the chunk across any number of loop
 * points, so a loop shorter than the chunk and a loop end at a chunk edge
 * work, it advances stereo data by samples, not frames, and a sound that ends
 * inside the chunk plays that chunk: the stream is complete on the next
 * refill, which reads nothing.
 *
 * Port strategy, not the original's behavior (its backend was not traced
 * down to the decoder): [loop_start, loop_end) repeats while laps are left
 * (loop_count 0 for ever, n > 1 for n laps); the last lap goes on to the end
 * of the file.
 */
static int cb_read_frames_grouped(struct channel *ch, float *out, sf_count_t frame_count, uint_least32_t *num_read)
{
	int nr_channels = ch->info.channels;
	sf_count_t done = 0;
	int idle = 0;
	while (done < frame_count && idle < 3) {
		bool looping = ch->loop_count != 1;
		uint_least32_t limit = looping ? ch->loop_end : (uint_least32_t)ch->info.frames;
		if (ch->frame >= limit) {
			if (!looping || !cb_seek(ch, ch->loop_start))
				break;
			if (ch->loop_count > 1)
				ch->loop_count--;
			idle++;
			continue;
		}
		sf_count_t n = sf_readf_float(ch->file, out + done * nr_channels, min(frame_count - done, (sf_count_t)(limit - ch->frame)));
		if (n <= 0) {
			// The decoder stopped before the limit (see the XXX in
			// cb_read_frames): that is where this lap ends.
			if (!looping || !cb_seek(ch, ch->loop_start))
				break;
			if (ch->loop_count > 1)
				ch->loop_count--;
			idle++;
			continue;
		}
		ch->frame += n;
		done += n;
		idle = 0;
	}
	*num_read = done;
	return done > 0 ? STS_STREAM_CONTINUE : STS_STREAM_COMPLETE;
}

/*
 * Calculate the gain for a fade at a given sample.
 */
static float cb_calc_fade(struct fade *fade)
{
	if (fade->elapsed >= fade->frames)
		return fade->stop ? 0.0 : fade->end_volume;

	float progress = (float)fade->elapsed / (float)fade->frames;
	float delta_v = fade->end_volume - fade->start_volume;
	float gain = fade->start_volume + delta_v * progress;
	if (gain < 0.0) return 0.0;
	if (gain > 1.0) return 1.0;
	return gain;
}

static int refill_stream(sts_mixer_sample_t *sample, void *data)
{
	struct channel *ch = data;
	uint_least32_t frames_read;
	memset(ch->data, 0, sizeof(float) * sample->length);

	// read audio data from file
	int r = ch->grouped ? cb_read_frames_grouped(ch, ch->data, CHUNK_SIZE, &frames_read)
		: cb_read_frames(ch, ch->data, CHUNK_SIZE, &frames_read);

	// convert mono to stereo
	if (ch->info.channels == 1) {
		for (int i = CHUNK_SIZE-1; i >= 0; i--) {
			ch->data[i*2+1] = ch->data[i];
			ch->data[i*2] = ch->data[i];
		}
	}

	// reverse LR channels
	else if (ch->swapped) {
		for (int i = 0; i < CHUNK_SIZE; i++) {
			float tmp = ch->data[i*2];
			ch->data[i*2] = ch->data[i*2+1];
			ch->data[i*2+1] = tmp;
		}
	}

	// set gain for fade
	if (ch->fade.fading) {
		float gain = cb_calc_fade(&ch->fade);
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
		ch->volume = gain * 100.0;

		ch->fade.elapsed += frames_read;
		if (ch->fade.elapsed >= ch->fade.frames) {
			ch->fade.fading = false;
			ch->volume = ch->fade.end_volume * 100.0;
			if (ch->fade.stop) {
				cb_seek(ch, 0);
				r = STS_STREAM_COMPLETE;
			}
		}
	} else {
		float gain = ch->volume / 100.0;
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
	}

	if (r == STS_STREAM_COMPLETE) {
		ch->voice = -1;
	}

	return r;
}

static int refill_mixer(sts_mixer_sample_t *sample, void *data)
{
	struct mixer *mixer = data;
	sts_mixer_mix_audio(&mixer->mixer, &mixer->data, CHUNK_SIZE);
	if (mixer->muted) {
		memset(mixer->data, 0, sizeof(float) * sample->length);
	}

	return STS_STREAM_CONTINUE;
}

int channel_play(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice >= 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 1;
	}
	memset(ch->data, 0, sizeof(ch->data));
	ch->voice = sts_mixer_play_stream(&mixers[ch->mixer_no].mixer, &ch->stream, 1.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_stop(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice < 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 1;
	}
	cb_seek(ch, 0);
	sts_mixer_stop_voice(&mixers[ch->mixer_no].mixer, ch->voice);
	ch->voice = -1;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_is_playing(struct channel *ch)
{
	return ch->voice >= 0;
}

int channel_set_loop_count(struct channel *ch, int count)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_count = count;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_get_loop_count(struct channel *ch)
{
	return ch->loop_count;
}

int channel_set_loop_start_pos(struct channel *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_start = pos;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_set_loop_end_pos(struct channel *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_end = pos;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_fade(struct channel *ch, int time, int volume, bool stop)
{
	if (!time && stop)
		return channel_stop(ch);

	SDL_LockAudioDevice(audio_device);
	if (!time) {
		// XXX: Fade with time=0 is used to set volume. This needs to
		//      take effect immediately, not via the audio callback
		//      because the stream isn't necessarily playing yet.
		//      E.g. the below call sequence should fade from 0 -> 33:
		//
		//          SACT2.Music_Fade(ch, 0, 0, 0);
		//          SACT2.Music_Fade(ch, 33, 4000, 0);
		//          SACT2.Music_Play(ch);
		ch->fade.fading = false;
		ch->volume = max(0, min(100, volume));
	} else {
		ch->fade.fading = true;
		ch->fade.stop = stop;
		ch->fade.start_pos = ch->frame;
		ch->fade.start_volume = (float)ch->volume / 100.0;
		ch->fade.frames = muldiv(time, ch->info.samplerate, 1000);
		ch->fade.elapsed = 0;
		ch->fade.end_volume = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	}
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_stop_fade(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	// XXX: we need to set the volume to end_volume and potentially stop the
	//      stream here; better to let the callback do it
	ch->fade.elapsed = ch->fade.frames;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_is_fading(struct channel *ch)
{
	return ch->fade.fading;
}

int channel_pause(possibly_unused struct channel *ch)
{
	// NOTE: not implemented in Sengoku Rance's SACT2.dll
	WARNING("channel_pause not implemented");
	return 0;
}

int channel_restart(possibly_unused struct channel *ch)
{
	// NOTE: not implemented in Sengoku Rance's SACT2.dll
	WARNING("channel_restart not implemented");
	return 0;
}

int channel_is_paused(possibly_unused struct channel *ch)
{
	return 0;
}

int channel_get_pos(struct channel *ch)
{
	return muldiv(ch->frame, 1000, ch->info.samplerate);
}

int channel_get_length(struct channel *ch)
{
	// FIXME: how is this different than channel_get_time_length?
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
}

int channel_get_sample_pos(struct channel *ch)
{
	return ch->frame;
}

int channel_get_sample_length(struct channel *ch)
{
	return ch->info.frames;
}

int channel_seek(struct channel *ch, int pos)
{
	// NOTE: SACT2.Music_Seek doesn't seem to do anything in Sengoku Rance...
	SDL_LockAudioDevice(audio_device);
	int r = cb_seek(ch, muldiv(pos, ch->info.samplerate, 1000));
	SDL_UnlockAudioDevice(audio_device);
	return r;
}

int channel_reverse_LR(struct channel *ch)
{
	ch->swapped = !ch->swapped;
	return 1;
}

int channel_get_volume(struct channel *ch)
{
	return ch->volume;
}

int channel_get_time_length(struct channel *ch)
{
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
}

int channel_get_data_no(struct channel *ch)
{
	return ch->dfile ? ch->dfile->no : -1;
}

static sf_count_t channel_vio_get_filelen(void *data)
{
	return ((struct channel*)data)->dfile->size;
}

static sf_count_t channel_vio_seek(sf_count_t offset, int whence, void *data)
{
	struct channel *ch = data;
	switch (whence) {
	case SEEK_CUR:
		ch->offset += offset;
		break;
	case SEEK_SET:
		ch->offset = offset;
		break;
	case SEEK_END:
		ch->offset = ch->dfile->size + offset;
		break;
	}
	ch->offset = clamp(0, (sf_count_t)ch->dfile->size, ch->offset);
	return ch->offset;
}

static sf_count_t channel_vio_read(void *ptr, sf_count_t count, void *data)
{
	struct channel *ch = data;
	sf_count_t c = min(count, (sf_count_t)ch->dfile->size - ch->offset);
	memcpy(ptr, ch->dfile->data + ch->offset, c);
	ch->offset += c;
	return c;
}

static sf_count_t channel_vio_write(possibly_unused const void *ptr,
				    possibly_unused sf_count_t count,
				    possibly_unused void *user_data)
{
	ERROR("sndfile vio write not supported");
}

static sf_count_t channel_vio_tell(void *data)
{
	return ((struct channel*)data)->offset;
}

static SF_VIRTUAL_IO channel_vio = {
	.get_filelen = channel_vio_get_filelen,
	.seek = channel_vio_seek,
	.read = channel_vio_read,
	.write = channel_vio_write,
	.tell = channel_vio_tell
};

/*
 * v14 sound groups: the group and the loop points of a sound from its archive's
 * timemap (asset_sound_info_by_entry), by the file's name. A file without a row
 * (or with a negative group) takes the fallback: Prepare's bStreaming argument
 * picks it in the original (nonzero 1, zero 2; 0x4a3568, the argument reaches
 * the channel through 0x4a3430 -> 0x49a130 -> 0x4a0040), and every other way
 * in (PlaySe, a file, the engine's own sounds) takes 2 here (port strategy).
 * A group the table does not have also takes the fallback (port strategy).
 * The loop points are used when 0 <= begin < end and begin is inside the
 * file; the end is exclusive and at most the file's length (port strategy:
 * the original hands both to its backend, 0x4a357b; how that treats an end of
 * 0, an end before the begin or past the file was not traced). Otherwise the
 * whole file loops, as without groups.
 */
static void channel_set_group(struct channel *ch, enum asset_type type, bool streaming)
{
	ch->grouped = true;
	ch->mixer_no = mixer_group_fallback(streaming);
	// The grouped refill needs begin < end; a BGI's loop (BGM) may not have
	// it: then the whole file loops.
	if (ch->loop_start >= ch->loop_end) {
		ch->loop_start = 0;
		ch->loop_end = ch->info.frames;
	}
	if (!ch->dfile || (type != ASSET_SOUND && type != ASSET_VOICE))
		return;
	struct sound_info si;
	if (!asset_sound_info_by_entry(type, ch->dfile->name, &si))
		return;
	if (si.group >= 0 && si.group < nr_mixers)
		ch->mixer_no = si.group;
	if (si.loop_begin >= 0 && si.loop_begin < si.loop_end && si.loop_begin < ch->info.frames) {
		ch->loop_start = si.loop_begin;
		ch->loop_end = min((sf_count_t)si.loop_end, ch->info.frames);
	}
}

static struct channel *_channel_open(enum asset_type type, int no, bool streaming)
{
	// get file from archive
	struct archive_data *dfile = asset_get(type, no);
	if (!dfile) {
		WARNING("Failed to load %s %d", type == ASSET_SOUND ? "WAV" : "BGM", no);
		return NULL;
	}

	struct channel *ch = channel_open_archive_data(dfile);
	if (!ch) {
		WARNING("Failed to open %s %d", type == ASSET_SOUND ? "WAV" : "BGM", no);
		return NULL;
	}

	if (type == ASSET_SOUND || type == ASSET_VOICE) {
		struct wai *wai = (type == ASSET_SOUND) ? wai_get(no) : NULL;
		ch->volume = 100;
		ch->loop_start = 0;
		ch->loop_end = ch->info.frames;
		ch->loop_count = 1;
		ch->mixer_no = wai ? wai->channel : 1;
	} else {
		struct bgi *bgi = bgi_get(no);
		if (bgi) {
			ch->volume = clamp(0, 100, bgi->volume);
			ch->loop_start = clamp(0, ch->info.frames, bgi->loop_start);
			ch->loop_end = clamp(0, ch->info.frames, bgi->loop_end);
			ch->loop_count = max(0, bgi->loop_count);
			ch->mixer_no = clamp(0, nr_mixers, bgi->channel);
		} else {
			ch->loop_count = 0;
		}
	}
	ch->no = no;
	if (groups_enabled) {
		// the group, not the legacy WAI/BGI mixer number
		if (type == ASSET_SOUND || type == ASSET_VOICE) {
			ch->loop_start = 0;
			ch->loop_end = ch->info.frames;
		}
		channel_set_group(ch, type, streaming);
	}

	return ch;
}

struct channel *channel_open(enum asset_type type, int no)
{
	return _channel_open(type, no, false);
}

struct channel *channel_open_streaming(enum asset_type type, int no, bool streaming)
{
	return _channel_open(type, no, streaming);
}

int channel_get_group(struct channel *ch)
{
	return ch->mixer_no;
}

static bool init_channel(struct channel *ch)
{
	if (sf_error(ch->file) != SF_ERR_NO_ERROR) {
		WARNING("sf_open_virtual failed: %s", sf_strerror(ch->file));
		return false;
	}
	if (ch->info.channels > 2) {
		WARNING("Audio file has more than 2 channels");
		return false;
	}

	// create stream
	ch->stream.userdata = ch;
	ch->stream.callback = refill_stream;
	ch->stream.sample.frequency = ch->info.samplerate;
	ch->stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
	ch->stream.sample.length = CHUNK_SIZE * 2;
	ch->stream.sample.data = ch->data;
	ch->voice = -1;

	ch->volume = 100;
	ch->loop_start = 0;
	ch->loop_end = ch->info.frames;
	ch->loop_count = 1;
	ch->mixer_no = 0;
	if (groups_enabled) {
		// a file or archive data without a timemap row (channel_set_group)
		ch->grouped = true;
		ch->mixer_no = mixer_group_fallback(false);
	}

	ch->no = -1;
	return true;
}

struct channel *channel_open_archive_data(struct archive_data *dfile)
{
	struct channel *ch = xcalloc(1, sizeof(struct channel));
	ch->dfile = dfile;

	// open file
	ch->file = sf_open_virtual(&channel_vio, SFM_READ, &ch->info, ch);

	if (!init_channel(ch)) {
		archive_free_data(dfile);
		if (ch->file)
			sf_close(ch->file);
		free(ch);
		return NULL;
	}
	return ch;
}

struct channel *channel_open_file(const char *path)
{
	struct channel *ch = xcalloc(1, sizeof(struct channel));
#ifdef _WIN32
	wchar_t *wpath = utf8_to_wchar(path);
	ch->file = sf_wchar_open(wpath, SFM_READ, &ch->info);
	free(wpath);
#else
	ch->file = sf_open(path, SFM_READ, &ch->info);
#endif

	if (!init_channel(ch)) {
		if (ch->file)
			sf_close(ch->file);
		free(ch);
		return NULL;
	}
	return ch;
}

void channel_close(struct channel *ch)
{
	channel_stop(ch);
	sf_close(ch->file);
	if (ch->dfile)
		archive_free_data(ch->dfile);
	free(ch);
}

#define SJIS_MASTER "\x83\x7d\x83\x58\x83\x5e\x81\x5b"
#define SJIS_VOICE  "\x89\xb9\x90\xba"

/*
 * v14 sound groups.
 *
 * The original builds its mixer groups from a table of the main EX file and
 * keeps every group's default and current volume as integers. Here that is
 * done for exactly AIN v14 with the GBK string rule when the table is there
 * with the shape below; every other game, and a table of another shape, keeps
 * the mixers above (Music/Sound/Master or the INI's) and their behavior.
 *
 * Original (static disassembly of the CN executable): the number of groups is
 * the first index without a default volume (0x4a1d50; at most 64, 0x49fc20);
 * the parent of group n is 0 for n <= 5, else 3 when the row's voice-group
 * column is negative and 5 when it is not (0x4a1e50); the default volume is
 * the column's float x 100.0 truncated, the current one that clamped to
 * 0..100 (0x4a20d0, 0x49fe90); a group's gain is the product of the current
 * volumes up the parent chain with the root counted once, 0 when any of them
 * is muted (0x4a5e70). Volume.sav is read after the defaults are set (0x4a28ef,
 * 0x4a28f6) and written when the sound engine is released (0x4a26f0), which a
 * restart does too.
 *
 * Here group 0 is the master mixer and every other group a mixer whose stream
 * plays in its parent's, so the chain is multiplied by the mixers themselves
 * and no channel multiplies it again. The graph is complete before the audio
 * device starts and does not change afterwards; a restart keeps it and the
 * volumes (port strategy: the original writes Volume.sav on a restart and
 * presumably reads it back when the engine is rebuilt, which was not traced).
 */
#define GROUPS_MAX 64
// The group table of the main EX file and its columns, by their names in the
// CN game's file (GBK): the table, the group number (an indexed int), the
// voice group (int) and the default volume (float).
#define GROUP_TABLE   "\xa3\xc5\xa3\xdf\xc2\x95\xd2\xf4\xc8\xba\xbd\x4d\xd4\x4f\xb6\xa8"
#define GROUP_NUMBER  "\xc8\xba\xbd\x4d\xbe\x8e\xcc\x96"
#define GROUP_VOICE   "\x8c\x9d\x8f\xea\xd5\x5a\xd2\xf4\xc8\xba\xbd\x4d\xbe\x8e\xcc\x96"
#define GROUP_DEFAULT "\xc4\xac\xd5\x4a\xa5\xdc\xa5\xea\xa5\xe5\xa9\x60\xa5\xe0"
#define VOLUME_SAV "Volume.sav"

struct groups_config {
	int nr;
	int parent[GROUPS_MAX];
	int default_volume[GROUPS_MAX];
};

static int groups_row(struct ex_table *t, int col, int key)
{
	for (unsigned r = 0; r < t->nr_rows; r++) {
		if (t->rows[r][col].i == key)
			return r;
	}
	return -1;
}

static bool groups_read_config(struct groups_config *gc)
{
	if (!ain || ain->version != 14 || !game_charset_is_gbk() || ain_get_library(ain, "MainEXFile") < 0)
		return false;
	struct ex *ex = main_ex_file();
	struct ex_value *v = ex ? ex_get(ex, GROUP_TABLE) : NULL;
	if (!v || v->type != EX_TABLE || !v->t)
		return false;
	struct ex_table *t = v->t;
	int c_no = ex_col_from_name(t, GROUP_NUMBER);
	int c_voice = ex_col_from_name(t, GROUP_VOICE);
	int c_default = ex_col_from_name(t, GROUP_DEFAULT);
	if (c_no < 0 || c_voice < 0 || c_default < 0 || !t->nr_rows
	    || t->fields[c_no].type != EX_INT || !t->fields[c_no].is_index
	    || t->fields[c_voice].type != EX_INT || t->fields[c_default].type != EX_FLOAT)
		return false;
	for (unsigned r = 0; r < t->nr_rows; r++) {
		if (t->rows[r][c_no].type != EX_INT || t->rows[r][c_voice].type != EX_INT
		    || t->rows[r][c_default].type != EX_FLOAT)
			return false;
	}
	gc->nr = 0;
	int voice[GROUPS_MAX];
	for (int g = 0; g < GROUPS_MAX; g++) {
		int r = groups_row(t, c_no, g);
		if (r < 0)
			break;
		float f = t->rows[r][c_default].f;
		if (!isfinite(f) || fabsf(f) > 1000.0f)
			return false;
		gc->default_volume[g] = (int)(f * 100.0f);
		voice[g] = t->rows[r][c_voice].i;
		gc->nr = g + 1;
	}
	// Port strategy: the fallback groups 1 and 2 must exist.
	if (gc->nr < 3)
		return false;
	int children[GROUPS_MAX] = {0};
	for (int g = 0; g < gc->nr; g++) {
		gc->parent[g] = g == 0 ? -1 : g <= 5 ? 0 : voice[g] < 0 ? 3 : 5;
		if (g > 0)
			children[gc->parent[g]]++;
	}
	// Port strategy: a group's mixer keeps at least half of its voices for
	// sounds (STS_MIXER_VOICES each); a table that needs more is not used.
	for (int g = 0; g < gc->nr; g++) {
		if (children[g] > STS_MIXER_VOICES / 2)
			return false;
	}
	return true;
}

/*
 * Volume.sav, as the original writes it (0x49f780): 0, the number of groups,
 * then for each group its current volume, 1 when it is muted (else 0) and 0.
 * The reader (0x49f980 -> 0x49fb60) skips the first byte and the third of
 * each group, takes the volume clamped to 0..100 (0x49fe40) and the mute as a
 * bool, never changes the defaults or the number of groups, and keeps the
 * defaults when the file is missing. A group that is cut short is not applied
 * here; the ones before it are (port strategy: the order of the original's
 * writes inside a group was not traced). The file is written only when what it
 * would hold differs from what it was read or last written with, and through
 * a temporary file (port strategy: the original writes it in place every
 * time). Only the save folder is used.
 */
static uint8_t volume_sav_bytes[2 + 3 * GROUPS_MAX];
static int volume_sav_len = -1;

static void groups_read_volumes(void)
{
	if (!config.save_dir)
		return;
	char *path = savedir_path(VOLUME_SAV);
	size_t len = 0;
	uint8_t *data = file_read(path, &len);
	free(path);
	if (!data)
		return;
	if (len >= 2) {
		int count = data[1];
		for (int i = 0; i < count && i < GROUPS_MAX; i++) {
			size_t at = 2 + 3 * (size_t)i;
			if (at + 3 > len)
				break;
			if (i >= nr_mixers)
				continue;
			mixers[i].volume = clamp(0, 100, (int)data[at]);
			mixers[i].mixer.gain = mixers[i].volume / 100.0f;
			mixers[i].muted = data[at + 1] != 0;
		}
	}
	if (len <= sizeof(volume_sav_bytes)) {
		memcpy(volume_sav_bytes, data, len);
		volume_sav_len = len;
	}
	free(data);
}

static bool volume_sav_write(const char *path, const uint8_t *data, size_t size)
{
#ifdef _WIN32
	// rename() does not replace an existing file there.
	return file_write(path, (uint8_t *)data, size);
#else
	size_t tmp_len = strlen(path) + 8;
	char *tmp = xmalloc(tmp_len);
	snprintf(tmp, tmp_len, "%s.xs4tmp", path);
	FILE *fp = file_open_utf8(tmp, "wb");
	if (!fp) {
		free(tmp);
		return false;
	}
	bool ok = fwrite(data, size, 1, fp) == 1;
	ok = ok && fflush(fp) == 0;
	ok = ok && fsync(fileno(fp)) == 0;
	if (fclose(fp) != 0)
		ok = false;
	ok = ok && rename(tmp, path) == 0;
	if (!ok) {
		int err = errno;
		remove_utf8(tmp);
		errno = err;
	}
	free(tmp);
	return ok;
#endif
}

void mixer_groups_save(void)
{
	if (!groups_enabled || !config.save_dir)
		return;
	uint8_t buf[sizeof(volume_sav_bytes)];
	int len = 2 + 3 * nr_mixers;
	buf[0] = 0;
	buf[1] = nr_mixers;
	for (int i = 0; i < nr_mixers; i++) {
		buf[2 + 3 * i] = mixers[i].volume;
		buf[3 + 3 * i] = mixers[i].muted ? 1 : 0;
		buf[4 + 3 * i] = 0;
	}
	if (len == volume_sav_len && !memcmp(buf, volume_sav_bytes, len))
		return;
	char *path = savedir_path(VOLUME_SAV);
	if (volume_sav_write(path, buf, len)) {
		memcpy(volume_sav_bytes, buf, len);
		volume_sav_len = len;
	} else {
		WARNING("Failed to save '%s': %s", display_utf0(path), strerror(errno));
	}
	free(path);
}

static void groups_init(const struct groups_config *gc)
{
	nr_mixers = gc->nr;
	mixers = xcalloc(nr_mixers, sizeof(struct mixer));
	master = &mixers[0];
	for (int i = 0; i < nr_mixers; i++) {
		sts_mixer_init(&mixers[i].mixer, 44100, STS_MIXER_SAMPLE_FORMAT_FLOAT);
		mixers[i].default_volume = gc->default_volume[i];
		mixers[i].volume = clamp(0, 100, gc->default_volume[i]);
		mixers[i].mixer.gain = mixers[i].volume / 100.0f;
		if (!i)
			continue;
		struct mixer *parent = &mixers[gc->parent[i]];
		mixers[i].parent = parent;
		parent->children = xrealloc_array(parent->children, parent->nr_children, parent->nr_children+1, sizeof(struct mixer*));
		parent->children[parent->nr_children++] = &mixers[i];
	}
	groups_read_volumes();
	// Parents come first (a parent's number is lower), so every stream is
	// played in a mixer that is set up.
	for (int i = 1; i < nr_mixers; i++) {
		mixers[i].stream.userdata = &mixers[i];
		mixers[i].stream.callback = refill_mixer;
		mixers[i].stream.sample.frequency = 44100;
		mixers[i].stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mixers[i].stream.sample.length = CHUNK_SIZE * 2;
		mixers[i].stream.sample.data = mixers[i].data;
		mixers[i].voice = sts_mixer_play_stream(&mixers[i].parent->mixer, &mixers[i].stream, 1.0f);
	}
	asset_sound_info_load();
	groups_enabled = true;
	NOTICE("Sound groups: %d groups from the main EX file", nr_mixers);
}

static void legacy_mixers_init(void)
{
	// initialize mixer naming
	if (!config.mixer_nr_channels) {
		nr_mixers = 3;
		mixers = xcalloc(nr_mixers, sizeof(struct mixer));
		mixers[0].name = strdup("Music");
		mixers[1].name = strdup("Sound");
		mixers[2].name = strdup("Master");
		master = &mixers[2];
	} else {
		// NOTE: Older games don't have an explicit master channel.
		//       We add an extra mixer in this case for consistency.
		int need_master = 1;
		if (!strcmp(config.mixer_channels[0], "Master") || !strcmp(config.mixer_channels[0], SJIS_MASTER)) {
			need_master = 0;
		}
		nr_mixers = config.mixer_nr_channels + need_master;
		mixers = xcalloc(nr_mixers, sizeof(struct mixer));
		for (unsigned i = 0; i < config.mixer_nr_channels; i++) {
			mixers[i].name = strdup(config.mixer_channels[i]);
		}
		if (need_master) {
			mixers[nr_mixers-1].name = strdup("Master");
			master = &mixers[nr_mixers-1];
		} else {
			master = &mixers[0];
		}
	}

	// initialize mixer hierarchy
	// NOTE: "Master" and "Voice" are special nodes; all channels following
	//       them in the list become their children. This appears to be
	//       hardcoded behavior in system 4.
	struct mixer *parent = master;
	for (int i = 0; i < nr_mixers; i++) {
		if (&mixers[i] == master)
			continue;
		mixers[i].parent = parent;
		parent->children = xrealloc_array(parent->children, parent->nr_children, parent->nr_children+1, sizeof(struct mixer*));
		parent->children[parent->nr_children++] = &mixers[i];
		if (!strcmp(mixers[i].name, "Voice") || !strcmp(mixers[i].name, SJIS_VOICE)) {
			parent = &mixers[i];
		}
	}

	// initialize mixers
	for (int i = 0; i < nr_mixers; i++) {
		sts_mixer_init(&mixers[i].mixer, 44100, STS_MIXER_SAMPLE_FORMAT_FLOAT);
		int volume = i < (int)config.mixer_nr_channels ? config.mixer_volumes[i] : config.default_volume;
		mixers[i].mixer.gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	}

	// initialize mixer streams
	for (int i = 0; i < nr_mixers; i++) {
		if (&mixers[i] == master)
			continue;
		mixers[i].stream.userdata = &mixers[i];
		mixers[i].stream.callback = refill_mixer;
		mixers[i].stream.sample.frequency = 44100;
		mixers[i].stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mixers[i].stream.sample.length = CHUNK_SIZE * 2;
		mixers[i].stream.sample.data = mixers[i].data;
		mixers[i].voice = sts_mixer_play_stream(&mixers[i].parent->mixer, &mixers[i].stream, 1.0f);
	}
}

void mixer_init(void)
{
	struct groups_config gc;
	if (groups_read_config(&gc))
		groups_init(&gc);
	else
		legacy_mixers_init();

	// read audio metadata
	if (config.bgi_path)
		bgi_read(config.bgi_path);
	if (config.wai_path)
		wai_load(config.wai_path);

	// Audio modules can start before video. Reuse an existing subsystem
	// without adding a reference; keep the mixer usable if output is unavailable.
	if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)
	    && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
		WARNING("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s", SDL_GetError());
		return;
	}

	SDL_AudioSpec have;
	SDL_AudioSpec want = {
		.format = AUDIO_F32,
		.freq = 44100,
		.channels = 2,
		.samples = CHUNK_SIZE,
		.callback = audio_callback,
	};
	audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!audio_device) {
		WARNING("SDL_OpenAudioDevice failed: %s", SDL_GetError());
		return;
	}
	SDL_PauseAudioDevice(audio_device, 0);
}

int mixer_get_numof(void)
{
	return nr_mixers;
}

const char *mixer_get_name(int n)
{
	if (n < 0 || n >= nr_mixers)
		return NULL;
	return mixers[n].name;
}

int mixer_set_name(int n, const char *name)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	free(mixers[n].name);
	mixers[n].name = strdup(name);
	return 1;
}

int mixer_get_volume(int n, int *volume)
{
	if (n < 0 || n >= nr_mixers) {
		return 0;
	}
	if (groups_enabled) {
		// the integer, so that a volume reads back as it was set
		*volume = mixers[n].volume;
		return 1;
	}
	SDL_LockAudioDevice(audio_device);
	*volume = clamp(0, 100, (int)(mixers[n].mixer.gain * 100));
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_set_volume(int n, int volume)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	SDL_LockAudioDevice(audio_device);
	if (groups_enabled) {
		// 0x49fc90: the current volume clamped to 0..100
		mixers[n].volume = clamp(0, 100, volume);
		mixers[n].mixer.gain = mixers[n].volume / 100.0f;
	} else {
		mixers[n].mixer.gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	}
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

bool mixer_groups_enabled(void)
{
	return groups_enabled;
}

// The fallback group of a sound without a timemap row (channel_set_group).
int mixer_group_fallback(bool streaming)
{
	return groups_enabled ? (streaming ? 1 : 2) : 0;
}

int mixer_get_default_volume(int n, int *volume)
{
	if (!groups_enabled || n < 0 || n >= nr_mixers)
		return 0;
	*volume = mixers[n].default_volume;
	return 1;
}

/*
 * GetGroupVolume with groups: the product of the current volumes from the
 * group up to the root (counted once), 0 when one of them is muted
 * (0x4a5e70), and 0 while the output is silent for the inactive window
 * (0x4a1a7c). A group the table does not have counts 1.0 itself (port
 * strategy; the original takes 1.0 for a node it does not find).
 */
float mixer_group_gain(int n)
{
	if (!groups_enabled)
		return 1.0f;
	if (focus_muted)
		return 0.0f;
	float gain = 1.0f;
	int k = n >= 0 && n < nr_mixers ? n : 0;
	if (k != n)
		gain = mixers[0].muted ? 0.0f : mixers[0].volume / 100.0f;
	else {
		for (int depth = 0; k >= 0 && depth < nr_mixers; depth++) {
			if (mixers[k].muted)
				return 0.0f;
			gain *= mixers[k].volume / 100.0f;
			k = mixers[k].parent ? (int)(mixers[k].parent - mixers) : -1;
		}
	}
	return gain;
}

/*
 * SetGlobalFocus (0x4a1438 keeps it with the groups, +0x30) and the window's
 * activation (+0x31; taken to be the window's, from the writes 0x4bdc14 and
 * 0x4be5e7 with the shape of WM_ACTIVATE): with both off a channel's gain and
 * GetGroupVolume are 0 (0x4a11b0, 0x4a1a7c). Here the whole output is silent
 * then (port strategy); the sounds go on playing and the user's volumes and
 * mutes are not touched.
 */
static void mixer_update_focus(void)
{
	focus_muted = groups_enabled && !global_focus && !window_active;
}

void mixer_set_global_focus(bool on)
{
	global_focus = on;
	mixer_update_focus();
}

void mixer_set_window_focus(bool active)
{
	window_active = active;
	mixer_update_focus();
}
int mixer_get_mute(int n, int *mute)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	*mute = mixers[n].muted;
	return 1;
}

int mixer_set_mute(int n, int mute)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	mixers[n].muted = !!mute;
	return 1;
}

int mixer_stream_play(sts_mixer_stream_t* stream, int volume)
{
	SDL_LockAudioDevice(audio_device);
	float gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	int voice = sts_mixer_play_stream(&master->mixer, stream, gain);
	SDL_UnlockAudioDevice(audio_device);
	return voice;
}

bool mixer_stream_set_volume(int voice, int volume)
{
	if (voice < 0 || voice >= STS_MIXER_VOICES)
		return false;
	SDL_LockAudioDevice(audio_device);
	master->mixer.voices[voice].gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	SDL_UnlockAudioDevice(audio_device);
	return true;
}

void mixer_stream_stop(int voice)
{
	SDL_LockAudioDevice(audio_device);
	sts_mixer_stop_voice(&master->mixer, voice);
	SDL_UnlockAudioDevice(audio_device);
}
