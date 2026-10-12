/*
 * save-thumbnail (2026-10-12): PartsEngine.SaveThumbnail writes the picture
 * of a save the way the original does, and the save list reads it back.
 *
 * Original (static disassembly of the Dohna Dohna executable only):
 * PartsEngine.SaveThumbnail(SaveFileName, ThumbnailWidth) is slot 0x57bccc ->
 * 0x56d140. A width of 0 or less is 1; the height is width * view height /
 * view width in integers, at least 1 (0x56d27d-0x56d28f); a surface of the
 * view's size is filled through 0x5aba30 -> 0x468890 -> 0x468440 (the
 * function that draws each frame: the scene is drawn anew), reduced into
 * width x height (0x5a7a10 -> 0x492d30: the average over the area each pixel
 * covers, truncated) and handed to the writer (0x468210).
 * The result of the write is not looked at: the function returns true
 * (0x56d3e2).
 *
 * Measured on the files the original leaves (124 x 69 from 1280 x 720): QNT
 * version 2 with a header of 68 bytes, 24 bpp, no alpha plane, the pixel
 * planes deflated at level 9; the reduction is the average over the area
 * each pixel covers (the fit to a screenshot of the same screen: mean square
 * error 2 against 5 for whole-pixel windows and 70 and more for bilinear).
 * Truncating the average is the better fit but is not proven.
 *
 * The script (FUNC 6242 gamesave::detail::SaveThumbnail) passes
 * system.GetSaveFolderName() + "\\" + "Thumb%04d.qnt", copies the picture to
 * the save's number with system.CopySaveFile (FUNC 6245), and the list sets
 * the CG "<save>Thumb%04d.qnt" (FUNC 30451 ThumbnailCapture::GetFileName).
 *
 * The scenes are made here; the expected pixels come from st_model, which
 * counts the cells of a common grid and shares nothing with the engine's
 * function. Every case runs in a child with the construction fixture's GL.
 */

#include <dirent.h>

extern void probe_main_surface_init(int w, int h);

static int st_failures;
#define ST_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); st_failures++; } } while (0)

static char st_dir[PATH_MAX];
static uint8_t *st_scene_px;
static Texture st_scene_tex;
static struct sprite st_scene_sp;

static void st_save_dir(void)
{
	const char *base = getenv("TMPDIR");
	if (!base || !base[0])
		base = "/tmp";
	snprintf(st_dir, sizeof st_dir, "%s/xsys4-save-thumbnail-XXXXXX", base);
	if (!mkdtemp(st_dir) || sp_under(st_dir, "XS4_SRC") || sp_under(st_dir, "XS4_MASTER_GAME")) {
		printf("SAVE-THUMBNAIL cannot use a temporary folder under %s\n", base);
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	config.save_dir = strdup(st_dir);
}

/* Removes the files of the save folder and the folder; anything else in it
 * (a folder the engine should not have made) stays and is reported. */
static void st_remove_dir(void)
{
	DIR *d = opendir(st_dir);
	struct dirent *e;
	while (d && (e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		char *p = mj_join(st_dir, e->d_name);
		if (remove(p))
			printf("  (could not remove %s)\n", e->d_name);
		free(p);
	}
	if (d)
		closedir(d);
	if (rmdir(st_dir))
		printf("  (could not remove the save folder)\n");
}

static void st_pattern(uint8_t *px, int w, int h, unsigned seed)
{
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			unsigned v = (x + seed) * 2654435761u ^ (y + 3 * seed) * 40503u;
			int r = x * 255 / w, g = y * 255 / h, b = ((x / 37 + y / 23 + (int)seed) & 1) ? 230 : 20;
			if (x % 7 == 0)
				r = 255 - r;
			if (y % 5 == 0)
				g ^= 0xff;
			b ^= (v >> 13) & 0x3f;
			uint8_t *p = px + (y * w + x) * 4;
			p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
		}
	}
}

