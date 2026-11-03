/* cursor_config_fixture.inc — the input page's mouse cursor settings and
 * their file, and the screen saver in movies and auto mode (2026-11-02).
 * Modes: mouse-cursor (MC), screensaver (SV).
 *
 * Original (static disassembly):
 *   - The settings object (constructor 0x497dc0) keeps three flags, all 0
 *     at first: type 0 hides a cursor that has not moved for 3000 ms (+0x34),
 *     type 1 hides it on a key or pad press, type 2 when the next message is
 *     shown. SystemService.SetMouseCursorConfig (0x4c3230) takes value != 0
 *     and does nothing when that is the current value; type 0 turned on
 *     restarts the still-time (0x498270); turned off, it shows the cursor
 *     and restarts the still-time, unless the game hides the cursor: then
 *     only the flag changes. Types 1 and 2 turned off show a cursor they
 *     hid. GetMouseCursorConfig (0x4c3300) answers 0 or 1 for types 0 to 2
 *     and refuses any other type.
 *   - IbisInputEngine.Mouse_HideByStepMessage (0x498190) and a key press
 *     with the cursor in the client area (0x4be6c0 -> 0x498130) hide the
 *     cursor at once when their type is on, it is not already hidden for
 *     that reason, and 1000 ms or more passed since the last time either of
 *     them hid it (one time for both). The timer counts from the engine's
 *     start.
 *   - Each WM_MOUSEMOVE and each 200 ms WM_TIMER (0x497f60): with another
 *     window in front or the cursor outside the client area the cursor is
 *     shown and the still-time restarts (the reasons stay recorded); while
 *     the game hides the cursor (Mouse_HideCursorByGame, 0x498760) it stays
 *     hidden; type 0: a move shows a cursor hidden for being still, 3000 ms
 *     without a move hide it; types 1 and 2: a move shows a cursor they hid
 *     and clears both reasons. Showing it again from the game shows it
 *     unless a key or the still-time hid it, and restarts the still-time
 *     (which forgets that the still-time hid it: a move does not show it
 *     then, the cursor outside the window does). The object starts with
 *     the last cursor position at -1,-1 (0x497dc0) and is made, then the
 *     file read (0x4bfd60). The restart loop (0x4b5a60) drops the old
 *     references (0x4c12e0, 0x4b30f0) and initializes the input engine
 *     again, so the first look after a restart also counts as a move
 *     and the still-time starts there; a
 *     restart of the still-time (0x498270: type 0 turned on, the game's
 *     show) takes the position again, so a look after it sees only a newer
 *     move.
 *   - MouseCursorSetting.sav in the save folder (writer 0x498310, reader
 *     0x4984f0): "AMC" and a 0 byte, then four little-endian int32: 2 (the
 *     version), type 0, type 1, type 2 (each 0 or 1). It is written when
 *     the window closes (0x4be9b0) and as the first step of a reset
 *     (0x4c12e0), always, and read when the object is made again. The
 *     reader stops at the first thing it cannot read and keeps what it read
 *     before: the name up to its 0 byte must be "AMC", the version is
 *     compared unsigned (above 2 is refused), type 1 is read only from
 *     version 1 on and type 2 only from version 2 on (type 2 is 0 below
 *     that), each value counts as value != 0, and bytes after the last one
 *     are not read. A missing or empty file keeps the defaults.
 *   - SystemService.RestrainScreensaver (0x4b9f00) records the time; for 30
 *     seconds after the last call the window refuses SC_SCREENSAVE and
 *     SC_MONITORPOWER (0x4bdea0 -> 0x4b9f70); otherwise the screen saver is
 *     allowed. The game calls it when its setting is on: each frame while
 *     a movie plays, and each time auto mode moves on from a message.
 *   - ChipmunkSpriteEngine.SYSTEM_SetConfigSleepByInactiveWindow: UpdateView
 *     sleeps 50 ms when the window is not active (0x4c5450, 0x4bffc0). The
 *     port has done this for every AIN version since before this fixture;
 *     SV5 only guards that it stays so.
 *
 * Port (asserted here): AIN v14 only, both string rules. A move is a real
 * mouse motion the event pump gets; the window is in front with the cursor
 * inside after the focus-gained and enter window events (the input code's
 * own flags). The cursor state is SDL's (SDL_ShowCursor). The screen saver
 * state is SDL's (SDL_IsScreenSaverEnabled; SDL disables it when its video
 * starts). A frame is SystemService.UpdateView (which pumps the events)
 * after the sprite engine marked its frame as left out, as it does while
 * messages are skipped, so that nothing is drawn (no GL in the probe).
 * Libraries start and stop through SystemService's and IbisInputEngine's
 * _PreLink, _PostLink, _ModuleInit and _ModuleFini, and through the real
 * vm_exit and vm_reset with only those two libraries listed. Every case runs
 * in its own fork with its own save folder (mkdtemp under $XS4_SAVE_TMP or
 * $TMPDIR, refused inside XS4_SRC or XS4_MASTER_GAME) and SDL's dummy video
 * driver (no window, nothing outside the process). Times are real. A check
 * meant to come after a limit is made 250 ms or more after it; one meant to
 * come before a limit is aimed 250 ms or more before it and is an
 * environment failure when the probe gets there too close to the limit
 * (within 50 ms of the 3000 ms one, 250 ms of the others). The cursor and
 * the screen saver change only inside a frame or a call, never between a
 * frame and the check after it.
 *
 * MC1  the exit writes the file: the three values (7 counts as 1, and the
 *      three differ, so that no two can trade places), and the defaults
 *      when nothing was set; no other file is left.
 * MC2  a reset writes the file and shows a cursor a message hid; the next
 *      start reads the file, not what was in memory; an unchanged second
 *      reset writes it again.
 * MC3  a start reads a version 2 file.
 * MC4  the reader: versions 0 and 1, a file cut after the version, after a
 *      value or inside one, values other than 0 and 1, bytes after the end;
 *      refused: another name, a longer or shorter name, no 0 byte, versions
 *      3 and -1, the name alone, an empty file. Each start begins from the
 *      defaults.
 * MC5  guard: no file gives 0, 0, 0 and is not created by the start; with
 *      these defaults nothing hides the cursor (3400 ms still, a message, a
 *      key); types 3 and -1 are refused by both functions.
 * MC6  guard, AIN v13: only type 0 is taken and read back, the file is not
 *      read or written, nothing hides the cursor but the game, and neither
 *      type 0 turned off nor the cursor outside undoes the game's hide.
 * MC7  type 0, turned on after 3400 ms still with it off: shown at 2700 ms
 *      from then, hidden at 3400 ms; setting the same value again does not
 *      restart the time, nor does the game's show (it did not hide the
 *      cursor); a move shows it and restarts the time; turned off, it is
 *      shown at once, and the game's show after its hide shows it.
 * MC8  type 2: Mouse_HideByStepMessage hides at once; a move shows it; a
 *      second call within 1000 ms does not hide, one after does; turned
 *      off, shown at once, and calls do nothing; a call while a message
 *      already hid the cursor does not count as a hide (a key 1500 ms after
 *      the first one's hide still hides).
 * MC9  type 1: a key hides; a move shows it; key and message share their
 *      1000 ms; the game's show does not look at a message's hide; a key
 *      with the cursor outside does not count; turned off, shown at once,
 *      and the game's show after its hide shows it.
 * MC10 another window in front or the cursor outside: shown, and the
 *      still-time starts again when it comes back.
 * MC11 the game's hide: neither type 0, 1 or 2 turned off nor a move shows
 *      the cursor, the cursor outside does, inside again hides it; shown again
 *      by the game only when no key hid it, with the still-time restarted;
 *      type 0 turned off shows a cursor a message hid.
 * MC12 the original's two ways of keeping a still cursor hidden, kept: the
 *      game shows again a cursor that was hidden for being still (a move
 *      while the game hid it did not show it): it stays hidden, and a move
 *      does not show it, with types 1 and 2 on as well; the cursor outside
 *      does. Type 0
 *      turned off while the game hides a still cursor: the game's show
 *      leaves it hidden.
 * MC13 the first look after the settings start over counts as a move: type
 *      0 on from the file, the engine 3400 ms past its start before the
 *      first frame: shown at the first frame and 2700 ms after it, hidden
 *      3400 ms after it; the same after a stop with a frame before the
 *      start (that frame does not hide again a cursor the game hid before
 *      the stop), and after vm_reset. With type 2 on from the file, the
 *      first look shows a cursor a message hid before it, unless type 0
 *      was turned on in between (that takes the position again). The
 *      game hiding and showing before the first look also takes the
 *      position again: a message hidden after that stays hidden.
 * MC14 guard, AIN v15: the parent implementation keeps all three types
 *      in memory, does not read or write the file, and does not hide for
 *      stillness, a key or a message. The game directly shows or hides the
 *      cursor, including repeated calls. Its start leaves the isolated
 *      v14 state alone. These are port compatibility checks, not claims
 *      about a v15 original.
 * SV1 v14: the screen saver SDL disabled at its video start is allowed
 *      after the engine's start and one frame.
 * SV2  RestrainScreensaver disables it; a second call 5 s later; still
 *      disabled 27 s after the second call (32 s after the first), allowed
 *      again 31 s after it.
 * SV3  guard, AIN v13: neither frames nor RestrainScreensaver change SDL's
 *      state.
 * SV4  guard: without SDL video, the start, frames and RestrainScreensaver
 *      do not start the video.
 * SV5  guard: a frame with SleepByInactiveWindow on waits 50 ms or more
 *      (no window here: inactive), with it off less than 30 ms, for AIN v13
 *      as for v14.
 * SV6  guard, AIN v15: frames and RestrainScreensaver leave SDL's state
 *      alone, both enabled and disabled. The call does not change the
 *      isolated v14 timer. This preserves the parent implementation, not
 *      a measured v15 original.
 *
 * Not covered: the pad press (no pad in the probe); the original's 200 ms
 * timer (the port checks every frame); a key or message within the engine's
 * first second; the Win32 menu; the text of the write error; whether the
 * port counts its window as active (there is no window here, so it never
 * is); the real power assertion (measured on the machine); the cursor on
 * the screen (not measured).
 */
