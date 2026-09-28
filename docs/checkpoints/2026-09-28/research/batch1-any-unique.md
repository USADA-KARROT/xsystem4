# any-unique

## 調查摘要
Any、Unique、UniqueSorted、Equals 這四組都有錯，其中三組會影響遊戲流程。

(1) Any#1 共 46 處，目前綁到 Array_Any(array)，AIN 傳來的 hll_func 直接被丟掉，實際回傳的是「陣列非空」。原版 EXE 跳表 index 27 Any(pred) 與 index 57 IsExist(pred) 指向同一個入口 VA 0x64473a（find-first predicate，結果 >= 0 即 true）。所以正解是綁到現有的 Array_IsExist，不該用未綁定的 Array_AnyPredicate。Array_AnyPredicate 本身也有缺陷：2 參數 lambda 的第二槽固定推 0；逐 slot 走訪而不是逐邏輯元素，65539 那 3 處 iface 陣列會把 metadata slot 當成物件餵給 predicate；也沒有限制 nr_args。它是 dead code，baseline 用 -Wall 編譯會出 unused-function 警告，建議刪除。

(2) Unique 的原版語義是全域去重：保留第一次出現的元素、順序不變、O(n^2)。現行 Array_Unique 只刪相鄰重複，而且用 .i 比較，字串比到的是 heap slot，內容相同也不會被刪；被移除的元素也沒有 heap_unref。所以 Unique#0 的 10 處全錯：5 處是 int，其中 BattleBonusCalculator@GetBonuses 在戰鬥路徑上；5 處是 array<string>，包括 Motion::ExecuterCollection@Join。UniqueSorted#0 有 4 處，相鄰語義是對的，但字串同樣比 slot，Shop m_purchasedItems 去不了重。Unique#1 的 lambda 是 2 參數、回傳 bool 的相等判斷（lhs 是較早且保留下來的元素，rhs 是較晚的元素），不是 key selector；目前 hll_func 被忽略。

(3) Equals#0 的 3 處都在 elkeditor，目前的簽名 Array_Equals(struct page**, struct page**) 是錯的。ffi 對 wrap<array> 傳的是 int heap slot（heap_slots[i]，CIF 型別是 pointer），C 端會拿 slot 加上未初始化的 heap_slots[i+1] 湊成一個 64 位元指標再解參考，幾乎一定當掉。這是讀程式碼推出來的，未實跑。Equals#1 沒有被呼叫，照原版 0x6489a0 的語義給了實作，列為低優先。

proposed_code 已套進 scratch 裡 Array.c 的副本，用 probe 的真 compile flags 加 -Wall -fsyntax-only 檢查通過，0 警告。worktree 沒有動。

## lambda
統計腳本在 scratch/overload/any-unique/lam.py，輸出存成 any1_lambdas.txt 和 unique1_lambdas.txt。格式：次數、hll_arg3、lambda 參數型別 → 回傳，後面附（CALLHLL 行號, fno, FUNC 行號）。

Any#1（46 處，全部回傳 bool）：
- 8 處，arg3=2，(string)：ain_code.txt:27722 fno 22344 (FUNC@27700)；27753 fno 22345；27784 fno 22346。
- 4 處，arg3=65538，(ref activityeditor::detail::CInstanceItem)：63921 fno 22393；66199 fno 22408。
- 4 處，arg3=2，(ref _3d::detail::SEffectInstance)：849440 fno 25700。
- 4 處，arg3=65538，(wrap<ActionTarget>)：1038236 fno 36270（遊戲路徑）。
- 2 處，arg3=2，(ref SASPair<string,float>)：18485 fno 22326。
- 2 處，arg3=65538，(ref elkeditor::detail::CEmitterData)：286526 fno 23964。
- 2 處，arg3=1，(int)：472977 fno 24712（frame < m_timeLineItem.method()）、473009 fno 24713。
- 2 處，arg3=65538，(ref parts::detail::CPartsTreeNode)：506852 fno 24821。
- 2 處，arg3=65538，(wrap<stageeditor::detail::CAS3DObject>)：761523 fno 25348。
- 2 處，arg3=65538，(wrap<Player>)：1011113 fno 36140（遊戲路徑）。
- 2 處，arg3=65539，(wrap<iwrap<IPlayerActionView>>, void)：1287048 fno 37203、1287082 fno 37204。這是 2 槽 iface 元素，第二槽必須推 values[i*2+1]。
- 各 1 處：ref CUserComponentSet 25789 fno 22333；ref activity::detail::CUserComponentManager 27843 fno 22347；ref activityeditor::detail::SPair 137310 fno 22500 (arg3=2)；ref elkeditor::detail::CEmitterKeyFrame 343960 fno 24228；ref SASPair<string,string> 418918 fno 24383 (arg3=2)；ref message::detail::CMessageText 421901 fno 24396 (arg3=2)；ref modelviewer::detail::CMotionMeshItem 451037 fno 24582；wrap<iwrap<stageeditor::detail::IElement>>, void 790447 fno 25461 (arg3=65539)；wrap<CASTask> 835529 fno 25696；wrap<BattlePlayer> 1035160 fno 36258；wrap<WorkerEventHint> 1204337 fno 36897；wrap<WorkerCollection> 1217402 fno 36940。
- 結論：全部都是 1 參數的 predicate，或 (iface, void) 這種 2 槽參數，而 Array_IsExist 兩種都有處理（src/hll/Array.c:1719-1744）。

