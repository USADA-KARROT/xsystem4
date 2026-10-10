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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <SDL.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "msgskip.h"
#include "vm.h"
#include "xsystem4.h"

#include "hll.h"

static bool enabled = false;
static int state = 0;

static char *save_path;
static uint8_t *flags;
static int nr_flags;

/*
 * Original file format.
 *
 * The Dohna Dohna engine (AIN v14) declares MsgSkip without an Init function,
 * creates the table itself for ain->nr_messages messages and keeps it in
 * "<save folder>/MsgSkip.msk". The layout below is from that one executable
 * (reader 0x4c7b50, writer 0x4c7840). It is used for every v14 AIN whose
 * MsgSkip library has no Init; no other v14 game has been checked. All
 * integers are little endian:
 *
 *   "MSK\0"            magic, a NUL-terminated string
 *   int32 0            format version
 *   string\0           a second string; read and written back unchanged
 *   int32 nr_messages
 *   int32 nr_bytes     size of the bit table, (nr_messages + 7) / 8
 *   byte[nr_bytes]     message n is bit 0x80 >> (n & 7) of byte n >> 3
 *   int32 nr_adv
 *   nr_adv times:      name\0, int32 version, int32 step
 *
 * The original builds the file name as <prefix> + "MsgSkip.msk" with a
 * prefix taken from the game's EX data (0x4b50c8); only the empty prefix is
 * implemented here.
 *
 * The file is written when the main window closes (0x4be9b0) and as the
 * first step of a reset (0x4c12e0, before anything is released), never from
 * SetFlag. _ModuleFini is the matching place for both, since vm_exit() and
 * vm_reset() run it; msgskip_flush() is the reset write for a caller that
 * does not get to vm_reset().
 */
#define MSK_FILE_NAME "MsgSkip.msk"

struct adv_flag {
	char *name;
	int version;
	int step;
};

// True from _PreLink to _ModuleFini when the original format is in use.
static bool msk_active;
// The second string of the file.
static char *msk_tag;
// The original keeps these in an unordered_map; here they stay in the order
// they were read or added and are searched linearly. The count is expected
// to be 0 (the original's own file has none).
static struct adv_flag *adv_flags;
static int nr_adv_flags;

static bool msgskip_write(void)
{
	FILE *f = file_open_utf8(save_path, "wb");
	if (!f)
		return false;

	uint8_t header[4];
	LittleEndian_putDW(header, 0, nr_flags);
	fwrite(header, sizeof(header), 1, f);
	fwrite(flags, (nr_flags + 7) / 8, 1, f);
	fclose(f);
	return true;
}

static void msgskip_save(void)
{
	if (!msgskip_write())
		ERROR("fopen: '%s': %s", display_utf0(save_path), strerror(errno));
}

static int msgskip_count(void)
{
	int count = 0;
	for (int i = 0; i < (nr_flags + 7) / 8; i++) {
		for (uint8_t b = flags[i]; b; b &= b - 1)
			count++;
	}
	return count;
}

static struct adv_flag *adv_find(const char *name)
{
	for (int i = 0; i < nr_adv_flags; i++) {
		if (!strcmp(adv_flags[i].name, name))
			return &adv_flags[i];
	}
	return NULL;
}

static void adv_add(const char *name, int version, int step)
{
	adv_flags = xrealloc_array(adv_flags, nr_adv_flags, nr_adv_flags + 1, sizeof(struct adv_flag));
	adv_flags[nr_adv_flags++] = (struct adv_flag) {
		.name = xstrdup(name),
		.version = version,
		.step = step,
	};
}

static void adv_clear(void)
{
	for (int i = 0; i < nr_adv_flags; i++)
		free(adv_flags[i].name);
	free(adv_flags);
	adv_flags = NULL;
	nr_adv_flags = 0;
}

struct msk_reader {
	const uint8_t *p;
	const uint8_t *end;
};

