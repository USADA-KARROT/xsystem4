/* HashMap.c：依宣告形狀選實作；不認得的形狀回傳 fallback（名稱匹配到的 C 表項目，也就是 HashMap_AnyKey），絕不回 NULL */
void *hashmap_select_function(const struct ain_hll_function *f, void *fallback)
{
	if (!f || !f->name || strcmp(f->name, "Any"))
		return fallback;
	if (f->nr_arguments == 1 && f->arguments && f->arguments[0].type.data == AIN_INT)
		return (void*)HashMap_Any;          /* [7] Any(int id) */
	if (f->nr_arguments == 2 && f->arguments && f->arguments[0].type.data == AIN_INT
	    && f->arguments[1].type.data == AIN_STRING)
		return (void*)HashMap_AnyKey;       /* [8] Any(int id, string key) */
	return fallback;
}

/* PartsEngine.c：只在宣告為 float 時換成 float 包裝，int 宣告（其他遊戲）維持原本的 int 實作 */
void *pe_layoutbox_select_function(const struct ain_hll_function *f, void *fallback)
{
	if (!f || !f->name)
		return fallback;
	if (!strcmp(f->name, "SetLayoutBoxReturn") && f->nr_arguments == 3 && f->arguments
	    && f->arguments[2].type.data == AIN_FLOAT)
		return (void*)PE_SetLayoutBoxReturnF;
	if (!strcmp(f->name, "GetLayoutBoxReturnSize") && f->return_type.data == AIN_FLOAT)
		return (void*)PE_GetLayoutBoxReturnSizeF;
	return fallback;
}

/* 呼叫端（ffi.c link_static_library，名稱匹配後）：funcptr = xxx_select_function(&ainlib->functions[i], funcptr); */