Unique#1（2 處，回傳 bool，2 個參數且都是 ref struct，是相等判斷）：
- ain_code.txt:354845 arg3=2，fno 24299 (FUNC@354834)：ARG0 lhs : ref elkeditor::detail::CEmitterKey，ARG1 rhs : ref elkeditor::detail::CEmitterKey；內容是 .LOCALREF lhs; PUSH 5396; .LOCALREF rhs; CALLMETHOD 1; RETURN，也就是 lhs.IsSame(rhs)。
- ain_code.txt:983315 arg3=2，fno 36014 (FUNC@983297)：ARG0 lhs : ref ExTableLine，ARG1 rhs : ref ExTableLine；兩邊各取 m_keyFormatName 欄位再做 S_EQUALE。這個 lambda 會用 PUSHSTRUCTPAGE（ExTable），依賴 hll_func_obj。

Equals#0（3 處，arg3=1，沒有 lambda）：329419 (CFloatListParam m_values，array<float>)、329600 (CIntListParam m_values)、343622 (Select<int,...> 的結果)。

原生 predicate 的參數順序：0x6468b0 以 (array, array, func) 建 comparator，呼叫時帶 (&i, &j)，i 是較早且保留的元素，所以 lhs=self[i]、rhs=self[j]。Equals#1 則是 (self, src, func) 帶 (&i, &i)。