static bool msk_get_string(struct msk_reader *r, const char **out)
{
	const uint8_t *nul = memchr(r->p, 0, r->end - r->p);
	if (!nul)
		return false;
	*out = (const char*)r->p;
	r->p = nul + 1;
	return true;
}

static bool msk_get_int(struct msk_reader *r, int32_t *out)
{
	if (r->end - r->p < 4)
		return false;
	*out = LittleEndian_getDW(r->p, 0);
	r->p += 4;
	return true;
}

/*
 * Reader (0x4c7b50). A missing file leaves every message unread. On a wrong
 * magic or version, a zero table size or a truncated file the original stops
 * without a message and keeps what it has read so far; so does this.
 */
static void msk_load(void)
{
	size_t size;
	uint8_t *data = file_read(save_path, &size);
	if (!data) {
		// The original reports an existing file it cannot read, or an
		// empty one, in a message box and goes on.
		if (file_exists(save_path))
			WARNING("Failed to read MsgSkip file '%s'", display_utf0(save_path));
		return;
	}

	struct msk_reader r = { data, data + size };
	const char *str;
	const uint8_t *bits;
	int32_t version, file_msgs, file_bytes, nr_adv;
	uint32_t table_bytes = (nr_flags + 7) / 8;
	uint32_t need;
	if (!msk_get_string(&r, &str) || strcmp(str, "MSK"))
		goto end;
	if (!msk_get_int(&r, &version) || version != 0)
		goto end;
	if (!msk_get_string(&r, &str))
		goto end;
	free(msk_tag);
	msk_tag = xstrdup(str);
	if (!msk_get_int(&r, &file_msgs) || !msk_get_int(&r, &file_bytes) || !file_bytes)
		goto end;
	if ((uint32_t)file_bytes > (size_t)(r.end - r.p))
		goto end;
	bits = r.p;
	r.p += (uint32_t)file_bytes;

	// 0x4c7e22: the table is cleared, then the first (n + 7) / 8 bytes are
	// taken for n = min(file count, AIN count), unless the file's table is
	// larger than that (unsigned compares). So a file whose table is
	// larger than this AIN's is not loaded at all, and one written for
	// fewer messages gives the leading part.
	memset(flags, 0, table_bytes);
	if (file_msgs > nr_flags)
		file_msgs = nr_flags;
	need = (file_msgs + 7) / 8;
	if (need > table_bytes || need < (uint32_t)file_bytes)
		goto end;
	// The original copies `need` bytes even from a shorter table, reading
	// past its buffer; the bytes the file does not have stay zero here.
	memcpy(flags, bits, (uint32_t)file_bytes);

	adv_clear();
	if (!msk_get_int(&r, &nr_adv))
		goto end;
	for (int i = 0; i < nr_adv; i++) {
		int32_t adv_version, adv_step;
		if (!msk_get_string(&r, &str) || !msk_get_int(&r, &adv_version)
		    || !msk_get_int(&r, &adv_step))
			break;
		// unordered_map insert: the first entry of a name is kept
		if (!adv_find(str))
			adv_add(str, adv_version, adv_step);
	}
end:
	free(data);
}

/*
 * The original overwrites the file in place (CreateFile with CREATE_ALWAYS,
 * 0x6dbb70). Replace it through a temporary file instead, so that a write
 * that fails halfway keeps the previous flags.
 */