/* Drawn with the stretching copy at 1:1: in the windowless context gfx_copy
 * and the default shader's gfx_render_texture draw nothing (the driver logs
 * "buildPipelineState failed" once), whatever the source and the target. */
static void st_scene_render(possibly_unused struct sprite *sp)
{
	int w = st_scene_tex.w, h = st_scene_tex.h;
	gfx_copy_stretch_with_alpha_map(gfx_main_surface(), 0, 0, w, h, &st_scene_tex, 0, 0, w, h);
}

/* The scene is one sprite, a picture of the view's size; what the frame
 * before left on the main surface is plain red, so a thumbnail of the surface
 * as it stands is not the scene. */
static void st_scene(int w, int h, unsigned seed)
{
	config.view_width = w;
	config.view_height = h;
	probe_main_surface_init(w, h);
	free(st_scene_px);
	st_scene_px = xmalloc(w * h * 4);
	st_pattern(st_scene_px, w, h, seed);
	gfx_delete_texture(&st_scene_tex);
	gfx_init_texture_with_pixels(&st_scene_tex, w, h, st_scene_px);
	if (!st_scene_sp.in_scene) {
		st_scene_sp = (struct sprite){ .z = 1, .has_pixel = true, .render = st_scene_render };
		scene_register_sprite(&st_scene_sp);
	}
	gfx_fill(gfx_main_surface(), 0, 0, w, h, 255, 0, 0);
}

/* The weight of source pixel j in destination pixel d: the cells of a grid
 * with n_dst cells per source pixel and n_src per destination pixel. */
static int *st_weights(int n_dst, int n_src)
{
	int *w = xcalloc(n_dst * n_src, sizeof(int));
	for (int d = 0; d < n_dst; d++)
		for (int i = d * n_src; i < (d + 1) * n_src; i++)
			w[d * n_src + i / n_dst]++;
	return w;
}

static uint8_t *st_model(const uint8_t *src, int sw, int sh, int dw, int dh)
{
	int *wx = st_weights(dw, sw), *wy = st_weights(dh, sh);
	uint8_t *dst = xmalloc(dw * dh * 3);
	for (int y = 0; y < dh; y++) {
		for (int x = 0; x < dw; x++) {
			uint64_t acc[3] = { 0 };
			for (int j = y * sh / dh; j < sh && j <= ((y + 1) * sh - 1) / dh; j++) {
				for (int k = x * sw / dw; k < sw && k <= ((x + 1) * sw - 1) / dw; k++) {
					uint64_t wt = (uint64_t)wy[y * sh + j] * wx[x * sw + k];
					for (int c = 0; c < 3; c++)
						acc[c] += wt * src[(j * sw + k) * 4 + c];
				}
			}
			for (int c = 0; c < 3; c++)
				dst[(y * dw + x) * 3 + c] = acc[c] / ((uint64_t)sw * sh);
		}
	}
	free(wx);
	free(wy);
	return dst;
}

static uint8_t *st_read(const char *name, size_t *size)
{
	char *p = mj_join(st_dir, name);
	FILE *f = fopen(p, "rb");
	free(p);
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = xmalloc(*size + 1);
	if (fread(buf, 1, *size, f) != *size)
		*size = 0;
	fclose(f);
	return buf;
}

static bool st_exists(const char *name)
{
	char *p = mj_join(st_dir, name);
	bool r = access(p, F_OK) == 0;
	free(p);
	return r;
}

static uint32_t st_dw(const uint8_t *b, int off)
{
	return b[off] | b[off + 1] << 8 | b[off + 2] << 16 | (uint32_t)b[off + 3] << 24;
}

