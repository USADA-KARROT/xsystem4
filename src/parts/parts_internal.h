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

#ifndef SYSTEM4_PARTS_INTERNAL_H
#define SYSTEM4_PARTS_INTERNAL_H

#include <cglm/types.h>
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "queue.h"
#include "scene.h"
#include "swf.h"

typedef struct cJSON cJSON;
struct string;
struct hash_table;
struct parts_message_window;

// NOTE: actual value is +1
enum parts_state_type {
	PARTS_STATE_DEFAULT = 0,
	PARTS_STATE_HOVERED = 1,
	PARTS_STATE_CLICKED = 2,
#define PARTS_NR_STATES 3
};

enum parts_motion_type {
	PARTS_MOTION_POS,
	PARTS_MOTION_ALPHA,
	PARTS_MOTION_CG,
	PARTS_MOTION_HGAUGE_RATE,
	PARTS_MOTION_VGAUGE_RATE,
	PARTS_MOTION_NUMERAL_NUMBER,
	PARTS_MOTION_MAG_X,
	PARTS_MOTION_MAG_Y,
	PARTS_MOTION_ROTATE_X,
	PARTS_MOTION_ROTATE_Y,
	PARTS_MOTION_ROTATE_Z,
	PARTS_MOTION_VIBRATION_SIZE
#define PARTS_NR_MOTION_TYPES (PARTS_MOTION_VIBRATION_SIZE+1)
};

union parts_motion_param {
	int i;
	float f;
	struct {
		int x;
		int y;
	};
};

enum parts_motion_curve {
	PARTS_MOTION_CURVE_LINEAR = 0,
	PARTS_MOTION_CURVE_EASE_IN,
	PARTS_MOTION_CURVE_EASE_OUT,
	PARTS_MOTION_CURVE_EASE_IN_OUT,
	PARTS_MOTION_CURVE_CUBIC_IN,
	PARTS_MOTION_CURVE_CUBIC_OUT,
	PARTS_MOTION_CURVE_CUBIC_IN_OUT,
};

struct parts_motion {
	TAILQ_ENTRY(parts_motion) entry;
	enum parts_motion_type type;
	enum parts_motion_curve curve;
	union parts_motion_param begin;
	union parts_motion_param end;
	int begin_time;
	int end_time;
};

TAILQ_HEAD(parts_motion_list, parts_motion);

struct sound_motion {
	TAILQ_ENTRY(sound_motion) entry;
	int begin_time;
	int sound_no;
	bool played;
};

struct parts_text_char {
	Texture t;
	char ch[5];
	float advance;
	Point off; // of the texture from the pen position on its line
};

struct parts_text_line {
	struct parts_text_char *chars;
	int nr_chars;
	unsigned height;
	float width;
};

enum parts_type {
	PARTS_UNINITIALIZED,
	PARTS_CG,
	PARTS_TEXT,
	PARTS_ANIMATION,
	PARTS_NUMERAL,
	PARTS_HGAUGE,
	PARTS_VGAUGE,
	PARTS_CONSTRUCTION_PROCESS,
	PARTS_FLASH,
	PARTS_FLAT,
	PARTS_MOVIE,
	PARTS_RECT_DETECTION,
	PARTS_LAYOUT_BOX,
	PARTS_3DLAYER,
#define PARTS_NR_TYPES (PARTS_3DLAYER+1)
};

struct parts_common {
	Texture texture;
	// Lazily read alpha for immutable CG textures; reset on replacement.
	uint8_t *hit_mask;
	int w, h;
	Point origin_offset;
	Rectangle hitbox;
	Rectangle surface_area;
};

struct parts_cg {
	struct parts_common common;
	int no;
	struct string *name;
};

struct parts_text {
	struct parts_common common;
	unsigned nr_lines;
	struct parts_text_line *lines;
	int line_space;
	struct { float x; int y; } cursor;
	struct text_style ts;
};

struct parts_animation {
	struct parts_common common;
	struct string *cg_name;
	unsigned start_no;
	unsigned nr_frames;
	Texture *frames;
	unsigned frame_time;
	unsigned elapsed;
	unsigned current_frame;
};

enum parts_numeral_font_type {
	// each digit has a separate CG (only first CG number stored in `cg_no` member)
	PARTS_NUMERAL_FONT_SEPARATE = 0,
	// digits packed into single CG
	PARTS_NUMERAL_FONT_COMBINED = 1,
	// each digit has a separate CG (CG numbers stored in `width` member)
	PARTS_NUMERAL_FONT_SEPARATE2 = 2,
};

