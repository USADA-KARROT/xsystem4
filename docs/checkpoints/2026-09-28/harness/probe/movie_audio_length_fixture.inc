/* movie-audio-length: how many samples a movie's sound gives the mixer, and
 * which ones. src/movie_ffmpeg.c's audio_callback hands the mixer one
 * decoded frame per call; the frame holds frame->nb_samples samples. Before
 * the fix the callback took linesize[0] / bytes per sample for that number:
 * the size of the frame's buffer, which for a Vorbis frame shorter than the
 * buffer (the first frame after a short block, the frame the stream's end
 * trims) is more than the frame has, so the samples after the frame's end
 * were played and the clock (stream_time) counted them. The last frame of
 * the game's opening movie (an APEG, whose sound is a Vorbis stream this
 * callback plays) holds one sample in a buffer of 1024.
 *
 * build_probe.py appends probe_movie_audio_pull() to a copy of
 * movie_ffmpeg.c: one call of the static audio_callback on the calling
 * thread, as the mixer makes it after movie_play, and what it gave. Every
 * case opens no audio device (an unavailable SDL driver, as sound-groups
 * does), so no other thread calls it; movie_play still registers the stream
 * with the mixer and sets the sample format.
 *
 * Data: generate_movie_fixtures.py (synthetic sine waves; lengths.txt and
 * the reference decodes made there by the ffmpeg command, which uses the
 * same libavcodec). The expected number of samples is the number written
 * to the file, which is also what ffprobe decodes; the expected samples are
 * the reference's, byte for byte.
 *
 *   ML1 long-stereo.ogg played to the end: 52920 samples (1.2 s written; its
 *       first frame holds 576, its last 120), byte for byte the reference,
 *       and the clock at the end 52920 / 44100 s.
 *   ML2 The same through the APEG wrapper (the path of the game's opening):
 *       52920 samples, the reference, the clock.
 *   ML3 tail-one.apeg, whose last frame holds one sample: the last call
 *       gives 1, the total is what was written, the samples the reference's.
 *   ML4 Unchanged: after movie_seek(500) the first call gives the frame from
 *       the start position (22050) on (nb_samples - trim, at most 1024), and
 *       those samples are the reference's from 22050.
 *   ML5 After movie_seek(500), the rest: 52920 - 22050 samples, the
 *       reference from 22050 on, the clock at the end 1.2 s.
 *   ML6 Unchanged: an MPEG-1 program stream with MP2 sound (frames of 1152
 *       samples, whose buffer is the frame) gives what the reference
 *       decode has (s16, byte for byte), before and after the fix.
 *
 * Not covered: a sample format other than planar float and planar s16, a
 * mono movie (the callback plays its one plane on both sides), the mixer's
 * resampling, a movie with a video stream (the AV control is
 * movie-audio-only's MA6), and real audio output.
 * Requires forked_cases.inc.
 */
#include "audio.h"
#include "movie.h"
#include "sts_mixer.h"

#ifndef XS4_MOVIE_FIXTURE_DIR
#error "build_probe.py must generate synthetic movie fixtures"
#endif

extern int probe_movie_audio_pull(struct movie_context *mc, const void **data, unsigned *values, double *stream_time);
extern int probe_movie_bytes_per_sample(struct movie_context *mc);

struct ml_length {
	char movie[64], ref[64];
	int width;		/* bytes per value */
	long decoded;		/* ffprobe's nb_samples added up */
	long last;		/* the last frame's nb_samples */
	long written;		/* frames written to the file, 0: not counted */
};

