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

static void Math_SetSeedByCurrentTime(void)
{
	srand(time(NULL));
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

static float Math_RandF(void)
{
	return rand() * (1.0 / (RAND_MAX + 1U));
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
			mt_init(5489);
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
	    HLL_EXPORT(Abs, abs),
	    HLL_EXPORT(AbsF, fabsf),
	    HLL_EXPORT(Pow, powf),
	    HLL_EXPORT(SetSeed, srand),
	    HLL_EXPORT(SetSeedByCurrentTime, Math_SetSeedByCurrentTime),
	    //HLL_EXPORT(SetRandMode, Math_SetRandMode),
	    HLL_EXPORT(Rand, rand),
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