## proposed_code
```c
/* ----------------------------------------------------------------------
 * Any / Unique / UniqueSorted / Equals overloads (CN AIN v14).
 *
 * Placement: insert this block immediately before array_select_function()
 * (it needs array_erase_stride, array_erase_value_equal, Array_IsExist).
 * Remove the old Array_AnyPredicate (dead, unbound), the old Array_Unique,
 * the old Array_UniqueSorted and the old Array_Equals. Keep Array_Any.
 * The HLL_EXPORT(Unique/UniqueSorted/Equals/Any, ...) lines stay as-is.
 *
 * Native reference: dohnadohna_dump_SCY.exe, Array dispatcher 0x644300,
 * jump table 0x644f18 (index = Array declaration index in the AIN).
 *   #26 Any()              0x6446fc: array length != 0.
 *   #27 Any(pred)          0x64473a: the SAME entry as #57 IsExist(pred),
 *                          i.e. find-first predicate match >= 0.
 *   #40 Equals(src)        0x6449ab -> 0x6488c0: lengths equal, then a
 *                          typed value compare of self[i] and src[i].
 *   #41 Equals(src, pred)  0x6449cf -> 0x6489a0: lengths equal, then
 *                          pred(self[i], src[i]) for every i.
 *   #61 Unique()           0x644c5e -> 0x6494f0: typed (int/enum, bool,
 *                          float, string) removal of EVERY later duplicate,
 *                          first occurrence kept, order kept; other element
 *                          types raise a native "type not comparable" error.
 *   #62 Unique(pred)       0x644c74 -> 0x649670: same loop, pred(self[i],
 *                          self[j]) for surviving i < j; true erases j.
 *   #63 UniqueSorted()     0x644c8f -> 0x649770: adjacent removal against
 *                          the last kept element (std::unique).
 *   #64 UniqueSorted(pred) 0x644ca5 -> 0x6498f0: same, pred(kept, next).
 *
 * The native loops erase in place. Here the callbacks run against the
 * unchanged page and the drops are applied once at the end: the call order
 * and arguments are identical (erasing j only shifts later indices), and
 * the FFI snapshot of `self` is never freed while a callback runs.
 * ---------------------------------------------------------------------- */

/* Element compare for the non-predicate overloads. v14 arrays often carry
 * a generic a_type, so float detection is best effort; otherwise the
 * compare is bitwise, which differs from native only for -0.0 and NaN. */
static bool array_elem_value_equal(const struct page *a, union vm_value x, union vm_value y)
{
	if (a->a_type == AIN_FLOAT || a->a_type == AIN_ARRAY_FLOAT)
		return x.f == y.f;
	/* Strings compare by content when the elements are heap references. */
	return array_erase_value_equal(x, y);
}

static bool array_elem_equal_at(const struct page *a, int i, const struct page *b, int j, int stride)
{
	if (!array_elem_value_equal(a, a->values[i * stride], b->values[j * stride]))
		return false;
	/* Extra slots (interface/option) are metadata, compare them raw. */
	for (int k = 1; k < stride; k++) {
		if (a->values[i * stride + k].i != b->values[j * stride + k].i)
			return false;
	}
	return true;
}

/* (lhs, rhs) -> bool callbacks. One slot per argument: a scalar, a string
 * slot, or the struct slot for `ref S`. vm_call_nopop owns the arg refs. */
static void array_pair_callback_shape(int func, int stride)
{
	if (func < 0 || func >= ain->nr_functions)
		VM_ERROR("Array pair predicate: invalid function %d", func);
	struct ain_function *cb = &ain->functions[func];
	if (cb->address >= ain->code_size || cb->return_type.data != AIN_BOOL
	    || !cb->vars || cb->nr_vars < cb->nr_args || cb->nr_args != 2 || stride != 1)
		VM_ERROR("Array pair predicate: unsupported signature %d / stride %d", func, stride);
	for (int k = 0; k < 2; k++) {
		switch (cb->vars[k].type.data) {
		case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
		case AIN_ENUM: case AIN_ENUM2: case AIN_STRING: case AIN_REF_STRING:
		case AIN_STRUCT: case AIN_REF_STRUCT: case AIN_WRAP:
			break;
		default:
			VM_ERROR("Array pair predicate: unsupported argument type %d",
				 cb->vars[k].type.data);
		}
	}
}

static bool array_pair_predicate(int func, union vm_value lhs, union vm_value rhs)
{
	int saved_sp = stack_ptr;
	stack_push(lhs);
	stack_push(rhs);
	vm_call_nopop(func, 2);
	bool match = stack_pop().i != 0;
	stack_ptr = saved_sp;
	return match;
}

/* The FFI passes a local snapshot of `self`. A callback that replaces the
 * owner's page would be overwritten by the FFI write-back; refuse it, as
 * array_query_predicate does. */
struct array_owner {
	int slot;
	bool tracked;
	struct page *before;
};

static struct array_owner array_owner_track(struct page *page)
{
	int owner = hll_self_slot;
	struct array_owner o = { owner, false, page };
	o.tracked = owner >= 0 && (size_t)owner < heap_size && HEAP_REF(owner) > 0
		&& heap[owner].type == VM_PAGE && heap[owner].page == page;
	return o;
}

static void array_owner_check(const struct array_owner *o, struct page **array)
{
	if (o->tracked) {
		if ((size_t)o->slot >= heap_size || HEAP_REF(o->slot) <= 0
		    || heap[o->slot].type != VM_PAGE)
			VM_ERROR("Array: predicate released its array owner");
		*array = heap[o->slot].page;
	}
	if (*array != o->before)
		VM_ERROR("Array: predicate changed array storage");
}

/* Drop the flagged logical elements in one pass. Only the first slot of an
 * element owns a heap reference (same contract as Array_Erase). */
static void array_drop_flagged(struct page **array, const bool *drop, int stride)
{
	struct page *a = *array;
	int count = a->nr_vars / stride;
	int keep = 0;
	for (int i = 0; i < count; i++) {
		if (!drop[i])
			keep++;
	}
	if (keep == count)
		return;
	if (array_elem_is_ref()) {
		for (int i = 0; i < count; i++) {
			if (drop[i] && a->values[i * stride].i > 0)
				heap_unref(a->values[i * stride].i);
		}
	}
	if (keep == 0) {
		free_page(a);
		*array = NULL;
		return;
	}
	struct page *n = alloc_page(ARRAY_PAGE, a->a_type, keep * stride);
	int w = 0;
	for (int i = 0; i < count; i++) {
		if (drop[i])
			continue;
		for (int k = 0; k < stride; k++)
			n->values[w * stride + k] = a->values[i * stride + k];
		w++;
	}
	n->array = a->array;
	free_page(a);
	*array = n;
}

static void array_unique(struct page **array, bool use_pred, int func, bool adjacent)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return;
	struct page *a = *array;
	int stride = array_erase_stride(a);
	int count = a->nr_vars / stride;
	if (count <= 1)
		return;
	if (use_pred)
		array_pair_callback_shape(func, stride);
	struct array_owner owner = array_owner_track(a);
	bool *drop = calloc(count, sizeof(bool));
	if (!drop)
		VM_ERROR("Array.Unique: out of memory");

	if (adjacent) {
		int kept = 0;
		for (int j = 1; j < count; j++) {
			bool dup;
			if (use_pred) {
				dup = array_pair_predicate(func, a->values[kept], a->values[j]);
				array_owner_check(&owner, array);
			} else {
				dup = array_elem_equal_at(a, kept, a, j, stride);
			}
			if (dup)
				drop[j] = true;
			else
				kept = j;
		}
	} else {
		for (int i = 0; i < count; i++) {
			if (drop[i])
				continue;
			for (int j = i + 1; j < count; j++) {
				if (drop[j])
					continue;
				bool dup;
				if (use_pred) {
					dup = array_pair_predicate(func, a->values[i], a->values[j]);
					array_owner_check(&owner, array);
				} else {
					dup = array_elem_equal_at(a, i, a, j, stride);
				}
				if (dup)
					drop[j] = true;
			}
		}
	}
	array_drop_flagged(array, drop, stride);
	free(drop);
}

/* #61 Unique(self): remove every later duplicate, keep the first. */
static void Array_Unique(struct page **array)
{
	array_unique(array, false, -1, false);
}

/* #62 Unique(self, func): func(lhs, rhs) is an equality test, lhs earlier. */
static void Array_UniqueIf(struct page **array, int func)
{
	array_unique(array, true, func, false);
}

/* #63 UniqueSorted(self): adjacent duplicates only (std::unique). */
static void Array_UniqueSorted(struct page **array)
{
	array_unique(array, false, -1, true);
}

/* #64 UniqueSorted(self, func): func(kept, next). */
static void Array_UniqueSortedIf(struct page **array, int func)
{
	array_unique(array, true, func, true);
}

/* wrap<array<T>> arrives as the array's heap slot (int), never as a
 * struct page ** (see ffi.c AIN_WRAP: heap_slots[i]). A live slot with a
 * NULL page is an empty array. */
static bool array_wrap_slot_valid(int slot)
{
	return slot > 0 && (size_t)slot < heap_size && HEAP_REF(slot) > 0
		&& heap[slot].type == VM_PAGE
		&& (!heap[slot].page || heap[slot].page->type == ARRAY_PAGE);
}

static bool array_equals(struct page **self, int src_slot, bool use_pred, int func)
{
	if (!self || !array_wrap_slot_valid(src_slot))
		return false;
	struct page *a = *self;
	struct page *b = heap[src_slot].page;
	if (a && a->type != ARRAY_PAGE)
		return false;
	int stride = a ? array_erase_stride(a) : (b ? array_erase_stride(b) : 1);
	if (a && b && array_erase_stride(b) != stride)
		return false;
	int na = a ? a->nr_vars / stride : 0;
	int nb = b ? b->nr_vars / stride : 0;
	if (na != nb)
		return false;
	if (na == 0)
		return true;
	if (use_pred)
		array_pair_callback_shape(func, stride);
	struct array_owner owner = array_owner_track(a);
	for (int i = 0; i < na; i++) {
		bool eq;
		if (use_pred) {
			eq = array_pair_predicate(func, a->values[i], b->values[i]);
			array_owner_check(&owner, self);
			if (!array_wrap_slot_valid(src_slot) || heap[src_slot].page != b)
				VM_ERROR("Array.Equals: predicate changed source storage");
		} else {
			eq = array_elem_equal_at(a, i, b, i, stride);
		}
		if (!eq)
			return false;
	}
	return true;
}

/* #40 Equals(self, wrap<array> src). */
static bool Array_Equals(struct page **self, int src_slot)
{
	return array_equals(self, src_slot, false, -1);
}

/* #41 Equals(self, wrap<array> src, func): func(self[i], src[i]). */
static bool Array_EqualsIf(struct page **self, int src_slot, int func)
{
	return array_equals(self, src_slot, true, func);
}

/* Any: keep the existing Array_Any(struct page **) for #26 (length != 0).
 * #27 Any(func) binds to the existing Array_IsExist(struct page **, int);
 * no new function is needed. Delete Array_AnyPredicate. */
```

