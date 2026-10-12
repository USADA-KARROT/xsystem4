/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The save thumbnail of the v14 PartsEngine (Dohna Dohna):
 * PartsEngine.SaveThumbnail(SaveFileName, ThumbnailWidth), original 0x56d140.
 *
 * The script takes the picture before it opens the save list
 * (ThumbnailCapture::Capture: number 0, width 124), copies the file to the
 * save's number with system.CopySaveFile, and the list shows it as the CG
 * "<save>Thumb%04d.qnt" (parts_cg_set).
 *
 * PE_save_thumbnail (PartsFunc 6 of the games before) is another function:
 * its second argument divides the surface.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "parts.h"
#include "scene.h"
#include "xsystem4.h"

// A picture wider or higher than this is not made (the original has no such
// limit; the script asks for a width of 124).
#define THUMBNAIL_MAX 8192

/*
 * Reduces an RGBA picture to dw x dh RGB by the average over the area each
 * destination pixel covers: in units of 1/dw of a source pixel, destination
 * pixel x covers [x * sw, (x + 1) * sw) and source pixel k covers
 * [k * dw, (k + 1) * dw).
 *
 * This is the original's function (0x5a7a10 forwards to slot 0x3c of the
 * graphics library, 0x492d30): the weight of a source pixel is the product
 * of the two overlaps (0x492f10-0x492f86), each channel is summed in 64 bits
 * and divided by sw * sh with truncation (0x493011-0x493056), and the fourth
 * byte is not written.
 */
static uint8_t *thumbnail_reduce(const uint8_t *src, int sw, int sh, int dw, int dh)
{
	uint8_t *dst = xmalloc((size_t)dw * dh * 3);
	for (int y = 0; y < dh; y++) {
		int64_t y0 = (int64_t)y * sh, y1 = y0 + sh;
		for (int x = 0; x < dw; x++) {
			int64_t x0 = (int64_t)x * sw, x1 = x0 + sw;
			uint64_t acc[3] = { 0 };
			for (int64_t j = y0 / dh; j * dh < y1; j++) {
				int64_t wy = min(y1, (j + 1) * dh) - max(y0, j * dh);
				const uint8_t *p = src + (j * sw + x0 / dw) * 4;
				for (int64_t k = x0 / dw; k * dw < x1; k++, p += 4) {
					uint64_t w = wy * (min(x1, (k + 1) * dw) - max(x0, k * dw));
					acc[0] += w * p[0];
					acc[1] += w * p[1];
					acc[2] += w * p[2];
				}
			}
			// The weights of one pixel add up to sw * sh.
			uint8_t *d = dst + ((size_t)y * dw + x) * 3;
			for (int c = 0; c < 3; c++)
				d[c] = acc[c] / ((uint64_t)sw * sh);
		}
	}
	return dst;
}

// A QNT plane holds the difference of each pixel from the mean of the pixels
// above and to the left.
static uint8_t qnt_filtered(const uint8_t *rgb, int w, int x, int y, int c)
{
	int v = rgb[(y * w + x) * 3 + c];
	if (!x && !y)
		return v;
	if (!y)
		return rgb[(x - 1) * 3 + c] - v;
	if (!x)
		return rgb[(y - 1) * w * 3 + c] - v;
	return ((rgb[((y - 1) * w + x) * 3 + c] + rgb[(y * w + x - 1) * 3 + c]) >> 1) - v;
}

/*
 * The original's thumbnails are version 2 QNT files with a header of 68
 * bytes and no alpha plane; the planes (blue first, in 2x2 blocks, the odd
 * row and column filled with 0) are deflated at level 9. Measured on its
 * files: their planes deflated again at level 9 give the same bytes.
 * libsys4's writer makes a 52 byte header and always an alpha plane.
 */
