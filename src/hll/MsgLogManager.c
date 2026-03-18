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
#include <string.h>
#include <stdio.h>
#include "system4.h"
#include "system4/string.h"
#include "hll.h"
#include "xsystem4.h"
#include "MsgLogManager.h"

static size_t msg_log_size = 0;
static size_t msg_log_i = 0;
struct msg_log_entry *msg_log = NULL;

#define LOG_MAX 2048

static void msg_log_alloc(void)
{
	if (msg_log_i < msg_log_size)
		return;
	if (!msg_log_size)
		msg_log_size = 1;

	// rotate log
	if (msg_log_i >= LOG_MAX) {
		for (size_t i = 0; i < msg_log_i - LOG_MAX/2; i++) {
			if (msg_log[i].data_type == MSG_LOG_STRING)
				free_string(msg_log[i].s);
		}
		memcpy(msg_log, msg_log + (msg_log_i - LOG_MAX/2), sizeof(struct msg_log_entry) * LOG_MAX/2);
		msg_log_i = LOG_MAX/2;
		return;
	}

	msg_log = xrealloc(msg_log, sizeof(struct msg_log_entry) * msg_log_size * 2);
	msg_log_size *= 2;
}

int MsgLogManager_Numof(void)
{
	return msg_log_i;
}

static void MsgLogManager_AddInt(int type, int value)
{
	msg_log_alloc();
	msg_log[msg_log_i++] = (struct msg_log_entry) {
		.data_type = MSG_LOG_INT,
		.type = type,
		.i = value
	};
}

static void MsgLogManager_AddString(int type, struct string *str)
{
	msg_log_alloc();
	msg_log[msg_log_i++] = (struct msg_log_entry) {
		.data_type = MSG_LOG_STRING,
		.type = type,
		.s = string_ref(str)
	};
}

static int MsgLogManager_GetType(int index)
{
	if (index < 0 || (size_t)index >= msg_log_i) return 0;
	return msg_log[index].type;
}

static int MsgLogManager_GetInt(int index)
{
	if (index < 0 || (size_t)index >= msg_log_i) return 0;
	if (msg_log[index].data_type != MSG_LOG_INT) return 0;
	return msg_log[index].i;
}

static void MsgLogManager_GetString(int index, struct string **str)
{
	if (index < 0 || (size_t)index >= msg_log_i || !str) return;
	if (msg_log[index].data_type == MSG_LOG_STRING && msg_log[index].s)
		*str = string_ref(msg_log[index].s);
	else
		*str = string_ref(&EMPTY_STRING);
}

static void MsgLogManager_SetLineMax(int line_max)
{
	/* hint only — our circular buffer already handles overflow */
}

static int MsgLogManager_GetInterface(void)
{
	return 0;
}

/*
 * Binary format: "MLG1" + count(u32) + entries[count]
 * Entry: data_type(u8) + type(i32) + value(i32 or u32+bytes)
 */
static int MsgLogManager_Save(struct string *filename)
{
	char *path = unix_path(filename->text);
	FILE *f = fopen(path, "wb");
	free(path);
	if (!f) return 0;

	fwrite("MLG1", 4, 1, f);
	uint32_t count = msg_log_i;
	fwrite(&count, 4, 1, f);
	for (size_t i = 0; i < msg_log_i; i++) {
		uint8_t dt = msg_log[i].data_type;
		fwrite(&dt, 1, 1, f);
		int32_t type = msg_log[i].type;
		fwrite(&type, 4, 1, f);
		if (dt == MSG_LOG_INT) {
			int32_t val = msg_log[i].i;
			fwrite(&val, 4, 1, f);
		} else {
			const char *text = msg_log[i].s ? msg_log[i].s->text : "";
			uint32_t len = strlen(text);
			fwrite(&len, 4, 1, f);
			fwrite(text, len, 1, f);
		}
	}
	fclose(f);
	return 1;
}

static int MsgLogManager_Load(struct string *filename)
{
	char *path = unix_path(filename->text);
	FILE *f = fopen(path, "rb");
	free(path);
	if (!f) return 0;

	char magic[4];
	if (fread(magic, 4, 1, f) != 1 || memcmp(magic, "MLG1", 4)) {
		fclose(f);
		return 0;
	}

	uint32_t count;
	if (fread(&count, 4, 1, f) != 1) { fclose(f); return 0; }

	/* clear existing log */
	for (size_t i = 0; i < msg_log_i; i++) {
		if (msg_log[i].data_type == MSG_LOG_STRING && msg_log[i].s)
			free_string(msg_log[i].s);
	}
	msg_log_i = 0;

	for (uint32_t i = 0; i < count; i++) {
		uint8_t dt;
		int32_t type;
		if (fread(&dt, 1, 1, f) != 1) break;
		if (fread(&type, 4, 1, f) != 1) break;

		msg_log_alloc();
		msg_log[msg_log_i].data_type = dt;
		msg_log[msg_log_i].type = type;

		if (dt == MSG_LOG_INT) {
			int32_t val;
			if (fread(&val, 4, 1, f) != 1) break;
			msg_log[msg_log_i].i = val;
		} else {
			uint32_t len;
			if (fread(&len, 4, 1, f) != 1) break;
			char *buf = xmalloc(len + 1);
			if (len > 0 && fread(buf, len, 1, f) != 1) { free(buf); break; }
			buf[len] = '\0';
			msg_log[msg_log_i].s = cstr_to_string(buf);
			free(buf);
		}
		msg_log_i++;
	}
	fclose(f);
	return 1;
}

HLL_LIBRARY(MsgLogManager,
	    HLL_EXPORT(Numof, MsgLogManager_Numof),
	    HLL_EXPORT(AddInt, MsgLogManager_AddInt),
	    HLL_EXPORT(AddString, MsgLogManager_AddString),
	    HLL_EXPORT(GetType, MsgLogManager_GetType),
	    HLL_EXPORT(GetInt, MsgLogManager_GetInt),
	    HLL_EXPORT(GetString, MsgLogManager_GetString),
	    HLL_EXPORT(Save, MsgLogManager_Save),
	    HLL_EXPORT(Load, MsgLogManager_Load),
	    HLL_EXPORT(SetLineMax, MsgLogManager_SetLineMax),
	    HLL_EXPORT(GetInterface, MsgLogManager_GetInterface));
