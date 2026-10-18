/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * APEG decoder (see include/apeg.h). The addresses are the original player's
 * functions this follows (Dohna Dohna, static disassembly):
 *
 *   container        0x4484f0 (open), 0x4492b0 (header)
 *   picture layer    0x45cd70 (I), 0x45ce10 (P), 0x45cf90 / 0x45d0f0 (B)
 *   macroblocks      0x45c730 (I), 0x45c7e0 (P), 0x45c980 (B)
 *   blocks           0x44b590 (intra), 0x44b330 (non-intra), 0x44afd0 (IDCT)
 *   prediction       0x44bbe0 (one direction), 0x44bd20 (both), 0x44bb10 (P skip)
 *   output           0x44c2c0 (clamping), the pixel shader of key 0x80 (matrix)
 *
 * Differences from the original, all only for data the original does not
 * check: a truncated or corrupt picture fails instead of being shown half
 * decoded; skip runs past the last macroblock, coefficient positions past 63,
 * invalid codes and missing reference pictures fail the picture; a prediction
 * block whose linear address range leaves the plane reads every sample at its
 * address clamped into the plane (the original reads outside its buffer).
 * Op.apeg never takes any of these paths.
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apeg.h"

// apeg_picture_to_rgba must round exactly like the reference arithmetic:
// a fused multiply-add would change results at .5 ties.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

#define APEG_HEADER_SIZE 0x98
#define APEG_MAX_DIMENSION 4096
#define BITS_PAD 8

/* ---------------------------------------------------------------------- */
/* Code tables. Codewords are written most significant bit first.          */
/* ---------------------------------------------------------------------- */

struct vlc_code {
	const char *bits;
	int value;
};

// Macroblock type flags: 1 intra, 2 coded block pattern, 4 backward vector,
// 8 forward vector, 16 quantiser scale.
static const struct vlc_code mb_type_p_codes[] = {
	{ "0", 10 }, { "10", 2 }, { "110", 8 }, { "11100", 1 }, { "11101", 26 },
	{ "11110", 18 }, { "111110", 17 }, { "111111", 24 }, { NULL, 0 }
};

static const struct vlc_code mb_type_b_codes[] = {
	{ "00", 14 }, { "01", 12 }, { "100", 6 }, { "101", 4 }, { "1100", 10 },
	{ "1101", 8 }, { "11100", 1 }, { "11101", 30 }, { "111100", 28 },
	{ "111101", 22 }, { "1111100", 20 }, { "1111101", 26 }, { "1111110", 24 },
	{ "1111111", 17 }, { NULL, 0 }
};

// Coded block pattern: bit 5 = Y0 ... bit 0 = Cr. -1 is not a pattern.
static const struct vlc_code cbp_codes[] = {
	{ "000", 60 }, { "0010", 4 }, { "0011", 8 }, { "0100", 16 }, { "0101", 32 },
	{ "01100", 1 }, { "01101", 2 }, { "01110", 12 }, { "01111", 20 },
	{ "10000", 28 }, { "10001", 40 }, { "10010", 44 }, { "10011", 48 },
	{ "10100", 52 }, { "10101", 56 }, { "10110", 61 }, { "10111", 62 },
	{ "110000", 3 }, { "110001", 24 }, { "110010", 36 }, { "110011", 63 },
	{ "1101000", 5 }, { "1101001", 6 }, { "1101010", 9 }, { "1101011", 10 },
	{ "1101100", 17 }, { "1101101", 18 }, { "1101110", 33 }, { "1101111", 34 },
	{ "11100000", 7 }, { "11100001", 11 }, { "11100010", 13 }, { "11100011", 14 },
	{ "11100100", 15 }, { "11100101", 19 }, { "11100110", 21 }, { "11100111", 22 },
	{ "11101000", 23 }, { "11101001", 25 }, { "11101010", 26 }, { "11101011", 29 },
	{ "11101100", 30 }, { "11101101", 35 }, { "11101110", 37 }, { "11101111", 38 },
	{ "11110000", 41 }, { "11110001", 42 }, { "11110010", 43 }, { "11110011", 45 },
	{ "11110100", 46 }, { "11110101", 49 }, { "11110110", 50 }, { "11110111", 51 },
	{ "11111000", 53 }, { "11111001", 54 }, { "11111010", 57 }, { "11111011", 58 },
	{ "11111111", -1 },
	{ "111111000", 27 }, { "111111001", 31 }, { "111111010", 39 },
	{ "111111011", 47 }, { "111111100", 55 }, { "111111101", 59 },
	{ NULL, 0 }
};

// Motion code; a sign bit and r_size residual bits follow a non-zero code.
static const struct vlc_code motion_codes[] = {
	{ "0", 0 }, { "10", 1 }, { "110", 2 }, { "1110", 3 }, { "111100", 4 },
	{ "1111010", 5 }, { "1111011", 6 }, { "1111100", 7 }, { "11111010", 8 },
	{ "11111011", 9 }, { "11111100", 10 }, { "111111010", 11 },
	{ "111111011", 12 }, { "111111100", 13 }, { "111111101", 14 },
	{ "111111110", 15 }, { "111111111", 16 }, { NULL, 0 }
};

// Number of macroblocks skipped after this one; -1 adds 35 and reads again.
static const struct vlc_code skip_run_codes[] = {
	{ "0", 0 }, { "100", 1 }, { "101", 2 }, { "1100", 3 }, { "1101", 4 },
	{ "11100", 5 }, { "11101", 6 }, { "1111000", 7 }, { "1111001", 8 },
	{ "11110100", 9 }, { "11110101", 10 }, { "11110110", 11 }, { "11110111", 12 },
	{ "11111000", 13 }, { "11111001", 14 }, { "11111111", -1 },
	{ "1111101000", 15 }, { "1111101001", 16 }, { "1111101010", 17 },
	{ "1111101011", 18 }, { "1111101100", 19 }, { "1111101101", 20 },
	{ "1111101110", 21 }, { "1111101111", 22 }, { "1111110000", 23 },
	{ "1111110001", 24 }, { "1111110010", 25 }, { "1111110011", 26 },
	{ "1111110100", 27 }, { "1111110101", 28 }, { "1111110110", 29 },
	{ "1111110111", 30 }, { "1111111000", 31 }, { "1111111001", 32 },
	{ "1111111010", 33 }, { "1111111011", 34 }, { NULL, 0 }
};

// Size in bits of the DC difference.
static const struct vlc_code dc_size_luma_codes[] = {
	{ "00", 1 }, { "01", 2 }, { "100", 0 }, { "101", 3 }, { "110", 4 },
	{ "1110", 5 }, { "11110", 6 }, { "111110", 7 }, { "111111", 8 }, { NULL, 0 }
};

static const struct vlc_code dc_size_chroma_codes[] = {
	{ "00", 0 }, { "01", 1 }, { "10", 2 }, { "110", 3 }, { "1110", 4 },
	{ "11110", 5 }, { "111110", 6 }, { "1111110", 7 }, { "1111111", 8 }, { NULL, 0 }
};

// Run/level pairs. A sign bit (1 = negative) follows every codeword except
// the end of block and the escape (6 bits run, 8 or 16 bits level).
#define COEF_EOB (-2)
#define COEF_ESC (-1)
#define COEF_BAD (-3)

struct coef_code {
	const char *bits;
	int run, level;
};

