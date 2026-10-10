/* v14 0x58bf80 / 0x58c020 / 0x58c0e0 all use the floating-point corner
 * transform at 0x534bb0. Included after the declaration-shape helpers. */
static void PE_v14_GetPartsUpperLeftPos(int number, float *x, float *y, int state)
{
	PE_GetPartsUpperLeftPosF(number, state, x, y);
}

static float PE_v14_GetPartsUpperLeftPosX(int number, int state)
{
	float x = 0;
	PE_GetPartsUpperLeftPosF(number, state, &x, NULL);
	return x;
}

static float PE_v14_GetPartsUpperLeftPosY(int number, int state)
{
	float y = 0;
	PE_GetPartsUpperLeftPosF(number, state, NULL, &y);
	return y;
}

static bool pe_upper_left_shape(const struct ain_hll_function *f, const char *shape)
{
	if (ain->version < 14 || !f || f->nr_arguments != (int)strlen(shape) - 1
	    || (f->nr_arguments && !f->arguments)
	    || !pe_v14_numeral_arg_shape(&f->return_type, shape[0]))
		return false;
	for (int i = 0; i < f->nr_arguments; i++) {
		if (!pe_v14_numeral_arg_shape(&f->arguments[i].type, shape[i + 1]))
			return false;
	}
	return true;
}

/* Scalar names already have legacy exports. Select only the known v14
 * float(int,int) declarations; other games keep their original binding. */
void *pe_upper_left_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (!pe_upper_left_shape(f, "fii"))
		return dflt;
	if (!strcmp(f->name, "Parts_GetPartsUpperLeftPosX"))
		return PE_v14_GetPartsUpperLeftPosX;
	if (!strcmp(f->name, "Parts_GetPartsUpperLeftPosY"))
		return PE_v14_GetPartsUpperLeftPosY;
	return dflt;
}

static void pe_v14_register_upper_left(int libno)
{
	struct ain_hll_function *f = get_fun(libno, "Parts_GetPartsUpperLeftPos");
	if (pe_upper_left_shape(f, "viFFi"))
		static_library_register(&lib_PartsEngine, "Parts_GetPartsUpperLeftPos",
				PE_v14_GetPartsUpperLeftPos);
}
