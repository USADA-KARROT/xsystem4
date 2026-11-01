/* Cancellation contracts: 0x5462b0/0x5463c0/0x546470 and
 * 0x548153..0x548278. DragEnd serialization is 0x577660:
 * mode 2 for another button, mode 3 for release without a position.
 * Synthetic parts only. Included after key_events_fixture.inc helpers.
 * Not covered: drop-target leave (14), whose target selection is separate
 * from these source-slot cases. */
static int kc_count(const struct ke_frame *f, int type, int no)
{
	int n = 0;
	for (int i = 0; i < f->n && i < KE_CAP; i++)
		n += f->m[i].type == type && f->m[i].no == no;
	return n;
}
static void kc_cancel_events(const struct ke_frame *f, int key, int mode, bool began)
{
	KE_CHECK(kc_count(f, 7, KE_A) == !began, "cancel DragBegin count");
	KE_CHECK(kc_count(f, 8, KE_A) == 1 && kc_count(f, 10, KE_A) == 1, "cancel Draging/DragEnd count");
	KE_CHECK(kc_count(f, 9, KE_A) == 0 && kc_count(f, 13, KE_A) == 0, "cancel sent normal drop completion");
	int previous = 0;
	for (int i = 0; i < f->n && i < KE_CAP; i++) {
		const struct ke_msg *m = &f->m[i];
		if (m->no != KE_A || (m->type != 7 && m->type != 8 && m->type != 10)) continue;
		KE_CHECK(m->type > previous, "cancel event order %d after %d", m->type, previous);
		previous = m->type;
		KE_CHECK(m->delegate == 3 && m->unique == 30, "cancel receiver metadata");
		if (m->type == 7) KE_CHECK(m->n == 1 && m->v[0] == key, "DragBegin key");
		if (m->type == 8) KE_CHECK(m->n == 5 && m->v[0] == 150 && m->v[1] == 140 && m->v[2] == 150 && m->v[3] == 140 && m->v[4] == key, "cancel drag coordinates/key/payload length");
		if (m->type == 10) KE_CHECK(m->n == 2 && m->v[0] == key && m->v[1] == mode, "cancel end mode/key");
	}
}
static void kc_no_drag_events(const struct ke_frame *f)
{
	for (int i = 0; i < f->n && i < KE_CAP; i++)
		KE_CHECK(f->m[i].type < 7 || f->m[i].type > 14,
			"cancelled source emitted another drag event: %d", f->m[i].type);
}
static void kc_left_cancel(int other, bool moved, bool draggable)
{
	ke_setup(); struct parts *a = parts_get(KE_A); a->draggable = draggable;
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	if (moved) {
		struct ke_frame motion = ke_frame(170, 145, 16);
		KE_CHECK(kc_count(&motion, 7, KE_A) == 1
			&& kc_count(&motion, 8, KE_A) == 1, "movement did not begin drag");
		KE_CHECK(a->local.pos.x == (draggable ? 120 : 100)
			&& a->local.pos.y == (draggable ? 105 : 100),
			"movement before cancellation did not respect draggable");
	}
	ke_key(other, true); struct ke_frame f = ke_frame(moved ? 170 : 150, moved ? 145 : 140, 16);
	kc_cancel_events(&f, VK_LBUTTON, 2, moved);
	KE_CHECK(a->local.pos.x == 100 && a->local.pos.y == 100, "cancel did not restore original position");
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && !he_hll_int("GetClickNumber"), "cancel changed focus/click-number");
	ke_key(other, false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, other) == 1 && ke_count(&f, 4, KE_A, other) == 1, "cancel swallowed other release click");
	KE_CHECK(!he_hll_int("GetClickNumber"), "other release became left click-number");
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, VK_LBUTTON) == 1 && ke_count(&f, 4, KE_A, VK_LBUTTON) == 1 && he_hll_int("GetClickNumber") == KE_A, "cancel swallowed left release");
}
static void kc_left_right(void) { kc_left_cancel(VK_RBUTTON, false, false); }
static void kc_left_middle(void) { kc_left_cancel(VK_MBUTTON, true, true); }
static void kc_left_right_moved(void) { kc_left_cancel(VK_RBUTTON, true, true); }
static void kc_left_unmoved_parts(void) { kc_left_cancel(VK_RBUTTON, true, false); }
static void kc_suppression(void)
{
	ke_setup(); ke_frame(150, 140, 16); ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame cancelled = ke_frame(150, 140, 16);
	kc_cancel_events(&cancelled, VK_LBUTTON, 2, false);
	ke_key(VK_LBUTTON, false); struct ke_frame partial = ke_frame(450, 140, 16);
	kc_no_drag_events(&partial);
	ke_key(VK_MBUTTON, true); struct ke_frame f = ke_frame(450, 140, 16);
	kc_no_drag_events(&f);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "partial release unlocked target selection");
	KE_CHECK(ke_count(&f, 15, KE_A, VK_MBUTTON) == 1 && !ke_count(&f, 15, KE_B, VK_MBUTTON), "suppression stopped/misdirected focused keys");
	KE_CHECK(parts_get(KE_A)->is_hovered && !parts_get(KE_B)->is_hovered, "suppression still walked hover");
	ke_key(VK_RBUTTON, false); f = ke_frame(450, 140, 16); kc_no_drag_events(&f);
	ke_key(VK_LBUTTON, true); f = ke_frame(450, 140, 16); kc_no_drag_events(&f);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "one remaining button did not hold suppression");
	ke_key(VK_LBUTTON, false); ke_key(VK_MBUTTON, false); ke_frame(450, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(450, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "all released did not unlock next press");
}
static void kc_priority(bool left)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, left); ke_key(VK_MBUTTON, true); ke_key(VK_RBUTTON, true);
	ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, left ? VK_LBUTTON : VK_MBUTTON, 2, false);
}
static void kc_priority_left(void) { kc_priority(true); }
static void kc_priority_middle(void) { kc_priority(false); }
static void kc_other_cancel(int source, int other)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(source, true); ke_frame(150, 140, 16);
	ke_key(other, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, source, 2, false);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "other cancellation changed focus");
	KE_CHECK(ke_count(&f, 15, KE_A, other) == 1, "cancel lost new key trigger");
	ke_key(other, false); f = ke_frame(150, 140, 16);
	kc_no_drag_events(&f);
	KE_CHECK(ke_count(&f, 18, KE_A, other) == 1, "cancel lost other key release");
	ke_key(source, false); f = ke_frame(150, 140, 16);
	kc_no_drag_events(&f);
	KE_CHECK(ke_count(&f, 18, KE_A, source) == 1, "cancel lost source key release");
}
static void kc_right_cancel(void) { kc_other_cancel(VK_RBUTTON, VK_LBUTTON); }
static void kc_middle_left(void) { kc_other_cancel(VK_MBUTTON, VK_LBUTTON); }
static void kc_middle_right(void) { kc_other_cancel(VK_MBUTTON, VK_RBUTTON); }
static void kc_right_middle(void) { kc_other_cancel(VK_RBUTTON, VK_MBUTTON); }
static void kc_outside_for(int key)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(key, true); ke_frame(150, 140, 16);
	mouse_focus = false; ke_key(key, false);
	struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, key, 3, false);
	KE_CHECK(!ke_count(&f, 4, 0, key) && !ke_count(&f, 4, KE_A, key), "outside cancellation clicked");
	KE_CHECK(ke_count(&f, 18, 0, key) == 1 && ke_count(&f, 18, KE_A, key) == 1, "outside cancellation lost key-up");
}
static void kc_outside(void) { kc_outside_for(VK_LBUTTON); }
static void kc_right_outside(void) { kc_outside_for(VK_RBUTTON); }
static void kc_middle_outside(void) { kc_outside_for(VK_MBUTTON); }
