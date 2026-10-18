/* movie-audio-only: real FFmpeg movie_load/play/draw/is_end/free, without
 * game assets, a window or a physical audio device. The generator makes
 * Vorbis audio, an APEG audio wrapper, and lossless red/blue video + audio.
 * Before the fix, audio-only movie_load reaches preload_packets with a NULL
 * video queue. A child signal is a failure, not an expected pass.
 *
 * Each case forks before SDL/CGL starts threads. A finite EOF deadline plus
 * an alarm catches a stuck decoder; the AV control must display both colors.
 * Dummy output proves callback/EOF progress, not audible output or APEG video.
 */
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <OpenGL/OpenGL.h>
#include "audio.h"
#include "movie.h"
#include "sprite.h"

#ifndef XS4_MOVIE_FIXTURE_DIR
#error "build_probe.py must generate synthetic movie fixtures"
#endif

static int ma_failures;
#define MA_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ma_failures++; } } while (0)

static bool ma_audio_setup(void)
{
	MA_CHECK(!SDL_WasInit(SDL_INIT_AUDIO | SDL_INIT_VIDEO), "SDL already initialized before case");
	if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1)
	    || SDL_setenv("SDL_VIDEODRIVER", "movie-probe-no-video", 1)) {
		MA_CHECK(false, "cannot select isolated SDL drivers");
		return false;
	}
	config.mixer_nr_channels = 0;
	config.default_volume = 100;
	config.bgi_path = config.wai_path = NULL;
	audio_init();
	const char *driver = SDL_GetCurrentAudioDriver();
	bool ok = driver && !strcmp(driver, "dummy") && !SDL_WasInit(SDL_INIT_VIDEO);
	MA_CHECK(ok, "dummy audio unavailable or video initialized: %s", SDL_GetError());
	return ok;
}

static struct movie_context *ma_load(const char *name)
{
	config.game_dir = XS4_MOVIE_FIXTURE_DIR;
	printf("  movie_load %s\n", name);
	fflush(stdout);
	struct movie_context *mc = movie_load(name);
	MA_CHECK(mc != NULL, "movie_load(%s) returned NULL", name);
	return mc;
}

static bool ma_wait_eof(struct movie_context *mc, struct sact_sprite *sprite,
		       bool *saw_red, bool *saw_blue)
{
	Uint64 start = SDL_GetTicks64();
	int max_position = 0;
	while (!movie_is_end(mc) && SDL_GetTicks64() - start < 5000) {
		movie_draw(mc, sprite);
		int pos = movie_get_position(mc);
		if (pos > max_position)
			max_position = pos;
		if (sprite) {
			uint8_t *pixels = gfx_get_pixels(sprite_get_texture(sprite));
			bool red = true, blue = true;
			for (int i = 0; i < 32 * 24; i++) {
				const uint8_t *p = pixels + i * 4;
				red &= p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255;
				blue &= p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255;
			}
			free(pixels);
			*saw_red |= red;
			*saw_blue |= blue;
		}
		SDL_Delay(5);
	}
	bool ended = movie_is_end(mc);
	MA_CHECK(ended, "movie did not reach EOF within 5 seconds");
	MA_CHECK(max_position > 0, "movie position did not advance");
	printf("  EOF=%d max_position=%d\n", ended, max_position);
	return ended;
}

static void ma1_load_free(void)
{
	struct movie_context *mc = ma_load("long-mono.ogg");
	if (!mc)
		return;
	MA_CHECK(!movie_is_end(mc), "unplayed audio is already finished");
	int w = -1, h = -1;
	movie_get_video_size(mc, &w, &h);
	MA_CHECK(w == 0 && h == 0, "plain audio video dimensions %dx%d, want 0x0", w, h);
	movie_free(mc);
}

static void ma_audio_eof(const char *name, bool apeg)
{
	if (!ma_audio_setup())
		return;
	struct movie_context *mc = ma_load(name);
	if (mc) {
		MA_CHECK(!movie_is_end(mc), "audio finished before play");
		if (apeg) {
			int w = -1, h = -1;
			movie_get_video_size(mc, &w, &h);
			MA_CHECK(w == 32 && h == 24, "APEG dimensions %dx%d, want 32x24", w, h);
		}
		bool played = movie_play(mc);
		MA_CHECK(played, "movie_play failed");
		if (played) {
			ma_wait_eof(mc, NULL, NULL, NULL);
			MA_CHECK(!movie_draw(mc, NULL), "audio-only draw still active after EOF");
		}
		movie_free(mc);
	}
	SDL_Quit();
}

static void ma2_short_eof(void) { ma_audio_eof("short-mono.ogg", false); }
static void ma3_stereo_eof(void) { ma_audio_eof("long-stereo.ogg", false); }
static void ma5_apeg(void) { ma_audio_eof("synthetic.apeg", true); }

