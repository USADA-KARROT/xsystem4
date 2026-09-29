/* frame-pacing: the original 60 fps limiter, one parts advance and one present
 * per game frame, and screenshots written off the main thread.
 *
 * Original semantics (static disassembly only, research/gui-visual/pacing.md):
 *   - view::detail::View_Update calls PartsEngine.UpdateComponent (the game's
 *     passed time), ChipmunkSpriteEngine.Update or TRANS_Update, then
 *     SystemService.UpdateView (both left out while the ADV engine skips).
 *   - UpdateView (SystemService case 6 -> 0x4c5450): Sleep(50) while the
 *     window is inactive with SleepByInactiveWindow on; the limiter 0x4676f0,
 *     or Sleep(1) when it did not sleep; Present (sync interval 0).
 *   - 0x4676f0: off unless SYSTEM_SetConfigOverFrameRateSleep (+0x4d); off
 *     while read-message skipping (+0x54) with SetConfigFrameSkipWhileMessage-
 *     Skip (+0x55) on and SetInvalidateFrameSkipWhileMessageSkip (+0x56) off.
 *     used = (float)(now - last) + carry; if 16.666666 > used: Sleep(ms =
 *     trunc(16.666666 - used)), carry = 16.666666 - (ms + used); else carry
 *     = 0; last = now (millisecond timer) either way.
 *   - ChipmunkSpriteEngine.Update (0x468a10) draws one frame in ten in that
 *     message-skip state (counter +0x58, limit 10 set at 0x467f51) and
 *     restarts the limiter (last = now, carry = 0) on the frames it leaves out.
 *
 * Cases (each in its own fork with a 30 s alarm):
 *   FP1 frame_pacing_limiter_step against the original arithmetic: fixed
 *       cases, the 32-bit timer wrap, bit-identical to an independent
 *       reference, and simulated frames of 2.3..11.3 ms work (average period
 *       with the Sleep ending on a 1 ms tick, and with an exact Sleep).
 *   FP2 the real clock: Sleep(n) returns on the (n + 1)th millisecond tick.
 *       Through the real ChipmunkSpriteEngine SYSTEM_Set* declarations:
 *       17 ms frames when limited, only Sleep(1) with OverFrameRateSleep off
 *       or while message skipping, one frame in ten drawn while message
 *       skipping and the limiter restarted on the others, the limiter on
 *       when the frame skip is invalidated or configured off, and Sleep(50)
 *       with SleepByInactiveWindow (no window here, so inactive).
 *   FP3 present ownership: UpdateView takes the present after its first call;
 *       two UpdateComponent calls without UpdateView, or 500 ms without it,
 *       give it back.
 *   FP4 one parts advance per frame: an alpha motion driven by the real
 *       UpdateComponent / ChipmunkSpriteEngine.Update / SystemService.
 *       UpdateView declarations advances by the game's passed time only
 *       (40 ms between frames, 16 ms passed each); UpdateView alone still
 *       advances by its own clock. Frames are message-skip frames, so nothing
 *       is drawn (no GL in the probe).
 *   FP5 the screenshot writer: the background write returns at once and
 *       writes the same bytes as the synchronous one, in call order.
 * The new functions are looked up with dlsym, so the fixture builds before
 * the fix and fails there. Frame times themselves need a window and GL; they
 * are measured in GUI runs (STAGE2_PERF), see research/gui-visual/pacing.md.
 */
#include <dlfcn.h>
#include <signal.h>
#include <math.h>
#include <SDL.h>
#include "gfx/private.h"
#include "system4/cg.h"

static int fp_failures;
#define FP_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fp_failures++; } } while (0)

static void *fp_sym(const char *name)
{
	void *p = dlsym(RTLD_DEFAULT, name);
	if (!p) {
		printf("  FAIL: %s is missing (frame pacing not implemented)\n", name);
		fp_failures++;
	}
	return p;
}

