/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

static void Confirm_Init(void *imainsystem) { }
static int Confirm_ExistKeyFile(struct string *filename, int keynum) { return 1; }
//int Confirm_CheckProtectFile(struct string *filename, int keynum);
//int Confirm_CreateKeyFile(struct string *filename, int keynum);

HLL_LIBRARY(Confirm,
			HLL_EXPORT(Init, Confirm_Init),
			HLL_EXPORT(ExistKeyFile, Confirm_ExistKeyFile)
			//HLL_EXPORT(CheckProtectFile, Confirm_CheckProtectFile),
			//HLL_EXPORT(CreateKeyFile, Confirm_CreateKeyFile)
			);