static const struct coef_code coef_codes[] = {
	{ "00", COEF_EOB, 0 }, { "01", 0, 1 },
	{ "100", 1, 1 }, { "1010", 0, 2 }, { "1011", 2, 1 }, { "11000", 0, 3 },
	{ "11001", 3, 1 }, { "11010", 4, 1 }, { "110110", COEF_ESC, 0 },
	{ "110111", 1, 2 }, { "111000", 5, 1 }, { "111001", 6, 1 }, { "111010", 7, 1 },
	{ "1110110", 0, 4 }, { "1110111", 2, 2 }, { "1111000", 8, 1 }, { "1111001", 9, 1 },
	{ "11110100", 0, 5 }, { "11110101", 0, 6 }, { "11110110", 1, 3 },
	{ "11110111", 3, 2 }, { "11111000", 10, 1 }, { "11111001", 11, 1 },
	{ "11111010", 12, 1 }, { "11111011", 13, 1 },
	{ "1111110000", 0, 7 }, { "1111110001", 1, 4 }, { "1111110010", 2, 3 },
	{ "1111110011", 4, 2 }, { "1111110100", 5, 2 }, { "1111110101", 14, 1 },
	{ "1111110110", 15, 1 }, { "1111110111", 16, 1 },
	{ "111111100000", 0, 8 }, { "111111100001", 0, 9 }, { "111111100010", 0, 10 },
	{ "111111100011", 0, 11 }, { "111111100100", 1, 5 }, { "111111100101", 2, 4 },
	{ "111111100110", 3, 3 }, { "111111100111", 4, 3 }, { "111111101000", 6, 2 },
	{ "111111101001", 7, 2 }, { "111111101010", 8, 2 }, { "111111101011", 17, 1 },
	{ "111111101100", 18, 1 }, { "111111101101", 19, 1 }, { "111111101110", 20, 1 },
	{ "111111101111", 21, 1 },
	{ "1111111100000", 0, 12 }, { "1111111100001", 0, 13 }, { "1111111100010", 0, 14 },
	{ "1111111100011", 0, 15 }, { "1111111100100", 1, 6 }, { "1111111100101", 1, 7 },
	{ "1111111100110", 2, 5 }, { "1111111100111", 3, 4 }, { "1111111101000", 5, 3 },
	{ "1111111101001", 9, 2 }, { "1111111101010", 10, 2 }, { "1111111101011", 22, 1 },
	{ "1111111101100", 23, 1 }, { "1111111101101", 24, 1 }, { "1111111101110", 25, 1 },
	{ "1111111101111", 26, 1 },
	{ "11111111100000", 0, 16 }, { "11111111100001", 0, 17 }, { "11111111100010", 0, 18 },
	{ "11111111100011", 0, 19 }, { "11111111100100", 0, 20 }, { "11111111100101", 0, 21 },
	{ "11111111100110", 0, 22 }, { "11111111100111", 0, 23 }, { "11111111101000", 0, 24 },
	{ "11111111101001", 0, 25 }, { "11111111101010", 0, 26 }, { "11111111101011", 0, 27 },
	{ "11111111101100", 0, 28 }, { "11111111101101", 0, 29 }, { "11111111101110", 0, 30 },
	{ "11111111101111", 0, 31 },
	{ "111111111100000", 0, 32 }, { "111111111100001", 0, 33 }, { "111111111100010", 0, 34 },
	{ "111111111100011", 0, 35 }, { "111111111100100", 0, 36 }, { "111111111100101", 0, 37 },
	{ "111111111100110", 0, 38 }, { "111111111100111", 0, 39 }, { "111111111101000", 0, 40 },
	{ "111111111101001", 1, 8 }, { "111111111101010", 1, 9 }, { "111111111101011", 1, 10 },
	{ "111111111101100", 1, 11 }, { "111111111101101", 1, 12 }, { "111111111101110", 1, 13 },
	{ "111111111101111", 1, 14 }, { "111111111110000", 1, 15 }, { "111111111110001", 1, 16 },
	{ "111111111110010", 1, 17 }, { "111111111110011", 1, 18 }, { "111111111110100", 6, 3 },
	{ "111111111110101", 11, 2 }, { "111111111110110", 12, 2 }, { "111111111110111", 13, 2 },
	{ "111111111111000", 14, 2 }, { "111111111111001", 15, 2 }, { "111111111111010", 16, 2 },
	{ "111111111111011", 27, 1 }, { "111111111111100", 28, 1 }, { "111111111111101", 29, 1 },
	{ "111111111111110", 30, 1 }, { "111111111111111", 31, 1 },
	{ NULL, 0, 0 }
};

