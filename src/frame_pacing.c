/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <SDL.h>

#include "frame_pacing.h"
#include "gfx/gfx.h"
#include "sact.h"

/* UpdateView stops presenting after this long without a call (a game that
 * called it once and then drives frames some other way). Longer than any
 * single frame seen in practice, so a slow frame does not add a present. */
#define UPDATE_VIEW_TIMEOUT_NS 500000000ull

/* While message skipping, one frame in this many is drawn (0x467f51). */
#define MESSAGE_SKIP_DRAW_INTERVAL 10

/* Limiter state (0x4676f0: timer value +0xc, carried value +0x10). */
static uint32_t limiter_last_ms;
static float limiter_carry;
/* Message-skip frame counter (+0x58). */
static int message_skip_counter;

static uint64_t last_update_view_ns;
static unsigned parts_updates_since_update_view;
static bool parts_updated;
static enum frame_pacing_frame frame_state;

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void sleep_ns(uint64_t ns)
{
	struct timespec ts = { (time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull) };
	nanosleep(&ts, NULL);
}

void frame_pacing_sleep(uint32_t ms)
{
	if (!ms) {
		SDL_Delay(0);
		return;
	}
	// The due time is ms after this call; the first tick at or after it is
	// the (ms + 1)th tick of the millisecond timer from here.
	uint64_t target = SDL_GetTicks64() + ms + 1;
	for (;;) {
		uint64_t t = SDL_GetTicks64();
		if (t >= target)
			return;
		// Between target - t - 1 and target - t milliseconds remain. Sleep
		// half of that (macOS may wake a sleep late by a good part of its
		// length), then poll the last two milliseconds.
		if (target - t > 2)
			sleep_ns((target - t - 1) * 500000ull);
		else
			sleep_ns(200000ull);
	}
}

int frame_pacing_limiter_step(float *carry, uint32_t elapsed_ms)
{
	// Same float arithmetic as the original: the unsigned difference goes
	// through double to float, then the carried value is added.
	float used = (float)(double)elapsed_ms + *carry;
	if (!(FRAME_PACING_PERIOD_MS > used)) {
		*carry = 0.0f;
		return -1;
	}
	int sleep_ms = (int)(FRAME_PACING_PERIOD_MS - used);
	// What the truncation left of the period is carried into the next step.
	*carry = FRAME_PACING_PERIOD_MS - ((float)(double)(uint32_t)sleep_ms + used);
	return sleep_ms;
}

/* Read-message skipping with the frame skip configured and not invalidated
 * (0x4676f0 and 0x468a10 test the same three flags, +0x56 +0x55 +0x54). */
static bool message_skip_frame_skip(void)
{
	return !StoatSpriteEngine_SYSTEM_GetInvalidateFrameSkipWhileMessageSkip()
		&& StoatSpriteEngine_SYSTEM_GetConfigFrameSkipWhileMessageSkip()
		&& StoatSpriteEngine_SYSTEM_GetReadMessageSkipping();
}

bool frame_pacing_limit(void)
{
	if (!StoatSpriteEngine_SYSTEM_GetConfigOverFrameRateSleep())
		return false;
	if (message_skip_frame_skip())
		return false;
	int sleep_ms = frame_pacing_limiter_step(&limiter_carry, SDL_GetTicks() - limiter_last_ms);
	if (sleep_ms >= 0)
		frame_pacing_sleep(sleep_ms);
	limiter_last_ms = SDL_GetTicks();
	return sleep_ms >= 0;
}

void frame_pacing_wait(void)
{
	if (StoatSpriteEngine_SYSTEM_GetConfigSleepByInactiveWindow() && !gfx_window_active())
		frame_pacing_sleep(50);
	// The original presents without vsync. When xsystem4 runs with vsync,
	// the swap already waits for the display; limiting as well would wait
	// twice.
	if (gfx_vsync_active())
		return;
	if (!frame_pacing_limit())
		frame_pacing_sleep(1);
}

bool frame_pacing_draw_frame(void)
{
	if (!message_skip_frame_skip())
		return true;
	if (++message_skip_counter < MESSAGE_SKIP_DRAW_INTERVAL) {
		limiter_last_ms = SDL_GetTicks();
		limiter_carry = 0.0f;
		return false;
	}
	message_skip_counter = 0;
	return true;
}

void frame_pacing_update_view(void)
{
	last_update_view_ns = now_ns();
	parts_updates_since_update_view = 0;
}

bool frame_pacing_update_view_presents(void)
{
	if (!last_update_view_ns)
		return false;
	if (parts_updates_since_update_view > 1)
		return false;
	return now_ns() - last_update_view_ns < UPDATE_VIEW_TIMEOUT_NS;
}

void frame_pacing_parts_updated(void)
{
	parts_updated = true;
	if (parts_updates_since_update_view < 1000)
		parts_updates_since_update_view++;
}

bool frame_pacing_take_parts_updated(void)
{
	bool updated = parts_updated;
	parts_updated = false;
	return updated;
}

void frame_pacing_set_frame(enum frame_pacing_frame state)
{
	frame_state = state;
}

enum frame_pacing_frame frame_pacing_take_frame(void)
{
	enum frame_pacing_frame state = frame_state;
	frame_state = FRAME_PACING_NOT_DRAWN;
	return state;
}