static bool thumbnail_write_qnt(const char *path, const uint8_t *rgb, int w, int h)
{
	const int pw = (w + 1) & ~1, ph = (h + 1) & ~1;
	const size_t raw_size = (size_t)pw * ph * 3;
	uint8_t *raw = xmalloc(raw_size), *p = raw;
	for (int c = 2; c >= 0; c--) {
		for (int y = 0; y < ph; y += 2) {
			for (int x = 0; x < pw; x += 2) {
				// (x, y), (x, y+1), (x+1, y), (x+1, y+1)
				for (int i = 0; i < 4; i++) {
					int px = x + (i >> 1), py = y + (i & 1);
					*p++ = px < w && py < h ? qnt_filtered(rgb, w, px, py, c) : 0;
				}
			}
		}
	}

	unsigned long size = compressBound(raw_size);
	uint8_t *data = xmalloc(size);
	int r = compress2(data, &size, raw, raw_size, Z_BEST_COMPRESSION);
	free(raw);
	if (r != Z_OK) {
		WARNING("SaveThumbnail: compress2 failed (%d)", r);
		free(data);
		return false;
	}

	uint8_t header[68] = { 'Q', 'N', 'T', 0 };
	LittleEndian_putDW(header, 4, 2);
	LittleEndian_putDW(header, 8, sizeof(header));
	LittleEndian_putDW(header, 20, w);
	LittleEndian_putDW(header, 24, h);
	LittleEndian_putDW(header, 28, 24);
	LittleEndian_putDW(header, 32, 1);
	LittleEndian_putDW(header, 36, size);

	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("SaveThumbnail: cannot open %s: %s", display_utf0(path), strerror(errno));
		free(data);
		return false;
	}
	bool ok = fwrite(header, sizeof(header), 1, fp) == 1 && fwrite(data, size, 1, fp) == 1;
	if (fclose(fp))
		ok = false;
	if (!ok)
		WARNING("SaveThumbnail: cannot write %s", display_utf0(path));
	free(data);
	return ok;
}

/*
 * The script passes system.GetSaveFolderName() + "\\" + name (FUNC 6242) and
 * the original hands that path to its writer (0x468210). Here the save folder
 * is config.save_dir whatever the script calls it (system.GetSaveFolderName
 * says "SaveData"), so that name is dropped, as MsgSkip_Init and the
 * "<save>SaveData\\" CG names do.
 */
static char *thumbnail_path(const char *name)
{
	if (!strncmp(name, "SaveData\\", 9) || !strncmp(name, "SaveData/", 9))
		name += 9;
	return savedir_path(name);
}

/*
 * Original 0x56d140: a width of 0 or less is 1; the height is width * view
 * height / view width in integers, at least 1 (0x56d27d-0x56d28f). A surface
 * of the view's size is filled by 0x5aba30 -> 0x468890, which draws the
 * scene anew with 0x468440, the function ChipmunkSpriteEngine.Update draws
 * each frame with (0x468a91), and copies the result into the surface. The
 * surface is reduced (0x5a7a10) and written (0x468210). The result of the
 * write is not looked at: the function returns true (0x56d3e2); it is false
 * only without the graphics library.
 *
 * Not done: while a transition is open (between TRANS_Begin and TRANS_End,
 * the flag 0x468969 tests) 0x468890 draws the transition at its last rate
 * (0x468760) instead of the plain scene.
 */
bool PE_SaveThumbnail(struct string *filename, int width)
{
	Texture *screen = gfx_main_surface();
	if (screen->w < 1 || screen->h < 1)
		return true;
	if (width < 1)
		width = 1;
	int64_t height = max(1, (int64_t)width * screen->h / screen->w);
	if (width > THUMBNAIL_MAX || height > THUMBNAIL_MAX) {
		WARNING("SaveThumbnail: %dx%lld is not supported", width, (long long)height);
		return true;
	}

	// The picture is of the scene as it is now, not of the frame shown last.
	scene_render();
	uint8_t *pixels = gfx_get_pixels(screen);
	uint8_t *thumbnail = thumbnail_reduce(pixels, screen->w, screen->h, width, height);
	free(pixels);

	char *path = thumbnail_path(filename->text);
	thumbnail_write_qnt(path, thumbnail, width, height);
	free(path);
	free(thumbnail);
	return true;
}