static const uint8_t zigzag[64] = {
	 0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

// IDCT input scale round(32 * a_i * a_j), a_0 = 1, a_k = sqrt(2) cos(k pi / 16)
// (the original's table at 0x80d408).
static const int32_t idct_scale[64] = {
	32, 44, 42, 38, 32, 25, 17,  9,
	44, 62, 58, 52, 44, 35, 24, 12,
	42, 58, 55, 49, 42, 33, 23, 12,
	38, 52, 49, 44, 38, 30, 20, 10,
	32, 44, 42, 38, 32, 25, 17,  9,
	25, 35, 33, 30, 25, 20, 14,  7,
	17, 24, 23, 20, 17, 14,  9,  5,
	 9, 12, 12, 10,  9,  7,  5,  2
};

struct vlc_entry {
	int16_t value;
	uint8_t len;      // 0: no codeword starts with these bits
};

struct coef_entry {
	int8_t run;       // >= 0, COEF_EOB, COEF_ESC or COEF_BAD
	int8_t level;     // signed
	uint8_t len;      // including the sign bit
};

#define DC_BITS 8
#define CBP_BITS 9
#define MOTION_BITS 9
#define SKIP_BITS 10
#define TYPE_BITS 8
#define COEF_BITS 16

struct apeg_tables {
	struct vlc_entry type_p[1 << TYPE_BITS], type_b[1 << TYPE_BITS];
	struct vlc_entry cbp[1 << CBP_BITS], motion[1 << MOTION_BITS], skip[1 << SKIP_BITS];
	struct vlc_entry dc_luma[1 << DC_BITS], dc_chroma[1 << DC_BITS];
	struct coef_entry coef[1 << COEF_BITS];
};

static bool code_value(const char *bits, unsigned *code, unsigned *len)
{
	unsigned v = 0, n = 0;
	for (; bits[n]; n++) {
		if (bits[n] != '0' && bits[n] != '1')
			return false;
		v = v << 1 | (bits[n] == '1');
	}
	*code = v;
	*len = n;
	return n > 0;
}

static bool vlc_build(struct vlc_entry *t, unsigned width, const struct vlc_code *codes)
{
	memset(t, 0, sizeof(*t) << width);
	for (const struct vlc_code *c = codes; c->bits; c++) {
		unsigned code, len;
		if (!code_value(c->bits, &code, &len) || len > width)
			return false;
		unsigned first = code << (width - len), n = 1u << (width - len);
		for (unsigned i = first; i < first + n; i++) {
			if (t[i].len)
				return false;  // not a prefix code
			t[i] = (struct vlc_entry){ .value = c->value, .len = len };
		}
	}
	return true;
}

static bool coef_fill(struct coef_entry *t, unsigned code, unsigned len, struct coef_entry e)
{
	if (len > COEF_BITS)
		return false;
	unsigned first = code << (COEF_BITS - len), n = 1u << (COEF_BITS - len);
	for (unsigned i = first; i < first + n; i++) {
		if (t[i].run != COEF_BAD)
			return false;
		t[i] = e;
	}
	return true;
}

static bool coef_build(struct coef_entry *t)
{
	for (unsigned i = 0; i < 1u << COEF_BITS; i++)
		t[i] = (struct coef_entry){ .run = COEF_BAD };
	for (const struct coef_code *c = coef_codes; c->bits; c++) {
		unsigned code, len;
		if (!code_value(c->bits, &code, &len))
			return false;
		if (c->run < 0) {
			if (!coef_fill(t, code, len, (struct coef_entry){ .run = c->run, .len = len }))
				return false;
			continue;
		}
		struct coef_entry pos = { .run = c->run, .level = c->level, .len = len + 1 };
		struct coef_entry neg = { .run = c->run, .level = -c->level, .len = len + 1 };
		if (!coef_fill(t, code << 1, len + 1, pos) || !coef_fill(t, code << 1 | 1, len + 1, neg))
			return false;
	}
	return true;
}

static bool tables_build(struct apeg_tables *t)
{
	return vlc_build(t->type_p, TYPE_BITS, mb_type_p_codes)
		&& vlc_build(t->type_b, TYPE_BITS, mb_type_b_codes)
		&& vlc_build(t->cbp, CBP_BITS, cbp_codes)
		&& vlc_build(t->motion, MOTION_BITS, motion_codes)
		&& vlc_build(t->skip, SKIP_BITS, skip_run_codes)
		&& vlc_build(t->dc_luma, DC_BITS, dc_size_luma_codes)
		&& vlc_build(t->dc_chroma, DC_BITS, dc_size_chroma_codes)
		&& coef_build(t->coef);
}

/* ---------------------------------------------------------------------- */
/* Container                                                              */
/* ---------------------------------------------------------------------- */

// The pixel shader (key 0x80 in the embedded shader pack): the inverse of
// the integer BT.601 forward transform, on UNORM inputs.
static const double ycc_matrix[3][3] = {
	{ 1.16364, -0.00275, 1.59907 },
	{ 1.16364, -0.39128, -0.81847 },
	{ 1.16364, 2.02626, 0.00177 },
};
static const double ycc_offset[3] = { -0.87971, 0.53433, -1.09726 };

struct apeg_chunk {
	uint64_t offset;   // payload (timestamp + bitstream)
	uint32_t size;
	uint32_t ts;
	char type;
};

struct apeg_slot {
	int16_t *y, *cb, *cr;
	int index;         // picture in it, -1 if none
};

struct apeg {
	FILE *fp;
	uint8_t *mem;
	uint64_t size;

	int version, width, height, duration;
	uint8_t qi[64], qn[64];
	uint32_t *index;   // pairs (ms, offset)
	int nr_index;
	bool has_sound;
	uint64_t sound_offset;
	uint32_t sound_size;

	struct apeg_chunk *pics;
	int nr_pics;
	int *by_ts;        // picture numbers in presentation order
	int *anchors_upto; // anchors_upto[i]: I/P pictures among 0 .. i-1
	int *seek;         // seek[i]: the I picture to start at for picture i, or -1
	uint8_t *broken;   // never decodable: bad data, or a reference the file lacks

	int w16, h16, mbw, nmb, cw;
	size_t luma_size, chroma_size;

	struct apeg_slot anchor[3], bpic;
	int past, future;  // anchor[] numbers or -1
	int next;          // next picture in decode order
	int seq_start;     // where the references in anchor[] were decoded from
	bool chain_broken; // an anchor failed since the last I
	int nr_decoded, nr_restarts;  // apeg_stats

	uint8_t *buf;
	size_t buf_cap;
	int32_t coef[64];  // dequantised, scaled IDCT input; kept zero between blocks

	double *chroma_rows[2][3];
	int chroma_row_no[2][3];
	double luma_term[3][256];  // ycc_matrix[k][0] * (Y / 255)
	double unorm[256];         // v / 255
	double *chroma_tmp;

	struct apeg_picture pic;
	const char *error;
	const char *warning;  // a problem at open that did not stop it
	struct apeg_tables t;
};

// Failures that depend on what was decoded before, not on the picture's own
// data: they are never remembered in broken[] by themselves.
static const char err_no_reference[] = "APEG: P/B picture without a reference picture";
static const char err_no_past[] = "APEG: B picture without a past reference";
static const char err_read[] = "APEG: cannot read picture";

static bool missing_reference(const char *e)
{
	return e == err_no_reference || e == err_no_past;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }

static bool read_at(struct apeg *a, uint64_t off, void *dst, size_t n)
{
	if (off > a->size || n > a->size - off)
		return false;
	if (a->mem) {
		memcpy(dst, a->mem + off, n);
		return true;
	}
	if (off > LONG_MAX || fseek(a->fp, (long)off, SEEK_SET))
		return false;
	return fread(dst, 1, n, a->fp) == n;
}

static void slot_free(struct apeg_slot *s)
{
	free(s->y);
	free(s->cb);
	free(s->cr);
}

void apeg_close(struct apeg *a)
{
	if (!a)
		return;
	if (a->fp)
		fclose(a->fp);
	free(a->mem);
	free(a->index);
	free(a->pics);
	free(a->by_ts);
	free(a->anchors_upto);
	free(a->seek);
	free(a->broken);
	for (int i = 0; i < 3; i++)
		slot_free(&a->anchor[i]);
	slot_free(&a->bpic);
	free(a->buf);
	free(a->chroma_tmp);
	for (int p = 0; p < 2; p++)
		for (int i = 0; i < 3; i++)
			free(a->chroma_rows[p][i]);
	free(a);
}

struct ts_key {
	uint32_t ts;
	int index;
};

static int cmp_ts(const void *x, const void *y)
{
	const struct ts_key *p = x, *q = y;
	if (p->ts != q->ts)
		return p->ts < q->ts ? -1 : 1;
	return p->index < q->index ? -1 : p->index > q->index;
}

static bool slot_alloc(struct apeg *a, struct apeg_slot *s)
{
	s->y = malloc(a->luma_size * sizeof(int16_t));
	s->cb = malloc(a->chroma_size * sizeof(int16_t));
	s->cr = malloc(a->chroma_size * sizeof(int16_t));
	s->index = -1;
	return s->y && s->cb && s->cr;
}

static const char *parse(struct apeg *a)
{
	uint8_t h[APEG_HEADER_SIZE];
	if (!read_at(a, 0, h, sizeof h))
		return "APEG: header truncated";
	if (memcmp(h, "APEG", 4))
		return "APEG: bad magic";
	uint64_t data = 8 + (uint64_t)le32(h + 4);
	a->version = le32(h + 8);
	if (a->version != 3 && a->version != 5)
		return "APEG: unsupported version (only 3 and 5)";
	a->width = le16(h + 12);
	a->height = le16(h + 14);
	if (!a->width || !a->height || a->width > APEG_MAX_DIMENSION || a->height > APEG_MAX_DIMENSION)
		return "APEG: bad picture size";
	memcpy(a->qi, h + 0x10, 64);
	memcpy(a->qn, h + 0x50, 64);
	a->duration = le32(h + 0x90) > INT_MAX ? INT_MAX : (int)le32(h + 0x90);
	if (data < APEG_HEADER_SIZE || data > a->size)
		return "APEG: bad data offset";
	// The I picture index is informative only (the pictures are found by
	// walking the chunks), so a bad one is dropped with a warning.
	uint32_t nr_index = le32(h + 0x94);
	if (nr_index > (data - APEG_HEADER_SIZE) / 8) {
		a->warning = "APEG: index does not fit in the header; ignored";
		nr_index = 0;
	}
	a->index = malloc((nr_index ? nr_index : 1) * 8);
	if (!a->index)
		return "APEG: out of memory";
	for (uint32_t i = 0; i < nr_index; i++) {
		uint8_t e[8];
		if (!read_at(a, APEG_HEADER_SIZE + 8 * (uint64_t)i, e, 8))
			return "APEG: cannot read the index";
		a->index[2*i] = le32(e);
		a->index[2*i+1] = le32(e + 4);
		if (a->index[2*i+1] < data || a->index[2*i+1] >= a->size) {
			a->warning = "APEG: index entry out of range; index ignored";
			nr_index = 0;
			break;
		}
	}
	a->nr_index = nr_index;

	int cap = 0;
	for (uint64_t pos = data; pos < a->size; ) {
		uint8_t c[12];
		if (a->size - pos < 8) {
			// Fewer bytes than a chunk header at the end of the file.
			if (!a->warning)
				a->warning = "APEG: bytes after the last chunk ignored";
			break;
		}
		if (!read_at(a, pos, c, 8))
			return "APEG: cannot read a chunk header";
		uint32_t len = le32(c + 4);
		if (len > a->size - pos - 8)
			return "APEG: chunk truncated";
		char type = 0;
		if (!memcmp(c, "IPIC", 4)) type = 'I';
		else if (!memcmp(c, "PPIC", 4)) type = 'P';
		else if (!memcmp(c, "BPIC", 4)) type = 'B';
		if (type) {
			if (len < 4 || !read_at(a, pos + 8, c + 8, 4))
				return "APEG: picture without timestamp";
			if (a->nr_pics == cap) {
				if (cap > INT_MAX / 2 || (size_t)cap * 2 > SIZE_MAX / sizeof(*a->pics))
					return "APEG: too many pictures";
				cap = cap ? cap * 2 : 1024;
				void *p = realloc(a->pics, cap * sizeof(*a->pics));
				if (!p)
					return "APEG: out of memory";
				a->pics = p;
			}
			a->pics[a->nr_pics++] = (struct apeg_chunk){
				.offset = pos + 8, .size = len, .ts = le32(c + 8), .type = type };
		} else if (!memcmp(c, "SOND", 4) && !a->has_sound) {
			a->has_sound = true;
			a->sound_offset = pos + 8;
			a->sound_size = len;
		}
		pos += 8 + (uint64_t)len;
	}

	int n = a->nr_pics;
	a->by_ts = malloc((n + 1) * sizeof(int));
	a->anchors_upto = malloc((n + 1) * sizeof(int));
	a->seek = malloc((n + 1) * sizeof(int));
	a->broken = calloc(n + 1, 1);
	if (!a->by_ts || !a->anchors_upto || !a->seek || !a->broken)
		return "APEG: out of memory";
	// Where decoding must start for picture i to have its references: an I or
	// P picture needs the last I up to it; a B picture its two references, the
	// last two I/P pictures before it in decode order, so the last I up to the
	// earlier of them (for the B pictures of an open GOP, shown before the I
	// they follow, that is the previous GOP's I).
	int last_i = -1, i_of_future = -1, i_of_past = -1;
	bool have_past = false, have_future = false;
	for (int i = 0; i < n; i++) {
		if (a->pics[i].type == 'B') {
			a->seek[i] = have_past && i_of_past >= 0 ? i_of_past : have_future ? i_of_future : last_i;
			continue;
		}
		if (a->pics[i].type == 'I')
			last_i = i;
		a->seek[i] = last_i;
		have_past = have_future;
		i_of_past = i_of_future;
		have_future = true;
		i_of_future = last_i;
	}
	struct ts_key *keys = malloc((n + 1) * sizeof(struct ts_key));
	if (!keys)
		return "APEG: out of memory";
	a->anchors_upto[0] = 0;
	for (int i = 0; i < n; i++) {
		keys[i] = (struct ts_key){ .ts = a->pics[i].ts, .index = i };
		a->anchors_upto[i+1] = a->anchors_upto[i] + (a->pics[i].type != 'B');
	}
	qsort(keys, n, sizeof(struct ts_key), cmp_ts);
	for (int i = 0; i < n; i++)
		a->by_ts[i] = keys[i].index;
	free(keys);

	a->w16 = (a->width + 15) & ~15;
	a->h16 = (a->height + 15) & ~15;
	a->mbw = a->w16 >> 4;
	a->nmb = a->mbw * (a->h16 >> 4);
	a->cw = a->w16 >> 1;
	a->luma_size = (size_t)a->w16 * a->h16;
	a->chroma_size = a->luma_size / 4;
	for (int i = 0; i < 3; i++)
		if (!slot_alloc(a, &a->anchor[i]))
			return "APEG: out of memory";
	if (!slot_alloc(a, &a->bpic))
		return "APEG: out of memory";
	for (int p = 0; p < 2; p++) {
		for (int i = 0; i < 3; i++) {
			a->chroma_rows[p][i] = malloc(a->w16 * sizeof(double));
			a->chroma_row_no[p][i] = -1;
			if (!a->chroma_rows[p][i])
				return "APEG: out of memory";
		}
	}
	a->chroma_tmp = malloc(a->cw * sizeof(double));
	if (!a->chroma_tmp)
		return "APEG: out of memory";
	for (int v = 0; v < 256; v++)
		a->unorm[v] = v / 255.0;
	for (int k = 0; k < 3; k++)
		for (int v = 0; v < 256; v++)
			a->luma_term[k][v] = ycc_matrix[k][0] * a->unorm[v];
	if (!tables_build(&a->t))
		return "APEG: internal code table error";
	apeg_rewind(a);
	return NULL;
}

static struct apeg *open_common(struct apeg *a, const char **error)
{
	const char *e = parse(a);
	if (e) {
		apeg_close(a);
		a = NULL;
	}
	if (error)
		*error = e;
	return a;
}

struct apeg *apeg_open(const char *path, const char **error)
{
	struct apeg *a = calloc(1, sizeof(struct apeg));
	if (!a) {
		if (error) *error = "APEG: out of memory";
		return NULL;
	}
	a->fp = fopen(path, "rb");
	if (!a->fp || fseek(a->fp, 0, SEEK_END)) {
		apeg_close(a);
		if (error) *error = "APEG: cannot open file";
		return NULL;
	}
	long end = ftell(a->fp);
	if (end < 0) {
		apeg_close(a);
		if (error) *error = "APEG: cannot open file";
		return NULL;
	}
	a->size = (uint64_t)end;
	return open_common(a, error);
}

struct apeg *apeg_open_memory(const void *data, size_t size, const char **error)
{
	struct apeg *a = calloc(1, sizeof(struct apeg));
	if (a)
		a->mem = malloc(size ? size : 1);
	if (!a || !a->mem) {
		apeg_close(a);
		if (error) *error = "APEG: out of memory";
		return NULL;
	}
	memcpy(a->mem, data, size);
	a->size = size;
	return open_common(a, error);
}

int apeg_version(const struct apeg *a) { return a->version; }
int apeg_width(const struct apeg *a) { return a->width; }
int apeg_height(const struct apeg *a) { return a->height; }
int apeg_duration(const struct apeg *a) { return a->duration; }
int apeg_nr_index(const struct apeg *a) { return a->nr_index; }
int apeg_nr_pictures(const struct apeg *a) { return a->nr_pics; }
const char *apeg_error(const struct apeg *a) { return a->error; }
const char *apeg_warning(const struct apeg *a) { return a->warning; }

void apeg_stats(const struct apeg *a, int *decoded, int *restarts)
{
	if (decoded) *decoded = a->nr_decoded;
	if (restarts) *restarts = a->nr_restarts;
}

bool apeg_index_entry(const struct apeg *a, int i, uint32_t *ms, uint32_t *offset)
{
	if (i < 0 || i >= a->nr_index)
		return false;
	if (ms) *ms = a->index[2*i];
	if (offset) *offset = a->index[2*i+1];
	return true;
}

bool apeg_sound(const struct apeg *a, uint64_t *offset, uint32_t *size)
{
	if (!a->has_sound)
		return false;
	if (offset) *offset = a->sound_offset;
	if (size) *size = a->sound_size;
	return true;
}

bool apeg_picture_info(const struct apeg *a, int i, char *type, uint32_t *ts)
{
	if (i < 0 || i >= a->nr_pics)
		return false;
	if (type) *type = a->pics[i].type;
	if (ts) *ts = a->pics[i].ts;
	return true;
}

// Forgets the references; decoding goes on at picture start (an I picture,
// or 0), the new start of the references.
static void restart(struct apeg *a, int start)
{
	a->past = a->future = -1;
	for (int i = 0; i < 3; i++)
		a->anchor[i].index = -1;
	a->bpic.index = -1;
	a->next = start;
	a->seq_start = start;
	a->chain_broken = false;
}

void apeg_rewind(struct apeg *a)
{
	restart(a, 0);
	// A fresh start also tries the pictures that failed before again.
	memset(a->broken, 0, a->nr_pics + 1);
}

/* ---------------------------------------------------------------------- */
/* Bitstream: most significant bit first                                  */
/* ---------------------------------------------------------------------- */

struct bitreader {
	const uint8_t *p;
	size_t cap;        // readable bytes; reads past them see zero bits
	size_t pos;        // in bits
};

static inline uint64_t br_window(const struct bitreader *b)
{
	size_t i = b->pos >> 3;
	uint64_t w;
	if (i + 8 <= b->cap) {
		// Only in bounds: past the data, b->p + i would point outside it.
		const uint8_t *p = b->p + i;
		w = (uint64_t)p[0] << 56 | (uint64_t)p[1] << 48 | (uint64_t)p[2] << 40 | (uint64_t)p[3] << 32
			| (uint64_t)p[4] << 24 | (uint64_t)p[5] << 16 | (uint64_t)p[6] << 8 | p[7];
	} else {
		w = 0;
		for (size_t k = 0; k < 8; k++)
			w = w << 8 | (i + k < b->cap ? b->p[i + k] : 0);
	}
	return w << (b->pos & 7);
}

// The next n bits (1 <= n <= 32) without consuming them.
static inline uint32_t br_peek(const struct bitreader *b, unsigned n)
{
	return (uint32_t)(br_window(b) >> (64 - n));
}

static inline uint32_t br_get(struct bitreader *b, unsigned n)
{
	uint32_t v = br_peek(b, n);
	b->pos += n;
	return v;
}

static inline int vlc_get(struct bitreader *b, const struct vlc_entry *t, unsigned width, bool *ok)
{
	const struct vlc_entry *e = &t[br_peek(b, width)];
	if (!e->len) {
		*ok = false;
		return 0;
	}
	b->pos += e->len;
	return e->value;
}

/* ---------------------------------------------------------------------- */
/* Blocks                                                                 */
/* ---------------------------------------------------------------------- */

// Dequantised values: saturate to 12 bits, else make even values odd away
// from zero (0 becomes -1) (0x44b3f2, 0x44b5de).
static inline int sat_odd(int v)
{
	if (v > 2047)
		return 2047;
	if (v < -2048)
		return -2048;
	if (!(v & 1))
		v += v > 0 ? 1 : -1;
	return v;
}

// Two passes of the AAN butterfly (0x44afd0). Every product is divided by
// 256 truncating toward zero, except one term of the column pass, which is
// shifted (rounds toward minus infinity). The output is not clamped.
static void idct(const int32_t in[64], int16_t out[64])
{
	int64_t b[64];
	for (int i = 0; i < 64; i++)
		b[i] = in[i];
	for (int i = 0; i < 8; i++) {
		int64_t s0 = b[i], s1 = b[8+i], s2 = b[16+i], s3 = b[24+i];
		int64_t s4 = b[32+i], s5 = b[40+i], s6 = b[48+i], s7 = b[56+i];
		int64_t b3 = s2 + s6, b4 = s5 - s3, t1 = s1 + s7, t2 = s3 + s5, b6 = s1 - s7, b7 = t1 + t2;
		int64_t x4 = (b6 * 473 - b4 * 196 + 128) / 256 - b7;
		int64_t x0 = x4 - (((t1 - t2) * 362 + 128) >> 8);
		int64_t x1 = s0 - s4, x2 = ((s2 - s6) * 362 + 128) / 256 - b3, x3 = s0 + s4;
		int64_t y3 = x1 + x2, y4 = x3 + b3, y5 = x1 - x2, y6 = x3 - b3;
		int64_t y7 = -x0 - (b4 * 473 + b6 * 196 + 128) / 256;
		b[i] = b7 + y4;
		b[8+i] = x4 + y3;
		b[16+i] = y5 - x0;
		b[24+i] = y6 - y7;
		b[32+i] = y6 + y7;
		b[40+i] = x0 + y5;
		b[48+i] = y3 - x4;
		b[56+i] = y4 - b7;
	}
	for (int r = 0; r < 64; r += 8) {
		int64_t s0 = b[r], s1 = b[r+1], s2 = b[r+2], s3 = b[r+3];
		int64_t s4 = b[r+4], s5 = b[r+5], s6 = b[r+6], s7 = b[r+7];
		int64_t b3 = s2 + s6, b4 = s5 - s3, t1 = s1 + s7, t2 = s3 + s5, b6 = s1 - s7, b7 = t1 + t2;
		int64_t x4 = (b6 * 473 - b4 * 196 + 128) / 256 - b7;
		int64_t x0 = x4 - ((t1 - t2) * 362 + 128) / 256;
		int64_t x1 = s0 - s4, x2 = ((s2 - s6) * 362 + 128) / 256 - b3, x3 = s0 + s4;
		int64_t y3 = x1 + x2, y4 = x3 + b3, y5 = x1 - x2, y6 = x3 - b3;
		int64_t y7 = -x0 - (b4 * 473 + b6 * 196 + 128) / 256;
		out[r] = (int16_t)((b7 + y4 + 128) / 256);
		out[r+1] = (int16_t)((x4 + y3 + 128) / 256);
		out[r+2] = (int16_t)((y5 - x0 + 128) / 256);
		out[r+3] = (int16_t)((y6 - y7 + 128) / 256);
		out[r+4] = (int16_t)((y6 + y7 + 128) / 256);
		out[r+5] = (int16_t)((x0 + y5 + 128) / 256);
		out[r+6] = (int16_t)((y3 - x4 + 128) / 256);
		out[r+7] = (int16_t)((y4 - b7 + 128) / 256);
	}
}

// Reads a run/level pair. Returns false on an invalid code; *run is COEF_EOB
// at the end of the block. first: the first coefficient of a non-intra block,
// where "0s" is (0, +-1).
static bool coef_get(struct apeg *a, struct bitreader *b, bool first, int *run, int *level)
{
	uint32_t w = br_peek(b, COEF_BITS);
	if (first && !(w & 0x8000)) {
		*run = 0;
		*level = (w & 0x4000) ? -1 : 1;
		b->pos += 2;
		return true;
	}
	const struct coef_entry *e = &a->t.coef[w];
	if (e->run == COEF_BAD)
		return false;
	b->pos += e->len;
	if (e->run != COEF_ESC) {
		*run = e->run;
		*level = e->level;
		return true;
	}
	uint32_t v = br_get(b, 14);
	int lv = v & 0xff;
	*run = v >> 8;
	if (lv == 0)
		lv = br_get(b, 8);
	else if (lv == 0x80)
		lv = (int)br_get(b, 8) - 256;
	else if (lv > 0x80)
		lv -= 256;
	*level = lv;
	return true;
}

static void coef_clear(struct apeg *a, const uint8_t *written, int n)
{
	a->coef[0] = 0;
	for (int i = 0; i < n; i++)
		a->coef[written[i]] = 0;
}

// Fills the block with the IDCT of a->coef (or the DC-only value) and clears
// a->coef. last: natural position of the last coefficient written.
static void block_finish(struct apeg *a, int last, const uint8_t *written, int n, int16_t out[64])
{
	if (last == 0) {
		// DC only (0x44b70c, 0x44b53c): no IDCT and no +128 rounding.
		int16_t v = (int16_t)(a->coef[0] / 256);
		for (int i = 0; i < 64; i++)
			out[i] = v;
	} else {
		idct(a->coef, out);
	}
	coef_clear(a, written, n);
}

static bool intra_block(struct apeg *a, struct bitreader *b, int dc, int qs, int16_t out[64])
{
	uint8_t written[64];
	int n = 0, i = 1, last = 0;
	a->coef[0] = sat_odd(dc) * 32;
	for (;;) {
		int run, lv;
		if (!coef_get(a, b, false, &run, &lv)) {
			a->error = "APEG: invalid coefficient code";
			goto fail;
		}
		if (run == COEF_EOB)
			break;
		i += run;
		if (i > 63) {
			a->error = "APEG: coefficient position past 63";
			goto fail;
		}
		int p = zigzag[i];
		a->coef[p] = idct_scale[p] * sat_odd(a->qi[p] * lv * qs * 2 / 16);
		written[n++] = p;
		last = p;
		i++;
	}
	block_finish(a, last, written, n, out);
	return true;
fail:
	coef_clear(a, written, n);
	return false;
}

static bool inter_block(struct apeg *a, struct bitreader *b, int qs, int16_t out[64])
{
	uint8_t written[64];
	int n = 0, i = 0, last = -1, run, lv;
	if (!coef_get(a, b, true, &run, &lv)) {
		a->error = "APEG: invalid coefficient code";
		goto fail;
	}
	i = run;
	for (;;) {
		if (i > 63) {
			a->error = "APEG: coefficient position past 63";
			goto fail;
		}
		int p = zigzag[i];
		a->coef[p] = idct_scale[p] * sat_odd((2 * lv + (lv > 0 ? 1 : -1)) * a->qn[p] * qs / 16);
		written[n++] = p;
		last = p;
		if (!coef_get(a, b, false, &run, &lv)) {
			a->error = "APEG: invalid coefficient code";
			goto fail;
		}
		if (run == COEF_EOB)
			break;
		i += 1 + run;
	}
	block_finish(a, last, written, n, out);
	return true;
fail:
	coef_clear(a, written, n);
	return false;
}

/* ---------------------------------------------------------------------- */
/* Prediction                                                             */
/* ---------------------------------------------------------------------- */

// One n x n prediction (n = 16 luma, 8 chroma) from ref at (x0, y0) moved by
// the half-sample vector (vx, vy): (vx >> 1, vy >> 1) whole samples plus a
// half sample where vx & 1, vy & 1 (0x44c6e0, 0x44ef10 .. 0x44f430). The
// averages truncate toward zero, as the reference samples may be negative.
static void predict(int32_t *out, const int16_t *ref, int stride, size_t size,
		int x0, int y0, int n, int vx, int vy)
{
	int hx = vx & 1, hy = vy & 1;
	int64_t base = (int64_t)(y0 + (vy >> 1)) * stride + x0 + (vx >> 1);
	int64_t hi = base + (int64_t)(n - 1 + hy) * stride + (n - 1 + hx);
	if (base >= 0 && hi < (int64_t)size) {
		const int16_t *r = ref + base;
		for (int y = 0; y < n; y++, r += stride, out += n) {
			switch (hy << 1 | hx) {
			case 0:
				for (int x = 0; x < n; x++)
					out[x] = r[x];
				break;
			case 1:
				for (int x = 0; x < n; x++)
					out[x] = (r[x] + r[x+1] + 1) / 2;
				break;
			case 2:
				for (int x = 0; x < n; x++)
					out[x] = (r[x] + r[x+stride] + 1) / 2;
				break;
			default:
				for (int x = 0; x < n; x++)
					out[x] = (r[x] + r[x+1] + r[x+stride] + r[x+stride+1] + 2) / 4;
				break;
			}
		}
		return;
	}
	// Outside the plane: the original reads past its buffer here; read each
	// sample at its linear address clamped into the plane instead.
	for (int y = 0; y < n; y++, out += n) {
		for (int x = 0; x < n; x++) {
			int32_t s[4];
			for (int k = 0; k < 4; k++) {
				int64_t i = base + (int64_t)(y + (k >> 1)) * stride + x + (k & 1);
				s[k] = ref[i < 0 ? 0 : i >= (int64_t)size ? (int64_t)size - 1 : i];
			}
			switch (hy << 1 | hx) {
			case 0: out[x] = s[0]; break;
			case 1: out[x] = (s[0] + s[1] + 1) / 2; break;
			case 2: out[x] = (s[0] + s[2] + 1) / 2; break;
			default: out[x] = (s[0] + s[1] + s[2] + s[3] + 2) / 4; break;
			}
		}
	}
}

struct mb_pred {
	const struct apeg_slot *ref[2];  // ref[1] only for bidirectional prediction
	int vx[2], vy[2];
};

// Adds the prediction to the residual already in dst (16-bit wrap-around, no
// clamping): one direction (0x44bbe0) or the truncating average of the two
// separately rounded predictions (0x44bd20).
static void add_prediction(struct apeg *a, struct apeg_slot *dst, int mb, const struct mb_pred *m)
{
	int x0 = (mb % a->mbw) * 16, y0 = (mb / a->mbw) * 16;
	int32_t p0[256], p1[256];
	int nref = m->ref[1] ? 2 : 1;
	for (int plane = 0; plane < 3; plane++) {
		int n = plane ? 8 : 16, stride = plane ? a->cw : a->w16;
		int px = plane ? x0 / 2 : x0, py = plane ? y0 / 2 : y0;
		size_t size = plane ? a->chroma_size : a->luma_size;
		for (int k = 0; k < nref; k++) {
			const struct apeg_slot *r = m->ref[k];
			const int16_t *src = plane == 0 ? r->y : plane == 1 ? r->cb : r->cr;
			int vx = m->vx[k], vy = m->vy[k];
			if (plane) {
				// Chroma: the luma vector halved toward zero (0x44bc61).
				vx /= 2;
				vy /= 2;
			}
			predict(k ? p1 : p0, src, stride, size, px, py, n, vx, vy);
		}
		int16_t *d = (plane == 0 ? dst->y : plane == 1 ? dst->cb : dst->cr) + py * stride + px;
		const int32_t *q0 = p0, *q1 = p1;
		for (int y = 0; y < n; y++, d += stride, q0 += n, q1 += n) {
			if (nref == 1) {
				for (int x = 0; x < n; x++)
					d[x] = (int16_t)(d[x] + q0[x]);
			} else {
				for (int x = 0; x < n; x++)
					d[x] = (int16_t)(d[x] + (q0[x] + q1[x] + 1) / 2);
			}
		}
	}
}

// Writes the six residual blocks (Y0 Y1 Y2 Y3 Cb Cr; NULL: all zero) of
// macroblock mb (0x44b850).
static void store_blocks(struct apeg *a, struct apeg_slot *dst, int mb, int16_t (*blk)[64])
{
	int x0 = (mb % a->mbw) * 16, y0 = (mb / a->mbw) * 16;
	for (int b = 0; b < 6; b++) {
		int16_t *d;
		int stride;
		if (b < 4) {
			stride = a->w16;
			d = dst->y + (y0 + (b >> 1) * 8) * stride + x0 + (b & 1) * 8;
		} else {
			stride = a->cw;
			d = (b == 4 ? dst->cb : dst->cr) + (y0 / 2) * stride + x0 / 2;
		}
		for (int r = 0; r < 8; r++, d += stride) {
			if (blk)
				memcpy(d, blk[b] + r * 8, 8 * sizeof(int16_t));
			else
				memset(d, 0, 8 * sizeof(int16_t));
		}
	}
}

// P skip: the macroblock copied from the reference (0x44bb10).
static void copy_mb(struct apeg *a, struct apeg_slot *dst, int mb, const struct apeg_slot *ref)
{
	int x0 = (mb % a->mbw) * 16, y0 = (mb / a->mbw) * 16;
	for (int r = 0; r < 16; r++) {
		size_t o = (size_t)(y0 + r) * a->w16 + x0;
		memcpy(dst->y + o, ref->y + o, 16 * sizeof(int16_t));
	}
	for (int r = 0; r < 8; r++) {
		size_t o = (size_t)(y0 / 2 + r) * a->cw + x0 / 2;
		memcpy(dst->cb + o, ref->cb + o, 8 * sizeof(int16_t));
		memcpy(dst->cr + o, ref->cr + o, 8 * sizeof(int16_t));
	}
}

/* ---------------------------------------------------------------------- */
/* Pictures                                                               */
/* ---------------------------------------------------------------------- */

// Motion vector update (0x45e170): pred plus or minus
// ((code - 1) << r) + residual + 1, wrapped into [-16 << r, (16 << r) - 1].
static int motion_update(struct apeg *a, struct bitreader *b, int pred, int r, bool *ok)
{
	int code = vlc_get(b, a->t.motion, MOTION_BITS, ok);
	if (!code)
		return pred;
	bool neg = br_get(b, 1);
	int res = r ? (int)br_get(b, r) : 0;
	int d = ((code - 1) << r) + res + 1;
	pred += neg ? -d : d;
	if (pred < -(16 << r))
		pred += 32 << r;
	else if (pred > (16 << r) - 1)
		pred -= 32 << r;
	return pred;
}

static bool load_payload(struct apeg *a, const struct apeg_chunk *c)
{
	size_t need = (size_t)c->size + BITS_PAD;
	if (need > a->buf_cap) {
		void *p = realloc(a->buf, need);
		if (!p)
			return false;
		a->buf = p;
		a->buf_cap = need;
	}
	memset(a->buf + c->size, 0, BITS_PAD);
	return read_at(a, c->offset, a->buf, c->size);
}

#define FAIL(msg) do { a->error = (msg); return false; } while (0)

static bool decode_picture(struct apeg *a, int index, struct apeg_slot *dst)
{
	const struct apeg_chunk *c = &a->pics[index];
	char kind = c->type;
	if (!load_payload(a, c))
		FAIL(err_read);
	struct bitreader br = { .p = a->buf, .cap = (size_t)c->size + BITS_PAD, .pos = 32 };
	const size_t nbits = (size_t)c->size * 8;
	int kf = 1, kb = 1, rf = 0, rb = 0;
	const struct apeg_slot *past = NULL, *future = NULL;
	if (kind != 'I') {
		// P: full_pel, r_size, 4 bits not used; B: forward and backward
		// full_pel and r_size (0x45d240, 0x45ce74).
		if (c->size < 5)
			FAIL("APEG: picture header truncated");
		uint32_t h = br_get(&br, 8);
		kf = (h & 0x80) ? 2 : 1;
		rf = (h >> 4) & 7;
		if (kind == 'B') {
			kb = (h & 8) ? 2 : 1;
			rb = h & 7;
		}
		if (a->future < 0)
			FAIL(err_no_reference);
		future = &a->anchor[a->future];
		if (a->past >= 0)
			past = &a->anchor[a->past];
	}

	memset(dst->y, 0, a->luma_size * sizeof(int16_t));
	memset(dst->cb, 0, a->chroma_size * sizeof(int16_t));
	memset(dst->cr, 0, a->chroma_size * sizeof(int16_t));
	dst->index = -1;

	// Start of every picture (0x45c6d0).
	int qs = 1, dcp[3] = { 128, 128, 128 };
	int mfx = 0, mfy = 0, mbx = 0, mby = 0;
	int16_t blk[6][64];
	bool ok = true;
	for (int mb = 0; mb < a->nmb; ) {
		int t, cbp = 0, skip = 0;
		if (kind == 'I') {
			t = 1;
			if (br_get(&br, 1))
				qs = br_get(&br, 5);
		} else {
			t = vlc_get(&br, kind == 'P' ? a->t.type_p : a->t.type_b, TYPE_BITS, &ok);
			if (!ok)
				FAIL("APEG: invalid macroblock type");
			if (kind == 'P') {
				if (t & 8) {
					mfx = motion_update(a, &br, mfx, rf, &ok);
					mfy = motion_update(a, &br, mfy, rf, &ok);
				} else {
					mfx = mfy = mbx = mby = 0;
				}
			} else {
				if (t & 8) {
					mfx = motion_update(a, &br, mfx, rf, &ok);
					mfy = motion_update(a, &br, mfy, rf, &ok);
				}
				if (t & 4) {
					mbx = motion_update(a, &br, mbx, rb, &ok);
					mby = motion_update(a, &br, mby, rb, &ok);
				}
				if (!(t & 0xc))
					mfx = mfy = mbx = mby = 0;
			}
			if (!ok)
				FAIL("APEG: invalid motion code");
			if (t & 2) {
				cbp = vlc_get(&br, a->t.cbp, CBP_BITS, &ok);
				if (!ok || cbp < 0)
					FAIL("APEG: invalid coded block pattern");
			}
			if (t & 16)
				qs = br_get(&br, 5);
			for (;;) {
				int s = vlc_get(&br, a->t.skip, SKIP_BITS, &ok);
				if (!ok)
					FAIL("APEG: invalid skip run");
				if (s >= 0) {
					skip += s;
					break;
				}
				skip += 35;
				if (skip > a->nmb)
					FAIL("APEG: skip run past the last macroblock");
			}
			if (skip > a->nmb - 1 - mb)
				FAIL("APEG: skip run past the last macroblock");
		}

		struct mb_pred m = { 0 };
		if (t & 1) {
			// Intra: the DC predictions carry on from the last intra
			// macroblock (0x45cb20).
			for (int b = 0; b < 6; b++) {
				int comp = b < 4 ? 0 : b - 3;
				int n = vlc_get(&br, b < 4 ? a->t.dc_luma : a->t.dc_chroma, DC_BITS, &ok);
				if (!ok)
					FAIL("APEG: invalid DC size");
				if (n) {
					int v = br_get(&br, n);
					dcp[comp] += (v >> (n - 1)) ? v : v + 1 - (1 << n);
				}
				if (!intra_block(a, &br, dcp[comp] * 8, qs, blk[b]))
					return false;
			}
			store_blocks(a, dst, mb, blk);
		} else {
			dcp[0] = dcp[1] = dcp[2] = 128;
			for (int b = 0; b < 6; b++) {
				if (!(cbp & (32 >> b)))
					memset(blk[b], 0, sizeof blk[b]);
				else if (!inter_block(a, &br, qs, blk[b]))
					return false;
			}
			store_blocks(a, dst, mb, blk);
			if (kind == 'P') {
				m.ref[0] = future;
				m.vx[0] = mfx * kf;
				m.vy[0] = mfy * kf;
			} else if (!(t & 4)) {
				m.ref[0] = past;
				m.vx[0] = mfx * kf;
				m.vy[0] = mfy * kf;
			} else if (!(t & 8)) {
				m.ref[0] = future;
				m.vx[0] = mbx * kb;
				m.vy[0] = mby * kb;
			} else {
				m.ref[0] = past;
				m.vx[0] = mfx * kf;
				m.vy[0] = mfy * kf;
				m.ref[1] = future;
				m.vx[1] = mbx * kb;
				m.vy[1] = mby * kb;
				if (!past)
					FAIL(err_no_past);
			}
			if (!m.ref[0])
				FAIL(err_no_past);
			add_prediction(a, dst, mb, &m);
		}

		if (skip) {
			dcp[0] = dcp[1] = dcp[2] = 128;
			if (kind == 'P') {
				// P skip: copied from the reference; vectors reset (0x45cf06).
				mfx = mfy = mbx = mby = 0;
				for (int k = 1; k <= skip; k++)
					copy_mb(a, dst, mb + k, future);
			} else {
				// B skip: the coded macroblock's type and vectors with no
				// residual; the vectors reset only when the run reaches
				// the end of the picture (0x45d06e).
				if (mb + 1 + skip >= a->nmb) {
					mfx = mfy = mbx = mby = 0;
					if (!(t & 1)) {
						int nref = m.ref[1] ? 2 : 1;
						for (int k = 0; k < nref; k++)
							m.vx[k] = m.vy[k] = 0;
					}
				}
				for (int k = 1; k <= skip; k++) {
					store_blocks(a, dst, mb + k, NULL);
					if (!(t & 1))
						add_prediction(a, dst, mb + k, &m);
				}
			}
		}
		mb += 1 + skip;
		if (br.pos > nbits)
			FAIL("APEG: picture data truncated");
	}
	dst->index = index;
	return true;
}

static int free_anchor_slot(const struct apeg *a)
{
	for (int i = 0; i < 3; i++)
		if (i != a->past && i != a->future)
			return i;
	return 0;  // not reached: at most two slots are references
}

// Decodes picture i in sequence. I and P pictures become the new future
// reference and the old one the past reference (0x45cd70, 0x45ce10); B
// pictures leave the references alone. A failure is remembered in broken[]
// only when it comes from the picture's own data, not from a missing
// reference or a read error.
static bool decode_one(struct apeg *a, int i)
{
	a->nr_decoded++;
	if (a->pics[i].type == 'B') {
		if (!decode_picture(a, i, &a->bpic)) {
			if (!missing_reference(a->error) && a->error != err_read)
				a->broken[i] = 1;
			return false;
		}
		return true;
	}
	bool first = a->future < 0;
	int s = free_anchor_slot(a);
	if (!decode_picture(a, i, &a->anchor[s])) {
		if (!missing_reference(a->error) && a->error != err_read)
			a->broken[i] = 1;
		a->past = a->future = -1;
		a->chain_broken = true;
		return false;
	}
	a->past = a->future;
	a->future = s;
	if (a->pics[i].type == 'I') {
		a->chain_broken = false;
		if (first)  // after a broken chain: the references start here
			a->seq_start = i;
	}
	return true;
}

static const struct apeg_picture *make_picture(struct apeg *a, const struct apeg_slot *s)
{
	const struct apeg_chunk *c = &a->pics[s->index];
	a->pic = (struct apeg_picture){
		.index = s->index, .ts = c->ts, .type = c->type,
		.width = a->width, .height = a->height,
		.luma_stride = a->w16, .luma_rows = a->h16,
		.chroma_stride = a->cw, .chroma_rows = a->h16 / 2,
		.y = s->y, .cb = s->cb, .cr = s->cr,
	};
	return &a->pic;
}

int apeg_decode_next(struct apeg *a, const struct apeg_picture **out)
{
	a->error = NULL;
	if (a->next >= a->nr_pics)
		return 0;
	int i = a->next++;
	if (!decode_one(a, i))
		return -1;
	*out = make_picture(a, a->pics[i].type == 'B' ? &a->bpic : &a->anchor[a->future]);
	return 1;
}

static const struct apeg_slot *decoded(const struct apeg *a, int d)
{
	if (a->pics[d].type == 'B')
		return a->bpic.index == d ? &a->bpic : NULL;
	if (a->future >= 0 && a->anchor[a->future].index == d)
		return &a->anchor[a->future];
	if (a->past >= 0 && a->anchor[a->past].index == d)
		return &a->anchor[a->past];
	return NULL;
}

static int decode_cost(const struct apeg *a, int from, int d)
{
	return a->anchors_upto[d + 1] - a->anchors_upto[from] + (a->pics[d].type == 'B');
}

const struct apeg_picture *apeg_picture_at(struct apeg *a, int64_t time_ms)
{
	a->error = NULL;
	// The latest picture whose timestamp is <= time_ms.
	int lo = 0, hi = a->nr_pics;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if ((int64_t)a->pics[a->by_ts[mid]].ts <= time_ms)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo == 0)
		return NULL;
	int d = a->by_ts[lo - 1];

	const struct apeg_slot *s = decoded(a, d);
	if (s)
		return make_picture(a, s);
	if (a->broken[d]) {
		a->error = "APEG: picture cannot be decoded";
		return NULL;
	}
	int start = a->seek[d];
	if (start < 0) {
		a->error = "APEG: no I picture before this picture";
		return NULL;
	}
	// The references decoded so far serve d only when they were decoded
	// from start or earlier, without a failure since.
	bool chain = !a->chain_broken && a->seq_start <= start;
	if (chain && a->pics[d].type == 'B' && a->next > d && a->future >= 0
	    && a->anchors_upto[a->next] == a->anchors_upto[d]) {
		// Only B pictures since d (the clock went back a little): d's
		// references are still the current ones, so decode just d again.
		a->next = d;
	} else if (!chain || a->next > d || a->next < start
		   || decode_cost(a, a->next, d) > decode_cost(a, start, d)) {
		// Start again at the I picture: the references in hand do not
		// serve d, d was already passed, or d lies in a later GOP. (The
		// cost test only holds together with next < start, since
		// anchors_upto never decreases; it is kept as a guard.)
		for (int i = start; i <= d; i++) {
			if (a->pics[i].type != 'B' && a->broken[i]) {
				a->broken[d] = 1;
				a->error = "APEG: a reference picture cannot be decoded";
				return NULL;
			}
		}
		restart(a, start);
		a->nr_restarts++;
	}
	while (a->next <= d) {
		int i = a->next++;
		if (a->pics[i].type == 'B' && i != d)
			continue;  // not a reference
		if (!decode_one(a, i)) {
			if (i != d ? a->broken[i] : missing_reference(a->error)) {
				// A reference with bad data; or the references are all
				// there are (decoded from start), so d lacks one in
				// the file itself: never retried.
				a->broken[d] = 1;
			}
			return NULL;
		}
	}
	s = decoded(a, d);
	return s ? make_picture(a, s) : NULL;
}

