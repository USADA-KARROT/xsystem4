/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <cglm/cglm.h>

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#include "hll.h"
#include "vm/page.h"

struct shuffle_table {
	int id;
	struct shuffle_table *next;
	int *elems;
	int size;
	int i;
};
static struct shuffle_table *shuffle_tables;

static struct shuffle_table *find_shuffle_table(int id)
{
	for (struct shuffle_table *tbl = shuffle_tables; tbl; tbl = tbl->next) {
		if (tbl->id == id)
			return tbl;
	}
	return NULL;
}

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

static inline float rad2deg(float rad)
{
	return rad * (180.0 / M_PI);
}

static float Math_Asin(float x)
{
	return rad2deg(asinf(x));
}

static float Math_Acos(float x)
{
	return rad2deg(acosf(x));
}

static float Math_Cos(float x)
{
	return cosf(deg2rad(x));
}

static float Math_Sin(float x)
{
	return sinf(deg2rad(x));
}

static float Math_Tan(float x)
{
	return tanf(deg2rad(x));
}

/* The original random number generator behind Math.SetSeed/SetSeedByCurrentTime/
 * Rand/RandF and Array.Shuffle: a 521-word lagged Fibonacci generator (p=521,
 * q=32, xor). 0x4c6e40 seeds it (17 words from a 32-step LCG x = x*0x5d588b65+1,
 * one bit per step; w[16] = w[16]<<23 ^ w[0]>>9 ^ w[15]; w[i+17] = w[i]<<23 ^
 * w[i+1]>>9 ^ w[i+16] for i < 504; four regenerations), 0x4c6d40 regenerates
 * it (w[i] ^= w[i+489] for i < 32, w[i] ^= w[i-32] after), and a draw is the
 * next word, regenerating after 521. Math keeps one global instance
 * ([0x87f8b4]); Array.Shuffle builds one on its stack per call. The MT entries
 * ([0x87f8b0]) are a separate generator and stay as they are below. */
static void sys4_rand521_regen(struct sys4_rand521 *r)
{
	for (int i = 0; i < 32; i++)
		r->w[i] ^= r->w[i + 489];
	for (int i = 32; i < 521; i++)
		r->w[i] ^= r->w[i - 32];
}

void sys4_rand521_seed(struct sys4_rand521 *r, uint32_t seed)
{
	uint32_t x = seed;
	for (int i = 0; i < 17; i++) {
		uint32_t acc = 0;
		for (int k = 0; k < 32; k++) {
			x = x * 0x5d588b65u + 1;
			acc = (acc >> 1) | (x & 0x80000000u);
		}
		r->w[i] = acc;
	}
	r->w[16] = (r->w[16] << 23) ^ (r->w[0] >> 9) ^ r->w[15];
	for (int i = 0; i < 504; i++)
		r->w[i + 17] = (r->w[i] << 23) ^ (r->w[i + 1] >> 9) ^ r->w[i + 16];
	for (int i = 0; i < 4; i++)
		sys4_rand521_regen(r);
	r->idx = -1;
}

uint32_t sys4_rand521_next(struct sys4_rand521 *r)
{
	if (++r->idx >= 521) {
		sys4_rand521_regen(r);
		r->idx = 0;
	}
	return r->w[r->idx];
}

/* 0x41b3c0 / 0x4c7060: timeGetTime() kept monotonic. Two calls within the same
 * millisecond get the same seed natively (so a second Shuffle(-1) repeats the
 * first permutation); here the value is also bumped past the previous one so
 * that back-to-back calls, which are far closer together than in the original
 * engine, never share a seed. XSYS4_RANDOM_SEED=<n> (testing only, unset by
 * default) replaces the clock: the k-th call returns n + k, so every
 * time-seeded generator gets a repeatable seed. */