struct parts_numeral_font {
	enum parts_numeral_font_type type;
	int cg_no;
	int width[12];
	Texture cg[12];
};

// v14 表示タイプ (the numeral's type byte, 0x5b3e80 +0x28)
enum parts_numeral_show_type {
	PARTS_NUMERAL_SHOW_CG = 0,        // a CG per character (ＣＧ名 is its format)
	PARTS_NUMERAL_SHOW_LINKED_CG = 1, // the characters side by side in one CG (幅リスト)
	PARTS_NUMERAL_SHOW_FONT = 2,      // drawn with the numeral's font
};

// The most digits a v14 numeral is padded to (桁數); the original has no limit.
#define PARTS_NUMERAL_MAX_LENGTH 32

struct parts_numeral {
	struct parts_common common;
	bool have_num;
	int num;
	int space;
	int show_comma;
	int length;
	int font_no;
	// v14 表示タイプ (enum parts_numeral_show_type); before v14 the CG font
	// `font_no` draws the number.
	int show_type;
	// v14 ゼロパディング (SetNumeralShowPadding): the leading zeros are drawn.
	// Hidden ones keep their cells (0x5b5188).
	bool zero_pad;
	bool full_pitch; // 全角: full-width digits
	struct text_style font;
	// v14: ＣＧ名 and 幅リスト, kept as they were given (the getters return
	// them whatever the type is).
	struct string *cg_name;
	int cg_widths[12];
};

struct parts_gauge {
	struct parts_common common;
	Texture cg;
	int cg_no;
	float rate;
	// v14 keeps the operands, not just their quotient (0x566b40).
	float numerator, denominator;
	bool reverse;
	struct string *cg_name;
};

// Serialized in save data. Do not reorder.
enum parts_cp_op_type {
	PARTS_CP_CREATE,
	PARTS_CP_CREATE_PIXEL_ONLY,
	PARTS_CP_CG,
	PARTS_CP_FILL,
	PARTS_CP_FILL_ALPHA_COLOR,
	PARTS_CP_FILL_AMAP,
	PARTS_CP_DRAW_RECT,
	PARTS_CP_DRAW_CUT_CG,
	PARTS_CP_COPY_CUT_CG,
	PARTS_CP_DRAW_TEXT,
	PARTS_CP_COPY_TEXT,
	PARTS_CP_GRAY_FILTER,
	PARTS_CP_FILL_WITH_ALPHA,
	PARTS_CP_FILL_PIE_AMAP,
	// The values are saved: new types go at the end. Those below work on
	// the surface's pixels (struct parts_cp_pixel).
	PARTS_CP_MUL_AMAP_GRADATION_ROWS,
	PARTS_CP_MUL_AMAP_GRADATION_COLUMNS,
	PARTS_CP_BLUR_H,
	PARTS_CP_BLUR_V,
	PARTS_CP_FILL_CIRCLE_AMAP,
	PARTS_CP_FILL_CIRCLE_BLEND,
	PARTS_CP_FILL_POLYGON_BLEND,
	PARTS_CP_TILE_CG,
	PARTS_CP_DRAW_CIRCLE_AMAP,
#define PARTS_NR_CP_TYPES (PARTS_CP_DRAW_CIRCLE_AMAP+1)
};

struct parts_cp_create {
	int w;
	int h;
};

struct parts_cp_cg {
	int no;
};

struct parts_cp_fill {
	int x, y, w, h;
	int r, g, b, a;
};

struct parts_cp_cut_cg {
	int cg_no;
	int dx, dy, dw, dh;
	int sx, sy, sw, sh;
	int interp_type;
};

struct parts_cp_text {
	struct string *text;
	int x, y;
	int line_space;
	struct text_style style;
};

struct parts_cp_filter {
	int x, y, w, h;
	bool full_size;
};

// A sector of the ellipse with radii (rx, ry) around (x, y), from `start`
// over `sweep` degrees; `angle` rotates it. Larger radii are not built
// (xsystem4's limit; the largest in a known game is 16).
#define PARTS_CP_PIE_MAX_RADIUS 1024
struct parts_cp_pie {
	int x, y;
	int rx, ry;
	int start, sweep;
	int a;
	int angle;
};

