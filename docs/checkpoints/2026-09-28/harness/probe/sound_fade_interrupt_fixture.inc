/* sound-fade-interrupt: KiwiSoundEngine.Fade on a channel whose fade is still
 * running, through the AIN declarations (hll_call), the channel pool and the
 * mixer, on sound-gain's synthetic data (sound_gain_fixture.inc; constant mono
 * PCM16, so a level is the gain).
 *
 * Original evidence (static disassembly of the one CN executable, AIN v14 with
 * the GBK string rule; never run):
 *  - Fade (0x4a17fc -> 0x4a3a60) sets the channel's volume (+0x18) and calls
 *    0x4a5050(nTime, bStop, FadeType). There, while the old fade's thread is
 *    alive (WaitForSingleObject(+0x48, 0) == 0x102, 0x4a5082..0x4a509b), it
 *    first cancels it (0x4a5220: SetEvent, wait for the thread, close it);
 *    the thread then ends without the old target (0x4a53ea..0x4a53fc) and,
 *    when its OLD stop flag is set, stops the buffer (0x4a5344..0x4a5366,
 *    vtable +0x14). Only after that are the new time, target and stop flag
 *    written (0x4a50c3..0x4a50e1).
 *  - nTime below 1, negative too (a signed test, 0x4a518d `test esi, esi;
 *    jg`), is immediate: the target volume (vtable +0x1c, 0x4a51b4), then
 *    Stop when bStop (0x4a51d9); no thread is left, so IsFade (0x4a3b70, the
 *    thread is alive) is false at once. Above 0 a new thread runs the fade
 *    (0x4a51fc).
 *  - Fade starts the thread whether the buffer plays or not (0x4a51fc ->
 *    0x4a5380, which starts 0x4a52c0 -> 0x4a52e0). The thread runs by wall
 *    time (a timer object, 0x4a52e0, 0x4a53d0); it only sets and reads the
 *    buffer's volume and calls its Stop, it never asks whether the buffer
 *    plays. Stop (0x4a3700) only stops the buffer; no call to the cancel
 *    routine (0x4a5220) was found on the end-of-sound path. Play (0x4a3640)
 *    leaves a running thread alone and otherwise sets the volume again to
 *    the last Fade's target (0x4a11b0 reads +0x18, then 0x4a5050 with time
 *    0); StopFade (0x4a3b00) cancels the thread (0x4a5220).
 *
 * Expected values: the cases follow those readings, except the ones marked
 * port: the port's own behavior, kept on purpose and checked so that it does
 * not change unnoticed:
 *  - port: the new rule is only for AIN v14 with the GBK string rule (the
 *    one sample read); every other game keeps the legacy fade (v13, v15,
 *    v14 SJIS, the GB18030 flag with the SJIS rule), including its frame
 *    driven timing, its immediate stop that keeps the old fade, its negative
 *    time and its StopFade that jumps to the target.
 *  - port: a timed v14 fade begun while its channel plays advances with the
 *    mixed frames until the channel's stop fade has been interrupted once;
 *    from then on the channel's timed v14 fades run by wall time (as read in
 *    that sample, the original's fades always do). A timed v14 fade begun
 *    while its channel does not play, and a v14 fade that is still running
 *    when its channel is stopped or its sound ends, run by wall time as
 *    well; neither turns the channel's clock on.
 *  - port: the getters (IsPlay, IsFade, GetPos, and the C entries for the
 *    volume and the sample position) take the audio device lock only while
 *    a wall-time fade runs on the channel (SFI8).
 *  - port: Play does not set the volume again. After StopFade, which leaves
 *    the volume where the cancelled fade had it, a Play keeps that volume
 *    (SFI2b); the original's Play would set the last Fade's target again
 *    (0x4a3640 -> 0x4a11b0, +0x18 written by Fade at 0x4a3ad6).
 *  - port: on a v14 GBK channel, StopFade on a fade that advances with the
 *    mixed frames still jumps to the target at the next mixed chunk and
 *    stops the channel when the fade's stop flag is set. As read in that
 *    sample, the original's StopFade cancels the thread (0x4a3b00 ->
 *    0x4a5220), the volume stays where it is (0x4a53ea..0x4a53fc) and only
 *    the stop flag applies (0x4a5344..0x4a5366). No case checks this.
 *  - Not covered: a Stop after StopFade and before the next mixed chunk, on
 *    a fade that advances with the mixed frames (from a reading of the
 *    port's code, not checked here, IsFade then stays true).
 *
 * Cases (forked children): SFI1 a stop fade or a non-stop fade interrupted
 * by a timed, an immediate or an immediate stopping fade, in six version and
 * charset cases; SFI2 the timed replacement runs by wall time while the
 * channel is stopped (first Play after it, also after a stop fade, Play
 * during it, a third Fade, an explicit Stop, Unprepare and a new channel,
 * GetPos first, Seek first, StopFade); SFI3
 * a fade with time 0 ends the running fade first, then the target, then the
 * stop (a wall-time and a frame-driven old fade; nothing of the old one comes
 * back after a Stop and a Play); SFI4 a negative time is
 * immediate; SFI5 a fade left running by Stop or by the sound's end ends by
 * wall time, so a later Fade is no interruption, and a new fade right after
 * Stop keeps its own time; SFI6 the wall-time fade's gain is the
 * interpolated value, not the integer volume; SFI7 later fades of an
 * interrupted channel run by wall time (a Fade or StopFade after one ended
 * unobserved starts from its target), a never interrupted one's by frames;
 * SFI8 the getters without the lock; SFI9 legacy StopFade; SFI10 a fade kept
 * running by Stop goes on for the rest of its time, from the gain it had
 * reached, and does not turn the channel's clock on; SFI11 a timed fade begun
 * on a prepared channel before its Play (its time passes first, or Play comes
 * during it; a later Fade after its end is no interruption; a new fade while
 * it runs replaces it; a Stop during it does not start it again); SFI12 the C
 * entries the HLL does not reach (the volume and sample position getters
 * first after a wall-time fade's end, the legacy fade on a channel whose
 * clock is on).
 *
 * Timing: the cases that pull levels have no audio device and pull the
 * callback on this thread (sg_level); wall time is SDL's. Every check that a
 * fade has ended waits at least its time plus 60 ms; every check that one is
 * still running leaves at least 600 ms of slack, so a loaded machine only
 * makes the waits longer. SFI8 uses the dummy audio driver and its callback
 * thread, and a helper thread that holds the device lock for 400 ms.
 *
 * Not covered: FadeType curves, a group or streaming channel's last lap
 * interrupted and played again, a real audio device and the original's
 * output, contention with a real audio thread beyond SFI8, other v14 games.
 */
#include <SDL_thread.h>

static bool sfi_fade(int id, int ms, float volume, bool stop)
{
	stack_push(id); stack_push(ms); stack_push(volume); stack_push(stop); stack_push(0);
	return sg_call("Fade").i;
}

