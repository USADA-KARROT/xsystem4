/* Copyright (C) 2026 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/cg.h"

#include "asset_manager.h"
#include "vm.h"
#include "xsystem4.h"
#include "parts.h"
#include "parts_internal.h"

/*
 * What a v14 豎滾動條 scrolls: the amounts and the position, as the original
 * widget keeps them (+0x110 全體スクロール量, +0x114 表示量, +0x118 スクロール
 * 位置, +0x11c スクロールレート), and the event it sends when the position or
 * the rate changed. The scripts read the amounts and move the position
 * themselves (the wheel and the keys of the backlog,
 * CBackLogView@MouseWheelEvent), and are told of a move by the event.
 * A 橫滾動條 is the same widget lying on its side (0x536660 makes one with
 * the 豎滾動條's constructor 0x572fb0 and then 0x573610, which clears +0xc):
 * the same amounts, the same setters and the same update and event; the
 * settings' sliders are its only use.
 *
 * The knob and the track (below) are drawn and can be dragged and clicked.
 * The two buttons at the ends are not made: every bar of the game has 前サ
 * イズ and 次サイズ 0, which hides them (0x5738e0).
 */

// The largest position: 全體スクロール量 less 表示量, at least 0 (0x542360).
static int scrollbar_max(const struct parts_scrollbar *sb)
{
	return sb->total > sb->view ? sb->total - sb->view : 0;
}

// 0x5423ad..0x5423d3 and 0x542403..0x542440: a rate of 1 or more is 1, and
// so is one that does not compare (0 / 0, when nothing can be scrolled);
// one that is not above 0 is 0.
static float scrollbar_rate(float rate)
{
	if (!(rate < 1.0f))
		return 1.0f;
	return rate > 0.0f ? rate : 0.0f;
}

/* The amounts of a bar the engine makes (0x541e30: 長さ 256, 幅 24, 前サイズ
 * and 次サイズ 24, 全體スクロール量 100, 表示量 1, position and rate 0, 按鈕點
 * 擊移動量 1, every name empty), for a parts that becomes a 橫滾動條 without
 * a pactex. Nothing was scrolled, so nothing is to be reported; without a
 * ＣＧ名 there is no knob to draw. */
void parts_scrollbar_construct(struct parts *parts)
{
	parts->scrollbar = (struct parts_scrollbar) {
		.total = 100, .view = 1,
		.length = 256, .width = 24, .prev_size = 24, .next_size = 24, .button_move = 1,
	};
	parts_scrollbar_look_free(parts);
}

/* SetVScrollbarScrollPos and SetHScrollbarScrollPos (0x591a90, 0x5923b0 ->
 * 0x542360): the position is kept within 0 and the largest position, and the
 * rate is the position over that. */
void parts_scrollbar_set_pos(struct parts *parts, int pos)
{
	struct parts_scrollbar *sb = &parts->scrollbar;
	const int max = scrollbar_max(sb);
	pos = pos >= max ? max : pos;
	pos = pos > 0 ? pos : 0;
	sb->pos = pos;
	sb->rate = scrollbar_rate((float)pos / (float)max);
}

/* SetVScrollbarScrollRate and SetHScrollbarScrollRate (0x591ac0, 0x5923e0 ->
 * 0x542400): the rate is kept within 0 and 1, and the position is the
 * largest position times it, truncated. The rate is kept as it was given:
 * 0.5 of a bar that scrolls 99 reads back as 0.5, at position 49. */
void parts_scrollbar_set_rate(struct parts *parts, float rate)
{
	struct parts_scrollbar *sb = &parts->scrollbar;
	const int max = scrollbar_max(sb);
	rate = scrollbar_rate(rate);
	int pos = (int)((float)max * rate);
	pos = pos >= max ? max : pos;
	sb->pos = pos > 0 ? pos : 0;
	sb->rate = rate;
}

/* SetVScrollbarTotalSize and SetVScrollbarViewSize (0x591a10, 0x591a50): a
 * new amount places the position again. */
