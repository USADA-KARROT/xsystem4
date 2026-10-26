/* Copyright (C) 2021 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/string.h"

#include "vm.h"
#include "vm/heap.h"
#include "hll.h"
#include "audio.h"
#include "asset_manager.h"
#include "mixer.h"
#include "xsystem4.h"

/*
 * KiwiSoundEngine — v14 unified sound API.
 *
 * AIN declares 39 functions with a unified ID-based API:
 *   SE functions (0-6): fire-and-forget sound effects
 *   General functions (7-25): prepare/play/stop by channel ID
 *   File query functions (26-28): check files by name
 *   Group/mixer functions (29-38): volume control per mixer group
 *
 * The general channels use the wav and bgm pools. With the v14 sound groups
 * the SE functions use a table of their own (audio_se_*, see audio.c and
 * the _v14 functions below); without them they share the wav pool with the
 * general channels, as before.
 */

static void KiwiSoundEngine_ModuleInit(void)
{
	audio_init();
}

static bool kiwi_trace(void);

// The sound engine's release writes Volume.sav (0x4a26f0), at the end and on a
// restart (vm_exit and vm_reset run this). The system sound table is emptied
// and its sounds stop: the original rebuilds its sound engine on a restart
// (inferred), and the game's InitSystemSE sets the table again (port
// strategy; its channels are this module's alone). Nothing else is freed: the
// parts and sprite engines' _ModuleFini, which come later in the AIN's order,
// still stop their sounds (audio_reset closes the general channels), and the
// mixers stay for the next run.
static void KiwiSoundEngine_ModuleFini(void)
{
	mixer_groups_save();
	int n = audio_se_reset();
	if (kiwi_trace())
		NOTICE("S2 sound reset se channels=%d", n);
}

static void KiwiSoundEngine_SetGlobalFocus(bool focus)
{
	(void)focus;
}

// --- SE (sound effect) functions ---

static int KiwiSoundEngine_GetFreeSeID(int min_id, int max_id)
{
	(void)min_id; (void)max_id;
	return wav_get_unused_channel();
}

static bool KiwiSoundEngine_IsExistSeID(int se_id)
{
	return wav_is_playing(se_id);
}

static bool KiwiSoundEngine_SetSeParam(int se_id, int play_span, bool multi_play)
{
	(void)se_id; (void)play_span; (void)multi_play;
	return true;
}

static bool KiwiSoundEngine_PlaySe(int se_id, struct string *name, struct string *filter)
{
	(void)filter;
	int no;
	if (asset_exists_by_name(ASSET_SOUND, name->text, &no)) {
		if (!wav_prepare(se_id, no))
			return false;
		return wav_play(se_id);
	}
	if (asset_exists_by_name(ASSET_VOICE, name->text, &no)) {
		if (!wav_prepare_voice(se_id, no))
			return false;
		return wav_play(se_id);
	}
	return false;
}

static int KiwiSoundEngine_Sound_GetGroupNum(int ch)
{
	return wav_get_group_num_from_data_num(ch);
}

static bool KiwiSoundEngine_Sound_PrepareFromFile(int ch, struct string *filename)
{
	return wav_prepare_from_file(ch, (char*)filename->text) > 0;
}

static bool KiwiSoundEngine_StopSe(int se_id)
{
	return wav_stop(se_id);
}

/*
 * Mixer group indices.
 *
 * The default mixer layout (no mixer config in ini) is:
 *   0 = Music (BGM)
 *   1 = Sound (SE/Voice)
 *   2 = Master
 *
 * When the ini specifies mixer channels, the layout follows the ini order
 * with Master either at index 0 or appended at the end. We search by name
 * so it works for both cases.
 */
static int find_mixer_by_name(const char *name)
{
	int n = mixer_get_numof();
	for (int i = 0; i < n; i++) {
		const char *mname = mixer_get_name(i);
		if (mname && !strcmp(mname, name))
			return i;
	}
	return -1;
}

static int KiwiSoundEngine_GetMasterGroup(void)
{
	int idx = find_mixer_by_name("Master");
	return idx >= 0 ? idx : 0;
}