/* got is want x base within 3% */
#define SFI_NEAR(got, base, want) ((got) > (base) * (want) * 0.97f && (got) < (base) * (want) * 1.03f)

static const struct { const char *name; struct sg_opts opts; bool v14_gbk; } sfi_vers[] = {
	{ "v14 GBK, group table", { GN_DS_KEYS, 14, SG_GBK, false, false, false, NULL, false }, true },
	{ "v14 GBK, no group table", { GN_DS_NOTABLE, 14, SG_GBK, false, false, false, NULL, false }, true },
	{ "v13 GBK", { GN_DS_KEYS, 13, SG_GBK, false, false, false, NULL, false }, false },
	{ "v15 GBK", { GN_DS_KEYS, 15, SG_GBK, false, false, false, NULL, false }, false },
	{ "v14 SJIS", { GN_DS_KEYS, 14, SG_SJIS, false, false, false, NULL, false }, false },
	{ "v14 GB18030 flag only", { GN_DS_KEYS, 14, SG_FLAG_ONLY, false, false, false, NULL, false }, false },
};
static int sfi_ver;
#define SFI_V (sfi_vers[sfi_ver].name)

/* A playing, looping channel whose stop fade has been interrupted by a fade
 * with time 0 to 1.0 (stopped, volume 1.0), played again: from then on its
 * timed v14 fades run by wall time. Returns the level at 1.0. */
static float sfi_interrupted(int id)
{
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: cannot play %d", SFI_V, id);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 0, 1.0f, false), "%s: setup fades %d", SFI_V, id);
	SG_CHECK(!sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: the interrupted stop fade did not stop %d", SFI_V, id);
	SG_CHECK(sg_i1("Play", id), "%s: cannot play %d again", SFI_V, id);
	float full = sg_level();
	SG_CHECK(SFI_NEAR(full, initial, 1.0f), "%s: level at 1.0 after the interruption %.5f, before %.5f", SFI_V, full, initial);
	return full;
}

/* ---- SFI1 ---- */
static void sfi1_child(void)
{
	bool v14 = sfi_vers[sfi_ver].v14_gbk;
	sg_setup(&sfi_vers[sfi_ver].opts);
	for (int scenario = 0; scenario < 5; scenario++) {
		bool old_stop = scenario < 3;
		bool new_stop = scenario == 2;
		int new_ms = (scenario == 0 || scenario == 3) ? 100 : 0;
		/* original: the old stop flag stops the channel; port legacy: only the new one */
		bool expected_stop = new_stop || (v14 && old_stop);
		int id = 20 + scenario;
		SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: cannot play %d", SFI_V, id);
		float initial = sg_level();
		SG_CHECK(initial > 0 && sg_i1("IsPlay", id), "%s: no sound on %d", SFI_V, id);
		SG_CHECK(sfi_fade(id, 10000, 0.25f, old_stop), "%s: old fade %d", SFI_V, id);
		sg_level();
		SG_CHECK(sg_i1("IsFade", id), "%s: old fade not running on %d", SFI_V, id);
		SG_CHECK(sfi_fade(id, new_ms, 0.50f, new_stop), "%s: new fade %d", SFI_V, id);
		int playing = sg_i1("IsPlay", id);
		SG_CHECK(!!playing == !expected_stop, "%s: scenario %d IsPlay=%d, want %d", SFI_V, scenario, playing, !expected_stop);
		if (expected_stop) {
			SG_CHECK(sg_i1("GetPos", id) == 0, "%s: scenario %d stopped at GetPos %d, want 0", SFI_V, scenario, sg_i1("GetPos", id));
			if (v14 && old_stop) {
				SG_CHECK(sg_i1("Play", id), "%s: scenario %d cannot play again", SFI_V, scenario);
				if (new_ms)
					SDL_Delay(new_ms + 60);
				float got = sg_level();
				SG_CHECK(!sg_i1("IsFade", id), "%s: scenario %d fade still running", SFI_V, scenario);
				got = sg_level();
				SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: scenario %d target 0.5 on replay %.5f, base %.5f", SFI_V, scenario, got, initial);
			}
		} else if (!new_ms) {
			SG_CHECK(!sg_i1("IsFade", id), "%s: scenario %d immediate fade left the old one", SFI_V, scenario);
			float got = sg_level();
			SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: scenario %d immediate target %.5f, base %.5f", SFI_V, scenario, got, initial);
		} else {
			/* The timed replacement ends at its own target (0x4a53d0 sets
			 * +0x3c at its end, 0x4a5432; port: by the mixed frames here,
			 * the channel plays and its clock is off). */
			SDL_Delay(new_ms + 60);
			sg_level();
			float got = sg_level();
			SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.5f), "%s: scenario %d timed target %.5f, base %.5f, fading %d",
				 SFI_V, scenario, got, initial, sg_i1("IsFade", id));
		}
		gn_stop(id);
	}
}

/* ---- SFI2 (v14 GBK) ---- */
static void sfi2_stopped_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	for (int final_stop = 0; final_stop <= 1; final_stop++) {
		int id = 27 + final_stop;
		SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: cannot play %d", SFI_V, id);
		float initial = sg_level();
		SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 80, 0.50f, final_stop), "%s: fades %d", SFI_V, id);
		SG_CHECK(!sg_i1("IsPlay", id), "%s: the interrupted stop fade did not stop %d", SFI_V, id);
		SDL_Delay(160);
		SG_CHECK(!sg_i1("IsFade", id), "%s: the timed fade did not end while stopped (%d)", SFI_V, id);
		SG_CHECK(!sg_i1("IsPlay", id), "%s: %d played again without Play", SFI_V, id);
		SG_CHECK(sg_i1("Play", id), "%s: cannot play %d after the fade", SFI_V, id);
		float got = sg_level();
		SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: target 0.5 after the fade ended stopped %.5f, base %.5f", SFI_V, got, initial);
		gn_stop(id);
	}
	const int id = 29;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: first-Play setup", SFI_V);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 80, 0.50f, false), "%s: first-Play fades", SFI_V);
	SDL_Delay(160);
	SG_CHECK(sg_i1("Play", id), "%s: first Play after the fade's time", SFI_V);
	float got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.5f), "%s: first Play after the fade's time: fading %d, %.5f, base %.5f",
		 SFI_V, sg_i1("IsFade", id), got, initial);
	gn_stop(id);
	/* The same with a stop fade: it ended (and stopped) before Play, so Play
	 * plays at its target (0x4a3640: no thread, the volume set again). */
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: first-Play stop setup", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 80, 0.50f, true), "%s: first-Play stop fades", SFI_V);
	SDL_Delay(160);
	SG_CHECK(sg_i1("Play", id), "%s: first Play after the stop fade's time", SFI_V);
	SG_CHECK(sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: first Play after the stop fade's time was stopped by it", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: first Play after the stop fade's time: %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	for (int final_stop = 0; final_stop <= 1; final_stop++) {
		SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: Play-during setup", SFI_V);
		initial = sg_level();
		SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 1500, 0.50f, final_stop), "%s: Play-during fades", SFI_V);
		SDL_Delay(40);
		SG_CHECK(sg_i1("IsFade", id), "%s: the fade ended before Play", SFI_V);
		SG_CHECK(sg_i1("Play", id), "%s: Play during the fade", SFI_V);
		SDL_Delay(1700);
		SG_CHECK(!sg_i1("IsFade", id), "%s: Play during the fade: still fading", SFI_V);
		SG_CHECK(!!sg_i1("IsPlay", id) == !final_stop, "%s: Play during the fade: IsPlay %d with stop %d", SFI_V, sg_i1("IsPlay", id), final_stop);
		if (final_stop)
			SG_CHECK(sg_i1("Play", id), "%s: Play during the fade: cannot play again", SFI_V);
		got = sg_level();
		SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: Play during the fade: %.5f, base %.5f", SFI_V, got, initial);
		gn_stop(id);
	}
}