static double fp_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void fp_busy(double ms)
{
	double end = fp_now_ms() + ms;
	while (fp_now_ms() < end)
		;
}

typedef int (*fp_step_fn)(float *, uint32_t);

/* The original's float arithmetic, written out independently. */
static int fp_reference_step(float *carry, uint32_t elapsed)
{
	volatile float period = 16.666666f;
	volatile float used = (float)(double)elapsed + *carry;
	if (!(period > used)) {
		*carry = 0.0f;
		return -1;
	}
	volatile float rest = period - used;
	int ms = (int)rest;
	volatile float slept = (float)(double)(uint32_t)ms;
	*carry = period - (slept + used);
	return ms;
}

/* Simulated frames: work 2.3..11.3 ms, the timer in whole milliseconds.
 * windows_sleep: Sleep(n) ends on the first 1 ms tick at or after its due
 * time (frame_pacing_sleep's model), else exactly n ms later. Returns the
 * average period. */
static double fp_simulate(fp_step_fn step, bool windows_sleep, int *bad_carry, int *long_sleep)
{
	double t = 1000.25, first = 0, last_present = 0;
	uint32_t last = (uint32_t)t;
	float carry = 0.0f;
	for (int i = 0; i < 600; i++) {
		t += 2.3 + (i % 7) * 1.5;
		int ms = step(&carry, (uint32_t)t - last);
		if (ms >= 0) {
			if (carry < 0.0f || carry >= 1.0f)
				(*bad_carry)++;
			if (ms > 16)
				(*long_sleep)++;
		} else {
			ms = 1; // UpdateView's Sleep(1)
		}
		t = windows_sleep && ms ? ceil(t + ms) : t + ms;
		last = (uint32_t)t;
		if (i == 0)
			first = t;
		last_present = t;
	}
	return (last_present - first) / 599;
}

static void fp1_limiter_step(void)
{
	fp_step_fn step = (fp_step_fn)fp_sym("frame_pacing_limiter_step");
	if (!step)
		return;
	static const struct { float carry; uint32_t elapsed; int ms; float carry_out; } cases[] = {
		{ 0.0f,  5, 11, 0.666666f },  // 16.666666 - 5 = 11.666666: sleep 11, keep .666666
		{ 0.5f,  3, 13, 0.166666f },  // 13.166666
		{ 0.0f, 16,  0, 0.666666f },  // less than a millisecond left: Sleep(0)
		{ 0.0f, 17, -1, 0.0f },       // a whole period used: no sleep, carry reset
		{ 0.9f, 16, -1, 0.0f },       // 16.9 with the carry
		{ 0.0f,  0, 16, 0.666666f },
	};
	int bad = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		float carry = cases[i].carry;
		int ms = step(&carry, cases[i].elapsed);
		if (ms != cases[i].ms || fabsf(carry - cases[i].carry_out) > 1e-5f) {
			printf("  case %u: carry %.6f elapsed %u -> sleep %d carry %.6f (want %d, %.6f)\n", i,
				cases[i].carry, cases[i].elapsed, ms, carry, cases[i].ms, cases[i].carry_out);
			bad++;
		}
	}
	FP_CHECK(bad == 0, "%d fixed cases differ from the original arithmetic", bad);
	/* the timer is compared as an unsigned 32-bit difference */
	float carry = 0.25f;
	uint32_t last = 0xfffffff8u, now = 5u;
	FP_CHECK(step(&carry, now - last) == 3 && fabsf(carry - 0.416666f) < 1e-5f,
		"timer wrap: 13 ms elapsed must sleep 3 and carry .416666 (carry %.6f)", carry);
	/* bit-identical to the reference over every carry the steps produce */
	int diff = 0;
	float c1 = 0.0f, c2 = 0.0f;
	for (uint32_t i = 0; i < 20000; i++) {
		uint32_t e = (i * 7919u) % 23u;
		int a = step(&c1, e), b = fp_reference_step(&c2, e);
		if (a != b || memcmp(&c1, &c2, sizeof(float)))
			diff++;
	}
	FP_CHECK(diff == 0, "%d of 20000 steps differ from the reference", diff);
	int bad_carry = 0, long_sleep = 0;
	double win = fp_simulate(step, true, &bad_carry, &long_sleep);
	double exact = fp_simulate(step, false, &bad_carry, &long_sleep);
	FP_CHECK(bad_carry == 0, "%d carries outside [0, 1) after a sleep", bad_carry);
	FP_CHECK(long_sleep == 0, "%d sleeps longer than 16 ms", long_sleep);
	/* with the sleep ending on a tick, the period is 17 ticks whatever the
	 * work (up to 16 ms); an exact sleep would give 16 on average */
	FP_CHECK(win > 16.99 && win < 17.01, "tick-aligned Sleep: average period %.3f ms", win);
	FP_CHECK(exact >= 15.9 && exact < 16.7, "exact Sleep: average period %.3f ms", exact);
	printf("FP1 limiter step matches the original; simulated period %.3f ms (Sleep ends on a 1 ms tick), %.3f ms (exact Sleep)\n",
		win, exact);
}