#include <dirent.h>
#include "frame_pacing.h"

#define CZ_FILE "MouseCursorSetting.sav"
#define CZ_MAGIC "AMC" /* with its 0 byte: 4 bytes */

static char cz_dir[PATH_MAX];

/* ---- environment ------------------------------------------------------- */

static char *cz_path(const char *name)
{
	static char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/%s", cz_dir, name);
	return p;
}

static void cz_remove_all(void)
{
	DIR *d = opendir(cz_dir);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
			unlink(cz_path(e->d_name));
	}
	closedir(d);
}

static void cz_cleanup(void)
{
	cz_remove_all();
	rmdir(cz_dir);
}

static void cz_save_dir(void)
{
	const char *base = getenv("XS4_SAVE_TMP");
	if (!base || !base[0])
		base = getenv("TMPDIR");
	if (!base || !base[0])
		base = "/tmp";
	snprintf(cz_dir, sizeof cz_dir, "%s/xsys4-cursor-XXXXXX", base);
	if (!mkdtemp(cz_dir))
		xc_env_fail("cannot create a temporary save folder");
	if (sp_under(cz_dir, "XS4_SRC") || sp_under(cz_dir, "XS4_MASTER_GAME")) {
		rmdir(cz_dir);
		xc_env_fail("the temporary save folder is inside XS4_SRC or XS4_MASTER_GAME");
	}
	free(config.save_dir);
	config.save_dir = strdup(cz_dir);
}

static void cz_frames_setup(void)
{
	fp_prelink_parts_engine();
	bu_postlink();
	extern void audio_reset(void);
	audio_reset();
	sdl.w = sdl.viewport.w = 1280;
	sdl.h = sdl.viewport.h = 720;
	fp_default_config();
	/* message skipping: the frame limiter does not sleep */
	fp_chipmunk("SYSTEM_SetReadMessageSkipping", 1);
	frame_pacing_update_view();
}

