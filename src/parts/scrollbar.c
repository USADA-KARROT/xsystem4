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

#include <math.h>

#include "system4.h"
#include "system4/ain.h"

#include "vm.h"
#include "xsystem4.h"
#include "parts.h"
#include "parts_internal.h"

/*
 * What a v14 豎滾動條 scrolls: the amounts and the position, as the original
 * widget keeps them (+0x110 全體スクロール量, +0x114 表示量, +0x118 スクロール
 * 位置, +0x11c スクロールレート), and the event it sends when the position or
 * the rate changed. The bar, its buttons and dragging are not implemented:
 * the scripts read the amounts and move the position themselves (the wheel
 * and the keys of the backlog, CBackLogView@MouseWheelEvent), and are told
 * of the result by the event.
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

/* SetVScrollbarScrollPos (0x591a90 -> 0x542360): the position is kept within
 * 0 and the largest position, and the rate is the position over that. */
void parts_scrollbar_set_pos(struct parts *parts, int pos)
{
	struct parts_scrollbar *sb = &parts->scrollbar;
	const int max = scrollbar_max(sb);
	pos = pos >= max ? max : pos;
	pos = pos > 0 ? pos : 0;
	sb->pos = pos;
	sb->rate = scrollbar_rate((float)pos / (float)max);
}

/* SetVScrollbarScrollRate (0x591ac0 -> 0x542400): the rate is kept within 0
 * and 1, and the position is the largest position times it, truncated. */
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
 * Once per update (0x573800 -> 0x5738e0, at 0x573a95): a bar whose position
 * or rate is not the one it last reported sends its observers the position
 * and the largest position, and remembers both. The scripts receive it as
 * message 21 (0x4df5b0), CPartsFunctionSet@CallFunctionScroll(scrollPos,
 * total). A bar nobody listens to remembers them all the same.
 */
void parts_scrollbar_update(void)
{
	if (ain->version < 14)
		return;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts->component_type != PARTS_COMPONENT_VSCROLLBAR)
			continue;
		struct parts_scrollbar *sb = &parts->scrollbar;
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
