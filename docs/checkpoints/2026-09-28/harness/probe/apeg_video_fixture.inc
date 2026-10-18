/* apeg-video: APEG pictures (src/apeg.c) and the v14 parts movie that shows
 * them. generate_apeg_fixtures.py writes small synthetic movies (48x32, 3x2
 * macroblocks; no game data) from macroblock parameters; the expected samples
 * here come from those parameters through a separate model (DC-only intra
 * block = its DC prediction, the prediction and averaging rules, a float
 * IDCT for the one block with AC coefficients, a float colour conversion),
 * never from the decoder.
 *
 * The generator and this model come from the same reading of the format, so
 * the probe shows that the two agree, not that they agree with the original
 * player: that rests on the private whole-movie comparison (the decoder against
 * a reference decoder checked against the original's screenshots).
 *
 * Some expectations are this port's choices or unproven inferences, not the
 * original's behaviour, and are marked "PORT CHOICE" or "INFERENCE" below: a
 * missing parts has ended (AV10; the original says false), ReleasePartsMovie
 * keeps the last picture (AV10; not read in the original), back colour values
 * are clamped to 0..255 (AV7; the original might keep the low 8 bits), a bad
 * index or stray bytes at the end only give a warning (AV8).
 *
 * The decoder functions are looked up with dlsym, so the fixture builds on a
 * revision without them (before-check): there every case fails.
 *
 * Cases (each forked, 25 s alarm):
 *   AV1  container: header, index, SOND, decode and presentation order
 *   AV2  I picture: DC-only blocks, quantiser bits
 *   AV3  P picture: intra, whole/half-sample vectors, skip copy, residuals
 *   AV4  B pictures: both/forward/backward/intra, skip repeat, skip to end
 *   AV5  I picture with AC coefficients (run/level and escape)
 *   AV6  picture_at: latest timestamp <= t, forward and backward seeks
 *   AV7  RGBA conversion (clamping, chroma upsampling, odd size), back colour
 *   AV8  rejected containers (by their error), containers that open with a
 *        warning, every truncation of the file and of the last picture
 *   AV9  undecodable pictures fail safely (by their error); decoding resumes
 *        at the next I; random byte flips in the pictures
 *   AV10 CreatePartsMovie/Play/IsEnd/EndTime/CurrentTime through the AIN
 *        declarations, drawn by PE_update_parts_movies with the audio clock;
 *        the picture never goes back
 *   AV11 PartsEngine.Release while playing stops the movie; re-create
 *   AV12 PlayPartsMovie start position
 *   AV13 movie_draw into a sprite (the DrawMovie route; black before play and
 *        for a sprite of another size) and the real UpdateComponent HLL
 *        drawing a parts movie
 *   AV14 open GOP: B pictures after an I but shown before it, reached from a
 *        cold start, after the I, after apeg_rewind and by going back; a B
 *        between the same references as the last one decoded is decoded alone
 *   AV15 the clock going back (a header length shorter than the sound) does
 *        not take the picture back
 */
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "parts_internal.h"
#include "frame_pacing.h"
#if __has_include("apeg.h")
#include "apeg.h"
#else
struct apeg;
struct apeg_picture {
	int index;
	uint32_t ts;
	char type;
	int width, height;
	int luma_stride, luma_rows, chroma_stride, chroma_rows;
	const int16_t *y, *cb, *cr;
};
#endif

#define AV_PI 3.14159265358979323846
#define AV_SQRT1_2 0.70710678118654752440

#ifndef XS4_APEG_FIXTURE_DIR
#error "build_probe.py must generate the synthetic APEG fixtures"
#endif

static int av_failures;
#define AV_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); av_failures++; } } while (0)

static struct {
	struct apeg *(*open)(const char *, const char **);
	struct apeg *(*open_memory)(const void *, size_t, const char **);
	void (*close)(struct apeg *);
	int (*version)(const struct apeg *);
	int (*width)(const struct apeg *);
	int (*height)(const struct apeg *);
	int (*duration)(const struct apeg *);
	int (*nr_index)(const struct apeg *);
	bool (*index_entry)(const struct apeg *, int, uint32_t *, uint32_t *);
	bool (*sound)(const struct apeg *, uint64_t *, uint32_t *);
	int (*nr_pictures)(const struct apeg *);
	bool (*picture_info)(const struct apeg *, int, char *, uint32_t *);
	int (*decode_next)(struct apeg *, const struct apeg_picture **);
	const struct apeg_picture *(*picture_at)(struct apeg *, int64_t);
	void (*rewind)(struct apeg *);
	const char *(*error)(const struct apeg *);
	void (*to_rgba)(struct apeg *, const struct apeg_picture *, uint8_t *, size_t);
	void (*ycbcr_to_rgb)(int, int, int, uint8_t *);
	void (*update_parts_movies)(void);
	int (*movie_duration)(struct movie_context *);
	const char *(*warning)(const struct apeg *);
	void (*stats)(const struct apeg *, int *, int *);
} av;

static bool av_resolve(void)
{
	static const char *names[] = { "apeg_open", "apeg_open_memory", "apeg_close", "apeg_version",
		"apeg_width", "apeg_height", "apeg_duration", "apeg_nr_index", "apeg_index_entry", "apeg_sound",
		"apeg_nr_pictures", "apeg_picture_info", "apeg_decode_next", "apeg_picture_at", "apeg_rewind",
		"apeg_error", "apeg_picture_to_rgba", "apeg_ycbcr_to_rgb", "PE_update_parts_movies",
		"movie_get_duration", "apeg_warning", "apeg_stats" };
	void **slots = (void **)&av;
	bool ok = true;
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		slots[i] = dlsym(RTLD_DEFAULT, names[i]);
		if (!slots[i]) {
			printf("  FAIL: %s is not linked\n", names[i]);
			av_failures++;
			ok = false;
		}
	}
	return ok;
}

static char *av_path(const char *name)
{
	static char buf[4096];
	snprintf(buf, sizeof buf, "%s/%s", XS4_APEG_FIXTURE_DIR, name);
	return buf;
}

