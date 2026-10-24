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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <assert.h>
#include <iconv.h>

#include "system4.h"
#include "system4/ald.h"
#include "system4/afa.h"
#include "system4/cg.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "gfx/font.h"

enum archive_type {
	AR_TYPE_ALD,
	AR_TYPE_AFA,
	AR_TYPE_MAX
};

const char *asset_strtype(enum asset_type type)
{
	switch (type) {
	case ASSET_BGM:   return "BGM";
	case ASSET_SOUND: return "Sound";
	case ASSET_VOICE: return "Voice";
	case ASSET_CG:    return "CG";
	case ASSET_FLAT:  return "Flat";
	case ASSET_PACT:  return "Pact";
	case ASSET_DATA:  return "Data";
	case ASSET_FLASH: return "Flash";
	}
	return "Invalid";
}

#define MAX_ARCHIVES 8

struct asset_manager {
	bool (*load_archive)(struct asset_manager *manager, const char *name);
	bool (*exists_by_id)(struct asset_manager *manager, int id);
	bool (*exists_by_name)(struct asset_manager *manager, const char *name, int *id_out);
	struct archive_data *(*get_by_id)(struct asset_manager *manager, int id);
	struct archive_data *(*get_by_name)(struct asset_manager *manager, const char *name, int *id_out);
};

// XXX: also used for AFAv1 in Daiteikoku, Shaman's Sanctuary
struct asset_manager_ald {
	struct asset_manager manager;
	struct archive *archive;
};

struct asset_manager_afa {
	struct asset_manager manager;
	struct afa_archive *archives[MAX_ARCHIVES];
};

static struct asset_manager *assets[ASSET_TYPE_MAX] = {0};

bool asset_manager_load_archive(enum asset_type type, const char *archive_name)
{
	if (!assets[type])
		return false;
	if (!assets[type]->load_archive)
		ERROR("load_archive not supported on this archive type");
	return assets[type]->load_archive(assets[type], archive_name);
}

bool asset_exists(enum asset_type type, int id)
{
	if (!assets[type])
		return false;
	return assets[type]->exists_by_id(assets[type], id);
}

bool asset_exists_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (!assets[type])
		return false;
	if (!assets[type]->exists_by_name)
		ERROR("exists_by_name not supported on this archive type");
	return assets[type]->exists_by_name(assets[type], name, id_out);
}

struct archive_data *asset_get(enum asset_type type, int id)
{
	if (!assets[type])
		return NULL;
	return assets[type]->get_by_id(assets[type], id);
}

struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (!assets[type])
		return NULL;
	if (!assets[type]->get_by_name)
		ERROR("get_by_name not supported on this archive type");
	return assets[type]->get_by_name(assets[type], name, id_out);
}

struct archive *asset_get_archive(enum asset_type type)
{
	if (!assets[type])
		return NULL;
	struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[type];
	if (manager->archives[0])
		return &manager->archives[0]->ar;
	return NULL;
}

struct cg *asset_cg_load(int id)
{
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	return cg;
}

// Convert string between SJIS and GBK using iconv.
// Returns malloc'd string or NULL on failure.
static char *convert_encoding(const char *src, const char *from, const char *to)
{
	iconv_t cd = iconv_open(to, from);
	if (cd == (iconv_t)-1)
		return NULL;
	size_t inlen = strlen(src);
	size_t outlen = inlen * 3 + 1;
	char *out = xmalloc(outlen);
	char *inp = (char *)src;
	char *outp = out;
	size_t inleft = inlen, outleft = outlen - 1;
	size_t ret = iconv(cd, &inp, &inleft, &outp, &outleft);
	iconv_close(cd);
	if (ret == (size_t)-1 || inleft > 0) {
		free(out);
		return NULL;
	}
	*outp = '\0';
	return out;
}

