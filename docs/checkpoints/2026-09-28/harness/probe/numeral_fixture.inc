/* numeral: the v14 數字部件 as the original keeps and draws it.
 *
 * Original (static disassembly only). The numeral object is made by 0x5b3e80:
 * 表示タイプ 0 (+0x28), no ＣＧ名 (+0x10), twelve widths of 0 (+0x2c), the
 * twelve half-width strings "0".."9" "-" "," (+0x38) and the 全角 ones
 * (+0x158), the default font (0x69f1d0: type 0, size 16, white, 太さ 0,
 * 縁取り 0, black edge), 全角 off (+0x2ac), number 0 (+0x2b0), コンマ表示
 * off (+0x2b4), 字間隔 0 (+0x2b8), 桁數 0 (+0x2bc), ゼロパディング ON
 * (+0x2c0). Every change rebuilds it (0x5b4df0):
 *
 *   0x5b5230  the characters: the digits of |number|, at least one; code 0
 *             up to 桁數; with コンマ表示 the comma code before every third
 *             digit from the right; the minus code first.
 *   0x5b4df0  one sprite per character: type 0 the CG "ＣＧ名 formatted
 *             with the index", type 1 the slice (sum of the widths before
 *             it, 0, its width) of the CG, type 2 the character's string in
 *             the numeral's font. Width: the sprites' widths and (n - 1)
 *             字間隔; height: the tallest. The leading code-0 and comma
 *             sprites are shown only with ゼロパディング, the last sprite
 *             always (0x5b5188..0x5b51e6); a hidden sprite keeps its place.
 *   0x5b4380  the sprites side by side from the left, 字間隔 apart, at the
 *             top, moved as a whole by the origin mode over the size.
 *
 * PartsEngine functions (0x599270..0x5999a0 look the component up, then
 * 0x567180..0x5679b0): SetNumeralCG and SetLinked... switch the type to 0
 * and 1, SetNumeralFont to 2, each a no-op for an equal value on that type
 * (0x5b49f0, 0x5b4ac0, 0x5b4b90); SetNumeralShowType takes 0..2 and keeps
 * everything else (0x567880); SetNumeralNumber rebuilds unconditionally
 * (0x567460); SetNumeralLength stores max(length, 0) (0x567690); the others
 * rebuild on a change; SetNumeralSurfaceArea only stores (0x567760). A
 * getter returns 0 / false / "" and writes nothing for a missing component.
 * The pactex loader 0x5b66f0 reads 表示タイプ, then by type ＣＧ名 / ＣＧ名
 * and 幅リスト / the font keys and 全角, then サーフェイスエリア, 數值,
 * コンマ表示, 字間隔, 桁數, ゼロパディング; a missing key is 0.
 *
 * Before the fix SetNumeralFont was unimplemented and SetNumeralShowType,
 * SetNumeralShowPadding, SetNumeralFullPitch and all the getters were empty:
 * the battle's DamageNumber (FUNC 33807: a numeral made by the script, type
 * 2, 字間隔 -6, DamageNumber::m_font) drew nothing, Number::get read 0 for
 * Motion::Executer@GetCurrent, and the loader read neither 數值 nor the CG
 * numerals' ＣＧ名 and 幅リスト.
 *
 * Cases (each in its own fork, with the construction mode's GL context):
 *   NM1 real bytecode: DamageNumber::CreateFont, NEW DamageNumber (FUNC
 *       33807), and the INumeralParts accessors of the object it made
 *       (CNumeralParts@Font::get, Number, NumeralType, CharSpace, Length,
 *       ShowPadding, ShowComma, Wide, CGName, GetSurfaceArea, GetWidths):
 *       the numeral's state, the parts' Z, origin and rotation, and its
 *       size before and after Number::set.
 *   NM2 real bytecode: DamageNumberCollection@Run (CreateDamage -> Parent,
 *       Pos, Show -> Motion::CreateCompiled with the real motion set).
 *   NM3 the PartsEngine functions through their AIN declarations: the
 *       defaults, every setter and getter, the no-op and clamp rules, a
 *       missing component, a state of another type.
 *   NM4 the characters (parts_numeral_codes): padding, commas, sign, the
 *       hidden ones.
 *   NM5 sizes of font numerals on the CN grid and the origin offset; hidden
 *       leading zeros keep the width; 全角; a negative 字間隔.
 *   NM6 drawn pixels with the repo's font: the damage numeral's body and
 *       edge, a hidden zero's cell empty, a later character over the one
 *       before it.
 *   NM7 CG numerals: a linked CG font's slices side by side, 字間隔, the
 *       tallest, a hidden zero, a character without a picture.
 *   NM8 the pactex loader: the three types, 數值 as the number shown, 桁數
 *       and ゼロパディング, a state without the keys.
 *   NM9 guards: AIN version 13 keeps the upstream numeral (no number, no
 *       texture, length 1, CG font by number); v14 off the grid (SJIS rule)
 *       sizes a font numeral by the renderer's advances.
 *
 * Requires base_ui_fixture.inc, layout_box_fixture.inc,
 * construction_fixture.inc and text_size_fixture.inc.
 */

static int nm_failures;
#define NM_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); nm_failures++; } } while (0)

static int nm_fn(const char *name)
{
	char copy[512];
	snprintf(copy, sizeof copy, "%s", name);
	int f = ain_get_function(ain, copy);
	if (f < 0) { printf("NUMERAL function %s not declared\n", name); exit(2); }
	return f;
}

static int nm_struct(const char *name)
{
	int s = ain_get_struct(ain, (char *)name);
	if (s < 0) { printf("NUMERAL struct %s not declared\n", name); exit(2); }
	return s;
}

static int nm_global(const char *name)
{
	for (int i = 0; i < ain->nr_globals; i++)
		if (!strcmp(ain->globals[i].name, name))
			return i;
	printf("NUMERAL global %s not declared\n", name);
	exit(2);
}

/* Calls fno as a method of obj (obj < 0: as a function) with the argument
 * slots given; copies up to 2 result slots and returns how many there were. */
static int nm_call(int fno, int obj, int nargs, const union vm_value *args, union vm_value *ret)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	if (obj >= 0)
		stack_push(obj);
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	if (obj >= 0)
		method_call(fno, VM_RETURN);
	else
		function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	NM_CHECK(call_stack_ptr == csp, "%s: call stack %d != %d", ain->functions[fno].name, call_stack_ptr, csp);
	int n = stack_ptr - sp;
	for (int i = 0; i < n && i < 2 && ret; i++)
		ret[i] = stack[sp + i];
	stack_ptr = sp;
	return n;
}

static int nm_get_int(const char *method, int obj)
{
	union vm_value r[2] = {{ .i = -999 }, { .i = -999 }};
	int n = nm_call(nm_fn(method), obj, 0, NULL, r);
	NM_CHECK(n == 1, "%s returned %d slot(s)", method, n);
	return r[0].i;
}

/* PartsEngine.fn through its AIN declaration; a wrap<int|float|bool> argument
 * takes two slots (page, variable). Returns the first result slot. */
static union vm_value nm_pe_call(const char *fn, int nslots, const union vm_value *slots)
{
	int lib = bu_lib("PartsEngine"), f = -1;
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++)
		if (!strcmp(ain->libraries[lib].functions[j].name, fn))
			f = j;
	if (f < 0) { printf("NUMERAL PartsEngine.%s not declared\n", fn); exit(2); }
	struct ain_hll_function *decl = &ain->libraries[lib].functions[f];
	int sp = stack_ptr, k = 0;
	for (int i = 0; i < decl->nr_arguments; i++) {
		if (decl->arguments[i].type.data == AIN_STRING)
			heap_ref(slots[k].i);
		k += decl->arguments[i].type.data == AIN_WRAP ? 2 : 1;
	}
	if (k != nslots) { printf("NUMERAL PartsEngine.%s takes %d slots, %d given\n", fn, k, nslots); exit(2); }
	for (int i = 0; i < nslots; i++)
		stack_push(slots[i]);
	hll_call(lib, f, 0);
	union vm_value r = { .i = -999 };
	if (stack_ptr > sp)
		r = stack[sp];
	stack_ptr = sp;
	return r;
}
#define NM_PE(fn, ...) nm_pe_call(fn, \
	sizeof((union vm_value[]){ __VA_ARGS__ }) / sizeof(union vm_value), (union vm_value[]){ __VA_ARGS__ })

static struct parts_numeral *nm_num(int no)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	return p && p->states[0].type == PARTS_NUMERAL ? &p->states[0].num : NULL;
}

static void nm_expect_size(int no, int w, int h, const char *what)
{
	int gw = PE_GetPartsWidth(no, 1), gh = PE_GetPartsHeight(no, 1);
	struct parts_numeral *num = nm_num(no);
	int tw = num && num->common.texture.handle ? num->common.texture.w : 0;
	int th = num && num->common.texture.handle ? num->common.texture.h : 0;
	NM_CHECK(gw == w && gh == h, "%s: size %dx%d, want %dx%d", what, gw, gh, w, h);
	NM_CHECK((w > 0 && h > 0) ? (tw == w && th == h) : (tw == 0 && th == 0),
		 "%s: texture %dx%d for a %dx%d numeral", what, tw, th, w, h);
}