static int KiwiSoundEngine_GetBGMGroup(void)
{
	int idx = find_mixer_by_name("Music");
	return idx >= 0 ? idx : 0;
}

static int KiwiSoundEngine_GetSEGroup(void)
{
	int idx = find_mixer_by_name("Sound");
	return idx >= 0 ? idx : 1;
}

static int KiwiSoundEngine_GetVoiceGroup(void)
{
	int idx = find_mixer_by_name("Voice");
	if (idx >= 0) return idx;
	// Fall back to Sound group if no dedicated Voice mixer
	return KiwiSoundEngine_GetSEGroup();
}

static int KiwiSoundEngine_GetGimicSEGroup(void)
{
	return KiwiSoundEngine_GetSEGroup();
}

static int KiwiSoundEngine_GetBackVoiceGroup(void)
{
	return KiwiSoundEngine_GetVoiceGroup();
}

static bool KiwiSoundEngine_IsPlaySe(int se_id)
{
	return wav_is_playing(se_id);
}

// --- General channel functions ---
// These operate on a unified channel ID that may be in either the wav or bgm pool.
// Prepare() dispatches to the correct pool based on asset type; all other functions
// must check both pools to find the channel.

static int KiwiSoundEngine_GetFreeID(int min_id, int max_id)
{
	(void)min_id; (void)max_id;
	return wav_get_unused_channel();
}

static bool KiwiSoundEngine_IsExistID(int id)
{
	return wav_is_playing(id) || bgm_is_playing(id);
}

// Trace (XSYS4_STAGE2_TRACE): what the script does with its channels, so that
// a run's log shows what a restart or a loaded save left playing.
static bool kiwi_trace(void)
{
	static int enabled = -1;
	if (enabled < 0) {
		const char *v = getenv("XSYS4_STAGE2_TRACE");
		enabled = v && *v && strcmp(v, "0");
	}
	return enabled;
}

static bool KiwiSoundEngine_Prepare(int id, struct string *name, struct string *filter, bool streaming)
{
	(void)filter; (void)streaming;
	if (kiwi_trace())
		NOTICE("S2 sound prepare id=%d name=%s", id, display_game0(name->text));
	int no;
	if (asset_exists_by_name(ASSET_SOUND, name->text, &no))
		return wav_prepare(id, no);
	if (asset_exists_by_name(ASSET_VOICE, name->text, &no))
		return wav_prepare_voice(id, no);
	if (asset_exists_by_name(ASSET_BGM, name->text, &no))
		return bgm_prepare(id, no);
	return false;
}

static bool KiwiSoundEngine_Unprepare(int id)
{
	return wav_unprepare(id) || bgm_unprepare(id);
}

static bool KiwiSoundEngine_Play(int id)
{
	if (kiwi_trace())
		NOTICE("S2 sound play id=%d", id);
	if (wav_play(id))
		return true;
	return bgm_play(id);
}

static bool KiwiSoundEngine_Stop(int id)
{
	if (kiwi_trace())
		NOTICE("S2 sound stop id=%d", id);
	bool r = false;
	if (wav_stop(id)) r = true;
	if (bgm_stop(id)) r = true;
	return r;
}

static bool KiwiSoundEngine_IsPlay(int id)
{
	return wav_is_playing(id) || bgm_is_playing(id);
}

static bool KiwiSoundEngine_SetLoopCount(int id, int count)
{
	if (wav_set_loop_count(id, count))
		return true;
	return bgm_set_loop_count(id, count);
}

static int KiwiSoundEngine_GetLoopCount(int id)
{
	int r = wav_get_loop_count(id);
	if (r > 0) return r;
	return bgm_get_loop_count(id);
}

static bool KiwiSoundEngine_Pause(int id)
{
	if (wav_pause(id))
		return true;
	return bgm_pause(id);
}

static bool KiwiSoundEngine_Restart(int id)
{
	if (wav_restart(id))
		return true;
	return bgm_restart(id);
}

static bool KiwiSoundEngine_IsPause(int id)
{
	return wav_is_paused(id) || bgm_is_paused(id);
}

