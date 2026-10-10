/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSTEM4_MSGSKIP_H
#define SYSTEM4_MSGSKIP_H

#include <stdbool.h>

/*
 * The read-message table of the MsgSkip library (src/hll/MsgSkip.c), for
 * engine code that needs the answers the game gets from MsgSkip.GetFlag and
 * MsgSkip.GetAdvFlag. The original message window asks the same object
 * directly (the call at 0x4eeada to 0x4c7700).
 */
bool msgskip_get_flag(int msgnum);
bool msgskip_get_adv_flag(const char *name, int version, int step);

/*
 * Write the table to its file now and keep it loaded. Only for the original
 * file format (MsgSkip.msk); nothing happens otherwise. The original does
 * this as the first step of a reset (0x4c12e0).
 */
void msgskip_flush(void);

#endif /* SYSTEM4_MSGSKIP_H */