/* SDL with the dummy video driver (video == true) or the events only. */
static void cz_setup(int version, bool video)
{
	cz_save_dir();
	if (video && !SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy"))
		xc_env_fail("cannot ask for SDL's dummy video driver");
	if (SDL_Init(SDL_INIT_EVENTS | (video ? SDL_INIT_VIDEO : 0)))
		xc_env_fail("SDL_Init failed");
	if (video && (!SDL_GetCurrentVideoDriver() || strcmp(SDL_GetCurrentVideoDriver(), "dummy")))
		xc_env_fail("SDL's video driver is not the dummy one");
	cz_frames_setup();
	ain->version = version;
	if (video && SDL_ShowCursor(SDL_QUERY) != 1)
		xc_env_fail("SDL's cursor is not shown after its video started");
	xc_ready();
}

/* ---- the engine -------------------------------------------------------- */

extern struct static_library lib_SystemService, lib_IbisInputEngine;

static void cz_lib_run(const char *name)
{
	library_run(&lib_SystemService, name);
	library_run(&lib_IbisInputEngine, name);
}

static void cz_start(void)
{
	cz_lib_run("_PreLink");
	cz_lib_run("_PostLink");
	cz_lib_run("_ModuleInit");
}

static void cz_stop(void) { cz_lib_run("_ModuleFini"); }

/* vm_exit / vm_reset stop every library the AIN lists: keep only these two,
 * so that nothing with a window, parts or audio is torn down here. */
static struct ain_library *cz_saved_libs;
static int cz_saved_nr;

static void cz_only_two(void)
{
	static struct ain_library two[2];
	cz_saved_libs = ain->libraries;
	cz_saved_nr = ain->nr_libraries;
	two[0] = ain->libraries[bu_lib("SystemService")];
	two[1] = ain->libraries[bu_lib("IbisInputEngine")];
	ain->libraries = two;
	ain->nr_libraries = 2;
	libraries_initialized = true;
}

static void cz_all_libs(void)
{
	ain->libraries = cz_saved_libs;
	ain->nr_libraries = cz_saved_nr;
}

static bool cz_set(int type, int value)
{
	return bu_call("SystemService", "SetMouseCursorConfig", 2, 0, 2, (union vm_value[]){ BU_I(type), BU_I(value) }).i;
}

/* GetMouseCursorConfig(type, wrap<int>): the value, or -1 when refused
 * (then nothing may have been written: -77 stays). */
static int cz_get(int type)
{
	int pg = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 1));
	heap[pg].page->values[0].i = -77;
	bool ok = bu_call("SystemService", "GetMouseCursorConfig", 2, 0, 3,
			  (union vm_value[]){ BU_I(type), BU_I(pg), BU_I(0) }).i;
	int v = heap[pg].page->values[0].i;
	heap_unref(pg);
	if (!ok && v != -77)
		XC_CHECK(0, "GetMouseCursorConfig(%d) refused but wrote %d", type, v);
	return ok ? v : -1;
}

static bool cz_values(int t0, int t1, int t2, const char *what)
{
	int a = cz_get(0), b = cz_get(1), c = cz_get(2);
	XC_CHECK(a == t0 && b == t1 && c == t2, "%s: the settings are %d, %d, %d; want %d, %d, %d", what, a, b, c, t0, t1, t2);
	return a == t0 && b == t1 && c == t2;
}

static void cz_step(void) { bu_call("IbisInputEngine", "Mouse_HideByStepMessage", 0, 0, 0, NULL); }

static void cz_game_hide(bool hide)
{
	bu_call("IbisInputEngine", "Mouse_HideCursorByGame", 1, 0, 1, (union vm_value[]){ BU_I(hide) });
}

static void cz_restrain(void) { bu_call("SystemService", "RestrainScreensaver", 0, 0, 0, NULL); }

static void cz_frame(void)
{
	/* What ChipmunkSpriteEngine.Update leaves for a frame it does not draw
	 * (it draws one in ten while skipping, and there is no GL here). */
	frame_pacing_set_frame(FRAME_PACING_SKIPPED);
	fp_update_view();
}

/* Frames (about 60 a second) until the given time. */
static void cz_until(double t)
{
	do {
		cz_frame();
		usleep(15000);
	} while (fp_now_ms() < t);
}

/* A check meant before a limit: the probe must still be before it. */
static void cz_before(double t, const char *what)
{
	if (fp_now_ms() >= t) {
		printf("  ENVIRONMENT: %s: %.0f ms late\n", what, fp_now_ms() - t);
		xc_env_fail("the probe fell behind a time limit");
	}
}

static bool cz_shown(void) { return SDL_ShowCursor(SDL_QUERY) == 1; }

static void cz_push_window(Uint8 event)
{
	SDL_Event e = { 0 };
	e.type = SDL_WINDOWEVENT;
	e.window.event = event;
	e.window.timestamp = SDL_GetTicks();
	SDL_PushEvent(&e);
}

static void cz_move(void)
{
	static int x = 100;
	x += 7;
	SDL_Event e = { 0 };
	e.type = SDL_MOUSEMOTION;
	e.motion.timestamp = SDL_GetTicks();
	e.motion.which = 0;
	e.motion.x = x;
	e.motion.y = 200;
	e.motion.xrel = 7;
	SDL_PushEvent(&e);
	cz_frame();
}

static void cz_key(void)
{
	SDL_Event e = { 0 };
	e.type = SDL_KEYDOWN;
	e.key.timestamp = SDL_GetTicks();
	e.key.state = SDL_PRESSED;
	e.key.keysym.scancode = SDL_SCANCODE_A;
	e.key.keysym.sym = SDLK_a;
	SDL_PushEvent(&e);
	e.type = SDL_KEYUP;
	e.key.state = SDL_RELEASED;
	SDL_PushEvent(&e);
	cz_frame();
}

/* ---- the file ---------------------------------------------------------- */

static size_t cz_make(uint8_t *b, const char *name, size_t name_len, int n, const int32_t *v)
{
	memcpy(b, name, name_len);
	size_t at = name_len;
	for (int i = 0; i < n; i++, at += 4)
		sp_put_le(b + at, v[i]);
	return at;
}

static bool cz_put(const uint8_t *b, size_t n)
{
	FILE *f = fopen(cz_path(CZ_FILE), "wb");
	if (!f)
		return false;
	bool ok = fwrite(b, 1, n, f) == n;
	return fclose(f) == 0 && ok;
}

/* The file's bytes, or -1 when it is missing. */
static long cz_read(uint8_t *b, size_t cap)
{
	FILE *f = fopen(cz_path(CZ_FILE), "rb");
	if (!f)
		return -1;
	size_t n = fread(b, 1, cap, f);
	fclose(f);
	return (long)n;
}

/* The save folder holds MouseCursorSetting.sav with exactly these values,
 * and nothing else but the window settings. */
static bool cz_expect_file(int t0, int t1, int t2, const char *what)
{
	uint8_t want[20], got[64];
	size_t n = cz_make(want, CZ_MAGIC, 4, 4, (const int32_t[]){ 2, t0, t1, t2 });
	long m = cz_read(got, sizeof got);
	bool ok = m == (long)n && !memcmp(got, want, n);
	if (m < 0)
		XC_CHECK(0, "%s: %s was not written", what, CZ_FILE);
	else if (!ok) {
		char hex[2 * 64 + 1] = "";
		for (long i = 0; i < m && i < 64; i++)
			snprintf(hex + 2 * i, 3, "%02x", got[i]);
		XC_CHECK(0, "%s: %s holds %ld bytes %s; want AMC, 2, %d, %d, %d", what, CZ_FILE, m, hex, t0, t1, t2);
	}
	DIR *d = opendir(cz_dir);
	char other[256] = "";
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && strcmp(e->d_name, CZ_FILE)
		    && strcmp(e->d_name, "WindowSetting.json"))
			snprintf(other, sizeof other, "%s", e->d_name);
	}
	if (d)
		closedir(d);
	XC_CHECK(!other[0], "%s: the save folder also holds %s", what, other);
	return ok && !other[0];
}