// The commands that work on the surface's pixels; each uses the fields its
// native step does:
//   MUL_AMAP_GRADATION_ROWS / _COLUMNS (v14 commands 25 / 26): the rectangle
//     (or `full`), the alpha `a` at its first line and `a2` after its last;
//   BLUR_H / BLUR_V (27 / 28): the rectangle (or `full`) and `radius`;
//   FILL_CIRCLE_AMAP / _BLEND (102 / 106): the centre (x, y), `radius` and
//     the alpha `a`, with the colour for the blend;
//   FILL_POLYGON_BLEND (97): `points` (x0, y0, x1, y1, ...) and the colour;
//   TILE_CG (129): the rectangle (or `full`) and `cg_no`;
//   DRAW_CIRCLE_AMAP (52): the centre (x, y), `radius`, `line_width` and
//     the alpha `a`.
// Larger circles and polygons are not built (xsystem4's limits).
#define PARTS_CP_CIRCLE_MAX_RADIUS 1024
#define PARTS_CP_POLYGON_MAX_POINTS 1024
struct parts_cp_pixel {
	int x, y, w, h;
	bool full;
	int r, g, b, a;
	int a2;
	int radius;
	int cg_no;
	int line_width;
	int nr_points;
	int *points;
};

struct parts_cp_op {
	TAILQ_ENTRY(parts_cp_op) entry;
	enum parts_cp_op_type type;
	union {
		struct parts_cp_create create;
		struct parts_cp_cg cg;
		struct parts_cp_fill fill;
		struct parts_cp_cut_cg cut_cg;
		struct parts_cp_text text;
		struct parts_cp_filter filter;
		struct parts_cp_pie pie;
		struct parts_cp_pixel pixel;
	};
};

struct parts_construction_process {
	struct parts_common common;
	TAILQ_HEAD(, parts_cp_op) ops;
};

// v14 パネル (component type 14): the size and colour its surface is built
// from, as the native widget keeps them (+0xa8/+0xac, +0xb0..+0xbc; 200x200
// and (220,220,220,255) from its constructor 0x4dc6f0). The surface is the
// normal state's construction: Create, then FillWithAlpha over all of it.
// Not saved: after a load the values are taken from those two operations.
struct parts_panel {
	bool valid;
	int w, h;
	int r, g, b, a;
};

enum parts_flash_blend_mode {
	PARTS_FLASH_BLEND_NORMAL0    = 0,
	PARTS_FLASH_BLEND_NORMAL1    = 1,
	PARTS_FLASH_BLEND_LAYER      = 2,
	PARTS_FLASH_BLEND_MULTIPLY   = 3,
	PARTS_FLASH_BLEND_SCREEN     = 4,
	PARTS_FLASH_BLEND_LIGHTEN    = 5,
	PARTS_FLASH_BLEND_DARKEN     = 6,
	PARTS_FLASH_BLEND_DIFFERENCE = 7,
	PARTS_FLASH_BLEND_ADD        = 8,
	PARTS_FLASH_BLEND_SUBTRACT   = 9,
	PARTS_FLASH_BLEND_INVERT     = 10,
	PARTS_FLASH_BLEND_ALPHA      = 11,
	PARTS_FLASH_BLEND_ERASE      = 12,
	PARTS_FLASH_BLEND_OVERLAY    = 13,
	PARTS_FLASH_BLEND_HARDLIGHT  = 14,
};

enum parts_draw_filter {
	PARTS_DRAW_FILTER_NORMAL   = 0,
	PARTS_DRAW_FILTER_ADDITIVE = 1,
	PARTS_DRAW_FILTER_MULTIPLY = 2,
	PARTS_DRAW_FILTER_SCREEN   = 3,
};

struct parts_flash_object {
	TAILQ_ENTRY(parts_flash_object) entry;
	uint16_t depth;
	uint16_t character_id;
	mat4 matrix;
	struct swf_cxform_with_alpha color_transform;
	enum parts_flash_blend_mode blend_mode;
};

struct parts_flash {
	struct parts_common common;
	struct string *name;
	struct swf *swf;

	struct swf_tag *tag;
	bool has_ended;
	bool stopped;
	unsigned elapsed;
	int current_frame;
	struct hash_table *dictionary;
	struct hash_table *bitmaps;  // bitmap character id -> struct texture *
	struct hash_table *sprites;  // sprite character id -> struct parts_flash_object *
	TAILQ_HEAD(, parts_flash_object) display_list;
};