static bool KiwiSoundEngine_Fade(int id, int time, float volume, bool stop, int fade_type)
{
	(void)fade_type;
	if (kiwi_trace())
		NOTICE("S2 sound fade id=%d time=%d volume=%.2f stop=%d", id, time, volume, stop);
	int vol = (int)(volume * 100.0f);
	if (vol < 0) vol = 0;
	if (vol > 100) vol = 100;
	if (wav_fade(id, time, vol, stop))
		return true;
	return bgm_fade(id, time, vol, stop);
}

static bool KiwiSoundEngine_StopFade(int id)
{
	if (wav_stop_fade(id))
		return true;
	return bgm_stop_fade(id);
}

static bool KiwiSoundEngine_IsFade(int id)
{
	return wav_is_fading(id) || bgm_is_fading(id);
}

static bool KiwiSoundEngine_Seek(int id, int millisec)
{
	if (wav_seek(id, millisec))
		return true;
	return bgm_seek(id, millisec);
}

static int KiwiSoundEngine_GetPos(int id)
{
	int r = wav_get_pos(id);
	if (r >= 0) return r;
	return bgm_get_pos(id);
}

static int KiwiSoundEngine_GetLength(int id)
{
	int r = wav_get_length(id);
	if (r > 0) return r;
	return bgm_get_length(id);
}

static int KiwiSoundEngine_GetGroupNum(int id)
{
	int r = wav_get_group_num(id);
	if (r > 0) return r;
	return 0; // bgm_get_group_num not available
}

// --- File query functions ---

static bool KiwiSoundEngine_IsExistFile(struct string *name)
{
	return asset_exists_by_name(ASSET_SOUND, name->text, NULL)
		|| asset_exists_by_name(ASSET_VOICE, name->text, NULL)
		|| asset_exists_by_name(ASSET_BGM, name->text, NULL);
}

static int KiwiSoundEngine_GetLengthFromFile(struct string *name)
{
	(void)name;
	return 0;
}

static int KiwiSoundEngine_GetGroupNumFromFile(struct string *name)
{
	int no;
	if (asset_exists_by_name(ASSET_SOUND, name->text, &no))
		return wav_get_group_num_from_data_num(no);
	if (asset_exists_by_name(ASSET_VOICE, name->text, &no))
		return wav_get_group_num_from_data_num(no);
	return 0;
}

// --- Group/mixer functions ---

static float KiwiSoundEngine_GetGroupVolume(int group)
{
	int vol;
	if (!mixer_get_volume(group, &vol))
		return 1.0f;
	return (float)vol / 100.0f;
}

static int KiwiSoundEngine_MillisecondsToSamples(int millisec, int samples_per_sec)
{
	return (int)((int64_t)millisec * samples_per_sec / 1000);
}

static int KiwiSoundEngine_GetMixerNumof(void)
{
	return mixer_get_numof();
}

static bool KiwiSoundEngine_GetMixerName(int n, int *name_out)
{
	(void)n; (void)name_out;
	return false;
}

static bool KiwiSoundEngine_GetMixerVolume(int n, int *volume)
{
	if (!volume)
		return false;
	return mixer_get_volume(n, volume);
}

static bool KiwiSoundEngine_GetMixerDefaultVolume(int n, int *volume)
{
	if (!volume)
		return false;
	*volume = 100;
	return true;
}

static bool KiwiSoundEngine_GetMixerMute(int n, int *mute)
{
	if (!mute)
		return false;
	return mixer_get_mute(n, mute);
}

static bool KiwiSoundEngine_SetMixerName(int n, struct string *name)
{
	return mixer_set_name(n, name->text);
}

static bool KiwiSoundEngine_SetMixerVolume(int n, int volume)
{
	return mixer_set_volume(n, volume);
}

static bool KiwiSoundEngine_SetMixerMute(int n, bool mute)
{
	return mixer_set_mute(n, mute);
}

