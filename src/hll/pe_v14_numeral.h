/* v14 numeral (數字部件) accessors. Included by PartsEngine.c. */

/* The PartsEngine functions at 0x599270..0x5999a0 look the component up
 * (0x53e510) and do nothing for a missing one (0x599540: je to the end): a
 * setter does not make it, and a getter returns 0, false or the empty string
 * and leaves its outputs alone. For an existing component
 * the getters, like the setters, first make the state a numeral (0x5b77f0
 * with type 24): a state of another type is replaced by a default numeral. */
static struct parts_numeral *pe_v14_numeral_get(int number, int state)
{
	if (state < 1 || state > PARTS_NR_STATES)
		return NULL;
	struct parts *parts = parts_try_get(number);
	if (!parts)
		return NULL;
	bool changed = parts->states[state - 1].type != PARTS_NUMERAL;
	struct parts_numeral *num = parts_get_numeral(parts, state - 1);
	if (changed)
		parts_dirty(parts);
	return num;
}

// 0x599540 -> 0x5672b0
static void PE_v14_SetNumeralFont(int number, int type, int size, int r, int g, int b,
		float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight,
		int state)
{
	if (!parts_try_get(number))
		return;
	PE_SetNumeralFont(number, type, size, r, g, b, bold_weight, edge_r, edge_g, edge_b,
			edge_weight, state);
}

/* 0x5995e0 -> 0x567310. wrap<int> and wrap<float> are value references:
 * ffi.c resolves the two VM slots (page, variable) to a pointer. */
static void PE_v14_GetNumeralFont(int number, int *type, int *size, int *r, int *g, int *b,
		float *bold_weight, int *edge_r, int *edge_g, int *edge_b, float *edge_weight,
		int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	if (!num)
		return;
	struct text_style *ts = &num->font;
	if (type) *type = ts->face;
	if (size) *size = lroundf(ts->size);
	if (r) *r = ts->color.r;
	if (g) *g = ts->color.g;
	if (b) *b = ts->color.b;
	if (bold_weight) *bold_weight = ts->bold_weight;
	if (edge_r) *edge_r = ts->edge_color.r;
	if (edge_g) *edge_g = ts->edge_color.g;
	if (edge_b) *edge_b = ts->edge_color.b;
	if (edge_weight) *edge_weight = text_style_edge_width(ts);
}

// 0x5996b0 -> 0x5673a0
static void PE_v14_SetNumeralFullPitch(int number, bool full, int state)
{
	if (!parts_try_get(number))
		return;
	PE_SetNumeralFullPitch(number, full, state);
}

// 0x5996e0 -> 0x567410
static bool PE_v14_IsNumeralFullPitch(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return num && num->full_pitch;
}

// 0x599740 -> 0x567510
static int PE_v14_GetNumeralNumber(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return num ? num->num : 0;
}

// 0x5997a0 -> 0x5674c0
static bool PE_v14_IsNumeralShowComma(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return num && num->show_comma;
}

// 0x599800 -> 0x567640
static int PE_v14_GetNumeralSpace(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return num ? num->space : 0;
}

// 0x599860 -> 0x567710
static int PE_v14_GetNumeralLength(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return num ? num->length : 0;
}

// 0x5998d0 -> 0x567800: the rectangle as it was set.
static void PE_v14_GetNumeralSurfaceArea(int number, int *x, int *y, int *w, int *h, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	if (!num)
		return;
	if (x) *x = num->common.surface_area.x;
	if (y) *y = num->common.surface_area.y;
	if (w) *w = num->common.surface_area.w;
	if (h) *h = num->common.surface_area.h;
}

// 0x599910 -> 0x567880
static void PE_v14_SetNumeralShowType(int number, int type, int state)
{
	if (!parts_try_get(number))
		return;
	PE_SetNumeralShowType(number, type, state);
}

// 0x599940 -> 0x5678f0
static int PE_v14_GetNumeralCGType(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return num ? num->show_type : 0;
}

// 0x599970 -> 0x567940
static void PE_v14_SetNumeralShowPadding(int number, bool show, int state)
{
	if (!parts_try_get(number))
		return;
	PE_SetNumeralShowPadding(number, show, state);
}

// 0x5999a0 -> 0x5679b0
static bool PE_v14_IsNumeralShowPadding(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return num && num->zero_pad;
}

