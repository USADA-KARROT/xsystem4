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

/*
 * CommonSystemData — persistent key-value store for cross-session settings.
 *
 * Data is stored in-memory in a typed array and persisted to disk as a
 * simple binary file.  LoadAtStartup reads the file; every Set* call
 * triggers an auto-save so data survives crashes.
 *
 * Binary format (little-endian):
 *   magic:  "CSD1" (4 bytes)
 *   count:  uint32
 *   entries[count]:
 *     type:     uint8 (1=int, 2=float, 3=string, 4=bool)
 *     name_len: uint32
 *     name:     bytes[name_len]   (no NUL)
 *     value:    int32 | float32 | (uint32 str_len + bytes[str_len]) | uint8
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "system4.h"
#include "system4/string.h"

#include "hll.h"
#include "xsystem4.h"

enum csd_type {
	CSD_INT   = 1,
	CSD_FLOAT = 2,
	CSD_STR   = 3,
	CSD_BOOL  = 4,
};

struct csd_entry {
	char *name;
	enum csd_type type;
	union {
		int32_t i;
		float f;
		struct string *s;
		bool b;
	};
};

static struct csd_entry *entries = NULL;
static int nr_entries = 0;
static char *save_path_utf8 = NULL;

static struct csd_entry *find_entry(const char *name)
{
	for (int i = 0; i < nr_entries; i++) {
		if (!strcmp(entries[i].name, name))
			return &entries[i];
	}
	return NULL;
}

static struct csd_entry *ensure_entry(const char *name, enum csd_type type)
{
	struct csd_entry *e = find_entry(name);
	if (e) {
		if (e->type == CSD_STR && e->s) {
			free_string(e->s);
			e->s = NULL;
		}
		e->type = type;
		return e;
	}
	entries = xrealloc_array(entries, nr_entries, nr_entries + 1, sizeof(struct csd_entry));
	e = &entries[nr_entries++];
	memset(e, 0, sizeof(*e));
	e->name = xstrdup(name);
	e->type = type;
	return e;
}

static void write_u8(FILE *f, uint8_t v) { fwrite(&v, 1, 1, f); }
static void write_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void write_f32(FILE *f, float v) { fwrite(&v, 4, 1, f); }

static bool read_u8(FILE *f, uint8_t *v) { return fread(v, 1, 1, f) == 1; }
static bool read_u32(FILE *f, uint32_t *v) { return fread(v, 4, 1, f) == 1; }
static bool read_f32(FILE *f, float *v) { return fread(v, 4, 1, f) == 1; }

static void csd_save(void)
{
	if (!save_path_utf8) return;
	FILE *f = fopen(save_path_utf8, "wb");
	if (!f) return;

	fwrite("CSD1", 4, 1, f);
	write_u32(f, nr_entries);
	for (int i = 0; i < nr_entries; i++) {
		struct csd_entry *e = &entries[i];
		write_u8(f, e->type);
		uint32_t nlen = strlen(e->name);
		write_u32(f, nlen);
		fwrite(e->name, nlen, 1, f);
		switch (e->type) {
		case CSD_INT:   write_u32(f, (uint32_t)e->i); break;
		case CSD_FLOAT: write_f32(f, e->f); break;
		case CSD_STR: {
			uint32_t slen = e->s ? e->s->size : 0;
			write_u32(f, slen);
			if (slen > 0) fwrite(e->s->text, slen, 1, f);
			break;
		}
		case CSD_BOOL: write_u8(f, e->b ? 1 : 0); break;
		}
	}
	fclose(f);
}

static bool csd_load(void)
{
	if (!save_path_utf8) return false;
	FILE *f = fopen(save_path_utf8, "rb");
	if (!f) return false;

	char magic[4];
	if (fread(magic, 4, 1, f) != 1 || memcmp(magic, "CSD1", 4)) {
		fclose(f);
		return false;
	}

	uint32_t count;
	if (!read_u32(f, &count)) { fclose(f); return false; }

	for (uint32_t i = 0; i < count; i++) {
		uint8_t type;
		uint32_t nlen;
		if (!read_u8(f, &type) || !read_u32(f, &nlen)) break;
		char *name = xmalloc(nlen + 1);
		if (fread(name, nlen, 1, f) != 1 && nlen > 0) { free(name); break; }
		name[nlen] = '\0';

		struct csd_entry *e = ensure_entry(name, type);
		free(name);

		switch (type) {
		case CSD_INT: {
			uint32_t v;
			if (read_u32(f, &v)) e->i = (int32_t)v;
			break;
		}
		case CSD_FLOAT:
			read_f32(f, &e->f);
			break;
		case CSD_STR: {
			uint32_t slen;
			if (!read_u32(f, &slen)) break;
			char *buf = xmalloc(slen + 1);
			if (slen > 0 && fread(buf, slen, 1, f) != 1) { free(buf); break; }
			buf[slen] = '\0';
			e->s = cstr_to_string(buf);
			free(buf);
			break;
		}
		case CSD_BOOL: {
			uint8_t v;
			if (read_u8(f, &v)) e->b = v != 0;
			break;
		}
		}
	}
	fclose(f);
	return true;
}

static void CommonSystemData_SetFullPathSaveFileName(struct string *filename)
{
	free(save_path_utf8);
	save_path_utf8 = unix_path(filename->text);
}

static bool CommonSystemData_LoadAtStartup(void)
{
	return csd_load() || true; /* missing file is OK */
}