void parts_scrollbar_set_total(struct parts *parts, int total)
{
	if (parts->scrollbar.total == total)
		return;
	parts->scrollbar.total = total;
	parts_scrollbar_set_pos(parts, parts->scrollbar.pos);
}

void parts_scrollbar_set_view(struct parts *parts, int view)
{
	if (parts->scrollbar.view == view)
		return;
	parts->scrollbar.view = view;
	parts_scrollbar_set_pos(parts, parts->scrollbar.pos);
}

/*
 * 全體スクロール量サイズ連動 and 表示量サイズ連動 (0x5749d0): an amount follows
 * the size of another parts, named in the pactex and kept as its number
 * (+0x214, +0x218). A number of 0, or of no parts, leaves the amount as it
 * is. Else the amount is a size of the state that parts shows (its widget's
 * vtable +0x38): for a 豎滾動條, whose +0xc is 1 (0x572ff9), the height,
 * through the widget's vtable +0x28 as Parts_GetPartsHeight takes it
 * (0x58c6e0); for a 橫滾動條, whose +0xc is clear, the width, through +0x24
 * as Parts_GetPartsWidth takes it (0x574a8b; 0x58c620). SceneStandViewer's
 * bar has both links: the standing picture's height and its frame's. No
 * 橫滾動條 of the game is linked, so the width was never seen at work.
 * Read for a linked 低等級部件 only (0x564ef0 -> 0x5b7e90, 0x564dd0 ->
 * 0x5b7d20: a state with nothing in it is 0 high). Which state another kind
 * of widget reports was not read, and the step back of 0x5b7e90 from a state
 * with nothing in it to an earlier one is not made.
 */
static int scrollbar_linked(const struct parts *bar, int link, int amount)
{
	struct parts *linked = link ? parts_try_get(link) : NULL;
	if (!linked)
		return amount;
	const struct parts_common *shown = &linked->states[linked->state].common;
	return bar->component_type == PARTS_COMPONENT_HSCROLLBAR ? shown->w : shown->h;
}

// GetVScrollbarTotalSize and GetVScrollbarViewSize (0x591b30, 0x591b60) give
// the linked size without keeping it.
int parts_scrollbar_total(struct parts *parts)
{
	return scrollbar_linked(parts, parts->scrollbar.total_link, parts->scrollbar.total);
}

int parts_scrollbar_view(struct parts *parts)
{
	return scrollbar_linked(parts, parts->scrollbar.view_link, parts->scrollbar.view);
}

/* What a bar has when its activity was loaded counts as reported (0x574bc0
 * after the loader 0x544630): the sliders of the settings are loaded at
 * position 99 of 99 and send nothing until a script moves them. */
void parts_scrollbar_loaded(struct parts *parts)
{
	parts->scrollbar.reported_pos = parts->scrollbar.pos;
	parts->scrollbar.reported_rate = parts->scrollbar.rate;
}

static void scrollbar_look_build(struct parts *parts);

/*
 * Once per update (0x573800 -> 0x5738e0). First the linked amounts are taken
 * (0x57390c .. 0x573964): one that differs is kept and places the position
 * again. Then (0x573a95) a bar whose position or rate is not the one it last
 * reported sends its observers the position and the largest position, and
 * remembers both. The scripts receive it as message 21 (0x4df5b0),
 * CPartsFunctionSet@CallFunctionScroll(scrollPos, total). A bar nobody
 * listens to remembers them all the same.
 * The original updates the widgets after the input (0x53c5b0: the input
 * update 0x547c90 at 0x53c64d, then 0x53c490 -> 0x535260, whose 0x535adc
 * calls the widget's update), so a drag sends its event in the same
 * UpdateComponent; PE_Update calls this after PE_UpdateInputState for that.
 * A knob whose names changed has its pictures loaded here (the original
 * builds its inner parts when the widget is dirty, 0x573800).
 * Not the original's: there the comparison, the message and the remembering
 * are left out while the flag at +0xa0 of the update's context is clear
 * (0x5739c4 .. 0x5739cf), and happen on the first update with it set. The
 * flag is handed down the tree, the parent's and an argument of each parts
 * (0x579c81 .. 0x579cae); the argument was not traced and is taken to be
 * "shown". Here a bar that is not shown reports as well.
 * A 橫滾動條 is updated by the same function. The settings' sound page
 * depends on it: it adds the event to three sliders and then sets their
 * position to a volume, and its 初始化 buttons set the default one; the
 * volume is written by the event, from the position it carries
 * (UserConfig::SetSoundEvent).
 */