static bool cz_put_values(int version, int t0, int t1, int t2)
{
	uint8_t b[20];
	size_t n = cz_make(b, CZ_MAGIC, 4, 4, (const int32_t[]){ version, t0, t1, t2 });
	return cz_put(b, n);
}

/* Runs the real vm_exit in a child of the case, after start(). */
static void cz_exit_child(void (*start)(void))
{
	fflush(NULL);
	pid_t pid = fork();
	if (pid == 0) {
		alarm(30);
		start();
		cz_only_two();
		fflush(NULL);
		vm_exit(0);
		_exit(99);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	XC_CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "the exit ended with status 0x%x", st);
}

/* The real vm_reset (vm_free, then back to the setjmp). */
static void cz_reset(void)
{
	static volatile int phase;
	phase = 0;
	cz_only_two();
	if (!setjmp(reset_buf)) {
		phase = 1;
		vm_reset();
	}
	cz_all_libs();
	XC_CHECK(phase == 1, "vm_reset did not come back");
}

/* ---- MC cases ---------------------------------------------------------- */

static void cz1_start_set(void)
{
	cz_start();
	cz_set(0, 7);
	cz_set(1, 1);
	cz_set(2, 0);
}

static void cz1_exit_writes(void)
{
	cz_setup(14, true);
	XC_CHECK(cz_read((uint8_t[4]){ 0 }, 4) < 0, "the save folder is not empty");
	cz_exit_child(cz1_start_set);
	cz_expect_file(1, 1, 0, "after the exit");
	/* nothing set: the defaults are written all the same */
	cz_remove_all();
	cz_exit_child(cz_start);
	cz_expect_file(0, 0, 0, "after an exit with the defaults");
	cz_cleanup();
}

static void cz2_reset(void)
{
	cz_setup(14, true);
	cz_start();
	cz_until(fp_now_ms() + 1150); /* past the engine's first second */
	cz_set(0, 1);
	cz_set(1, 1);
	cz_set(2, 1);
	cz_step(); /* a message hides the cursor */
	cz_reset();
	cz_expect_file(1, 1, 1, "after the reset");
	XC_CHECK(cz_shown(), "the cursor is hidden after the reset");
	/* the file decides the next start, not the values in memory */
	XC_CHECK(cz_put_values(2, 0, 1, 0), "cannot write the fixture file");
	cz_start();
	cz_values(0, 1, 0, "after the restart");
	/* an unchanged reset writes again */
	cz_remove_all();
	cz_reset();
	cz_expect_file(0, 1, 0, "after a second reset");
	cz_cleanup();
}

static void cz3_read(void)
{
	cz_setup(14, true);
	XC_CHECK(cz_put_values(2, 1, 0, 1), "cannot write the fixture file");
	cz_start();
	cz_values(1, 0, 1, "after the start");
	cz_cleanup();
}

/* Dirty the values, stop (as a reset does), put the file, start. */
static void cz4_try(const uint8_t *b, size_t n, int t0, int t1, int t2, const char *what)
{
	cz_set(0, 1);
	cz_set(1, 1);
	cz_set(2, 1);
	cz_stop();
	XC_CHECK(cz_put(b, n), "%s: cannot write the fixture file", what);
	cz_start();
	cz_values(t0, t1, t2, what);
}

static void cz4_reader(void)
{
	cz_setup(14, true);
	cz_start();
	uint8_t b[64];
	size_t n;
	n = cz_make(b, CZ_MAGIC, 4, 4, (const int32_t[]){ 1, 1, 1, 1 });
	cz4_try(b, n, 1, 1, 0, "version 1");
	n = cz_make(b, CZ_MAGIC, 4, 2, (const int32_t[]){ 0, 1 });
	cz4_try(b, n, 1, 0, 0, "version 0");
	n = cz_make(b, CZ_MAGIC, 4, 4, (const int32_t[]){ 0, 1, 1, 1 });
	cz4_try(b, n, 1, 0, 0, "version 0 with more values");
	n = cz_make(b, CZ_MAGIC, 4, 1, (const int32_t[]){ 2 });
	cz4_try(b, n, 0, 0, 0, "version 2 cut after the version");
	n = cz_make(b, CZ_MAGIC, 4, 2, (const int32_t[]){ 2, 1 });
	cz4_try(b, n, 1, 0, 0, "version 2 cut after type 0");
	n = cz_make(b, CZ_MAGIC, 4, 2, (const int32_t[]){ 2, 1 });
	b[n] = 1, b[n + 1] = 0;
	cz4_try(b, n + 2, 1, 0, 0, "version 2 cut inside type 1");
	n = cz_make(b, CZ_MAGIC, 4, 3, (const int32_t[]){ 2, 0, 1 });
	cz4_try(b, n, 0, 1, 0, "version 2 cut after type 1");
	n = cz_make(b, CZ_MAGIC, 4, 4, (const int32_t[]){ 2, 5, -1, 0x100 });
	cz4_try(b, n, 1, 1, 1, "values 5, -1 and 256");
	n = cz_make(b, CZ_MAGIC, 4, 5, (const int32_t[]){ 2, 0, 1, 1, 7 });
	cz4_try(b, n, 0, 1, 1, "bytes after the end");
	n = cz_make(b, "AMD", 4, 4, (const int32_t[]){ 2, 1, 1, 1 });
	cz4_try(b, n, 0, 0, 0, "another name");
	n = cz_make(b, "AMCX", 5, 4, (const int32_t[]){ 2, 1, 1, 1 });
	cz4_try(b, n, 0, 0, 0, "a longer name");
	n = cz_make(b, "AM", 3, 4, (const int32_t[]){ 2, 1, 1, 1 });
	cz4_try(b, n, 0, 0, 0, "a shorter name");
	cz4_try((const uint8_t *)"AMC", 3, 0, 0, 0, "no 0 byte");
	memcpy(b, "AMC\x01\x02\x00\x00\x00\x01\x00\x00\x00", 12);
	cz4_try(b, 12, 0, 0, 0, "a name running into the values");
	n = cz_make(b, CZ_MAGIC, 4, 4, (const int32_t[]){ 3, 1, 1, 1 });
	cz4_try(b, n, 0, 0, 0, "version 3");
	n = cz_make(b, CZ_MAGIC, 4, 4, (const int32_t[]){ -1, 1, 1, 1 });
	cz4_try(b, n, 0, 0, 0, "version -1");
	cz4_try((const uint8_t *)CZ_MAGIC, 4, 0, 0, 0, "the name alone");
	cz4_try(b, 0, 0, 0, 0, "an empty file");
	cz_cleanup();
}