struct flat_layer_state {
	int current_frame;
	bool stopped;
	bool suppress_advance;
	int jump_target;  // pending jump frame, -1 = none
	// Per-timeline: last matched script key index for change detection
	// (-2 = uninitialized, -1 = no match, >= 0 = key index)
	int *last_script_keys;
	// Per-timeline: child layer state (for TIMELINE libs), NULL otherwise
	struct flat_layer_state **children;
	size_t nr_timelines;
};

// Per-frame CG list for a STOP_MOTION library. lib_indices[k] is
// the library index of the CG to display at frame k.
struct flat_stop_motion_frames {
	int *lib_indices;
	int count;
};

struct parts_flat {
	struct parts_common common;
	struct string *name;
	struct flat *flat;
	bool stopped;
	bool needs_advance;
	unsigned elapsed;
	int end_frame;
	int pending_seek_delta;
	struct flat_layer_state *root_state;
	size_t nr_libraries;
	Texture *textures;  // indexed by library index (only CG libs have valid textures)
	// Indexed by library index. Only entries for STOP_MOTION libraries have lib_indices populated.
	struct flat_stop_motion_frames *stop_motion_frames;
};

struct parts_movie {
	struct parts_common common;
	int sprite_no;  // SACT sprite number used as movie render target
};

enum parts_layout_type {
	PARTS_LAYOUT_FREE       = 0,  // no automatic layout
	PARTS_LAYOUT_VERTICAL   = 1,
	PARTS_LAYOUT_HORIZONTAL = 2,
};

// In AliceSoft's PartsEngine implementation, LayoutBox is a component type
// that is NOT per-state. We store it per-state for uniformity with other
// component types, but parts_get_layout_box() and all code in layoutbox.c
// operate on states[0] only. This is safe as long as game scripts never
// convert a LayoutBox parts to/from another component type.
struct parts_layout_box {
	struct parts_common common;
	enum parts_layout_type layout_type;
	bool wrap;
	int wrap_size;
	int align;
	int padding_top;
	int padding_bottom;
	int padding_left;
	int padding_right;
};

struct parts_3dlayer {
	struct parts_common common;
	int plugin;    // ReignEngine plugin handle (-1 = none)
	int sprite_no; // SACT sprite used as render target
};

struct parts_state {
	enum parts_type type;
	union {
		struct parts_common common;
		struct parts_cg cg;
		struct parts_text text;
		struct parts_animation anim;
		struct parts_numeral num;
		struct parts_gauge gauge;
		struct parts_construction_process cproc;
		struct parts_flash flash;
		struct parts_flat flat;
		struct parts_movie movie;
		struct parts_layout_box layout_box;
		struct parts_3dlayer layer3d;
	};
};

TAILQ_HEAD(parts_list, parts);

struct parts_params {
	int z;
	Point pos;
	bool show;
	uint8_t alpha;
	struct { float x, y; } scale;
	struct { float x, y, z; } rotation;
	SDL_Color add_color;
	SDL_Color multiply_color;
	// v14 SetComponentReverseLR/TB (native parts +0xaa/+0xa9). local: the
	// parts' own flags; global: XOR of the flags along the parent chain.
	bool reverse_lr;
	bool reverse_tb;
	// global only: the accumulated transform of the parts' anchor frame, as
	// the original keeps one per parts (+0x250, written by 0x535260 ->
	// 0x579bb0 -> 0x4e6d80): the parent's matrix, then this level's
	// T(pos) * Rz * S(scale) * S(reverse) (column vectors; the original's
	// row-vector product is S(reverse) * S(scale) * R * T(pos) * parent).
	// `pos` is the matrix applied to the origin. Not saved: rebuilt from
	// the local parameters along the parent chain.
	mat4 matrix;
	// global only (v14): the signed sum of the add colours along the parent
	// chain, as the original keeps it (0x579bb0, +0x8c..+0x94; a parts in
	// 減算色モード adds its own negated, 0x535260). add_color is what is
	// drawn: the sum clamped to 0..255, or, when all three components are
	// below 1, the negated sum with add_subtract set (0x5aac60, 0x5b2330).
	// Not saved: rebuilt like the matrix.
	int add_sum[3];
	bool add_subtract;
};