/*
 * The v14 sound groups (audio_mixer.c, mixer_init). These are bound only to the
 * declarations of the v14 shapes (kiwi_select_function); without groups each
 * one does what the function above it does, so a game without the group
 * table keeps its behavior.
 */

/*
 * The system sound (SE) functions with groups: the SE table (audio_se_*), apart
 * from the general channels (the original's two tables of the sound engine,
 * 0x4a2f00; see audio.c). Without groups each does what it did before.
 */
static int KiwiSoundEngine_GetFreeSeID_v14(int min_id, int max_id)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_GetFreeSeID(min_id, max_id);
	return audio_se_get_free_id(min_id, max_id);
}

// 0x4a14c5: GetFreeSeID(SeID, SeID) != SeID. The range is empty, which gives
// 0, so this is true for every ID but 0, with or without an entry: the
// original's arithmetic, kept (the scripts do not call it).
static bool KiwiSoundEngine_IsExistSeID_v14(int se_id)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_IsExistSeID(se_id);
	return audio_se_get_free_id(se_id, se_id) != se_id;
}

static bool KiwiSoundEngine_SetSeParam_v14(int se_id, int play_span, bool multi_play)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_SetSeParam(se_id, play_span, multi_play);
	return audio_se_set_param(se_id, play_span, multi_play);
}

// The filter is not used, as before (how 0x4a3460 -> 0x4a4b20 uses it was
// not traced).
static bool KiwiSoundEngine_PlaySe_v14(int se_id, struct string *name, struct string *filter)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_PlaySe(se_id, name, filter);
	return audio_se_play(se_id, name->text);
}

static bool KiwiSoundEngine_StopSe_v14(int se_id)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_StopSe(se_id);
	return audio_se_stop(se_id);
}

static bool KiwiSoundEngine_IsPlaySe_v14(int se_id)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_IsPlaySe(se_id);
	return audio_se_is_playing(se_id);
}

// 0x4a1438: kept with the groups; see mixer_set_global_focus.
static void KiwiSoundEngine_SetGlobalFocus_v14(bool focus)
{
	if (!mixer_groups_enabled()) {
		KiwiSoundEngine_SetGlobalFocus(focus);
		return;
	}
	mixer_set_global_focus(focus);
}

// Prepare(int ID, string SoundName, string FilterName, bool bStreaming):
// bStreaming picks the group of a file without a timemap row (nonzero 1,
// zero 2; see channel_set_group).
static bool KiwiSoundEngine_Prepare_v14(int id, struct string *name, struct string *filter, bool streaming)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_Prepare(id, name, filter, streaming);
	if (kiwi_trace())
		NOTICE("S2 sound prepare id=%d name=%s", id, display_game0(name->text));
	int no;
	if (asset_exists_by_name(ASSET_SOUND, name->text, &no))
		return wav_prepare_streaming(id, ASSET_SOUND, no, streaming);
	if (asset_exists_by_name(ASSET_VOICE, name->text, &no))
		return wav_prepare_streaming(id, ASSET_VOICE, no, streaming);
	if (asset_exists_by_name(ASSET_BGM, name->text, &no))
		return bgm_prepare_streaming(id, no, streaming);
	return false;
}

// The group of the channel (0x4a1951 -> 0x4a3da0: the group Prepare gave it,
// 0 without a channel). A parts movie's SoundID is a channel there too
// (0x4a3140): its group is the movie's (mixer_movie_sound_set); a script's
// channel of the same ID comes first here (port strategy).
static int KiwiSoundEngine_GetGroupNum_v14(int id)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_GetGroupNum(id);
	int g = audio_get_channel_group(id);
	if (g < 0)
		g = mixer_movie_sound_group(id);
	return g >= 0 ? g : 0;
}

/*
 * GetLengthFromFile(string) with groups: the length in ms, truncated, of the
 * file Prepare would open for the name (0x4a42a0 looks it up with Prepare's
 * 0x4a4050; here Prepare_v14's order, Sound, Voice, then BGM: port strategy),
 * 0 for a name without one and for a file that is neither a WAV nor Ogg
 * Vorbis (mixer_data_length_ms). The scripts' AFL_Sound_GetLength wraps it;
 * the ending waits that long plus a join time after each voice.
 */