static void cz5_defaults(void)
{
	cz_setup(14, true);
	cz_start();
	cz_values(0, 0, 0, "without a file");
	XC_CHECK(cz_read((uint8_t[4]){ 0 }, 4) < 0, "the start wrote %s", CZ_FILE);
	cz_until(fp_now_ms() + 3400);
	cz_step();
	cz_key();
	XC_CHECK(cz_shown(), "the defaults hid the cursor (3400 ms still, a message or a key)");
	XC_CHECK(cz_get(3) == -1 && cz_get(-1) == -1, "GetMouseCursorConfig answers type 3 or -1");
	XC_CHECK(!cz_set(3, 1) && !cz_set(-1, 1), "SetMouseCursorConfig takes type 3 or -1");
	XC_CHECK(cz_set(0, 1) && cz_set(1, 1) && cz_set(2, 1), "SetMouseCursorConfig refuses one of the types 0 to 2");
	cz_cleanup();
}

static void cz6_start_v13(void)
{
	cz_start();
	cz_set(0, 1);
}

static void cz6_v13_guard(void)
{
	cz_setup(13, true);
	XC_CHECK(cz_put_values(2, 1, 1, 1), "cannot write the fixture file");
	cz_start();
	XC_CHECK(cz_get(0) == 0, "v13 read the file (type 0 is %d)", cz_get(0));
	XC_CHECK(cz_get(1) == -1 && cz_get(2) == -1, "v13 answers type 1 or 2");
	XC_CHECK(cz_set(0, 1) && !cz_set(1, 1) && !cz_set(2, 1), "v13 takes another type than 0, or refuses 0");
	XC_CHECK(cz_get(0) == 1, "v13: type 0 does not read back");
	double t = fp_now_ms();
	cz_until(t + 3400);
	XC_CHECK(cz_shown(), "v13: a still cursor was hidden");
	cz_step();
	cz_key();
	XC_CHECK(cz_shown(), "v13: a message or a key hid the cursor");
	cz_game_hide(true);
	XC_CHECK(!cz_shown(), "v13: the game's hide did not hide the cursor");
	cz_set(0, 0);
	XC_CHECK(!cz_shown(), "v13: turning type 0 off showed the cursor the game hid");
	cz_move();
	cz_push_window(SDL_WINDOWEVENT_LEAVE);
	cz_frame();
	XC_CHECK(!cz_shown(), "v13: the game's hide was undone by a move or the cursor outside");
	cz_push_window(SDL_WINDOWEVENT_ENTER);
	cz_frame();
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "v13: the game did not show the cursor again");
	/* the exit leaves the file alone */
	uint8_t want[20], got[64];
	size_t n = cz_make(want, CZ_MAGIC, 4, 4, (const int32_t[]){ 2, 1, 1, 1 });
	cz_exit_child(cz6_start_v13);
	long m = cz_read(got, sizeof got);
	XC_CHECK(m == (long)n && !memcmp(got, want, n), "v13: the exit changed %s (%ld bytes)", CZ_FILE, m);
	cz_cleanup();
}

static void cz7_still(void)
{
	cz_setup(14, true);
	cz_start();
	cz_until(fp_now_ms() + 3400); /* still, with type 0 off */
	double t0 = fp_now_ms();
	XC_CHECK(cz_set(0, 1), "SetMouseCursorConfig(0, 1) refused");
	cz_until(t0 + 2000);
	cz_set(0, 1); /* the same value: the time goes on */
	cz_until(t0 + 2700);
	cz_before(t0 + 2950, "2700 ms still");
	XC_CHECK(cz_shown(), "hidden after 2700 ms without a move");
	cz_until(t0 + 3400);
	XC_CHECK(!cz_shown(), "shown after 3400 ms without a move (not hidden, or setting the same value restarted the time)");
	cz_game_hide(false); /* the game did not hide it: nothing */
	cz_move();
	double t1 = fp_now_ms();
	XC_CHECK(cz_shown(), "a move did not show the cursor");
	cz_until(t1 + 2700);
	cz_before(t1 + 2950, "2700 ms after the move");
	XC_CHECK(cz_shown(), "hidden 2700 ms after a move");
	cz_until(t1 + 3400);
	XC_CHECK(!cz_shown(), "shown 3400 ms after a move");
	cz_set(0, 0);
	XC_CHECK(cz_shown(), "turning type 0 off did not show the cursor at once");
	cz_until(fp_now_ms() + 600);
	XC_CHECK(cz_shown(), "hidden with type 0 off");
	cz_game_hide(true);
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "the game's show after type 0 was turned off left the cursor hidden (the still-time's hide still counted?)");
	cz_cleanup();
}

static void cz8_step(void)
{
	cz_setup(14, true);
	cz_start();
	cz_until(fp_now_ms() + 1150);
	cz_step();
	XC_CHECK(cz_shown(), "a message hid the cursor with type 2 off");
	XC_CHECK(cz_set(2, 1), "SetMouseCursorConfig(2, 1) refused");
	cz_step();
	double t0 = fp_now_ms();
	XC_CHECK(!cz_shown(), "Mouse_HideByStepMessage did not hide the cursor at once");
	cz_until(t0 + 200);
	XC_CHECK(!cz_shown(), "the cursor came back without a move");
	cz_move();
	XC_CHECK(cz_shown(), "a move did not show the cursor");
	cz_until(t0 + 400);
	cz_step();
	cz_before(t0 + 750, "the second message");
	XC_CHECK(cz_shown(), "a second message within 1000 ms hid the cursor");
	cz_until(t0 + 1250);
	cz_step();
	double t1 = fp_now_ms();
	XC_CHECK(!cz_shown(), "a message 1250 ms after the last hide did not hide the cursor");
	cz_set(2, 0);
	XC_CHECK(cz_shown(), "turning type 2 off did not show the cursor at once");
	cz_until(t1 + 1250);
	cz_step();
	XC_CHECK(cz_shown(), "a message hid the cursor after type 2 was turned off");
	XC_CHECK(cz_set(1, 1) && cz_set(2, 1), "SetMouseCursorConfig refused type 1 or 2");
	cz_until(fp_now_ms() + 1250);
	cz_step();
	double t2 = fp_now_ms();
	XC_CHECK(!cz_shown(), "a message did not hide the cursor");
	cz_until(t2 + 1250);
	cz_step(); /* hidden for a message already: nothing */
	cz_move();
	cz_until(t2 + 1500);
	cz_key();
	cz_before(t2 + 2000, "the key 1500 ms after the message's hide");
	XC_CHECK(!cz_shown(), "a key 1500 ms after a message's hide did not hide the cursor (a second message while hidden restarted the time?)");
	cz_cleanup();
}