struct parts {
	struct sprite sp;
	enum parts_state_type state;
	struct parts_state states[PARTS_NR_STATES];
	TAILQ_ENTRY(parts) parts_list_entry;
	TAILQ_ENTRY(parts) child_list_entry;
	TAILQ_ENTRY(parts) dirty_list_entry;
	struct parts_list children;
	struct parts_params local;
	struct parts_params global;
	struct parts *parent;
	int dirty;
	int no;
	int delegate_index;
	int sprite_deform;
	bool clickable;
	// v14: clickable only because its pactex says 點擊許可 1. The loader sets
	// it after PE_SetClickable, which clears it: the script, a button or a
	// ＣＧ判定部件 then owns the flag. Without an event such a parts leaves a
	// press as the whole-screen click (v14_click_unclaimed). Not saved.
	bool click_permission_only;
	bool pass_cursor;
	bool pixel_hittest;
	bool lock_input_state;
	bool want_save;
	bool draggable;
	int on_cursor_sound;
	int on_click_sound;
	int origin_mode;
	int pending_parent;
	int linked_to;
	int linked_from;
	bool is_hovered;
	int hover_time;
	int draw_filter;
	// v14 減算色モード (SetComponentSubColorMode; pactex 減算色模式, stored as
	// == 1 by 0x554020, native +0xc4): the parts' own add colour counts
	// negative in the sum along the parent chain (parts_params.add_sum).
	// MapNodeView's name and base blink yellow by subtracting (0, 0, c)
	// from white. Saved since XPE version 8 (a list
	// of parts numbers after the parts records).
	bool sub_color_mode;
	bool message_window;
	struct parts_message_window *message;
	int alpha_clipper_parts_no;
	bool clip_enabled;
	Rectangle clip_area;
	int margin_top;
	int margin_bottom;
	int margin_left;
	int margin_right;
	// v14: the offset a free layout box gives each of its children, by its
	// own origin mode and size (native +0x238/+0x23c, written by 0x5499e0).
	// It is added to the position wherever the parts is placed (0x53567a,
	// 0x537b2f). Not saved: the box lays out again after loading.
	Point layout_offset;
	struct parts_motion_list motion;
	int controller_no;
	int component_type;   // v14 component widget type (EPartsType)
	// v14 low-level (EPartsType 18) state types recognized by the pactex
	// loader (19 CG, 21 text, 24 numeral, 27 CG detection); 0 = none.
	int component_state_type[PARTS_NR_STATES];
	int unique_id;        // v14 unique ID for event dispatch
	char *user_component_name; // v14 user component name from pactex (heap-allocated)
	// v14 button widget: base CG name (<base>／普通 etc.) and the native
	// enable flag (+0xb0, default enabled), stored inverted so a new parts
	// starts enabled.
	char *button_cg_name;
	bool button_disabled;
	// v14 user component data: key/value strings from the pactex 數據 list
	// (Get/SetUserComponentData), stored as key0, value0, key1, value1, ...
	char **uc_data;
	int nr_uc_data;
	// v14 pactex 編輯上表示 = 0: the parts and its subtree are never shown
	// (folded into global.show). The native parts keeps it at +0xac beside
	// 表示 (+0xab) and skips a parts unless both are set (e.g. 0x53c3c5).
	bool edit_hidden;
	// v14 pactex 構築部件 normal state whose steps the loader does not run:
	// the canvas its first step creates (コマンド 0/1, 先矩形 W,H). The
	// original builds it while loading, so a layout box sizes the parts by
	// it; only used while the state has no size of its own (layoutbox.c).
	int pactex_canvas_w, pactex_canvas_h;
	struct parts_panel panel;
};

#define PARTS_LIST_FOREACH(iter) TAILQ_FOREACH(iter, &parts_list, parts_list_entry)
#define PARTS_LIST_FOREACH_REVERSE(iter) TAILQ_FOREACH_REVERSE(iter, &parts_list, parts_list, parts_list_entry)
#define PARTS_FOREACH_CHILD(iter, parent) TAILQ_FOREACH(iter, &parent->children, child_list_entry)

// parts.c
extern struct parts_list parts_list;
void parts_button_set_cg_name(struct parts *parts, const char *base);
void parts_set_edit_hidden(struct parts *parts, bool hidden);
void parts_uc_data_set(struct parts *parts, const char *key, const char *value);
const char *parts_uc_data_get(struct parts *parts, const char *key);

