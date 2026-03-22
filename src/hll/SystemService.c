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

#include <string.h>
#include <assert.h>
#include <time.h>

#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "cJSON.h"
#include "audio.h"
#include "input.h"
#include "gfx/gfx.h"
#include "mixer.h"
#include "savedata.h"
#include "scene.h"
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "hll.h"
#include "system4/file.h"

static int SystemService_GetMixerName(int n, struct string **name)
{
	const char *r = mixer_get_name(n);
	if (!r)
		return 0;
	*name = make_string(r, strlen(r));
	return 1;
}

static bool SystemService_GetMixerDefaultVolume(int n, int *volume)
{
	if (n < 0 || (unsigned)n >= config.mixer_nr_channels)
		return false;
	*volume = config.mixer_volumes[n];
	return true;
}

static bool SystemService_SetMixerName(int n, struct string *name)
{
	return mixer_set_name(n, name->text);
}

static int SystemService_GetGameVersion(void)
{
	return ain->game_version;
}

static void SystemService_GetGameName(struct string **game_name)
{
	if (*game_name)
		free_string(*game_name);
	*game_name = cstr_to_string(config.game_name);
}

static bool SystemService_AddURLMenu(struct string *title, struct string *url)
{
	/* URL menu not applicable on macOS — silently ignore */
	return true;
}

static bool SystemService_IsFullScreen(void)
{
	return gfx_is_fullscreen();
}

static bool SystemService_ChangeNormalScreen(void)
{
	return gfx_set_fullscreen(false);
}

static bool SystemService_ChangeFullScreen(void)
{
	return gfx_set_fullscreen(true);
}

static bool SystemService_InitMainWindowPosAndSize(void)
{
	/* window position managed by SDL/window manager */
	return true;
}

// From parts — avoid full include of parts_internal.h
extern void parts_render_update(int passed_time);
extern void PE_UpdateInputState(int passed_time);
extern void PE_UpdateComponent(int passed_time);
extern void parts_update_animation(int passed_time);

static bool SystemService_UpdateView(void)
{
	// Compute elapsed time for animation/motion updates
	static struct timespec last_time = {0};
	int passed_time = 0;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (last_time.tv_sec > 0) {
		double delta_ms = (now.tv_sec - last_time.tv_sec) * 1000.0
			+ (now.tv_nsec - last_time.tv_nsec) / 1e6;
		passed_time = (int)delta_ms;
		if (passed_time < 0) passed_time = 0;
		if (passed_time > 100) passed_time = 100;
	}
	last_time = now;

	handle_events();
	audio_update();
	sprite_call_plugins();
	PE_UpdateComponent(passed_time);
	parts_update_animation(passed_time);
	PE_UpdateInputState(passed_time);
	parts_render_update(passed_time);

	// Throttle scene_render + gfx_swap to ~60fps.
	static uint32_t sv_last_render_ms = 0;
	uint32_t now_ms = SDL_GetTicks();
	if (now_ms - sv_last_render_ms >= 16) {
		scene_render();
		gfx_swap();
		sv_last_render_ms = now_ms;
	} else {
		SDL_Delay(1); // yield to OS when render is skipped
	}
	return true;
}

static int SystemService_GetViewWidth(void)
{
	return config.view_width;
}

static int SystemService_GetViewHeight(void)
{
	return config.view_height;
}

static bool SystemService_MoveMouseCursorPosImmediately(int x, int y)
{
	mouse_set_pos(x, y);
	return true;
}

static bool SystemService_SetHideMouseCursorByGame(bool hide)
{
	return mouse_show_cursor(!hide);
}

//bool SystemService_GetHideMouseCursorByGame(void);
static bool SystemService_SetUsePower2Texture(bool use)
{
	/* modern GPUs support NPOT textures — no-op */
	return true;
}
//bool SystemService_GetUsePower2Texture(void);

enum window_settings_asect_ratio {
	ASPECT_RATIO_NORMAL,
	ASPECT_RATIO_FIXED
};

enum window_settings_scaling_type {
	SCALING_NORMAL,
	SCALING_BICUBIC,
};

struct window_settings {
	enum window_settings_asect_ratio aspect_ratio;
	enum window_settings_scaling_type scaling_type;
	bool wait_vsync;
	bool record_pos_size;
	bool minimize_by_full_screen_inactive;
	bool back_to_title_confirm;
	bool close_game_confirm;
};