## selector
```c
/* Replaces the current array_select_function() in src/hll/Array.c. */
static bool array_decl_has_func(const struct ain_hll_function *f)
{
	for (int i = 0; i < f->nr_arguments; i++) {
		enum ain_data_type t = f->arguments[i].type.data;
		if (t == AIN_HLL_FUNC || t == AIN_HLL_FUNC_71)
			return true;
	}
	return false;
}

/* Select by declared signature, never by a game's function index. */
void *array_select_function(const struct ain_hll_function *f)
{
	if (!f || !f->name || !f->arguments || f->nr_arguments < 1)
		return NULL;
	switch (f->arguments[0].type.data) {
	case AIN_REF_ARRAY_TYPE:
	case AIN_REF_ARRAY:
		break;
	default:
		return NULL;
	}
	bool has_func = array_decl_has_func(f);
	int n = f->nr_arguments;
	if (!strcmp(f->name, "First"))
		return has_func ? (void *)Array_First : (void *)Array_First_NoPred;
	/* Native #27 Any(pred) and #57 IsExist(pred) share entry 0x64473a. */
	if (!strcmp(f->name, "Any")) {
		if (f->return_type.data != AIN_BOOL)
			return NULL;
		if (n == 1)
			return (void *)Array_Any;
		return n == 2 && has_func ? (void *)Array_IsExist : NULL;
	}
	if (!strcmp(f->name, "Unique") || !strcmp(f->name, "UniqueSorted")) {
		bool sorted = f->name[6] == 'S';
		if (f->return_type.data != AIN_VOID)
			return NULL;
		if (n == 1)
			return sorted ? (void *)Array_UniqueSorted : (void *)Array_Unique;
		if (n == 2 && has_func)
			return sorted ? (void *)Array_UniqueSortedIf : (void *)Array_UniqueIf;
		return NULL;
	}
	if (!strcmp(f->name, "Equals")) {
		if (f->return_type.data != AIN_BOOL || n < 2
		    || f->arguments[1].type.data != AIN_WRAP)
			return NULL;
		if (n == 2)
			return (void *)Array_Equals;
		return n == 3 && has_func ? (void *)Array_EqualsIf : NULL;
	}
	return NULL;
}

/* src/ffi.c link_static_library (currently lines 1172-1175): widen the
 * First-only branch so these names also go through the selector. */
				if (!strcmp(lib->name, "Array") && (!strcmp(ainlib->functions[i].name, "First")
				    || !strcmp(ainlib->functions[i].name, "Any")
				    || !strcmp(ainlib->functions[i].name, "Unique")
				    || !strcmp(ainlib->functions[i].name, "UniqueSorted")
				    || !strcmp(ainlib->functions[i].name, "Equals"))) {
					extern void *array_select_function(const struct ain_hll_function *f);
					funcptr = array_select_function(&ainlib->functions[i]);
				}

/* Note: when the selector returns NULL the function is left unlinked (same
 * as the Erase/IsExist/query selectors), so a declaration shape nobody
 * expected surfaces as an unimplemented HLL call instead of a silent
 * mis-binding. All 8 CN declarations (#26,#27,#40,#41,#61-#64) match a
 * branch above. */
```

