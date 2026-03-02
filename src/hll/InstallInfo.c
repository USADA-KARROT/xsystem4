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

#include "hll.h"

HLL_QUIET_UNIMPLEMENTED(1, int, InstallInfo, IsInstalledGame, struct string *game_name);
HLL_QUIET_UNIMPLEMENTED(0, int, InstallInfo, GetInstallMediaType, void);

HLL_LIBRARY(InstallInfo,
	    HLL_EXPORT(IsInstalledGame, InstallInfo_IsInstalledGame),
	    HLL_EXPORT(GetInstallMediaType, InstallInfo_GetInstallMediaType));