static uint8_t *av_read_file(const char *name, size_t *size)
{
	FILE *f = fopen(av_path(name), "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = xmalloc(*size ? *size : 1);
	*size = fread(d, 1, *size, f);
	fclose(f);
	return d;
}

static bool av_has(const char *s, const char *word)
{
	return s && strstr(s, word);
}

static int av_decoded(struct apeg *a)
{
	int n = -1;
	av.stats(a, &n, NULL);
	return n;
}

static int av_restarts(struct apeg *a)
{
	int n = -1;
	av.stats(a, NULL, &n);
	return n;
}

static struct apeg *av_open(const char *name)
{
	const char *err = NULL;
	struct apeg *a = av.open(av_path(name), &err);
	AV_CHECK(a, "apeg_open(%s): %s", name, err ? err : "(no error)");
	return a;
}

/* ---- the model ---- */

#define AV_W16 48
#define AV_H16 32
#define AV_CW 24
#define AV_CH 16

struct av_pic {
	uint32_t ts;
	char type;
	int16_t y[AV_W16 * AV_H16], cb[AV_CW * AV_CH], cr[AV_CW * AV_CH];
	bool tolerant[AV_W16 * AV_H16];  // luma samples of the float-IDCT block
};

static void av_fill(int16_t *p, int stride, int x0, int y0, int n, int v)
{
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
			p[(y0 + y) * stride + x0 + x] = (int16_t)v;
}

/* An intra macroblock of DC-only blocks: sat(8P) is 8P +- 1, and
 * (8P +- 1) * 32 / 256 truncates back to P, so every sample is the block's
 * DC prediction P. */
static void av_intra_mb(struct av_pic *p, int mb, const int v[6])
{
	int x0 = (mb % 3) * 16, y0 = (mb / 3) * 16;
	for (int b = 0; b < 4; b++)
		av_fill(p->y, AV_W16, x0 + (b & 1) * 8, y0 + (b >> 1) * 8, 8, v[b]);
	av_fill(p->cb, AV_CW, x0 / 2, y0 / 2, 8, v[4]);
	av_fill(p->cr, AV_CW, x0 / 2, y0 / 2, 8, v[5]);
}

static int av_floor_half(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

/* Sample (x, y) of an n x n plane region predicted from ref with the
 * half-sample vector (vx, vy); averages truncate (the samples are >= 0). */
static int av_pred_sample(const int16_t *ref, int stride, int x, int y, int vx, int vy)
{
	int ix = av_floor_half(vx), iy = av_floor_half(vy), hx = vx - 2 * ix, hy = vy - 2 * iy;
	const int16_t *r = ref + (y + iy) * stride + x + ix;
	if (!hx && !hy) return r[0];
	if (hx && !hy) return (r[0] + r[1] + 1) / 2;
	if (!hx && hy) return (r[0] + r[stride] + 1) / 2;
	return (r[0] + r[1] + r[stride] + r[stride + 1] + 2) / 4;
}

/* Macroblock prediction: luma with (vx, vy), chroma with each halved toward
 * zero. ref2 (bidirectional): average of the two, rounded up at .5. */
static void av_pred_mb(struct av_pic *d, int mb, const struct av_pic *r1, int v1x, int v1y,
		       const struct av_pic *r2, int v2x, int v2y)
{
	int x0 = (mb % 3) * 16, y0 = (mb / 3) * 16;
	for (int plane = 0; plane < 3; plane++) {
		int n = plane ? 8 : 16, stride = plane ? AV_CW : AV_W16, px = plane ? x0 / 2 : x0, py = plane ? y0 / 2 : y0;
		int16_t *dp = plane == 0 ? d->y : plane == 1 ? d->cb : d->cr;
		const int16_t *a = plane == 0 ? r1->y : plane == 1 ? r1->cb : r1->cr;
		const int16_t *b = r2 ? (plane == 0 ? r2->y : plane == 1 ? r2->cb : r2->cr) : NULL;
		int ax = plane ? v1x / 2 : v1x, ay = plane ? v1y / 2 : v1y;
		int bx = plane ? v2x / 2 : v2x, by = plane ? v2y / 2 : v2y;
		for (int y = py; y < py + n; y++) {
			for (int x = px; x < px + n; x++) {
				int s = av_pred_sample(a, stride, x, y, ax, ay);
				if (b)
					s = (s + av_pred_sample(b, stride, x, y, bx, by) + 1) / 2;
				dp[y * stride + x] = (int16_t)s;
			}
		}
	}
}

/* A non-intra block with the single coefficient (0, level) at quantiser
 * scale qs (non-intra matrix 16): ((2|l| + 1) sign * 16 * qs / 16) made odd,
 * then the DC-only value 32 v / 256, truncated. */
static int av_inter_dc(int level, int qs)
{
	int v = (2 * level + (level > 0 ? 1 : -1)) * 16 * qs / 16;
	if (!(v & 1))
		v += v > 0 ? 1 : -1;
	return 32 * v / 256;
}

static void av_add_block(int16_t *p, int stride, int x0, int y0, int add)
{
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
			p[(y0 + y) * stride + x0 + x] = (int16_t)(p[(y0 + y) * stride + x0 + x] + add);
}

/* P1's macroblocks (generate_apeg_fixtures.py P1_MBS) predicted from ref. */
static void av_model_p(struct av_pic *p, const struct av_pic *ref)
{
	int v0[6] = { 200, 200, 200, 200, 60, 70 };
	av_intra_mb(p, 0, v0);
	av_pred_mb(p, 1, ref, -16, 0, NULL, 0, 0);
	av_pred_mb(p, 2, ref, -3, 1, NULL, 0, 0);
	av_pred_mb(p, 3, ref, 0, 0, NULL, 0, 0);          // skipped: copied
	av_pred_mb(p, 4, ref, 0, 0, NULL, 0, 0);
	av_add_block(p->y, AV_W16, 16, 16, av_inter_dc(1, 16));
	av_add_block(p->cr, AV_CW, 8, 8, av_inter_dc(-1, 16));
	av_pred_mb(p, 5, ref, 0, -2, NULL, 0, 0);
	av_add_block(p->y, AV_W16, 32 + 8, 16 + 8, av_inter_dc(-1, 8));
}

/* B2's macroblocks (B2_MBS) from the past and future references. */
static void av_model_b2(struct av_pic *b, const struct av_pic *past, const struct av_pic *future)
{
	av_pred_mb(b, 0, past, 1, 1, future, 2, 1);
	av_add_block(b->cb, AV_CW, 0, 0, av_inter_dc(1, 16));
	av_pred_mb(b, 1, past, 1, 1, future, 2, 1);       // skipped: same type and vectors
	av_pred_mb(b, 2, past, -2, 0, NULL, 0, 0);
	av_pred_mb(b, 3, future, 0, -1, NULL, 0, 0);
	int v4[6] = { 99, 88, 77, 66, 33, 222 };
	av_intra_mb(b, 4, v4);
	av_pred_mb(b, 5, past, 0, -2, future, -1, 0);
}

/* B3 (B3_MBS): one coded macroblock, then a skip run to the end: zero
 * vectors. */
static void av_model_b3(struct av_pic *b, const struct av_pic *past, const struct av_pic *future)
{
	av_pred_mb(b, 0, past, 2, 0, future, 0, 2);
	for (int mb = 1; mb < 6; mb++)
		av_pred_mb(b, mb, past, 0, 0, future, 0, 0);
}

static void av_build_model(struct av_pic m[5])
{
	memset(m, 0, 5 * sizeof(*m));
	struct av_pic *i0 = &m[0], *b2 = &m[1], *b3 = &m[2], *p1 = &m[3], *i4 = &m[4];  // presentation order
	i0->ts = 0; i0->type = 'I'; b2->ts = 100; b2->type = 'B'; b3->ts = 200; b3->type = 'B';
	p1->ts = 300; p1->type = 'P'; i4->ts = 400; i4->type = 'I';
	for (int mb = 0; mb < 6; mb++) {
		int v[6] = { 40 + 20 * mb, 49 + 20 * mb, 58 + 20 * mb, 67 + 20 * mb, 100 + 7 * mb, 150 - 9 * mb };
		av_intra_mb(i0, mb, v);
		int w[6] = { 200 - 15 * mb, 195 - 15 * mb, 190 - 15 * mb, 185 - 15 * mb, 60 + 11 * mb, 190 - 13 * mb };
		av_intra_mb(i4, mb, w);
	}
	// I4 macroblock 4, block Y1: DC 135 and, at quantiser scale 2 (intra
	// matrix 16), levels 12 at (0,1), -8 at (1,0), -30 at (0,2) -> odd
	// dequantised 49, -33, -121; a float IDCT, compared within 1.
	{
		double F[8][8] = { { 0 } };
		F[0][0] = 8 * 135 + 1;
		F[0][1] = 49; F[1][0] = -33; F[0][2] = -121;
		int x0 = 16 + 8, y0 = 16;
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 8; x++) {
				double s = 0;
				for (int v = 0; v < 8; v++)
					for (int u = 0; u < 8; u++)
						s += (u ? 1 : AV_SQRT1_2) * (v ? 1 : AV_SQRT1_2) * F[v][u]
							* cos((2 * x + 1) * u * AV_PI / 16) * cos((2 * y + 1) * v * AV_PI / 16);
				i4->y[(y0 + y) * AV_W16 + x0 + x] = (int16_t)lround(s / 4);
				i4->tolerant[(y0 + y) * AV_W16 + x0 + x] = true;
			}
		}
	}
	av_model_p(p1, i0);         // P1 from I0
	av_model_b2(b2, i0, p1);    // B2, B3: past I0, future P1
	av_model_b3(b3, i0, p1);
}

/* open-gop.apeg in presentation order: I0 B100 B200 P300 B400 B500 I600 B700
 * B800 P900. The second GOP is open: B400 and B500 come after I600 in decode
 * order, with P300 of the first GOP as their past reference. */
#define AV_GOP_N 10
static const int av_gop_slot[AV_GOP_N] = { 0, 3, 1, 2, 6, 4, 5, 9, 7, 8 };  // decode order -> presentation

static void av_build_gop_model(struct av_pic m[AV_GOP_N])
{
	struct av_pic *first = xcalloc(5, sizeof(struct av_pic));
	av_build_model(first);
	memset(m, 0, AV_GOP_N * sizeof(*m));
	for (int k = 0; k < 4; k++)
		m[k] = first[k];  // I0 B100 B200 P300
	free(first);
	static const char types[] = "IBBPBBIBBP";
	for (int k = 0; k < AV_GOP_N; k++) {
		m[k].ts = 100 * k;
		m[k].type = types[k];
	}
	struct av_pic *p3 = &m[3], *i6 = &m[6], *p9 = &m[9];
	for (int mb = 0; mb < 6; mb++) {
		int v[6] = { 30 + 25 * mb, 36 + 25 * mb, 42 + 25 * mb, 48 + 25 * mb, 170 - 8 * mb, 80 + 12 * mb };
		av_intra_mb(i6, mb, v);
	}
	av_model_b2(&m[4], p3, i6);  // B400, B500: past P300, future I600
	av_model_b3(&m[5], p3, i6);
	av_model_p(p9, i6);          // P900 from I600
	av_model_b2(&m[7], i6, p9);  // B700, B800: past I600, future P900
	av_model_b3(&m[8], i6, p9);
}