/* ---------------------------------------------------------------------- */
/* Output                                                                 */
/* ---------------------------------------------------------------------- */

static inline uint8_t unorm8(double v)
{
	if (v < 0.0)
		v = 0.0;
	else if (v > 1.0)
		v = 1.0;
	double s = v * 255.0;
	return (uint8_t)(s + 0.5);
}

static inline int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

// Chroma row j (clamped to 16..240, scaled to UNORM) upsampled horizontally:
// sample 2k = 0.75 C[k] + 0.25 C[k-1], 2k+1 = 0.75 C[k] + 0.25 C[k+1], edges
// clamped (the bilinear sampler at x/2 - 0.25).
static const double *chroma_row(struct apeg *a, int plane, const int16_t *src, int cw, int j)
{
	int slot = j % 3;
	double *out = a->chroma_rows[plane][slot];
	if (a->chroma_row_no[plane][slot] == j)
		return out;
	const int16_t *s = src + (size_t)j * cw;
	double *c = a->chroma_tmp;
	for (int k = 0; k < cw; k++)
		c[k] = a->unorm[clampi(s[k], 16, 240)];
	for (int k = 0; k < cw; k++) {
		double c75 = 0.75 * c[k];
		double l25 = 0.25 * c[k > 0 ? k - 1 : 0];
		double r25 = 0.25 * c[k < cw - 1 ? k + 1 : cw - 1];
		out[2*k] = c75 + l25;
		out[2*k+1] = c75 + r25;
	}
	a->chroma_row_no[plane][slot] = j;
	return out;
}