static int KiwiSoundEngine_GetLengthFromFile_v14(struct string *name)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_GetLengthFromFile(name);
	if (!name || !name->text[0])
		return 0;
	static const enum asset_type types[] = { ASSET_SOUND, ASSET_VOICE, ASSET_BGM };
	for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		int no;
		if (!asset_exists_by_name(types[i], name->text, &no))
			continue;
		struct archive_data *dfile = asset_get(types[i], no);
		int ms = mixer_data_length_ms(dfile);
		if (dfile)
			archive_free_data(dfile);
		return ms;
	}
	return 0;
}

// The timemap's group of a name; 2 when no timemap has the name (0x4a1a38).
static int KiwiSoundEngine_GetGroupNumFromFile_v14(struct string *name)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_GetGroupNumFromFile(name);
	struct sound_info si;
	if (asset_sound_info_by_name(ASSET_SOUND, name->text, &si) || asset_sound_info_by_name(ASSET_VOICE, name->text, &si))
		return si.group;
	return 2;
}

static float KiwiSoundEngine_GetGroupVolume_v14(int group)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_GetGroupVolume(group);
	return mixer_group_gain(group);
}

// 0x4a6720 checks 0 <= n < the number of groups.
static bool KiwiSoundEngine_GetMixerDefaultVolume_v14(int n, int *volume)
{
	if (!mixer_groups_enabled())
		return KiwiSoundEngine_GetMixerDefaultVolume(n, volume);
	return volume && mixer_get_default_volume(n, volume);
}

/*
 * GetMixerName(int, wrap<string>): ffi passes the wrap's heap slot, not a
 * pointer. The groups have no names of their own (the table has none); one
 * that SetMixerName gave is written back. Without groups this returns false
 * and writes nothing, as before.
 */
static bool KiwiSoundEngine_GetMixerName_v14(int n, int name_slot)
{
	if (!mixer_groups_enabled() || !string_index_valid(name_slot))
		return false;
	const char *name = mixer_get_name(n);
	if (!name)
		return false;
	struct string *s = make_string(name, strlen(name));
	heap_string_assign(name_slot, s);
	free_string(s);
	return true;
}

static bool kiwi_type_is(const struct ain_type *t, enum ain_data_type d)
{
	return t->data == d;
}

static bool kiwi_wrap_of(const struct ain_type *t, enum ain_data_type d)
{
	return t->data == AIN_WRAP && t->array_type && t->array_type->data == d;
}

static bool kiwi_shape(const struct ain_hll_function *f, enum ain_data_type ret, int nr_args)
{
	return f->return_type.data == ret && f->nr_arguments == nr_args && (!nr_args || f->arguments);
}

/*
 * The functions above for the v14 declarations, exactly those shapes; every
 * other declaration keeps the library table's function.
 */
