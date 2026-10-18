/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSTEM4_APEG_H
#define SYSTEM4_APEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * APEG, AliceSoft's movie format (System4 v14; Dohna Dohna's Movie/Op.apeg).
 *
 * The file is a header (versions 3 and 5), an index of the I pictures, and a
 * list of chunks: TMNL (thumbnail), SOND (an Ogg Vorbis stream, played by the
 * caller) and IPIC/PPIC/BPIC pictures in decode order. The pictures are an
 * MPEG-1 like 4:2:0 bitstream with the engine's own syntax and code tables.
 * src/apeg.c decodes them with the same integer arithmetic as the original
 * player (its functions 0x4484f0 .. 0x45e170, listed in src/apeg.c); where the
 * original's behaviour is undefined (corrupt data, missing references), it
 * fails safely instead. Pictures are kept as unclamped int16 Y/Cb/Cr planes,
 * like the original's reference frames; apeg_picture_to_rgba() does the
 * conversion the original leaves to its pixel shader.
 *
 * Nothing here depends on the rest of the engine: errors are reported as
 * static strings, never as engine warnings or aborts.
 */

struct apeg;

struct apeg_picture {
	int index;            // decode order position (0 .. apeg_nr_pictures()-1)
	uint32_t ts;          // presentation time in ms
	char type;            // 'I', 'P' or 'B'
	int width, height;    // displayed size
	int luma_stride;      // (width + 15) & ~15; also the number of samples per row
	int luma_rows;        // (height + 15) & ~15
	int chroma_stride;    // luma_stride / 2
	int chroma_rows;      // luma_rows / 2
	const int16_t *y, *cb, *cr;
};

// Opens a file or a copy of a memory buffer. On failure returns NULL and, if
// error is not NULL, stores a static description in *error. A bad I picture
// index or a few stray bytes after the last chunk do not stop it: the index
// is then empty and apeg_warning() says what was wrong.
struct apeg *apeg_open(const char *path, const char **error);
struct apeg *apeg_open_memory(const void *data, size_t size, const char **error);
void apeg_close(struct apeg *a);
// What apeg_open overlooked, or NULL.
const char *apeg_warning(const struct apeg *a);

int apeg_version(const struct apeg *a);
int apeg_width(const struct apeg *a);
int apeg_height(const struct apeg *a);
// The total length in the header (ms); what GetPartsMovieEndTime returns.
int apeg_duration(const struct apeg *a);
int apeg_nr_index(const struct apeg *a);
bool apeg_index_entry(const struct apeg *a, int i, uint32_t *ms, uint32_t *offset);
// The first SOND chunk's payload (an Ogg stream).
bool apeg_sound(const struct apeg *a, uint64_t *offset, uint32_t *size);

int apeg_nr_pictures(const struct apeg *a);
// Decode order: type and timestamp of picture i.
bool apeg_picture_info(const struct apeg *a, int i, char *type, uint32_t *ts);

// Decodes the next picture in decode order (starting with picture 0 after
// open or apeg_rewind). Returns 1 with *out set, 0 at the end, -1 when the
// picture could not be decoded (apeg_error() says why; the next call goes on
// with the following picture).
int apeg_decode_next(struct apeg *a, const struct apeg_picture **out);

// The picture to show at time_ms: the one with the latest timestamp <= time_ms
// (0x44a1b0). Decodes whatever it needs: it goes on from the last picture
// decoded, or starts again at the I picture that gives the picture its
// references (for the B pictures of an open GOP, the previous GOP's I). A B
// picture between the same references as the last one decoded is decoded
// alone. Returns NULL when no picture is due yet, or when the picture cannot
// be decoded (then apeg_error() is not NULL). Afterwards a picture whose own
// data is bad, or that needs such a picture or a reference missing from the
// file, fails at once; after a read error the next call tries again. The
// result stays valid until the next call on this decoder.
const struct apeg_picture *apeg_picture_at(struct apeg *a, int64_t time_ms);

// Forgets the reference pictures and the pictures that failed; decoding
// starts again at picture 0.
void apeg_rewind(struct apeg *a);

// Since open: pictures decoded, and how often apeg_picture_at started again at
// an I picture. For diagnostics and tests.
void apeg_stats(const struct apeg *a, int *decoded, int *restarts);

// The last failure, or NULL.
const char *apeg_error(const struct apeg *a);

// Converts a picture to width x height RGBA (alpha 255) with the original
// shader's arithmetic: Y clamped to 16..235 and Cb/Cr to 16..240 (0x44c2c0),
// chroma upsampled bilinearly at x/2 - 0.25 with clamped edges, and the
// shader's matrix. pitch is in bytes.
void apeg_picture_to_rgba(struct apeg *a, const struct apeg_picture *p, uint8_t *rgba, size_t pitch);

// The same matrix for one unclamped Y/Cb/Cr colour (each 0..255), as the
// shader draws a surface filled with it (CreatePartsMovie's back colour).
void apeg_ycbcr_to_rgb(int y, int cb, int cr, uint8_t rgb[3]);

#endif /* SYSTEM4_APEG_H */