struct cg *asset_cg_load_by_name(const char *name, int *id_out)
{
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, id_out);
	// Fallback: if AIN is GB18030, CG archive may use GBK names while
	// game code / pactex uses SJIS names. Try SJIS→GBK conversion.
	if (!data && ain_is_gb18030) {
		char *gbk_name = convert_encoding(name, "SHIFT_JIS", "GBK");
		if (gbk_name) {
			data = asset_get_by_name(ASSET_CG, gbk_name, id_out);
			free(gbk_name);
		}
		// Also try GBK→SJIS (pactex might be GBK, archive might be SJIS)
		if (!data) {
			char *sjis_name = convert_encoding(name, "GBK", "SHIFT_JIS");
			if (sjis_name) {
				data = asset_get_by_name(ASSET_CG, sjis_name, id_out);
				free(sjis_name);
			}
		}
	}
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	return cg;
}

bool asset_cg_get_metrics(int id, struct cg_metrics *metrics)
{
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

bool asset_cg_get_metrics_by_name(const char *name, struct cg_metrics *metrics)
{
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, NULL);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

static bool ald_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	return archive_exists(manager->archive, id - 1);
}

static struct archive_data *ald_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	return archive_get(manager->archive, id - 1);
}

static bool afa_load_archive(struct asset_manager *_manager, const char *name)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	if (manager->archives[MAX_ARCHIVES-1])
		ERROR("Archive limit exceeded");

	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s.afa", name);

	int error;
	struct afa_archive *ar = afa_open(path, MMAP_IF_64BIT, &error);
	if (!ar) {
		snprintf(path, PATH_MAX, "%s.AFA", name);
		ar = afa_open(path, MMAP_IF_64BIT, &error);
		if (!ar) {
			WARNING("Failed to open archive: %s", display_utf0(path));
			return false;
		}
	}

	for (int i = MAX_ARCHIVES - 1; i > 0; i--) {
		manager->archives[i] = manager->archives[i-1];
	}
	manager->archives[0] = ar;
	return true;
}

static bool afa_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	for (int i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		if (archive_exists(&manager->archives[i]->ar, id - 1))
			return true;
	}
	return false;
}

static struct archive_data *afa_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	for (int i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		struct archive_data *data = archive_get(&manager->archives[i]->ar, id - 1);
		if (data)
			return data;
	}
	return NULL;
}

static struct archive_data *afa_get_by_name(struct asset_manager *_manager, const char *name,
		int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;

	for (unsigned i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		struct archive_data *data;
		data = archive_get_by_basename(&manager->archives[i]->ar, name);
		if (data) {
			if (id_out)
				*id_out = data->no + 1;
			return data;
		}
	}
	return NULL;
}

static bool afa_exists_by_name(struct asset_manager *_manager, const char *name, int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;

	for (unsigned i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		if (archive_exists_by_basename(&manager->archives[i]->ar, name, id_out)) {
			if (id_out)
				*id_out += 1;
			return true;
		}
	}
	return false;
}

/*
 * v14 sound groups: the timemaps of the sound and voice archives.
 *
 * Original (static disassembly of the CN executable): every member of
 * Sound.afa and Voice.afa whose name starts with "timemap_" is an EX file with
 * a table SoundInfo, and a row's Name keys a map to its Group,
 * LoopBeginSample and LoopEndSample (0x4a07f0, 0x4a0a20, 0x4a0e00; columns 0
 * to 3). Names are compared with A-Z folded to a-z and the byte after a lead
 * byte 0x81..0xFE kept as it is (0x4195b0, the GBK rule), not with the
 * archive's own folding. GetGroupNumFromFile looks the name up in maps keyed by
 * name, not in the archive (0x4a45a0 also checks two other maps whose writers
 * were not traced), so a row without a file counts.
 *
 * Port strategies: a timemap is used only with those four columns by name and
 * type (Name a string, the others ints); a timemap of another shape gives its
 * archive's files no rows. The first row of a name is kept, in the order of
 * the archives (the original's order between archives and between rows of one
 * name was not traced). Only the archives open when the sound engine starts
 * are read.
 */
