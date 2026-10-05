/* v14 low-level gauge accessors. Included by PartsEngine.c. */

/* Native 0x598e20/0x598e90 and 0x566cc0: a missing component returns
 * the empty value, but an existing component creates the requested gauge
 * state through 0x5b77f0. Other states retain their type and contents. */
static struct parts_gauge *pe_v14_gauge_get(int number, int state, bool vertical)
{
	if (state < 1 || state > PARTS_NR_STATES)
		return NULL;
	struct parts *parts = parts_try_get(number);
	if (!parts)
		return NULL;
	int index = state - 1;
	enum parts_type type = vertical ? PARTS_VGAUGE : PARTS_HGAUGE;
	int native_type = vertical ? 23 : 22;
	bool changed = parts->states[index].type != type
		|| parts->component_state_type[index] != native_type;
	struct parts_gauge *g = vertical ? parts_get_vgauge(parts, index)
		: parts_get_hgauge(parts, index);
	parts->component_state_type[index] = native_type;
	if (changed)
		parts_dirty(parts);
	return g;
}

static struct string *pe_v14_gauge_cg(int number, int state, bool vertical)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, vertical);
	return string_ref(g && g->cg_name ? g->cg_name : &EMPTY_STRING);
}

static void pe_v14_gauge_reverse(int number, bool enable, int state, bool vertical)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, vertical);
	if (g && g->reverse != enable) {
		g->reverse = enable;
		parts_dirty(parts_try_get(number));
	}
}

/* wrap<int> is a value reference: ffi.c resolves the two VM slots
 * (page, variable) to int*. The one-slot wrap<array/string> ABI does not
 * apply here. Native 0x598fa0 leaves outputs untouched for missing parts;
 * 0x566db0 returns the raw rectangle, before render-time clipping. */
static void pe_v14_gauge_surface(int number, int *x, int *y, int *w, int *h,
		int state, bool vertical)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, vertical);
	if (!g)
		return;
	if (x) *x = g->common.surface_area.x;
	if (y) *y = g->common.surface_area.y;
	if (w) *w = g->common.surface_area.w;
	if (h) *h = g->common.surface_area.h;
}

static struct string *PE_v14_GetHGaugeCG(int number, int state)
{
	return pe_v14_gauge_cg(number, state, false);
}

static struct string *PE_v14_GetVGaugeCG(int number, int state)
{
	return pe_v14_gauge_cg(number, state, true);
}

static float PE_v14_GetHGaugeNumerator(int number, int state)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, false);
	return g ? g->numerator : 0.0f;
}

static float PE_v14_GetHGaugeDenominator(int number, int state)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, false);
	return g ? g->denominator : 0.0f;
}

static float PE_v14_GetVGaugeNumerator(int number, int state)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, true);
	return g ? g->numerator : 0.0f;
}

static float PE_v14_GetVGaugeDenominator(int number, int state)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, true);
	return g ? g->denominator : 0.0f;
}

static void PE_v14_SetHGaugeReverse(int number, bool enable, int state)
{
	pe_v14_gauge_reverse(number, enable, state, false);
}

static void PE_v14_SetVGaugeReverse(int number, bool enable, int state)
{
	pe_v14_gauge_reverse(number, enable, state, true);
}

static bool PE_v14_IsHGaugeReverse(int number, int state)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, false);
	return g && g->reverse;
}

static bool PE_v14_IsVGaugeReverse(int number, int state)
{
	struct parts_gauge *g = pe_v14_gauge_get(number, state, true);
	return g && g->reverse;
}

static void PE_v14_GetHGaugeSurfaceArea(int number, int *x, int *y, int *w, int *h,
		int state)
{
	pe_v14_gauge_surface(number, x, y, w, h, state, false);
}

static void PE_v14_GetVGaugeSurfaceArea(int number, int *x, int *y, int *w, int *h,
		int state)
{
	pe_v14_gauge_surface(number, x, y, w, h, state, true);
}

static bool pe_v14_gauge_arg_shape(const struct ain_type *type, enum ain_data_type data)
{
	if (type->data != data)
		return false;
	if (data == AIN_WRAP)
		return type->array_type && type->array_type->data == AIN_INT
			&& !type->array_type->array_type;
	return !type->array_type;
}

/* Register before pe_v14_prelink.h fills still-empty entries with stubs.
 * Unknown declarations retain those bindings, rather than receiving a C
 * function with an incompatible CIF. */
static void pe_v14_register_gauges(int libno)
{
	if (ain->version < 14)
		return;
	static const struct {
		const char *name;
		void *fun;
		enum ain_data_type result;
		int nr_arguments;
		enum ain_data_type arguments[6];
	} bindings[] = {
		{ "Parts_GetHGaugeCG", PE_v14_GetHGaugeCG, AIN_STRING, 2, { AIN_INT, AIN_INT } },
		{ "Parts_GetVGaugeCG", PE_v14_GetVGaugeCG, AIN_STRING, 2, { AIN_INT, AIN_INT } },
		{ "Parts_GetHGaugeNumerator", PE_v14_GetHGaugeNumerator, AIN_FLOAT, 2, { AIN_INT, AIN_INT } },
		{ "Parts_GetHGaugeDenominator", PE_v14_GetHGaugeDenominator, AIN_FLOAT, 2, { AIN_INT, AIN_INT } },
		{ "Parts_GetVGaugeNumerator", PE_v14_GetVGaugeNumerator, AIN_FLOAT, 2, { AIN_INT, AIN_INT } },
		{ "Parts_GetVGaugeDenominator", PE_v14_GetVGaugeDenominator, AIN_FLOAT, 2, { AIN_INT, AIN_INT } },
		{ "Parts_SetHGaugeReverse", PE_v14_SetHGaugeReverse, AIN_VOID, 3, { AIN_INT, AIN_BOOL, AIN_INT } },
		{ "Parts_SetVGaugeReverse", PE_v14_SetVGaugeReverse, AIN_VOID, 3, { AIN_INT, AIN_BOOL, AIN_INT } },
		{ "Parts_IsHGaugeReverse", PE_v14_IsHGaugeReverse, AIN_BOOL, 2, { AIN_INT, AIN_INT } },
		{ "Parts_IsVGaugeReverse", PE_v14_IsVGaugeReverse, AIN_BOOL, 2, { AIN_INT, AIN_INT } },
		{ "GetHGaugeSurfaceArea", PE_v14_GetHGaugeSurfaceArea, AIN_VOID, 6,
			{ AIN_INT, AIN_WRAP, AIN_WRAP, AIN_WRAP, AIN_WRAP, AIN_INT } },
		{ "GetVGaugeSurfaceArea", PE_v14_GetVGaugeSurfaceArea, AIN_VOID, 6,
			{ AIN_INT, AIN_WRAP, AIN_WRAP, AIN_WRAP, AIN_WRAP, AIN_INT } },
	};
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
		struct ain_hll_function *fun = get_fun(libno, bindings[i].name);
		if (!fun || !fun->arguments || fun->nr_arguments != bindings[i].nr_arguments
				|| !pe_v14_gauge_arg_shape(&fun->return_type, bindings[i].result))
			continue;
		bool match = true;
		for (int j = 0; j < fun->nr_arguments; j++) {
			if (!pe_v14_gauge_arg_shape(&fun->arguments[j].type, bindings[i].arguments[j])) {
				match = false;
				break;
			}
		}
		if (match)
			static_library_register(&lib_PartsEngine, bindings[i].name, bindings[i].fun);
	}
}