## fixture_plan
仿照 first_overload_fixture.inc 的寫法：真 AIN、真 hll_call、不跑 bytecode。宣告用 fo_find_fn(lib, name, nr_args, has_func, nth) 找。func 參數依 ffi AIN_HLL_FUNC 分支的順序推 2 槽：先 stack_push(-1)（obj，無 env），再 stack_push(fno)。每次呼叫後 assert stack_ptr 平衡。

A. Any（arg3=2，字串陣列，用 fo_make_string_array）
  - lambda 用真 fno 22496（ain_code.txt:136908，(value:string)->bool，內容是 value.EndsWith(".pact")，不碰 struct page 或 env）。
  - Any#1 對 ["a.pact","b.txt"]，預期 1。
  - Any#1 對 ["x.txt","y.txt"]，預期 0。現行實作回 1（只看非空），這筆就是 bug 重現點。
  - Any#1 對空陣列（slot 的 page 為 NULL），預期 0。
  - Any#0 對 ["x"] 預期 1，對空陣列預期 0（現行已正確，當回歸對照）。
  - 另外做 IsExist#1 同輸入的對照組，結果必須與 Any#1 逐筆相同（原生共用 0x64473a）。

B. Unique#0 / UniqueSorted#0
  - int，arg3=1：alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 5)，內容 [3,1,3,2,1]。
    - Unique#0 預期 [3,1,2]。現行（只刪相鄰）得 [3,1,3,2,1]。
    - UniqueSorted#0 對 [1,1,2,1] 預期 [1,2,1]。
  - string，arg3=2：["b","a","b","a"]，每個元素是獨立的 heap string slot。
    - Unique#0 預期剩 ["b","a"]。現行不會刪任何元素。
    - 被刪的兩個 slot 在 heap_unref 後 HEAP_REF 必須降為 0 或已釋放；ASAN 下不得出現 UAF 或 leak。
    - UniqueSorted#0 對 ["a","a","b"] 預期 ["a","b"]。現行因為比 slot 而不變。

C. Unique#1 / UniqueSorted#1（arg3=2，struct 陣列）
  - 用 struct 273 modelviewer::detail::SMotionAlphaParam（structures.txt:3303-3307，field0 是 int FrameNumber）。
  - 陣列：alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, n)，array.struct_type=273，rank=1。每個元素用 alloc_struct(273) 建立，再設 page->values[0].i。
  - lambda 用真 fno 24536（ain_code.txt:438982，(a: ref SMotionAlphaParam, b: ref SMotionAlphaParam)->bool，內容是 a.FrameNumber < b.FrameNumber，純函式）。
  - Unique#1 對 FrameNumber [1,3]：pred(1,3)=true，預期剩 [1]。
    - 若參數順序顛倒，pred(3,1)=false，會剩 [1,3]。
    - 現行（忽略 func、比 slot）也是 [1,3]。
    - 這筆同時驗證 lhs=earlier 的順序與 predicate 有被呼叫。
  - Unique#1 對 [2,5,1,3]，預期 [2,1]。
  - UniqueSorted#1 對 [2,5,1,3]，預期 [2,1]。
  - 被刪的 struct slot 的 refcount 必須減 1。
  - 用一個計數 wrapper（或 instrumented-vm.inc 的呼叫計數）確認 Unique#1 在 [1,3,0] 上呼叫 predicate 的順序是 (1,3)、(1,0)，對應原生 0x649670 的順序。

D. Equals（arg3=1）
  - int 陣列 A=[1,2,3]，self 推 A 的 slot，src 推 B 的 heap slot（wrap<array> 只佔 1 槽）。
    - B=[1,2,3]，預期 1。
    - B=[1,2,4]，預期 0。
    - B=[1,2]，預期 0。
    - A、B 都是空 page（NULL），預期 1。
  - 現行實作預期在 ASAN 下 SEGV 或讀到野指標，因為 C 端把 int slot 當 struct page ** 用。這筆可以當 bug 重現點。
  - Equals#1：用 C 段的 struct 陣列和 fno 24536，A=[1,2]、B=[2,3]，逐位 a<b，預期 1；B=[2,2]，預期 0。

E. 綁定檢查
  - link 後印出 libraries[lib][idx].fun 的指標值。
  - #27 必須等於 IsExist#1 (#57) 的指標，#61..#64 與 #40/#41 必須對應各自的新函式。
  - 用 dladdr 或比對函式位址確認沒有任何一個回落到舊的 HLL_EXPORT 函式。