static void ma4_early_free(void)
{
	if (!ma_audio_setup())
		return;
	for (int i = 0; i < 4; i++) {
		struct movie_context *mc = ma_load("long-stereo.ogg");
		if (!mc)
			break;
		bool played = movie_play(mc);
		MA_CHECK(played, "movie_play failed before early free");
		if (played) {
			SDL_Delay(30);
			MA_CHECK(!movie_is_end(mc), "early-free fixture already reached EOF");
		}
		movie_free(mc);
		// Keep the device running: a stale callback would access the freed movie.
		SDL_Delay(30);
	}
	SDL_Quit();
}

static CGLContextObj ma_gl_setup(void)
{
	CGLPixelFormatAttribute attrs[] = {
		kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
		kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
		kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8, 0 };
	CGLPixelFormatObj pf = NULL;
	CGLContextObj ctx = NULL;
	GLint count = 0;
	if (CGLChoosePixelFormat(attrs, &pf, &count) || !pf
	    || CGLCreateContext(pf, NULL, &ctx) || CGLSetCurrentContext(ctx)) {
		printf("  ENVIRONMENT FAILURE: cannot create windowless CGL context\n");
		if (pf) CGLDestroyPixelFormat(pf);
		if (ctx) CGLDestroyContext(ctx);
		return NULL;
	}
	CGLDestroyPixelFormat(pf);
	glewExperimental = GL_TRUE;
	const char *src = getenv("XS4_SRC");
	if (glewInit() != GLEW_OK || !src || chdir(src)) {
		printf("  ENVIRONMENT FAILURE: GLEW or source shader directory unavailable\n");
		CGLSetCurrentContext(NULL);
		CGLDestroyContext(ctx);
		return NULL;
	}
	while (glGetError() != GL_NO_ERROR) {}
	probe_gl_initialize();
	return ctx;
}

static void ma6_av_control(void)
{
	CGLContextObj ctx = ma_gl_setup();
	if (!ctx) {
		ma_failures++;
		return;
	}
	struct sact_sprite sprite = {0};
	sprite_init(&sprite);
	sprite_init_color(&sprite, 32, 24, 0, 255, 0, 255);
	struct texture *tex = sprite_get_texture(&sprite);
	uint8_t *initial = gfx_get_pixels(tex);
	bool green = tex->w == 32 && tex->h == 24;
	for (int i = 0; green && i < 32 * 24; i++)
		green = initial[i*4] == 0 && initial[i*4+1] == 255
			&& initial[i*4+2] == 0 && initial[i*4+3] == 255;
	free(initial);
	MA_CHECK(green, "CGL texture self-check did not read synthetic green pixels");
	if (green && ma_audio_setup()) {
		struct movie_context *mc = ma_load("red-blue-av.mkv");
		if (mc) {
			int w = -1, h = -1;
			movie_get_video_size(mc, &w, &h);
			MA_CHECK(w == 32 && h == 24, "AV dimensions %dx%d, want 32x24", w, h);
			bool played = movie_play(mc), red = false, blue = false;
			MA_CHECK(played, "AV movie_play failed");
			if (played)
				ma_wait_eof(mc, &sprite, &red, &blue);
			MA_CHECK(red && blue, "AV frames not both observed: red=%d blue=%d", red, blue);
			printf("  AV colors red=%d blue=%d\n", red, blue);
			movie_free(mc);
		}
	}
	SDL_Quit();
	sprite_free(&sprite);
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
}

static void ma7_rejected_files(void)
{
	config.game_dir = XS4_MOVIE_FIXTURE_DIR;
	const char *names[] = { "missing-file.ogg", "empty.bin", "invalid.bin", "video-only.mkv" };
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		struct movie_context *mc = movie_load(names[i]);
		MA_CHECK(mc == NULL, "%s should be rejected", names[i]);
		if (mc)
			movie_free(mc);
	}
}

static int ma_case(const char *name, void (*fn)(void))
{
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0) {
		printf("  FAIL: %s fork: %s\n", name, strerror(errno));
		return 1;
	}
	if (pid == 0) {
		alarm(12);
		ma_failures = 0;
		fn();
		fflush(NULL);
		_exit(ma_failures ? 1 : 0);
	}
	int status = 0;
	pid_t waited;
	do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
	bool ok = waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (waited == pid && WIFSIGNALED(status))
		printf("  FAIL: %s died with signal %d\n", name, WTERMSIG(status));
	printf("%s: %s\n", name, ok ? "ok" : "FAIL");
	return !ok;
}

static int test_movie_audio_only(void)
{
	int failed = 0;
	failed += ma_case("MA1 audio-only load and unplayed free", ma1_load_free);
	failed += ma_case("MA2 short mono preload EOF", ma2_short_eof);
	failed += ma_case("MA3 stereo streaming EOF", ma3_stereo_eof);
	failed += ma_case("MA4 early free during dummy playback", ma4_early_free);
	failed += ma_case("MA5 synthetic APEG audio wrapper", ma5_apeg);
	failed += ma_case("MA6 normal AV red/blue rendering and EOF", ma6_av_control);
	failed += ma_case("MA7 invalid and video-only cleanup", ma7_rejected_files);
	printf("MOVIE-AUDIO-ONLY %s: %d of 7 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