// A controller is identified by its ID (parts->controller_no). Its position
// in the stack (0 = bottom) decides the drawing order and changes when another
// controller is inserted below it, removed or moved: the original manager
// keeps a vector of controllers (+0x88) and finds one by the number of its
// root parts (e.g. 0x53d480). Engines before v14 only push and pop, so there
// the ID always equals the position. The system overlay controller lives
// outside the stack.
//
// v14: the ID is the number of the controller's root parts (the original's
// ctrl+4, whose number +0x40 every lookup compares). Each new parts is linked
// under the root of the controller it goes to (0x578cb0 -> 0x53d1d0 ->
// 0x53afb0: the new parts' parent number +0x13c becomes the root's number and
// the root's child list +0xbc gets the new number), so Parent::get of a
// top-level parts is its layer ID, the root's children are the layer's
// top-level parts, and the root itself has no parent (its +0x13c stays 0: the
// table's listener is off while 0x53d220 creates the two roots). The script
// relies on all of that: AFL_Parts_GetLayerIDByParts walks Parent::get up to
// 0 and returns the last number; AdvMessageWindow@SearchMessageBoxWindow
// lists GetChild of the active layer's ID; CMessageWindow@OnErasingLayer
// compares the walked ID with EraseLayer's. The root is not drawn, not hit
// and not saved, and goes with its controller (0x53bea0 releases both roots).
// The original takes the numbers from the free-number allocator (base
// 1000010000, 0x539b13); here they come from their own range below the
// numbers PE_GetFreeNumber hands out, which starts at 1000001000, so the
// two never meet. The system overlay controller keeps its constant and has
// no root parts (parts on it have no parent, as before).
#define PARTS_CONTROLLER_STACK_MAX 10000
#define PARTS_CONTROLLER_SYSTEM_OVERLAY PARTS_CONTROLLER_STACK_MAX
#define PARTS_CONTROLLER_ID_BASE 1000000000

struct parts_controller_stack {
	int nr_controllers;
	// The ID of the controller new parts go to,
	// PARTS_CONTROLLER_SYSTEM_OVERLAY, or -1 when none is designated (v14:
	// the top of the stack acts, the original's null pointer at +0x94).
	int active;
	int *ids;  // ids[position] is the ID of the controller there
	int cap;
};
extern struct parts_controller_stack ctrl_stack;
extern bool parts_multi_controller;
int parts_controller_index(int id);
void parts_controller_move(int id, int index);
void parts_controller_set_stack(int nr, const int *ids);
// The ID a save without an ID section gives the controller at a position.
int parts_controller_default_id(int position);
// v14: the controller's root parts (created when missing), NULL for the
// system overlay controller, an unknown ID, or an older engine.
struct parts *parts_controller_root(int id);
bool parts_is_controller_root(struct parts *parts);
// Puts every parts that has no parent or a root as its parent under the root
// of the controller it belongs to (after loading a save).
void parts_controller_relink_roots(void);

// pe_v14_activity.c
// A controller is releasing the parts of this number; if it is an activity
// component (a pactex part), remember that. The activity table keeps its
// entries (and IsExistActivity its answer) when a controller releases parts.
void pe_v14_activity_parts_released(int parts_no);
bool pe_v14_activity_number_released(int parts_no);

struct parts *parts_try_get(int parts_no);
struct parts *parts_get(int parts_no);
bool parts_exists(int parts_no);
struct parts_cg *parts_get_cg(struct parts *parts, int state);
struct parts_text *parts_get_text(struct parts *parts, int state);
struct parts_animation *parts_get_animation(struct parts *parts, int state);
struct parts_numeral *parts_get_numeral(struct parts *parts, int state);
struct parts_gauge *parts_get_hgauge(struct parts *parts, int state);
struct parts_gauge *parts_get_vgauge(struct parts *parts, int state);
struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state);
struct parts_flash *parts_get_flash(struct parts *parts, int state);
struct parts_flat *parts_get_flat(struct parts *parts, int state);
struct parts_movie *parts_get_movie(struct parts *parts, int state);
struct parts_layout_box *parts_get_layout_box(struct parts *parts);
struct parts_3dlayer *parts_get_3dlayer(struct parts *parts, int state);
void parts_set_pos(struct parts *parts, Point pos);
void parts_set_global_pos(Point pos);
void parts_clear_hit_mask(struct parts_common *common);
void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h);
void parts_set_scale_x(struct parts *parts, float mag);
void parts_set_scale_y(struct parts *parts, float mag);
void parts_set_rotation_z(struct parts *parts, float rot);
void parts_set_alpha(struct parts *parts, int alpha);
void parts_set_reverse(struct parts *parts, bool lr, bool tb);
void parts_set_sub_color_mode(struct parts *parts, bool enable);
Point parts_placed_pos(const struct parts *parts);
void parts_parent_transform(struct parts *parts, mat4 out);
void parts_load_transform(struct parts *parts);
Rectangle parts_screen_hitbox(struct parts *parts, struct parts_common *common);
Point parts_screen_upper_left(struct parts *parts, struct parts_common *common);
void parts_set_state(struct parts *parts, enum parts_state_type state);
void parts_release(int parts_no);
void parts_release_all(void);
void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h);
extern bool parts_message_window_show;