void parts_scrollbar_update(void)
{
	if (ain->version < 14)
		return;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts->component_type != PARTS_COMPONENT_VSCROLLBAR
				&& parts->component_type != PARTS_COMPONENT_HSCROLLBAR)
			continue;
		struct parts_scrollbar *sb = &parts->scrollbar;
		if (parts->scrollbar_look && parts->scrollbar_look->dirty)
			scrollbar_look_build(parts);
		parts_scrollbar_set_total(parts, parts_scrollbar_total(parts));
		parts_scrollbar_set_view(parts, parts_scrollbar_view(parts));
		if (sb->reported_pos == sb->pos && sb->reported_rate == sb->rate)
			continue;
		if (parts->delegate_index >= 0) {
			const int vars[2] = { sb->pos, scrollbar_max(sb) };
			parts_enqueue_message_vars(21, parts->no, parts->delegate_index,
					parts->unique_id, 2, vars);
		}
		sb->reported_pos = sb->pos;
		sb->reported_rate = sb->rate;
	}
}

/*
 * The knob (バー) and the track (背景).
 *
 * The original makes them inner parts of the widget (+0xb0 and +0xa4) and
 * builds them whenever the widget is dirty (0x573800):
 *  - the knob is a 低等級部件 with three states (0x5743f0 -> 0x574670), each
 *    the CG named 覆蓋バーＣＧ名, or ＣＧ名 with ／バー when that is empty,
 *    followed by ／普通, ／オン or ／ダウン (0x542a00, 0x5748a0); nothing at
 *    all without a ＣＧ名;
 *  - the track is one CG, 覆蓋背景ＣＧ名 or ＣＧ名 with ／背景 (0x542b40,
 *    0x574470), and the one of the two that can be clicked (its +0x1a4).
 *    The game's tracks are transparent: they are where a click lands.
 * Here both are pictures the bar's parts draws over its own state
 * (parts_render) and finds under the cursor (parts_scrollbar_hittest).
 * The names are the CN executable's (GBK): a pactex with another spelling
 * gives the bar no ＣＧ名, and it shows nothing, as before.
 *
 * Not the original's: a フラット名 is not read and the knob is a CG or
 * nothing; a missing state CG of the knob shows its ／普通 (the original
 * draws a grey rectangle there, 0x574920); a knob or a track whose CG is
 * missing is neither drawn nor hit (the original draws a grey knob of the
 * computed length, 0x574670, and makes a construction surface for the
 * track, 0x574470); no state of the bar's parts changes the pictures.
 */
static const char *const scrollbar_state_suffix[3] = {
	"\xa3\xaf\xc6\xd5\xcd\xa8",         // ／普通 (0x7dc59c)
	"\xa3\xaf\xa5\xaa\xa5\xf3",         // ／オン (0x7dc5a4)
	"\xa3\xaf\xa5\xc0\xa5\xa6\xa5\xf3", // ／ダウン (0x7dc50c)
};
#define SCROLLBAR_BAR_SUFFIX "\xa3\xaf\xa5\xd0\xa9\x60"	// ／バー (0x7d9cf8)
#define SCROLLBAR_BG_SUFFIX "\xa3\xaf\xb1\xb3\xbe\xb0"		// ／背景 (0x7d9cf0)

static bool scrollbar_vertical(const struct parts *parts)
{
	return parts->component_type == PARTS_COMPONENT_VSCROLLBAR;
}