static int av_compare(const struct apeg_picture *p, const struct av_pic *m, const char *what)
{
	int bad = 0, first = -1;
	if (!p || p->luma_stride != AV_W16 || p->luma_rows != AV_H16 || p->chroma_stride != AV_CW || p->chroma_rows != AV_CH) {
		AV_CHECK(false, "%s: no picture or wrong plane layout", what);
		return 1;
	}
	for (int i = 0; i < AV_W16 * AV_H16; i++) {
		int d = abs(p->y[i] - m->y[i]);
		if (m->tolerant[i] ? d > 1 : d != 0) { bad++; if (first < 0) first = i; }
	}
	for (int i = 0; i < AV_CW * AV_CH; i++) {
		if (p->cb[i] != m->cb[i] || p->cr[i] != m->cr[i]) { bad++; if (first < 0) first = 100000 + i; }
	}
	AV_CHECK(p->ts == m->ts && p->type == m->type, "%s: picture ts=%u %c, want %u %c", what, p->ts, p->type, m->ts, m->type);
	if (bad) {
		if (first < 100000)
			AV_CHECK(false, "%s: %d samples differ; first Y(%d,%d) = %d, model %d", what, bad,
				 first % AV_W16, first / AV_W16, p->y[first], m->y[first]);
		else
			AV_CHECK(false, "%s: %d samples differ; first chroma (%d,%d) = %d/%d, model %d/%d", what, bad,
				 (first - 100000) % AV_CW, (first - 100000) / AV_CW, p->cb[first - 100000],
				 p->cr[first - 100000], m->cb[first - 100000], m->cr[first - 100000]);
	}
	return bad;
}

/* Colour model: Y clamped to 16..235, Cb/Cr to 16..240, chroma bilinear at
 * (x/2 - 1/4, y/2 - 1/4) with clamped edges, the shader matrix, rounded. */
static void av_model_rgb(const int16_t *Y, const int16_t *Cb, const int16_t *Cr, int w16, int h16,
			 int w, int h, uint8_t *out /* w*h*3 */)
{
	static const double M[3][3] = { { 1.16364, -0.00275, 1.59907 }, { 1.16364, -0.39128, -0.81847 },
		{ 1.16364, 2.02626, 0.00177 } };
	static const double O[3] = { -0.87971, 0.53433, -1.09726 };
	int cw = w16 / 2, ch = h16 / 2;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			double c[2];
			for (int k = 0; k < 2; k++) {
				const int16_t *P = k ? Cr : Cb;
				double sx = x / 2.0 - 0.25, sy = y / 2.0 - 0.25;
				int xa = (int)floor(sx), ya = (int)floor(sy);
				double fx = sx - xa, fy = sy - ya, s = 0;
				for (int j = 0; j < 2; j++) {
					for (int i = 0; i < 2; i++) {
						int xx = xa + i < 0 ? 0 : xa + i >= cw ? cw - 1 : xa + i;
						int yy = ya + j < 0 ? 0 : ya + j >= ch ? ch - 1 : ya + j;
						int v = P[yy * cw + xx];
						v = v < 16 ? 16 : v > 240 ? 240 : v;
						s += (i ? fx : 1 - fx) * (j ? fy : 1 - fy) * v;
					}
				}
				c[k] = s / 255;
			}
			int yv = Y[y * w16 + x];
			yv = yv < 16 ? 16 : yv > 235 ? 235 : yv;
			for (int k = 0; k < 3; k++) {
				double v = M[k][0] * yv / 255 + M[k][1] * c[0] + M[k][2] * c[1] + O[k];
				v = v < 0 ? 0 : v > 1 ? 1 : v;
				out[(y * w + x) * 3 + k] = (uint8_t)lround(v * 255);
			}
		}
	}
}

static int av_rgba_diff_tol(const uint8_t *rgba, const uint8_t *rgb, int n, int *maxd, int tol)
{
	int bad = 0;
	*maxd = 0;
	for (int i = 0; i < n; i++) {
		for (int k = 0; k < 3; k++) {
			int d = abs(rgba[i * 4 + k] - rgb[i * 3 + k]);
			if (d > *maxd) *maxd = d;
			if (d > tol) bad++;
		}
		if (rgba[i * 4 + 3] != 255) bad++;
	}
	return bad;
}

static int av_rgba_diff(const uint8_t *rgba, const uint8_t *rgb, int n, int *maxd)
{
	return av_rgba_diff_tol(rgba, rgb, n, maxd, 1);
}

/* ---- decoder cases ---- */

static void av1_container(void)
{
	if (!av_resolve())
		return;
	struct apeg *a = av_open("video.apeg");
	if (!a)
		return;
	AV_CHECK(av.version(a) == 5 && av.width(a) == 48 && av.height(a) == 32 && av.duration(a) == 900,
		 "header: version %d, %dx%d, duration %d", av.version(a), av.width(a), av.height(a), av.duration(a));
	AV_CHECK(!av.warning(a), "video.apeg opened with a warning: %s", av.warning(a));
	size_t size;
	uint8_t *file = av_read_file("video.apeg", &size);
	uint32_t ms = 1, off = 0;
	AV_CHECK(av.nr_index(a) == 2, "index entries %d, want 2", av.nr_index(a));
	for (int i = 0; i < 2; i++) {
		bool ok = av.index_entry(a, i, &ms, &off);
		AV_CHECK(ok && ms == (i ? 400u : 0u) && off + 4 <= size && !memcmp(file + off, "IPIC", 4),
			 "index %d: ms %u, offset %u is not an IPIC", i, ms, off);
	}
	AV_CHECK(!av.index_entry(a, 2, &ms, &off) && !av.index_entry(a, -1, &ms, &off), "index past the end accepted");
	uint64_t so = 0;
	uint32_t ss = 0;
	bool snd = av.sound(a, &so, &ss);
	AV_CHECK(snd && so >= 8 && so + ss <= size && !memcmp(file + so - 8, "SOND", 4) && !memcmp(file + so, "OggS", 4)
		 && (file[so - 4] | file[so - 3] << 8 | file[so - 2] << 16 | (uint32_t)file[so - 1] << 24) == ss,
		 "SOND chunk at %llu size %u not found", (unsigned long long)so, ss);
	static const char types[] = "IPBBI";
	static const uint32_t ts[] = { 0, 300, 100, 200, 400 };
	AV_CHECK(av.nr_pictures(a) == 5, "pictures %d, want 5", av.nr_pictures(a));
	for (int i = 0; i < 5; i++) {
		char t = 0;
		uint32_t s = 0;
		AV_CHECK(av.picture_info(a, i, &t, &s) && t == types[i] && s == ts[i],
			 "decode order %d: %c %u, want %c %u", i, t, s, types[i], ts[i]);
	}
	AV_CHECK(!av.picture_info(a, 5, NULL, NULL) && !av.picture_info(a, -1, NULL, NULL), "picture past the end accepted");
	// Presentation order: the decoder reports what it decodes in decode order.
	const struct apeg_picture *p;
	for (int i = 0; i < 5; i++) {
		int r = av.decode_next(a, &p);
		AV_CHECK(r == 1 && p->index == i && p->ts == ts[i] && p->type == types[i]
			 && p->width == 48 && p->height == 32, "decode_next %d: r=%d", i, r);
	}
	AV_CHECK(av.decode_next(a, &p) == 0, "decode_next past the end");
	free(file);
	av.close(a);
}

static void av_decode_all(struct apeg *a, uint32_t ts[5], char type[5],
			  int16_t (*keep)[AV_W16 * AV_H16 + 2 * AV_CW * AV_CH])
{
	const struct apeg_picture *p;
	for (int i = 0; i < 5; i++) {
		int r = av.decode_next(a, &p);
		AV_CHECK(r == 1, "decode_next %d failed: %s", i, av.error(a) ? av.error(a) : "-");
		if (r != 1)
			continue;
		ts[i] = p->ts;
		type[i] = p->type;
		memcpy(keep[i], p->y, AV_W16 * AV_H16 * 2);
		memcpy(keep[i] + AV_W16 * AV_H16, p->cb, AV_CW * AV_CH * 2);
		memcpy(keep[i] + AV_W16 * AV_H16 + AV_CW * AV_CH, p->cr, AV_CW * AV_CH * 2);
	}
}

/* Checks decode-order picture i (model presentation slot m). */
static void av_check_picture(int i, int m, const char *what)
{
	if (!av_resolve())
		return;
	struct av_pic *model = xcalloc(5, sizeof(struct av_pic));
	av_build_model(model);
	struct apeg *a = av_open("video.apeg");
	if (a) {
		int16_t (*keep)[AV_W16 * AV_H16 + 2 * AV_CW * AV_CH] = xcalloc(5, sizeof(*keep));
		uint32_t ts[5] = { 0 };
		char types[5] = { 0 };
		av_decode_all(a, ts, types, keep);
		// The planes kept from decode_next, with the ts and type it reported.
		struct apeg_picture p = { .index = i, .ts = ts[i], .type = types[i], .width = 48, .height = 32,
			.luma_stride = AV_W16, .luma_rows = AV_H16, .chroma_stride = AV_CW, .chroma_rows = AV_CH,
			.y = keep[i], .cb = keep[i] + AV_W16 * AV_H16, .cr = keep[i] + AV_W16 * AV_H16 + AV_CW * AV_CH };
		int bad = av_compare(&p, &model[m], what);
		printf("  %s: %d samples differ from the model\n", what, bad);
		free(keep);
		av.close(a);
	}
	free(model);
}