static void fp_chipmunk(const char *fn, int value)
{
	bu_call("ChipmunkSpriteEngine", fn, 1, 0, 1, (union vm_value[]){ BU_I(value) });
}

static void fp_default_config(void)
{
	/* the values _system::detail::Init sets from the game's default config */
	fp_chipmunk("SYSTEM_SetConfigOverFrameRateSleep", 1);
	fp_chipmunk("SYSTEM_SetConfigSleepByInactiveWindow", 0);
	fp_chipmunk("SYSTEM_SetConfigFrameSkipWhileMessageSkip", 1);
	fp_chipmunk("SYSTEM_SetInvalidateFrameSkipWhileMessageSkip", 0);
	fp_chipmunk("SYSTEM_SetReadMessageSkipping", 0);
}

/* Average frame period of n waits with the given work per frame. */
static double fp_frames(void (*wait)(void), int n, double work_ms)
{
	wait();
	double start = fp_now_ms();
	for (int i = 0; i < n; i++) {
		fp_busy(work_ms);
		wait();
	}
	return (fp_now_ms() - start) / n;
}

static void fp2_limiter_clock(void)
{
	void (*wait)(void) = (void (*)(void))fp_sym("frame_pacing_wait");
	bool (*limit)(void) = (bool (*)(void))fp_sym("frame_pacing_limit");
	bool (*draw)(void) = (bool (*)(void))fp_sym("frame_pacing_draw_frame");
	if (!wait || !limit || !draw)
		return;
	void (*fsleep)(uint32_t) = (void (*)(uint32_t))fp_sym("frame_pacing_sleep");
	if (!fsleep)
		return;
	/* Sleep(n): returns on the (n + 1)th millisecond tick */
	int off_tick = 0;
	double late = 0;
	static const uint32_t ns[] = { 1, 5, 13 };
	for (int k = 0; k < 3; k++) {
		for (int r = 0; r < 5; r++) {
			uint64_t k0 = SDL_GetTicks64();
			double t0 = fp_now_ms();
			fsleep(ns[k]);
			double dt = fp_now_ms() - t0;
			uint64_t dk = SDL_GetTicks64() - k0;
			if (dk != ns[k] + 1)
				off_tick++;
			if (dt - ns[k] > late)
				late = dt - ns[k];
			FP_CHECK(dt >= ns[k] - 0.01, "Sleep(%u) returned after %.3f ms", ns[k], dt);
		}
	}
	FP_CHECK(off_tick <= 2, "Sleep(n) left on another tick than the (n + 1)th %d times of 15", off_tick);
	FP_CHECK(late < 2.5, "Sleep(n) took up to n + %.3f ms", late);

	fp_default_config();
	double period = fp_frames(wait, 60, 3.0);
	FP_CHECK(period > 16.0 && period < 18.0, "limited frames: %.3f ms per frame (want 17, 16.666666 on the tick)", period);
	fp_chipmunk("SYSTEM_SetConfigOverFrameRateSleep", 0);
	double off = fp_frames(wait, 30, 3.0);
	FP_CHECK(off < 8.0, "OverFrameRateSleep off: %.3f ms per frame (want Sleep(1) only)", off);
	fp_chipmunk("SYSTEM_SetConfigOverFrameRateSleep", 1);

	fp_chipmunk("SYSTEM_SetReadMessageSkipping", 1);
	FP_CHECK(!limit(), "limiter slept while message skipping");
	double skip = fp_frames(wait, 30, 3.0);
	FP_CHECK(skip < 8.0, "message skipping: %.3f ms per frame (want Sleep(1) only)", skip);
	int drawn = 0;
	for (int i = 0; i < 30; i++)
		drawn += draw();
	FP_CHECK(drawn == 3, "%d of 30 frames drawn while message skipping (want 3)", drawn);
	/* a left-out frame restarts the limiter: the next limited frame sleeps a
	 * whole period although the previous step was long ago */
	usleep(100000);
	FP_CHECK(!draw(), "frame after a drawn one was drawn while message skipping");
	fp_chipmunk("SYSTEM_SetReadMessageSkipping", 0);
	double t0 = fp_now_ms();
	bool slept = limit();
	double restart = fp_now_ms() - t0;
	FP_CHECK(slept && restart > 12.0, "limiter not restarted by a left-out frame (slept %d, %.3f ms)", slept, restart);
	drawn = 0;
	for (int i = 0; i < 30; i++)
		drawn += draw();
	FP_CHECK(drawn == 30, "only %d of 30 frames drawn without skipping", drawn);

	fp_chipmunk("SYSTEM_SetReadMessageSkipping", 1);
	fp_chipmunk("SYSTEM_SetInvalidateFrameSkipWhileMessageSkip", 1);
	usleep(20000);
	FP_CHECK(draw(), "frame left out with the frame skip invalidated");
	limit();
	FP_CHECK(limit(), "limiter off while skipping with the frame skip invalidated");
	fp_chipmunk("SYSTEM_SetInvalidateFrameSkipWhileMessageSkip", 0);
	fp_chipmunk("SYSTEM_SetConfigFrameSkipWhileMessageSkip", 0);
	limit();
	FP_CHECK(limit(), "limiter off while skipping with the frame skip configured off");
	fp_default_config();

	/* no window in the probe: it counts as inactive */
	fp_chipmunk("SYSTEM_SetConfigSleepByInactiveWindow", 1);
	t0 = fp_now_ms();
	wait();
	double inactive = fp_now_ms() - t0;
	FP_CHECK(inactive >= 50.0, "SleepByInactiveWindow: waited %.3f ms (want 50 or more)", inactive);
	fp_chipmunk("SYSTEM_SetConfigSleepByInactiveWindow", 0);
	t0 = fp_now_ms();
	wait();
	double active = fp_now_ms() - t0;
	FP_CHECK(active < 30.0, "SleepByInactiveWindow off: waited %.3f ms", active);
	printf("FP2 Sleep(n) at most n + %.3f ms (%d of 15 off the tick); limited %.3f ms/frame, off %.3f, skipping %.3f, restart sleep %.3f, inactive wait %.3f\n",
		late, off_tick, period, off, skip, restart, inactive);
}