static void cz9_key(void)
{
	cz_setup(14, true);
	cz_start();
	cz_until(fp_now_ms() + 1150);
	cz_key();
	XC_CHECK(cz_shown(), "a key hid the cursor with type 1 off");
	XC_CHECK(cz_set(1, 1) && cz_set(2, 1), "SetMouseCursorConfig refused type 1 or 2");
	cz_key();
	double t0 = fp_now_ms();
	XC_CHECK(!cz_shown(), "a key did not hide the cursor");
	cz_move();
	XC_CHECK(cz_shown(), "a move did not show the cursor");
	/* a message within 1000 ms of the key's hide does not hide */
	cz_until(t0 + 300);
	cz_step();
	cz_before(t0 + 750, "the message after the key");
	XC_CHECK(cz_shown(), "a message 300 ms after a key's hide hid the cursor");
	cz_until(t0 + 1250);
	cz_step();
	double t1 = fp_now_ms();
	XC_CHECK(!cz_shown(), "a message 1250 ms after a key's hide did not hide the cursor");
	cz_game_hide(true);
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "the game's show left a cursor a message hid hidden");
	cz_move();
	cz_until(t1 + 300);
	cz_key();
	cz_before(t1 + 750, "the key after the message");
	XC_CHECK(cz_shown(), "a key 300 ms after a message's hide hid the cursor");
	/* a key with the cursor outside does not count */
	cz_until(t1 + 1250);
	cz_push_window(SDL_WINDOWEVENT_LEAVE);
	cz_frame();
	cz_key();
	double t2 = fp_now_ms();
	XC_CHECK(cz_shown(), "the cursor is hidden while outside");
	cz_push_window(SDL_WINDOWEVENT_ENTER);
	cz_frame();
	cz_key();
	cz_before(t2 + 750, "the key inside again");
	XC_CHECK(!cz_shown(), "a key inside did not hide the cursor (a key outside was counted?)");
	cz_set(1, 0);
	XC_CHECK(cz_shown(), "turning type 1 off did not show the cursor at once");
	cz_game_hide(true);
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "the game's show after type 1 was turned off left the cursor hidden (the key's hide still counted?)");
	cz_cleanup();
}

static void cz10_away(void)
{
	cz_setup(14, true);
	cz_start();
	cz_frame();
	double t0 = fp_now_ms();
	cz_set(0, 1);
	cz_until(t0 + 3400);
	XC_CHECK(!cz_shown(), "not hidden after 3400 ms without a move");
	cz_push_window(SDL_WINDOWEVENT_FOCUS_LOST);
	cz_frame();
	XC_CHECK(cz_shown(), "another window in front: the cursor stays hidden");
	cz_until(fp_now_ms() + 1500);
	XC_CHECK(cz_shown(), "hidden while another window is in front");
	cz_push_window(SDL_WINDOWEVENT_FOCUS_GAINED);
	cz_frame();
	double t1 = fp_now_ms();
	cz_until(t1 + 2700);
	cz_before(t1 + 2950, "2700 ms after the window came back");
	XC_CHECK(cz_shown(), "hidden 2700 ms after the window came to the front (the still-time did not start again)");
	cz_until(t1 + 3400);
	XC_CHECK(!cz_shown(), "shown 3400 ms after the window came to the front");
	cz_push_window(SDL_WINDOWEVENT_LEAVE);
	cz_frame();
	XC_CHECK(cz_shown(), "the cursor outside: it stays hidden");
	cz_until(fp_now_ms() + 600);
	cz_push_window(SDL_WINDOWEVENT_ENTER);
	cz_frame();
	double t2 = fp_now_ms();
	XC_CHECK(cz_shown(), "hidden as soon as the cursor came back inside");
	cz_until(t2 + 2700);
	cz_before(t2 + 2950, "2700 ms after the cursor came back");
	XC_CHECK(cz_shown(), "hidden 2700 ms after the cursor came back inside");
	cz_cleanup();
}

static void cz11_game(void)
{
	cz_setup(14, true);
	cz_start();
	cz_until(fp_now_ms() + 1150);
	cz_game_hide(true);
	XC_CHECK(!cz_shown(), "Mouse_HideCursorByGame(1) did not hide the cursor");
	cz_set(0, 1);
	cz_set(0, 0);
	XC_CHECK(!cz_shown(), "turning type 0 off showed the cursor the game hides");
	cz_set(1, 1);
	cz_set(1, 0);
	cz_set(2, 1);
	cz_set(2, 0);
	XC_CHECK(!cz_shown(), "turning type 1 or 2 off showed the cursor the game hides");
	cz_move();
	cz_until(fp_now_ms() + 100);
	XC_CHECK(!cz_shown(), "a move showed the cursor the game hides");
	cz_push_window(SDL_WINDOWEVENT_LEAVE);
	cz_frame();
	XC_CHECK(cz_shown(), "the cursor outside: the game's hide stays");
	cz_push_window(SDL_WINDOWEVENT_ENTER);
	cz_frame();
	XC_CHECK(!cz_shown(), "inside again: the game's hide was lost");
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "Mouse_HideCursorByGame(0) did not show the cursor");
	/* a key's hide outlasts the game's */
	cz_set(1, 1);
	cz_key();
	XC_CHECK(!cz_shown(), "a key did not hide the cursor");
	cz_game_hide(true);
	cz_game_hide(false);
	XC_CHECK(!cz_shown(), "the game showed a cursor a key hid");
	cz_move();
	XC_CHECK(cz_shown(), "a move did not show the cursor a key hid");
	cz_set(1, 0);
	/* the end of the game's hide restarts the still-time */
	cz_set(0, 1);
	double t0 = fp_now_ms();
	cz_until(t0 + 2000);
	cz_game_hide(true);
	cz_frame();
	cz_game_hide(false);
	double t1 = fp_now_ms();
	XC_CHECK(cz_shown(), "the game did not show the cursor again");
	cz_until(t1 + 2700);
	cz_before(t1 + 2950, "2700 ms after the game showed the cursor");
	XC_CHECK(cz_shown(), "hidden 2700 ms after the game showed it (the still-time did not start again)");
	cz_until(t1 + 3400);
	XC_CHECK(!cz_shown(), "shown 3400 ms after the game showed it");
	/* type 0 turned off shows a cursor a message hid */
	cz_move();
	cz_set(2, 1);
	cz_step();
	XC_CHECK(!cz_shown(), "a message did not hide the cursor");
	cz_set(0, 0);
	XC_CHECK(cz_shown(), "turning type 0 off did not show a cursor a message hid");
	cz_cleanup();
}