static void av2_i_picture(void) { av_check_picture(0, 0, "I0"); }
static void av3_p_picture(void) { av_check_picture(1, 3, "P1"); }
static void av4_b_pictures(void) { av_check_picture(2, 1, "B2"); av_check_picture(3, 2, "B3"); }

static void av5_ac_block(void)
{
	av_check_picture(4, 4, "I4");
	if (!av.open)
		return;
	struct apeg *a = av_open("video.apeg");
	const struct apeg_picture *p = a ? av.picture_at(a, 400) : NULL;
	if (p) {
		// The block varies along both axes, as the model does.
		int lo = 255, hi = 0, row0 = 0, col0 = 0;
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++) {
				int v = p->y[(16 + y) * AV_W16 + 24 + x];
				lo = v < lo ? v : lo; hi = v > hi ? v : hi;
			}
		row0 = p->y[16 * AV_W16 + 24] - p->y[16 * AV_W16 + 31];
		col0 = p->y[16 * AV_W16 + 24] - p->y[23 * AV_W16 + 24];
		AV_CHECK(hi - lo > 30 && row0 && col0, "AC block too flat: %d..%d, row %d, column %d", lo, hi, row0, col0);
	}
	if (a)
		av.close(a);
}

static void av6_picture_at(void)
{
	if (!av_resolve())
		return;
	struct av_pic *model = xcalloc(5, sizeof(struct av_pic));
	av_build_model(model);
	struct apeg *a = av_open("video.apeg");
	if (a) {
		const struct apeg_picture *p = av.picture_at(a, -1);
		AV_CHECK(!p && !av.error(a), "t=-1: a picture or an error before the first picture");
		static const struct { int64_t t; int m; } want[] = {
			{ 0, 0 }, { 99, 0 }, { 100, 1 }, { 150, 1 }, { 200, 2 }, { 299, 2 }, { 300, 3 }, { 399, 3 },
			{ 400, 4 }, { 100000, 4 }, { 150, 1 }, { 0, 0 }, { 350, 3 }, { 210, 2 }, { 120, 1 }, { 401, 4 },
			{ 5, 0 }, { 300, 3 }, { 100, 1 } };
		for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
			p = av.picture_at(a, want[i].t);
			char what[64];
			snprintf(what, sizeof what, "picture_at(%lld)", (long long)want[i].t);
			if (!p) {
				AV_CHECK(false, "%s: NULL (%s)", what, av.error(a) ? av.error(a) : "-");
				continue;
			}
			av_compare(p, &model[want[i].m], what);
		}
		// The same picture again: no decoding.
		int before = av_decoded(a);
		const struct apeg_picture *q = av.picture_at(a, 101);
		AV_CHECK(q && q->ts == 100 && av_decoded(a) == before, "picture_at(101): ts %d, %d pictures decoded for it",
			 q ? (int)q->ts : -1, av_decoded(a) - before);
		av.rewind(a);
		p = av.picture_at(a, 250);
		AV_CHECK(p && p->ts == 200, "after rewind: picture_at(250) ts %d", p ? (int)p->ts : -1);
		av.close(a);
	}
	free(model);
}

static void av7_colour(void)
{
	if (!av_resolve())
		return;
	struct av_pic *model = xcalloc(5, sizeof(struct av_pic));
	av_build_model(model);
	struct apeg *a = av_open("video.apeg");
	uint8_t rgba[48 * 32 * 4], rgb[48 * 32 * 3];
	for (int m = 0; a && m < 5; m++) {
		const struct apeg_picture *p = av.picture_at(a, model[m].ts);
		if (!p)
			continue;
		memset(rgba, 0, sizeof rgba);
		av.to_rgba(a, p, rgba, 48 * 4);
		av_model_rgb(p->y, p->cb, p->cr, AV_W16, AV_H16, 48, 32, rgb);
		int maxd, bad = av_rgba_diff(rgba, rgb, 48 * 32, &maxd);
		AV_CHECK(!bad, "RGBA of ts %u: %d values off by more than 1 (max %d)", model[m].ts, bad, maxd);
		printf("  RGBA ts %u: max difference from the float model %d\n", model[m].ts, maxd);
	}
	if (a)
		av.close(a);
	// Odd size: 40x24 shown of 48x32, samples outside the clamping ranges;
	// the pitch is wider than a row (the extra bytes stay untouched).
	a = av_open("odd.apeg");
	const struct apeg_picture *p = a ? av.picture_at(a, 0) : NULL;
	AV_CHECK(!a || (p && p->width == 40 && p->height == 24 && p->luma_stride == 48 && p->luma_rows == 32),
		 "odd.apeg: picture %dx%d of %dx%d", p ? p->width : -1, p ? p->height : -1, p ? p->luma_stride : -1, p ? p->luma_rows : -1);
	if (p && p->width == 40) {
		bool lo = false, hi = false;
		for (int i = 0; i < 48 * 32; i++) { lo |= p->y[i] < 0; hi |= p->y[i] > 235; }
		AV_CHECK(lo && hi, "odd.apeg: planes are not kept unclamped (negative %d, above 235 %d)", lo, hi);
		uint8_t out[24][44 * 4], want[40 * 24 * 3];
		memset(out, 0xab, sizeof out);
		av.to_rgba(a, p, &out[0][0], 44 * 4);
		av_model_rgb(p->y, p->cb, p->cr, 48, 32, 40, 24, want);
		uint8_t packed[40 * 24 * 4];
		bool pad = true;
		for (int y = 0; y < 24; y++) {
			memcpy(packed + y * 160, out[y], 160);
			for (int k = 160; k < 176; k++) pad &= out[y][k] == 0xab;
		}
		int maxd, bad = av_rgba_diff(packed, want, 40 * 24, &maxd);
		AV_CHECK(!bad && pad, "odd.apeg RGBA: %d values off (max %d), pitch padding kept %d", bad, maxd, pad);
	}
	if (a)
		av.close(a);
	// Back colours: Y/Cb/Cr through the same matrix, without the 16..235
	// clamp. INFERENCE: values outside 0..255 (the last one) are clamped; the
	// original stores them in bytes, which might keep the low 8 bits instead.
	static const struct { int y, cb, cr; } cols[] = { { 0, 128, 128 }, { 255, 128, 128 }, { 81, 90, 240 }, { 145, 54, 34 }, { 300, -5, 128 } };
	for (size_t i = 0; i < sizeof(cols) / sizeof(cols[0]); i++) {
		uint8_t got[3];
		av.ycbcr_to_rgb(cols[i].y, cols[i].cb, cols[i].cr, got);
		int16_t Y = (int16_t)(cols[i].y < 0 ? 0 : cols[i].y > 255 ? 255 : cols[i].y);
		int16_t Cb = (int16_t)(cols[i].cb < 0 ? 0 : cols[i].cb > 255 ? 255 : cols[i].cb);
		int16_t Cr = (int16_t)(cols[i].cr < 0 ? 0 : cols[i].cr > 255 ? 255 : cols[i].cr);
		// No 16..235 clamp for a fill colour: compute the matrix directly.
		static const double M[3][3] = { { 1.16364, -0.00275, 1.59907 }, { 1.16364, -0.39128, -0.81847 },
			{ 1.16364, 2.02626, 0.00177 } };
		static const double O[3] = { -0.87971, 0.53433, -1.09726 };
		for (int k = 0; k < 3; k++) {
			double v = (M[k][0] * Y + M[k][1] * Cb + M[k][2] * Cr) / 255 + O[k];
			int w = (int)lround((v < 0 ? 0 : v > 1 ? 1 : v) * 255);
			AV_CHECK(abs(got[k] - w) <= 1, "back colour (%d,%d,%d) channel %d: %d, want %d", cols[i].y, cols[i].cb, cols[i].cr, k, got[k], w);
		}
	}
	uint8_t black[3];
	av.ycbcr_to_rgb(0, 128, 128, black);
	AV_CHECK(!black[0] && !black[1] && !black[2], "the game's black back colour is (%d,%d,%d)", black[0], black[1], black[2]);
	free(model);
}

/* The chunk boundaries of a file: data offset, then after every chunk. */
static int av_boundaries(const uint8_t *f, size_t size, size_t *b, int max, int *pics_before)
{
	size_t pos = 8 + (f[4] | f[5] << 8 | f[6] << 16 | (size_t)f[7] << 24);
	int n = 0, pics = 0;
	while (n < max) {
		b[n] = pos;
		pics_before[n] = pics;
		n++;
		if (pos + 8 > size)
			break;
		uint32_t len = f[pos + 4] | f[pos + 5] << 8 | f[pos + 6] << 16 | (uint32_t)f[pos + 7] << 24;
		pics += !memcmp(f + pos, "IPIC", 4) || !memcmp(f + pos, "PPIC", 4) || !memcmp(f + pos, "BPIC", 4);
		pos += 8 + (size_t)len;
	}
	return n;
}

