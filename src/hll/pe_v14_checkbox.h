/* v14 functions the settings pages (config::detail::CConfigView), the system
 * menu and the dialogs call on their check boxes and sliders: a check box's
 * value and enable flag, a 橫滾動條's rate and position. The settings write
 * the values and the rates from the configuration before each update and
 * read them back into the configuration after it, so a getter that returned
 * nothing cleared the configuration of the page that was open. Included by
 * PartsEngine.c after pe_v14_gauge.h; registered before the stubs of
 * pe_v14_prelink.h, which then leave these names alone. */

/* The check box widget with that number (0x53da40). The component is looked
 * up as 0x53d920 does it (-> 0x53a0b0 -> 0x539c80, 0x578ee0): a number of 0
 * or less has none, and the functions below do nothing or return false; any
 * other number without a component gets one. A component of another type, a
 * new one included, then becomes a check box with the constructor's values
 * (0x53da8b: 0x535e20 with type 1; parts_widget_type_changed). */
static struct parts *pe_v14_checkbox(int number)
{
	if (number <= 0)
		return NULL;
	struct parts *parts = parts_get(number);
	if (parts->component_type != PARTS_COMPONENT_CHECKBOX)
		PE_SetComponentType(number, PARTS_COMPONENT_CHECKBOX, 1);
	return parts;
}

/* void SetCheckBoxEnable(int Number, bool Enable): case 235 -> 0x591120. */
static void PE_v14_SetCheckBoxEnable(int number, bool enable)
{
	struct parts *parts = pe_v14_checkbox(number);
	if (parts)
		parts_checkbox_set_enable(parts, enable);
}

/* bool IsCheckBoxEnable(int Number): case 236 -> 0x591150 (+0xf0). */
static bool PE_v14_IsCheckBoxEnable(int number)
{
	struct parts *parts = pe_v14_checkbox(number);
	return parts && parts_get_checkbox(parts)->enable;
}

/* void CheckBoxChecked(int Number, bool Check): case 237 -> 0x591170 ->
 * 0x52f030 (parts_checkbox_set_checked). */
static void PE_v14_CheckBoxChecked(int number, bool check)
{
	struct parts *parts = pe_v14_checkbox(number);
	if (parts)
		parts_checkbox_set_checked(parts, check);
}

/* bool IsCheckBoxChecked(int Number): case 238 -> 0x5911a0 (+0xf1). */
static bool PE_v14_IsCheckBoxChecked(int number)
{
	struct parts *parts = pe_v14_checkbox(number);
	return parts && parts_get_checkbox(parts)->checked;
}

/* The 橫滾動條 widget with that number (0x53dba0): looked up like the check
 * box, and a component of another type becomes a horizontal scroll bar with
 * the constructor's amounts (0x536660: 0x536540 with type 3; 100 by 1,
 * parts_scrollbar_construct). The 豎滾動條 functions of pe_v14_backlog.h take
 * their widget from 0x53daf0, which does the same with type 2; they still
 * keep to a parts that is a bar already. */
static struct parts *pe_v14_hscrollbar(int number)
{
	if (number <= 0)
		return NULL;
	struct parts *parts = parts_get(number);
	if (parts->component_type != PARTS_COMPONENT_HSCROLLBAR)
		PE_SetComponentType(number, PARTS_COMPONENT_HSCROLLBAR, 1);
	return parts;
}

/* void SetHScrollbarScrollRate(int Number, float Rate): case 314 -> 0x5923e0
 * -> 0x542400. */
static void PE_v14_SetHScrollbarScrollRate(int number, float rate)
{
	struct parts *parts = pe_v14_hscrollbar(number);
	if (parts)
		parts_scrollbar_set_rate(parts, rate);
}

/* float GetHScrollbarScrollRate(int Number): case 319 -> 0x5924d0 (+0x11c). */
static float PE_v14_GetHScrollbarScrollRate(int number)
{
	struct parts *parts = pe_v14_hscrollbar(number);
	return parts ? parts->scrollbar.rate : 0.0f;
}

/* void SetHScrollbarScrollPos(int Number, int Pos): case 313 -> 0x5923b0 ->
 * 0x542360, the setter of the 豎滾動條. The sound page's three voice sliders
 * are placed with it, at a volume, when the page is made and by their 初始化
 * buttons (UserConfig::SetSoundEvent); the volume itself is set by the event
 * the bar then sends (parts_scrollbar_update). */
static void PE_v14_SetHScrollbarScrollPos(int number, int pos)
{
	struct parts *parts = pe_v14_hscrollbar(number);
	if (parts)
		parts_scrollbar_set_pos(parts, pos);
}

/* int GetHScrollbarScrollPos(int Number): case 318 -> 0x5924b0 (+0x118). */
static int PE_v14_GetHScrollbarScrollPos(int number)
{
	struct parts *parts = pe_v14_hscrollbar(number);
	return parts ? parts->scrollbar.pos : 0;
}

/* Only the declarations these were written for get them; any other keeps
 * the binding it has (a stub), rather than a C function of another shape. */
static void pe_v14_register_checkbox(int libno)
{
	if (ain->version < 14)
		return;
	static const struct {
		const char *name;
		void *fun;
		enum ain_data_type result;
		int nr_arguments;
		enum ain_data_type arguments[2];
	} bindings[] = {
		{ "SetCheckBoxEnable", PE_v14_SetCheckBoxEnable, AIN_VOID, 2, { AIN_INT, AIN_BOOL } },
		{ "IsCheckBoxEnable", PE_v14_IsCheckBoxEnable, AIN_BOOL, 1, { AIN_INT } },
		{ "CheckBoxChecked", PE_v14_CheckBoxChecked, AIN_VOID, 2, { AIN_INT, AIN_BOOL } },
		{ "IsCheckBoxChecked", PE_v14_IsCheckBoxChecked, AIN_BOOL, 1, { AIN_INT } },
		{ "SetHScrollbarScrollRate", PE_v14_SetHScrollbarScrollRate, AIN_VOID, 2, { AIN_INT, AIN_FLOAT } },
		{ "GetHScrollbarScrollRate", PE_v14_GetHScrollbarScrollRate, AIN_FLOAT, 1, { AIN_INT } },
		{ "SetHScrollbarScrollPos", PE_v14_SetHScrollbarScrollPos, AIN_VOID, 2, { AIN_INT, AIN_INT } },
		{ "GetHScrollbarScrollPos", PE_v14_GetHScrollbarScrollPos, AIN_INT, 1, { AIN_INT } },
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