/* The file has the original's shape and holds the area average of src. */
static void st_expect(const char *name, const uint8_t *src, int sw, int sh, int dw, int dh, const char *what)
{
	size_t size = 0;
	uint8_t *b = st_read(name, &size);
	ST_CHECK(b, "%s: %s was not written", what, name);
	if (!b)
		return;
	bool shape = size > 70 && !memcmp(b, "QNT", 4) && st_dw(b, 4) == 2 && st_dw(b, 8) == 68
		&& st_dw(b, 12) == 0 && st_dw(b, 16) == 0 && st_dw(b, 28) == 24 && st_dw(b, 32) == 1
		&& st_dw(b, 40) == 0 && size == 68 + (size_t)st_dw(b, 36) && b[68] == 0x78 && b[69] == 0xda;
	for (int i = 44; shape && i < 68; i++)
		shape = !b[i];
	ST_CHECK(shape, "%s: %s is not a version 2 QNT with a 68 byte header, 24 bpp, no alpha plane and level 9 "
		 "pixels (%zu bytes, header %u, bpp %u, alpha %u)", what, name, size, size > 44 ? st_dw(b, 8) : 0,
		 size > 44 ? st_dw(b, 28) : 0, size > 44 ? st_dw(b, 40) : 0);
	bool dims = size > 44 && (int)st_dw(b, 20) == dw && (int)st_dw(b, 24) == dh;
	ST_CHECK(dims, "%s: %s is %ux%u, want %dx%d", what, name, size > 44 ? st_dw(b, 20) : 0,
		 size > 44 ? st_dw(b, 24) : 0, dw, dh);
	free(b);
	if (!shape || !dims)
		return;
	char *p = mj_join(st_dir, name);
	struct cg *cg = cg_load_file(p);
	free(p);
	ST_CHECK(cg && cg->metrics.w == dw && cg->metrics.h == dh && !cg->metrics.has_alpha,
		 "%s: %s does not load as %dx%d without alpha", what, name, dw, dh);
	if (!cg)
		return;
	uint8_t *want = st_model(src, sw, sh, dw, dh);
	const uint8_t *got = cg->pixels;
	int bad = 0, fx = -1, fy = -1;
	for (int i = 0; i < dw * dh; i++) {
		if (memcmp(got + i * 4, want + i * 3, 3) && !bad++) {
			fx = i % dw;
			fy = i / dw;
		}
	}
	if (bad) {
		const uint8_t *g = got + (fy * dw + fx) * 4, *w = want + (fy * dw + fx) * 3;
		ST_CHECK(0, "%s: %d of %d pixels are not the area average; first (%d,%d) = (%d,%d,%d), want (%d,%d,%d)",
			 what, bad, dw * dh, fx, fy, g[0], g[1], g[2], w[0], w[1], w[2]);
	}
	free(want);
	cg_free(cg);
}

/* PartsEngine.SaveThumbnail through the game's declaration. */
static int st_save(const char *name, int width)
{
	union vm_value a[2] = { { .i = cf_str_slot(name) }, { .i = width } };
	return bu_call("PartsEngine", "SaveThumbnail", 2, 0, 2, a).i;
}