F. 不在本階段做
  - 65539 iface 2 槽的 Any#1（fno 37203/37204/25461）需要建 iface 陣列與真 vtable offset，另開 fixture。
  - Unique#1 fno 36014 依賴 PUSHSTRUCTPAGE(ExTable)，需要真 hll_func_obj，不適合 headless。

## open
- Any#1 綁到 Array_IsExist，是為了與原生共用入口 0x64473a 一致，但 Array_IsExist 沒有 array_query_predicate 那種 owner page refresh 保護。另一個選項是綁到 Array_FindIf(...) >= 0 的 wrapper：shape 檢查較嚴，遇到非預期形狀會 VM_ERROR。要選哪種政策，應與 IsExist#1 一起決定，兩者必須維持相同行為。
- 原生 Unique()/UniqueSorted() 遇到不可比較的元素型別（struct、long_int 等）會以 0x7e22f0 的格式字串報「配列比較出来ない型【 %d 】です」，但 0x7285d0 是否 fatal 未驗證。提案目前對這類型別回落為 slot identity 比較，CN AIN 沒有這種呼叫處。
- v14 陣列的 a_type 常是泛型值（page.c alloc_array 與 PushBack 預設 AIN_ARRAY_INT），float 陣列無法可靠辨識。提案的 float 分支是 best effort，其餘情況 bitwise 比較，只在 -0.0 與 NaN 上與原生 ucomiss 不同。Equals#0 在 329419 的 CFloatListParam 是 array<float>，實際 a_type 未驗證。
- 原生 Equals 的型別化 comparator 0x646620 沒有完整反組譯，string/struct 元素的比較規則未驗證。CN 的呼叫處只有 int/float，影響不大。
- 0x6468b0（predicate comparator 工廠）的 delegate capture/retain 契約未還原。參數順序 lhs=self[i]（較早且保留）、rhs=self[j] 是由 push 順序與呼叫時的 (&i, &j) 推得，已有靜態證據，但沒有動態驗證。
- Rufim 的 ix_unique 是倒序走訪，pred(j,i) 取所有 j<i，呼叫順序與原生不同；對對稱的相等 predicate 結果相同。本提案依原生順序實作。
- Equals#0 現行實作「解參考垃圾指標而當掉」是由 ffi.c:437-463（heap_slots 是 int 陣列、CIF 型別是 ffi_type_pointer）推得，未實跑。3 處都在 elkeditor（編輯器），正常遊戲流程可能不會觸發。
- hll_arg3=2 同時用於 array<string> 與值型 struct 陣列。array_erase_value_equal 只在兩邊都是 VM_STRING 時比內容，其餘比 slot；若有 struct 陣列誤呼叫 Unique#0，行為是 identity 去重，未驗證是否與原生錯誤路徑一致。

## artifacts
- <scratchpad>/overload/any-unique/proposed.c
- <scratchpad>/overload/any-unique/selector.c
- <scratchpad>/overload/any-unique/patched/src/hll/Array.c
- <scratchpad>/overload/any-unique/Array.proposed.diff
- <scratchpad>/overload/any-unique/patched.log
- <scratchpad>/overload/any-unique/base.log
- <scratchpad>/overload/any-unique/cc.sh
- <scratchpad>/overload/any-unique/any.asm
- <scratchpad>/overload/any-unique/equals.asm
- <scratchpad>/overload/any-unique/unique.asm
- <scratchpad>/overload/any-unique/unique_impl.asm
- <scratchpad>/overload/any-unique/unique_string.asm
- <scratchpad>/overload/any-unique/native_unique_int_0x64a580.asm
- <scratchpad>/overload/any-unique/native_uniquesorted_int_0x64ac30.asm
- <scratchpad>/overload/any-unique/native_equals_0x6488c0_0x6489a0.asm
- <scratchpad>/overload/any-unique/any1_lambdas.txt
- <scratchpad>/overload/any-unique/unique1_lambdas.txt
- <scratchpad>/overload/any-unique/sites.txt
- <scratchpad>/overload/any-unique/pe.py
- <scratchpad>/overload/any-unique/lam.py

## 驗證 refuted=False residual=0.82
四組語義我都獨立核對過，沒有找到能推翻的證據。

(1) 跳表與索引：我用自己寫的 capstone/pefile 腳本（scratch/overload/any-unique-refute/pe.py）直接讀 0x644f18。#26=0x6446fc，#27=0x64473a，#57=0x64473a（與 #27 相同），#40=0x6449ab，#41=0x6449cf，#61..#64=0x644c5e/0x644c74/0x644c8f/0x644ca5。libraries.txt 的 Array 段 0-based 索引 26/27/40/41/57/61-64 與簽名相符。