static bool scrollbar_is_bar(const struct parts *parts)
{
	return parts->component_type == PARTS_COMPONENT_VSCROLLBAR
		|| parts->component_type == PARTS_COMPONENT_HSCROLLBAR;
}

static void scrollbar_textures_free(struct parts_scrollbar_look *look)
{
	for (int i = 0; i < 3; i++) {
		if (look->bar[i].handle)
			gfx_delete_texture(&look->bar[i]);
		look->bar[i] = (Texture) { 0 };
	}
	if (look->bg.handle)
		gfx_delete_texture(&look->bg);
	look->bg = (Texture) { 0 };
	look->bar_cg_length = -1;
}

// The bar that is being dragged and the one whose knob is under the cursor
// (one of each at most: the original's focus and cursor walk).
static int scrollbar_drag_no;
static int scrollbar_hover_no;

void parts_scrollbar_look_free(struct parts *parts)
{
	struct parts_scrollbar_look *look = parts->scrollbar_look;
	if (!look)
		return;
	scrollbar_textures_free(look);
	free(look->cg_name);
	free(look->over_bar_name);
	free(look->over_bg_name);
	free(look);
	parts->scrollbar_look = NULL;
	if (scrollbar_drag_no == parts->no)
		scrollbar_drag_no = 0;
	if (scrollbar_hover_no == parts->no)
		scrollbar_hover_no = 0;
}

static char *scrollbar_name(const char *name)
{
	return name && name[0] ? xstrdup(name) : NULL;
}

/* The pactex's ＣＧ名, 覆蓋バーＣＧ名 and 覆蓋背景ＣＧ名 (0x544630: +0x124,
 * +0x16c, +0x184). Without a ＣＧ名 both names are empty (0x542a00 at
 * 0x542a34, 0x542b40 at 0x542b74), so the bar has no look. */
void parts_scrollbar_set_names(struct parts *parts, const char *cg_name,
		const char *over_bar_name, const char *over_bg_name)
{
	parts_scrollbar_look_free(parts);
	if (!cg_name || !cg_name[0])
		return;
	struct parts_scrollbar_look *look = xcalloc(1, sizeof(struct parts_scrollbar_look));
	look->cg_name = xstrdup(cg_name);
	look->over_bar_name = scrollbar_name(over_bar_name);
	look->over_bg_name = scrollbar_name(over_bg_name);
	look->bar_cg_length = -1;
	look->dirty = true;
	parts->scrollbar_look = look;
}

static bool scrollbar_load(Texture *t, const char *name)
{
	int no;
	struct cg *cg = asset_cg_load_by_name(name, &no);
	if (!cg)
		return false;
	gfx_init_texture_with_cg(t, cg);
	cg_free(cg);
	return true;
}

/* The knob's three CGs and the track's (0x5743f0, 0x574470). The knob's
 * length is its normal CG along the bar (0x573e30 -> 0x4e0870: the width
 * of a 橫滾動條, the height of a 豎滾動條). */
static void scrollbar_look_build(struct parts *parts)
{
	struct parts_scrollbar_look *look = parts->scrollbar_look;
	look->dirty = false;
	scrollbar_textures_free(look);
	char base[512], name[600];
	if (look->over_bar_name)
		snprintf(base, sizeof base, "%s", look->over_bar_name);
	else
		snprintf(base, sizeof base, "%s" SCROLLBAR_BAR_SUFFIX, look->cg_name);
	snprintf(name, sizeof name, "%s%s", base, scrollbar_state_suffix[0]);
	if (scrollbar_load(&look->bar[0], name)) {
		look->bar_cg_length = scrollbar_vertical(parts) ? look->bar[0].h : look->bar[0].w;
		for (int i = 1; i < 3; i++) {
			snprintf(name, sizeof name, "%s%s", base, scrollbar_state_suffix[i]);
			scrollbar_load(&look->bar[i], name);
		}
	}
	if (look->over_bg_name)
		snprintf(name, sizeof name, "%s", look->over_bg_name);
	else
		snprintf(name, sizeof name, "%s" SCROLLBAR_BG_SUFFIX, look->cg_name);
	scrollbar_load(&look->bg, name);
}