static int st_fn(const char *name)
{
	int f = ain_get_function(ain, name);
	if (f < 0) {
		printf("SAVE-THUMBNAIL %s is not in this AIN\n", name);
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	return f;
}

static void st_begin(int w, int h, unsigned seed)
{
	st_save_dir();
	cn_gl_setup();
	st_scene(w, h, seed);
}

/* ST1: the call the game makes. */
static void st_game_call(void)
{
	st_begin(1280, 720, 1);
	uint8_t *shown = gfx_get_pixels(gfx_main_surface());
	ST_CHECK(shown[0] == 255 && !shown[1] && !shown[2], "the main surface is not the red of the frame before");
	free(shown);
	int r = st_save("SaveData\\Thumb0000.qnt", 124);
	ST_CHECK(r == 1, "SaveThumbnail returned %d, want true", r);
	st_expect("Thumb0000.qnt", st_scene_px, 1280, 720, 124, 69, "ST1");
	ST_CHECK(!st_exists("SaveData"), "a SaveData folder was made inside the save folder");
	/* The scene was drawn for the picture. */
	shown = gfx_get_pixels(gfx_main_surface());
	ST_CHECK(!memcmp(shown, st_scene_px, 1280 * 4) && !memcmp(shown + 719 * 1280 * 4, st_scene_px + 719 * 1280 * 4, 1280 * 4),
		 "the main surface does not hold the scene after the call");
	free(shown);
	/* A second picture replaces the first. */
	st_scene(1280, 720, 9);
	ST_CHECK(st_save("SaveData\\Thumb0000.qnt", 124) == 1, "the second SaveThumbnail did not return true");
	st_expect("Thumb0000.qnt", st_scene_px, 1280, 720, 124, 69, "ST1 again");
	st_remove_dir();
}

/* ST2: width and height. */
static void st_sizes(void)
{
	static const struct { int vw, vh, width, dw, dh; } t[] = {
		{ 1280, 720, 300, 300, 168 }, { 1280, 720, 1, 1, 1 }, { 1280, 720, 0, 1, 1 }, { 1280, 720, -5, 1, 1 },
		{ 1280, 720, 2, 2, 1 }, { 1280, 720, 4, 4, 2 }, { 1280, 720, 1280, 1280, 720 }, { 800, 600, 124, 124, 93 },
		{ 1000, 300, 3, 3, 1 }, { 333, 217, 50, 50, 32 },
	};
	st_save_dir();
	cn_gl_setup();
	for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
		char what[64], name[64];
		snprintf(what, sizeof what, "ST2 %dx%d width %d", t[i].vw, t[i].vh, t[i].width);
		snprintf(name, sizeof name, "SaveData\\Size%u.qnt", i);
		st_scene(t[i].vw, t[i].vh, 20 + i);
		ST_CHECK(st_save(name, t[i].width) == 1, "%s: not true", what);
		st_expect(name + 9, st_scene_px, t[i].vw, t[i].vh, t[i].dw, t[i].dh, what);
	}
	st_remove_dir();
}

/* ST3: the name. The save folder is the engine's whatever the script calls
 * it; a file that cannot be written is still true and nothing else. */
static void st_names(void)
{
	st_begin(320, 180, 3);
	ST_CHECK(st_save("SaveData/Slash.qnt", 31) == 1, "a forward slash: not true");
	st_expect("Slash.qnt", st_scene_px, 320, 180, 31, 17, "ST3 forward slash");
	ST_CHECK(st_save("Bare.qnt", 31) == 1, "no folder: not true");
	st_expect("Bare.qnt", st_scene_px, 320, 180, 31, 17, "ST3 no folder");
	ST_CHECK(st_save("SaveDataX\\Other.qnt", 31) == 1, "a folder that does not exist: not true");
	ST_CHECK(!st_exists("Other.qnt") && !st_exists("SaveDataX"), "SaveDataX\\ was taken for the save folder");
	ST_CHECK(!st_exists("SaveData"), "a SaveData folder was made inside the save folder");
	/* A size this engine does not make (its own limit). */
	ST_CHECK(st_save("SaveData\\Huge.qnt", 1 << 30) == 1, "a width of 2^30: not true");
	ST_CHECK(!st_exists("Huge.qnt"), "a width of 2^30 wrote a file");
	st_remove_dir();
	/* The folder is gone: nothing to write to. */
	ST_CHECK(st_save("SaveData\\Gone.qnt", 31) == 1, "without a save folder: not true");
	ST_CHECK(access(st_dir, F_OK), "the save folder was made again");
}

