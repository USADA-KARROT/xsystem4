/* v14 sound names and the on-cursor show link (Dohna Dohna): included by
 * PartsEngine.c after pe_v14_numeral.h (pe_v14_numeral_arg_shape).
 *
 * Parts_SetSoundName (HLL 195, 0x590230), Parts_GetSoundName (196, 0x5902e0)
 * and Parts_PlaySound (854, 0x59b220) are PE_SetSoundName, PE_GetSoundName
 * and PE_PlaySound (src/parts/input.c); Parts_GetOnCursorShowLinkPartsNumber
 * (152, 0x58e770) is PE_v14_GetOnCursorShowLinkPartsNumber, 0 for no link,
 * which the engines before v14 do not share (their
 * PE_GetOnCursorShowLinkPartsNumber gives -1). The Set of the link is bound
 * by the export table (PE_SetOnCursorShowLinkPartsNumber, which has the v14
 * behaviour for a program of v14 and later).
 *
 * Registered before pe_v14_prelink.h fills the still-empty entries with
 * stubs, and only under the declaration the game has: a declaration of
 * another shape keeps the binding it has. */
static void pe_v14_register_sound_names(int libno)
{
	if (ain->version < 14)
		return;
	static const struct {
		const char *name;
		void *fun;
		const char *shape;	// the result, then the arguments
	} bindings[] = {
		{ "Parts_SetSoundName", PE_SetSoundName, "visi" },
		{ "Parts_GetSoundName", PE_GetSoundName, "sii" },
		{ "Parts_PlaySound", PE_PlaySound, "vii" },
		{ "Parts_GetOnCursorShowLinkPartsNumber", PE_v14_GetOnCursorShowLinkPartsNumber, "ii" },
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