static void sfi2_more_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	const int id = 30;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: third-Fade setup", SFI_V);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 1500, 0.30f, false), "%s: third-Fade fades", SFI_V);
	SDL_Delay(40);
	SG_CHECK(sfi_fade(id, 80, 0.75f, false), "%s: third Fade", SFI_V);
	SDL_Delay(160);
	SG_CHECK(!sg_i1("IsFade", id) && !sg_i1("IsPlay", id), "%s: third Fade did not end stopped", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: third Fade: cannot play again", SFI_V);
	float got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.75f), "%s: third Fade target %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* An explicit Stop does not end the fade (0x4a3700). */
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: Stop setup", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 100, 0.50f, false), "%s: Stop fades", SFI_V);
	SDL_Delay(25);
	SG_CHECK(sg_i1("Play", id) && sg_i1("Stop", id), "%s: Play and Stop", SFI_V);
	SDL_Delay(160);
	SG_CHECK(!sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: after Stop and the fade's time: IsPlay %d IsFade %d",
		 SFI_V, sg_i1("IsPlay", id), sg_i1("IsFade", id));
	SG_CHECK(sg_i1("Play", id), "%s: Stop: cannot play again", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: Stop: target %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* port: Unprepare closes the channel; a new one on the ID is not touched by the old fade. */
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: Unprepare setup", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 80, 0.25f, true), "%s: Unprepare fades", SFI_V);
	SG_CHECK(sg_i1("Unprepare", id) && sg_play_loop(id, "gn_g2", 0), "%s: Unprepare and prepare again", SFI_V);
	SDL_Delay(160);
	SG_CHECK(sg_i1("IsPlay", id), "%s: the old fade stopped the new channel", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 1.0f), "%s: the old fade changed the new channel %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* GetPos is the first call after the fade's time. */
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: GetPos setup", SFI_V);
	sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 80, 0.50f, true), "%s: GetPos fades", SFI_V);
	SDL_Delay(10);
	SG_CHECK(sg_i1("Play", id), "%s: GetPos Play", SFI_V);
	sg_level();
	SDL_Delay(160);
	SG_CHECK(sg_i1("GetPos", id) == 0, "%s: GetPos first after the fade's time did not see the stop", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: GetPos did not settle the fade", SFI_V);
	gn_stop(id);
	/* Seek is the first call after the end of a stop fade during play: the
	 * fade's stop comes first, then the seek (port: Stop rewinds; the
	 * position is the one sought). */
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: Seek setup", SFI_V);
	sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 80, 0.50f, true), "%s: Seek fades", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: Seek Play", SFI_V);
	SDL_Delay(160);
	SG_CHECK(sg_i2("Seek", id, 50), "%s: Seek", SFI_V);
	int pos = sg_i1("GetPos", id);
	SG_CHECK(!sg_i1("IsPlay", id) && pos >= 49 && pos <= 51, "%s: Seek after the stop fade's end: IsPlay %d GetPos %d, want stopped at 50",
		 SFI_V, sg_i1("IsPlay", id), pos);
	gn_stop(id);
	/* StopFade cancels: the volume stays where it is (the cancelled thread
	 * does not set its target, 0x4a53ea..0x4a53fc); the stop flag applies
	 * (0x4a3b00 -> 0x4a5220). port: with the stop flag, the level after the
	 * Play that follows is the port's own: its Play keeps the volume, while
	 * the original's Play sets the last Fade's target (0.25 here) again
	 * when no thread runs (0x4a3640 -> 0x4a11b0 reads +0x18, 0x4a3ad6). */
	for (int final_stop = 0; final_stop <= 1; final_stop++) {
		SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: StopFade setup", SFI_V);
		initial = sg_level();
		SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 10000, 0.25f, final_stop), "%s: StopFade fades", SFI_V);
		SG_CHECK(sg_i1("Play", id), "%s: StopFade Play", SFI_V);
		SDL_Delay(40);
		SG_CHECK(sg_i1("StopFade", id), "%s: StopFade", SFI_V);
		SG_CHECK(!sg_i1("IsFade", id) && !!sg_i1("IsPlay", id) == !final_stop, "%s: StopFade state with stop %d", SFI_V, final_stop);
		if (final_stop)
			SG_CHECK(sg_i1("Play", id), "%s: StopFade: cannot play again", SFI_V);
		got = sg_level();
		SG_CHECK(got > initial * 0.94f && got < initial * 1.02f, "%s: %sStopFade went to the target %.5f, base %.5f", SFI_V,
			 final_stop ? "port: Play after " : "", got, initial);
		gn_stop(id);
	}
}