/* The font a numeral keeps: type, size, colour, 太さ, 縁取り, edge colour. */
static void nm_expect_font(int no, unsigned face, int size, int r, int g, int b, float bold, float edge,
		int er, int eg, int eb, const char *what)
{
	struct parts_numeral *num = nm_num(no);
	if (!num) { NM_CHECK(false, "%s: parts %d is no numeral", what, no); return; }
	struct text_style *ts = &num->font;
	NM_CHECK(ts->face == face && ts->size == (float)size && ts->color.r == r && ts->color.g == g && ts->color.b == b
		 && ts->bold_weight == bold && text_style_edge_width(ts) == edge
		 && ts->edge_color.r == er && ts->edge_color.g == eg && ts->edge_color.b == eb,
		 "%s: font %u/%g (%d,%d,%d) bold %g edge %g (%d,%d,%d), want %u/%d (%d,%d,%d) bold %g edge %g (%d,%d,%d)", what,
		 ts->face, ts->size, ts->color.r, ts->color.g, ts->color.b, ts->bold_weight, text_style_edge_width(ts),
		 ts->edge_color.r, ts->edge_color.g, ts->edge_color.b, face, size, r, g, b, bold, edge, er, eg, eb);
}

/* A struct page with int members set. */
static int nm_new(const char *type, int n, const union vm_value *members)
{
	int slot = alloc_struct(nm_struct(type));
	assert(slot > 0 && heap[slot].page && heap[slot].page->nr_vars >= n);
	for (int i = 0; i < n; i++)
		heap[slot].page->values[i] = members[i];
	return slot;
}

static void nm_set_global(const char *name, int slot)
{
	heap[global_page_slot].page->values[nm_global(name)].i = slot;
}

/* CASColor::WHITE and BLACK as the alloc function leaves them, and
 * DamageNumber::m_font by the game's own DamageNumber::CreateFont. */
static void nm_damage_globals(void)
{
	nm_set_global("CASColor::WHITE", nm_new("CASColor", 4, (union vm_value[]){ BU_I(255), BU_I(255), BU_I(255), BU_I(255) }));
	nm_set_global("CASColor::BLACK", nm_new("CASColor", 4, (union vm_value[]){ BU_I(0), BU_I(0), BU_I(0), BU_I(255) }));
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = nm_call(nm_fn("DamageNumber::CreateFont"), -1, 0, NULL, r);
	NM_CHECK(n == 1 && da_live_page(r[0].i), "DamageNumber::CreateFont returned %d slot(s) [%d]", n, r[0].i);
	nm_set_global("DamageNumber::m_font", r[0].i);
}

