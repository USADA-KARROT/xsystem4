/* Copyright (C) 2024 <nunuhara@haniwa.technology>
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

#include <SDL.h>
#include "system4/string.h"
#include "xsystem4.h"
#include "hll.h"

// GBK games convert to and from the UTF-8 clipboard; SJIS games keep the raw
// bytes as before.
static void Clipboard_SetText(struct string *text)
{
	if (!game_charset_is_gbk()) {
		SDL_SetClipboardText(text->text);
		return;
	}
	char *utf = game_to_utf8(text->text, text->size);
	SDL_SetClipboardText(utf);
	free(utf);
}

static struct string *Clipboard_GetText(void)
{
	char *text = SDL_GetClipboardText();
	if (game_charset_is_gbk() && text && *text) {
		char *gbk = utf8_to_game(text, 0);
		SDL_free(text);
		struct string *s = make_string(gbk, strlen(gbk));
		free(gbk);
		return s;
	}
	struct string *s = make_string(text ? text : "", text ? strlen(text) : 0);
	SDL_free(text);
	return s;
}

HLL_LIBRARY(Clipboard,
	    HLL_EXPORT(SetText, Clipboard_SetText),
	    HLL_EXPORT(GetText, Clipboard_GetText));