static void av8_rejected(void)
{
	if (!av_resolve())
		return;
	static const struct { const char *name, *word; } bad[] = {
		{ "bad-magic.apeg", "magic" }, { "version-4.apeg", "version" }, { "version-0.apeg", "version" },
		{ "short-header.apeg", "header truncated" }, { "data-offset.apeg", "data offset" },
		{ "chunk-truncated.apeg", "chunk truncated" }, { "zero-width.apeg", "picture size" },
		{ "missing.apeg", "cannot open" } };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		const char *err = NULL;
		struct apeg *a = av.open(av_path(bad[i].name), &err);
		AV_CHECK(!a && av_has(err, bad[i].word), "%s: opened or not for its error (%s; want \"%s\")",
			 bad[i].name, err ? err : "none", bad[i].word);
		printf("  %s: %s\n", bad[i].name, err ? err : "(none)");
		if (a)
			av.close(a);
	}
	// A bad index or stray bytes at the end: opened, with a warning; the
	// pictures are found by walking the chunks. PORT CHOICE: what the
	// original does with such files was not read.
	static const struct { const char *name, *word; int nr_index; } warn[] = {
		{ "index-count.apeg", "index", 0 }, { "index-offset.apeg", "index", 0 },
		{ "trailing-bytes.apeg", "after the last chunk", 2 } };
	for (size_t i = 0; i < sizeof(warn) / sizeof(warn[0]); i++) {
		const char *err = NULL;
		struct apeg *a = av.open(av_path(warn[i].name), &err);
		int ok = 0;
		const struct apeg_picture *p;
		for (int k = 0; a && k < 5; k++)
			ok += av.decode_next(a, &p) == 1;
		AV_CHECK(a && av_has(av.warning(a), warn[i].word) && av.nr_index(a) == warn[i].nr_index
			 && av.nr_pictures(a) == 5 && ok == 5,
			 "%s: opened %d, warning \"%s\" (want \"%s\"), index %d, %d pictures decoded", warn[i].name,
			 a != NULL, a && av.warning(a) ? av.warning(a) : "-", warn[i].word, a ? av.nr_index(a) : -1, ok);
		printf("  %s: %s\n", warn[i].name, a && av.warning(a) ? av.warning(a) : err ? err : "(none)");
		if (a)
			av.close(a);
	}
	// Every truncation of the file opens exactly when it ends at a chunk
	// boundary or less than a chunk header after one (then with a warning),
	// with the pictures before it; the last picture cut short (its length
	// patched to match) never decodes.
	size_t size;
	uint8_t *file = av_read_file("video.apeg", &size);
	if (!file) {
		AV_CHECK(false, "cannot read video.apeg");
		return;
	}
	size_t bnd[16];
	int pics_before[16];
	int nb = av_boundaries(file, size, bnd, 16, pics_before);
	struct apeg *a = av_open("video.apeg");
	uint32_t last_off = 0;
	if (a) {
		av.index_entry(a, 1, NULL, &last_off);  // I4 is the last chunk
		av.close(a);
	}
	int opened = 0, cut_open = 0, cut_decoded = 0, cuts = 0, wrong = 0, tried = 0;
	for (int pass = 0; pass < 2; pass++) {
		// Pass 0: a sweep; pass 1: every boundary and the 7 bytes after it.
		size_t n = 0, end = pass ? (size_t)nb * 8 : size;
		for (size_t k = 0; k < end; ) {
			n = pass ? bnd[k / 8] + k % 8 : k;
			k += pass ? 1 : (k < 0x400 || k + 0x200 > size ? 1 : 29);
			if (n >= size)
				continue;
			tried++;
			int j = -1;
			for (int q = 0; q < nb; q++)
				if (bnd[q] <= n)
					j = q;
			bool want = j >= 0 && n - bnd[j] < 8;
			const char *err = NULL;
			struct apeg *t = av.open_memory(file, n, &err);
			bool right = want ? t && av.nr_pictures(t) == pics_before[j] && (n == bnd[j] || av.warning(t))
					  : !t && err;
			if (!right && wrong++ < 5)
				printf("  truncation at %zu: opened %d (want %d), %d pictures (want %d), warning %s, error %s\n", n,
				       t != NULL, want, t ? av.nr_pictures(t) : -1, j >= 0 ? pics_before[j] : -1,
				       t && av.warning(t) ? av.warning(t) : "-", err ? err : "-");
			if (t) {
				opened++;
				const struct apeg_picture *p;
				while (av.decode_next(t, &p) != 0) {}
				av.close(t);
			}
		}
	}
	AV_CHECK(!wrong, "%d of %d truncations opened when they should not, or the other way round", wrong, tried);
	for (size_t n = last_off + 8; last_off && n < size; n++) {
		uint8_t *t = xmalloc(n);
		memcpy(t, file, n);
		uint32_t len = (uint32_t)(n - last_off - 8);
		memcpy(t + last_off + 4, &len, 4);
		const char *err = NULL;
		struct apeg *b = av.open_memory(t, n, &err);
		cuts++;
		if (b) {
			cut_open++;
			const struct apeg_picture *p;
			int ok = 0;
			for (int i = 0; i < 5; i++)
				ok += av.decode_next(b, &p) == 1;
			cut_decoded += ok == 5;
			AV_CHECK(ok == 4 && av.error(b), "last picture cut to %u bytes: %d pictures decoded", len, ok);
			AV_CHECK(!av.picture_at(b, 500) && av_has(av.error(b), "cannot be decoded"),
				 "cut to %u: picture_at shows a broken picture (%s)", len, av.error(b) ? av.error(b) : "-");
			av.close(b);
		} else {
			AV_CHECK(len < 4 && av_has(err, "timestamp"), "cut to %u bytes: not opened (%s)", len, err ? err : "-");
		}
		free(t);
	}
	printf("  %d truncations (%d chunk boundaries) opened %d; %d cuts of the last picture, %d opened, %d fully decoded\n",
	       tried, nb, opened, cuts, cut_open, cut_decoded);
	AV_CHECK(cuts > 20 && cut_decoded == 0, "last-picture truncations: %d cuts, %d decoded", cuts, cut_decoded);
	free(file);
}

static uint32_t av_rand_state = 12345;
static uint32_t av_rand(void)
{
	av_rand_state ^= av_rand_state << 13;
	av_rand_state ^= av_rand_state >> 17;
	av_rand_state ^= av_rand_state << 5;
	return av_rand_state;
}

static void av9_undecodable(void)
{
	if (!av_resolve())
		return;
	// The error says which check failed; afterwards picture_at fails at once
	// for bad data, and for the P without an I before it.
	static const struct { const char *name; int fail_at; const char *word, *again; } files[] = {
		{ "p-first.apeg", 0, "without a reference", "no I picture" },
		{ "i-truncated.apeg", 0, "truncated", "cannot be decoded" },
		{ "bad-cbp.apeg", 1, "coded block pattern", "cannot be decoded" },
		{ "coef-63.apeg", 0, "past 63", "cannot be decoded" },
		{ "skip-past.apeg", 1, "skip run", "cannot be decoded" } };
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		struct apeg *a = av_open(files[i].name);
		if (!a)
			continue;
		const struct apeg_picture *p;
		int n = av.nr_pictures(a);
		for (int k = 0; k < n; k++) {
			int r = av.decode_next(a, &p);
			if (k == files[i].fail_at)
				AV_CHECK(r == -1 && av_has(av.error(a), files[i].word), "%s picture %d: r=%d (%s), want a failure for \"%s\"",
					 files[i].name, k, r, av.error(a) ? av.error(a) : "-", files[i].word);
			else
				AV_CHECK(r == 1, "%s picture %d: r=%d (%s)", files[i].name, k, r, av.error(a) ? av.error(a) : "-");
			if (r == -1)
				printf("  %s picture %d: %s\n", files[i].name, k, av.error(a));
		}
		uint32_t ts = 0;
		av.picture_info(a, files[i].fail_at, NULL, &ts);
		int before = av_decoded(a);
		AV_CHECK(!av.picture_at(a, ts) && av_has(av.error(a), files[i].again) && av_decoded(a) == before,
			 "%s: picture_at(%u): %s, %d pictures decoded (want \"%s\" at once)", files[i].name, ts,
			 av.error(a) ? av.error(a) : "no failure", av_decoded(a) - before, files[i].again);
		av.close(a);
	}
	// A broken I, a P on it, a good I: the P fails, the second I decodes.
	struct apeg *a = av_open("broken-chain.apeg");
	if (a) {
		const struct apeg_picture *p;
		int r0 = av.decode_next(a, &p);
		bool e0 = av_has(av.error(a), "truncated");
		int r1 = av.decode_next(a, &p);
		bool e1 = av_has(av.error(a), "without a reference");
		int r2 = av.decode_next(a, &p);
		AV_CHECK(r0 == -1 && e0 && r1 == -1 && e1 && r2 == 1 && p->ts == 200, "broken chain: %d %d %d", r0, r1, r2);
		AV_CHECK(!av.picture_at(a, 150) && av_has(av.error(a), "reference picture cannot be decoded"),
			 "picture_at(150) shows the P of a broken I (%s)", av.error(a) ? av.error(a) : "-");
		const struct apeg_picture *q = av.picture_at(a, 250);
		AV_CHECK(q && q->ts == 200 && !av.error(a), "picture_at(250) after a broken chain");
		AV_CHECK(!av.picture_at(a, 0) && av_has(av.error(a), "cannot be decoded"), "picture_at(0) shows the broken I");
		av.close(a);
	}
	// Random byte flips in the pictures (everything after the first IPIC).
	size_t size;
	uint8_t *file = av_read_file("video.apeg", &size);
	a = file ? av_open("video.apeg") : NULL;
	uint32_t first = 0;
	if (a) {
		av.index_entry(a, 0, NULL, &first);
		av.close(a);
	}
	int opened = 0, decoded = 0, failed = 0, shown = 0;
	uint8_t *t = file ? xmalloc(size) : NULL, *rgba = xmalloc(48 * 32 * 4);
	for (int it = 0; first && it < 1500; it++) {
		memcpy(t, file, size);
		int nflip = 1 + av_rand() % 4;
		for (int k = 0; k < nflip; k++)
			t[first + av_rand() % (size - first)] ^= (uint8_t)(1 + av_rand() % 255);
		struct apeg *b = av.open_memory(t, size, NULL);
		if (!b)
			continue;
		opened++;
		const struct apeg_picture *p;
		int r;
		while ((r = av.decode_next(b, &p)) != 0) {
			if (r == 1) { decoded++; av.to_rgba(b, p, rgba, 48 * 4); }
			else failed++;
		}
		for (int k = 0; k < 6; k++) {
			p = av.picture_at(b, (int64_t)(av_rand() % 600) - 50);
			if (p) { shown++; av.to_rgba(b, p, rgba, 48 * 4); }
		}
		av.close(b);
	}
	printf("  byte flips: %d files opened, %d pictures decoded, %d failed, %d shown\n", opened, decoded, failed, shown);
	AV_CHECK(opened > 500 && failed > 50 && decoded > 50, "byte flips did not exercise both outcomes");
	free(t);
	free(rgba);
	free(file);
}