static bool msk_write_file(const char *path, uint8_t *data, size_t size)
{
#ifdef _WIN32
	// rename() does not replace an existing file there.
	return file_write(path, data, size);
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

static uint8_t *msk_put_int(uint8_t *p, int32_t v)
{
	LittleEndian_putDW(p, 0, v);
	return p + 4;
}

static uint8_t *msk_put_string(uint8_t *p, const char *s)
{
	size_t len = strlen(s) + 1;
	memcpy(p, s, len);
	return p + len;
}

/*
 * Writer (0x4c7840): the whole file every time, changed or not, and nothing
 * for an empty table. A failure is reported and otherwise ignored (a message
 * box in the original).
 */
static bool msk_save(void)
{
	uint32_t table_bytes = (nr_flags + 7) / 8;
	if (!flags || !table_bytes)
		return true;

	const char *tag = msk_tag ? msk_tag : "";
	size_t size = 4 + 4 + strlen(tag) + 1 + 4 + 4 + table_bytes + 4;
	for (int i = 0; i < nr_adv_flags; i++)
		size += strlen(adv_flags[i].name) + 1 + 4 + 4;

	uint8_t *buf = xmalloc(size);
	uint8_t *p = msk_put_string(buf, "MSK");
	p = msk_put_int(p, 0);
	p = msk_put_string(p, tag);
	p = msk_put_int(p, nr_flags);
	p = msk_put_int(p, table_bytes);
	memcpy(p, flags, table_bytes);
	p += table_bytes;
	p = msk_put_int(p, nr_adv_flags);
	for (int i = 0; i < nr_adv_flags; i++) {
		p = msk_put_string(p, adv_flags[i].name);
		p = msk_put_int(p, adv_flags[i].version);
		p = msk_put_int(p, adv_flags[i].step);
	}
	assert(p == buf + size);

	bool ok = msk_write_file(save_path, buf, size);
	free(buf);
	if (ok)
		NOTICE("MsgSkip: saved %d of %d messages read (%s)", msgskip_count(), nr_flags, MSK_FILE_NAME);
	else
		WARNING("Failed to save MsgSkip file '%s': %s", display_utf0(save_path), strerror(errno));
	return ok;
}

#ifdef __ANDROID__
static int msgskip_event_handler(possibly_unused void *user_data, SDL_Event *e) {
	switch (e->type) {
	case SDL_APP_WILLENTERBACKGROUND:
		if (msk_active)
			msk_save();
		else if (flags)
			msgskip_save();
		break;
	}
	return 0;
}
#endif

// 0x4c7650, called by the original once the AIN and the EX data are loaded.
static void msk_init(void)
{
	nr_flags = ain->nr_messages > 0 ? ain->nr_messages : 0;
	flags = nr_flags ? xcalloc((nr_flags + 7) / 8, 1) : NULL;
	save_path = savedir_path(MSK_FILE_NAME);
	// An empty table is neither read nor written (0x4c7b92, 0x4c7870).
	if (flags)
		msk_load();
	msk_active = true;
	if (flags)
		NOTICE("MsgSkip: %d of %d messages read (%s)", msgskip_count(), nr_flags, MSK_FILE_NAME);
#ifdef __ANDROID__
	static bool watching = false;
	if (!watching) {
		SDL_AddEventWatch(msgskip_event_handler, NULL);
		watching = true;
	}
#endif
}

static void msk_fini(void)
{
	msk_save();
	// The original destroys the object after the reset write (0x4b33c0)
	// and reads the file again when it starts over.
	free(flags);
	flags = NULL;
	nr_flags = 0;
	free(save_path);
	save_path = NULL;
	free(msk_tag);
	msk_tag = NULL;
	adv_clear();
	msk_active = false;
}

static int MsgSkip_Init(struct string *name)
{
	if (flags)
		return 1;

	nr_flags = ain->nr_messages;
	flags = xcalloc((nr_flags + 7) / 8, 1);

	// Most games have system.GetSaveFolderName() prepended to `name`, but some
	// games pass a constant string "SaveData\\MsgSkip.asd". If you pass it to
	// savedir_path() as it is, "SaveData/" will be duplicated, so remove it.
	if (!strncmp(name->text, "SaveData\\", 9)) {
		save_path = savedir_path(name->text + 9);
	} else {
		save_path = savedir_path(name->text);
	}

	size_t data_size;
	uint8_t *data = file_read(save_path, &data_size);
	if (data) {
		if (data_size == (size_t)ain->nr_messages) {
			// Migrate from the old format.
			for (int i = 0; i < data_size; i++) {
				if (data[i])
					flags[i >> 3] |= 0x80 >> (i & 7);
			}
		} else {
			int n = LittleEndian_getDW(data, 0);
			if ((n + 7) / 8 != data_size - 4) {
				WARNING("Corrupted MsgSkip file '%s'", display_utf0(save_path));
			} else if (n != nr_flags) {
				WARNING("Incorrect size for MsgSkip file '%s'", display_utf0(save_path));
				if (n > nr_flags)
					n = nr_flags;
				memcpy(flags, data + 4, (n + 7) / 8);
			} else {
				memcpy(flags, data + 4, (nr_flags + 7) / 8);
			}
		}
		free(data);
	}
#ifdef __ANDROID__
	SDL_AddEventWatch(msgskip_event_handler, NULL);
#endif
	atexit(msgskip_save);
	return 1;
}

// 0x4c8700: nothing is written to the file here.
static void MsgSkip_SetFlag(int msgnum)
{
	if (!flags || msgnum < 0 || msgnum >= nr_flags)
		return;
	flags[msgnum >> 3] |= 0x80 >> (msgnum & 7);
}

// 0x4c7700
static int MsgSkip_GetFlag(int msgnum)
{
	if (!flags || msgnum < 0 || msgnum >= nr_flags)
		return 0;
	return !!(flags[msgnum >> 3] & 0x80 >> (msgnum & 7));
}

/*
 * 0x4c7760: a new name is added; with the recorded version the step only
 * grows; another version replaces both.
 */
static void MsgSkip_SetAdvFlag(struct string *name, int version, int step)
{
	if (!msk_active)
		return;
	struct adv_flag *f = adv_find(name->text);
	if (!f) {
		adv_add(name->text, version, step);
	} else if (f->version != version) {
		f->version = version;
		f->step = step;
	} else if (f->step < step) {
		f->step = step;
	}
}

// 0x4c8750: the name is recorded with this version and has reached `step`.
bool msgskip_get_adv_flag(const char *name, int version, int step)
{
	if (!msk_active)
		return false;
	struct adv_flag *f = adv_find(name);
	return f && f->version == version && f->step >= step;
}

static int MsgSkip_GetAdvFlag(struct string *name, int version, int step)
{
	return msgskip_get_adv_flag(name->text, version, step);
}

bool msgskip_get_flag(int msgnum)
{
	return MsgSkip_GetFlag(msgnum);
}

/*
 * The write the original does first on a reset (0x4b5bc3 -> 0x4c12e0 ->
 * 0x4c7840), before the object is destroyed and read back: the file is
 * written and the table stays as it is. Only the original format; the old
 * one is left to _ModuleFini.
 */
void msgskip_flush(void)
{
	if (msk_active)
		msk_save();
}

static void MsgSkip_SetEnable(int enable)
{
	enabled = !!enable;
}

static int MsgSkip_GetEnable(void)
{
	return enabled;
}

static void MsgSkip_SetState(int _state)
{
	state = _state;
}

static int MsgSkip_GetState(void)
{
	return state;
}

HLL_WARN_UNIMPLEMENTED( , void, MsgSkip, UseFlag, int use);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofMsg, void);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofFlag, void);