void *kiwi_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (!f || !f->name)
		return dflt;
	const struct ain_hll_argument *a = f->arguments;
	if (!strcmp(f->name, "SetGlobalFocus") && kiwi_shape(f, AIN_VOID, 1) && kiwi_type_is(&a[0].type, AIN_BOOL))
		return (void *)KiwiSoundEngine_SetGlobalFocus_v14;
	if (!strcmp(f->name, "GetFreeSeID") && kiwi_shape(f, AIN_INT, 2) && kiwi_type_is(&a[0].type, AIN_INT)
	    && kiwi_type_is(&a[1].type, AIN_INT))
		return (void *)KiwiSoundEngine_GetFreeSeID_v14;
	if (!strcmp(f->name, "IsExistSeID") && kiwi_shape(f, AIN_BOOL, 1) && kiwi_type_is(&a[0].type, AIN_INT))
		return (void *)KiwiSoundEngine_IsExistSeID_v14;
	if (!strcmp(f->name, "SetSeParam") && kiwi_shape(f, AIN_BOOL, 3) && kiwi_type_is(&a[0].type, AIN_INT)
	    && kiwi_type_is(&a[1].type, AIN_INT) && kiwi_type_is(&a[2].type, AIN_BOOL))
		return (void *)KiwiSoundEngine_SetSeParam_v14;
	if (!strcmp(f->name, "PlaySe") && kiwi_shape(f, AIN_BOOL, 3) && kiwi_type_is(&a[0].type, AIN_INT)
	    && kiwi_type_is(&a[1].type, AIN_STRING) && kiwi_type_is(&a[2].type, AIN_STRING))
		return (void *)KiwiSoundEngine_PlaySe_v14;
	if (!strcmp(f->name, "StopSe") && kiwi_shape(f, AIN_BOOL, 1) && kiwi_type_is(&a[0].type, AIN_INT))
		return (void *)KiwiSoundEngine_StopSe_v14;
	if (!strcmp(f->name, "IsPlaySe") && kiwi_shape(f, AIN_BOOL, 1) && kiwi_type_is(&a[0].type, AIN_INT))
		return (void *)KiwiSoundEngine_IsPlaySe_v14;
	if (!strcmp(f->name, "Prepare") && kiwi_shape(f, AIN_BOOL, 4) && kiwi_type_is(&a[0].type, AIN_INT)
	    && kiwi_type_is(&a[1].type, AIN_STRING) && kiwi_type_is(&a[2].type, AIN_STRING) && kiwi_type_is(&a[3].type, AIN_BOOL))
		return (void *)KiwiSoundEngine_Prepare_v14;
	if (!strcmp(f->name, "GetGroupNum") && kiwi_shape(f, AIN_INT, 1) && kiwi_type_is(&a[0].type, AIN_INT))
		return (void *)KiwiSoundEngine_GetGroupNum_v14;
	if (!strcmp(f->name, "GetGroupNumFromFile") && kiwi_shape(f, AIN_INT, 1) && kiwi_type_is(&a[0].type, AIN_STRING))
		return (void *)KiwiSoundEngine_GetGroupNumFromFile_v14;
	if (!strcmp(f->name, "GetLengthFromFile") && kiwi_shape(f, AIN_INT, 1) && kiwi_type_is(&a[0].type, AIN_STRING))
		return (void *)KiwiSoundEngine_GetLengthFromFile_v14;
	if (!strcmp(f->name, "GetGroupVolume") && kiwi_shape(f, AIN_FLOAT, 1) && kiwi_type_is(&a[0].type, AIN_INT))
		return (void *)KiwiSoundEngine_GetGroupVolume_v14;
	if (!strcmp(f->name, "GetMixerDefaultVolume") && kiwi_shape(f, AIN_BOOL, 2) && kiwi_type_is(&a[0].type, AIN_INT)
	    && kiwi_wrap_of(&a[1].type, AIN_INT))
		return (void *)KiwiSoundEngine_GetMixerDefaultVolume_v14;
	if (!strcmp(f->name, "GetMixerName") && kiwi_shape(f, AIN_BOOL, 2) && kiwi_type_is(&a[0].type, AIN_INT)
	    && kiwi_wrap_of(&a[1].type, AIN_STRING))
		return (void *)KiwiSoundEngine_GetMixerName_v14;
	return dflt;
}

// Rance 9 declares Music/Sound functions that take a filename-number
// rather than a data number (upstream behavior, keyed off Sound_Seek).
static bool rance9_bgm_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_BGM, name, NULL);
}

static int rance9_bgm_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_BGM, name, NULL);
	if (!dfile)
		return 0;
	return bgm_prepare_from_archive_data(ch, dfile);
}

static bool rance9_wav_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_SOUND, name, NULL);
}

static int rance9_wav_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_SOUND, name, NULL);
	if (!dfile)
		return 0;
	return wav_prepare_from_archive_data(ch, dfile);
}

static void KiwiSoundEngine_PreLink(void);