// 0x5992a0 -> 0x5671e0
static struct string *PE_v14_GetNumeralCGName(int number, int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	return string_ref(num && num->cg_name ? num->cg_name : &EMPTY_STRING);
}

// 0x599440 -> 0x567250: the twelve widths as they were set (0 by default).
static void PE_v14_GetNumeralCGNumberWidthList(int number, int *w0, int *w1, int *w2, int *w3,
		int *w4, int *w5, int *w6, int *w7, int *w8, int *w9, int *w_minus, int *w_comma,
		int state)
{
	struct parts_numeral *num = pe_v14_numeral_get(number, state);
	if (!num)
		return;
	int *out[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	for (int i = 0; i < 12; i++) {
		if (out[i])
			*out[i] = num->cg_widths[i];
	}
}

/* One letter per type: v void, i int, b bool, f float, s string, I wrap<int>,
 * F wrap<float>. */
static bool pe_v14_numeral_arg_shape(const struct ain_type *type, char shape)
{
	enum ain_data_type data = AIN_VOID, inner = AIN_VOID;
	switch (shape) {
	case 'v': data = AIN_VOID; break;
	case 'i': data = AIN_INT; break;
	case 'b': data = AIN_BOOL; break;
	case 'f': data = AIN_FLOAT; break;
	case 's': data = AIN_STRING; break;
	case 'I': data = AIN_WRAP; inner = AIN_INT; break;
	case 'F': data = AIN_WRAP; inner = AIN_FLOAT; break;
	default: return false;
	}
	if (type->data != data)
		return false;
	if (data == AIN_WRAP)
		return type->array_type && type->array_type->data == inner
			&& !type->array_type->array_type;
	return !type->array_type;
}

/* Register before pe_v14_prelink.h fills still-empty entries with stubs.
 * A declaration of another shape keeps the binding it has, rather than
 * getting a C function with an incompatible CIF. */
static void pe_v14_register_numerals(int libno)
{
	if (ain->version < 14)
		return;
	static const struct {
		const char *name;
		void *fun;
		const char *shape;	// the result, then the arguments
	} bindings[] = {
		{ "SetNumeralFont", PE_v14_SetNumeralFont, "viiiiiifiiifi" },
		{ "GetNumeralFont", PE_v14_GetNumeralFont, "viIIIIIFIIIFi" },
		{ "SetNumeralFullPitch", PE_v14_SetNumeralFullPitch, "vibi" },
		{ "IsNumerarlFullPitch", PE_v14_IsNumeralFullPitch, "bii" },
		{ "Parts_GetNumeralNumber", PE_v14_GetNumeralNumber, "iii" },
		{ "Parts_IsNumeralShowComma", PE_v14_IsNumeralShowComma, "bii" },
		{ "Parts_GetNumeralSpace", PE_v14_GetNumeralSpace, "iii" },
		{ "GetNumeralLength", PE_v14_GetNumeralLength, "iii" },
		{ "GetNumeralSurfaceArea", PE_v14_GetNumeralSurfaceArea, "viIIIIi" },
		{ "SetNumeralShowType", PE_v14_SetNumeralShowType, "viii" },
		{ "Parts_GetNumeralCGType", PE_v14_GetNumeralCGType, "iii" },
		{ "SetNumeralShowPadding", PE_v14_SetNumeralShowPadding, "vibi" },
		{ "IsNumeralShowPadding", PE_v14_IsNumeralShowPadding, "bii" },
		{ "Parts_GetNumeralCGName", PE_v14_GetNumeralCGName, "sii" },
		{ "GetNumeralCGNumberWidthList", PE_v14_GetNumeralCGNumberWidthList, "viIIIIIIIIIIIIi" },
	};
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
		const char *shape = bindings[i].shape;
		struct ain_hll_function *fun = get_fun(libno, bindings[i].name);
		if (!fun || fun->nr_arguments != (int)strlen(shape) - 1
				|| (fun->nr_arguments && !fun->arguments)
				|| !pe_v14_numeral_arg_shape(&fun->return_type, shape[0]))
			continue;
		bool match = true;
		for (int j = 0; j < fun->nr_arguments; j++) {
			if (!pe_v14_numeral_arg_shape(&fun->arguments[j].type, shape[j + 1])) {
				match = false;
				break;
			}
		}
		if (match)
			static_library_register(&lib_PartsEngine, bindings[i].name, bindings[i].fun);
	}
}