static void MsgSkip_PreLink(void)
{
	int libno = ain_get_library(ain, "MsgSkip");
	assert(libno >= 0);
	if (ain_get_library_function(ain, libno, "Init") >= 0)
		return;
	if (ain->version >= 14) {
		if (!msk_active)
			msk_init();
		return;
	}
	struct string *name = cstr_to_string("MsgSkip.asd");
	MsgSkip_Init(name);
	free_string(name);
}

extern struct static_library lib_MsgSkip;

/*
 * The ADV flags exist only in the original format, so SetAdvFlag and
 * GetAdvFlag are bound only there, and only for the declaration the
 * original dispatches (0x4c7230: string, int, int). Anything else stays
 * unlinked as before.
 */
static void msk_bind(int libno, const char *name, enum ain_data_type return_type, void *fun)
{
	int fno = ain_get_library_function(ain, libno, name);
	if (fno < 0)
		return;
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];
	if (f->return_type.data != return_type || f->nr_arguments != 3
	    || f->arguments[0].type.data != AIN_STRING
	    || f->arguments[1].type.data != AIN_INT
	    || f->arguments[2].type.data != AIN_INT)
		return;
	static_library_register(&lib_MsgSkip, name, fun);
}

// Runs after link_libraries(), which static_library_register() needs.
static void MsgSkip_PostLink(void)
{
	int libno = ain_get_library(ain, "MsgSkip");
	if (libno < 0 || !msk_active)
		return;
	msk_bind(libno, "SetAdvFlag", AIN_VOID, MsgSkip_SetAdvFlag);
	msk_bind(libno, "GetAdvFlag", AIN_INT, MsgSkip_GetAdvFlag);
}