static bool CommonSystemData_SetDataInt(struct string *name, int value)
{
	struct csd_entry *e = ensure_entry(name->text, CSD_INT);
	e->i = value;
	csd_save();
	return true;
}

static bool CommonSystemData_SetDataFloat(struct string *name, float value)
{
	struct csd_entry *e = ensure_entry(name->text, CSD_FLOAT);
	e->f = value;
	csd_save();
	return true;
}

static bool CommonSystemData_SetDataString(struct string *name, struct string *value)
{
	struct csd_entry *e = ensure_entry(name->text, CSD_STR);
	e->s = string_dup(value);
	csd_save();
	return true;
}

static bool CommonSystemData_SetDataBool(struct string *name, bool value)
{
	struct csd_entry *e = ensure_entry(name->text, CSD_BOOL);
	e->b = value;
	csd_save();
	return true;
}

// v14: AIN declares GetData* arg[1] as AIN_WRAP — pointer-based interface

static bool CommonSystemData_GetDataInt(struct string *name, int *value)
{
	struct csd_entry *e = find_entry(name->text);
	if (!e || e->type != CSD_INT) return false;
	if (value) *value = e->i;
	return true;
}

static bool CommonSystemData_GetDataFloat(struct string *name, float *value)
{
	struct csd_entry *e = find_entry(name->text);
	if (!e || e->type != CSD_FLOAT) return false;
	if (value) *value = e->f;
	return true;
}

static bool CommonSystemData_GetDataString(struct string *name, int *value)
{
	struct csd_entry *e = find_entry(name->text);
	if (!e || e->type != CSD_STR || !e->s) return false;
	wrap_set_string(value, string_dup(e->s));
	return true;
}

static bool CommonSystemData_GetDataBool(struct string *name, int *value)
{
	struct csd_entry *e = find_entry(name->text);
	if (!e || e->type != CSD_BOOL) return false;
	if (value) *value = e->b ? 1 : 0;
	return true;
}

HLL_LIBRARY(CommonSystemData,
	    HLL_EXPORT(SetFullPathSaveFileName, CommonSystemData_SetFullPathSaveFileName),
	    HLL_EXPORT(LoadAtStartup, CommonSystemData_LoadAtStartup),
	    HLL_EXPORT(SetDataInt, CommonSystemData_SetDataInt),
	    HLL_EXPORT(SetDataFloat, CommonSystemData_SetDataFloat),
	    HLL_EXPORT(SetDataString, CommonSystemData_SetDataString),
	    HLL_EXPORT(SetDataBool, CommonSystemData_SetDataBool),
	    HLL_EXPORT(GetDataInt, CommonSystemData_GetDataInt),
	    HLL_EXPORT(GetDataFloat, CommonSystemData_GetDataFloat),
	    HLL_EXPORT(GetDataString, CommonSystemData_GetDataString),
	    HLL_EXPORT(GetDataBool, CommonSystemData_GetDataBool));