/* ---- SFI3 (v14 GBK): time 0 ends the running fade, then the target, then the stop ---- */
static void sfi3_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	/* a: a wall-time non-stop fade, then time 0 with stop */
	int id = 31;
	float full = sfi_interrupted(id);
	SG_CHECK(sfi_fade(id, 3000, 0.30f, false), "%s: a: wall-time fade", SFI_V);
	SDL_Delay(20);
	SG_CHECK(sg_i1("IsFade", id), "%s: a: the 3 s fade is not running", SFI_V);
	SG_CHECK(sfi_fade(id, 0, 0.60f, true), "%s: a: time 0 with stop", SFI_V);
	SG_CHECK(!sg_i1("IsFade", id), "%s: a: time 0 with stop left the wall-time fade running", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id), "%s: a: time 0 with stop did not stop", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: a: cannot play again", SFI_V);
	float got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, full, 0.6f), "%s: a: replay at the new target: %.5f, base %.5f, fading %d",
		 SFI_V, got, full, sg_i1("IsFade", id));
	/* d: a wall-time non-stop fade, then time 0 without stop */
	SG_CHECK(sfi_fade(id, 3000, 0.30f, false), "%s: d: wall-time fade", SFI_V);
	SG_CHECK(sfi_fade(id, 0, 0.80f, false), "%s: d: time 0", SFI_V);
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && sg_i1("IsPlay", id) && SFI_NEAR(got, full, 0.8f), "%s: d: time 0 during a wall-time fade: %.5f, base %.5f",
		 SFI_V, got, full);
	gn_stop(id);
	/* b: a frame-driven non-stop fade (never interrupted channel), then time 0 with stop */
	id = 32;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: b: cannot play", SFI_V);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, 3000, 0.30f, false), "%s: b: fade", SFI_V);
	sg_level();
	SG_CHECK(sg_i1("IsFade", id), "%s: b: the fade is not running", SFI_V);
	SG_CHECK(sfi_fade(id, 0, 0.60f, true), "%s: b: time 0 with stop", SFI_V);
	SG_CHECK(!sg_i1("IsFade", id), "%s: b: time 0 with stop left the old fade running", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id), "%s: b: time 0 with stop did not stop", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: b: cannot play again", SFI_V);
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.6f), "%s: b: replay at the new target: %.5f, base %.5f, fading %d",
		 SFI_V, got, initial, sg_i1("IsFade", id));
	/* Nothing runs after time 0: a Stop and a Play keep the target (Stop,
	 * 0x4a3700, only stops; Play, 0x4a3640, sets +0x18, 0.6, again; the
	 * port's keeps it), and no ended fade comes back. */
	SG_CHECK(sg_i1("Stop", id) && sg_i1("Play", id), "%s: b: Stop and Play", SFI_V);
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.6f), "%s: b: Stop and Play after time 0: %.5f, base %.5f, fading %d",
		 SFI_V, got, initial, sg_i1("IsFade", id));
	gn_stop(id);
	/* c: a frame-driven non-stop fade, then time 0 without stop */
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: c: cannot play", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 3000, 0.30f, false), "%s: c: fade", SFI_V);
	sg_level();
	SG_CHECK(sfi_fade(id, 0, 0.60f, false), "%s: c: time 0", SFI_V);
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && sg_i1("IsPlay", id) && SFI_NEAR(got, initial, 0.6f), "%s: c: time 0 during a fade: %.5f, base %.5f",
		 SFI_V, got, initial);
	gn_stop(id);
}

/* ---- SFI4 (v14 GBK): a negative time is immediate ---- */
static void sfi4_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	int id = 33;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: cannot play", SFI_V);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, -5, 0.50f, false), "%s: Fade(-5)", SFI_V);
	SG_CHECK(!sg_i1("IsFade", id) && sg_i1("IsPlay", id), "%s: Fade(-5) is not immediate: IsFade %d IsPlay %d",
		 SFI_V, sg_i1("IsFade", id), sg_i1("IsPlay", id));
	float got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: Fade(-5) target %.5f, base %.5f", SFI_V, got, initial);
	SG_CHECK(sfi_fade(id, -1, 0.30f, true), "%s: Fade(-1, stop)", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: Fade(-1, stop) is not an immediate stop: IsPlay %d IsFade %d",
		 SFI_V, sg_i1("IsPlay", id), sg_i1("IsFade", id));
	SG_CHECK(sg_i1("Play", id), "%s: cannot play again", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.3f), "%s: Fade(-1, stop) target %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* A negative time interrupting a stop fade: stopped, no fade left. */
	id = 34;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: interrupt: cannot play", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, -100, 0.40f, false), "%s: interrupt: fades", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: interrupt by Fade(-100): IsPlay %d IsFade %d",
		 SFI_V, sg_i1("IsPlay", id), sg_i1("IsFade", id));
	SG_CHECK(sg_i1("Play", id), "%s: interrupt: cannot play again", SFI_V);
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.4f), "%s: interrupt: target %.5f, base %.5f", SFI_V, got, initial);
	/* The channel's clock is on now; a negative time is still immediate. */
	SG_CHECK(sfi_fade(id, -7, 0.70f, false), "%s: Fade(-7) on a wall-time channel", SFI_V);
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.7f), "%s: Fade(-7) on a wall-time channel: %.5f, base %.5f, fading %d",
		 SFI_V, got, initial, sg_i1("IsFade", id));
	gn_stop(id);
}

/* ---- SFI5 (v14 GBK): a fade left running by Stop or by the sound's end ---- */
static void sfi5_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	/* a: Stop during a frame-driven stop fade; its time passes; Play; Fade */
	int id = 35;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: a: cannot play", SFI_V);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, 400, 0.25f, true), "%s: a: stop fade", SFI_V);
	sg_level();
	SG_CHECK(sg_i1("IsFade", id) && sg_i1("IsPlay", id), "%s: a: the stop fade is not running", SFI_V);
	SG_CHECK(sg_i1("Stop", id) && !sg_i1("IsPlay", id), "%s: a: Stop", SFI_V);
	SDL_Delay(460);
	SG_CHECK(!sg_i1("IsFade", id), "%s: a: the fade did not end by time after Stop", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id), "%s: a: played again without Play", SFI_V);
	SG_CHECK(sg_i1("Play", id) && sg_i1("IsPlay", id), "%s: a: cannot play again", SFI_V);
	float got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.25f), "%s: a: replay at the fade's target %.5f, base %.5f", SFI_V, got, initial);
	SG_CHECK(sfi_fade(id, 0, 0.50f, false), "%s: a: Fade after the replay", SFI_V);
	SG_CHECK(sg_i1("IsPlay", id), "%s: a: a Fade after the ended fade was taken for an interruption", SFI_V);
	got = sg_level();
	SG_CHECK(sg_i1("IsPlay", id) && SFI_NEAR(got, initial, 0.5f), "%s: a: level after the Fade %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* b: Stop during a stop fade, Play before its end: its end stops the channel (0x4a5344..0x4a5366) */
	id = 36;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: b: cannot play", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 1500, 0.25f, true), "%s: b: stop fade", SFI_V);
	sg_level();
	SG_CHECK(sg_i1("Stop", id), "%s: b: Stop", SFI_V);
	SDL_Delay(50);
	SG_CHECK(sg_i1("Play", id) && sg_i1("IsPlay", id), "%s: b: Play during the fade", SFI_V);
	SG_CHECK(sg_i1("IsFade", id), "%s: b: the fade ended early", SFI_V);
	SDL_Delay(1600);
	sg_level();
	SG_CHECK(!sg_i1("IsPlay", id), "%s: b: the fade's end did not stop the channel played again", SFI_V);
	SG_CHECK(!sg_i1("IsFade", id), "%s: b: still fading after the fade's time", SFI_V);
	gn_stop(id);
	/* c: the sound ends during a stop fade; its time passes; Play; Fade */
	id = 37;
	SG_CHECK(sg_play_loop(id, "gn_g2", 1), "%s: c: cannot play once", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 1000, 0.25f, true), "%s: c: stop fade", SFI_V);
	sg_level();
	sg_level();
	SG_CHECK(!sg_i1("IsPlay", id), "%s: c: the sound did not end", SFI_V);
	SDL_Delay(1060);
	SG_CHECK(!sg_i1("IsFade", id), "%s: c: the fade did not end by time after the sound's end", SFI_V);
	SG_CHECK(sg_i2("SetLoopCount", id, 0) && sg_i1("Play", id) && sg_i1("IsPlay", id), "%s: c: cannot play again", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.25f), "%s: c: replay at the fade's target %.5f, base %.5f", SFI_V, got, initial);
	SG_CHECK(sfi_fade(id, 0, 0.50f, false), "%s: c: Fade after the replay", SFI_V);
	SG_CHECK(sg_i1("IsPlay", id), "%s: c: a Fade after the ended fade was taken for an interruption", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: c: level after the Fade %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* d: Stop during a non-stop fade, then a new 2 s fade at once: the new
	 * one is still running 400 ms later (the original's new thread runs its
	 * own 2 s; port: begun on a stopped channel, it runs its 2 s by wall
	 * time), not ended with the old one's time. */
	id = 43;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: d: cannot play", SFI_V);
	sg_level();
	SG_CHECK(sfi_fade(id, 300, 0.25f, false), "%s: d: fade", SFI_V);
	sg_level();
	SG_CHECK(sg_i1("Stop", id) && sfi_fade(id, 2000, 0.75f, false), "%s: d: Stop and new fade", SFI_V);
	SDL_Delay(400);
	SG_CHECK(sg_i1("IsFade", id), "%s: d: the new fade ended with the old fade's time", SFI_V);
	gn_stop(id);
}