static void fp3_present_owner(void)
{
	void (*update_view)(void) = (void (*)(void))fp_sym("frame_pacing_update_view");
	bool (*presents)(void) = (bool (*)(void))fp_sym("frame_pacing_update_view_presents");
	void (*parts_updated)(void) = (void (*)(void))fp_sym("frame_pacing_parts_updated");
	bool (*take)(void) = (bool (*)(void))fp_sym("frame_pacing_take_parts_updated");
	if (!update_view || !presents || !parts_updated || !take)
		return;
	FP_CHECK(!presents(), "UpdateView presents before its first call");
	update_view();
	FP_CHECK(presents(), "UpdateView not presenting right after its call");
	parts_updated();
	FP_CHECK(presents(), "one UpdateComponent in the frame took the present away");
	FP_CHECK(take(), "UpdateComponent's advance not reported to UpdateView");
	FP_CHECK(!take(), "the advance was reported twice");
	update_view();
	parts_updated();
	parts_updated();
	FP_CHECK(!presents(), "two UpdateComponent calls without UpdateView must present themselves");
	update_view();
	FP_CHECK(presents(), "UpdateView did not take the present back");
	FP_CHECK(take(), "advance mark lost");
	usleep(600000);
	FP_CHECK(!presents(), "500 ms without UpdateView must give presenting back");
	update_view();
	FP_CHECK(presents(), "UpdateView did not take the present back after a pause");
	printf("FP3 present ownership checked\n");
}