(2) Any：0x6446fc 是 length != 0。0x64473a 呼叫 0x648c40，後者再呼叫 0x648cc0（長度 <= 0 時 or eax,-1，否則用 predicate 搜尋），結果經 setns 轉成 bool，也就是 find-first >= 0。我獨立統計 Any#1 共 46 處：arg3=2 單參數 17 處、65538 單參數 24 處、arg3=1 單參數 2 處、65539 的 (x, void) 3 處，全部回傳 bool，都落在 Array_IsExist 支援的形狀內。stride 由 PushBack 設 struct_type=2 決定（Array.c:157-162），與 Numof/At 一致。vm_call_nopop（vm.c:1718-1770）會對 AIN_WRAP/STRING/REF_TYPE（REF_TYPE 巨集包含 REF_STRUCT）的參數做 heap_ref，回傳時由 local page 釋放，ref 計數平衡。

(3) Unique：0x6494f0 依元素型別分派，10/92 走 0x64a580（int/enum），我讀出的組語是先把值複製到本地 vector，再跑雙層迴圈 i、j=i+1..，v[i]==v[j] 時呼叫 vtable+0x5c 刪除 j 並 j--，所以是全域去重、保留第一次出現的元素。0x649670：ebx=i，esi=j=i+1，push &j 再 push &i 後呼叫 comparator [vtable+8]，所以 arg0=&i、arg1=&j；結果為 true 就刪 j 並 j--。0x6498f0：size <= 1 直接 return，ebx=kept，esi=next；true 就刪 next，false 就令 kept=next。0x64ac30（int 版 UniqueSorted）是 std::unique。提案把刪除延到最後一次套用，callback 的呼叫順序與參數都和原生相同。

呼叫處核對：Unique#0 的 10 處中，5 處是 array<string>（27667 typeList、237449 list、273942 folderList、998493 m_joinSectionNames、1714284 GetUnique<string>），5 處是 int/enum（276528、351578、356088、475734，以及 1047665 m_bonuses，型別 array<BattleBonusType#92>）。UniqueSorted#0 的 4 處：75451 是 int；1024828、1291931、1714308 是 string，arg3 都是 2，所以 array_erase_value_equal 會走比內容的路徑。Unique#1 兩個 lambda 都是 (ref S, ref S) -> bool：24299 呼叫 CEmitterKey@IsSame（fno 5396），36014 是 lhs.GetString(m_keyFormatName) == rhs.GetString(m_keyFormatName)（fno 26726 = ExTableLine@GetString）。提案說 36014 是『比較 m_keyFormatName 欄位』，描述不精確，但它確實是相等判斷。CALLHLL 前面的 PUSHSTRUCTPAGE 會設定 hll_func_obj，ffi.c:400-403 與 800 段落附近也會 save/restore 這個值與 arg3、self_slot。

(4) Equals：0x6488c0 用 ecx=self、edx=src；先比兩邊 vtable+0xc 的長度，不等就回 false；再由 0x646620 建 comparator，對每個 i 以 (&i,&i) 呼叫，遇到 false 就回 false。3 處呼叫都在 elkeditor：CFloatListParam/CIntListParam 是 (newValues, m_values)，CEmitterKeyFrameLine 是 (frameNumberList, Select 結果)，src 都是陣列 heap slot。

程式碼部分：push 槽數（pair predicate 限定 stride=1，每個參數 1 槽）、stack 平衡（saved_sp 模式與既有的 IsExist/First 相同）、回傳 bool 用 .i != 0、value struct（arg3=2）刪除時 unref 第一槽、空陣列與 count <= 1 提前返回、selector 的 name[6] 判斷，都沒有找到會造成錯誤行為的 bug。找到的問題都是證據描述錯誤或低優先的邊界。

我沒有實際執行 probe，也沒有反組譯 0x6468b0/0x646620 的內部。