static void cz12_kept_hidden(void)
{
	cz_setup(14, true);
	cz_start();
	cz_frame();
	XC_CHECK(cz_set(0, 1), "SetMouseCursorConfig(0, 1) refused");
	cz_until(fp_now_ms() + 3400);
	XC_CHECK(!cz_shown(), "not hidden after 3400 ms without a move");
	cz_game_hide(true);
	cz_frame();
	cz_move();
	XC_CHECK(!cz_shown(), "a move showed the cursor the game hides (type 0 on)");
	cz_game_hide(false);
	cz_frame();
	XC_CHECK(!cz_shown(), "the game showed again a cursor that was hidden for being still");
	XC_CHECK(cz_set(1, 1) && cz_set(2, 1), "SetMouseCursorConfig refused type 1 or 2");
	cz_move();
	cz_until(fp_now_ms() + 100);
	XC_CHECK(!cz_shown(), "a move showed the cursor after the game's show (the original forgot the still-time's hide there)");
	cz_push_window(SDL_WINDOWEVENT_LEAVE);
	cz_frame();
	XC_CHECK(cz_shown(), "the cursor outside: it stays hidden");
	cz_push_window(SDL_WINDOWEVENT_ENTER);
	cz_frame();
	/* type 0 turned off while the game hides a still cursor */
	cz_until(fp_now_ms() + 3400);
	XC_CHECK(!cz_shown(), "not hidden 3400 ms after the cursor came back inside");
	cz_game_hide(true);
	XC_CHECK(cz_set(0, 0), "SetMouseCursorConfig(0, 0) refused");
	cz_frame();
	cz_game_hide(false);
	cz_frame();
	XC_CHECK(!cz_shown(), "type 0 turned off while the game hid a still cursor: the game's show showed it");
	cz_push_window(SDL_WINDOWEVENT_LEAVE);
	cz_frame();
	XC_CHECK(cz_shown(), "the cursor outside with type 0 off: it stays hidden");
	cz_cleanup();
}

/* Type 0 on from the file: shown at the first frame and 2700 ms after it,
 * hidden 3400 ms after it. */
static void cz13_from_first_look(const char *what)
{
	cz_frame();
	double t0 = fp_now_ms();
	XC_CHECK(cz_shown(), "%s: hidden at the first look (type 0 on from the file)", what);
	cz_until(t0 + 2700);
	cz_before(t0 + 2950, "2700 ms after the first look");
	XC_CHECK(cz_shown(), "%s: hidden 2700 ms after the first look", what);
	cz_until(t0 + 3400);
	XC_CHECK(!cz_shown(), "%s: shown 3400 ms after the first look", what);
}

static void cz13_first_look(void)
{
	cz_setup(14, true);
	XC_CHECK(cz_put_values(2, 1, 0, 0), "cannot write the fixture file");
	usleep(3400 * 1000); /* the engine has run 3400 ms, no frames yet */
	cz_start();
	cz13_from_first_look("start");
	/* a frame between the stop and the start does not take the first look,
	 * nor hide again a cursor the game hid before the stop */
	cz_game_hide(true);
	cz_stop();
	bool shown = cz_shown();
	XC_CHECK(shown, "the stop did not show the cursor the game hid");
	cz_frame();
	if (shown)
		XC_CHECK(cz_shown(), "a frame after the stop hid the cursor again (the game's hide was kept)");
	cz_start();
	cz_game_hide(false); /* nothing to show: the start forgot the game's hide */
	cz13_from_first_look("a frame before the start");
	cz_reset();
	cz_start();
	cz13_from_first_look("after vm_reset");
	/* type 2 on from the file: the first look also shows a cursor a message
	 * hid before it */
	cz_stop();
	XC_CHECK(cz_put_values(2, 0, 0, 1), "cannot write the fixture file");
	cz_start();
	cz_step();
	bool hid = !cz_shown();
	XC_CHECK(hid, "a message did not hide the cursor before the first look");
	cz_frame();
	if (hid)
		XC_CHECK(cz_shown(), "the first look did not show a cursor a message hid (it is a move)");
	/* type 0 turned on before the first look takes the position again: the
	 * first look is no move then */
	cz_stop();
	cz_start();
	cz_step();
	hid = !cz_shown();
	XC_CHECK(hid, "a message did not hide the cursor before the first look (second time)");
	XC_CHECK(cz_set(0, 1), "SetMouseCursorConfig(0, 1) refused");
	cz_frame();
	if (hid) {
		XC_CHECK(!cz_shown(), "type 0 turned on before the first look: the first look showed the cursor a message hid");
		cz_move();
		XC_CHECK(cz_shown(), "a move did not show the cursor a message hid");
	}
	/* The game's show also takes the position again (0x498760 ->
	 * 0x498270), even before the first look after the start. */
	cz_stop();
	XC_CHECK(cz_put_values(2, 0, 0, 1), "cannot write the fixture file");
	cz_start();
	cz_game_hide(true);
	XC_CHECK(!cz_shown(), "the game did not hide before the first look");
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "the game did not show before the first look");
	cz_step();
	hid = !cz_shown();
	XC_CHECK(hid, "a message did not hide after the game's show before the first look");
	cz_frame();
	if (hid)
		XC_CHECK(!cz_shown(), "the game's show before the first look kept a move and showed a cursor a message hid");
	cz_cleanup();
}

