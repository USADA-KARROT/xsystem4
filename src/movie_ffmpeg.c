/* Copyright (C) 2023 kichikuou <KichikuouChrome@gmail.com>
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

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "system4.h"

#include "apeg.h"
#include "gfx/gfx.h"
#include "movie.h"
#include "mixer.h"
#include "sprite.h"
#include "sts_mixer.h"
#include "xsystem4.h"

#define PRELOAD_PACKETS 10

struct decoder {
	AVStream *stream;
	AVCodecContext *ctx;
	AVFrame *frame;
	AVFifo *queue;
	bool finished;
};

struct movie_context {
	AVFormatContext *format_ctx;
	bool format_eof;
	SDL_mutex *format_mutex;  // also protects video.queue and audio.queue

	struct decoder video;
	struct decoder audio;

	struct texture temp_surface;

	bool has_pending_video_frame;
	bool black_frame_drawn;  // for audio-only mode: true after first black frame
	int apeg_width, apeg_height;  // for audio-only APEG: original video dimensions
	// APEG pictures, decoded on the drawing thread (src/apeg.c); NULL when the
	// file's pictures cannot be decoded (then only its audio plays).
	struct apeg *apeg;
	uint8_t *apeg_rgba;
	int apeg_shown;      // picture in the last texture drawn, -1 for none
	unsigned apeg_shown_handle;  // handle of that texture
	int apeg_time;       // the latest time a picture was picked for; the
	                     // pictures never go back (the original drops the
	                     // ones past due, 0x44a5d0) while the clock may
	bool apeg_size_warned;
	unsigned apeg_errors;    // pictures that could not be shown
	bool playing;        // movie_play was called
	int start_ms;        // movie_seek
	bool audio_skipping;     // the audio callback drops the audio before
	int64_t audio_start_pts; // this (audio time base) first
	struct SwsContext *sws_ctx;
	AVFrame *sws_frame;
	void *sws_buf;

	sts_mixer_stream_t sts_stream;
	int bytes_per_sample;
	int voice;  // the mixer's stream handle
	int volume;
	int sound_group;  // movie_set_sound_group; -1: the master mixer

	// Time keeping data. Written by audio handler and referenced by video
	// handler (i.e. we sync the video to the audio).
	double stream_time;  // in seconds
	uint32_t wall_time_ms;
	SDL_mutex *timer_mutex;
};

static void free_decoder(struct decoder *dec)
{
	if (dec->ctx)
		avcodec_free_context(&dec->ctx);
	if (dec->frame)
		av_frame_free(&dec->frame);
	if (dec->queue) {
		while (av_fifo_can_read(dec->queue) > 0) {
			AVPacket *packet;
			av_fifo_read(dec->queue, &packet, 1);
			av_packet_free(&packet);
		}
		av_fifo_freep2(&dec->queue);
	}
}

static bool init_decoder(struct decoder *dec, AVStream *stream)
{
	if (!stream)
		return false;
	dec->stream = stream;
	const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!codec)
		return false;
	dec->ctx = avcodec_alloc_context3(codec);
	if (avcodec_parameters_to_context(dec->ctx, stream->codecpar) < 0)
		return false;
	if (avcodec_open2(dec->ctx, codec, NULL) != 0)
		return false;
	dec->frame = av_frame_alloc();
	dec->queue = av_fifo_alloc2(0, sizeof(AVPacket*), AV_FIFO_FLAG_AUTO_GROW);
	return true;
}

static void read_packet(struct movie_context *mc)
{
	if (mc->format_eof)
		return;

	AVPacket *packet = av_packet_alloc();

	while (av_read_frame(mc->format_ctx, packet) == 0) {
		if (mc->video.stream && packet->stream_index == mc->video.stream->index) {
			av_fifo_write(mc->video.queue, &packet, 1);
			return;
		} else if (mc->audio.stream && packet->stream_index == mc->audio.stream->index) {
			av_fifo_write(mc->audio.queue, &packet, 1);
			return;
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	packet = NULL;
	mc->format_eof = true;
	if (mc->video.queue)
		av_fifo_write(mc->video.queue, &packet, 1);
	av_fifo_write(mc->audio.queue, &packet, 1);
}

static void preload_packets(struct movie_context *mc)
{
	SDL_LockMutex(mc->format_mutex);
	// Audio-only movies have no video decoder or packet queue.
	while (!mc->format_eof && ((mc->video.queue ? av_fifo_can_read(mc->video.queue) : 0)
				  + av_fifo_can_read(mc->audio.queue) < PRELOAD_PACKETS))
		read_packet(mc);
	SDL_UnlockMutex(mc->format_mutex);
}

static bool decode_frame(struct decoder *dec, struct movie_context *mc)
{
	int ret;
	while ((ret = avcodec_receive_frame(dec->ctx, dec->frame)) == AVERROR(EAGAIN)) {
		SDL_LockMutex(mc->format_mutex);
		AVPacket *packet;
		while (av_fifo_can_read(dec->queue) == 0)
			read_packet(mc);
		av_fifo_read(dec->queue, &packet, 1);
		SDL_UnlockMutex(mc->format_mutex);

		if ((ret = avcodec_send_packet(dec->ctx, packet)) != 0) {
			WARNING("avcodec_send_packet failed: %d", ret);
			return false;
		}
		av_packet_free(&packet);
	}
	if (ret == AVERROR_EOF)
		dec->finished = true;
	else if (ret)
		WARNING("avcodec_receive_frame failed: %d", ret);
	return ret == 0;
}

// After movie_seek: decodes past the frames that end before the start
// position; *trim is how many samples of the frame then decoded are before it.
// False at the end of the stream.
static bool skip_to_start(struct movie_context *mc, int *trim)
{
	for (;;) {
		AVFrame *f = mc->audio.frame;
		int64_t pts = f->best_effort_timestamp;
		if (pts == AV_NOPTS_VALUE || pts + f->nb_samples > mc->audio_start_pts) {
			if (pts != AV_NOPTS_VALUE && pts < mc->audio_start_pts)
				*trim = (int)(mc->audio_start_pts - pts);
			mc->audio_skipping = false;
			return true;
		}
		if (!decode_frame(&mc->audio, mc))
			return false;
	}
}

static int audio_callback(sts_mixer_sample_t *sample, void *data)
{
	struct movie_context *mc = data;
	assert(sample == &mc->sts_stream.sample);

	int trim = 0;
	if (!decode_frame(&mc->audio, mc) || (mc->audio_skipping && !skip_to_start(mc, &trim))) {
		free(sample->data);
		sample->length = 0;
		sample->data = NULL;
		mc->voice = -1;
		return STS_STREAM_COMPLETE;
	}
	const int nr_channels = 2;
	unsigned samples = mc->audio.frame->linesize[0] / mc->bytes_per_sample;
	if (trim)
		samples = mc->audio.frame->nb_samples - trim;
	if (samples * nr_channels != sample->length) {
		free(sample->data);
		sample->length = samples * nr_channels;
		sample->data = xmalloc(sample->length * mc->bytes_per_sample);
	}
	// Interleave.
	uint8_t *l = mc->audio.frame->data[0] + trim * mc->bytes_per_sample;
	uint8_t *r = mc->audio.frame->data[mc->audio.ctx->ch_layout.nb_channels > 1 ? 1 : 0]
		+ trim * mc->bytes_per_sample;
	uint8_t *out = sample->data;
	for (unsigned i = 0; i < samples; i++) {
		memcpy(out, l, mc->bytes_per_sample);
		out += mc->bytes_per_sample;
		l += mc->bytes_per_sample;
		memcpy(out, r, mc->bytes_per_sample);
		out += mc->bytes_per_sample;
		r += mc->bytes_per_sample;
	}

	// Update the timestamp.
	SDL_LockMutex(mc->timer_mutex);
	mc->stream_time += (double)samples / mc->audio.ctx->sample_rate;
	mc->wall_time_ms = SDL_GetTicks();
	SDL_UnlockMutex(mc->timer_mutex);
	return STS_STREAM_CONTINUE;
}

// Extract the Ogg audio stream from an APEG file to a temp path.
// Returns a malloc'd temp path on success (caller must free + unlink), or NULL.
// If out_w/out_h are non-NULL, filled with APEG video dimensions.
static char *apeg_extract_ogg(const char *apeg_path, int *out_w, int *out_h)
{
	FILE *fp = fopen(apeg_path, "rb");
	if (!fp)
		return NULL;

	uint8_t hdr[16];
	if (fread(hdr, 1, 16, fp) != 16 || memcmp(hdr, "APEG", 4) != 0) {
		fclose(fp);
		return NULL;
	}
	uint32_t data_off = hdr[4] | (hdr[5]<<8) | (hdr[6]<<16) | (hdr[7]<<24);
	if (out_w) *out_w = hdr[12] | (hdr[13]<<8);
	if (out_h) *out_h = hdr[14] | (hdr[15]<<8);

	// Chunks start 8 bytes after data_off (skip mystery bytes).
	if (fseek(fp, (long)(data_off + 8), SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}

	// Scan chunks: tag(4) + body_size(4) + body[body_size]
	uint8_t chunk_hdr[8];
	char *tmp_path = NULL;
	while (fread(chunk_hdr, 1, 8, fp) == 8) {
		uint32_t body_size = chunk_hdr[4] | (chunk_hdr[5]<<8) | (chunk_hdr[6]<<16) | (chunk_hdr[7]<<24);
		if (memcmp(chunk_hdr, "SOND", 4) == 0 && body_size > 0) {
			// Body is raw Ogg Vorbis data.
			tmp_path = xmalloc(64);
			snprintf(tmp_path, 64, "/tmp/xsys4_apeg_audio_%d.ogg", (int)getpid());
			FILE *out = fopen(tmp_path, "wb");
			if (!out) {
				free(tmp_path);
				tmp_path = NULL;
				break;
			}
			uint8_t buf[65536];
			uint32_t remaining = body_size;
			while (remaining > 0) {
				uint32_t to_read = remaining < sizeof(buf) ? remaining : sizeof(buf);
				size_t got = fread(buf, 1, to_read, fp);
				if (got == 0) break;
				fwrite(buf, 1, got, out);
				remaining -= (uint32_t)got;
			}
			fclose(out);
			break;
		}
		if (fseek(fp, (long)body_size, SEEK_CUR) != 0)
			break;
	}

	fclose(fp);
	return tmp_path;
}

struct movie_context *movie_load(const char *filename)
{
	struct movie_context *mc = xcalloc(1, sizeof(struct movie_context));
	mc->voice = -1;
	mc->sound_group = -1;
	mc->apeg_shown = -1;
	char *path = gamedir_path_icase(filename);
	if (!path) {
		WARNING("%s: file does not exist", filename);
		movie_free(mc);
		return NULL;
	}

	// Detect APEG format and extract Ogg audio to a temp file.
	char *apeg_tmp = NULL;
	{
		FILE *fp = fopen(path, "rb");
		if (fp) {
			uint8_t magic[4];
			if (fread(magic, 1, 4, fp) == 4 && memcmp(magic, "APEG", 4) == 0) {
				fclose(fp);
				apeg_tmp = apeg_extract_ogg(path, &mc->apeg_width, &mc->apeg_height);
				if (!apeg_tmp) {
					WARNING("%s: APEG file but failed to extract audio", path);
					free(path);
					movie_free(mc);
					return NULL;
				}
				// The pictures are optional: a file the decoder rejects
				// still plays its audio, as before.
				const char *err = NULL;
				mc->apeg = apeg_open(path, &err);
				if (!mc->apeg) {
					WARNING("%s: APEG pictures not decoded: %s", path, err);
				} else if (apeg_width(mc->apeg) != mc->apeg_width
					   || apeg_height(mc->apeg) != mc->apeg_height) {
					WARNING("%s: APEG pictures not decoded: the decoder reads %dx%d, the header %dx%d",
						path, apeg_width(mc->apeg), apeg_height(mc->apeg),
						mc->apeg_width, mc->apeg_height);
					apeg_close(mc->apeg);
					mc->apeg = NULL;
				} else if (apeg_warning(mc->apeg)) {
					WARNING("%s: %s", path, apeg_warning(mc->apeg));
				}
			} else {
				fclose(fp);
			}
		}
	}

	const char *open_path = apeg_tmp ? apeg_tmp : path;
	int ret;
	if ((ret = avformat_open_input(&mc->format_ctx, open_path, NULL, NULL)) != 0) {
		WARNING("%s: avformat_open_input failed: %d", path, ret);
		if (apeg_tmp) { remove(apeg_tmp); free(apeg_tmp); }
		free(path);
		movie_free(mc);
		return NULL;
	}
	if (apeg_tmp) { remove(apeg_tmp); free(apeg_tmp); }
	free(path);

	if ((ret = avformat_find_stream_info(mc->format_ctx, NULL)) < 0) {
		WARNING("avformat_find_stream_info failed: %d", ret);
		movie_free(mc);
		return NULL;
	}

	AVStream *video_stream = NULL;
	AVStream *audio_stream = NULL;
	for (int i = 0; i < (int)mc->format_ctx->nb_streams; ++i) {
		switch (mc->format_ctx->streams[i]->codecpar->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			if (!video_stream)
				video_stream = mc->format_ctx->streams[i];
			break;
		case AVMEDIA_TYPE_AUDIO:
			if (!audio_stream)
				audio_stream = mc->format_ctx->streams[i];
			break;
		default:
			break;
		}
	}
	// For APEG (audio-only), skip video decoder and mark video as finished.
	if (video_stream) {
		if (!init_decoder(&mc->video, video_stream)) {
			WARNING("Cannot initialize video decoder");
			movie_free(mc);
			return NULL;
		}
		NOTICE("video: %d x %d, %s", mc->video.ctx->width, mc->video.ctx->height, av_get_pix_fmt_name(mc->video.ctx->pix_fmt));
	} else {
		mc->video.finished = true;
	}
	if (!init_decoder(&mc->audio, audio_stream)) {
		WARNING("Cannot initialize audio decoder");
		movie_free(mc);
		return NULL;
	}

	NOTICE("audio: %d hz, %d channels, %s", mc->audio.ctx->sample_rate, mc->audio.ctx->ch_layout.nb_channels, av_get_sample_fmt_name(mc->audio.ctx->sample_fmt));

	mc->format_mutex = SDL_CreateMutex();
	mc->timer_mutex = SDL_CreateMutex();
	mc->volume = 100;

	preload_packets(mc);
	return mc;
}

void movie_free(struct movie_context *mc)
{
	if (mc->voice >= 0)
		mixer_stream_stop(mc->voice);
	mixer_movie_sound_release(mc);
	if (mc->sts_stream.sample.data)
		free(mc->sts_stream.sample.data);

	if (mc->format_ctx)
		avformat_close_input(&mc->format_ctx);
	if (mc->format_mutex)
		SDL_DestroyMutex(mc->format_mutex);
	if (mc->timer_mutex)
		SDL_DestroyMutex(mc->timer_mutex);

	free_decoder(&mc->video);
	free_decoder(&mc->audio);

	if (mc->temp_surface.handle)
		gfx_delete_texture(&mc->temp_surface);
	if (mc->sws_ctx)
		sws_freeContext(mc->sws_ctx);
	if (mc->sws_frame)
		av_frame_free(&mc->sws_frame);
	if (mc->sws_buf)
		av_free(mc->sws_buf);
	apeg_close(mc->apeg);
	free(mc->apeg_rgba);

	free(mc);
}

bool movie_play(struct movie_context *mc)
{
	// Start the audio stream.
	mc->stream_time = mc->start_ms / 1000.0;
	mc->wall_time_ms = SDL_GetTicks();
	mc->apeg_time = INT_MIN;
	mc->sts_stream.userdata = mc;
	mc->sts_stream.callback = audio_callback;
	mc->sts_stream.sample.frequency = mc->audio.ctx->sample_rate;
	switch (mc->audio.ctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16P:
		mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_16;
		mc->bytes_per_sample = 2;
		break;
	case AV_SAMPLE_FMT_S32P:
		mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_32;
		mc->bytes_per_sample = 4;
		break;
	case AV_SAMPLE_FMT_FLTP:
		mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mc->bytes_per_sample = 4;
		break;
	default:
		WARNING("Unsupported audio format %d", mc->audio.ctx->sample_fmt);
		return false;
	}
	mc->playing = true;
	mc->voice = mixer_stream_play_group(&mc->sts_stream, mc->volume, mc->sound_group);
	return true;
}

void movie_set_sound_group(struct movie_context *mc, int sound_id, int sound_group)
{
	mc->sound_group = mixer_movie_sound_set(sound_id, sound_group, mc);
}

// The APEG picture due at the audio clock: the latest one whose timestamp is
// not later than it (the original's 0x44a1b0), converted and uploaded when it
// is not the one tex already shows. The clock is made monotonic here: it can
// read a few ms less than before between two audio callbacks, and the
// original never shows an older picture again. Decoding runs here, on the
// drawing thread; the audio callback only shares timer_mutex for the clock.
static bool apeg_update_texture(struct movie_context *mc, struct texture *tex)
{
	if (tex->w != mc->apeg_width || tex->h != mc->apeg_height) {
		if (!mc->apeg_size_warned)
			WARNING("APEG %dx%d cannot be drawn into a %dx%d texture",
				mc->apeg_width, mc->apeg_height, tex->w, tex->h);
		mc->apeg_size_warned = true;
		return false;
	}
	int t = movie_get_position(mc);
	if (t < mc->apeg_time)
		t = mc->apeg_time;
	mc->apeg_time = t;
	const struct apeg_picture *p = apeg_picture_at(mc->apeg, t);
	if (!p) {
		// Before the first picture, or a picture that cannot be decoded:
		// the texture keeps what it shows. The first failures are all
		// reported, later ones now and then.
		const char *err = apeg_error(mc->apeg);
		if (err) {
			mc->apeg_errors++;
			if (mc->apeg_errors <= 8 || !(mc->apeg_errors & 255))
				WARNING("APEG picture at %d ms: %s (%u failures so far)", t, err, mc->apeg_errors);
		}
		return false;
	}
	if (p->index == mc->apeg_shown && tex->handle == mc->apeg_shown_handle)
		return false;
	if (!mc->apeg_rgba)
		mc->apeg_rgba = xmalloc((size_t)p->width * p->height * 4);
	apeg_picture_to_rgba(mc->apeg, p, mc->apeg_rgba, (size_t)p->width * 4);
	gfx_update_texture_with_pixels(tex, mc->apeg_rgba);
	mc->apeg_shown = p->index;
	mc->apeg_shown_handle = tex->handle;
	return true;
}

bool movie_update_texture(struct movie_context *mc, struct texture *tex)
{
	if (!mc->playing || !tex || !tex->handle)
		return false;
	// Only APEG pictures; other parts movies keep their back colour.
	return mc->apeg ? apeg_update_texture(mc, tex) : false;
}

// Audio-only mode (e.g. an APEG whose pictures are not decoded): one black
// frame, then the audio alone.
static bool draw_black_once(struct movie_context *mc, struct sact_sprite *sprite)
{
	if (!mc->black_frame_drawn) {
		mc->black_frame_drawn = true;
		struct texture *tex = sprite ? sprite_get_texture(sprite) : NULL;
		if (tex && tex->w > 0 && tex->h > 0) {
			int npixels = tex->w * tex->h;
			uint32_t *black = xcalloc(npixels, sizeof(uint32_t));
			gfx_update_texture_with_pixels(tex, black);
			free(black);
			if (sprite)
				sprite_dirty(sprite);
		}
	}
	return !mc->audio.finished;
}

bool movie_draw(struct movie_context *mc, struct sact_sprite *sprite)
{
	// APEG: the audio clock picks the picture.
	if (mc->video.finished && !mc->video.ctx && mc->apeg) {
		if (sprite) {
			struct texture *tex = sprite_get_texture(sprite);
			if (tex->w != mc->apeg_width || tex->h != mc->apeg_height) {
				// Not the movie's size: as before the pictures were
				// decoded, one black frame.
				if (!mc->apeg_size_warned)
					WARNING("APEG %dx%d cannot be drawn into a %dx%d sprite; drawing it black",
						mc->apeg_width, mc->apeg_height, tex->w, tex->h);
				mc->apeg_size_warned = true;
				return draw_black_once(mc, sprite);
			}
			if (movie_update_texture(mc, tex))
				sprite_dirty(sprite);
			else if (mc->apeg_shown < 0)
				draw_black_once(mc, sprite);  // before play or the first picture
		} else {
			// Draw directly to the main framebuffer, at the movie's size.
			if (!mc->temp_surface.handle)
				gfx_init_texture_blank(&mc->temp_surface, mc->apeg_width, mc->apeg_height);
			if (movie_update_texture(mc, &mc->temp_surface)) {
				gfx_clear();
				gfx_render_texture(&mc->temp_surface, NULL);
				gfx_swap();
			}
		}
		return !mc->audio.finished;
	}
	if (mc->video.finished && !mc->video.ctx)
		return draw_black_once(mc, sprite);

	struct texture *texture;
	if (sprite) {
		texture = sprite_get_texture(sprite);
	} else {
		if (!mc->temp_surface.handle)
			gfx_init_texture_blank(&mc->temp_surface, config.view_width, config.view_height);
		texture = &mc->temp_surface;
	}

	if (!mc->sws_ctx) {
		mc->sws_ctx = sws_getContext(
			mc->video.ctx->width, mc->video.ctx->height, mc->video.ctx->pix_fmt,
			texture->w, texture->h, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);
		mc->sws_frame = av_frame_alloc();
		int size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, texture->w, texture->h, 1);
		mc->sws_buf = av_malloc(size);
		av_image_fill_arrays(
			mc->sws_frame->data, mc->sws_frame->linesize,
			mc->sws_buf, AV_PIX_FMT_RGBA, texture->w, texture->h, 1);
	}

	// Decode a frame, unless we already have one.
	if (!mc->has_pending_video_frame && !decode_frame(&mc->video, mc))
		return mc->video.finished;

	// If the frame's timestamp is in the future, save the frame and return.
	double pts = av_q2d(mc->video.stream->time_base) * mc->video.frame->best_effort_timestamp;
	SDL_LockMutex(mc->timer_mutex);
	double now = mc->stream_time + (SDL_GetTicks() - mc->wall_time_ms) / 1000.0;
	SDL_UnlockMutex(mc->timer_mutex);
	if (pts > now) {
		mc->has_pending_video_frame = true;
		// We have spare time, let's buffer some packets.
		preload_packets(mc);
		return true;
	}

	// Convert to RGB and update the texture.
	sws_scale(mc->sws_ctx, (const uint8_t **)mc->video.frame->data, mc->video.frame->linesize,
		 0, mc->video.ctx->height, mc->sws_frame->data, mc->sws_frame->linesize);
	gfx_update_texture_with_pixels(texture, mc->sws_buf);
	mc->has_pending_video_frame = false;

	if (sprite) {
		sprite_dirty(sprite);
	} else {
		// Draw directly to the main framebuffer.
		gfx_clear();
		gfx_render_texture(&mc->temp_surface, NULL);
		gfx_swap();
	}

	return true;
}

bool movie_is_end(struct movie_context *mc)
{
	return mc->video.finished && mc->audio.finished;
}

int movie_get_position(struct movie_context *mc)
{
	SDL_LockMutex(mc->timer_mutex);
	int ms = mc->wall_time_ms ? mc->stream_time * 1000 + SDL_GetTicks() - mc->wall_time_ms : 0;
	SDL_UnlockMutex(mc->timer_mutex);
	// An APEG's time is its total length once its sound stops (0x44a4c0).
	if (mc->apeg && mc->playing && mc->audio.finished)
		ms = apeg_duration(mc->apeg);
	return ms;
}

int movie_get_duration(struct movie_context *mc)
{
	if (mc->apeg)
		return apeg_duration(mc->apeg);
	if (mc->apeg_width || !mc->format_ctx || mc->format_ctx->duration <= 0)
		return 0;
	return (int)(mc->format_ctx->duration / (AV_TIME_BASE / 1000));
}

bool movie_seek(struct movie_context *mc, int msec)
{
	if (msec <= 0 || mc->playing)
		return msec == 0 && !mc->playing;
	// Only the audio of an APEG (no video stream) can start elsewhere.
	if (mc->video.ctx || !mc->audio.stream)
		return false;
	SDL_LockMutex(mc->format_mutex);
	while (av_fifo_can_read(mc->audio.queue) > 0) {
		AVPacket *packet;
		av_fifo_read(mc->audio.queue, &packet, 1);
		av_packet_free(&packet);
	}
	int64_t ts = av_rescale_q(msec, (AVRational){ 1, 1000 }, mc->audio.stream->time_base);
	int ret = av_seek_frame(mc->format_ctx, mc->audio.stream->index, ts, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		WARNING("movie_seek(%d) failed: %d; playing from the start", msec, ret);
		av_seek_frame(mc->format_ctx, mc->audio.stream->index, 0, AVSEEK_FLAG_BACKWARD);
	}
	avcodec_flush_buffers(mc->audio.ctx);
	mc->format_eof = false;
	mc->audio.finished = false;
	SDL_UnlockMutex(mc->format_mutex);
	// The seek lands on a page at or before msec; the audio callback drops
	// what comes before it, so the sound starts where the clock does.
	mc->start_ms = ret < 0 ? 0 : msec;
	mc->apeg_time = INT_MIN;
	mc->audio_skipping = ret >= 0;
	mc->audio_start_pts = ts;
	preload_packets(mc);
	return ret >= 0;
}

bool movie_set_volume(struct movie_context *mc, int volume)
{
	mc->volume = volume;
	if (mc->voice >= 0)
		mixer_stream_set_volume(mc->voice, volume);
	return true;
}

void movie_get_video_size(struct movie_context *mc, int *out_w, int *out_h)
{
	if (mc->video.ctx) {
		if (out_w) *out_w = mc->video.ctx->width;
		if (out_h) *out_h = mc->video.ctx->height;
	} else {
		if (out_w) *out_w = mc->apeg_width;
		if (out_h) *out_h = mc->apeg_height;
	}
}