/* ---- parts movie cases ---- */

static int av_lib(const char *name) { int l = ain_get_library(ain, name); assert(l >= 0); return l; }

static union vm_value av_pe(const char *fn, int n, const union vm_value *args)
{
	return bu_call("PartsEngine", fn, n, 0, n, args);
}

#define AV_I(v) ((union vm_value){ .i = (v) })

static bool av_create(int number, const char *file, int y, int cb, int cr)
{
	int s = heap_alloc_string(make_string(file, strlen(file)));
	union vm_value args[8] = { AV_I(number), AV_I(s), AV_I(1000), AV_I(1), AV_I(y), AV_I(cb), AV_I(cr), AV_I(1) };
	bool r = av_pe("CreatePartsMovie", 8, args).i;
	heap_unref(s);
	return r;
}

static Texture *av_texture(int number)
{
	struct parts *p = parts_try_get(number);
	if (!p || p->states[0].type != PARTS_MOVIE || !p->states[0].common.texture.handle)
		return NULL;
	return &p->states[0].common.texture;
}

/* Which of the five model pictures (presentation order) the texture shows;
 * 5: the back colour; -1: none. Only identifies the picture (within 3: the
 * model's float IDCT block may be one off, a sample of Y one more); AV2-AV7
 * check the values. */
static int av_match(Texture *tex, const uint8_t rgb[6][48 * 32 * 3])
{
	uint8_t *px = gfx_get_pixels(tex);
	int found = -1;
	for (int m = 0; m < 6 && found < 0; m++) {
		int maxd;
		if (!av_rgba_diff_tol(px, rgb[m], 48 * 32, &maxd, 3))
			found = m;
	}
	free(px);
	return found;
}

static int av_due(int64_t t)
{
	static const int ts[5] = { 0, 100, 200, 300, 400 };
	int d = -1;
	for (int i = 0; i < 5; i++)
		if (ts[i] <= t)
			d = i;
	return d;
}

static bool av_parts_setup(CGLContextObj *ctx, uint8_t rgb[6][48 * 32 * 3], int by, int bcb, int bcr)
{
	*ctx = ma_gl_setup();
	if (!*ctx) {
		av_failures++;
		return false;
	}
	if (!ma_audio_setup())
		return false;
	ri_postlink();
	config.game_dir = XS4_APEG_FIXTURE_DIR;
	struct av_pic *model = xcalloc(5, sizeof(struct av_pic));
	av_build_model(model);
	for (int m = 0; m < 5; m++)
		av_model_rgb(model[m].y, model[m].cb, model[m].cr, AV_W16, AV_H16, 48, 32, rgb[m]);
	free(model);
	// Back colour model: the fill colour through the matrix.
	static const double M[3][3] = { { 1.16364, -0.00275, 1.59907 }, { 1.16364, -0.39128, -0.81847 },
		{ 1.16364, 2.02626, 0.00177 } };
	static const double O[3] = { -0.87971, 0.53433, -1.09726 };
	for (int k = 0; k < 3; k++) {
		double v = (M[k][0] * by + M[k][1] * bcb + M[k][2] * bcr) / 255 + O[k];
		uint8_t c = (uint8_t)lround((v < 0 ? 0 : v > 1 ? 1 : v) * 255);
		for (int i = 0; i < 48 * 32; i++)
			rgb[5][i * 3 + k] = c;
	}
	return true;
}

static void av_parts_teardown(CGLContextObj ctx)
{
	SDL_Quit();
	if (ctx) {
		CGLSetCurrentContext(NULL);
		CGLDestroyContext(ctx);
	}
}