/* ---- SFI6 (v14 GBK): the wall-time fade's gain is the interpolated value ---- */
static void sfi6_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	const int id = 38;
	float full = sfi_interrupted(id);
	/* From 1.0 to 0.99 over 2 s: an integer volume would read 99 (0.99)
	 * as soon as the fade begins. */
	SG_CHECK(sfi_fade(id, 2000, 0.99f, false), "%s: 2 s fade to 0.99", SFI_V);
	SDL_Delay(500);
	float got = sg_level();
	float r = got / full;
	printf("  %s: 500 ms into a 2 s fade from 1.0 to 0.99: %.6f of full\n", SFI_V, r);
	SG_CHECK(sg_i1("IsFade", id) && r > 0.9902f && r < 0.999f, "%s: gain 500 ms into a 2 s fade from 1.0 to 0.99: %.6f of full, want within (0.9902, 0.999)",
		 SFI_V, r);
	gn_stop(id);
}

/* ---- SFI7 (v14 GBK): later fades of an interrupted channel; a never interrupted one ---- */
static void sfi7_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	int id = 39;
	float full = sfi_interrupted(id);
	SG_CHECK(sfi_fade(id, 80, 0.50f, false), "%s: later fade", SFI_V);
	SDL_Delay(200);
	SG_CHECK(!sg_i1("IsFade", id) && sg_i1("IsPlay", id), "%s: a later fade of an interrupted channel did not run by wall time", SFI_V);
	float got = sg_level();
	SG_CHECK(SFI_NEAR(got, full, 0.5f), "%s: later fade target %.5f, base %.5f", SFI_V, got, full);
	SG_CHECK(sfi_fade(id, 80, 0.70f, true), "%s: later stop fade", SFI_V);
	SDL_Delay(200);
	SG_CHECK(!sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: a later stop fade did not stop by wall time", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: cannot play again", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, full, 0.7f), "%s: later stop fade target %.5f, base %.5f", SFI_V, got, full);
	/* A fade that ended unobserved: the next Fade starts from its target
	 * (0x4a52e0 reads the volume the old thread left), and so does StopFade
	 * (0x4a3b00: nothing to cancel). */
	SG_CHECK(sfi_fade(id, 80, 0.30f, false), "%s: unobserved fade", SFI_V);
	SDL_Delay(200);
	SG_CHECK(sfi_fade(id, 4000, 0.30f, false), "%s: fade after an unobserved end", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, full, 0.3f), "%s: a fade begun after an unobserved end did not start from its target: %.5f, base %.5f", SFI_V, got, full);
	SG_CHECK(sfi_fade(id, 0, 1.0f, false) && sfi_fade(id, 80, 0.40f, false), "%s: second unobserved fade", SFI_V);
	sg_level();
	SDL_Delay(200);
	SG_CHECK(sg_i1("StopFade", id), "%s: StopFade after an unobserved end", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, full, 0.4f), "%s: StopFade after an unobserved end left %.5f, base %.5f (want the target)", SFI_V, got, full);
	gn_stop(id);
	/* port: a never interrupted channel's fade advances with the mixed frames */
	id = 40;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: frames: cannot play", SFI_V);
	sg_level();
	SG_CHECK(sfi_fade(id, 80, 0.50f, false), "%s: frames: fade", SFI_V);
	SDL_Delay(200);
	SG_CHECK(sg_i1("IsFade", id), "%s: port: a never interrupted channel's fade ran without frames", SFI_V);
	sg_level();
	SG_CHECK(!sg_i1("IsFade", id), "%s: frames: 5 chunks did not end an 80 ms fade", SFI_V);
	gn_stop(id);
}

/* ---- SFI8 (v14 GBK, dummy driver): the getters without the lock ---- */
static SDL_atomic_t sfi8_state;
static SDL_AudioDeviceID sfi8_dev;
static int sfi8_hold(void *data)
{
	(void)data;
	SDL_LockAudioDevice(sfi8_dev);
	SDL_AtomicSet(&sfi8_state, 1);
	SDL_Delay(400);
	SDL_UnlockAudioDevice(sfi8_dev);
	return 0;
}
/* ms one getter call takes while another thread holds the device lock for
 * 400 ms: an HLL function by name, or (fn NULL) a C entry */
