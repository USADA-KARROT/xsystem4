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

#include <SDL.h>
#include "input.h"
#include "hll.h"

extern bool key_state[];

HLL_WARN_UNIMPLEMENTED(1, int,  AliceLogo4, Init, void *imainsystem);
HLL_WARN_UNIMPLEMENTED( , void, AliceLogo4, SetWaveNum, int n, int wave);

static void AliceLogo4_Run(int type, int loop_flag)
{
	// Wait up to 3 seconds or until user clicks
	uint32_t start = SDL_GetTicks();
	while (SDL_GetTicks() - start < 3000) {
		handle_events();
		if (key_is_down(VK_LBUTTON) || key_is_down(VK_RBUTTON))
			break;
		SDL_Delay(16);
	}
	// Signal completion: set LBUTTON down so the SceneLogo delegate's
	// Key_IsDown(1) check succeeds, allowing the scene to advance.
	key_state[VK_LBUTTON] = true;
}

HLL_LIBRARY(AliceLogo4,
	    HLL_EXPORT(Init, AliceLogo4_Init),
	    HLL_EXPORT(SetWaveNum, AliceLogo4_SetWaveNum),
	    HLL_EXPORT(Run, AliceLogo4_Run));