### code_bugs
- 證據描述錯誤，但結論不變（Equals#0 當掉的機制）：src/ffi.c:1134-1142 的 link_static_library_function 對帶 array_type 的 AIN_WRAP 另外處理，inner 不是 int/float/bool/long_int 時 CIF 型別是 ffi_type_sint32，不是提案說的 ffi_type_pointer（ffi.c:1109 那個分支實際上不會走到）。參數傳的是 &heap_slots[i]（ffi.c:391 宣告為 int heap_slots[]，1-slot 路徑在 ffi.c:460-463）。所以現行 Array_Equals(struct page **a, struct page **b) 的 b 其實是 slot 號碼經 sign-extend 後當成指標，不是提案說的『slot 拼上未初始化的 heap_slots[i+1]』。*b 會解參考一個小於 4GB 的位址，在 macOS arm64 會打到 __PAGEZERO，所以只要 slot 不是 0 就一定 SIGSEGV（未實跑）。提案用 int src_slot 接參數，與 sint32 CIF 一致，修法本身正確。
- 原生 Any(pred)/IsExist(pred) 在 NULL 陣列物件上的回傳值與 Array_IsExist 相反：0x648c40 在 0x6473b0 回報『NULLオブジェクトからarrayオブジェクト関數が呼び出されました』（0x7e21c8）之後回傳 eax=0，接著 0x644755 的 setns 把它轉成 true。Array_IsExist（Array.c:1722-1724）在 page 為 NULL 時回傳 false。xsystem4 的 NULL page 代表空陣列，原生的空陣列物件則不是 NULL（空陣列走 0x648d06-0x648d0a 回傳 -1，也就是 false），所以正常流程兩邊一致；差異只出現在原生的錯誤路徑上，列為低優先註記。
- Any#1 綁到 Array_IsExist 後會沿用它沒有防護的地方（Array.c:1719-1745）：沒有檢查 cb->address 與 return_type。如果 lambda 位址無效，vm_call_nopop（vm.c:1723-1724）會直接 return、不 push 回傳值，stack_pop 就會把剛 push 的元素當成結果讀走（saved_sp 會還原，所以 stack 仍然平衡）。另外 predicate 執行後不會刷新 owner page：*self 是 FFI 的快照，callback 若替換或釋放 owner page，就會讀到懸空的 page。這兩點在 IsExist#1 已經存在，不是新引入的，但提案裡新寫的 pair-predicate 路徑有 shape 與 owner 檢查，Any#1 卻沒有，保護程度不一致。建議 Any#1 與 IsExist#1 一起改走 array_query_callback_shape/array_query_predicate（Array_FindIf(...) >= 0），CN 的 46 處形狀都在它的白名單內：(string/ref struct/wrap/int) 單參數、stride=1，或 (wrap<iwrap>, void)、stride=2。
- array_wrap_slot_valid 只接受 heap[slot].page 是 NULL 或 ARRAY_PAGE。如果 src 傳進來的是 wrap 的 STRUCT_PAGE（ffi.c:537-560 的 REF_ARRAY 路徑會做這種 unwrap，WRAP 路徑不會），Equals 會靜默回傳 false，不會報錯。CN 的 3 處傳的都是陣列 slot 本身：329419/329600 是 .STRUCTREF m_values，343622 是 Select 結果經 X_ASSIGN 留在 stack 上，所以不受影響，屬於低優先的防禦缺口。
- 字串比較的邊界：array_erase_value_equal（Array.c:672-684）只在兩邊都是有效的 VM_STRING slot 時才比內容。一邊是 0/-1（未初始化的字串元素）、另一邊是內容為 "" 的 slot 時會判為不相等，原生 0x64aa00 是比內容和長度，會判為相等。這只影響空字串，CN 各呼叫處都是 Concat/PushBack 產生的非空字串，影響極低。
- 提案對原生會報錯的型別（long_int=55、struct、65538 ref struct）沒有報錯，而是退回用 slot identity 或 bitwise 比較。原生的型別分派表只收 10/11/12/47/92（0x64961c，我獨立讀出的結果：{10:0,11:1,12:2,47:3,92:0}，其餘到 0x649571 報錯）。CN 沒有這類呼叫處，但行為與原生不同，可以考慮改成 VM_ERROR，比較符合『不靜默走錯』的慣例。

### corrected
語義部分同意，只修正一處證據描述。

- Equals#0 當掉機制的正確說法：wrap<array> 的 CIF 是 ffi_type_sint32（ffi.c:1134-1142），C 端的 struct page **b 會收到 sign-extend 後的 slot 整數，*b 解參考低位址而 SIGSEGV（未實跑）。

四組語義我都獨立確認為正確，證據在 scratch/overload/any-unique-refute/*.asm：
- Any#1 就是 IsExist(pred)，也就是 find-first >= 0，入口 0x64473a 與 #57 共用。
- Unique#0 是全域去重，保留第一次出現的元素（0x64a580）。
- Unique#1 呼叫 pred(self[i] 存活元素, self[j])，j>i，true 就刪 j（0x649670）。
- UniqueSorted#0 與 #1 是相鄰去重，比較對象是最後保留的元素（0x64ac30、0x6498f0）。
- Equals#0 與 #1 先比長度，再逐一比 self[i] 與 src[i]（0x6488c0、0x6489a0）。

建議：Any#1 與 IsExist#1 一起改走 array_query_predicate 路徑（Array_FindIf >= 0），補上 shape 與 owner 檢查。

### counter_evidence
以下幾點削弱或修正了原提案的論述，但都不足以推翻語義。

1. ffi.c:1134-1142 顯示 wrap<array> 的 CIF 是 sint32，不是 pointer，提案對 Equals 當掉機制的說明有誤。不過推論出的『解參考壞指標、SIGSEGV』反而更確定，因為 b 會是小於 4GB 的整數指標。

2. 原生 0x648c40 在 NULL 陣列物件上回傳 0，經 setns 變成 true，與 xsystem4 回傳 false 不同。這只發生在原生的錯誤路徑（先報『NULLオブジェクトから…』），空陣列物件則會回傳 -1，也就是 false，所以一般情況一致。

3. 『Unique#0 10 處全錯』的說法太強。int 陣列在輸入本來有序或重複元素相鄰時，現行的相鄰語義結果恰好正確。以 1047665 GetBonuses 的 m_bonuses 來說，只有出現不相鄰的重複 enum 才會出錯，這點未驗證。字串陣列那 5 處則確實失效，因為現行實作比的是 slot。

4. Unique#1 的 lhs/rhs 順序（arg0=&i）我已經從 0x6496f2-0x6496fd 的 push 順序獨立確認。但 0x6468b0 如何把 index 轉成 VM 參數，我沒有反組譯。兩個 lambda（IsSame、GetString==GetString）在語意上是對稱的，所以就算順序反了，CN 的結果也不受影響。