uint32_t sys4_rand_time_seed(void)
{
	static int fixed = -1;
	static uint32_t fixed_seed, calls, last;
	if (fixed < 0) {
		const char *env = getenv("XSYS4_RANDOM_SEED");
		fixed = env && *env ? 1 : 0;
		if (fixed)
			fixed_seed = (uint32_t)strtoul(env, NULL, 0);
	}
	if (fixed)
		return fixed_seed + calls++;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	uint32_t now = (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
	if (now <= last)
		now = last + 1;
	last = now;
	return now;
}

/* Math's global generator. The R521 generator was read out of this game's v14
 * EXE only (0x4c673c SetSeed, 0x4c7060 SetSeedByCurrentTime, 0x4c676a Rand,
 * 0x4c70c0 RandF); every other AIN version keeps the C library srand/rand
 * these entries had before. */
static struct sys4_rand521 math_rand;
static bool math_rand_seeded;

static bool math_uses_r521(void)
{
	return ain->version >= 14;
}

static struct sys4_rand521 *math_rand_state(void)
{
	if (!math_rand_seeded) {
		sys4_rand521_seed(&math_rand, sys4_rand_time_seed());
		math_rand_seeded = true;
	}
	return &math_rand;
}

// [16] 0x4c673c
static void Math_SetSeed(int seed)
{
	if (!math_uses_r521()) {
		srand(seed);
		return;
	}
	sys4_rand521_seed(&math_rand, (uint32_t)seed);
	math_rand_seeded = true;
}

// [17] 0x4c7060. The C library is seeded here as well: the engine's own
// effects (DrawRain/DrawSnow/DrawRipple, motion jitter, particles) draw from
// rand() and got a fresh sequence from this call before.
static void Math_SetSeedByCurrentTime(void)
{
	if (!math_uses_r521()) {
		srand(time(NULL));
		return;
	}
	uint32_t seed = sys4_rand_time_seed();
	srand(seed);
	sys4_rand521_seed(&math_rand, seed);
	math_rand_seeded = true;
}

// [18] 0x4c676a: the next word & 0x7fffffff
static int Math_Rand(void)
{
	if (!math_uses_r521())
		return rand();
	return (int)(sys4_rand521_next(math_rand_state()) & 0x7fffffffu);
}

static int Math_Min(int a, int b)
{
	return a < b ? a : b;
}

static float Math_MinF(float a, float b)
{
	return a < b ? a : b;
}

static int Math_Max(int a, int b)
{
	return a > b ? a : b;
}

static int Math_Clamp(int val, int low, int high)
{
	if (val < low) return low;
	if (val > high) return high;
	return val;
}

static float Math_ClampF(float val, float low, float high)
{
	if (val < low) return low;
	if (val > high) return high;
	return val;
}

static float Math_MaxF(float a, float b)
{
	return a > b ? a : b;
}

/* v14 overloads: Abs/Min/Max/Clamp are declared once per shape under one
 * name. Semantics follow the native Math dispatcher (0x4c6520, table
 * 0x4c6ca0). The float helpers rely on IEEE NaN and signed-zero behaviour;
 * do not build this file with -ffast-math. */

// Abs(int): x86 cdq/xor/sub, so INT_MIN stays INT_MIN (libc abs is UB there).
static int Math_AbsI(int v)
{
	return v < 0 ? (int)(0u - (unsigned)v) : v;
}

// Abs(float): clears the sign bit; NaN stays NaN.
static float Math_AbsFloat(float v)
{
	return fabsf(v);
}

// MSVC UCRT fminf: a NaN operand yields the other one; ties prefer -0.0.
static float msvc_fminf(float x, float y)
{
	if (isnan(x))
		return y;
	if (isnan(y))
		return x;
	if (y < x)
		return y;
	if (y == x)
		return signbit(x) ? x : y;
	return x;
}

// MSVC UCRT fmaxf: a NaN operand yields the other one; ties prefer +0.0.
static float msvc_fmaxf(float x, float y)
{
	if (isnan(x))
		return y;
	if (isnan(y))
		return x;
	if (y > x)
		return y;
	if (y == x)
		return signbit(x) ? y : x;
	return x;
}

static int Math_Min3(int x, int y, int z)
{
	int r = x;
	if (y < r) r = y;
	if (z < r) r = z;
	return r;
}

static int Math_Min4(int x, int y, int z, int w)
{
	int r = Math_Min3(x, y, z);
	return w < r ? w : r;
}

static int Math_Max3(int x, int y, int z)
{
	int r = x;
	if (r < y) r = y;
	if (r < z) r = z;
	return r;
}

static int Math_Max4(int x, int y, int z, int w)
{
	int r = Math_Max3(x, y, z);
	return r < w ? w : r;
}

// Float Min/Max nest the same way as the native code:
// three arguments f(f(x, y), z), four arguments f(f(x, y), f(z, w)).
static float Math_Min2F(float x, float y)
{
	return msvc_fminf(x, y);
}

static float Math_Min3F(float x, float y, float z)
{
	return msvc_fminf(msvc_fminf(x, y), z);
}

static float Math_Min4F(float x, float y, float z, float w)
{
	return msvc_fminf(msvc_fminf(x, y), msvc_fminf(z, w));
}

static float Math_Max2F(float x, float y)
{
	return msvc_fmaxf(x, y);
}

static float Math_Max3F(float x, float y, float z)
{
	return msvc_fmaxf(msvc_fmaxf(x, y), z);
}

static float Math_Max4F(float x, float y, float z, float w)
{
	return msvc_fmaxf(msvc_fmaxf(x, y), msvc_fmaxf(z, w));
}

// Clamp(float value, float low, float high): low > value -> low;
// high > value -> value; else high. A NaN value returns high. No swap.
static float Math_Clamp3F(float value, float low, float high)
{
	if (low > value)
		return low;
	if (high > value)
		return value;
	return high;
}

static bool math_all_args(const struct ain_hll_function *f, enum ain_data_type t)
{
	for (int i = 0; i < f->nr_arguments; i++) {
		if (f->arguments[i].type.data != t)
			return false;
	}
	return true;
}

// The int two-argument Min/Max, int Clamp and int Abs keep their existing
// C functions; other shapes get their own implementation.
void *math_select_function(const struct ain_hll_function *f, void *fallback)
{
	if (!f || !f->name || !f->arguments)
		return fallback;
	int n = f->nr_arguments;
	bool flt = n > 0 && f->return_type.data == AIN_FLOAT && math_all_args(f, AIN_FLOAT);
	bool itg = n > 0 && f->return_type.data == AIN_INT && math_all_args(f, AIN_INT);
	if (!strcmp(f->name, "Abs")) {
		if (n == 1 && flt)
			return (void *)Math_AbsFloat;
		return n == 1 && itg ? (void *)Math_AbsI : fallback;
	}
	if (!strcmp(f->name, "Min")) {
		if (flt && n == 2) return (void *)Math_Min2F;
		if (flt && n == 3) return (void *)Math_Min3F;
		if (flt && n == 4) return (void *)Math_Min4F;
		if (itg && n == 3) return (void *)Math_Min3;
		if (itg && n == 4) return (void *)Math_Min4;
		return fallback;
	}
	if (!strcmp(f->name, "Max")) {
		if (flt && n == 2) return (void *)Math_Max2F;
		if (flt && n == 3) return (void *)Math_Max3F;
		if (flt && n == 4) return (void *)Math_Max4F;
		if (itg && n == 3) return (void *)Math_Max3;
		if (itg && n == 4) return (void *)Math_Max4;
		return fallback;
	}
	if (!strcmp(f->name, "Clamp") && flt && n == 3)
		return (void *)Math_Clamp3F;
	return fallback;
}

static void Math_Swap(int *a, int *b)
{
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

static void Math_SwapF(float *a, float *b)
{
	float tmp = *a;
	*a = *b;
	*b = tmp;
}

//void Math_SetRandMode(int mode);

// [19] 0x4c70c0: (double)(uint32)word * 2^-32 (0x813088), then to float. The
// rounding to float gives 1.0f for words >= 0xFFFFFF80, so the range is
// [0, 1] with 1.0 at probability 2^-25, as in the original.
static float Math_RandF(void)
{
	if (!math_uses_r521())
		return rand() * (1.0 / (RAND_MAX + 1U));
	return (float)((double)sys4_rand521_next(math_rand_state()) * 2.3283064365386963e-10);
}

static void shuffle_array(int *a, int len)
{
	for (int i = len - 1; i > 0; i--) {
		int j = Math_RandF() * i;
		int tmp = a[j];
		a[j] = a[i];
		a[i] = tmp;
	}
}

static void Math_RandTableInit(int num, int size)
{
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (tbl) {
		free(tbl->elems);
	} else {
		tbl = xmalloc(sizeof(struct shuffle_table));
		tbl->id = num;
		tbl->next = shuffle_tables;
		shuffle_tables = tbl;
	}
	tbl->elems = xmalloc(size * sizeof(int));
	tbl->size = size;
	tbl->i = 0;
	for (int i = 0; i < size; i++)
		tbl->elems[i] = i;
	shuffle_array(tbl->elems, size);
}

static int Math_RandTable(int num)
{
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (!tbl) {
		WARNING("Invalid rand table id %d", num);
		return 0;
	}
	if (tbl->i >= tbl->size) {
		// reshuffle
		shuffle_array(tbl->elems, tbl->size);
		tbl->i = 0;
	}
	return tbl->elems[tbl->i++];
}

//void Math_RandTable2Init(int num, struct page *array);
//int Math_RandTable2(int num);

static int Math_Ceil(float f)
{
	return (int)ceilf(f);
}

static int Math_Floor(float f)
{
	return (int)floorf(f);
}

static int Math_Round(float f)
{
	return (int)roundf(f);
}

/* Mersenne Twister MT19937 */
#define MT_N 624
#define MT_M 397
static uint32_t mt_state[MT_N];
static int mt_index = MT_N + 1;

static void mt_init(uint32_t seed)
{
	mt_state[0] = seed;
	for (int i = 1; i < MT_N; i++)
		mt_state[i] = 1812433253U * (mt_state[i-1] ^ (mt_state[i-1] >> 30)) + i;
	mt_index = MT_N;
}

static uint32_t mt_generate(void)
{
	if (mt_index >= MT_N) {
		if (mt_index > MT_N)
			// 0x4b516c: at engine start-up the original seeds the MT
			// generator from the clock too (0x4c7110 -> 0x4c63b0); other
			// AIN versions keep the fixed default seed.
			mt_init(ain->version >= 14 ? sys4_rand_time_seed() : 5489);
		for (int i = 0; i < MT_N; i++) {
			uint32_t y = (mt_state[i] & 0x80000000U) | (mt_state[(i+1) % MT_N] & 0x7fffffffU);
			mt_state[i] = mt_state[(i + MT_M) % MT_N] ^ (y >> 1);
			if (y & 1)
				mt_state[i] ^= 0x9908b0dfU;
		}
		mt_index = 0;
	}
	uint32_t y = mt_state[mt_index++];
	y ^= y >> 11;
	y ^= (y << 7) & 0x9d2c5680U;
	y ^= (y << 15) & 0xefc60000U;
	y ^= y >> 18;
	return y;
}

static void Math_MTSetSeed(int seed)
{
	mt_init((uint32_t)seed);
}

static void Math_MTSetSeedByCurrentTime(void)
{
	mt_init((uint32_t)time(NULL));
}

static int Math_MTRand(void)
{
	return (int)(mt_generate() >> 1); /* positive int */
}

static float Math_MTRandF(void)
{
	return mt_generate() * (1.0f / 4294967296.0f); /* [0, 1) */
}

static float Math_MTRandFInclude1(void)
{
	return mt_generate() * (1.0f / 4294967295.0f); /* [0, 1] */
}

static bool Math_BezierCurve(struct page **x_array, struct page **y_array, int num, float t, int *result_x, int *result_y)
{
	vec2 *coeffs = xmalloc(num * sizeof(vec2));
	for (int i = 0; i < num; i++) {
		coeffs[i][0] = (*x_array)->values[i].i;
		coeffs[i][1] = (*y_array)->values[i].i;
	}

	// De Casteljau's algorithm.
	for (; num > 1; num--) {
		for (int i = 0; i < num - 1; i++)
			glm_vec2_lerp(coeffs[i], coeffs[i + 1], t, coeffs[i]);
	}
	*result_x = coeffs[0][0];
	*result_y = coeffs[0][1];

	free(coeffs);
	return true;
}

HLL_LIBRARY(Math,
	    HLL_EXPORT(Cos, Math_Cos),
	    HLL_EXPORT(Sin, Math_Sin),
	    HLL_EXPORT(Tan, Math_Tan),
	    HLL_EXPORT(Sqrt, sqrtf),
	    HLL_EXPORT(Atan, atanf),
	    HLL_EXPORT(Atan2, atan2f),
	    HLL_EXPORT(Abs, Math_AbsI),
	    HLL_EXPORT(AbsF, fabsf),
	    HLL_EXPORT(Pow, powf),
	    HLL_EXPORT(SetSeed, Math_SetSeed),
	    HLL_EXPORT(SetSeedByCurrentTime, Math_SetSeedByCurrentTime),
	    //HLL_EXPORT(SetRandMode, Math_SetRandMode),
	    HLL_EXPORT(Rand, Math_Rand),
	    HLL_EXPORT(RandF, Math_RandF),
	    HLL_EXPORT(RandTableInit, Math_RandTableInit),
	    HLL_EXPORT(RandTable, Math_RandTable),
	    //HLL_EXPORT(RandTable2Init, Math_RandTable2Init),
	    //HLL_EXPORT(RandTable2, Math_RandTable2),
	    HLL_EXPORT(Min, Math_Min),
	    HLL_EXPORT(MinF, Math_MinF),
	    HLL_EXPORT(Max, Math_Max),
	    HLL_EXPORT(MaxF, Math_MaxF),
	    HLL_EXPORT(Swap, Math_Swap),
	    HLL_EXPORT(SwapF, Math_SwapF),
	    HLL_EXPORT(Log, logf),
	    HLL_EXPORT(Log10, log10f),
	    HLL_EXPORT(Ceil, Math_Ceil),
	    HLL_EXPORT(Floor, Math_Floor),
	    HLL_EXPORT(Round, Math_Round),
	    HLL_EXPORT(BezierCurve, Math_BezierCurve),
	    HLL_EXPORT(Clamp, Math_Clamp),
	    HLL_EXPORT(ClampF, Math_ClampF),
	    HLL_EXPORT(Asin, Math_Asin),
	    HLL_EXPORT(Acos, Math_Acos),
	    HLL_EXPORT(MTSetSeed, Math_MTSetSeed),
	    HLL_EXPORT(MTSetSeedByCurrentTime, Math_MTSetSeedByCurrentTime),
	    HLL_EXPORT(MTRand, Math_MTRand),
	    HLL_EXPORT(MTRandF, Math_MTRandF),
	    HLL_EXPORT(MTRandFInclude1, Math_MTRandFInclude1));