/* init_probe links without _PreLink; run PartsEngine's (it binds the v14
 * UpdateComponent by its five-argument declaration) and relink the library. */
static void fp_prelink_parts_engine(void)
{
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++) {
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PreLink")) {
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
			break;
		}
	}
	int lib = bu_lib("PartsEngine");
	libraries[lib] = link_static_library(&ain->libraries[lib], &lib_PartsEngine);
}

static void fp_update_component(int passed)
{
	BU_PE("UpdateComponent", BU_I(passed), BU_I(passed), BU_I(0), BU_F(1.0f), BU_F(1.0f));
}

static void fp_chipmunk_update(void)
{
	bu_call("ChipmunkSpriteEngine", "Update", 0, 0, 0, NULL);
}

static void fp_update_view(void)
{
	bu_call("SystemService", "UpdateView", 0, 0, 0, NULL);
}

static void fp4_one_advance(void)
{
	void (*update_view)(void) = (void (*)(void))fp_sym("frame_pacing_update_view");
	if (!update_view)
		return;
	fp_prelink_parts_engine();
	bu_postlink();
	extern void audio_reset(void);
	audio_reset(); /* no audio device in the probe: mark the anonymous channels free */
	/* no window: give the input code a 1280x720 view so the mouse mapping
	 * does not divide by zero */
	sdl.w = sdl.viewport.w = 1280;
	sdl.h = sdl.viewport.h = 720;
	fp_default_config();
	/* message-skip frames: ChipmunkSpriteEngine.Update leaves them out and
	 * UpdateView presents nothing (fewer than ten frames here) */
	fp_chipmunk("SYSTEM_SetReadMessageSkipping", 1);
	update_view(); /* UpdateView owns the present from the start */
	fp_chipmunk_update();
	fp_update_view(); /* first call: UpdateView's own clock starts */

	const int part = 7001;
	PE_AddMotionAlpha(part, 0, 1000, 0, 1000);
	PE_BeginMotion();
	int a0 = PE_GetPartsAlpha(part);
	int alpha[3];
	for (int f = 0; f < 2; f++) {
		usleep(40000);
		fp_update_component(16);
		fp_chipmunk_update();
		fp_update_view();
		alpha[f] = PE_GetPartsAlpha(part);
	}
	FP_CHECK(a0 == 0, "alpha %d before the first frame", a0);
	FP_CHECK(alpha[0] >= 15 && alpha[0] <= 17, "first frame: alpha %d, want 16 (the game's passed time once)", alpha[0]);
	FP_CHECK(alpha[1] >= 31 && alpha[1] <= 33, "second frame: alpha %d, want 32", alpha[1]);
	/* a frame without UpdateComponent: UpdateView advances by its own clock */
	usleep(40000);
	fp_chipmunk_update();
	fp_update_view();
	alpha[2] = PE_GetPartsAlpha(part);
	FP_CHECK(alpha[2] - alpha[1] >= 30, "UpdateView alone advanced %d ms (want about 40)", alpha[2] - alpha[1]);
	printf("FP4 alpha after frames: %d %d (16 ms each), then +%d by UpdateView alone\n",
		alpha[0], alpha[1], alpha[2] - alpha[1]);
}