/* ST4: the list's side, by the name the script builds. */
static void st_list(void)
{
	st_begin(1280, 720, 4);
	ST_CHECK(st_save("SaveData\\Thumb1000.qnt", 124) == 1, "SaveThumbnail: not true");
	stack_push((union vm_value){ .i = 1000 });
	vm_call(st_fn("ThumbnailCapture::GetFileName"), -1);
	int name = stack_pop().i;
	ST_CHECK(!strcmp(heap_get_string(name)->text, "<save>Thumb1000.qnt"), "ThumbnailCapture::GetFileName(1000) is \"%s\"",
		 heap_get_string(name)->text);
	enum { NO = 9100 };
	union vm_value a[4] = { { .i = NO }, { .i = name }, { .i = 0 }, { .i = 1 } };
	ST_CHECK(bu_call("PartsEngine", "Parts_SetPartsCG", 4, 0, 4, a).i == 1, "Parts_SetPartsCG with the picture: not true");
	ST_CHECK(PE_GetPartsWidth(NO, 1) == 124 && PE_GetPartsHeight(NO, 1) == 69, "the parts is %dx%d, want 124x69",
		 PE_GetPartsWidth(NO, 1), PE_GetPartsHeight(NO, 1));
	struct parts *p = parts_get(NO);
	Texture *t = &parts_get_cg(p, 0)->common.texture;
	if (t->handle && t->w == 124 && t->h == 69) {
		uint8_t *got = gfx_get_pixels(t), *want = st_model(st_scene_px, 1280, 720, 124, 69);
		int bad = 0;
		for (int i = 0; i < 124 * 69; i++)
			bad += !!memcmp(got + i * 4, want + i * 3, 3) || got[i * 4 + 3] != 255;
		ST_CHECK(!bad, "%d pixels of the parts are not the picture, opaque", bad);
		free(got);
		free(want);
	} else {
		ST_CHECK(0, "the parts has no 124x69 texture");
	}
	/* The picture written over is the one shown next time. */
	st_scene(1280, 720, 44);
	ST_CHECK(st_save("SaveData\\Thumb1000.qnt", 124) == 1, "SaveThumbnail over the file: not true");
	a[1].i = name;
	ST_CHECK(bu_call("PartsEngine", "Parts_SetPartsCG", 4, 0, 4, a).i == 1, "Parts_SetPartsCG again: not true");
	t = &parts_get_cg(parts_get(NO), 0)->common.texture;
	if (t->handle && t->w == 124 && t->h == 69) {
		uint8_t *got = gfx_get_pixels(t), *want = st_model(st_scene_px, 1280, 720, 124, 69);
		int bad = 0;
		for (int i = 0; i < 124 * 69; i++)
			bad += !!memcmp(got + i * 4, want + i * 3, 3);
		ST_CHECK(!bad, "%d pixels of the parts are not the new picture", bad);
		free(got);
		free(want);
	} else {
		ST_CHECK(0, "the parts has no 124x69 texture after the second picture");
	}
	/* A save without a picture (a folder from before): no CG, no error. */
	enum { NO2 = 9101 };
	union vm_value b[4] = { { .i = NO2 }, { .i = cf_str_slot("<save>Thumb1001.qnt") }, { .i = 0 }, { .i = 1 } };
	ST_CHECK(bu_call("PartsEngine", "Parts_SetPartsCG", 4, 0, 4, b).i == 0, "Parts_SetPartsCG without a file: not false");
	ST_CHECK(!parts_get_cg(parts_get(NO2), 0)->common.texture.handle, "a parts without a picture has a texture");
	PE_ReleaseParts(NO);
	PE_ReleaseParts(NO2);
	st_remove_dir();
}

static int st_vm_int(const char *fn, int nargs, int a, int b)
{
	stack_push((union vm_value){ .i = a });
	if (nargs > 1)
		stack_push((union vm_value){ .i = b });
	vm_call(st_fn(fn), -1);
	return stack_pop().i;
}

/* ST5: the script's own file handling on the picture (real bytecode):
 * gamesave::detail::IsExistThumbnail and CopyThumbnail, and a delete. */
