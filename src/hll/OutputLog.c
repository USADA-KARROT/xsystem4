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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "xsystem4.h"

struct output_log {
	struct string *name;
	char **lines;
	int nr_lines;
	char *auto_save_path;
};

static struct output_log *logs = NULL;
static int nr_logs = 0;

int OutputLog_Create(struct string *name)
{
	logs = xrealloc_array(logs, nr_logs, nr_logs + 1, sizeof(struct output_log));
	memset(&logs[nr_logs], 0, sizeof(struct output_log));
	logs[nr_logs].name = string_dup(name);
	return nr_logs++;
}

static void do_auto_save(int handle);

static void OutputLog_Output(int handle, struct string *s)
{
	if (handle < 0 || handle >= nr_logs) {
		log_message("OutputLog", "%s", display_sjis0(s->text));
		return;
	}
	struct output_log *log = &logs[handle];
	log_message(display_sjis0(log->name->text), "%s", display_sjis1(s->text));
	log->lines = xrealloc_array(log->lines, log->nr_lines, log->nr_lines + 1, sizeof(char*));
	log->lines[log->nr_lines++] = xstrdup(s->text);
	if (log->auto_save_path)
		do_auto_save(handle);
}

static void OutputLog_Clear(int handle)
{
	if (handle < 0 || handle >= nr_logs) return;
	struct output_log *log = &logs[handle];
	for (int i = 0; i < log->nr_lines; i++)
		free(log->lines[i]);
	free(log->lines);
	log->lines = NULL;
	log->nr_lines = 0;
}

static int OutputLog_Save(int handle, struct string *filename)
{
	if (handle < 0 || handle >= nr_logs) return 0;
	struct output_log *log = &logs[handle];
	char *path = unix_path(filename->text);
	FILE *f = fopen(path, "w");
	if (!f) {
		WARNING("OutputLog.Save: fopen('%s'): %s", display_utf0(path), strerror(errno));
		free(path);
		return 0;
	}
	for (int i = 0; i < log->nr_lines; i++) {
		fputs(log->lines[i], f);
		fputc('\n', f);
	}
	fclose(f);
	free(path);
	return 1;
}

static void do_auto_save(int handle)
{
	if (handle < 0 || handle >= nr_logs) return;
	struct output_log *log = &logs[handle];
	if (!log->auto_save_path) return;
	FILE *f = fopen(log->auto_save_path, "w");
	if (!f) return;
	for (int i = 0; i < log->nr_lines; i++) {
		fputs(log->lines[i], f);
		fputc('\n', f);
	}
	fclose(f);
}

static bool OutputLog_EnableAutoSave(int handle, struct string *filename)
{
	if (handle < 0 || handle >= nr_logs) return false;
	struct output_log *log = &logs[handle];
	free(log->auto_save_path);
	log->auto_save_path = unix_path(filename->text);
	return true;
}

static bool OutputLog_DisableAutoSave(int handle)
{
	if (handle < 0 || handle >= nr_logs) return false;
	struct output_log *log = &logs[handle];
	free(log->auto_save_path);
	log->auto_save_path = NULL;
	return true;
}

HLL_LIBRARY(OutputLog,
	    HLL_EXPORT(Create, OutputLog_Create),
	    HLL_EXPORT(Output, OutputLog_Output),
	    HLL_EXPORT(Clear, OutputLog_Clear),
	    HLL_EXPORT(Save, OutputLog_Save),
	    HLL_EXPORT(EnableAutoSave, OutputLog_EnableAutoSave),
	    HLL_EXPORT(DisableAutoSave, OutputLog_DisableAutoSave));
