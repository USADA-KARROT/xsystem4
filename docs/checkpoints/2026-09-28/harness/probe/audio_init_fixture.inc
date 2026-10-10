/* audio-init: SDL audio must not depend on the first video initialization.
 * KiwiSoundEngine._ModuleInit calls audio_init before PartsEngine opens a
 * window. This is an SDL platform initialization defect, not a Windows
 * native ABI inference. No game files or physical audio device are used.
 *
 * AI1 cold startup: real Kiwi module entry, dummy output, no video; a stream
 *     refill reached through the engine's SDL callback proves the device runs.
 *     Repeated module init keeps mixer state and stream; one audio quit must
 *     balance initialization (no repeated subsystem references).
 * AI2 externally initialized audio: do not acquire another subsystem ref.
 * AI3 unavailable driver: nonfatal, usable mixer state and a useful warning;
 *     repeated module init does not rebuild. Missing warning is a test failure.
 *
 * Each case forks before SDL starts threads. Public mixer stream API only:
 * no copied production body or test-only hook. Device-open failure after
 * subsystem initialization is not forced. Dummy output does not prove real
 * BGM decoding or audible output. The initial 86 modes become 87 on this base.
 */
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <SDL.h>
#include "mixer.h"
#include "sts_mixer.h"
#include "xsystem4.h"

static int ai_failures;
#define AI_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ai_failures++; } } while (0)

static void ai_module_init(void)
{
	extern struct static_library lib_KiwiSoundEngine;
	library_run(&lib_KiwiSoundEngine, "_ModuleInit");
}

static bool ai_setup(const char *driver)
{
	Uint32 already = SDL_WasInit(SDL_INIT_AUDIO | SDL_INIT_VIDEO);
	AI_CHECK(!already, "SDL audio/video already initialized before case: 0x%x", already);
	if (already)
		return false;
	int a = SDL_setenv("SDL_AUDIODRIVER", driver, 1);
	int v = SDL_setenv("SDL_VIDEODRIVER", "audio-init-no-video", 1);
	AI_CHECK(!a && !v, "could not select isolated SDL drivers");
	config.mixer_nr_channels = 0;
	config.default_volume = 100;
	config.bgi_path = config.wai_path = NULL;
	return !a && !v;
}

struct ai_stream {
	sts_mixer_stream_t stream;
	float samples[128];
	SDL_atomic_t refills;
	SDL_atomic_t on_audio_thread;
	SDL_threadID main_thread;
};

static int ai_refill(sts_mixer_sample_t *sample, void *userdata)
{
	struct ai_stream *s = userdata;
	float *data = sample->data;
	for (unsigned i = 0; i < sample->length; i++)
		data[i] = i & 1 ? -0.125f : 0.125f;
	if (SDL_ThreadID() != s->main_thread)
		SDL_AtomicSet(&s->on_audio_thread, 1);
	SDL_AtomicAdd(&s->refills, 1);
	return STS_STREAM_CONTINUE;
}

static void ai_wait_refill(struct ai_stream *s, int after)
{
	Uint64 start = SDL_GetTicks64();
	while (SDL_AtomicGet(&s->refills) <= after && SDL_GetTicks64() - start < 1500)
		SDL_Delay(5);
}

static void ai_repeat_module(void)
{
	AI_CHECK(mixer_get_numof() == 3, "default mixer count %d, want 3", mixer_get_numof());
	AI_CHECK(mixer_set_name(0, "audio-init-marker"), "cannot set mixer marker");
	AI_CHECK(mixer_set_volume(0, 50), "cannot set mixer volume");
	for (int i = 0; i < 3; i++)
		ai_module_init();
	const char *name = mixer_get_name(0);
	int volume = -1;
	AI_CHECK(name && !strcmp(name, "audio-init-marker"), "repeated module init rebuilt mixer names");
	AI_CHECK(mixer_get_volume(0, &volume) && volume == 50,
		 "repeated module init changed mixer volume to %d", volume);
}

