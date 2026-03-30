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

#include <string.h>

#include "system4.h"
#include "system4/string.h"

#include "hll.h"
#include "audio.h"
#include "asset_manager.h"
#include "mixer.h"

/*
 * KiwiSoundEngine — v14 unified sound API.
 *
 * AIN declares 39 functions with a unified ID-based API:
 *   SE functions (0-6): fire-and-forget sound effects
 *   General functions (7-25): prepare/play/stop by channel ID
 *   File query functions (26-28): check files by name
 *   Group/mixer functions (29-38): volume control per mixer group
 *
 * All channels use the wav pool internally.
 */

static void KiwiSoundEngine_ModuleInit(void)
{
	audio_init();
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

static bool KiwiSoundEngine_Prepare(int id, struct string *name, struct string *filter, bool streaming)
{
	(void)filter; (void)streaming;
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
	if (wav_play(id))
		return true;
	return bgm_play(id);
}

static bool KiwiSoundEngine_Stop(int id)
{
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

HLL_LIBRARY(KiwiSoundEngine,
	    HLL_EXPORT(_ModuleInit, KiwiSoundEngine_ModuleInit),
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