static struct parts_scrollbar_look *scrollbar_look(struct parts *parts)
{
	if (ain->version < 14 || !scrollbar_is_bar(parts) || !parts->scrollbar_look)
		return NULL;
	if (parts->scrollbar_look->dirty)
		scrollbar_look_build(parts);
	return parts->scrollbar_look;
}

bool parts_scrollbar_has_look(struct parts *parts)
{
	return ain->version >= 14 && scrollbar_is_bar(parts) && parts->scrollbar_look;
}

/* cvttss2si: a float that is no int (out of range, NaN) gives INT_MIN. */
static int scrollbar_trunc(float f)
{
	if (!(f > (float)INT_MIN && f < (float)INT_MAX))
		return INT_MIN;
	return (int)f;
}

struct scrollbar_geometry {
	int max;         // the largest position
	int bar_length;  // +0x228, 0x573e30
	int track;       // 長さ less 前サイズ and 次サイズ
	Point bar;       // the knob's corner, from the parts' anchor
	Point bg;        // the track's corner
	bool bar_shown;  // the knob's +0xab
};

static void scrollbar_geometry(struct parts *parts, const struct parts_scrollbar_look *look,
		struct scrollbar_geometry *g)
{
	const struct parts_scrollbar *sb = &parts->scrollbar;
	const bool vertical = scrollbar_vertical(parts);
	g->max = scrollbar_max(sb);
	g->track = sb->length - sb->next_size - sb->prev_size;
	// 0x573e30: no knob when nothing scrolls; else the CG's length, or a
	// length in proportion, at least +0xb4 (16), without one.
	if (!g->max)
		g->bar_length = 0;
	else if (look && look->bar_cg_length >= 0)
		g->bar_length = look->bar_cg_length;
	else
		g->bar_length = max(scrollbar_trunc((float)g->track / (float)sb->total * (float)sb->view), 16);
	// 0x573bf0: the knob's offset is the rate of what is left of the
	// track, truncated and at least 0; the origin mode of the parts moves
	// all of it by the widget's size (vertical 幅 x 長さ, horizontal 長さ x
	// 幅, 0x574990), halved towards 0 (cdq; sub; sar).
	const int off = max(scrollbar_trunc(((float)g->track - (float)g->bar_length) * sb->rate), 0);
	const int w = vertical ? sb->width : sb->length;
	const int h = vertical ? sb->length : sb->width;
	int ox = 0, oy = 0;
	switch (parts->origin_mode) {
	case 2: case 5: case 8: ox = -(w / 2); break;
	case 3: case 6: case 9: ox = -w; break;
	}
	switch (parts->origin_mode) {
	case 4: case 5: case 6: oy = -(h / 2); break;
	case 7: case 8: case 9: oy = -h; break;
	}
	if (vertical) {
		g->bg = (Point) { ox, oy + sb->prev_size };
		g->bar = (Point) { ox, oy + sb->prev_size + off };
	} else {
		g->bg = (Point) { ox + sb->prev_size, oy };
		g->bar = (Point) { ox + sb->prev_size + off, oy };
	}
	// 0x5738e0 shows the knob while something scrolls; 0x5743f0 hides it
	// when its length is below 1.
	g->bar_shown = g->max != 0 && g->bar_length >= 1;
}

static void scrollbar_anchor(struct parts *parts, mat4 m)
{
	parts_anchor_transform(parts, glm_rad(parts->local.rotation.z), true, m);
}

/* The knob or the track under a point of the screen, the knob first: it is
 * made last and lies over the track. The knob does not take the cursor when
 * the bar passes it (the bar hands +0x1a5, オン指針透過, to its inner parts,
 * 0x573b20); the track can always be clicked (+0x1a4, 0x574470). An area is
 * its CG's size; 鼠標指針ピクセル判定 is not followed (no bar of the game
 * has it). */