static inline double unorm_clamp(double v)
{
	v = v < 0.0 ? 0.0 : v;
	return v > 1.0 ? 1.0 : v;
}

void apeg_picture_to_rgba(struct apeg *a, const struct apeg_picture *p, uint8_t *rgba, size_t pitch)
{
	int cw = p->chroma_stride, ch = p->chroma_rows;
	for (int plane = 0; plane < 2; plane++)
		for (int i = 0; i < 3; i++)
			a->chroma_row_no[plane][i] = -1;
	const double m01 = ycc_matrix[0][1], m02 = ycc_matrix[0][2], o0 = ycc_offset[0];
	const double m11 = ycc_matrix[1][1], m12 = ycc_matrix[1][2], o1 = ycc_offset[1];
	const double m21 = ycc_matrix[2][1], m22 = ycc_matrix[2][2], o2 = ycc_offset[2];
	for (int y = 0; y < p->height; y++) {
		int j = y >> 1;
		int j2 = (y & 1) ? (j + 1 < ch ? j + 1 : ch - 1) : (j > 0 ? j - 1 : 0);
		const double *b0 = chroma_row(a, 0, p->cb, cw, j), *b1 = chroma_row(a, 0, p->cb, cw, j2);
		const double *r0 = chroma_row(a, 1, p->cr, cw, j), *r1 = chroma_row(a, 1, p->cr, cw, j2);
		const int16_t *ys = p->y + (size_t)y * p->luma_stride;
		uint8_t *o = rgba + (size_t)y * pitch;
		for (int x = 0; x < p->width; x++, o += 4) {
			int yi = clampi(ys[x], 16, 235);
			// Every product and sum rounded on its own, in this order.
			double cb75 = 0.75 * b0[x], cb25 = 0.25 * b1[x];
			double cr75 = 0.75 * r0[x], cr25 = 0.25 * r1[x];
			double cb = cb75 + cb25, cr = cr75 + cr25;
			double tb0 = m01 * cb, tr0 = m02 * cr;
			double tb1 = m11 * cb, tr1 = m12 * cr;
			double tb2 = m21 * cb, tr2 = m22 * cr;
			double s0 = a->luma_term[0][yi] + tb0, s1 = a->luma_term[1][yi] + tb1, s2 = a->luma_term[2][yi] + tb2;
			s0 = s0 + tr0;
			s1 = s1 + tr1;
			s2 = s2 + tr2;
			s0 = unorm_clamp(s0 + o0) * 255.0;
			s1 = unorm_clamp(s1 + o1) * 255.0;
			s2 = unorm_clamp(s2 + o2) * 255.0;
			o[0] = (uint8_t)(s0 + 0.5);
			o[1] = (uint8_t)(s1 + 0.5);
			o[2] = (uint8_t)(s2 + 0.5);
			o[3] = 255;
		}
	}
}

void apeg_ycbcr_to_rgb(int y, int cb, int cr, uint8_t rgb[3])
{
	double yv = clampi(y, 0, 255) / 255.0;
	double cbv = clampi(cb, 0, 255) / 255.0;
	double crv = clampi(cr, 0, 255) / 255.0;
	for (int k = 0; k < 3; k++) {
		double ty = ycc_matrix[k][0] * yv;
		double tb = ycc_matrix[k][1] * cbv;
		double tr = ycc_matrix[k][2] * crv;
		double s = ty + tb;
		s = s + tr;
		s = s + ycc_offset[k];
		rgb[k] = unorm8(s);
	}
}