static bool ml_length_of(const char *movie, struct ml_length *out)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/lengths.txt", XS4_MOVIE_FIXTURE_DIR);
	FILE *f = fopen(path, "r");
	if (!f)
		xc_env_fail("lengths.txt is missing");
	bool found = false;
	struct ml_length l;
	while (fscanf(f, "%63s %63s %d %ld %ld %ld", l.movie, l.ref, &l.width, &l.decoded, &l.last, &l.written) == 6) {
		if (!strcmp(l.movie, movie)) {
			*out = l;
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

static uint8_t *ml_read(const char *name, size_t *size)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", XS4_MOVIE_FIXTURE_DIR, name);
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = xmalloc(n > 0 ? n : 1);
	*size = n > 0 && fread(b, 1, n, f) == (size_t)n ? (size_t)n : 0;
	fclose(f);
	return b;
}

/* The fixture's facts as the generator recorded them; an environment failure
 * when they are not what the case is about. */
static uint8_t *ml_reference(const char *movie, struct ml_length *l, size_t *size)
{
	if (!ml_length_of(movie, l)) {
		printf("  ENVIRONMENT: %s is not in lengths.txt (the generator made none)\n", movie);
		fflush(stdout);
		_exit(XC_ENV_EXIT);
	}
	uint8_t *ref = ml_read(l->ref, size);
	if (!ref || *size != (size_t)l->decoded * 2 * l->width) {
		printf("  ENVIRONMENT: %s is %zu bytes, want %ld frames of 2 x %d\n", l->ref, ref ? *size : 0,
		       l->decoded, l->width);
		fflush(stdout);
		_exit(XC_ENV_EXIT);
	}
	if (l->written && l->written != l->decoded) {
		printf("  ENVIRONMENT: %s decodes to %ld frames, %ld were written\n", movie, l->decoded, l->written);
		fflush(stdout);
		_exit(XC_ENV_EXIT);
	}
	return ref;
}

static void ml_audio_setup(void)
{
	if (SDL_setenv("SDL_AUDIODRIVER", "movie-audio-length-no-device", 1)
	    || SDL_setenv("SDL_VIDEODRIVER", "movie-audio-length-no-video", 1))
		xc_env_fail("cannot select the SDL drivers");
	config.mixer_nr_channels = 0;
	config.default_volume = 100;
	config.bgi_path = config.wai_path = NULL;
	config.game_dir = XS4_MOVIE_FIXTURE_DIR;
	audio_init();
	if (SDL_GetCurrentAudioDriver() || SDL_WasInit(SDL_INIT_VIDEO))
		xc_env_fail("an audio device or video was opened; the callback would run on another thread");
}

static struct movie_context *ml_open(const char *name, int seek_ms)
{
	struct movie_context *mc = movie_load(name);
	if (!mc) {
		printf("  ENVIRONMENT: movie_load(%s) failed\n", name);
		fflush(stdout);
		_exit(XC_ENV_EXIT);
	}
	if (seek_ms && !movie_seek(mc, seek_ms)) {
		printf("  ENVIRONMENT: movie_seek(%s, %d) failed\n", name, seek_ms);
		fflush(stdout);
		_exit(XC_ENV_EXIT);
	}
	if (!movie_play(mc))
		xc_env_fail("movie_play failed");
	xc_ready();
	return mc;
}

struct ml_pull {
	uint8_t *out;
	size_t bytes, cap;
	long frames;		/* what the calls gave, in frames of two values */
	long first, last;	/* the first and the last call's frames */
	unsigned calls;
	double clock;		/* stream_time after the last call that gave samples */
	bool complete;
};

/* Calls the callback until it reports the end (at most max_calls calls
 * that give samples; 0: no limit). */
static void ml_pull(struct movie_context *mc, unsigned max_calls, struct ml_pull *p)
{
	memset(p, 0, sizeof *p);
	const int width = probe_movie_bytes_per_sample(mc);
	for (unsigned k = 0; k < 200000 && (!max_calls || p->calls < max_calls); k++) {
		const void *data;
		unsigned values;
		double t;
		int r = probe_movie_audio_pull(mc, &data, &values, &t);
		if (r == STS_STREAM_COMPLETE) {
			p->complete = true;
			p->clock = t;
			return;
		}
		size_t n = (size_t)values * width;
		if (p->bytes + n > p->cap) {
			p->cap = (p->bytes + n) * 2;
			p->out = xrealloc(p->out, p->cap);
		}
		if (n)
			memcpy(p->out + p->bytes, data, n);
		p->bytes += n;
		const long frames = values / 2;
		if (!p->calls)
			p->first = frames;
		p->last = frames;
		p->frames += frames;
		p->calls++;
		p->clock = t;
	}
}

/* The samples given against the reference from frame `from` on: equal over
 * the whole length when `whole`, else over what was given. */
static void ml_expect_samples(const struct ml_pull *p, const uint8_t *ref, size_t ref_bytes, int width, long from,
		bool whole, const char *what)
{
	const size_t off = (size_t)from * 2 * width;
	const size_t want = whole ? ref_bytes - off : p->bytes;
	XC_CHECK(p->bytes == want, "%s: %zu bytes given, want %zu (%ld frames given)", what, p->bytes, want, p->frames);
	size_t n = p->bytes < ref_bytes - off ? p->bytes : ref_bytes - off;
	size_t i = 0;
	while (i < n && p->out[i] == ref[off + i])
		i++;
	XC_CHECK(i == n, "%s: the samples differ from the reference at frame %zu of %zu", what,
		 i / (2 * (size_t)width), n / (2 * (size_t)width));
}

static void ml_whole(const char *movie, const char *file, const char *what, bool check_last)
{
	struct ml_length l;
	size_t ref_bytes;
	uint8_t *ref = ml_reference(movie, &l, &ref_bytes);
	ml_audio_setup();
	struct movie_context *mc = ml_open(file, 0);
	struct ml_pull p;
	ml_pull(mc, 0, &p);
	printf("  %s: %u calls, %ld frames (first %ld, last %ld), clock %.6f s; decoded %ld (last frame %ld)\n", what,
	       p.calls, p.frames, p.first, p.last, p.clock, l.decoded, l.last);
	XC_CHECK(p.complete, "%s: the stream did not end", what);
	XC_CHECK(p.frames == l.decoded, "%s: %ld frames given, the file has %ld", what, p.frames, l.decoded);
	ml_expect_samples(&p, ref, ref_bytes, l.width, 0, true, what);
	XC_CHECK(fabs(p.clock - (double)l.decoded / 44100) < 1e-9, "%s: the clock ends at %.9f s, want %.9f s", what,
		 p.clock, (double)l.decoded / 44100);
	if (check_last)
		XC_CHECK(l.last == 1 && p.last == 1, "%s: the last call gave %ld frames, the file's last frame has %ld",
			 what, p.last, l.last);
	movie_free(mc);
	free(p.out);
	free(ref);
}

static void ml1_ogg(void) { ml_whole("long-stereo.ogg", "long-stereo.ogg", "ML1 long-stereo.ogg", false); }
static void ml2_apeg(void) { ml_whole("long-stereo.ogg", "synthetic.apeg", "ML2 synthetic.apeg", false); }
static void ml3_tail(void) { ml_whole("tail-one.ogg", "tail-one.apeg", "ML3 tail-one.apeg", true); }
static void ml6_mp2(void) { ml_whole("mp2-stereo.mpg", "mp2-stereo.mpg", "ML6 mp2-stereo.mpg", false); }

enum { ML_SEEK_MS = 500, ML_SEEK_FRAME = 22050 };

static void ml4_seek_first(void)
{
	struct ml_length l;
	size_t ref_bytes;
	uint8_t *ref = ml_reference("long-stereo.ogg", &l, &ref_bytes);
	ml_audio_setup();
	struct movie_context *mc = ml_open("synthetic.apeg", ML_SEEK_MS);
	struct ml_pull p;
	ml_pull(mc, 1, &p);
	printf("  ML4 first call after the seek: %ld frames, clock %.6f s\n", p.first, p.clock);
	XC_CHECK(p.calls == 1 && p.first > 0 && p.first <= 1024, "ML4: the first call gave %ld frames", p.first);
	ml_expect_samples(&p, ref, ref_bytes, l.width, ML_SEEK_FRAME, false, "ML4 the first call after the seek");
	XC_CHECK(fabs(p.clock - (ML_SEEK_MS / 1000.0 + (double)p.first / 44100)) < 1e-9,
		 "ML4: the clock is %.9f s after the first call, want %.9f s", p.clock,
		 ML_SEEK_MS / 1000.0 + (double)p.first / 44100);
	movie_free(mc);
	free(p.out);
	free(ref);
}

static void ml5_seek_rest(void)
{
	struct ml_length l;
	size_t ref_bytes;
	uint8_t *ref = ml_reference("long-stereo.ogg", &l, &ref_bytes);
	ml_audio_setup();
	struct movie_context *mc = ml_open("synthetic.apeg", ML_SEEK_MS);
	struct ml_pull p;
	ml_pull(mc, 0, &p);
	printf("  ML5 after the seek: %u calls, %ld frames (last %ld), clock %.6f s\n", p.calls, p.frames, p.last,
	       p.clock);
	XC_CHECK(p.complete, "ML5: the stream did not end");
	XC_CHECK(p.frames == l.decoded - ML_SEEK_FRAME, "ML5: %ld frames after the seek, want %ld", p.frames,
		 l.decoded - ML_SEEK_FRAME);
	ml_expect_samples(&p, ref, ref_bytes, l.width, ML_SEEK_FRAME, true, "ML5 after the seek");
	XC_CHECK(fabs(p.clock - (double)l.decoded / 44100) < 1e-9, "ML5: the clock ends at %.9f s, want %.9f s",
		 p.clock, (double)l.decoded / 44100);
	movie_free(mc);
	free(p.out);
	free(ref);
}

static int test_movie_audio_length(void)
{
	static const struct xc_case cases[] = {
		{ "ML1", ml1_ogg }, { "ML2", ml2_apeg }, { "ML3", ml3_tail },
		{ "ML4", ml4_seek_first }, { "ML5", ml5_seek_rest }, { "ML6", ml6_mp2 },
	};
	return xc_run("MOVIE-AUDIO-LENGTH", cases, sizeof cases / sizeof cases[0], 60);
}