static int sfi8_blocked_by(const char *fn, int (*c_entry)(int), int id)
{
	SDL_AtomicSet(&sfi8_state, 0);
	SDL_Thread *t = SDL_CreateThread(sfi8_hold, "sfi8", NULL);
	if (!t)
		return -1;
	while (!SDL_AtomicGet(&sfi8_state))
		SDL_Delay(1);
	uint64_t t0 = SDL_GetTicks64();
	if (fn)
		sg_i1(fn, id);
	else
		c_entry(id);
	int ms = (int)(SDL_GetTicks64() - t0);
	SDL_WaitThread(t, NULL);
	return ms;
}
static int sfi8_blocked(const char *fn, int id) { return sfi8_blocked_by(fn, NULL, id); }
static void sfi8_child(void)
{
	struct sg_opts o = sfi_vers[sfi_ver].opts;
	o.dummy_audio = true;
	sg_setup(&o);
	sfi8_dev = 0;
	for (SDL_AudioDeviceID d = 1; d < 64 && !sfi8_dev; d++)
		if (SDL_GetAudioDeviceStatus(d) == SDL_AUDIO_PLAYING)
			sfi8_dev = d;
	SG_CHECK(sfi8_dev, "%s: no running audio device", SFI_V);
	if (!sfi8_dev)
		return;
	const int id = 41;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: cannot play", SFI_V);
	static const char *const getters[] = { "IsPlay", "IsFade", "GetPos" };
	for (int i = 0; i < SG_COUNT(getters); i++) {
		int ms = sfi8_blocked(getters[i], id);
		printf("  %s: %s waited %d ms while the device lock was held (no wall-time fade)\n", SFI_V, getters[i], ms);
		SG_CHECK(ms >= 0 && ms < 200, "%s: %s without a wall-time fade waited %d ms for the device lock", SFI_V, getters[i], ms);
	}
	/* the C getters of the volume and the sample position, which the HLL does not have */
	static const struct { const char *name; int (*fn)(int); } c_getters[] = {
		{ "wav_get_volume", wav_get_volume }, { "wav_get_sample_pos", wav_get_sample_pos },
	};
	for (int i = 0; i < SG_COUNT(c_getters); i++) {
		int ms = sfi8_blocked_by(NULL, c_getters[i].fn, id);
		printf("  %s: %s waited %d ms while the device lock was held (no wall-time fade)\n", SFI_V, c_getters[i].name, ms);
		SG_CHECK(ms >= 0 && ms < 200, "%s: %s without a wall-time fade waited %d ms for the device lock", SFI_V, c_getters[i].name, ms);
	}
	/* a wall-time fade: the getters take the lock (this also shows the lock held is the mixer's) */
	SG_CHECK(sfi_fade(id, 10000, 0.25f, true) && sfi_fade(id, 0, 1.0f, false) && sg_i1("Play", id)
		 && sfi_fade(id, 5000, 0.5f, false), "%s: wall-time fade setup", SFI_V);
	int ms = sfi8_blocked("IsPlay", id);
	printf("  %s: IsPlay waited %d ms while the device lock was held (wall-time fade)\n", SFI_V, ms);
	SG_CHECK(ms >= 200, "%s: IsPlay during a wall-time fade took %d ms, want the lock (>= 200)", SFI_V, ms);
	/* no sg_level here: the callback thread runs */
	sg_i1("Stop", id);
	sg_i1("Unprepare", id);
}

/* ---- SFI9 (legacy, v13 GBK): StopFade unchanged ---- */
static void sfi9_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	const int id = 42;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: cannot play", SFI_V);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, 10000, 0.25f, false), "%s: fade", SFI_V);
	sg_level();
	SG_CHECK(sg_i1("StopFade", id), "%s: StopFade", SFI_V);
	float got = sg_level();
	/* port: the legacy StopFade lets the callback jump to the target */
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.25f), "%s: port: legacy StopFade %.5f, base %.5f", SFI_V, got, initial);
	/* port: legacy immediate stop keeps the old fade; legacy negative time is a fade */
	SG_CHECK(sfi_fade(id, 3000, 0.5f, false) && sfi_fade(id, 0, 0.6f, true), "%s: legacy time 0 stop", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id) && sg_i1("IsFade", id), "%s: port: legacy time 0 stop: IsPlay %d IsFade %d", SFI_V,
		 sg_i1("IsPlay", id), sg_i1("IsFade", id));
	gn_stop(id);
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: negative: cannot play", SFI_V);
	SG_CHECK(sfi_fade(id, -5, 0.5f, false), "%s: legacy Fade(-5)", SFI_V);
	SG_CHECK(sg_i1("IsPlay", id) && sg_i1("IsFade", id), "%s: port: legacy Fade(-5): IsPlay %d IsFade %d", SFI_V,
		 sg_i1("IsPlay", id), sg_i1("IsFade", id));
	gn_stop(id);
	/* port: legacy Stop during a stop fade keeps the fade where it is */
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: Stop: cannot play", SFI_V);
	SG_CHECK(sfi_fade(id, 400, 0.25f, true), "%s: Stop: fade", SFI_V);
	sg_level();
	SG_CHECK(sg_i1("Stop", id), "%s: Stop", SFI_V);
	SDL_Delay(460);
	SG_CHECK(sg_i1("IsFade", id), "%s: port: a legacy fade ran on after Stop", SFI_V);
	gn_stop(id);
}

/* Mix n chunks on this thread without measuring. */
static void sfi_mix(int chunks)
{
	static float buf[SG_CHUNK * 2];
	for (int i = 0; i < chunks; i++)
		sg_pull(buf, SG_CHUNK);
}

/* ---- SFI10 (v14 GBK): a fade kept running by Stop ---- */
static void sfi10_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	/* A 3 s fade from 1.0 to 0.25 on a never interrupted channel, 86 chunks
	 * of it mixed (about 1997 ms of 3000; port: by the mixed frames), then
	 * Stop. port: the rest of it, at most 1027 ms with one chunk either way,
	 * runs by wall time from the gain reached (the original's thread just
	 * goes on by its own wall time, 0x4a53d0; here the frames mixed so far
	 * are the progress). The times above and the gain p that b expects
	 * depend on SG_CHUNK (1024 frames a pull, one refill per pull) and on
	 * the synthetic data's 44.1 kHz; change either and p must change too
	 * (one chunk moves the gain by about 1.2% of the level b expects; the
	 * check allows 3%). */
	const float p = 86.0f * SG_CHUNK / (3.0f * 44100.0f);
	/* a: the rest of the time, not the whole; the clock stays off */
	int id = 44;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: a: cannot play", SFI_V);
	float initial = sg_level();
	SG_CHECK(sfi_fade(id, 3000, 0.25f, false), "%s: a: fade", SFI_V);
	sfi_mix(86);
	SG_CHECK(sg_i1("Stop", id) && sg_i1("IsFade", id), "%s: a: port: the fade did not go on after Stop", SFI_V);
	SDL_Delay(1100);
	SG_CHECK(!sg_i1("IsFade", id), "%s: a: port: still fading 1100 ms after Stop (the whole time, not the rest)", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: a: cannot play again", SFI_V);
	float got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.25f), "%s: a: replay at the target %.5f, base %.5f", SFI_V, got, initial);
	SG_CHECK(sfi_fade(id, 80, 0.50f, false), "%s: a: later fade", SFI_V);
	SDL_Delay(200);
	SG_CHECK(sg_i1("IsFade", id), "%s: a: port: the kept fade turned the channel's clock on (a later fade ran without frames)", SFI_V);
	sg_level();
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.5f), "%s: a: later fade target %.5f, base %.5f, fading %d",
		 SFI_V, got, initial, sg_i1("IsFade", id));
	gn_stop(id);
	/* b: the start is the gain reached: Play at once after Stop */
	id = 45;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: b: cannot play", SFI_V);
	initial = sg_level();
	SG_CHECK(sfi_fade(id, 3000, 0.25f, false), "%s: b: fade", SFI_V);
	sfi_mix(86);
	SG_CHECK(sg_i1("Stop", id) && sg_i1("Play", id), "%s: b: Stop and Play", SFI_V);
	got = sg_level();
	float want = 1.0f + (0.25f - 1.0f) * p;
	SG_CHECK(sg_i1("IsFade", id) && SFI_NEAR(got, initial, want), "%s: b: port: Play right after Stop at %.5f, base %.5f, want %.4f of it (the gain reached), fading %d",
		 SFI_V, got, initial, want, sg_i1("IsFade", id));
	gn_stop(id);
}