struct sound_info_map {
	struct hash_table *rows;
	struct sound_info *info;
	int nr;
};
static struct sound_info_map sound_info_maps[ASSET_TYPE_MAX];

static char *sound_info_key(const char *name, size_t len)
{
	char *key = xmalloc(len + 1);
	for (size_t i = 0; i < len; i++) {
		uint8_t c = (uint8_t)name[i];
		if (c >= 0x81 && c <= 0xfe && i + 1 < len) {
			key[i] = name[i];
			key[i + 1] = name[i + 1];
			i++;
		} else {
			key[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
		}
	}
	key[len] = '\0';
	return key;
}

static void sound_info_read(struct sound_info_map *map, struct archive_data *d)
{
	struct ex *tm = ex_read(d->data, d->size);
	struct ex_table *t = tm ? ex_get_table(tm, "SoundInfo") : NULL;
	int c_name = t ? ex_col_from_name(t, "Name") : -1;
	int c_group = t ? ex_col_from_name(t, "Group") : -1;
	int c_begin = t ? ex_col_from_name(t, "LoopBeginSample") : -1;
	int c_end = t ? ex_col_from_name(t, "LoopEndSample") : -1;
	bool ok = c_name >= 0 && c_group >= 0 && c_begin >= 0 && c_end >= 0
		&& t->fields[c_name].type == EX_STRING && t->fields[c_group].type == EX_INT
		&& t->fields[c_begin].type == EX_INT && t->fields[c_end].type == EX_INT;
	for (unsigned r = 0; ok && r < t->nr_rows; r++) {
		struct ex_value *v = t->rows[r];
		ok = v[c_name].type == EX_STRING && v[c_name].s && v[c_group].type == EX_INT
			&& v[c_begin].type == EX_INT && v[c_end].type == EX_INT;
	}
	if (!ok) {
		WARNING("Sound groups: %s in another shape; its files have no timemap rows", display_utf0(d->name));
		if (tm)
			ex_free(tm);
		return;
	}
	if (!map->rows)
		map->rows = ht_create(t->nr_rows * 3 / 2 + 1);
	map->info = xrealloc_array(map->info, map->nr, map->nr + t->nr_rows, sizeof(struct sound_info));
	int first = map->nr;
	for (unsigned r = 0; r < t->nr_rows; r++) {
		struct ex_value *v = t->rows[r];
		char *key = sound_info_key(v[c_name].s->text, v[c_name].s->size);
		struct ht_slot *slot = ht_put(map->rows, key, NULL);
		free(key);
		if (slot->value)
			continue;
		map->info[map->nr] = (struct sound_info) { v[c_group].i, v[c_begin].i, v[c_end].i };
		// an index + 1, the table may still move
		slot->value = (void *)(intptr_t)(map->nr + 1);
		map->nr++;
	}
	NOTICE("Sound groups: %d of %u rows from %s", map->nr - first, t->nr_rows, display_utf0(d->name));
	ex_free(tm);
}

void asset_sound_info_load(void)
{
	static const enum asset_type types[] = { ASSET_SOUND, ASSET_VOICE };
	for (unsigned k = 0; k < sizeof(types) / sizeof(types[0]); k++) {
		enum asset_type type = types[k];
		struct sound_info_map *map = &sound_info_maps[type];
		if (map->rows || !assets[type] || assets[type]->load_archive != afa_load_archive)
			continue;
		struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[type];
		for (int i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
			struct afa_archive *ar = manager->archives[i];
			for (uint32_t f = 0; f < ar->nr_files; f++) {
				const char *n = ar->files[f].name->text;
				if (strncasecmp(n, "timemap_", 8))
					continue;
				const char *ext = file_extension(n);
				if (!ext || strcasecmp(ext, "ex"))
					continue;
				struct archive_data *d = archive_get_by_name(&ar->ar, n);
				if (!d)
					continue;
				sound_info_read(map, d);
				archive_free_data(d);
			}
		}
	}
}

static bool sound_info_lookup(enum asset_type type, const char *name, size_t len, struct sound_info *out)
{
	struct sound_info_map *map = &sound_info_maps[type];
	if (!map->rows)
		return false;
	char *key = sound_info_key(name, len);
	intptr_t i = (intptr_t)ht_get(map->rows, key, NULL);
	free(key);
	if (i <= 0)
		return false;
	*out = map->info[i - 1];
	return true;
}

bool asset_sound_info_by_name(enum asset_type type, const char *name, struct sound_info *out)
{
	return name && sound_info_lookup(type, name, strlen(name), out);
}

bool asset_sound_info_by_entry(enum asset_type type, const char *entry_name, struct sound_info *out)
{
	if (!entry_name)
		return false;
	const char *dot = strrchr(entry_name, '.');
	return sound_info_lookup(type, entry_name, dot ? (size_t)(dot - entry_name) : strlen(entry_name), out);
}

static void _ald_init(enum asset_type type, struct archive *ar)
{
	struct asset_manager_ald *manager = xcalloc(1, sizeof(struct asset_manager_ald));
	manager->manager.exists_by_id = ald_exists_by_id;
	manager->manager.get_by_id = ald_get_by_id;
	manager->manager.load_archive = NULL;
	manager->manager.exists_by_name = NULL;
	manager->manager.get_by_name = NULL;
	manager->archive = ar;
	assets[type] = &manager->manager;
}

static void ald_init(enum asset_type type, char **files, int count)
{
	if (count < 1)
		return;
	if (assets[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));

	int error;
	struct archive *ar = ald_open(files, count, MMAP_IF_64BIT, &error);
	if (!ar)
		ERROR("Failed to open ALD file: %s", archive_strerror(error));

	_ald_init(type, ar);

	for (int i = 0; i < count; i++) {
		free(files[i]);
	}
}

static void afa_init(enum asset_type type, char *file)
{
	if (!file)
		return;
	if (assets[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));

	int error;
	struct afa_archive *ar = afa_open(file, MMAP_IF_64BIT, &error);
	if (!ar)
		ERROR("Failed to open AFA file: %s", archive_strerror(error));

	struct asset_manager_afa *manager = xcalloc(1, sizeof(struct asset_manager_afa));
	manager->manager.load_archive = afa_load_archive;
	manager->manager.exists_by_id = afa_exists_by_id;
	manager->manager.get_by_id = afa_get_by_id;
	manager->manager.exists_by_name = afa_exists_by_name;
	manager->manager.get_by_name = afa_get_by_name;
	manager->archives[0] = ar;
	assets[type] = &manager->manager;

	free(file);
}

static char *get_base_name(const char *ain_filename)
{
	char *path = game_to_utf8(ain_filename, 0);
	char *dot = strrchr(path, '.');
	if (dot)
		*dot = '\0';
	return path;
}

void asset_manager_init(void)
{
	char *ald_filenames[ASSET_TYPE_MAX][ALD_FILEMAX] = {0};
	int ald_count[ASSET_TYPE_MAX] = {0};
	char *afa_filenames[ASSET_TYPE_MAX] = {0};

	UDIR *dir;
	char *d_name;

	if (!(dir = opendir_utf8(config.game_dir))) {
		ERROR("Failed to open directory: %s", display_utf0(config.game_dir));
	}

	char *base = get_base_name(config.ain_filename);
	size_t base_len = strlen(base);

	// get ALD filenames
	while ((d_name = readdir_utf8(dir))) {
		// archive filename must begin with ain filename
		if (strncasecmp(base, d_name, base_len))
			goto loop_next;
		const char *ext = file_extension(d_name);
		if (!ext)
			goto loop_next;
		if (!strcasecmp(ext, "ald")) {
			int dno = toupper(d_name[base_len+1]) - 'A';
			if (dno < 0 || dno >= ALD_FILEMAX) {
				WARNING("Invalid ALD index: %s", display_utf0(d_name));
				goto loop_next;
			}

			switch (d_name[base_len]) {
			case 'b':
			case 'B':
				ald_filenames[ASSET_BGM][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_BGM] = max(ald_count[ASSET_BGM], dno+1);
				break;
			case 'g':
			case 'G':
				ald_filenames[ASSET_CG][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_CG] = max(ald_count[ASSET_CG], dno+1);
				break;
			case 'w':
			case 'W':
				ald_filenames[ASSET_SOUND][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_SOUND] = max(ald_count[ASSET_SOUND], dno+1);
				break;
			case 'd':
			case 'D':
				ald_filenames[ASSET_DATA][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_DATA] = max(ald_count[ASSET_DATA], dno+1);
				break;
			default:
				WARNING("Unhandled ALD file: %s", display_utf0(d_name));
				break;
			}
		} else if (!strcasecmp(ext, "bgi")) {
			if (config.bgi_path) {
				WARNING("Multiple bgi files");
				goto loop_next;
			}
			config.bgi_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "wai")) {
			if (config.wai_path) {
				WARNING("Multiple wai files");
				goto loop_next;
			}
			config.wai_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "ex")) {
			if (config.ex_path) {
				WARNING("Multiple ex files");
				goto loop_next;
			}
			config.ex_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "afa")) {
			const char *type = d_name + base_len;
			if (!strcmp(type, "BA.afa")) {
				afa_filenames[ASSET_BGM] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "WA.afa") || !strcmp(type, "Sound.afa")) {
				afa_filenames[ASSET_SOUND] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Voice.afa")) {
				afa_filenames[ASSET_VOICE] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "CG.afa")) {
				afa_filenames[ASSET_CG] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Flat.afa")) {
				afa_filenames[ASSET_FLAT] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Pact.afa")) {
				afa_filenames[ASSET_PACT] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "AFF.afa")) {
				afa_filenames[ASSET_FLASH] = path_join(config.game_dir, d_name);
			}
		} else if (!strcasecmp(ext, "fnl")) {
			if (!config.fnl_path)
				config.fnl_path = path_join(config.game_dir, d_name);
		}
	loop_next:
		free(d_name);
	}
	closedir_utf8(dir);
	free(base);

	// open ALD archives
	ald_init(ASSET_BGM, ald_filenames[ASSET_BGM], ald_count[ASSET_BGM]);
	ald_init(ASSET_SOUND, ald_filenames[ASSET_SOUND], ald_count[ASSET_SOUND]);
	ald_init(ASSET_VOICE, ald_filenames[ASSET_VOICE], ald_count[ASSET_VOICE]);
	ald_init(ASSET_CG, ald_filenames[ASSET_CG], ald_count[ASSET_CG]);
	ald_init(ASSET_FLAT, ald_filenames[ASSET_FLAT], ald_count[ASSET_FLAT]);
	ald_init(ASSET_PACT, ald_filenames[ASSET_PACT], ald_count[ASSET_PACT]);
	ald_init(ASSET_DATA, ald_filenames[ASSET_DATA], ald_count[ASSET_DATA]);
	ald_init(ASSET_FLASH, ald_filenames[ASSET_FLASH], ald_count[ASSET_FLASH]);

	// open AFA archives
	afa_init(ASSET_BGM, afa_filenames[ASSET_BGM]);
	afa_init(ASSET_SOUND, afa_filenames[ASSET_SOUND]);
	afa_init(ASSET_VOICE, afa_filenames[ASSET_VOICE]);
	afa_init(ASSET_CG, afa_filenames[ASSET_CG]);
	afa_init(ASSET_FLAT, afa_filenames[ASSET_FLAT]);
	afa_init(ASSET_PACT, afa_filenames[ASSET_PACT]);
	afa_init(ASSET_DATA, afa_filenames[ASSET_DATA]);
	afa_init(ASSET_FLASH, afa_filenames[ASSET_FLASH]);
}