static void st_lifecycle(void)
{
	st_begin(1280, 720, 5);
	ST_CHECK(!st_vm_int("gamesave::detail::IsExistThumbnail", 1, 0, 0), "IsExistThumbnail(0) before any picture");
	ST_CHECK(!st_vm_int("gamesave::detail::CopyThumbnail", 2, 1000, 0), "CopyThumbnail(1000, 0) without a picture: not false");
	ST_CHECK(st_save("SaveData\\Thumb0000.qnt", 124) == 1, "SaveThumbnail: not true");
	ST_CHECK(st_vm_int("gamesave::detail::IsExistThumbnail", 1, 0, 0) == 1, "IsExistThumbnail(0) after the picture");
	ST_CHECK(st_vm_int("gamesave::detail::CopyThumbnail", 2, 1000, 0) == 1, "CopyThumbnail(1000, 0): not true");
	size_t n0 = 0, n1 = 0;
	uint8_t *f0 = st_read("Thumb0000.qnt", &n0), *f1 = st_read("Thumb1000.qnt", &n1);
	ST_CHECK(f0 && f1 && n0 == n1 && !memcmp(f0, f1, n0), "Thumb1000.qnt is not a copy of Thumb0000.qnt");
	free(f0);
	free(f1);
	st_expect("Thumb1000.qnt", st_scene_px, 1280, 720, 124, 69, "ST5 the copy");
	/* The second save takes the picture of its own moment. */
	st_scene(1280, 720, 55);
	ST_CHECK(st_save("SaveData\\Thumb0000.qnt", 124) == 1, "the second SaveThumbnail: not true");
	ST_CHECK(st_vm_int("gamesave::detail::CopyThumbnail", 2, 1001, 0) == 1, "CopyThumbnail(1001, 0): not true");
	st_expect("Thumb1001.qnt", st_scene_px, 1280, 720, 124, 69, "ST5 the second save");
	f0 = st_read("Thumb1000.qnt", &n0);
	f1 = st_read("Thumb1001.qnt", &n1);
	ST_CHECK(f0 && f1 && (n0 != n1 || memcmp(f0, f1, n0)), "the first save's picture changed with the second");
	free(f0);
	free(f1);
	/* The list's delete (LocalSave@Delete, FUNC 30459) removes the save file
	 * and leaves the picture; a save that takes the number again writes over
	 * it. */
	st_scene(1280, 720, 555);
	ST_CHECK(st_save("SaveData\\Thumb0000.qnt", 124) == 1, "the third SaveThumbnail: not true");
	ST_CHECK(st_vm_int("gamesave::detail::CopyThumbnail", 2, 1000, 0) == 1, "CopyThumbnail(1000, 0) over a picture: not true");
	st_expect("Thumb1000.qnt", st_scene_px, 1280, 720, 124, 69, "ST5 the number taken again");
	/* gamesave::detail::Delete (FUNC 6244; nothing in the script calls it)
	 * would remove the picture too: its second half is system.DeleteSaveFile
	 * on the name of FUNC 6222. (The first half, the save itself, builds its
	 * name with the game's EX file, which the probe does not load.) */
	stack_push((union vm_value){ .i = 1000 });
	vm_call(st_fn("gamesave::detail::GameSave_GetThumbnailFileName"), -1);
	union vm_value d[1] = { { .i = stack_pop().i } };
	ST_CHECK(!strcmp(heap_get_string(d[0].i)->text, "Thumb1000.qnt"), "GameSave_GetThumbnailFileName(1000) is \"%s\"",
		 heap_get_string(d[0].i)->text);
	ST_CHECK(bu_call("system", "DeleteSaveFile", 1, 0, 1, d).i == 1, "DeleteSaveFile on the picture: not true");
	ST_CHECK(!st_exists("Thumb1000.qnt"), "the delete left Thumb1000.qnt");
	ST_CHECK(st_exists("Thumb1001.qnt") && st_exists("Thumb0000.qnt"), "the delete took another picture");
	ST_CHECK(!st_vm_int("gamesave::detail::IsExistThumbnail", 1, 1000, 0), "IsExistThumbnail(1000) after Delete");
	st_remove_dir();
}