static void MsgSkip_ModuleFini(void)
{
	if (msk_active) {
		msk_fini();
	} else if (flags) {
		// The old format: a game that called Init, or one before v14
		// without Init that _PreLink set up. Upstream saves it from
		// the atexit handler registered in MsgSkip_Init; sys_exit() is
		// _exit() in this tree, so that handler never runs. This
		// brings the save back for vm_exit() only: a VM error, ERROR()
		// and a main() that returns still do not save, and neither
		// does a game that uses vmMsgSkip without calling Close.
		// Unlike upstream it also writes on a reset; the table stays
		// in memory there, as before.
		if (!msgskip_write())
			WARNING("Failed to save MsgSkip file '%s': %s", display_utf0(save_path), strerror(errno));
	}
}

HLL_LIBRARY(MsgSkip,
	    HLL_EXPORT(_PreLink, MsgSkip_PreLink),
	    HLL_EXPORT(_PostLink, MsgSkip_PostLink),
	    HLL_EXPORT(_ModuleFini, MsgSkip_ModuleFini),
	    HLL_EXPORT(Init, MsgSkip_Init),
	    HLL_EXPORT(UseFlag, MsgSkip_UseFlag),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(SetFlag, MsgSkip_SetFlag),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState),
	    HLL_EXPORT(GetFlag, MsgSkip_GetFlag),
	    HLL_EXPORT(GetNumofMsg, MsgSkip_GetNumofMsg),
	    HLL_EXPORT(GetNumofFlag, MsgSkip_GetNumofFlag));

HLL_QUIET_UNIMPLEMENTED(, void, vmMsgSkip, Init, void *imainsystem);

static int vmMsgSkip_EnumHandle(void)
{
	return flags ? 1 : 0;
}

static int vmMsgSkip_Open(struct string *fname, possibly_unused int option)
{
	return MsgSkip_Init(fname);
}

static void vmMsgSkip_Close(possibly_unused int handle)
{
	msgskip_save();
	free(flags);
	flags = NULL;
}

static int vmMsgSkip_Query(possibly_unused int handle, int msgnum)
{
	int r = MsgSkip_GetFlag(msgnum);
	// vmMsgSkip does not have an explicit set function.
	MsgSkip_SetFlag(msgnum);
	return state && r;
}

HLL_LIBRARY(vmMsgSkip,
	    HLL_EXPORT(Init, vmMsgSkip_Init),
	    HLL_EXPORT(EnumHandle, vmMsgSkip_EnumHandle),
	    HLL_EXPORT(Open, vmMsgSkip_Open),
	    HLL_EXPORT(Close, vmMsgSkip_Close),
	    HLL_EXPORT(Query, vmMsgSkip_Query),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState)
	    );