static void av10_parts_movie(void)
{
	bool linked = av_resolve();
	CGLContextObj ctx = NULL;
	static uint8_t rgb[6][48 * 32 * 3];
	if (!av_parts_setup(&ctx, rgb, 81, 90, 240)) {
		av_parts_teardown(ctx);
		return;
	}
	const int N = 7001;
	AV_CHECK(!av_create(N, "missing.apeg", 0, 128, 128) && !parts_try_get(N), "a missing file made a movie");
	bool created = av_create(N, "video.apeg", 81, 90, 240);
	Texture *tex = av_texture(N);
	AV_CHECK(created && tex && tex->w == 48 && tex->h == 32, "CreatePartsMovie: %d, movie parts with a 48x32 surface: %s",
		 created, tex ? "yes" : "no");
	if (!tex || !linked) {
		av_parts_teardown(ctx);
		return;
	}
	AV_CHECK(av_match(tex, rgb) == 5, "before play: the surface is not the back colour");
	av.update_parts_movies();
	AV_CHECK(av_match(tex, rgb) == 5, "before play: an update drew a picture");
	AV_CHECK(av_pe("GetPartsMovieEndTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i == 900, "GetPartsMovieEndTime is not the header's 900");
	AV_CHECK(!av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i, "ended before play");
	AV_CHECK(av_pe("PlayPartsMovie", 3, (union vm_value[]){ AV_I(N), AV_I(0), AV_I(1) }).i, "PlayPartsMovie failed");
	bool seen[6] = { false };
	int order_bad = 0, last = -1, updates = 0, backwards = 0, clock_back = 0, prev_t = -1, seen_max = INT_MIN;
	Uint64 start = SDL_GetTicks64();
	while (!av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i && SDL_GetTicks64() - start < 6000) {
		int t0 = av_pe("GetPartsMovieCurrentTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i;
		av.update_parts_movies();
		int t1 = av_pe("GetPartsMovieCurrentTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i;
		int m = av_match(tex, rgb);
		updates++;
		// The picture due at the clock read just before or after the update.
		// The clock may step back by a few ms between callbacks, and the
		// pictures follow the latest time so far: so the latest reading so
		// far, give or take AV_JITTER ms.
		enum { AV_JITTER = 5 };
		int lo = t0 < t1 ? t0 : t1, hi = t0 < t1 ? t1 : t0;
		seen_max = hi > seen_max ? hi : seen_max;
		int due_lo = av_due(lo - AV_JITTER), due_hi = av_due(seen_max + AV_JITTER);
		if (m < 0 || m == 5 || m < due_lo || m > due_hi) {
			if (order_bad++ < 5)
				printf("  update at %d..%d ms shows %d (due %d..%d)\n", t0, t1, m, due_lo, due_hi);
		}
		clock_back += t0 < prev_t;
		prev_t = t1;
		if (m >= 0 && m < 5) {
			seen[m] = true;
			backwards += m < last;
			last = m;
		}
		SDL_Delay(4);
	}
	printf("  clock stepped back %d times, picture went back %d times\n", clock_back, backwards);
	bool ended = av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i;
	int end_time = av_pe("GetPartsMovieCurrentTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i;
	av.update_parts_movies();
	int final = av_match(tex, rgb);
	printf("  %d updates; pictures seen %d%d%d%d%d; ended %d at %d ms; last picture %d\n", updates,
	       seen[0], seen[1], seen[2], seen[3], seen[4], ended, end_time, final);
	AV_CHECK(!order_bad, "%d updates showed a picture not due at the audio clock", order_bad);
	AV_CHECK(seen[0] && seen[1] && seen[2] && seen[3] && seen[4], "not every picture was shown in order");
	AV_CHECK(!backwards, "the picture went back %d times", backwards);
	AV_CHECK(ended && end_time == 900 && final == 4, "after the sound: ended %d, time %d (want 900), picture %d", ended, end_time, final);
	// ReleasePartsMovie: no movie, ended, the picture stays (INFERENCE: the
	// original's ReleasePartsMovie was not read).
	av_pe("ReleasePartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) });
	AV_CHECK(av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i
		 && !av_pe("GetPartsMovieEndTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i
		 && !av_pe("PlayPartsMovie", 3, (union vm_value[]){ AV_I(N), AV_I(0), AV_I(1) }).i,
		 "after ReleasePartsMovie the movie is still there");
	AV_CHECK(av_match(tex, rgb) == 4, "ReleasePartsMovie cleared the surface");
	// PORT CHOICE: the original (0x59b160) says false for a missing parts;
	// true here keeps a script from waiting forever on one.
	AV_CHECK(av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(424242), AV_I(1) }).i, "a missing parts has not ended");
	av_pe("Release", 1, (union vm_value[]){ AV_I(N) });
	av_parts_teardown(ctx);
}

static void av11_release(void)
{
	bool linked = av_resolve();
	CGLContextObj ctx = NULL;
	static uint8_t rgb[6][48 * 32 * 3];
	if (!av_parts_setup(&ctx, rgb, 0, 128, 128)) {
		av_parts_teardown(ctx);
		return;
	}
	const int N = 7002;
	for (int round = 0; round < 3; round++) {
		AV_CHECK(av_create(N, "video.apeg", 0, 128, 128) && av_texture(N), "round %d: CreatePartsMovie", round);
		if (round == 1)  // a second movie on the same parts replaces the first
			AV_CHECK(av_create(N, "video.apeg", 0, 128, 128) && av_texture(N), "re-create");
		AV_CHECK(av_pe("PlayPartsMovie", 3, (union vm_value[]){ AV_I(N), AV_I(0), AV_I(1) }).i, "round %d: play", round);
		SDL_Delay(120);
		if (linked)
			av.update_parts_movies();
		AV_CHECK(!av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i, "round %d: ended early", round);
		// How the game's CDrawMovie disposes of the movie.
		av_pe("Release", 1, (union vm_value[]){ AV_I(N) });
		AV_CHECK(!parts_try_get(N), "round %d: parts still there", round);
		AV_CHECK(av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i, "round %d: released movie not ended", round);
		if (linked)
			av.update_parts_movies();  // the entry for the released parts drops out
		SDL_Delay(60);  // a callback into a freed movie would be caught here
	}
	av_parts_teardown(ctx);
}

static void av12_start_position(void)
{
	bool linked = av_resolve();
	CGLContextObj ctx = NULL;
	static uint8_t rgb[6][48 * 32 * 3];
	if (!av_parts_setup(&ctx, rgb, 0, 128, 128)) {
		av_parts_teardown(ctx);
		return;
	}
	const int N = 7003;
	Uint64 took[2] = { 0, 0 };
	for (int k = 0; k < 2; k++) {
		int start = k ? 600 : 0;
		AV_CHECK(av_create(N, "video.apeg", 0, 128, 128), "CreatePartsMovie");
		Uint64 t0 = SDL_GetTicks64();
		AV_CHECK(av_pe("PlayPartsMovie", 3, (union vm_value[]){ AV_I(N), AV_I(start), AV_I(1) }).i, "PlayPartsMovie(%d)", start);
		int pos = av_pe("GetPartsMovieCurrentTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i;
		AV_CHECK(pos >= start && pos < start + 300, "start %d: first time %d", start, pos);
		if (linked && k) {
			av.update_parts_movies();
			Texture *tex = av_texture(N);
			AV_CHECK(tex && av_match(tex, rgb) == 4, "start 600: not the picture at 400");
		}
		while (!av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i && SDL_GetTicks64() - t0 < 6000)
			SDL_Delay(5);
		took[k] = SDL_GetTicks64() - t0;
		av_pe("Release", 1, (union vm_value[]){ AV_I(N) });
	}
	printf("  sound from 0 ended after %llu ms, from 600 after %llu ms\n", (unsigned long long)took[0], (unsigned long long)took[1]);
	AV_CHECK(took[0] < 6000 && took[1] + 300 < took[0], "a start position does not shorten the sound");
	av_parts_teardown(ctx);
}

static bool av_all_zero(Texture *tex)
{
	uint8_t *px = gfx_get_pixels(tex);
	bool zero = true;
	for (int i = 0; i < tex->w * tex->h * 4; i++)
		zero &= !px[i];
	free(px);
	return zero;
}

static void av13_other_routes(void)
{
	bool linked = av_resolve();
	CGLContextObj ctx = NULL;
	static uint8_t rgb[6][48 * 32 * 3];
	if (!av_parts_setup(&ctx, rgb, 0, 128, 128)) {
		av_parts_teardown(ctx);
		return;
	}
	// movie_draw into a sprite (DrawMovie): the APEG picture of the clock.
	struct movie_context *mc = movie_load("video.apeg");
	AV_CHECK(mc, "movie_load(video.apeg)");
	if (mc) {
		AV_CHECK(linked && av.movie_duration(mc) == 900, "movie_get_duration");
		struct sact_sprite sprite = { 0 };
		sprite_init(&sprite);
		sprite_init_color(&sprite, 48, 32, 0, 255, 0, 255);
		// Before play: black once (as before the pictures were decoded).
		movie_draw(mc, &sprite);
		AV_CHECK(av_all_zero(sprite_get_texture(&sprite)), "movie_draw before play: the sprite is not black");
		AV_CHECK(movie_play(mc), "movie_play");
		bool seen[6] = { false };
		Uint64 t0 = SDL_GetTicks64();
		while (!movie_is_end(mc) && SDL_GetTicks64() - t0 < 6000) {
			movie_draw(mc, &sprite);
			int m = av_match(sprite_get_texture(&sprite), rgb);
			if (m >= 0)
				seen[m] = true;
			SDL_Delay(4);
		}
		AV_CHECK(seen[0] && seen[4] && seen[1] + seen[2] + seen[3] >= 2, "movie_draw pictures seen %d%d%d%d%d",
			 seen[0], seen[1], seen[2], seen[3], seen[4]);
		movie_free(mc);
		sprite_free(&sprite);
	}
	// A sprite of another size than the movie: black, as before.
	mc = movie_load("video.apeg");
	if (mc) {
		struct sact_sprite small = { 0 };
		sprite_init(&small);
		sprite_init_color(&small, 40, 24, 0, 255, 0, 255);
		AV_CHECK(movie_play(mc), "movie_play");
		SDL_Delay(120);
		for (int k = 0; k < 3; k++)
			movie_draw(mc, &small);
		AV_CHECK(av_all_zero(sprite_get_texture(&small)), "movie_draw into a 40x24 sprite: not black");
		movie_free(mc);
		sprite_free(&small);
	}
	// The real UpdateComponent HLL draws a playing parts movie (after an
	// UpdateView, so it does not present).
	const int N = 7004;
	AV_CHECK(av_create(N, "video.apeg", 0, 128, 128), "CreatePartsMovie");
	AV_CHECK(av_pe("PlayPartsMovie", 3, (union vm_value[]){ AV_I(N), AV_I(0), AV_I(1) }).i, "PlayPartsMovie");
	SDL_Delay(150);
	frame_pacing_update_view();
	// No window: give the main surface a size (the input update scales the
	// mouse position by it).
	extern void probe_main_surface_init(int w, int h);
	probe_main_surface_init(48, 32);
	int lib = av_lib("PartsEngine"), f = sf_find(lib, "UpdateComponent", 5);
	AV_CHECK(f >= 0, "UpdateComponent/5 not declared");
	if (f >= 0) {
		// The probe links without _PreLink, which binds the v14 form.
		extern struct static_library lib_PartsEngine;
		for (int i = 0; lib_PartsEngine.functions[i].name; i++)
			if (!strcmp(lib_PartsEngine.functions[i].name, "_PreLink"))
				((void (*)(void))lib_PartsEngine.functions[i].fun)();
		for (int i = 0; lib_PartsEngine.functions[i].name; i++)
			if (!strcmp(lib_PartsEngine.functions[i].name, "UpdateComponent"))
				libraries[lib][f].fun = lib_PartsEngine.functions[i].fun;
		stack_push(AV_I(16)); stack_push(AV_I(16)); stack_push(AV_I(1));
		stack_push((union vm_value){ .f = 1.0f }); stack_push((union vm_value){ .f = 1.0f });
		int sp = stack_ptr - 5;
		hll_call(lib, f, 0);
		stack_ptr = sp;
		Texture *tex = av_texture(N);
		int m = tex ? av_match(tex, rgb) : -1;
		AV_CHECK(m >= 0 && m < 5, "after UpdateComponent the parts shows %d", m);
	}
	av_pe("Release", 1, (union vm_value[]){ AV_I(N) });
	av_parts_teardown(ctx);
}

/* Checks picture_at(t) against presentation slot m of the model. */
static bool av_gop_at(struct apeg *a, int64_t t, const struct av_pic *model, int m)
{
	const struct apeg_picture *p = av.picture_at(a, t);
	char what[64];
	snprintf(what, sizeof what, "open-gop picture_at(%lld)", (long long)t);
	if (!p) {
		AV_CHECK(false, "%s: NULL (%s)", what, av.error(a) ? av.error(a) : "-");
		return false;
	}
	return !av_compare(p, &model[m], what);
}

static void av14_open_gop(void)
{
	if (!av_resolve())
		return;
	struct av_pic *m = xcalloc(AV_GOP_N, sizeof(struct av_pic));
	av_build_gop_model(m);
	// Decode order, every picture against the model.
	struct apeg *a = av_open("open-gop.apeg");
	for (int i = 0; a && i < AV_GOP_N; i++) {
		const struct apeg_picture *p = NULL;
		int r = av.decode_next(a, &p);
		char what[64];
		snprintf(what, sizeof what, "open-gop decode_next %d", i);
		if (r != 1)
			AV_CHECK(false, "%s: r=%d (%s)", what, r, av.error(a) ? av.error(a) : "-");
		else
			av_compare(p, &m[av_gop_slot[i]], what);
	}
	if (a)
		av.close(a);
	// The I first, then just before it: B500, whose past reference (P300)
	// is in the GOP before. Then B400 and B500 again: only B pictures were
	// decoded since, so each is decoded alone, without starting again.
	a = av_open("open-gop.apeg");
	if (a) {
		av_gop_at(a, 600, m, 6);
		av_gop_at(a, 599, m, 5);
		int d = av_decoded(a), r = av_restarts(a);
		av_gop_at(a, 450, m, 4);
		AV_CHECK(av_decoded(a) == d + 1 && av_restarts(a) == r, "B400 after B500: %d pictures decoded, %d restarts",
			 av_decoded(a) - d, av_restarts(a) - r);
		d = av_decoded(a);
		av_gop_at(a, 550, m, 5);
		AV_CHECK(av_decoded(a) == d + 1 && av_restarts(a) == r, "B500 after B400: %d pictures decoded, %d restarts",
			 av_decoded(a) - d, av_restarts(a) - r);
		// Forward through the second GOP, then back to the open-GOP B
		// pictures and the first GOP.
		static const struct { int64_t t; int m; } walk[] = {
			{ 650, 6 }, { 700, 7 }, { 899, 8 }, { 900, 9 }, { 5000, 9 }, { 450, 4 }, { 599, 5 }, { 300, 3 },
			{ 150, 1 }, { 800, 8 }, { 400, 4 }, { 0, 0 }, { 500, 5 } };
		for (size_t i = 0; i < sizeof(walk) / sizeof(walk[0]); i++)
			av_gop_at(a, walk[i].t, m, walk[i].m);
		av.close(a);
	}
	// A failure that comes from what was decoded before is not remembered:
	// decode_next right after starting at I600 reaches B400 without its past
	// reference and fails, yet picture_at decodes B400 afterwards.
	a = av_open("open-gop.apeg");
	if (a) {
		av_gop_at(a, 600, m, 6);
		const struct apeg_picture *p = NULL;
		int r = av.decode_next(a, &p);
		AV_CHECK(r == -1 && av_has(av.error(a), "past reference"), "decode_next after I600: r=%d (%s)", r,
			 av.error(a) ? av.error(a) : "-");
		av_gop_at(a, 450, m, 4);
		av.close(a);
	}
	// Cold start straight at each open-GOP B picture.
	for (int k = 4; k <= 5; k++) {
		a = av_open("open-gop.apeg");
		if (a) {
			av_gop_at(a, 100 * k, m, k);
			av.close(a);
		}
	}
	// After apeg_rewind, elsewhere first.
	a = av_open("open-gop.apeg");
	if (a) {
		av_gop_at(a, 900, m, 9);
		av.rewind(a);
		av_gop_at(a, 500, m, 5);
		av_gop_at(a, 600, m, 6);
		av.rewind(a);
		av_gop_at(a, 400, m, 4);
		av.close(a);
	}
	free(m);
}

static void av15_clock_back(void)
{
	bool linked = av_resolve();
	CGLContextObj ctx = NULL;
	static uint8_t rgb[6][48 * 32 * 3];
	if (!av_parts_setup(&ctx, rgb, 0, 128, 128)) {
		av_parts_teardown(ctx);
		return;
	}
	// clockback.apeg: video.apeg's pictures and sound, header length 250.
	// The clock runs to about 1.5 s with the sound, then reads 250 (the
	// length) once the sound has ended: the picture must stay at 400.
	const int N = 7005;
	AV_CHECK(av_create(N, "clockback.apeg", 0, 128, 128), "CreatePartsMovie");
	Texture *tex = av_texture(N);
	if (!tex || !linked) {
		AV_CHECK(tex, "no movie parts");
		av_parts_teardown(ctx);
		return;
	}
	AV_CHECK(av_pe("PlayPartsMovie", 3, (union vm_value[]){ AV_I(N), AV_I(0), AV_I(1) }).i, "PlayPartsMovie failed");
	int last = -1, backwards = 0, max_t = 0;
	Uint64 start = SDL_GetTicks64();
	while (!av_pe("IsEndPartsMovie", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i && SDL_GetTicks64() - start < 6000) {
		int t = av_pe("GetPartsMovieCurrentTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i;
		max_t = t > max_t ? t : max_t;
		av.update_parts_movies();
		int k = av_match(tex, rgb);
		if (k >= 0 && k < 5) {
			backwards += k < last;
			last = k;
		}
		SDL_Delay(4);
	}
	int end_time = av_pe("GetPartsMovieCurrentTime", 2, (union vm_value[]){ AV_I(N), AV_I(1) }).i;
	for (int k = 0; k < 3; k++)
		av.update_parts_movies();
	int final = av_match(tex, rgb);
	printf("  clock up to %d ms, then %d; last picture during play %d, after the end %d, went back %d times\n",
	       max_t, end_time, last, final, backwards);
	AV_CHECK(max_t > 400 && end_time == 250, "clock: up to %d, at the end %d (want 250)", max_t, end_time);
	AV_CHECK(last == 4 && final == 4 && !backwards, "after the clock went back the parts shows %d (want 4)", final);
	av_pe("Release", 1, (union vm_value[]){ AV_I(N) });
	av_parts_teardown(ctx);
}

static int av_case(const char *name, void (*fn)(void))
{
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0) {
		printf("  FAIL: %s fork: %s\n", name, strerror(errno));
		return 1;
	}
	if (pid == 0) {
		alarm(25);
		av_failures = 0;
		ma_failures = 0;
		fn();
		fflush(NULL);
		_exit(av_failures || ma_failures ? 1 : 0);
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

static int test_apeg_video(void)
{
	int failed = 0;
	failed += av_case("AV1 container, index, sound, picture order", av1_container);
	failed += av_case("AV2 I picture: DC-only blocks, quantiser bits", av2_i_picture);
	failed += av_case("AV3 P picture: intra, vectors, skip copy, residuals", av3_p_picture);
	failed += av_case("AV4 B pictures: both/forward/backward/intra, skips", av4_b_pictures);
	failed += av_case("AV5 I picture with AC coefficients and escape", av5_ac_block);
	failed += av_case("AV6 picture_at: latest timestamp <= t, seeks", av6_picture_at);
	failed += av_case("AV7 RGBA conversion, odd size, back colour", av7_colour);
	failed += av_case("AV8 rejected and warned containers, truncations", av8_rejected);
	failed += av_case("AV9 undecodable pictures, resume at I, byte flips", av9_undecodable);
	failed += av_case("AV10 parts movie: create, play, clock, end", av10_parts_movie);
	failed += av_case("AV11 PartsEngine.Release stops the movie", av11_release);
	failed += av_case("AV12 PlayPartsMovie start position", av12_start_position);
	failed += av_case("AV13 movie_draw sprite route, UpdateComponent HLL", av13_other_routes);
	failed += av_case("AV14 open GOP: cold start, rewind, going back", av14_open_gop);
	failed += av_case("AV15 the clock going back keeps the picture", av15_clock_back);
	printf("APEG-VIDEO %s: %d of 15 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}