static struct window_settings window_settings = {
	.aspect_ratio = ASPECT_RATIO_NORMAL,
	.scaling_type = SCALING_NORMAL,
	.wait_vsync = false,
	.record_pos_size = false,
	.minimize_by_full_screen_inactive = true,
	.back_to_title_confirm = true,
	.close_game_confirm = true,
};

enum window_settings_id {
	WINDOW_SETTINGS_ASPECT_RATIO = 0,
	WINDOW_SETTINGS_SCALING_TYPE = 1,
	WINDOW_SETTINGS_WAIT_VSYNC = 2,
	WINDOW_SETTINGS_RECORD_POS_SIZE = 3,
	WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE = 4,
	WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM = 5,
	WINDOW_SETTINGS_CLOSE_GAME_CONFIRM = 6,
};

static void save_window_settings(void)
{
	cJSON *root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "wait_vsync", window_settings.wait_vsync);
	save_json("WindowSetting.json", root);
	cJSON_Delete(root);
}

static void load_window_settings(void)
{
	cJSON *root = load_json("WindowSetting.json");
	if (!root)
		return;
	cJSON *v;
	if ((v = cJSON_GetObjectItem(root, "wait_vsync"))) {
		window_settings.wait_vsync = cJSON_IsTrue(v);
		gfx_set_wait_vsync(window_settings.wait_vsync);
	}
	cJSON_Delete(root);
}

