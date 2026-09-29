/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSTEM4_FRAME_PACING_H
#define SYSTEM4_FRAME_PACING_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Frame pacing for games whose frames end in SystemService.UpdateView (v14).
 *
 * The original frame (AIN view::detail::View_Update): PartsEngine.Update-
 * Component advances the parts with the game's own passed time, then
 * ChipmunkSpriteEngine.Update (or TRANS_Update) draws the scene, then
 * SystemService.UpdateView presents it; both are skipped while the ADV engine
 * skips. UpdateView (SystemService case 6 -> 0x4c5450) does, in order:
 *   - Sleep(50) while the window is inactive, if
 *     ChipmunkSpriteEngine.SYSTEM_SetConfigSleepByInactiveWindow is on;
 *   - the 60 fps limiter 0x4676f0 (frame_pacing_limiter_step), or Sleep(1)
 *     when the limiter did not sleep;
 *   - Present with sync interval 0 (no vsync).
 * The limiter is off unless SYSTEM_SetConfigOverFrameRateSleep is on, and
 * does not run while read-message skipping with SYSTEM_SetConfigFrameSkip-
 * WhileMessageSkip on and SYSTEM_SetInvalidateFrameSkipWhileMessageSkip off.
 * In that message-skip state ChipmunkSpriteEngine.Update (0x468a10) draws one
 * frame in ten and restarts the limiter on the frames it does not draw.
 */

/* The original limiter period in milliseconds (float constant at 0x8132ec). */
#define FRAME_PACING_PERIOD_MS 16.666666f

/*
 * One step of the original limiter (0x4676f0). elapsed_ms is the timer
 * difference since the previous step (whole milliseconds, 32-bit wrap) and
 * *carry the value kept between steps. When carry + elapsed is below the
 * period, returns the whole milliseconds to sleep (the rest of the period,
 * truncated) and keeps the truncated fraction in *carry; otherwise returns -1
 * and resets *carry to 0.
 */
int frame_pacing_limiter_step(float *carry, uint32_t elapsed_ms);

/*
 * Sleep(ms) as the original gets it. It raises the timer resolution to 1 ms
 * (timeBeginPeriod, 0x41b360); a Windows sleep then ends on the first 1 ms
 * timer tick at or after its due time (presumed from the Windows timer
 * design, not measured on Windows), and the limiter's millisecond timer reads
 * that tick. Here the millisecond timer is SDL_GetTicks: the sleep returns
 * when it has advanced by ms + 1 (ms = 0 only yields). A plain sleep on macOS
 * can end several milliseconds late, so the last two are polled.
 */
void frame_pacing_sleep(uint32_t ms);

/* The limiter itself: flags, one step on the millisecond timer, the sleep.
 * Returns whether it slept (the original returns this to UpdateView). */
bool frame_pacing_limit(void);

/* The waits of UpdateView before the present (0x4c5450). */
void frame_pacing_wait(void);

/* ChipmunkSpriteEngine.Update: whether this frame is drawn. False for nine
 * frames in ten while message skipping (see above); those restart the
 * limiter. */
bool frame_pacing_draw_frame(void);

/* Present ownership. UpdateView calls frame_pacing_update_view() and presents
 * the frame. While frame_pacing_update_view_presents() is true, the calls
 * earlier in the frame (UpdateComponent, ChipmunkSpriteEngine.Update and
 * TRANS_Update) and system.Peek after it do not present. It is false before
 * the first UpdateView, after 500 ms without one, and when UpdateComponent
 * ran twice without an UpdateView in between (the ADV engine skipping): then
 * they present as before. */
void frame_pacing_update_view(void);
bool frame_pacing_update_view_presents(void);

/* PartsEngine.UpdateComponent advanced the parts for this frame. */
void frame_pacing_parts_updated(void);
/* UpdateView: whether UpdateComponent advanced the parts since the previous
 * UpdateView (clears the mark). UpdateView advances them only when not. */
bool frame_pacing_take_parts_updated(void);

/* What ChipmunkSpriteEngine.Update / TRANS_Update did with this frame. */
enum frame_pacing_frame {
	FRAME_PACING_NOT_DRAWN,
	FRAME_PACING_DRAWN,
	FRAME_PACING_SKIPPED,
};
void frame_pacing_set_frame(enum frame_pacing_frame state);
/* UpdateView: this frame's state (resets it to FRAME_PACING_NOT_DRAWN). */
enum frame_pacing_frame frame_pacing_take_frame(void);

#endif /* SYSTEM4_FRAME_PACING_H */
