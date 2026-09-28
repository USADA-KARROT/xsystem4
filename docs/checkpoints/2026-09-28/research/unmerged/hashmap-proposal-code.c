=== 1. src/hll/HashMap.c（完整 diff：scratchpad/overload2/hashmap/HashMap.diff；修改後全檔：scratchpad/overload2/hashmap/HashMap.c，clang -Wall -Wextra 無警告）===

// [6] bool Empty(int id)
// Native (dohnadohna_dump_SCY.exe): dispatcher 0x654fb0 case 6 (0x655108)
// returns helper 0x6556a0 as-is. 0x6556a0 is "id valid && size == 0";
// an invalid or released id yields false, not true.
static bool HashMap_Empty(int id)
{
	struct hashmap *map = get_map(id);
	return map && map->count == 0;
}

// [7] bool Any(int id)
// Native: case 7 (0x655132) calls the same helper 0x6556a0 and returns
// (al == 0), i.e. !(id valid && size == 0). An invalid id therefore
// reports true on the original engine. Kept that way on purpose.
static bool HashMap_Any(int id)
{
	struct hashmap *map = get_map(id);
	return !map || map->count != 0;
}

// [8] bool Any(int id, string key)
// Native: case 8 (0x65515d) -> 0x655710: invalid id -> false, otherwise
// std::map find(key) != end.
static bool HashMap_AnyKey(int id, struct string *key)
{
	struct hashmap *map = get_map(id);
	if (!map || !key)
		return false;

	unsigned int h = hash_string(key->text);
	for (struct hm_entry *e = map->buckets[h]; e; e = e->next) {
		if (!strcmp(e->key, key->text))
			return true;
	}
	return false;
}

/*
 * link_static_library binds by name only, so Any(id) and Any(id, key) used
 * to share HashMap_AnyKey and the 1-arg form read a stale register as key.
 * Pick by declared shape. Unknown shapes keep the name-matched fallback so
 * the declaration still links (NULL would route it to UNIMPL).
 */
void *hashmap_select_function(const struct ain_hll_function *f, void *fallback)
{
	if (!f || !f->name || strcmp(f->name, "Any"))
		return fallback;
	if (f->nr_arguments == 1 && f->arguments && f->arguments[0].type.data == AIN_INT)
		return (void*)HashMap_Any;
	if (f->nr_arguments == 2 && f->arguments && f->arguments[0].type.data == AIN_INT
	    && f->arguments[1].type.data == AIN_STRING)
		return (void*)HashMap_AnyKey;
	return fallback;
}

// 匯出表：HLL_EXPORT(Any, HashMap_Any) 改成 HLL_EXPORT(Any, HashMap_AnyKey)，
// 讓不認得的形狀維持舊行為（2 參數語義）。
// 註：如果要把 Empty 的改動和 Any 的修正拆成兩個 commit，Empty 那段可以單獨撤回，不影響 Any 的修正。

=== 2. src/hll/PartsEngine.c（放在 HLL_LIBRARY(PartsEngine, ...) 之前，也就是快照 PartsEngine.c:358 前；該檔已 include system4/ain.h、parts.h、string.h。檔案：scratchpad/overload2/hashmap/pe_layoutbox_select.inc，已做語法檢查並用 libffi 實測）===

/*
 * v14 declares the layout-box return size as float:
 *   [481] void SetLayoutBoxReturn(int Number, bool Return, float ReturnSize)
 *   [483] float GetLayoutBoxReturnSize(int Number)
 * Native stores it as a float at +0x48 (dohnadohna_dump_SCY.exe 0x594d80 /
 * 0x594df0, dispatcher cases 0x5828db / 0x58294f; return type 0xb via
 * 0x657bc0). The int PE_* versions stay for AINs that declare int. With the
 * int prototype under a float CIF, arm64 passes ReturnSize in s0 while C
 * reads w2, and the float return is read from s0 while C writes w0.
 * CN bytecode always feeds ITOF(int) and reads back with FTOI (fno 12029,
 * 12044, 12045), so int storage round-trips exactly.
 */
static void PE_SetLayoutBoxReturnF(int parts_no, bool return_flag, float return_size)
{
	PE_SetLayoutBoxReturn(parts_no, return_flag, (int)return_size);
}

static float PE_GetLayoutBoxReturnSizeF(int parts_no)
{
	return (float)PE_GetLayoutBoxReturnSize(parts_no);
}

// Unknown shapes keep the name-matched fallback (never NULL, which would UNIMPL).
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

=== 3. src/ffi.c link_static_library（插在快照 ffi.c:1176-1180 Array Numof/Count/Find 區塊之後、ffi.c:1181「if (funcptr)」之前。另一位工程師正在改 ffi.c，行號可能漂移，請以「if (funcptr) link_static_library_function」為錨點）===

				if (!strcmp(lib->name, "HashMap")) {
					extern void *hashmap_select_function(const struct ain_hll_function *f, void *fallback);
					funcptr = hashmap_select_function(&ainlib->functions[i], funcptr);
				}
				if (!strcmp(lib->name, "PartsEngine")) {
					extern void *pe_layoutbox_select_function(const struct ain_hll_function *f, void *fallback);
					funcptr = pe_layoutbox_select_function(&ainlib->functions[i], funcptr);
				}

=== 4.（可選，0 呼叫）src/hll/SystemService.c:616 ===
static bool SystemService_RenderToSurfaceImageByScale(int surface, int reduction_size) { return false; }
// 回傳值依宣告改成 bool、參數改成 int；原版語義未驗證，所以先回 false，和同檔其他 stub 的做法一致