/* ST6 (also passes before the fix): another declaration keeps the binding it
 * had, and the PartsFunc 6 thumbnail of the games before is as it was (its
 * second argument divides the surface as it stands; libsys4's QNT). */
static void st_guards(void)
{
	st_save_dir();
	int lib = bu_lib("PartsEngine"), f = sf_find(lib, "SaveThumbnail", 2);
	if (f < 0) {
		printf("SAVE-THUMBNAIL PartsEngine.SaveThumbnail is not declared\n");
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	ain->libraries[lib].functions[f].arguments[1].type.data = AIN_FLOAT;
	cn_gl_setup();
	st_scene(1280, 720, 6);
	union vm_value a[2] = { { .i = cf_str_slot("SaveData\\Thumb0000.qnt") }, { .f = 124.0f } };
	bu_call("PartsEngine", "SaveThumbnail", 2, 0, 2, a);
	ST_CHECK(!st_exists("Thumb0000.qnt") && !st_exists("SaveData"), "SaveThumbnail(string, float) wrote a file");

	struct string *old = make_string("Old.qnt", 7);
	ST_CHECK(PE_save_thumbnail(old, 4), "PE_save_thumbnail: not true");
	free_string(old);
	size_t size = 0;
	uint8_t *b = st_read("Old.qnt", &size);
	ST_CHECK(b && size > 52 && st_dw(b, 8) == 52 && st_dw(b, 20) == 320 && st_dw(b, 24) == 180 && st_dw(b, 40) > 0,
		 "PE_save_thumbnail(4) did not write a 320x180 QNT with a 52 byte header and an alpha plane");
	free(b);
	char *p = mj_join(st_dir, "Old.qnt");
	struct cg *cg = b ? cg_load_file(p) : NULL;
	free(p);
	if (cg) {
		const uint8_t *px = cg->pixels;
		ST_CHECK(px[0] == 255 && !px[1] && !px[2], "PE_save_thumbnail drew the scene: (%d,%d,%d), want the red "
			 "surface", px[0], px[1], px[2]);
		cg_free(cg);
	}
	st_remove_dir();
}

/* ST7 (also passes before the fix): a declaration that returns an int keeps
 * the empty function too. The arguments are the original's, so a binding
 * made without looking at the shape would write the file. */
static void st_guard_return(void)
{
	st_save_dir();
	int lib = bu_lib("PartsEngine"), f = sf_find(lib, "SaveThumbnail", 2);
	if (f < 0) {
		printf("SAVE-THUMBNAIL PartsEngine.SaveThumbnail is not declared\n");
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
	ain->libraries[lib].functions[f].return_type.data = AIN_INT;
	cn_gl_setup();
	st_scene(1280, 720, 7);
	union vm_value a[2] = { { .i = cf_str_slot("SaveData\\Thumb0000.qnt") }, { .i = 124 } };
	bu_call("PartsEngine", "SaveThumbnail", 2, 0, 2, a);
	ST_CHECK(!st_exists("Thumb0000.qnt") && !st_exists("SaveData"), "int SaveThumbnail(string, int) wrote a file");
	st_remove_dir();
}

static int test_save_thumbnail(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "ST1", st_game_call }, { "ST2", st_sizes }, { "ST3", st_names }, { "ST4", st_list },
		{ "ST5", st_lifecycle }, { "ST6", st_guards }, { "ST7", st_guard_return },
	};
	int n = sizeof cases / sizeof cases[0], failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		int r = cn_fork_case(cases[i].name, cases[i].fn, &st_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("SAVE-THUMBNAIL ENVIRONMENT FAILURE: %d of %d cases could not run, %d failed\n", env, n, failed);
		return 2;
	}
	printf("SAVE-THUMBNAIL %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