/* --- NM1 --- */
static void nm_damage_number(void)
{
	gk_enable();
	cn_gl_setup();
	nm_damage_globals();
	if (nm_failures)
		return;
	int dn = vm_construct_struct(nm_struct("DamageNumber"));	/* NEW DamageNumber 33807 */
	NM_CHECK(dn > 0 && da_live_page(dn), "NEW DamageNumber gave %d", dn);
	if (nm_failures)
		return;
	/* m_parts: [CNumeralParts object, vtable offset]; number_ is its member 1. */
	int np = heap[dn].page->values[0].i;
	NM_CHECK(da_live_page(np) && heap[np].page->index == nm_struct("parts::detail::CNumeralParts"),
		 "NM1 DamageNumber.m_parts is %d, no CNumeralParts", np);
	if (nm_failures)
		return;
	int no = heap[np].page->values[1].i;
	struct parts *p = parts_try_get(no);
	struct parts_numeral *num = nm_num(no);
	NM_CHECK(p && num, "NM1 the numeral's parts %d: %s", no, p ? "its normal state is no numeral" : "does not exist");
	if (nm_failures)
		return;
	NM_CHECK(PE_GetComponentType(no, 1) == 24, "NM1 component type %d, want 24 (數字部件)", PE_GetComponentType(no, 1));
	/* FUNC 33807: Z 100000, NumeralType 2, CharSpace -6, Font m_font,
	 * OriginPosMode 5, RotationZ 30. */
	NM_CHECK(num->show_type == 2 && num->space == -6, "NM1 表示タイプ %d 字間隔 %d, want 2 and -6", num->show_type, num->space);
	nm_expect_font(no, 257, 56, 255, 255, 255, 0.4f, 2.0f, 0, 0, 0, "NM1 DamageNumber::m_font");
	NM_CHECK(p->local.z == 100000 && p->origin_mode == 5 && p->local.rotation.z == 30.0f,
		 "NM1 Z %d origin %d rotation %g, want 100000, 5, 30", p->local.z, p->origin_mode, p->local.rotation.z);
	/* Before Show: the number 0 in the font: one half-width cell, 28 + 2e
	 * (e = max(ceil(0.4), ceil(2)) = 2) wide and 56 + 2e high. */
	nm_expect_size(no, 32, 60, "NM1 before Show (\"0\")");
	/* The accessors, by the game's own wrappers. */
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@NumeralType::get", np) == 2, "NM1 NumeralType::get is not 2");
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@CharSpace::get", np) == -6, "NM1 CharSpace::get is not -6");
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@Number::get", np) == 0, "NM1 Number::get is not 0 before Show");
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@Length::get", np) == 0, "NM1 Length::get is not 0");
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@ShowPadding::get", np) == 1, "NM1 ShowPadding::get is not true");
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@ShowComma::get", np) == 0, "NM1 ShowComma::get is not false");
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@Wide::get", np) == 0, "NM1 Wide::get is not false");
	{
		int s = nm_get_int("parts::detail::CNumeralParts@CGName::get", np);
		NM_CHECK(!strcmp(bu_str(s), ""), "NM1 CGName::get is \"%s\"", bu_str(s));
		if (s > 0) heap_unref(s);
	}
	{
		/* Font::get: GetNumeralFont's ten outputs into NEW CASFont. */
		union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
		int n = nm_call(nm_fn("parts::detail::CNumeralParts@Font::get"), np, 0, NULL, r);
		NM_CHECK(n == 1 && da_live_page(r[0].i), "NM1 Font::get returned %d slot(s)", n);
		if (n == 1 && da_live_page(r[0].i)) {
			union vm_value *f = heap[r[0].i].page->values;
			int color = f[2].i, edge = f[5].i;
			bool pages = da_live_page(color) && da_live_page(edge);
			NM_CHECK(f[0].i == 257 && f[1].i == 56 && f[3].f == 0.4f && f[4].f == 2.0f,
				 "NM1 Font::get: type %d size %d 太さ %g 縁取り %g, want 257, 56, 0.4, 2", f[0].i, f[1].i, f[3].f, f[4].f);
			NM_CHECK(pages && heap[color].page->values[0].i == 255 && heap[color].page->values[1].i == 255
				 && heap[color].page->values[2].i == 255 && heap[edge].page->values[0].i == 0
				 && heap[edge].page->values[1].i == 0 && heap[edge].page->values[2].i == 0,
				 "NM1 Font::get: the colours are not white and black");
			heap_unref(r[0].i);
		}
	}
	{
		union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
		int n = nm_call(nm_fn("parts::detail::CNumeralParts@GetSurfaceArea"), np, 0, NULL, r);
		NM_CHECK(n == 1 && da_live_page(r[0].i), "NM1 GetSurfaceArea returned %d slot(s)", n);
		if (n == 1 && da_live_page(r[0].i)) {
			union vm_value *v = heap[r[0].i].page->values;
			NM_CHECK(!v[0].i && !v[1].i && !v[2].i && !v[3].i, "NM1 surface area (%d,%d,%d,%d), want zeros",
				 v[0].i, v[1].i, v[2].i, v[3].i);
			heap_unref(r[0].i);
		}
	}
	/* Number::set, as DamageNumber@Show does first. */
	nm_call(nm_fn("parts::detail::CNumeralParts@Number::set"), np, 1, (union vm_value[]){ BU_I(42) }, NULL);
	NM_CHECK(nm_get_int("parts::detail::CNumeralParts@Number::get", np) == 42, "NM1 Number::get is not 42");
	/* "42": two cells of 32 and one 字間隔 of -6. */
	nm_expect_size(no, 58, 60, "NM1 \"42\"");
	NM_CHECK(num->common.origin_offset.x == -29 && num->common.origin_offset.y == -30,
		 "NM1 origin offset (%d,%d), want (-29,-30)", num->common.origin_offset.x, num->common.origin_offset.y);
	nm_call(nm_fn("parts::detail::CNumeralParts@Number::set"), np, 1, (union vm_value[]){ BU_I(105) }, NULL);
	nm_expect_size(no, 84, 60, "NM1 \"105\" (3 * 32 - 12)");
	heap_unref(dn);
	NM_CHECK(!parts_try_get(no), "NM1 releasing the DamageNumber left its parts %d", no);
	printf("NM1 DamageNumber by its bytecode: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM2 --- */
#define NM_DAMAGE_MOTION "[Time:200|Scale:5 1 EaseOutBack|Alpha:0 255][Time:700|Y:-208 Rel EaseInOutExp|Alpha:0 EaseOutCubic]"
static void nm_collection(void)
{
	gk_enable();
	cn_gl_setup();
	nm_damage_globals();
	if (nm_failures)
		return;
	/* The globals DamageNumber@Show reads, made by the game's own code:
	 * Motion::Instances::executer and DamageNumber::m_motionSet (the string
	 * is the alloc function's). */
	nm_set_global("Motion::Instances::executer", vm_construct_struct(nm_struct("Motion::ExecuterCollection")));
	{
		int a = sf_str(NM_DAMAGE_MOTION, -1);
		heap[a].ref |= HEAP_TEMP_FLAG;
		union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
		int n = nm_call(nm_fn("Motion::GetCompiled"), -1, 1, (union vm_value[]){ BU_I(a) }, r);
		NM_CHECK(n == 1 && da_live_page(r[0].i), "Motion::GetCompiled returned %d slot(s) [%d]", n, r[0].i);
		nm_set_global("DamageNumber::m_motionSet", r[0].i);
	}
	const int PARENT = 9301;
	parts_get(PARENT);
	PE_SetPos(PARENT, 100, 50);
	int col = vm_construct_struct(nm_struct("DamageNumberCollection"));
	NM_CHECK(col > 0 && da_live_page(col), "NEW DamageNumberCollection gave %d", col);
	if (nm_failures)
		return;
	/* <Parent> and Pos, as BattlePlayerView gives them. */
	heap[col].page->values[1].i = PARENT;
	int pos = heap[col].page->values[2].i;
	NM_CHECK(da_live_page(pos), "DamageNumberCollection.Pos is %d", pos);
	if (nm_failures)
		return;
	heap[pos].page->values[0].f = 300.0f;
	heap[pos].page->values[1].f = 200.0f;
	nm_call(nm_fn("DamageNumberCollection@Run"), col, 1, (union vm_value[]){ BU_I(42) }, NULL);
	int arr = heap[col].page->values[0].i;
	NM_CHECK(da_live_page(arr) && heap[arr].page->nr_vars == 1, "NM2 m_uiDamages holds %d numeral(s), want 1",
		 da_live_page(arr) ? heap[arr].page->nr_vars : -1);
	if (nm_failures)
		return;
	int dn = heap[arr].page->values[0].i;
	int np = da_live_page(dn) ? heap[dn].page->values[0].i : -1;
	int no = da_live_page(np) ? heap[np].page->values[1].i : -1;
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	struct parts_numeral *num = nm_num(no);
	NM_CHECK(p && num, "NM2 the damage numeral's parts %d is no numeral", no);
	if (nm_failures)
		return;
	NM_CHECK(num->num == 42 && num->show_type == 2 && num->space == -6, "NM2 number %d type %d 字間隔 %d, want 42, 2, -6",
		 num->num, num->show_type, num->space);
	nm_expect_font(no, 257, 56, 255, 255, 255, 0.4f, 2.0f, 0, 0, 0, "NM2");
	nm_expect_size(no, 58, 60, "NM2 \"42\"");
	NM_CHECK(p->parent && p->parent->no == PARENT, "NM2 parent %d, want %d", p->parent ? p->parent->no : -1, PARENT);
	NM_CHECK(p->local.pos.x == 300 && p->local.pos.y == 200, "NM2 position (%d,%d), want (300,200)",
		 p->local.pos.x, p->local.pos.y);
	/* The motion was created for the numeral's parts (EndMotion::get is
	 * what DamageNumberCollection@Run erases the finished numerals by). */
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	nm_call(nm_fn("Motion::IsExist"), -1, 1, (union vm_value[]){ BU_I(no) }, r);
	NM_CHECK(r[0].i == 1, "NM2 Motion::IsExist(%d) is %d after Show", no, r[0].i);
	NM_CHECK(nm_get_int("DamageNumber@EndMotion::get", dn) == 0, "NM2 EndMotion::get is true right after Show");
	printf("NM2 DamageNumberCollection@Run by its bytecode: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM3 --- */
#define NM_SENTINEL -987654
/* Whether a setter rebuilt the numeral: a rebuild writes the state's width. */
static void nm_arm(int no) { nm_num(no)->common.w = NM_SENTINEL; }
static bool nm_rebuilt(int no) { return nm_num(no)->common.w != NM_SENTINEL; }
/* SetNumeralNumber rebuilds whatever the number is (0x567460). */
static void nm_disarm(int no) { if (!nm_rebuilt(no)) PE_SetNumeralNumber(no, nm_num(no)->num, 1); }

static int nm_out_page(int n)
{
	int page = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n));
	for (int i = 0; i < n; i++)
		heap[page].page->values[i].i = NM_SENTINEL;
	return page;
}

/* GetNumeralFont's outputs: type, size, r, g, b, 太さ, er, eg, eb, 縁取り. */
static int nm_font_get(int no, int state)
{
	int pg = nm_out_page(10);
	NM_PE("GetNumeralFont", BU_I(no), BU_I(pg), BU_I(0), BU_I(pg), BU_I(1), BU_I(pg), BU_I(2), BU_I(pg), BU_I(3),
	      BU_I(pg), BU_I(4), BU_I(pg), BU_I(5), BU_I(pg), BU_I(6), BU_I(pg), BU_I(7), BU_I(pg), BU_I(8),
	      BU_I(pg), BU_I(9), BU_I(state));
	return pg;
}

static void nm_expect_font_get(int no, int type, int size, int r, int g, int b, float bold, int er, int eg, int eb,
		float edge, const char *what)
{
	int pg = nm_font_get(no, 1);
	union vm_value *v = heap[pg].page->values;
	NM_CHECK(v[0].i == type && v[1].i == size && v[2].i == r && v[3].i == g && v[4].i == b && v[5].f == bold
		 && v[6].i == er && v[7].i == eg && v[8].i == eb && v[9].f == edge,
		 "%s: GetNumeralFont %d/%d (%d,%d,%d) %g (%d,%d,%d) %g, want %d/%d (%d,%d,%d) %g (%d,%d,%d) %g", what,
		 v[0].i, v[1].i, v[2].i, v[3].i, v[4].i, v[5].f, v[6].i, v[7].i, v[8].i, v[9].f,
		 type, size, r, g, b, bold, er, eg, eb, edge);
	heap_unref(pg);
}

static void nm_expect_area(int no, int state, int x, int y, int w, int h, const char *what)
{
	int pg = nm_out_page(4);
	NM_PE("GetNumeralSurfaceArea", BU_I(no), BU_I(pg), BU_I(0), BU_I(pg), BU_I(1), BU_I(pg), BU_I(2), BU_I(pg), BU_I(3),
	      BU_I(state));
	union vm_value *v = heap[pg].page->values;
	NM_CHECK(v[0].i == x && v[1].i == y && v[2].i == w && v[3].i == h, "%s: surface area (%d,%d,%d,%d), want (%d,%d,%d,%d)",
		 what, v[0].i, v[1].i, v[2].i, v[3].i, x, y, w, h);
	heap_unref(pg);
}

static void nm_expect_widths(int no, int state, const int *want, const char *what)
{
	int pg = nm_out_page(12);
	union vm_value slots[26];
	int k = 0;
	slots[k++] = BU_I(no);
	for (int i = 0; i < 12; i++) {
		slots[k++] = BU_I(pg);
		slots[k++] = BU_I(i);
	}
	slots[k++] = BU_I(state);
	nm_pe_call("GetNumeralCGNumberWidthList", k, slots);
	union vm_value *v = heap[pg].page->values;
	bool same = true;
	for (int i = 0; i < 12; i++)
		same = same && v[i].i == want[i];
	NM_CHECK(same, "%s: widths %d %d %d %d %d %d %d %d %d %d %d %d", what, v[0].i, v[1].i, v[2].i, v[3].i, v[4].i,
		 v[5].i, v[6].i, v[7].i, v[8].i, v[9].i, v[10].i, v[11].i);
	heap_unref(pg);
}

static void nm_expect_cg_name(int no, int state, const char *want, const char *what)
{
	int s = NM_PE("Parts_GetNumeralCGName", BU_I(no), BU_I(state)).i;
	NM_CHECK(!strcmp(bu_str(s), want), "%s: CG name \"%s\", want \"%s\"", what, bu_str(s), want);
	if (s > 0)
		heap_unref(s);
}

static int nm_linked(int no, const char *name, const int *w, int state)
{
	int s = sf_str(name, -1);
	union vm_value slots[15];
	int k = 0;
	slots[k++] = BU_I(no);
	slots[k++] = BU_I(s);
	for (int i = 0; i < 12; i++)
		slots[k++] = BU_I(w[i]);
	slots[k++] = BU_I(state);
	int r = nm_pe_call("Parts_SetNumeralLinkedCGNumberWidthWidthList", k, slots).i;
	heap_unref(s);
	return r;
}

static int nm_set_cg(int no, const char *name, int state)
{
	int s = sf_str(name, -1);
	int r = NM_PE("Parts_SetNumeralCG", BU_I(no), BU_I(s), BU_I(state)).i;
	heap_unref(s);
	return r;
}

static void nm_hll(void)
{
	gk_enable();
	cn_gl_setup();
	static const int zeros[12] = { 0 }, ramp[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
	/* A component that does not exist (0x53e510 finds none). */
	const int MISSING = 9390;
	NM_CHECK(NM_PE("Parts_GetNumeralNumber", BU_I(MISSING), BU_I(1)).i == 0
		 && NM_PE("Parts_GetNumeralCGType", BU_I(MISSING), BU_I(1)).i == 0
		 && NM_PE("Parts_GetNumeralSpace", BU_I(MISSING), BU_I(1)).i == 0
		 && NM_PE("GetNumeralLength", BU_I(MISSING), BU_I(1)).i == 0
		 && NM_PE("Parts_IsNumeralShowComma", BU_I(MISSING), BU_I(1)).i == 0
		 && NM_PE("IsNumeralShowPadding", BU_I(MISSING), BU_I(1)).i == 0
		 && NM_PE("IsNumerarlFullPitch", BU_I(MISSING), BU_I(1)).i == 0,
		 "NM3 a getter of a missing component is not 0");
	nm_expect_cg_name(MISSING, 1, "", "NM3 missing component");
	{
		int pg = nm_font_get(MISSING, 1);
		bool untouched = true;
		for (int i = 0; i < 10; i++)
			untouched = untouched && heap[pg].page->values[i].i == NM_SENTINEL;
		NM_CHECK(untouched, "NM3 GetNumeralFont wrote its outputs for a missing component");
		heap_unref(pg);
		static const int keep[12] = { NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL,
			NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL };
		nm_expect_widths(MISSING, 1, keep, "NM3 missing component (outputs kept)");
		nm_expect_area(MISSING, 1, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, NM_SENTINEL, "NM3 missing component (outputs kept)");
	}
	NM_CHECK(!parts_try_get(MISSING), "NM3 a getter created the missing component");

	/* An existing component whose state is something else: a getter makes
	 * it a numeral with the constructor's values (0x5b77f0 -> 0x5b3e80). */
	const int N = 9391;
	struct parts *p = parts_get(N);
	p->component_type = 18;	/* a low-level widget, as AFL_Parts_CreateSprite makes */
	parts_get_cg(p, 0);
	NM_CHECK(NM_PE("Parts_GetNumeralCGType", BU_I(N), BU_I(1)).i == 0 && nm_num(N), "NM3 the getter did not make the state a numeral");
	if (!nm_num(N))
		return;
	NM_CHECK(PE_GetComponentType(N, 1) == 24, "NM3 the state's component type is %d, want 24 (數字部件)", PE_GetComponentType(N, 1));
	NM_CHECK(NM_PE("Parts_GetNumeralNumber", BU_I(N), BU_I(1)).i == 0 && NM_PE("Parts_GetNumeralSpace", BU_I(N), BU_I(1)).i == 0
		 && NM_PE("GetNumeralLength", BU_I(N), BU_I(1)).i == 0 && NM_PE("Parts_IsNumeralShowComma", BU_I(N), BU_I(1)).i == 0
		 && NM_PE("IsNumerarlFullPitch", BU_I(N), BU_I(1)).i == 0,
		 "NM3 defaults: number, 字間隔, 桁數, コンマ表示 or 全角 is not 0");
	NM_CHECK(NM_PE("IsNumeralShowPadding", BU_I(N), BU_I(1)).i == 1, "NM3 default ゼロパディング is not on (0x5b3f89)");
	nm_expect_font_get(N, 0, 16, 255, 255, 255, 0.0f, 0, 0, 0, 0.0f, "NM3 default font (0x69f1d0)");
	nm_expect_cg_name(N, 1, "", "NM3 default");
	nm_expect_widths(N, 1, zeros, "NM3 default");
	nm_expect_area(N, 1, 0, 0, 0, 0, "NM3 default");
	nm_expect_size(N, 0, 0, "NM3 default (表示タイプ 0 without a CG)");

	/* SetNumeralShowType: 0..2 only; nothing else changes. */
	nm_arm(N);
	NM_PE("SetNumeralShowType", BU_I(N), BU_I(2), BU_I(1));
	NM_CHECK(nm_rebuilt(N) && NM_PE("Parts_GetNumeralCGType", BU_I(N), BU_I(1)).i == 2, "NM3 SetNumeralShowType(2) did not make a font numeral");
	nm_expect_size(N, 8, 16, "NM3 \"0\" in the default font (one cell of 8)");
	nm_arm(N);
	NM_PE("SetNumeralShowType", BU_I(N), BU_I(2), BU_I(1));
	NM_CHECK(!nm_rebuilt(N), "NM3 SetNumeralShowType with the type it has rebuilt the numeral");
	NM_PE("SetNumeralShowType", BU_I(N), BU_I(3), BU_I(1));
	NM_PE("SetNumeralShowType", BU_I(N), BU_I(-1), BU_I(1));
	NM_CHECK(!nm_rebuilt(N) && NM_PE("Parts_GetNumeralCGType", BU_I(N), BU_I(1)).i == 2, "NM3 SetNumeralShowType(3 / -1) was not ignored");
	nm_disarm(N);

	/* SetNumeralNumber rebuilds whether the number changes or not (0x567460). */
	NM_CHECK(NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(1234), BU_I(1)).i == 1, "NM3 Parts_SetNumeralNumber returned false");
	NM_CHECK(NM_PE("Parts_GetNumeralNumber", BU_I(N), BU_I(1)).i == 1234, "NM3 Parts_GetNumeralNumber is not 1234");
	nm_expect_size(N, 32, 16, "NM3 \"1234\"");
	nm_arm(N);
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(1234), BU_I(1));
	NM_CHECK(nm_rebuilt(N), "NM3 Parts_SetNumeralNumber with the same number did not rebuild");

	/* SetNumeralFont: the font, 表示タイプ 2; nothing for an equal font. */
	NM_PE("SetNumeralFont", BU_I(N), BU_I(1), BU_I(20), BU_I(10), BU_I(20), BU_I(30), BU_F(0.3f), BU_I(40), BU_I(50), BU_I(60),
	      BU_F(1.5f), BU_I(1));
	nm_expect_font_get(N, 1, 20, 10, 20, 30, 0.3f, 40, 50, 60, 1.5f, "NM3 SetNumeralFont");
	nm_expect_font(N, 1, 20, 10, 20, 30, 0.3f, 1.5f, 40, 50, 60, "NM3 SetNumeralFont");
	nm_expect_size(N, 4 * 14, 24, "NM3 \"1234\" at 20 with 縁取り 1.5 (cells of 10 + 2 * 2)");
	nm_arm(N);
	NM_PE("SetNumeralFont", BU_I(N), BU_I(1), BU_I(20), BU_I(10), BU_I(20), BU_I(30), BU_F(0.3f), BU_I(40), BU_I(50), BU_I(60),
	      BU_F(1.5f), BU_I(1));
	NM_CHECK(!nm_rebuilt(N), "NM3 SetNumeralFont with the font it has rebuilt the numeral");
	NM_PE("SetNumeralFont", BU_I(N), BU_I(1), BU_I(20), BU_I(10), BU_I(20), BU_I(30), BU_F(0.3f), BU_I(40), BU_I(50), BU_I(61),
	      BU_F(1.5f), BU_I(1));
	NM_CHECK(nm_rebuilt(N), "NM3 SetNumeralFont with another edge colour did not rebuild");

	/* コンマ表示, 字間隔, 桁數, ゼロパディング, 全角: stored, read back,
	 * rebuilt on a change only. */
	static const struct { const char *set, *get; int value, other; } prop[] = {
		{ "Parts_SetNumeralShowComma", "Parts_IsNumeralShowComma", 1, 0 },
		{ "Parts_SetNumeralSpace", "Parts_GetNumeralSpace", -3, 5 },
		{ "Parts_SetNumeralLength", "GetNumeralLength", 6, 2 },
		{ "SetNumeralShowPadding", "IsNumeralShowPadding", 0, 1 },
		{ "SetNumeralFullPitch", "IsNumerarlFullPitch", 1, 0 },
	};
	for (int i = 0; i < 5; i++) {
		nm_arm(N);
		NM_PE(prop[i].set, BU_I(N), BU_I(prop[i].value), BU_I(1));
		NM_CHECK(nm_rebuilt(N) && NM_PE(prop[i].get, BU_I(N), BU_I(1)).i == prop[i].value, "NM3 %s(%d): %s is %d%s",
			 prop[i].set, prop[i].value, prop[i].get, NM_PE(prop[i].get, BU_I(N), BU_I(1)).i,
			 nm_rebuilt(N) ? "" : ", and the numeral was not rebuilt");
		nm_disarm(N);
		nm_arm(N);
		NM_PE(prop[i].set, BU_I(N), BU_I(prop[i].value), BU_I(1));
		NM_CHECK(!nm_rebuilt(N), "NM3 %s with the value it has rebuilt the numeral", prop[i].set);
		NM_PE(prop[i].set, BU_I(N), BU_I(prop[i].other), BU_I(1));
		NM_CHECK(nm_rebuilt(N) && NM_PE(prop[i].get, BU_I(N), BU_I(1)).i == prop[i].other, "NM3 %s(%d) was not taken",
			 prop[i].set, prop[i].other);
		nm_disarm(N);
	}
	/* 桁數 below 0 is 0 (0x5676e5). */
	NM_PE("Parts_SetNumeralLength", BU_I(N), BU_I(-5), BU_I(1));
	NM_CHECK(NM_PE("GetNumeralLength", BU_I(N), BU_I(1)).i == 0, "NM3 Parts_SetNumeralLength(-5): 桁數 is %d, want 0",
		 NM_PE("GetNumeralLength", BU_I(N), BU_I(1)).i);

	/* The surface area is kept as given and rebuilds nothing (0x567760). */
	nm_arm(N);
	NM_CHECK(NM_PE("Parts_SetNumeralSurfaceArea", BU_I(N), BU_I(2), BU_I(3), BU_I(10), BU_I(8), BU_I(1)).i == 1,
		 "NM3 Parts_SetNumeralSurfaceArea returned false");
	NM_CHECK(!nm_rebuilt(N), "NM3 Parts_SetNumeralSurfaceArea rebuilt the numeral");
	nm_disarm(N);
	nm_expect_area(N, 1, 2, 3, 10, 8, "NM3 surface area");
	NM_PE("Parts_SetNumeralSurfaceArea", BU_I(N), BU_I(0), BU_I(0), BU_I(0), BU_I(0), BU_I(1));
	nm_expect_area(N, 1, 0, 0, 0, 0, "NM3 surface area cleared");

	/* ＣＧ名 makes a CG numeral (type 0), the linked CG a type 1; both are
	 * kept and read back whether the CG exists or not. The font stays. */
	NM_CHECK(nm_linked(N, "probe-linked", ramp, 1) == 1, "NM3 SetNumeralLinked... returned false for a CG that does not exist");
	NM_CHECK(NM_PE("Parts_GetNumeralCGType", BU_I(N), BU_I(1)).i == 1, "NM3 the linked CG did not make 表示タイプ 1");
	nm_expect_cg_name(N, 1, "probe-linked", "NM3 linked CG");
	nm_expect_widths(N, 1, ramp, "NM3 linked CG");
	/* Four characters without pictures: only the three 字間隔 of 5. */
	nm_expect_size(N, 15, 0, "NM3 a linked CG that does not exist");
	nm_arm(N);
	nm_linked(N, "probe-linked", ramp, 1);
	NM_CHECK(!nm_rebuilt(N), "NM3 the same linked CG and widths rebuilt the numeral");
	nm_linked(N, "probe-linked", zeros, 1);
	NM_CHECK(nm_rebuilt(N), "NM3 other widths did not rebuild the numeral");
	nm_expect_widths(N, 1, zeros, "NM3 linked CG, other widths");
	NM_CHECK(nm_set_cg(N, "", 1) == 1 && NM_PE("Parts_GetNumeralCGType", BU_I(N), BU_I(1)).i == 0,
		 "NM3 Parts_SetNumeralCG did not make 表示タイプ 0");
	nm_expect_cg_name(N, 1, "", "NM3 Parts_SetNumeralCG(\"\")");
	nm_arm(N);
	nm_set_cg(N, "", 1);
	NM_CHECK(!nm_rebuilt(N), "NM3 Parts_SetNumeralCG with the name it has rebuilt the numeral");
	nm_disarm(N);
	NM_PE("SetNumeralShowType", BU_I(N), BU_I(2), BU_I(1));
	nm_expect_font_get(N, 1, 20, 10, 20, 30, 0.3f, 40, 50, 61, 1.5f, "NM3 the font after the CG types");
	nm_arm(N);
	nm_set_cg(N, "", 1);
	NM_CHECK(nm_rebuilt(N) && NM_PE("Parts_GetNumeralCGType", BU_I(N), BU_I(1)).i == 0,
		 "NM3 Parts_SetNumeralCG with the same name on a font numeral did not make it a CG numeral");

	/* The states are separate numerals; a state that is none is refused. */
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(77), BU_I(2));
	NM_CHECK(NM_PE("Parts_GetNumeralNumber", BU_I(N), BU_I(2)).i == 77 && NM_PE("Parts_GetNumeralNumber", BU_I(N), BU_I(1)).i == 1234,
		 "NM3 the on-cursor state's number is not its own");
	NM_CHECK(NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(5), BU_I(0)).i == 0 && NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(5), BU_I(4)).i == 0
		 && NM_PE("Parts_GetNumeralNumber", BU_I(N), BU_I(0)).i == 0 && NM_PE("Parts_GetNumeralNumber", BU_I(N), BU_I(4)).i == 0,
		 "NM3 state 0 or 4 was accepted");
	printf("NM3 PartsEngine numeral functions: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM4 --- */
/* parts_numeral_codes, which an engine before the fix does not have. */
static int nm_codes(struct parts_numeral *num, uint8_t *codes, bool *shown)
{
	int (*fn)(struct parts_numeral *, uint8_t *, bool *) =
		(int (*)(struct parts_numeral *, uint8_t *, bool *))dlsym(RTLD_DEFAULT, "parts_numeral_codes");
	if (!fn) {
		NM_CHECK(false, "this engine has no parts_numeral_codes");
		return 0;
	}
	return fn(num, codes, shown);
}

static void nm_characters(void)
{
	gk_enable();
	cn_gl_setup();
	/* shown: '.' for a character that is not drawn. */
	static const struct { int num, length; bool comma, pad; const char *chars, *shown; } c[] = {
		{ 0, 0, false, true, "0", "0" },
		{ 0, 0, false, false, "0", "0" },
		{ 0, 3, false, false, "000", "..0" },
		{ 7, 3, false, true, "007", "007" },
		{ 7, 3, false, false, "007", "..7" },
		{ 100, 0, false, false, "100", "100" },
		{ 1234, 0, true, true, "1,234", "1,234" },
		{ 1234567, 0, true, false, "1,234,567", "1,234,567" },
		{ 999999, 6, true, false, "999,999", "999,999" },
		{ 12, 6, true, true, "000,012", "000,012" },
		{ 12, 6, true, false, "000,012", ".....12" },
		{ 0, 4, true, false, "0,000", "....0" },
		{ -5, 3, false, false, "-005", "-005" },
		{ -5, 0, false, true, "-5", "-5" },
		{ -1234, 0, true, true, "-1,234", "-1,234" },
		{ 2147483647, 0, true, true, "2,147,483,647", "2,147,483,647" },
		{ -2147483647 - 1, 0, false, true, "-2147483648", "-2147483648" },
		{ 5, 40, false, true, "00000000000000000000000000000005", "00000000000000000000000000000005" },
	};
	for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
		struct parts_numeral num = { .num = c[i].num, .length = c[i].length, .show_comma = c[i].comma, .zero_pad = c[i].pad };
		uint8_t codes[64];
		bool shown[64];
		char got[65], vis[65];
		int n = nm_codes(&num, codes, shown);
		for (int k = 0; k < n && k < 64; k++) {
			got[k] = codes[k] < 10 ? '0' + codes[k] : codes[k] == 10 ? '-' : codes[k] == 11 ? ',' : '?';
			vis[k] = shown[k] ? got[k] : '.';
		}
		got[n < 64 ? n : 64] = vis[n < 64 ? n : 64] = '\0';
		NM_CHECK(!strcmp(got, c[i].chars) && !strcmp(vis, c[i].shown),
			 "NM4 %d 桁數 %d%s%s: \"%s\" shown \"%s\", want \"%s\" shown \"%s\"", c[i].num, c[i].length,
			 c[i].comma ? " コンマ" : "", c[i].pad ? " ゼロパディング" : "", got, vis, c[i].chars, c[i].shown);
	}
	printf("NM4 characters, padding, commas and sign: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM5 --- */
static void nm_font_numeral(int no, int size, float bold, float edge, int space, int number)
{
	parts_get(no);
	NM_PE("SetNumeralFont", BU_I(no), BU_I(0), BU_I(size), BU_I(255), BU_I(255), BU_I(255), BU_F(bold), BU_I(0), BU_I(0), BU_I(0),
	      BU_F(edge), BU_I(1));
	NM_PE("Parts_SetNumeralSpace", BU_I(no), BU_I(space), BU_I(1));
	NM_PE("Parts_SetNumeralNumber", BU_I(no), BU_I(number), BU_I(1));
}

static void nm_sizes(void)
{
	gk_enable();
	cn_gl_setup();
	const int A = 9401, B = 9402, C = 9403, D = 9404, E = 9405;
	nm_font_numeral(A, 20, 0, 0, -1, 999);
	nm_expect_size(A, 28, 20, "NM5 \"999\" at 20, 字間隔 -1 (3 * 10 - 2)");
	NM_PE("Parts_SetNumeralShowComma", BU_I(A), BU_I(1), BU_I(1));
	NM_PE("Parts_SetNumeralNumber", BU_I(A), BU_I(10000), BU_I(1));
	nm_expect_size(A, 55, 20, "NM5 \"10,000\" (6 * 10 - 5)");
	/* 桁數 pads with cells whether they are drawn or not. */
	nm_font_numeral(B, 19, 0, 2, 0, 5);
	nm_expect_size(B, 14, 23, "NM5 \"5\" at 19 with 縁取り 2");
	NM_PE("Parts_SetNumeralLength", BU_I(B), BU_I(3), BU_I(1));
	nm_expect_size(B, 42, 23, "NM5 \"005\"");
	NM_PE("SetNumeralShowPadding", BU_I(B), BU_I(0), BU_I(1));
	nm_expect_size(B, 42, 23, "NM5 \"  5\" (hidden zeros keep their cells)");
	PE_SetPartsOriginPosMode(B, 3);
	PE_SetPos(B, 300, 100);
	PE_UpdateComponent(0);
	NM_CHECK(PE_GetPartsUpperLeftPosX(B, 1) == 258 && PE_GetPartsUpperLeftPosY(B, 1) == 100,
		 "NM5 origin 3: upper left (%d,%d), want (258,100)", PE_GetPartsUpperLeftPosX(B, 1), PE_GetPartsUpperLeftPosY(B, 1));
	NM_PE("Parts_SetNumeralNumber", BU_I(B), BU_I(1234), BU_I(1));
	nm_expect_size(B, 56, 23, "NM5 \"1234\" with 桁數 3 (the number is not cut)");
	/* 全角: full-width cells. */
	nm_font_numeral(C, 20, 0, 0, 0, 12);
	NM_PE("SetNumeralFullPitch", BU_I(C), BU_I(1), BU_I(1));
	nm_expect_size(C, 40, 20, "NM5 全角 \"１２\" (2 * 20)");
	NM_PE("Parts_SetNumeralNumber", BU_I(C), BU_I(-3), BU_I(1));
	nm_expect_size(C, 40, 20, "NM5 全角 \"－３\"");
	NM_PE("SetNumeralFullPitch", BU_I(C), BU_I(0), BU_I(1));
	nm_expect_size(C, 20, 20, "NM5 \"-3\"");
	/* An odd size: the half-width cell is (size + 1) / 2, the height size + 2e. */
	nm_font_numeral(D, 27, 0.3f, 0, 0, 100);
	nm_expect_size(D, 3 * 16, 29, "NM5 \"100\" at 27 with 太さ 0.3 (cells of 14 + 2)");
	/* The origin offset is over the whole row (0x5b4380). */
	PE_SetPartsOriginPosMode(D, 5);
	NM_CHECK(nm_num(D)->common.origin_offset.x == -24 && nm_num(D)->common.origin_offset.y == -14,
		 "NM5 origin 5 of 48x29: offset (%d,%d), want (-24,-14)", nm_num(D)->common.origin_offset.x,
		 nm_num(D)->common.origin_offset.y);
	/* A row narrower than nothing draws nothing. */
	nm_font_numeral(E, 10, 0, 0, -20, 12);
	nm_expect_size(E, 0, 10, "NM5 \"12\" at 10 with 字間隔 -20");
	NM_PE("Parts_SetNumeralSpace", BU_I(E), BU_I(0), BU_I(1));
	nm_expect_size(E, 10, 10, "NM5 \"12\" at 10");
	printf("NM5 sizes of font numerals: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM6 --- */
/* The pixels of a numeral's texture: white body (r >= 128), dark edge
 * (r < 128), both at least half opaque. */
struct nm_ink { int body, edge; };
static struct nm_ink nm_ink_in(uint8_t *px, int tw, int x0, int x1, int y0, int y1)
{
	struct nm_ink ink = { 0, 0 };
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			const uint8_t *q = px + (y * tw + x) * 4;
			if (q[3] < 128)
				continue;
			if (q[0] >= 128) ink.body++; else ink.edge++;
		}
	}
	return ink;
}

static void nm_pixels(void)
{
	gk_enable();
	cn_gl_setup();
	ts_real_fonts();
	/* The damage numeral: size 56, 太さ 0.4, 縁取り 2, 字間隔 -6. */
	const int A = 9411, B = 9412, C = 9413, D = 9414;
	nm_font_numeral(A, 56, 0.4f, 2.0f, -6, 42);
	nm_expect_size(A, 58, 60, "NM6 \"42\"");
	struct parts_numeral *num = nm_num(A);
	if (nm_failures || !num || !num->common.texture.handle) {
		NM_CHECK(false, "NM6 the damage numeral has no texture");
		return;
	}
	uint8_t *px = gfx_get_pixels(&num->common.texture);
	struct nm_ink left = nm_ink_in(px, 58, 0, 26, 0, 60), right = nm_ink_in(px, 58, 32, 58, 0, 60);
	if (!left.body || !right.body)
		cn_env_fail("the real font drew no digit body");
	NM_CHECK(left.body > 100 && left.edge > 100 && right.body > 100 && right.edge > 100,
		 "NM6 \"42\": body/edge pixels %d/%d in the first cell and %d/%d in the second", left.body, left.edge,
		 right.body, right.edge);
	/* The glyph is at (e, e) of its cell: nothing of the body in the first
	 * two columns or rows. */
	struct nm_ink col = nm_ink_in(px, 58, 0, 2, 0, 60), row = nm_ink_in(px, 58, 0, 58, 0, 2);
	NM_CHECK(!col.body && !row.body, "NM6 \"42\": %d body pixels in the first two columns, %d in the first two rows",
		 col.body, row.body);
	free(px);

	/* Hidden leading zeros: their cells stay empty, the last digit is
	 * where it is with the zeros drawn. */
	nm_font_numeral(B, 24, 0, 1, 0, 7);
	NM_PE("Parts_SetNumeralLength", BU_I(B), BU_I(3), BU_I(1));
	nm_expect_size(B, 3 * 14, 26, "NM6 \"007\"");
	if (nm_failures)
		return;	/* the pixels below are read by these sizes */
	uint8_t *shown = gfx_get_pixels(&nm_num(B)->common.texture);
	NM_PE("SetNumeralShowPadding", BU_I(B), BU_I(0), BU_I(1));
	nm_expect_size(B, 3 * 14, 26, "NM6 \"  7\"");
	if (nm_failures) {
		free(shown);
		return;
	}
	uint8_t *hidden = gfx_get_pixels(&nm_num(B)->common.texture);
	struct nm_ink z_shown = nm_ink_in(shown, 42, 0, 28, 0, 26), z_hidden = nm_ink_in(hidden, 42, 0, 28, 0, 26);
	int transparent = 0;
	for (int y = 0; y < 26; y++)
		for (int x = 0; x < 28; x++)
			transparent += hidden[(y * 42 + x) * 4 + 3] == 0;
	NM_CHECK(z_shown.body > 20 && z_shown.edge > 20, "NM6 \"007\": the zeros have %d body and %d edge pixels", z_shown.body, z_shown.edge);
	NM_CHECK(!z_hidden.body && !z_hidden.edge && transparent == 28 * 26,
		 "NM6 \"  7\": the hidden zeros' cells hold %d body, %d edge pixels, %d of %d transparent", z_hidden.body,
		 z_hidden.edge, transparent, 28 * 26);
	bool same7 = true;
	for (int y = 0; y < 26 && same7; y++)
		same7 = !memcmp(shown + (y * 42 + 28) * 4, hidden + (y * 42 + 28) * 4, 14 * 4);
	NM_CHECK(same7, "NM6 the 7 is not drawn the same with and without the zeros");
	free(shown);
	free(hidden);

	/* A character over the one before it is drawn after it, body and edge
	 * (the sprites of 0x5b4df0 in their order): where the second 8's edge
	 * crosses the first 8's body the edge shows. C is the first 8 alone, D
	 * the second alone at the same place in a row of the same size. */
	nm_font_numeral(C, 40, 0, 3, -14, 88);
	nm_expect_size(C, 38, 46, "NM6 \"88\" at 40 with 縁取り 3, 字間隔 -14 (2 * 26 - 14)");
	nm_font_numeral(D, 40, 0, 3, 0, 8);
	nm_expect_size(D, 26, 46, "NM6 \"8\"");
	if (nm_failures)
		return;
	uint8_t *both = gfx_get_pixels(&nm_num(C)->common.texture), *one = gfx_get_pixels(&nm_num(D)->common.texture);
	int crossed = 0, edge_wins = 0;
	for (int y = 0; y < 46; y++) {
		for (int x = 12; x < 26; x++) {
			const uint8_t *first = one + (y * 26 + x) * 4;		/* the first 8 at x */
			const uint8_t *second = one + (y * 26 + (x - 12)) * 4;	/* the second 8, 12 further right */
			const uint8_t *got = both + (y * 38 + x) * 4;
			bool first_body = first[3] == 255 && first[0] >= 250;
			bool second_edge = second[3] == 255 && second[0] <= 5;
			if (first_body && second_edge) {
				crossed++;
				edge_wins += got[0] <= 5 && got[3] == 255;
			}
		}
	}
	if (!crossed)
		cn_env_fail("the two 8s do not cross");
	NM_CHECK(edge_wins == crossed, "NM6 \"88\": the second 8's edge shows on %d of the %d pixels where it crosses the first 8's body",
		 edge_wins, crossed);
	free(both);
	free(one);
	printf("NM6 drawn font numerals: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM7 --- */
/* A CG font made here (the probe has no CG archive): character i is
 * (4 + i) x 9 of (20 * i + 15, 0, 0), the minus sign 3 high, the comma 12,
 * and 5 has no picture. */
static int nm_cg_font(enum parts_numeral_font_type type)
{
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1, sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->type = type;
	font->cg_no = -77;
	for (int i = 0; i < 12; i++) {
		font->width[i] = i == 5 ? 0 : 4 + i;
		if (i == 5)
			continue;
		gfx_init_texture_rgba(&font->cg[i], 4 + i, i == 10 ? 3 : i == 11 ? 12 : 9, (SDL_Color){ 20 * i + 15, 0, 0, 255 });
	}
	return font_no;
}

/* The red of each column of the top row, -1 where it is transparent. */
static void nm_expect_row(int no, const int *want, int w, const char *what)
{
	struct parts_numeral *num = nm_num(no);
	if (!num || !num->common.texture.handle || num->common.texture.w != w) {
		NM_CHECK(false, "%s: no texture %d wide", what, w);
		return;
	}
	uint8_t *px = gfx_get_pixels(&num->common.texture);
	for (int x = 0; x < w; x++) {
		int got = px[x * 4 + 3] ? px[x * 4] : -1;
		if (got != want[x]) {
			NM_CHECK(false, "%s: column %d is %d, want %d", what, x, got, want[x]);
			break;
		}
	}
	free(px);
}

static void nm_cg_numerals(void)
{
	gk_enable();
	cn_gl_setup();
	const int N = 9421;
	struct parts *p = parts_get(N);
	struct parts_numeral *num = parts_get_numeral(p, 0);
	/* A linked CG numeral whose font is the one made here. */
	num->show_type = 1;	/* a linked CG numeral */
	num->font_no = nm_cg_font(PARTS_NUMERAL_FONT_COMBINED);
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(120), BU_I(1));
	nm_expect_size(N, 5 + 6 + 4, 9, "NM7 \"120\" (widths 5, 6, 4)");
	{
		int want[15], k = 0;
		for (int i = 0; i < 5; i++) want[k++] = 35;
		for (int i = 0; i < 6; i++) want[k++] = 55;
		for (int i = 0; i < 4; i++) want[k++] = 15;
		nm_expect_row(N, want, 15, "NM7 \"120\"");
	}
	NM_PE("Parts_SetNumeralSpace", BU_I(N), BU_I(2), BU_I(1));
	nm_expect_size(N, 19, 9, "NM7 \"120\" with 字間隔 2");
	{
		int want[19], k = 0;
		for (int i = 0; i < 5; i++) want[k++] = 35;
		want[k++] = -1; want[k++] = -1;
		for (int i = 0; i < 6; i++) want[k++] = 55;
		want[k++] = -1; want[k++] = -1;
		for (int i = 0; i < 4; i++) want[k++] = 15;
		nm_expect_row(N, want, 19, "NM7 \"120\" with 字間隔 2");
	}
	NM_PE("Parts_SetNumeralSpace", BU_I(N), BU_I(0), BU_I(1));
	/* The comma is the tallest; the minus sign comes first. */
	NM_PE("Parts_SetNumeralShowComma", BU_I(N), BU_I(1), BU_I(1));
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(-1000), BU_I(1));
	nm_expect_size(N, 14 + 5 + 15 + 4 * 3, 12, "NM7 \"-1,000\" (14 + 5 + 15 + 3 * 4 wide, the comma's 12 high)");
	{
		int want[46], k = 0;
		for (int i = 0; i < 14; i++) want[k++] = 215;
		for (int i = 0; i < 5; i++) want[k++] = 35;
		for (int i = 0; i < 15; i++) want[k++] = 235;
		for (int i = 0; i < 12; i++) want[k++] = 15;
		nm_expect_row(N, want, 46, "NM7 \"-1,000\"");
	}
	NM_PE("Parts_SetNumeralShowComma", BU_I(N), BU_I(0), BU_I(1));
	/* A character without a picture has no width; a row of none has no
	 * texture. */
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(15), BU_I(1));
	nm_expect_size(N, 5, 9, "NM7 \"15\" (the 5 has no picture)");
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(5), BU_I(1));
	nm_expect_size(N, 0, 0, "NM7 \"5\"");
	/* Hidden zeros keep their widths. */
	NM_PE("Parts_SetNumeralLength", BU_I(N), BU_I(3), BU_I(1));
	NM_PE("SetNumeralShowPadding", BU_I(N), BU_I(0), BU_I(1));
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(7), BU_I(1));
	nm_expect_size(N, 4 + 4 + 11, 9, "NM7 \"  7\"");
	{
		int want[19], k = 0;
		for (int i = 0; i < 8; i++) want[k++] = -1;
		for (int i = 0; i < 11; i++) want[k++] = 155;
		nm_expect_row(N, want, 19, "NM7 \"  7\"");
	}
	/* The number 0 is one character (the original's do-while, 0x5b5280). */
	NM_PE("Parts_SetNumeralLength", BU_I(N), BU_I(0), BU_I(1));
	NM_PE("Parts_SetNumeralNumber", BU_I(N), BU_I(0), BU_I(1));
	nm_expect_size(N, 4, 9, "NM7 \"0\"");
	printf("NM7 CG numerals: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM8 --- */
#define NM_CG_NAME "\xa3\xc3\xa3\xc7\xc3\xfb"						/* ＣＧ名 */
#define NM_WIDTHS "\xb7\xf9\xa5\xea\xa5\xb9\xa5\xc8"					/* 幅リスト */
#define NM_VALUE "\x94\xb5\xd6\xb5"							/* 數值 */
#define NM_COMMA "\xa5\xb3\xa5\xf3\xa5\xde\xb1\xed\xca\xbe"				/* コンマ表示 */
#define NM_FULL "\xc8\xab\xbd\xc7"							/* 全角 */
#define NM_LENGTH "\xe8\xec\x94\xb5"							/* 桁數 */
#define NM_PADDING "\xa5\xbc\xa5\xed\xa5\xd1\xa5\xc7\xa5\xa3\xa5\xf3\xa5\xb0"		/* ゼロパディング */
#define NM_FACE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xbf\xa5\xa4\xa5\xd7"		/* フォントタイプ */
#define NM_EDGE_COLOR "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea\xc9\xab"	/* フォント縁取り色 */

struct nm_state {
	int show_type;
	const char *cg;
	int widths[12];
	int face, size, color[3];
	float bold, edge;
	int edge_color[3];
	int full, area[4], value, comma, space, length, padding;
};

/* A 數字部件 with every key of the game's pactex, in the game's order. */
static struct ex_tree nm_pactex_numeral(const char *name, const struct lb_place *pl, const struct nm_state *st)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 17);
	struct ex_tree *s = &info.children[1];
	int k = 0;
	s->children[k++] = bu_sleaf(BU_TYPE, BU_NUMERAL);
	s->children[k++] = bu_ileaf(NM_FACE, st->face);
	s->children[k++] = bu_ileaf(TD_SIZE, st->size);
	s->children[k++] = lb_ilist(TS_FONT_COLOR, 3, st->color);
	s->children[k++] = lb_fleaf(LB_WEIGHT, st->bold);
	s->children[k++] = lb_fleaf(LB_EDGE, st->edge);
	s->children[k++] = lb_ilist(NM_EDGE_COLOR, 3, st->edge_color);
	s->children[k++] = bu_sleaf(NM_CG_NAME, st->cg);
	s->children[k++] = lb_ilist(CN_AREA, 4, st->area);
	s->children[k++] = bu_ileaf(LB_SHOW_TYPE, st->show_type);
	s->children[k++] = lb_ilist(NM_WIDTHS, 12, st->widths);
	s->children[k++] = bu_ileaf(NM_VALUE, st->value);
	s->children[k++] = bu_ileaf(NM_COMMA, st->comma);
	s->children[k++] = bu_ileaf(LB_SPACE, st->space);
	s->children[k++] = bu_ileaf(NM_FULL, st->full);
	s->children[k++] = bu_ileaf(NM_LENGTH, st->length);
	s->children[k++] = bu_ileaf(NM_PADDING, st->padding);
	assert(k == 17);
	return lb_node(name, pl, info, 0);
}