static bool SystemService_SetWindowSetting(int type, int value)
{
	switch (type) {
	case WINDOW_SETTINGS_ASPECT_RATIO:
		window_settings.aspect_ratio = value;
		break;
	case WINDOW_SETTINGS_SCALING_TYPE:
		window_settings.scaling_type = value;
		break;
	case WINDOW_SETTINGS_WAIT_VSYNC:
		window_settings.wait_vsync = value;
		gfx_set_wait_vsync(value);
		break;
	case WINDOW_SETTINGS_RECORD_POS_SIZE:
		window_settings.record_pos_size = value;
		break;
	case WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE:
		window_settings.minimize_by_full_screen_inactive = value;
		break;
	case WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM:
		window_settings.back_to_title_confirm = value;
		break;
	case WINDOW_SETTINGS_CLOSE_GAME_CONFIRM:
		window_settings.close_game_confirm = value;
		break;
	default:
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	save_window_settings();
	return true;
}

// v14: AIN declares arg[1] as AIN_WRAP — pointer-based interface
static bool SystemService_GetWindowSetting(int type, int *value)
{
	int v;
	switch (type) {
	case WINDOW_SETTINGS_ASPECT_RATIO:
		v = window_settings.aspect_ratio; break;
	case WINDOW_SETTINGS_SCALING_TYPE:
		v = window_settings.scaling_type; break;
	case WINDOW_SETTINGS_WAIT_VSYNC:
		v = window_settings.wait_vsync; break;
	case WINDOW_SETTINGS_RECORD_POS_SIZE:
		v = window_settings.record_pos_size; break;
	case WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE:
		v = window_settings.minimize_by_full_screen_inactive; break;
	case WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM:
		v = window_settings.back_to_title_confirm; break;
	case WINDOW_SETTINGS_CLOSE_GAME_CONFIRM:
		v = window_settings.close_game_confirm; break;
	default:
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	if (value) *value = v;
	return true;
}

// XXX: Values for 'type' above 1 are invalid in Haru Urare, may be different in other games
#define NR_MOUSE_CURSOR_CONFIG 1
static int mouse_cursor_config[NR_MOUSE_CURSOR_CONFIG] = {0};

static bool SystemService_SetMouseCursorConfig(int type, int value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	mouse_cursor_config[type] = value;
	return true;
}

// v14: AIN declares arg[1] as AIN_WRAP — pointer-based interface
static bool SystemService_GetMouseCursorConfig(int type, int *value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	if (value) *value = !!mouse_cursor_config[type];
	return true;
}

//bool SystemService_RunProgram(struct string *program_file_name, struct string *parameter);
//bool SystemService_IsOpenedMutex(struct string *mutex_name);

void SystemService_GetGameFolderPath(struct string **folder_path)
{
	if (!folder_path)
		return;
	if (!config.game_dir) {
		*folder_path = cstr_to_string(".");
		return;
	}
	// Return UTF-8 path directly — macOS filesystem uses UTF-8.
	// Do NOT convert to SJIS: CJK chars in path may not exist in SJIS.
	*folder_path = cstr_to_string(config.game_dir);
}

static void SystemService_GetTime(int *hour_out, int *min_out, int *sec_out)
{
	int hour, min, sec, ms;
	get_time(&hour, &min, &sec, &ms);
	if (hour_out) *hour_out = hour;
	if (min_out) *min_out = min;
	if (sec_out) *sec_out = sec;
}

static void SystemService_GetDate(int *year_out, int *month_out, int *mday_out, int *wday_out)
{
	int year, month, mday, wday;
	get_date(&year, &month, &mday, &wday);
	if (year_out) *year_out = year;
	if (month_out) *month_out = month;
	if (mday_out) *mday_out = mday;
	if (wday_out) *wday_out = wday;
}

static bool SystemService_IsResetOnce(void)
{
	return vm_reset_once;
}

static bool SystemService_IsResetOnce_Drapeko(struct string **text)
{
	*text = cstr_to_string("XXX TTT YYY"); // ???
	return vm_reset_once;
}

static char *get_manual_filename(void) {
	char *manual_path = path_join("Manual", "index.html");
	char *file_path = path_join(config.game_dir, manual_path);
	free(manual_path);
	return file_path;
}

static bool SystemService_IsExistPlayingManual(void) {
	char *filename = get_manual_filename();
	const bool exists = file_exists(filename);
	free(filename);
	return exists;
}

#ifndef _WIN32
static char *percent_encode(const char *str) {
	const char *hex = "0123456789ABCDEF";
	// Worst case all characters are percent-encoded
	char *encoded = xmalloc(strlen(str) * 3 + 1);

	char *p = encoded;
	while (*str) {
		const unsigned char c = *str++;
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
			*p++ = (char)c;
		} else {
			*p++ = '%';
			*p++ = hex[c >> 4];
			*p++ = hex[c & 15];
		}
	}

	*p = '\0';
	return encoded;
}
#endif

static void SystemService_OpenPlayingManual(void) {
	if (!SystemService_IsExistPlayingManual()) {
		return;
	}
#ifdef __ANDROID__
	const int COMMAND_OPEN_PLAYING_MANUAL = 0x8000;
	SDL_AndroidSendMessage(COMMAND_OPEN_PLAYING_MANUAL, 0);
#else
	char *filename = get_manual_filename();

	char *real_path = realpath_utf8(filename);
	free(filename);
	if (!real_path) {
		return;
	}

#ifdef _WIN32
	const char *prefix = "file:///";
	char *path_component = real_path;
#else
	const char *prefix = "file://";
	char *path_component = percent_encode(real_path);
	free(real_path);
#endif
	char *url = xmalloc(strlen(prefix) + strlen(path_component) + 1);
	strcpy(url, prefix);
	strcat(url, path_component);

	if (SDL_OpenURL(url) < 0) {
		WARNING("Failed to open manual at '%s': %s", url, SDL_GetError());
	}

	free(url);
	free(path_component);
#endif
}

//static bool SystemService_IsExistSystemMessage(void);
HLL_QUIET_UNIMPLEMENTED(false, bool, SystemService, IsExistSystemMessage);
//static bool SystemService_PopSystemMessage(int *message);

static void SystemService_RestrainScreensaver(void) { }

static int SystemService_Debug_GetUseVideoMemorySize(void) { return 0; }

static float SystemService_GetGameViewScaleRate(void) { return 1.0f; }
static int SystemService_Debug_GetUseMemorySize(void) { return 0; }

// Rance 01
static void SystemService_Rance0123456789(struct string **text)
{
	*text = cstr_to_string("-RANCE010ECNAR-");
}

// Rance 01 trial edition
static void SystemService_XXXXX01XXXXXXXX(struct string **text)
{
	*text = cstr_to_string("RANCE01RANCEKAKKOII");
}

// Drapeko
static void SystemService_Test(struct string **text)
{
	*text = cstr_to_string("DELETE ALL 758490275489207548093");
}

// Drapeko trial edition
static void SystemService_DRPKT(struct string **text)
{
	*text = cstr_to_string("DRPKT QWERTY NUFUAUEO 75849027582754829");
}

static struct string *SystemService_GetGameVersionByText(void)
{
	// JAST English edition version string (extracted from dohnadohna.exe).
	// Game reads chars at index 2 and 24 to determine edition/language.
	return cstr_to_string("sd4gv86sdf834fs68f63qvna4");
}

// Rance 9
static void SystemService_Rance96161988(struct string **text) {
	*text = cstr_to_string("=Rance99/RANCE99=");
}

// Pascha3 Plus Contents
static void SystemService_XXX(struct string **text) {
	*text = cstr_to_string("FORMAT HDD ERASE 578205024758284076520478254092784789752384758204687293");
}

static void SystemService_PreLink(void);

static void SystemService_ModuleInit(void)
{
	load_window_settings();
}

static void SystemService_AddBackupSaveFileName(struct string *name)
{
	// stub — backup save file name tracking not needed
}

static void SystemService_ShowWaitMessage(bool show)
{
	// stub — wait message overlay not implemented
}

/* GameVariable: simple key-value string store (persisted per session) */
#define GAMEVAR_MAX 128
static struct { char *key; char *value; } game_vars[GAMEVAR_MAX];
static int game_var_count = 0;

static int gamevar_find(const char *key)
{
	for (int i = 0; i < game_var_count; i++) {
		if (game_vars[i].key && !strcmp(game_vars[i].key, key))
			return i;
	}
	return -1;
}

static bool SystemService_GameVariable_IsExist(struct string *key)
{
	return gamevar_find(key->text) >= 0;
}

static void SystemService_GameVariable_Set(struct string *key, struct string *value)
{
	int idx = gamevar_find(key->text);
	if (idx >= 0) {
		free(game_vars[idx].value);
		game_vars[idx].value = strdup(value->text);
		return;
	}
	if (game_var_count >= GAMEVAR_MAX) return;
	game_vars[game_var_count].key = strdup(key->text);
	game_vars[game_var_count].value = strdup(value->text);
	game_var_count++;
}

static struct string *SystemService_GameVariable_Get(struct string *key)
{
	int idx = gamevar_find(key->text);
	if (idx >= 0)
		return cstr_to_string(game_vars[idx].value);
	return string_ref(&EMPTY_STRING);
}

static int SystemService_GameVariable_NumofKey(void)
{
	return game_var_count;
}

static struct string *SystemService_GameVariable_GetKey(int index)
{
	if (index >= 0 && index < game_var_count && game_vars[index].key)
		return cstr_to_string(game_vars[index].key);
	return string_ref(&EMPTY_STRING);
}

static void SystemService_GameVariable_Erase(struct string *key)
{
	int idx = gamevar_find(key->text);
	if (idx < 0) return;
	free(game_vars[idx].key);
	free(game_vars[idx].value);
	if (idx < game_var_count - 1)
		game_vars[idx] = game_vars[game_var_count - 1];
	game_var_count--;
}

// v14 save/load stubs — pretend to succeed so game logic continues
static bool SystemService_Save(struct string *filename)
{
	return true;
}

static bool SystemService_Load(struct string *filename)
{
	return true;
}

HLL_QUIET_UNIMPLEMENTED( , void, SystemService, SetAntiAliasingMode, int mode);
HLL_QUIET_UNIMPLEMENTED( , void, SystemService, SetHookCloseApp, bool hook);
HLL_QUIET_UNIMPLEMENTED( , void, SystemService, SetAndroidViewKeepScreen, bool keep);

static bool SystemService_GetHideMouseCursorByGame(void) { return false; }
static bool SystemService_GetUsePower2Texture(void) { return false; }
static bool SystemService_RunProgram(struct string *prog) { return false; }
static bool SystemService_IsOpenedMutex(struct string *name) { return false; }
static bool SystemService_PopSystemMessage(void) { return false; }
static int SystemService_GetDefaultViewWidth(void) { return config.view_width; }
static int SystemService_GetDefaultViewHeight(void) { return config.view_height; }
static int SystemService_GetWindowPosX(void) { return 0; }
static int SystemService_GetWindowPosY(void) { return 0; }
static void SystemService_SetWindowPos(int x, int y) { }
static void SystemService_SetViewSize(int w, int h) { }
static void SystemService_SetGameViewScaleRate2(float rate) { }
static int SystemService_GetPlatformType(void) { return 0; /* 0=Windows */ }
static int SystemService_GetCountryNo(void) { return 1; /* 1=Japan */ }
static int SystemService_GetAntiAliasingMode(void) { return 0; }
static bool SystemService_IsViewResizableMode(void) { return false; }
static void SystemService_SetViewResizableMode(int mode) { }
static bool SystemService_IsResizableUserControl(void) { return false; }
static void SystemService_SetResizableUserControl(int ctrl) { }
static bool SystemService_IsHookCloseApp(void) { return false; }
static void SystemService_BackupSaveFile(void) { }
static bool SystemService_EnableSystemPopup(void) { return true; }
static void SystemService_PopSystemMessageString(struct string *msg) { }
static void SystemService_RenderToSurfaceImage(int surface) { }
static void SystemService_RenderToSurfaceImageByScale(int surface, float scale) { }
static bool SystemService_SaveScreenshot(struct string *path, int x, int y, int w, int h) { return false; }

/* SystemVariable — per-session key-value store (not persisted) */
struct sys_var { char *key; char *val; };
static struct sys_var *sys_vars = NULL;
static int nr_sys_vars = 0;

static bool SystemService_SystemVariable_IsExist(struct string *key)
{
	for (int i = 0; i < nr_sys_vars; i++)
		if (!strcmp(sys_vars[i].key, key->text)) return true;
	return false;
}

static void SystemService_SystemVariable_Set(struct string *key, struct string *val)
{
	for (int i = 0; i < nr_sys_vars; i++) {
		if (!strcmp(sys_vars[i].key, key->text)) {
			free(sys_vars[i].val);
			sys_vars[i].val = strdup(val->text);
			return;
		}
	}
	sys_vars = xrealloc_array(sys_vars, nr_sys_vars, nr_sys_vars + 1, sizeof(struct sys_var));
	sys_vars[nr_sys_vars].key = strdup(key->text);
	sys_vars[nr_sys_vars].val = strdup(val->text);
	nr_sys_vars++;
}

static struct string *SystemService_SystemVariable_Get(struct string *key)
{
	for (int i = 0; i < nr_sys_vars; i++)
		if (!strcmp(sys_vars[i].key, key->text))
			return cstr_to_string(sys_vars[i].val);
	return string_ref(&EMPTY_STRING);
}

static int SystemService_SystemVariable_NumofKey(void) { return nr_sys_vars; }

static struct string *SystemService_SystemVariable_GetKey(int index)
{
	if (index < 0 || index >= nr_sys_vars)
		return string_ref(&EMPTY_STRING);
	return cstr_to_string(sys_vars[index].key);
}

static void SystemService_SystemVariable_Erase(struct string *key)
{
	for (int i = 0; i < nr_sys_vars; i++) {
		if (!strcmp(sys_vars[i].key, key->text)) {
			free(sys_vars[i].key);
			free(sys_vars[i].val);
			for (int j = i + 1; j < nr_sys_vars; j++)
				sys_vars[j-1] = sys_vars[j];
			nr_sys_vars--;
			return;
		}
	}
}

/* Debug functions */
static bool SystemService_Debug_IsShowMipmapLevel(void) { return false; }
static void SystemService_Debug_SetShowMipmapLevel(int level) { }

/* Stubs for functions not applicable to desktop */
static void SystemService_SetAndroidViewOrientation(int orient) { }

/* CPU/Memory/OS info stubs */
static bool SystemService_GetCPUInfo(int *vendor, int *family, int *model, int *stepping) {
	if (vendor) *vendor = 0;
	if (family) *family = 0;
	if (model) *model = 0;
	if (stepping) *stepping = 0;
	return true;
}
static bool SystemService_GetMemoryInfo(int *total_mb, int *avail_mb) {
	if (total_mb) *total_mb = 8192;
	if (avail_mb) *avail_mb = 4096;
	return true;
}
static struct string *SystemService_GetOSInfo(void) { return cstr_to_string("macOS"); }
static struct string *SystemService_GetScreenInfo(void) {
	char buf[64];
	snprintf(buf, sizeof(buf), "%dx%d", config.view_width, config.view_height);
	return cstr_to_string(buf);
}

HLL_LIBRARY(SystemService,
	    HLL_EXPORT(_PreLink, SystemService_PreLink),
	    HLL_EXPORT(_ModuleInit, SystemService_ModuleInit),
	    HLL_EXPORT(GetMixerNumof, mixer_get_numof),
	    HLL_EXPORT(GetMixerName, SystemService_GetMixerName),
	    HLL_EXPORT(GetMixerVolume, mixer_get_volume),
	    HLL_EXPORT(GetMixerDefaultVolume, SystemService_GetMixerDefaultVolume),
	    HLL_EXPORT(GetMixerMute, mixer_get_mute),
	    HLL_EXPORT(SetMixerName, SystemService_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, mixer_set_volume),
	    HLL_EXPORT(SetMixerMute, mixer_set_mute),
	    HLL_EXPORT(GetGameVersion, SystemService_GetGameVersion),
	    HLL_EXPORT(GetGameName, SystemService_GetGameName),
	    HLL_EXPORT(AddURLMenu, SystemService_AddURLMenu),
	    HLL_EXPORT(IsFullScreen, SystemService_IsFullScreen),
	    HLL_EXPORT(ChangeNormalScreen, SystemService_ChangeNormalScreen),
	    HLL_EXPORT(ChangeFullScreen, SystemService_ChangeFullScreen),
	    HLL_EXPORT(InitMainWindowPosAndSize, SystemService_InitMainWindowPosAndSize),
	    HLL_EXPORT(UpdateView, SystemService_UpdateView),
	    HLL_EXPORT(GetViewWidth, SystemService_GetViewWidth),
	    HLL_EXPORT(GetViewHeight, SystemService_GetViewHeight),
	    HLL_EXPORT(MoveMouseCursorPosImmediately, SystemService_MoveMouseCursorPosImmediately),
	    HLL_EXPORT(SetHideMouseCursorByGame, SystemService_SetHideMouseCursorByGame),
	    HLL_EXPORT(GetHideMouseCursorByGame, SystemService_GetHideMouseCursorByGame),
	    HLL_EXPORT(SetUsePower2Texture, SystemService_SetUsePower2Texture),
	    HLL_EXPORT(GetUsePower2Texture, SystemService_GetUsePower2Texture),
	    HLL_EXPORT(SetWindowSetting, SystemService_SetWindowSetting),
	    HLL_EXPORT(GetWindowSetting, SystemService_GetWindowSetting),
	    HLL_EXPORT(SetMouseCursorConfig, SystemService_SetMouseCursorConfig),
	    HLL_EXPORT(GetMouseCursorConfig, SystemService_GetMouseCursorConfig),
	    HLL_EXPORT(RunProgram, SystemService_RunProgram),
	    HLL_EXPORT(IsOpenedMutex, SystemService_IsOpenedMutex),
	    HLL_EXPORT(GetGameFolderPath, SystemService_GetGameFolderPath),
	    HLL_EXPORT(GetDate, SystemService_GetDate),
	    HLL_EXPORT(GetTime, SystemService_GetTime),
	    HLL_EXPORT(IsResetOnce, SystemService_IsResetOnce),
	    HLL_EXPORT(OpenPlayingManual, SystemService_OpenPlayingManual),
	    HLL_EXPORT(IsExistPlayingManual, SystemService_IsExistPlayingManual),
	    HLL_EXPORT(IsExistSystemMessage, SystemService_IsExistSystemMessage),
	    HLL_EXPORT(PopSystemMessage, SystemService_PopSystemMessage),
	    HLL_EXPORT(RestrainScreensaver, SystemService_RestrainScreensaver),
	    HLL_EXPORT(GetGameViewScaleRate, SystemService_GetGameViewScaleRate),
	    HLL_EXPORT(Debug_GetUseMemorySize, SystemService_Debug_GetUseMemorySize),
	    HLL_EXPORT(Debug_GetUseVideoMemorySize, SystemService_Debug_GetUseVideoMemorySize),
	    HLL_EXPORT(Rance0123456789, SystemService_Rance0123456789),
	    HLL_EXPORT(XXXXX01XXXXXXXX, SystemService_XXXXX01XXXXXXXX),
	    HLL_EXPORT(XXX, SystemService_XXX),
	    HLL_EXPORT(Test, SystemService_Test),
	    HLL_EXPORT(DRPKT, SystemService_DRPKT),
	    HLL_EXPORT(GetGameVersionByText, SystemService_GetGameVersionByText),
	    HLL_EXPORT(Rance96161988, SystemService_Rance96161988),
	    HLL_EXPORT(AddBackupSaveFileName, SystemService_AddBackupSaveFileName),
	    HLL_EXPORT(ShowWaitMessage, SystemService_ShowWaitMessage),
	    HLL_EXPORT(GameVariable_IsExist, SystemService_GameVariable_IsExist),
	    HLL_EXPORT(GameVariable_Set, SystemService_GameVariable_Set),
	    HLL_EXPORT(GameVariable_Get, SystemService_GameVariable_Get),
	    HLL_EXPORT(GameVariable_NumofKey, SystemService_GameVariable_NumofKey),
	    HLL_EXPORT(GameVariable_GetKey, SystemService_GameVariable_GetKey),
	    HLL_EXPORT(GameVariable_Erase, SystemService_GameVariable_Erase),
	    HLL_EXPORT(Save, SystemService_Save),
	    HLL_EXPORT(Load, SystemService_Load),
	    HLL_EXPORT(SetAntiAliasingMode, SystemService_SetAntiAliasingMode),
	    HLL_EXPORT(SetHookCloseApp, SystemService_SetHookCloseApp),
	    HLL_EXPORT(SetAndroidViewKeepScreen, SystemService_SetAndroidViewKeepScreen),
	    HLL_EXPORT(GetDefaultViewWidth, SystemService_GetDefaultViewWidth),
	    HLL_EXPORT(GetDefaultViewHeight, SystemService_GetDefaultViewHeight),
	    HLL_EXPORT(GetWindowPosX, SystemService_GetWindowPosX),
	    HLL_EXPORT(GetWindowPosY, SystemService_GetWindowPosY),
	    HLL_EXPORT(SetWindowPos, SystemService_SetWindowPos),
	    HLL_EXPORT(SetViewSize, SystemService_SetViewSize),
	    HLL_EXPORT(SetGameViewScaleRate, SystemService_SetGameViewScaleRate2),
	    HLL_EXPORT(GetPlatformType, SystemService_GetPlatformType),
	    HLL_EXPORT(GetCountryNo, SystemService_GetCountryNo),
	    HLL_EXPORT(GetAntiAliasingMode, SystemService_GetAntiAliasingMode),
	    HLL_EXPORT(IsViewResizableMode, SystemService_IsViewResizableMode),
	    HLL_EXPORT(SetViewResizableMode, SystemService_SetViewResizableMode),
	    HLL_EXPORT(IsResizableUserControl, SystemService_IsResizableUserControl),
	    HLL_EXPORT(SetResizableUserControl, SystemService_SetResizableUserControl),
	    HLL_EXPORT(IsHookCloseApp, SystemService_IsHookCloseApp),
	    HLL_EXPORT(BackupSaveFile, SystemService_BackupSaveFile),
	    HLL_EXPORT(EnableSystemPopup, SystemService_EnableSystemPopup),
	    HLL_EXPORT(PopSystemMessageString, SystemService_PopSystemMessageString),
	    HLL_EXPORT(RenderToSurfaceImage, SystemService_RenderToSurfaceImage),
	    HLL_EXPORT(RenderToSurfaceImageByScale, SystemService_RenderToSurfaceImageByScale),
	    HLL_EXPORT(SaveScreenshot, SystemService_SaveScreenshot),
	    HLL_EXPORT(SetAndroidViewOrientation, SystemService_SetAndroidViewOrientation),
	    HLL_EXPORT(SystemVariable_IsExist, SystemService_SystemVariable_IsExist),
	    HLL_EXPORT(SystemVariable_Set, SystemService_SystemVariable_Set),
	    HLL_EXPORT(SystemVariable_Get, SystemService_SystemVariable_Get),
	    HLL_EXPORT(SystemVariable_NumofKey, SystemService_SystemVariable_NumofKey),
	    HLL_EXPORT(SystemVariable_GetKey, SystemService_SystemVariable_GetKey),
	    HLL_EXPORT(SystemVariable_Erase, SystemService_SystemVariable_Erase),
	    HLL_EXPORT(Debug_IsShowMipmapLevel, SystemService_Debug_IsShowMipmapLevel),
	    HLL_EXPORT(Debug_SetShowMipmapLevel, SystemService_Debug_SetShowMipmapLevel),
	    HLL_EXPORT(GetCPUInfo, SystemService_GetCPUInfo),
	    HLL_EXPORT(GetMemoryInfo, SystemService_GetMemoryInfo),
	    HLL_EXPORT(GetOSInfo, SystemService_GetOSInfo),
	    HLL_EXPORT(GetScreenInfo, SystemService_GetScreenInfo)
	);

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void SystemService_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "SystemService");
	assert(libno >= 0);

	fun = get_fun(libno, "IsResetOnce");
	if (fun && fun->nr_arguments == 1) {
		static_library_replace(&lib_SystemService, "IsResetOnce",
				SystemService_IsResetOnce_Drapeko);
	}
}