/* ---- SFI11 (v14 GBK): a timed fade begun on a prepared channel before Play ---- */
static bool sfi_prepare(int id)
{
	return sg_prepare(id, "gn_g2", false) && sg_i2("SetLoopCount", id, 0);
}
static void sfi11_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	int id = 46;
	SG_CHECK(sg_play_loop(id, "gn_g2", 0), "%s: cannot play", SFI_V);
	float initial = sg_level();
	gn_stop(id);
	/* a: its time passes before Play. Fade starts the thread on the buffer
	 * that does not play (0x4a51fc -> 0x4a5380), the thread ends by wall
	 * time (0x4a53d0) and its Stop (0x4a5344..0x4a5366) comes while nothing
	 * plays (what that Stop does to a buffer that does not play was not
	 * traced further); Play then finds no thread and sets the target again
	 * (0x4a3640 -> 0x4a11b0), and no fade is left to stop it. */
	SG_CHECK(sfi_prepare(id), "%s: a: cannot prepare", SFI_V);
	SG_CHECK(sfi_fade(id, 1000, 0.25f, true), "%s: a: fade before Play", SFI_V);
	SG_CHECK(sg_i1("IsFade", id) && !sg_i1("IsPlay", id), "%s: a: the fade is not running before Play", SFI_V);
	SDL_Delay(1060);
	SG_CHECK(!sg_i1("IsFade", id), "%s: a: the fade begun before Play did not end by time", SFI_V);
	SG_CHECK(!sg_i1("IsPlay", id), "%s: a: played without Play", SFI_V);
	SG_CHECK(sg_i1("Play", id) && sg_i1("IsPlay", id), "%s: a: cannot play", SFI_V);
	float got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.25f), "%s: a: Play after the fade's time at %.5f, base %.5f (want the target)", SFI_V, got, initial);
	sfi_mix(55); /* some 1277 ms of frames, more than the fade's 1000 */
	SG_CHECK(sg_i1("IsPlay", id) && !sg_i1("IsFade", id), "%s: a: the ended stop fade stopped the channel played after it: IsPlay %d IsFade %d",
		 SFI_V, sg_i1("IsPlay", id), sg_i1("IsFade", id));
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.25f), "%s: a: level after more frames %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* b: Play during it: it goes on by its time and its end stops the
	 * channel when it was a stop fade (0x4a5344..0x4a5366) */
	for (int stop = 0; stop <= 1; stop++) {
		SG_CHECK(sfi_prepare(id), "%s: b: cannot prepare", SFI_V);
		SG_CHECK(sfi_fade(id, 1500, 0.50f, stop), "%s: b: fade before Play", SFI_V);
		SDL_Delay(40);
		SG_CHECK(sg_i1("Play", id) && sg_i1("IsPlay", id) && sg_i1("IsFade", id), "%s: b: Play during the fade (stop %d)", SFI_V, stop);
		SDL_Delay(1560);
		sg_level();
		SG_CHECK(!sg_i1("IsFade", id), "%s: b: still fading after the fade's time (stop %d)", SFI_V, stop);
		SG_CHECK(!!sg_i1("IsPlay", id) == !stop, "%s: b: IsPlay %d after the fade's end with stop %d", SFI_V, sg_i1("IsPlay", id), stop);
		if (stop)
			SG_CHECK(sg_i1("Play", id), "%s: b: cannot play again", SFI_V);
		got = sg_level();
		SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: b: target %.5f, base %.5f (stop %d)", SFI_V, got, initial, stop);
		gn_stop(id);
	}
	/* c: after its time, a Fade on the channel that still does not play is
	 * no interruption (0x4a5082..0x4a509b: only a live thread is cancelled);
	 * port: the channel's clock stays off, so a fade begun later while it
	 * plays advances with the mixed frames. */
	SG_CHECK(sfi_prepare(id), "%s: c: cannot prepare", SFI_V);
	SG_CHECK(sfi_fade(id, 300, 0.25f, true), "%s: c: stop fade before Play", SFI_V);
	SDL_Delay(360);
	SG_CHECK(sfi_fade(id, 0, 1.0f, false), "%s: c: Fade after its time", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: c: cannot play", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 1.0f), "%s: c: level %.5f, base %.5f", SFI_V, got, initial);
	SG_CHECK(sfi_fade(id, 80, 0.50f, false), "%s: c: later fade", SFI_V);
	SDL_Delay(200);
	SG_CHECK(sg_i1("IsFade", id), "%s: c: port: taken for an interruption (a later fade ran without frames)", SFI_V);
	sg_level();
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.5f), "%s: c: later fade target %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* d: the same, the Fade after Play: nothing stops the channel (no live
	 * thread, so no old stop flag; 0x4a5082..0x4a509b) and the clock stays
	 * off (port) */
	SG_CHECK(sfi_prepare(id), "%s: d: cannot prepare", SFI_V);
	SG_CHECK(sfi_fade(id, 300, 0.25f, true), "%s: d: stop fade before Play", SFI_V);
	SDL_Delay(360);
	SG_CHECK(sg_i1("Play", id) && sfi_fade(id, 80, 0.50f, false), "%s: d: Play and Fade", SFI_V);
	SG_CHECK(sg_i1("IsPlay", id), "%s: d: a Fade after the ended stop fade was taken for an interruption", SFI_V);
	SDL_Delay(200);
	SG_CHECK(sg_i1("IsFade", id), "%s: d: port: the clock was turned on (the fade ran without frames)", SFI_V);
	sg_level();
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && sg_i1("IsPlay", id) && SFI_NEAR(got, initial, 0.5f), "%s: d: fade target %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
	/* e: a non-stop fade begun before Play, then Play and a new fade while
	 * it runs: the new one replaces it with its own time and target (the
	 * old thread is cancelled, 0x4a5082..0x4a509b, the new one runs its
	 * own, 0x4a53d0; port: by the mixed frames, the channel plays and its
	 * clock is off), nothing of the old one is left. */
	SG_CHECK(sfi_prepare(id), "%s: e: cannot prepare", SFI_V);
	SG_CHECK(sfi_fade(id, 3000, 0.25f, false) && sg_i1("Play", id) && sfi_fade(id, 100, 0.75f, false), "%s: e: fades and Play", SFI_V);
	SDL_Delay(200);
	SG_CHECK(sg_i1("IsFade", id) && sg_i1("IsPlay", id), "%s: e: port: the new fade did not wait for frames", SFI_V);
	sg_level();
	got = sg_level();
	SG_CHECK(!sg_i1("IsFade", id) && SFI_NEAR(got, initial, 0.75f), "%s: e: the new fade's target %.5f, base %.5f, fading %d",
		 SFI_V, got, initial, sg_i1("IsFade", id));
	gn_stop(id);
	/* f: Stop during a fade begun before Play does not start it again: it
	 * ends by its own time (Stop, 0x4a3700, leaves the thread alone) */
	SG_CHECK(sfi_prepare(id), "%s: f: cannot prepare", SFI_V);
	SG_CHECK(sfi_fade(id, 1500, 0.50f, false), "%s: f: fade before Play", SFI_V);
	SDL_Delay(40);
	SG_CHECK(sg_i1("Play", id), "%s: f: Play", SFI_V);
	SDL_Delay(700);
	SG_CHECK(sg_i1("Stop", id) && sg_i1("IsFade", id), "%s: f: Stop during the fade", SFI_V);
	SDL_Delay(860);
	SG_CHECK(!sg_i1("IsFade", id), "%s: f: still fading 1600 ms into a 1500 ms fade stopped at 740 ms", SFI_V);
	SG_CHECK(sg_i1("Play", id), "%s: f: cannot play again", SFI_V);
	got = sg_level();
	SG_CHECK(SFI_NEAR(got, initial, 0.5f), "%s: f: target %.5f, base %.5f", SFI_V, got, initial);
	gn_stop(id);
}