/* The same with only 部件タイプ and 表示タイプ. */
static struct ex_tree nm_pactex_bare(const char *name, const struct lb_place *pl, int show_type)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 2);
	info.children[1].children[0] = bu_sleaf(BU_TYPE, BU_NUMERAL);
	info.children[1].children[1] = bu_ileaf(LB_SHOW_TYPE, show_type);
	return lb_node(name, pl, info, 0);
}

static void nm_loader(void)
{
	gk_enable();
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("NumeralLoader", 6, &kids);
	struct lb_place pl = { 10, 20, 1 };
	/* MoneyView's Money: a font numeral, with the preview number the
	 * original shows until the script sets one. */
	static const struct nm_state font = { .show_type = 2, .cg = "", .face = 0, .size = 20, .color = { 10, 20, 30 },
		.bold = 0.3f, .edge = 1.5f, .edge_color = { 40, 50, 60 }, .full = 0, .value = 999999, .comma = 1, .space = -1,
		.length = 0, .padding = 1 };
	/* 桁數 2 with and without ゼロパディング, 全角. */
	static const struct nm_state padded = { .show_type = 2, .cg = "", .size = 20, .color = { 255, 255, 255 }, .value = 8,
		.length = 2, .padding = 1 };
	static const struct nm_state unpadded = { .show_type = 2, .cg = "", .size = 20, .color = { 255, 255, 255 }, .value = 8,
		.length = 2, .padding = 0, .full = 1, .area = { 1, 2, 30, 18 } };
	/* HpViewEnemy's Number: a linked CG numeral. The font keys are there
	 * too and are not read for it. */
	static const struct nm_state linked = { .show_type = 1, .cg = "probe-no-such-cg",
		.widths = { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 }, .size = 12, .color = { 255, 255, 255 },
		.value = 99999, .padding = 1 };
	static const struct nm_state separate = { .show_type = 0, .cg = "", .size = 12, .color = { 9, 9, 9 }, .value = 3,
		.padding = 1 };
	kids->children[0] = nm_pactex_numeral("font", &pl, &font);
	kids->children[1] = nm_pactex_numeral("padded", &pl, &padded);
	kids->children[2] = nm_pactex_numeral("unpadded", &pl, &unpadded);
	kids->children[3] = nm_pactex_numeral("linked", &pl, &linked);
	kids->children[4] = nm_pactex_numeral("separate", &pl, &separate);
	kids->children[5] = nm_pactex_bare("bare", &pl, 2);
	int act = ts_load("NumeralLoader", e);
	int n_font = ts_no(act, "font"), n_pad = ts_no(act, "padded"), n_unpad = ts_no(act, "unpadded"),
		n_linked = ts_no(act, "linked"), n_sep = ts_no(act, "separate"), n_bare = ts_no(act, "bare");
	heap_unref(act);
	nm_failures += ts_failures;
	if (nm_failures)
		return;
	PE_UpdateComponent(0);
	static const int zeros[12] = { 0 }, tens[12] = { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 };

	NM_CHECK(PE_GetComponentType(n_font, 1) == 24, "NM8 component type %d, want 24", PE_GetComponentType(n_font, 1));
	NM_CHECK(NM_PE("Parts_GetNumeralCGType", BU_I(n_font), BU_I(1)).i == 2, "NM8 font: 表示タイプ is not 2");
	nm_expect_font_get(n_font, 0, 20, 10, 20, 30, 0.3f, 40, 50, 60, 1.5f, "NM8 font");
	NM_CHECK(NM_PE("Parts_GetNumeralNumber", BU_I(n_font), BU_I(1)).i == 999999, "NM8 font: 數值 was not read (number %d)",
		 NM_PE("Parts_GetNumeralNumber", BU_I(n_font), BU_I(1)).i);
	NM_CHECK(NM_PE("Parts_IsNumeralShowComma", BU_I(n_font), BU_I(1)).i == 1 && NM_PE("Parts_GetNumeralSpace", BU_I(n_font), BU_I(1)).i == -1
		 && NM_PE("GetNumeralLength", BU_I(n_font), BU_I(1)).i == 0 && NM_PE("IsNumeralShowPadding", BU_I(n_font), BU_I(1)).i == 1
		 && NM_PE("IsNumerarlFullPitch", BU_I(n_font), BU_I(1)).i == 0, "NM8 font: コンマ表示, 字間隔, 桁數, ゼロパディング or 全角");
	/* "999,999": seven cells of 10 + 2 * 2 and six 字間隔 of -1. */
	nm_expect_size(n_font, 7 * 14 - 6, 24, "NM8 font \"999,999\"");

	NM_CHECK(NM_PE("GetNumeralLength", BU_I(n_pad), BU_I(1)).i == 2 && NM_PE("IsNumeralShowPadding", BU_I(n_pad), BU_I(1)).i == 1,
		 "NM8 padded: 桁數 or ゼロパディング");
	nm_expect_size(n_pad, 20, 20, "NM8 padded \"08\"");
	NM_CHECK(NM_PE("IsNumeralShowPadding", BU_I(n_unpad), BU_I(1)).i == 0 && NM_PE("IsNumerarlFullPitch", BU_I(n_unpad), BU_I(1)).i == 1,
		 "NM8 unpadded: ゼロパディング 0 or 全角 1 was not read");
	nm_expect_area(n_unpad, 1, 1, 2, 30, 18, "NM8 unpadded");
	{
		uint8_t codes[64];
		bool shown[64];
		int n = nm_codes(nm_num(n_unpad), codes, shown);
		NM_CHECK(n == 2 && !shown[0] && shown[1], "NM8 unpadded: %d characters, the first %s", n, n == 2 && shown[0] ? "shown" : "hidden");
	}

	NM_CHECK(NM_PE("Parts_GetNumeralCGType", BU_I(n_linked), BU_I(1)).i == 1, "NM8 linked: 表示タイプ is not 1");
	nm_expect_cg_name(n_linked, 1, "probe-no-such-cg", "NM8 linked");
	nm_expect_widths(n_linked, 1, tens, "NM8 linked");
	NM_CHECK(NM_PE("Parts_GetNumeralNumber", BU_I(n_linked), BU_I(1)).i == 99999, "NM8 linked: 數值 was not read");
	nm_expect_font_get(n_linked, 0, 16, 255, 255, 255, 0.0f, 0, 0, 0, 0.0f, "NM8 linked (the font keys are not read: 0x5b6954)");

	NM_CHECK(NM_PE("Parts_GetNumeralCGType", BU_I(n_sep), BU_I(1)).i == 0 && NM_PE("Parts_GetNumeralNumber", BU_I(n_sep), BU_I(1)).i == 3,
		 "NM8 separate: 表示タイプ or 數值");
	nm_expect_widths(n_sep, 1, zeros, "NM8 separate (幅リスト is not read: 0x5b6855)");
	nm_expect_size(n_sep, 0, 0, "NM8 separate without a ＣＧ名");

	/* Missing keys are 0 (0x5b6a5c, 0x5b6ec4, 0x5b6f52 pass 0 for the
	 * default): size 0, black, no padding. */
	NM_CHECK(NM_PE("Parts_GetNumeralCGType", BU_I(n_bare), BU_I(1)).i == 2, "NM8 bare: 表示タイプ is not 2");
	nm_expect_font_get(n_bare, 0, 0, 0, 0, 0, 0.0f, 0, 0, 0, 0.0f, "NM8 bare");
	NM_CHECK(NM_PE("IsNumeralShowPadding", BU_I(n_bare), BU_I(1)).i == 0 && NM_PE("GetNumeralLength", BU_I(n_bare), BU_I(1)).i == 0
		 && NM_PE("Parts_GetNumeralNumber", BU_I(n_bare), BU_I(1)).i == 0, "NM8 bare: ゼロパディング, 桁數 or 數值 is not 0");
	nm_expect_size(n_bare, 0, 0, "NM8 bare (size 0)");
	printf("NM8 pactex numerals: %s\n", nm_failures ? "FAIL" : "ok");
}

