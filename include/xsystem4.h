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

#ifndef XSYSTEM4_H
#define XSYSTEM4_H

#include <stddef.h>
#include <stdbool.h>

enum resume_save_format {
	SAVE_FORMAT_JSON,
	SAVE_FORMAT_RSM,
};

struct config {
	char *game_name;
	char *boot_name;
	char *ain_filename;
	char *vm_name;
	char *game_dir;
	char *save_dir;
	char *home_dir;
	int view_width;
	int view_height;
	size_t mixer_nr_channels;
	char **mixer_channels;
	int *mixer_volumes;
	int default_volume;

	char *bgi_path;
	char *wai_path;
	char *ex_path;
	char *fnl_path;
	char *font_paths[2];

	bool joypad;
	bool echo;
	float text_x_scale;
	bool manual_text_x_scale;
	enum resume_save_format save_format;
	int msgskip_delay;
	bool skip_title;
};

extern struct config config;

// Convenience functions to avoid cumbersome memory management when passing
// text to printf-like functions
const char *display_sjis0(const char *sjis);
const char *display_sjis1(const char *sjis);
const char *display_sjis2(const char *sjis);
const char *display_utf0(const char *utf);
const char *display_utf1(const char *utf);
const char *display_utf2(const char *utf);
// Raw game strings (see game_charset_is_gbk): UTF-8 in GBK mode, unchanged otherwise.
const char *display_game0(const char *s);
const char *display_game1(const char *s);
const char *display_game2(const char *s);

bool game_charset_is_gbk(void);
char *game_to_utf8(const char *s, size_t len);
char *utf8_to_game(const char *s, size_t len);
int rename_noreplace(const char *from, const char *to);
char *save_dir_for_game_charset(const char *home, const char *game_name,
		const char *save_folder, const char *legacy_save_dir);
struct string;
void migrate_legacy_save_files(struct string **strings, int nr_strings);

void indent_message(int indent, const char *fmt, ...);

void log_message(const char *log, const char *fmt, ...);

#define UNIMPLEMENTED(fmt, ...) \
	sys_warning("unimplemented: %s" fmt "\n", __func__, ##__VA_ARGS__)

bool is_absolute_path(const char *path);
char *unix_path(const char *path);
char *gamedir_path(const char *path);
char *gamedir_path_icase(const char *path);
char *savedir_path(const char *path);

void get_date(int *year, int *month, int *mday, int *wday);
void get_time(int *hour, int *min, int *sec, int *ms);

void screenshot_save(void);

#ifndef XSYS4_DATA_DIR
#define XSYS4_DATA_DIR "/usr/local/share/xsystem4"
#endif

#define MMAP_IF_64BIT (sizeof(void*) >= 8 ? ARCHIVE_MMAP : 0)

extern bool game_daibanchou_en;
extern bool game_rance02_mg;
extern bool game_rance6_mg;
extern bool game_rance7_mg;
extern bool game_rance8;
extern bool game_rance8_mg;
extern bool game_dungeons_and_dolls;

/* CN (GBK/GB18030) text support. Auto-detected in system4.c; consumed by the
 * text/encoding paths. Originally declared in the fork's libsys4 header, which
 * is no longer available, so it lives here and is defined in hacks.c. */
extern bool ain_is_gb18030;

/*
 * GB18030/GBK detection over an AIN string table (hacks.c).
 * legacy:       the historical check: among the first 100 strings, those with
 *               a byte in A1..DF followed by a byte >= 0x40. It also fires on
 *               real SJIS tables (e.g. hiragana 82 A2 before a lead byte), so
 *               it only keeps driving ain_is_gb18030 as before.
 * boundary:     the same test at SJIS character boundaries only (first 100).
 * sjis_invalid: strings that are invalid SJIS but valid GBK (all strings).
 * nonascii, utf8_valid: strings with a byte >= 0x80, and how many of those
 *               are well-formed UTF-8.
 * Returns the strict verdict: boundary > 5, sjis_invalid >= 16 and fewer than
 * half of the non-ASCII strings are UTF-8. NULL entries are skipped.
 */
struct gb18030_scores {
	int legacy;
	int boundary;
	int sjis_invalid;
	int nonascii;
	int utf8_valid;
};
struct string;
bool gb18030_detect_strings(struct string **strs, int n, struct gb18030_scores *out);

// Turns on the Chinese text paths (ain_is_gb18030) and libsys4's GBK
// character rule for String/CharRef/CharAssign.
void gbk_string_rules_enable(void);

#endif /* XSYSTEM4_H */