typedef int (*fp_write_fn)(void *, int, int, const char *, enum cg_type);
typedef void (*fp_write_async_fn)(void *, int, int, const char *, enum cg_type);

static uint8_t *fp_read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = xmalloc(*size ? *size : 1);
	if (fread(b, 1, *size, f) != *size) {
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

static void fp5_screenshot_writer(void)
{
	fp_write_fn write_sync = (fp_write_fn)fp_sym("gfx_write_pixels");
	fp_write_async_fn write_async = (fp_write_async_fn)fp_sym("gfx_write_pixels_async");
	void (*wait_writes)(void) = (void (*)(void))fp_sym("gfx_wait_pixels_writes");
	if (!write_sync || !write_async || !wait_writes)
		return;
	const char *tmp = getenv("TMPDIR");
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s/xs4-frame-pacing-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	if (!mkdtemp(dir)) {
		FP_CHECK(0, "mkdtemp failed");
		return;
	}
	enum { w = 1280, h = 720, n = 3 };
	char sync_path[n][PATH_MAX], async_path[n][PATH_MAX];
	double sync_ms = 0, async_ms = 0;
	for (int k = 0; k < n; k++) {
		uint8_t *px = xmalloc(w * h * 4);
		for (int i = 0; i < w * h; i++) {
			px[i*4+0] = (i % w + k * 40) & 0xff;
			px[i*4+1] = (i / w * 3) & 0xff;
			px[i*4+2] = ((i * 2654435761u) >> 24) & 0x3f;
			px[i*4+3] = 0xff;
		}
		snprintf(sync_path[k], PATH_MAX, "%s/sync%d.png", dir, k);
		snprintf(async_path[k], PATH_MAX, "%s/async%d.png", dir, k);
		double t0 = fp_now_ms();
		write_sync(px, w, h, sync_path[k], ALCG_PNG);
		sync_ms += fp_now_ms() - t0;
		t0 = fp_now_ms();
		write_async(px, w, h, async_path[k], ALCG_PNG); /* takes px */
		async_ms += fp_now_ms() - t0;
	}
	wait_writes();
	int same = 0;
	for (int k = 0; k < n; k++) {
		size_t sa = 0, sb = 0;
		uint8_t *a = fp_read_file(sync_path[k], &sa), *b = fp_read_file(async_path[k], &sb);
		if (a && b && sa == sb && sa > 0 && !memcmp(a, b, sa))
			same++;
		free(a);
		free(b);
		unlink(sync_path[k]);
		unlink(async_path[k]);
	}
	rmdir(dir);
	FP_CHECK(same == n, "%d of %d background writes equal the synchronous ones", same, n);
	/* the calls themselves do not compress: much faster than a synchronous write */
	FP_CHECK(async_ms / n < 5.0 && async_ms * 4 < sync_ms,
		"background write calls took %.3f ms each (synchronous %.3f ms)", async_ms / n, sync_ms / n);
	printf("FP5 %d/%d background PNGs identical; call %.3f ms vs synchronous write %.3f ms\n",
		same, n, async_ms / n, sync_ms / n);
}

static int fp_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		alarm(30);
		fp_failures = 0;
		fn();
		fflush(stdout);
		_exit(fp_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (WIFSIGNALED(status))
		printf("  FAIL: %s died with signal %d\n", name, WTERMSIG(status));
	printf("FRAME-PACING %s %s\n", name, ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}

static int test_frame_pacing(void)
{
	int failed = 0;
	failed += fp_fork("FP1", fp1_limiter_step);
	failed += fp_fork("FP2", fp2_limiter_clock);
	failed += fp_fork("FP3", fp3_present_owner);
	failed += fp_fork("FP4", fp4_one_advance);
	failed += fp_fork("FP5", fp5_screenshot_writer);
	printf("FRAME-PACING %s %d/5 failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