// message_window.c: the sidecar owns text/strings; the parts owns the background.
void parts_message_window_free(struct parts_message_window *message);
struct parts_text *parts_message_window_render_text(struct parts *parts, Point *position);

// message queue (implemented in PartsEngine.c)
void parts_enqueue_message(int type, int parts_no, int delegate_index, int unique_id);
void parts_enqueue_message_vars(int type, int parts_no, int delegate_index, int unique_id,
                                int nr_vars, const int *vars);

extern struct parts_numeral_font *parts_numeral_fonts;
extern int parts_nr_numeral_fonts;

// for save.c
void parts_list_resort(struct parts *parts);
void parts_list_ensure_sorted(void);
void parts_ensure_scene_registered(struct parts *parts);
void parts_component_dirty(struct parts *parts);
void parts_recalculate_hitbox(struct parts *parts);
void parts_state_reset(struct parts_state *state, enum parts_type type);
bool parts_cg_set(struct parts *parts, struct parts_cg *cg, struct string *cg_name);
bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no);
void parts_text_append(struct parts *parts, struct parts_text *t, struct string *text);
bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time);
bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time);
void parts_numeral_font_init(struct parts_numeral_font *font);
bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n);
bool parts_numeral_update(struct parts *parts, struct parts_numeral *num);
int parts_numeral_codes(struct parts_numeral *num, uint8_t *codes, bool *shown);
bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name);
bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no);
void parts_v14_gauge_render_geometry(struct parts *parts, struct parts_gauge *g, bool vertical, mat4 transform, Rectangle *source);
void parts_v14_gauge_surface(struct parts_gauge *g, Rectangle *out);
void parts_v14_gauge_fill_rect(struct parts_gauge *g, bool vertical, Rectangle *out);
void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);
void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);

// text.c
void parts_text_free(struct parts_text *t);
void parts_text_extent(struct parts_text *t, int *w, int *h);
struct string *parts_text_line_get(struct parts_text_line *line);
struct string *parts_text_get(struct parts_text *t);

// render.c
int parts_clip_area_transform(struct parts *parts, mat4 out);
int parts_effective_alpha_clipper(struct parts *parts);
void parts_render_init(void);
void parts_render_update(void);
void parts_engine_dirty(void);
void parts_engine_clean(void);
void parts_dirty(struct parts *parts);
void parts_sprite_render(struct sprite *sp);
void parts_render(struct parts *parts);
void parts_render_family(struct parts *parts);
void parts_anchor_transform(struct parts *parts, float angle, bool rotate_scale, mat4 out);
void parts_box_transform(struct parts *parts, struct parts_common *common, mat4 out);

// motion.c
void parts_clear_motion(struct parts *parts);
void parts_add_motion(struct parts *parts, struct parts_motion *motion);

// input.c
extern bool parts_began_click;
void parts_reset_input(void);
void parts_input_reset_drag(struct parts *parts);
bool parts_screen_to_box(struct parts *parts, struct parts_common *common, float sx, float sy,
		float *bx, float *by);

// message.c
enum parts_message_type {
	PARTS_MSG_MOUSE_ENTER    = 1,
	PARTS_MSG_MOUSE_MOVE     = 2,
	PARTS_MSG_MOUSE_LEAVE    = 3,
	PARTS_MSG_MOUSE_WHEEL    = 4,
	PARTS_MSG_MOUSE_CLICK    = 5,
	PARTS_MSG_MOUSE_ON       = 6,
	PARTS_MSG_DRAG_BEGIN     = 7,
	PARTS_MSG_DRAGGING       = 8,
	PARTS_MSG_DRAG_END       = 9,
	PARTS_MSG_DROP_ENTER     = 10,
	PARTS_MSG_DROP_ON        = 11,
	PARTS_MSG_DROPPED        = 12,
	PARTS_MSG_DROP_LEAVE     = 13,
	PARTS_MSG_KEY_TRIGGER    = 14,
	PARTS_MSG_KEY_DOWN       = 15,
	PARTS_MSG_KEY_UP         = 17,
};