/* ---- SFI12 (v14 GBK): C entries the HLL does not reach ---- */
static void sfi12_child(void)
{
	sg_setup(&sfi_vers[sfi_ver].opts);
	const int id = 47;
	float full = sfi_interrupted(id);
	/* a: the volume getter first after a wall-time fade's end reads its
	 * target (the thread sets it at its end, 0x4a53d0 -> 0x4a5432) */
	SG_CHECK(sfi_fade(id, 80, 0.50f, false), "%s: a: fade", SFI_V);
	SDL_Delay(200);
	int v = wav_get_volume(id);
	SG_CHECK(v == 50, "%s: a: wav_get_volume first after a wall-time fade's end: %d, want 50", SFI_V, v);
	/* b: the sample position getter first after a wall-time stop fade's
	 * end sees the stop (0x4a5344..0x4a5366; port: a stop rewinds) */
	sg_level();
	SG_CHECK(wav_get_sample_pos(id) > 0, "%s: b: no frames mixed", SFI_V);
	SG_CHECK(sfi_fade(id, 80, 0.70f, true), "%s: b: stop fade", SFI_V);
	SDL_Delay(200);
	int pos = wav_get_sample_pos(id);
	SG_CHECK(pos == 0 && !sg_i1("IsPlay", id), "%s: b: wav_get_sample_pos first after a wall-time stop fade's end: %d (want 0), IsPlay %d",
		 SFI_V, pos, sg_i1("IsPlay", id));
	/* c: port: the legacy entry keeps its frame-driven fade also on a
	 * channel whose v14 clock is on */
	SG_CHECK(sg_i1("Play", id), "%s: c: cannot play again", SFI_V);
	sg_level();
	SG_CHECK(wav_fade(id, 80, 40, false), "%s: c: wav_fade", SFI_V);
	SDL_Delay(200);
	SG_CHECK(wav_is_fading(id), "%s: c: port: a legacy fade on a channel whose clock is on ran without frames", SFI_V);
	sg_level();
	float got = sg_level();
	SG_CHECK(!wav_is_fading(id) && SFI_NEAR(got, full, 0.4f), "%s: c: legacy fade target %.5f, base %.5f", SFI_V, got, full);
	gn_stop(id);
}

static int sfi_run(const char *id, const char *what, void (*fn)(void), bool v14_only, bool legacy_only)
{
	int f = 0;
	for (sfi_ver = 0; sfi_ver < SG_COUNT(sfi_vers); sfi_ver++) {
		if ((v14_only && !sfi_vers[sfi_ver].v14_gbk) || (legacy_only && sfi_ver != 2))
			continue;
		int r = sg_fork(fn);
		if (r)
			printf("  %s failed in: %s\n", id, sfi_vers[sfi_ver].name);
		f += r;
	}
	return sg_report(id, what, f);
}

static int test_sound_fade_interrupt(void)
{
	if (!gn_make_data()) {
		gn_remove_data();
		printf("SOUND-FADE-INTERRUPT FAIL: no synthetic data\n");
		return 1;
	}
	static const struct { const char *id, *what; void (*fn)(void); bool v14_only, legacy_only; } c[] = {
		{ "SFI1", "stop and non-stop fades interrupted (six version cases)", sfi1_child, false, false },
		{ "SFI2", "timed replacement runs by wall time while stopped", sfi2_stopped_child, true, false },
		{ "SFI2b", "third Fade, Stop, Unprepare, GetPos first, StopFade", sfi2_more_child, true, false },
		{ "SFI3", "time 0 ends the running fade, then target, then stop", sfi3_child, true, false },
		{ "SFI4", "negative time is immediate", sfi4_child, true, false },
		{ "SFI5", "a fade left running by Stop or the sound's end ends by wall time", sfi5_child, true, false },
		{ "SFI6", "wall-time fade gain is the interpolated value", sfi6_child, true, false },
		{ "SFI7", "later fades of an interrupted channel by wall time; others by frames", sfi7_child, true, false },
		{ "SFI8", "getters lock only during a wall-time fade", sfi8_child, true, false },
		{ "SFI9", "legacy StopFade, time 0 stop, negative time and Stop kept", sfi9_child, false, true },
		{ "SFI10", "a fade kept running by Stop: the rest of its time, from the gain reached, no clock", sfi10_child, true, false },
		{ "SFI11", "a timed fade begun before Play runs by its time; a later Fade is no interruption", sfi11_child, true, false },
		{ "SFI12", "C entries: volume and sample position getters, legacy fade on a wall-time channel", sfi12_child, true, false },
	};
	int failed = 0;
	for (int i = 0; i < SG_COUNT(c); i++) {
		sg_begin(c[i].id, c[i].what);
		failed += sfi_run(c[i].id, c[i].what, c[i].fn, c[i].v14_only, c[i].legacy_only);
	}
	gn_remove_data();
	printf("SOUND-FADE-INTERRUPT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, SG_COUNT(c));
	return failed ? 1 : 0;
}