enum parts_scrollbar_hit parts_scrollbar_hittest(struct parts *parts, Point pos)
{
	struct parts_scrollbar_look *look = scrollbar_look(parts);
	if (!look)
		return PARTS_SCROLLBAR_MISS;
	struct parts_common anchor = { 0 };
	float x, y;
	if (!parts_screen_to_box(parts, &anchor, (float)pos.x, (float)pos.y, &x, &y))
		return PARTS_SCROLLBAR_MISS;
	struct scrollbar_geometry g;
	scrollbar_geometry(parts, look, &g);
	if (g.bar_shown && look->bar[0].handle && !parts->pass_cursor
			&& x >= g.bar.x && x < g.bar.x + look->bar[0].w
			&& y >= g.bar.y && y < g.bar.y + look->bar[0].h)
		return PARTS_SCROLLBAR_BAR;
	if (look->bg.handle && x >= g.bg.x && x < g.bg.x + look->bg.w
			&& y >= g.bg.y && y < g.bg.y + look->bg.h)
		return PARTS_SCROLLBAR_TRACK;
	return PARTS_SCROLLBAR_MISS;
}

/* What parts_render draws for the bar, in the parts' anchor frame: the
 * track and the knob in the state of the input (／ダウン while dragged,
 * ／オン with the cursor on it, else ／普通). NULL for what is not shown. */
Texture *parts_scrollbar_render_textures(struct parts *parts, Texture **bar, Point *bg_pos,
		Point *bar_pos)
{
	*bar = NULL;
	struct parts_scrollbar_look *look = scrollbar_look(parts);
	if (!look)
		return NULL;
	struct scrollbar_geometry g;
	scrollbar_geometry(parts, look, &g);
	*bg_pos = g.bg;
	*bar_pos = g.bar;
	if (g.bar_shown && look->bar[0].handle) {
		const int state = look->dragging ? 2 : look->hovered ? 1 : 0;
		*bar = look->bar[state].handle ? &look->bar[state] : &look->bar[0];
	}
	return look->bg.handle ? &look->bg : NULL;
}

/* The cursor along the bar: the original takes the screen's coordinate as
 * it is (0x572d20, 0x574c60), x for a 橫滾動條 and y for a 豎滾動條. */
static int scrollbar_axis(struct parts *parts, Point pos)
{
	return scrollbar_vertical(parts) ? pos.y : pos.x;
}

/* A drag of the knob (event 16, 0x572e00): the rate at the press plus the
 * cursor's move over what the knob can travel, both in pixels of the
 * screen. 0x542400 keeps the result within 0 and 1 (a track the knob fills
 * divides by 0: NaN and +inf are 1, -inf is 0). Nothing moves while the
 * bar has nothing to scroll. */
static void scrollbar_drag(struct parts *parts, struct parts_scrollbar_look *look, Point pos)
{
	struct scrollbar_geometry g;
	scrollbar_geometry(parts, look, &g);
	if (!g.max)
		return;
	const float rate = (float)(scrollbar_axis(parts, pos) - look->drag_cursor)
		/ (float)(g.track - g.bar_length) + look->drag_rate;
	parts_scrollbar_set_rate(parts, rate);
}

/*
 * The press of the left button on the knob or the track (event 15,
 * 0x572c30): nothing while the bar has nothing to scroll. Else the knob is
 * held down (state 3, its input state locked: +0x92) and the cursor along
 * the bar is kept (+0xb8). A press on the track first moves the knob's
 * centre to the cursor (0x572d90): the cursor less the track's corner on the
 * screen (its matrix applied to (0, 0), 0x534510 / 0x534580) and half the
 * knob, truncated, over what the knob can travel. The rate after that is
 * kept (+0xc0).
 */