HLL_LIBRARY(KiwiSoundEngine,
	    HLL_EXPORT(_ModuleInit, KiwiSoundEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, KiwiSoundEngine_ModuleFini),
	    HLL_EXPORT(_PreLink, KiwiSoundEngine_PreLink),
	    HLL_EXPORT(SetGlobalFocus, KiwiSoundEngine_SetGlobalFocus),
	    HLL_EXPORT(Music_IsExist, bgm_exists),
	    HLL_EXPORT(Music_Prepare, bgm_prepare),
	    HLL_EXPORT(Music_Unprepare, bgm_unprepare),
	    HLL_EXPORT(Music_Play, bgm_play),
	    HLL_EXPORT(Music_Stop, bgm_stop),
	    HLL_EXPORT(Music_IsPlay, bgm_is_playing),
	    HLL_EXPORT(Music_SetLoopCount, bgm_set_loop_count),
	    HLL_EXPORT(Music_GetLoopCount, bgm_get_loop_count),
	    HLL_EXPORT(Music_SetLoopStartPos, bgm_set_loop_start_pos),
	    HLL_EXPORT(Music_SetLoopEndPos, bgm_set_loop_end_pos),
	    HLL_EXPORT(Music_Fade, bgm_fade),
	    HLL_EXPORT(Music_StopFade, bgm_stop_fade),
	    HLL_EXPORT(Music_IsFade, bgm_is_fading),
	    HLL_EXPORT(Music_Pause, bgm_pause),
	    HLL_EXPORT(Music_Restart, bgm_restart),
	    HLL_EXPORT(Music_IsPause, bgm_is_paused),
	    HLL_EXPORT(Music_GetPos, bgm_get_pos),
	    HLL_EXPORT(Music_GetLength, bgm_get_length),
	    HLL_EXPORT(Music_GetSamplePos, bgm_get_sample_pos),
	    HLL_EXPORT(Music_GetSampleLength, bgm_get_sample_length),
	    HLL_EXPORT(Music_Seek, bgm_seek),
	    //HLL_EXPORT(Music_MillisecondsToSamples, KiwiSoundEngine_Music_MillisecondsToSamples),
	    //HLL_EXPORT(Music_GetFormat, KiwiSoundEngine_Music_GetFormat),
	    HLL_EXPORT(Sound_IsExist, wav_exists),
	    HLL_EXPORT(Sound_Prepare, wav_prepare),
	    HLL_EXPORT(Sound_Unprepare, wav_unprepare),
	    HLL_EXPORT(Sound_Play, wav_play),
	    HLL_EXPORT(Sound_Stop, wav_stop),
	    HLL_EXPORT(Sound_IsPlay, wav_is_playing),
	    HLL_EXPORT(Sound_Fade, wav_fade),
	    HLL_EXPORT(Sound_StopFade, wav_stop_fade),
	    HLL_EXPORT(Sound_IsFade, wav_is_fading),
	    HLL_EXPORT(Sound_GetTimeLength, wav_get_time_length),
	    HLL_EXPORT(Sound_GetGroupNum, KiwiSoundEngine_Sound_GetGroupNum),
	    HLL_EXPORT(Sound_GetGroupNumFromDataNum, wav_get_group_num_from_data_num),
	    HLL_EXPORT(Sound_PrepareFromFile, KiwiSoundEngine_Sound_PrepareFromFile),
	    HLL_EXPORT(Sound_GetDataLength, wav_get_data_length),
	    HLL_EXPORT(GetFreeSeID, KiwiSoundEngine_GetFreeSeID),
	    HLL_EXPORT(IsExistSeID, KiwiSoundEngine_IsExistSeID),
	    HLL_EXPORT(SetSeParam, KiwiSoundEngine_SetSeParam),
	    HLL_EXPORT(PlaySe, KiwiSoundEngine_PlaySe),
	    HLL_EXPORT(StopSe, KiwiSoundEngine_StopSe),
	    HLL_EXPORT(IsPlaySe, KiwiSoundEngine_IsPlaySe),
	    HLL_EXPORT(GetFreeID, KiwiSoundEngine_GetFreeID),
	    HLL_EXPORT(IsExistID, KiwiSoundEngine_IsExistID),
	    HLL_EXPORT(Prepare, KiwiSoundEngine_Prepare),
	    HLL_EXPORT(Unprepare, KiwiSoundEngine_Unprepare),
	    HLL_EXPORT(Play, KiwiSoundEngine_Play),
	    HLL_EXPORT(GetMasterGroup, KiwiSoundEngine_GetMasterGroup),
	    HLL_EXPORT(GetBGMGroup, KiwiSoundEngine_GetBGMGroup),
	    HLL_EXPORT(GetSEGroup, KiwiSoundEngine_GetSEGroup),
	    HLL_EXPORT(GetVoiceGroup, KiwiSoundEngine_GetVoiceGroup),
	    HLL_EXPORT(GetGimicSEGroup, KiwiSoundEngine_GetGimicSEGroup),
	    HLL_EXPORT(GetBackVoiceGroup, KiwiSoundEngine_GetBackVoiceGroup),
	    HLL_EXPORT(GetPos, KiwiSoundEngine_GetPos),
	    HLL_EXPORT(IsExistFile, KiwiSoundEngine_IsExistFile),
	    HLL_EXPORT(Fade, KiwiSoundEngine_Fade),
	    HLL_EXPORT(Stop, KiwiSoundEngine_Stop),
	    HLL_EXPORT(IsPlay, KiwiSoundEngine_IsPlay),
	    HLL_EXPORT(SetLoopCount, KiwiSoundEngine_SetLoopCount),
	    HLL_EXPORT(GetLoopCount, KiwiSoundEngine_GetLoopCount),
	    HLL_EXPORT(Pause, KiwiSoundEngine_Pause),
	    HLL_EXPORT(Restart, KiwiSoundEngine_Restart),
	    HLL_EXPORT(IsPause, KiwiSoundEngine_IsPause),
	    HLL_EXPORT(Fade, KiwiSoundEngine_Fade),
	    HLL_EXPORT(StopFade, KiwiSoundEngine_StopFade),
	    HLL_EXPORT(IsFade, KiwiSoundEngine_IsFade),
	    HLL_EXPORT(Seek, KiwiSoundEngine_Seek),
	    HLL_EXPORT(GetPos, KiwiSoundEngine_GetPos),
	    HLL_EXPORT(GetLength, KiwiSoundEngine_GetLength),
	    HLL_EXPORT(GetGroupNum, KiwiSoundEngine_GetGroupNum),
	    HLL_EXPORT(IsExistFile, KiwiSoundEngine_IsExistFile),
	    HLL_EXPORT(GetLengthFromFile, KiwiSoundEngine_GetLengthFromFile),
	    HLL_EXPORT(GetGroupNumFromFile, KiwiSoundEngine_GetGroupNumFromFile),
	    HLL_EXPORT(GetGroupVolume, KiwiSoundEngine_GetGroupVolume),
	    HLL_EXPORT(MillisecondsToSamples, KiwiSoundEngine_MillisecondsToSamples),
	    HLL_EXPORT(GetMixerNumof, KiwiSoundEngine_GetMixerNumof),
	    HLL_EXPORT(GetMixerName, KiwiSoundEngine_GetMixerName),
	    HLL_EXPORT(GetMixerVolume, KiwiSoundEngine_GetMixerVolume),
	    HLL_EXPORT(GetMixerDefaultVolume, KiwiSoundEngine_GetMixerDefaultVolume),
	    HLL_EXPORT(GetMixerMute, KiwiSoundEngine_GetMixerMute),
	    HLL_EXPORT(SetMixerName, KiwiSoundEngine_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, KiwiSoundEngine_SetMixerVolume),
	    HLL_EXPORT(SetMixerMute, KiwiSoundEngine_SetMixerMute)
	);

static void KiwiSoundEngine_PreLink(void)
{
	int libno = ain_get_library(ain, "KiwiSoundEngine");
	if (libno < 0)
		return;
	if (ain_get_library_function(ain, libno, "Sound_Seek") >= 0) {
		static_library_replace(&lib_KiwiSoundEngine, "Music_IsExist", rance9_bgm_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Music_Prepare", rance9_bgm_prepare);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_IsExist", rance9_wav_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_Prepare", rance9_wav_prepare);
	}
}