/* --- NM9 --- */
static void nm_guards(void)
{
	gk_enable();
	cn_gl_setup();
	/* AIN version 13: the upstream numeral. No number until one is set,
	 * 桁數 at least 1, digits from a CG font by number; the minus sign is
	 * drawn last (upstream's order), and 0 without 桁數 would be nothing. */
	ain->version = 13;
	const int V = 9431;
	struct parts *p = parts_get(V);
	struct parts_numeral *num = parts_get_numeral(p, 0);
	NM_CHECK(num->length == 1 && num->font_no == -1 && !num->have_num, "NM9 v13 defaults: 桁數 %d font %d have_num %d",
		 num->length, num->font_no, num->have_num);
	PE_SetNumeralNumber(V, 120, 1);
	NM_CHECK(!num->common.texture.handle && PE_GetPartsWidth(V, 1) == 0, "NM9 v13: a numeral without a CG font has a texture");
	num->font_no = nm_cg_font(PARTS_NUMERAL_FONT_SEPARATE);
	PE_SetNumeralNumber(V, 120, 1);
	NM_CHECK(PE_GetPartsWidth(V, 1) == 15 && PE_GetPartsHeight(V, 1) == 9, "NM9 v13 \"120\": %dx%d, want 15x9",
		 PE_GetPartsWidth(V, 1), PE_GetPartsHeight(V, 1));
	PE_SetNumeralNumber(V, -12, 1);
	{
		int want[25], k = 0;
		for (int i = 0; i < 5; i++) want[k++] = 35;
		for (int i = 0; i < 6; i++) want[k++] = 55;
		for (int i = 0; i < 14; i++) want[k++] = 215;
		NM_CHECK(PE_GetPartsWidth(V, 1) == 25, "NM9 v13 \"-12\": %d wide, want 25", PE_GetPartsWidth(V, 1));
		nm_expect_row(V, want, 25, "NM9 v13 \"-12\" (1, 2, then the minus sign)");
	}
	PE_SetNumeralLength(V, 0, 1);
	NM_CHECK(num->length == 1, "NM9 v13 SetNumeralLength(0): 桁數 %d, want 1", num->length);
	PE_SetNumeralSurfaceArea(V, -1, 0, 5, 5, 1);
	NM_CHECK(num->common.surface_area.w == 0, "NM9 v13: a surface area with a negative corner was taken");
	NM_CHECK(p->component_state_type[0] == 0, "NM9 v13: the state got a v14 component type");
	ain->version = 14;

	/* v14 off the CN grid (the SJIS rule), a font numeral from pactex as
	 * before the fix: as wide as the renderer's advances and (n - 1)
	 * 字間隔, ceil(size + 2 * 縁取り) high, 桁數 cells wide whether the
	 * padding shows or not. */
	gk_force_sjis();
	struct ex_tree *kids;
	struct ex *e = lb_activity("NumeralGuardSjis", 1, &kids);
	struct lb_place pl = { 10, 20, 1 };
	kids->children[0] = ts_numeral("s", &pl, 16, 0, 1.5f, 0);
	int act = ts_load("NumeralGuardSjis", e);
	const int S = ts_no(act, "s");
	heap_unref(act);
	nm_failures += ts_failures;
	NM_PE("Parts_SetNumeralNumber", BU_I(S), BU_I(12), BU_I(1));
	int w = PE_GetPartsWidth(S, 1);
	NM_CHECK(w > 0 && PE_GetPartsHeight(S, 1) == 19, "NM9 SJIS rule \"12\": %dx%d, want 19 high", w, PE_GetPartsHeight(S, 1));
	NM_PE("Parts_SetNumeralSpace", BU_I(S), BU_I(3), BU_I(1));
	NM_CHECK(PE_GetPartsWidth(S, 1) == w + 3, "NM9 SJIS rule 字間隔 3: %d wide, want %d", PE_GetPartsWidth(S, 1), w + 3);
	NM_PE("Parts_SetNumeralSpace", BU_I(S), BU_I(0), BU_I(1));
	NM_PE("Parts_SetNumeralLength", BU_I(S), BU_I(4), BU_I(1));
	NM_CHECK(PE_GetPartsWidth(S, 1) == 2 * w, "NM9 SJIS rule 桁數 4: %d wide, want %d", PE_GetPartsWidth(S, 1), 2 * w);
	printf("NM9 guards (v13, v14 off the grid): %s\n", nm_failures ? "FAIL" : "ok");
}

static int test_numeral(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "NM1", nm_damage_number }, { "NM2", nm_collection }, { "NM3", nm_hll }, { "NM4", nm_characters },
		{ "NM5", nm_sizes }, { "NM6", nm_pixels }, { "NM7", nm_cg_numerals }, { "NM8", nm_loader },
		{ "NM9", nm_guards },
	};
	const int n = sizeof cases / sizeof cases[0];
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		int r = cn_fork_case(cases[i].name, cases[i].fn, &nm_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("NUMERAL ENVIRONMENT FAILURE: %d case(s) could not run, %d of %d cases failed\n", env, failed, n);
		return 2;
	}
	printf("NUMERAL %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}