static void scrollbar_press(struct parts *parts, struct parts_scrollbar_look *look, Point pos,
		enum parts_scrollbar_hit hit)
{
	struct scrollbar_geometry g;
	scrollbar_geometry(parts, look, &g);
	if (!g.max)
		return;
	look->dragging = true;
	scrollbar_drag_no = parts->no;
	look->drag_cursor = scrollbar_axis(parts, pos);
	if (hit == PARTS_SCROLLBAR_TRACK) {
		mat4 m;
		scrollbar_anchor(parts, m);
		const float corner = scrollbar_vertical(parts)
			? m[0][1] * g.bg.x + m[1][1] * g.bg.y + m[3][1]
			: m[0][0] * g.bg.x + m[1][0] * g.bg.y + m[3][0];
		const float rate = ((float)look->drag_cursor - corner - (float)(g.bar_length / 2))
			/ (float)(g.track - g.bar_length);
		parts_scrollbar_set_rate(parts, rate);
	}
	look->drag_rate = parts->scrollbar.rate;
}

static void scrollbar_set_hover(int no)
{
	if (no == scrollbar_hover_no)
		return;
	struct parts *old = scrollbar_hover_no ? parts_try_get(scrollbar_hover_no) : NULL;
	if (old && old->scrollbar_look)
		old->scrollbar_look->hovered = false;
	scrollbar_hover_no = no;
	struct parts *now = no ? parts_try_get(no) : NULL;
	if (now && now->scrollbar_look)
		now->scrollbar_look->hovered = true;
}

static void scrollbar_end_drag(void)
{
	struct parts *bar = scrollbar_drag_no ? parts_try_get(scrollbar_drag_no) : NULL;
	if (bar && bar->scrollbar_look)
		bar->scrollbar_look->dragging = false;
	scrollbar_drag_no = 0;
}

/*
 * The bars' part of the v14 input update, within an input scope (the
 * original's does nothing outside one, 0x547cba). target is the parts under
 * the cursor that takes the mouse (v14_input_target); a bar is that parts
 * when its knob or its track is under the cursor.
 *
 * The original sends the key events to its focus, the inner parts the left
 * button went down on, which only a press changes (0x546890 at 0x5482f9;
 * 0x578580 on the focus at 0x54836a): at the press event 15 and then 16,
 * then 16 every update while the button is held, 18 at its release. So a
 * knob that is dragged follows the cursor wherever the cursor goes until the
 * button comes up (0x572e00 does not ask where the cursor is), and the
 * release unlocks it (0x572f50). The knob shows ／オン while the cursor is
 * on it (0x577b90 sets state 2 every update) and ／普通 off it (0x577cf0),
 * except while it is locked down.
 *
 * Not the original's: the track's MouseClick (it is clickable) and the
 * cursor sound on moving between the knob and the track are not sent; the
 * KeyTrigger, KeyDown and KeyUp the bar passes on to the scripts are not
 * either (no other v14 parts sends them here).
 */
void parts_scrollbar_input(Point pos, bool down, bool was_down, struct parts *target)
{
	if (ain->version < 14)
		return;
	enum parts_scrollbar_hit hit = PARTS_SCROLLBAR_MISS;
	struct parts_scrollbar_look *look = NULL;
	if (target && parts_scrollbar_has_look(target)) {
		look = scrollbar_look(target);
		hit = parts_scrollbar_hittest(target, pos);
	}
	if (down && !was_down) {
		scrollbar_end_drag();
		if (hit != PARTS_SCROLLBAR_MISS)
			scrollbar_press(target, look, pos, hit);
	}
	struct parts *dragged = scrollbar_drag_no ? parts_try_get(scrollbar_drag_no) : NULL;
	if (dragged && !parts_scrollbar_has_look(dragged)) {
		scrollbar_drag_no = 0;
		dragged = NULL;
	}
	if (dragged && down)
		scrollbar_drag(dragged, scrollbar_look(dragged), pos);
	if (!down && was_down)
		scrollbar_end_drag();
	scrollbar_set_hover(hit == PARTS_SCROLLBAR_BAR ? target->no : 0);
}

/* A new or ended input scope forgets the drag and the cursor (0x53e830,
 * 0x53e8b0 reset the input's elements). */
void parts_scrollbar_reset_input(void)
{
	scrollbar_end_drag();
	scrollbar_set_hover(0);
}