static void ai_output_case(bool preinitialized)
{
	if (!ai_setup("dummy"))
		return;
	if (preinitialized) {
		int rc = SDL_InitSubSystem(SDL_INIT_AUDIO);
		AI_CHECK(rc == 0, "dummy audio preinitialization failed: %s", SDL_GetError());
		if (rc < 0)
			return;
	}
	ai_module_init();
	AI_CHECK(SDL_WasInit(SDL_INIT_AUDIO) == SDL_INIT_AUDIO,
		 "Kiwi module did not initialize SDL audio: %s", SDL_GetError());
	AI_CHECK(!SDL_WasInit(SDL_INIT_VIDEO), "Kiwi module initialized video");
	const char *driver = SDL_GetCurrentAudioDriver();
	AI_CHECK(driver && !strcmp(driver, "dummy"), "audio driver is %s, want dummy", driver ? driver : "(none)");
	struct ai_stream s = {0};
	s.main_thread = SDL_ThreadID();
	s.stream.userdata = &s;
	s.stream.callback = ai_refill;
	s.stream.sample = (sts_mixer_sample_t) {
		.length = sizeof(s.samples) / sizeof(s.samples[0]),
		.frequency = 44100,
		.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT,
		.data = s.samples,
	};
	int voice = mixer_stream_play(&s.stream, 100);
	AI_CHECK(voice >= 0, "mixer refused synthetic stream");
	if (voice >= 0) {
		ai_wait_refill(&s, 0);
		AI_CHECK(SDL_AtomicGet(&s.refills) > 0 && SDL_AtomicGet(&s.on_audio_thread),
			 "no engine audio-thread refill (count %d)", SDL_AtomicGet(&s.refills));
	}
	ai_repeat_module();
	if (voice >= 0) {
		int before = SDL_AtomicGet(&s.refills);
		ai_wait_refill(&s, before);
		AI_CHECK(SDL_AtomicGet(&s.refills) > before, "repeated module init lost the playing stream");
		mixer_stream_stop(voice); // device lock waits for any in-flight refill
	}
	AI_CHECK(!SDL_WasInit(SDL_INIT_VIDEO), "repeated module init initialized video");
	printf("  dummy refills=%d, audio-thread=%d, preinitialized=%d\n",
		SDL_AtomicGet(&s.refills), SDL_AtomicGet(&s.on_audio_thread), preinitialized);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	AI_CHECK(!SDL_WasInit(SDL_INIT_AUDIO),
		 "one QuitSubSystem did not balance audio initialization (extra reference)");
	SDL_Quit();
}

static void ai1_cold(void) { ai_output_case(false); }
static void ai2_existing(void) { ai_output_case(true); }

static void ai3_unavailable(void)
{
	if (!ai_setup("audio-init-unavailable-driver"))
		return;
	FILE *capture = tmpfile();
	AI_CHECK(capture != NULL, "cannot capture warning: %s", strerror(errno));
	if (!capture)
		return;
	fflush(stderr);
	int saved = dup(STDERR_FILENO);
	if (saved < 0 || dup2(fileno(capture), STDERR_FILENO) < 0) {
		AI_CHECK(false, "cannot redirect warning: %s", strerror(errno));
		if (saved >= 0)
			close(saved);
		fclose(capture);
		return;
	}
	ai_module_init(); // an ERROR/exit fails only this child, not the mode
	fflush(stderr);
	int restored = dup2(saved, STDERR_FILENO);
	close(saved);
	AI_CHECK(restored >= 0, "cannot restore stderr: %s", strerror(errno));
	rewind(capture);
	char log[4096];
	size_t len = fread(log, 1, sizeof(log) - 1, capture);
	log[len] = '\0';
	fclose(capture);
	AI_CHECK(strstr(log, "WARNING") && strstr(log, "SDL_InitSubSystem")
		 && strstr(log, "audio-init-unavailable-driver"),
		 "missing useful SDL initialization warning; captured: %s", len ? log : "(none)");
	AI_CHECK(!SDL_WasInit(SDL_INIT_AUDIO | SDL_INIT_VIDEO), "failed audio init initialized audio/video");
	ai_repeat_module();
	AI_CHECK(!SDL_WasInit(SDL_INIT_AUDIO | SDL_INIT_VIDEO), "repeated failed init initialized audio/video");
	SDL_Quit();
}

static int ai_fork(const char *name, void (*fn)(void))
{
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0) {
		printf("  FAIL: %s fork: %s\n", name, strerror(errno));
		return 1;
	}
	if (pid == 0) {
		alarm(15);
		ai_failures = 0;
		fn();
		fflush(NULL);
		_exit(ai_failures ? 1 : 0);
	}
	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(pid, &status, 0);
	} while (waited < 0 && errno == EINTR);
	bool ok = waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (waited == pid && WIFSIGNALED(status))
		printf("  FAIL: %s died with signal %d\n", name, WTERMSIG(status));
	printf("%s: %s\n", name, ok ? "ok" : "FAIL");
	return !ok;
}

static int test_audio_init(void)
{
	int failed = 0;
	failed += ai_fork("AI1 audio before video and repeated module", ai1_cold);
	failed += ai_fork("AI2 existing audio is not retained again", ai2_existing);
	failed += ai_fork("AI3 unavailable audio is nonfatal and observable", ai3_unavailable);
	printf("AUDIO-INIT %s: %d of 3 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