void parts_msg_push(struct parts* parts, int type, const char *fmt, ...);

// construction.c
void parts_cp_op_free(struct parts_cp_op *op);
void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op);
bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc);
bool parts_clear_construction_process(struct parts_construction_process *cproc);
void parts_cp_tile(uint8_t *pixels, int tw, int th, int x, int y, int w, int h,
		const uint8_t *src, int cw, int ch);
struct parts_panel *parts_get_panel(struct parts *parts);
void parts_panel_init(struct parts *parts, int w, int h, int r, int g, int b, int a);
void parts_panel_set_size(struct parts *parts, int w, int h);
void parts_panel_set_color(struct parts *parts, int r, int g, int b, int a);
void parts_panel_load_blended(struct parts *parts);

// flash.c
void parts_flash_free(struct parts_flash *f);
bool parts_flash_load(struct parts *parts, struct parts_flash *f, struct string *filename);
bool parts_flash_update(struct parts_flash *f, int passed_time);
bool parts_flash_seek(struct parts_flash *f, int frame);

// flat.c
void parts_flat_free(struct parts_flat *f);
bool parts_flat_load(struct parts *parts, struct parts_flat *f, struct string *filename);
bool parts_flat_update(struct parts_flat *f, int passed_time);
int parts_flat_find_library(struct flat *fl, const char *name);
int parts_flat_stop_motion_get_cg_lib(struct parts_flat *f, int sm_lib_idx, int local);

struct flat_emitter;
struct flat_key_data_graphic;

struct flat_emitter_particle {
	vec2 pos;          // emitter-space position (pixels)
	vec2 scale;
	vec3 rot;          // degrees (x, y, z)
	float fade_alpha;  // 0-1.0
	int cg_lib_idx;    // CG library index of the texture for this particle
};

// Per-key emitter properties after applying the emitter's inherit_* flags.
struct flat_emitter_layer_effective {
	vec2 pos;
	bool reverse_lr, reverse_tb;
	float alpha;
	vec3 add_color;
	vec3 mul_color;
	int draw_filter;
	bool use_scale;
	bool use_rotation;
	bool use_origin;
};

#define FLAT_MAX_ANCESTOR_DEPTH 32
struct flat_key_stack {
	const struct flat_key_data_graphic *keys[FLAT_MAX_ANCESTOR_DEPTH];
	int count;
};

typedef void (*flat_emitter_particle_fn)(const struct flat_emitter_particle *p,
		void *ud);
bool parts_flat_emitter_get_align_offset(struct parts_flat *f, int emitter_lib_idx, vec2 out);
void parts_flat_foreach_emitter_particle(struct parts_flat *f, int emitter_lib_idx,
		const struct flat_key_data_graphic *keys,
		int birth_frame, int age, int frame_count,
		flat_emitter_particle_fn fn, void *ud);
void parts_flat_build_layer_matrix(const struct flat_key_data_graphic *key,
		vec2 pos,
		bool use_rotation, bool use_scale, bool use_origin,
		bool reverse_lr, bool reverse_tb,
		mat4 out);
void parts_flat_build_emitter_base_matrix(const struct flat_emitter *em,
		const struct flat_key_stack *stack, mat4 out);
void parts_flat_emitter_resolve_layer(
		const struct flat_emitter *em,
		const struct flat_key_data_graphic *key,
		float parts_alpha, float layer_alpha,
		struct flat_emitter_layer_effective *out);

// layoutbox.c
void parts_do_layout(struct parts *parts);
void parts_layout_size_changed(struct parts *parts);

// debug.c
struct sprite;
void parts_debug_init(void);
cJSON *parts_engine_to_json(struct sprite *sp, bool verbose);
cJSON *parts_sprite_to_json(struct sprite *sp, bool verbose);

static inline bool parts_state_valid(int state)
{
	return state >= 0 && state <= 2;
}

#endif /* SYSTEM4_PARTS_INTERNAL_H */