static void cz14_v15_guard(void)
{
	cz_setup(15, true);
	XC_CHECK(cz_put_values(2, 1, 1, 1), "cannot write the fixture file");
	cz_start();
	cz_values(0, 0, 0, "v15 did not read the file");
	XC_CHECK(cz_set(0, 7) && cz_set(1, -2) && cz_set(2, 1), "v15 refuses a previously supported type");
	cz_values(1, 1, 1, "v15 keeps the three flags in memory");
	cz_until(fp_now_ms() + 3400);
	cz_step();
	cz_key();
	XC_CHECK(cz_shown(), "v15: stillness, a message or a key hid the cursor");
	/* The legacy API always asks SDL, even when the previous request was
	 * the same. There is no game-hidden state machine in this version. */
	SDL_ShowCursor(SDL_DISABLE);
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "v15: a repeated show did not show the cursor");
	cz_game_hide(true);
	SDL_ShowCursor(SDL_ENABLE);
	cz_game_hide(true);
	XC_CHECK(!cz_shown(), "v15: a repeated hide did not hide the cursor");
	XC_CHECK(cz_set(0, 0) && cz_set(1, 0) && cz_set(2, 0), "v15 cannot turn off its flags");
	XC_CHECK(!cz_shown(), "v15: turning off a flag showed the cursor");
	cz_move();
	cz_push_window(SDL_WINDOWEVENT_LEAVE);
	cz_frame();
	XC_CHECK(!cz_shown(), "v15: a move or the cursor outside undid the game's hide");
	cz_push_window(SDL_WINDOWEVENT_ENTER);
	cz_frame();
	cz_stop();
	XC_CHECK(!cz_shown(), "v15: the stop changed the cursor state");
	cz_game_hide(false);
	XC_CHECK(cz_shown(), "v15: the game did not show the cursor");
	uint8_t want[20], got[64];
	size_t n = cz_make(want, CZ_MAGIC, 4, 4, (const int32_t[]){ 2, 1, 1, 1 });
	cz_exit_child(cz1_start_set);
	long m = cz_read(got, sizeof got);
	XC_CHECK(m == (long)n && !memcmp(got, want, n), "v15: the stop or exit changed %s", CZ_FILE);
	/* Isolate the start's version guard: v15 must not initialize or read
	 * into the v14-only state, even when that state already exists. */
	ain->version = 14;
	cz_start();
	XC_CHECK(cz_set(0, 1) && cz_set(1, 1) && cz_set(2, 1), "cannot seed the isolated v14 flags");
	XC_CHECK(cz_put_values(2, 0, 0, 0), "cannot replace the fixture file");
	ain->version = 15;
	cz_start();
	ain->version = 14;
	cz_values(1, 1, 1, "v15 start left the isolated v14 state alone");
	/* A v15 message call must not set the v14 hidden state either. */
	ain->version = 15;
	cz_step();
	XC_CHECK(cz_shown(), "v15: a message used the isolated v14 state");
	cz_cleanup();
}

static int test_mouse_cursor(void)
{
	static const struct xc_case cases[] = {
		{ "MC1", cz1_exit_writes }, { "MC2", cz2_reset }, { "MC3", cz3_read }, { "MC4", cz4_reader },
		{ "MC5", cz5_defaults }, { "MC6", cz6_v13_guard }, { "MC7", cz7_still }, { "MC8", cz8_step },
		{ "MC9", cz9_key }, { "MC10", cz10_away }, { "MC11", cz11_game }, { "MC12", cz12_kept_hidden },
		{ "MC13", cz13_first_look }, { "MC14", cz14_v15_guard },
	};
	return xc_run("MOUSE-CURSOR", cases, sizeof cases / sizeof *cases, 40);
}

/* ---- SV cases ---------------------------------------------------------- */

static void sv1_allowed(void)
{
	cz_setup(14, true);
	XC_CHECK(!SDL_IsScreenSaverEnabled(), "SDL's video start did not disable the screen saver (the precondition)");
	cz_start();
	cz_frame();
	cz_frame();
	XC_CHECK(SDL_IsScreenSaverEnabled(), "v14: the screen saver is still disabled after the start and a frame");
	cz_cleanup();
}

static void sv2_restrain(void)
{
	cz_setup(14, true);
	cz_start();
	cz_frame();
	XC_CHECK(SDL_IsScreenSaverEnabled(), "v14: the screen saver is disabled before RestrainScreensaver");
	double t0 = fp_now_ms();
	cz_restrain();
	cz_frame();
	XC_CHECK(!SDL_IsScreenSaverEnabled(), "RestrainScreensaver did not disable the screen saver");
	cz_until(t0 + 5000);
	cz_restrain();
	double t1 = fp_now_ms();
	cz_until(t0 + 32000);
	cz_before(t1 + 29750, "27 s after the second call");
	XC_CHECK(!SDL_IsScreenSaverEnabled(), "allowed again 27 s after the second call (the first call's time counted?)");
	cz_until(t1 + 31000);
	XC_CHECK(SDL_IsScreenSaverEnabled(), "still disabled 31 s after the last call");
	cz_cleanup();
}

static void sv3_v13_guard(void)
{
	cz_setup(13, true);
	cz_start();
	cz_frame();
	cz_frame();
	XC_CHECK(!SDL_IsScreenSaverEnabled(), "v13: the start or a frame changed SDL's screen saver state");
	cz_restrain();
	cz_until(fp_now_ms() + 200);
	XC_CHECK(!SDL_IsScreenSaverEnabled(), "v13: RestrainScreensaver changed SDL's screen saver state");
	cz_cleanup();
}

static void sv4_no_video(void)
{
	cz_setup(14, false);
	cz_start();
	cz_frame();
	cz_restrain();
	cz_until(fp_now_ms() + 200);
	XC_CHECK(!SDL_WasInit(SDL_INIT_VIDEO), "the engine started SDL's video");
	cz_cleanup();
}

static double sv5_frame_ms(void)
{
	double t = fp_now_ms();
	cz_frame();
	return fp_now_ms() - t;
}

static void sv5_inactive_sleep(void)
{
	cz_setup(13, false);
	cz_start();
	for (int v = 13; v <= 14; v++) {
		ain->version = v;
		fp_chipmunk("SYSTEM_SetConfigSleepByInactiveWindow", 1);
		double on = sv5_frame_ms();
		fp_chipmunk("SYSTEM_SetConfigSleepByInactiveWindow", 0);
		double off = sv5_frame_ms();
		XC_CHECK(on >= 50.0 && off < 30.0, "AIN v%d: a frame took %.1f ms with SleepByInactiveWindow on (want 50 or more) and %.1f ms with it off (want less than 30)", v, on, off);
	}
	cz_cleanup();
}

static void sv6_v15_guard(void)
{
	cz_setup(15, true);
	cz_start();
	cz_frame();
	XC_CHECK(!SDL_IsScreenSaverEnabled(), "v15: a frame changed SDL's disabled screen saver state");
	SDL_EnableScreenSaver();
	cz_restrain();
	cz_frame();
	XC_CHECK(SDL_IsScreenSaverEnabled(), "v15: RestrainScreensaver or a frame changed the enabled state");
	/* The v15 call must leave the isolated v14 timer unchanged. */
	ain->version = 14;
	cz_frame();
	XC_CHECK(SDL_IsScreenSaverEnabled(), "v15: RestrainScreensaver changed the isolated v14 timer");
	ain->version = 15;
	cz_cleanup();
}

static int test_screensaver(void)
{
	static const struct xc_case cases[] = {
		{ "SV1", sv1_allowed }, { "SV2", sv2_restrain }, { "SV3", sv3_v13_guard }, { "SV4", sv4_no_video },
		{ "SV5", sv5_inactive_sleep }, { "SV6", sv6_v15_guard },
	};
	return xc_run("SCREENSAVER", cases, sizeof cases / sizeof *cases, 60);
}
