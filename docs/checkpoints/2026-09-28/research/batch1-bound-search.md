# bound-search

## 調查摘要
結論：predicate 版的 LowerBound#1（30 處）與 BinarySearch#1（30 處）一律是「單一元素的三向 comparator」，回傳 int，符號約定是 sign(元素 - 目標)：元素排在目標前面回傳負數，相等回傳 0，排在後面回傳正數。搜尋目標不經參數傳入，由 lambda 透過 X_GETENV 捕捉。這個約定有三方面證據：
(1) 60 個呼叫處的 lambda 宣告都是 RETURN int。body 有四種寫法，方向都一樣：IdArray@CompareFunc(obj, id)（fno 34982，S_LT 回 -1、S_GT 回 1）、StringExtensions::Compare(elem, key)（fno 20957）、elem.key - target 用 SUB（fno 23896/36511）、FrameInfo 區間判斷（fno 36403/36408，time < start 時回 1）。
(2) SCY EXE 反組譯 index 50-55，三個二分核心 0x646c70/0x646cc0/0x646d10 都是 lo=0、hi=len 的半開區間。LowerBound 在 cmp<0 時往右，UpperBound 在 cmp<=0 時往右，BinarySearch 在 cmp==0 時回傳 mid，找不到回 -1。predicate functor 0x810eec 經 0x64b880 呼叫 0x646590，直接回傳 lambda 的 int 值，沒有取負。
(3) 呼叫端用法：IdArray@IsExist 判斷 FindIndex != -1，IdSet@Exists 與 IsExistNode 判斷 >= 0，GetShakePosition 在 < 0 時走預設。Add 系列把 LowerBound 的結果直接交給 Insert（可以等於 Numof）。

目前 HEAD 按名稱把 #1 綁到值比較實作，func 編號被當成搜尋值，導致 IdArray/IdSet/UniqueArray/ExTable/ExSaverTree/MapStructure/FrameInfo 的排序插入與查找全部失效。

值版 LowerBound#0 只有 2 處，都在 array<int> m_unusedIndexList（CPartsMessageManager，dump 498406/498450），回傳值當插入位置與 PopBack 界線。現行 Array_LowerBound 在這兩處行為正確。BinarySearch#0 在 CN 沒有呼叫處，但現行實作用閉區間 [lo,hi]，重複元素時回傳的 index 與原版不同（{1,3,3,5} 找 3：現行回 1，原版回 2）。

修法：
- 把 callback helper 一般化成可指定回傳型別（bool 或 int）。
- 新增 Array_{LowerBound,UpperBound,BinarySearch}{Value,If} 六個實作，完全照原版半開二分。
- 值版照原版只接受 int、float、string。int 用 32-bit wrapping 減法；float 用 comiss，NaN 視為相等；string 先 memcmp 較短長度，再比長度。
- array_select_function 依宣告分流，ffi.c 把這三個名稱加進 dispatch 條件。

驗證：在 scratchpad 複製 harness，搭配真 AIN 與真 ffi 實跑（原 harness 與 worktree 都沒動）。baseline（HEAD a8d92df）10 項失敗：IdSet<string> 插入順序錯、還插進重複項，Exists 全部回 false；IdSet<GameDialogType> 同樣失敗；BinarySearch#0 重複鍵的 index 不同。patched 版 0 失敗。既有的 first-overload、observer、reentrancy、personality、assignment、heap-reuse、click、metadata、array-reinit、deleted-event fixture 在 patched 與 baseline 結果完全一致（deleted-event 兩版都是 rc=87，屬既有狀態）。patch 可以 git apply --check 通過。

## lambda
60 個呼叫處全部是 RETURN int。格式：CALLHLL 行號 / fno / FUNC 行號 / 參數 / arg3。dump 取自 <cn-dump>/ain_code.txt。

【兩槽 iface 元素，ARG1 為 void】
- LowerBound#1 @238792 fno 23862 FUNC@238759 (obj: wrap<iwrap<IResourceInfo>>, <void>) arg3=65539。ResourceInfoCollection@Add，body 為 CALLFUNC StringExtensions::Compare(obj.name, env.resourceName)。
- LowerBound#1 @296331 fno 23985 FUNC@296296 (obj: elkeditor::detail::IKeyData, <void>) arg3=65539。body 為 obj.method() SUB env[0]。

【wrap<T> 單參數，arg3=65538】
- 23896@272247 (LowerBound) 與 23897@272451 (BinarySearch)：CDebugThumbnail，thumbnail.m4312() SUB env[0]。
- 36029@986761 (LowerBound)、36030/36031/36032/36033@986819/986853/986887/986921 (BinarySearch)：ExSaverTreeNode/Leaf，StringExtensions::Compare(node.field2, env[0])。
- 36403@1064068 (BinarySearch)：FrameInfoCollection@GetIndexFromTime。start<=time<end 回 0；time<start 回 1；否則回 -1。
- 36408@1064887 (BinarySearch)：GetShakePositionFromEnemyShake。time<start 回 1；time>=end 回 -1；否則回 0。帶局部變數 start/end，nr_args=1。
- 36511@1090618 (BinarySearch)：MapStructure@GetNode，value.field1 SUB env[0]。
- 37580..37584@1718184/1718244/1718303/1718362/1718422 (LowerBound)：ExSaverTreeNode@AddLeaf<T>，l: wrap<ExSaverTreeLeaf>。

【ref struct 或 string 單參數，arg3=2】
- ExTable：36012@983055 (BinarySearch)、36013@983221 (LowerBound)，obj: ref ExTableLine，呼叫 CompareFunc 26703(obj, env.id)。26703 在 FUNC@983243：tId<id 回 -1，tId>id 回 1。
- StandNameFinder：36473@1083057 (BinarySearch)，obj: ref StandNameTags，obj.m29890() 以 S_LT 回 -1、S_GT 回 1。
- IdArray<string|int, T>：Add 用 LowerBound，FindIndex 用 BinarySearch，14 組共 28 處。fno 對照：
  - ActivityInstances 37443/37444（@1695959/@1696008）
  - CgPartsQueue 37449/37450
  - Motion::ParsedObject 37455/37456
  - GameTimeScaling 37461/37462
  - VariableTimer 37467/37468
  - StockCount 37476/37477
  - Customer 37482/37483
  - DungeonState 37488/37489
  - ShopDisplayUnit 37496/37497
  - Worker 37501/37502
  - BattleSkill 37507/37508
  - Dungeon 37521/37522
  - ReachedInfo 37527/37528
  - MapStructure 37533/37534
  - MusicInformation 37539/37540
  - Weapon 37545/37546
  - AchievementStateContainer 37551/37552
  
  body 都是 CALLMETHOD CompareFunc(obj, env.key)。以 ActivityInstances 版為例，CompareFunc 是 fno 34982，FUNC 在 dump 約 1696032：S_LT 回 -1，S_GT 回 1，否則回 0。
- IdSet<string>：37473@1697973 (LowerBound)、37474@1698010 (BinarySearch)，obj: string，呼叫 CompareFunc 35063(lhs=obj, rhs=env.t)，FUNC@1698068。
- UniqueArray<string>：37494@1699535 (BinarySearch)、37495@1699644 (LowerBound)，obj: string，inline S_LT 回 -1、S_GT 回 1。

【enum 單參數，arg3=1】
- IdSet<GameDialogType>：37557@1706026 (LowerBound)、37558@1706062 (BinarySearch)，obj: GameDialogType#92。

StringExtensions::Compare(source, target) 是 fno 20957，FUNC@846539：source<target 回 -1，>target 回 1。

所有 body 的第一運算元都是元素、第二運算元是捕捉的目標，所以符號約定是 sign(元素 - 目標)，和 EXE 核心的「cmp<0 往右」一致。

## proposed_code
```c
/* ==== (A) Replaces array_query_callback_shape() and array_query_predicate()
 * in place (src/hll/Array.c:1879-1945 @a8d92df). Find/Count/IsExist keep
 * the same behavior through the two thin wrappers at the end. ==== */

/* 'ret' is the declared callback return type: bool for predicates
 * (Find/Count/IsExist), int for the three-way comparators of
 * LowerBound/UpperBound/BinarySearch. Argument shapes are unchanged. */
static bool array_callback_shape(struct page **array, int func, int stride, enum ain_data_type ret)
{
	if (func < 0 || func >= ain->nr_functions)
		VM_ERROR("Array query: invalid callback %d", func);
	struct ain_function *cb = &ain->functions[func];
	if (cb->address >= ain->code_size || cb->return_type.data != ret
	    || !cb->vars || cb->nr_vars < cb->nr_args || cb->nr_args < 1 || cb->nr_args > 2)
		VM_ERROR("Array query: unsupported callback signature %d", func);
	enum ain_data_type type = cb->vars[0].type.data;
	if (cb->nr_args == 1 && stride == 1) {
		switch (type) {
		case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
		case AIN_ENUM: case AIN_ENUM2: case AIN_STRING: case AIN_REF_STRING:
		case AIN_STRUCT: case AIN_REF_STRUCT: case AIN_WRAP:
			return false;
		default:
			break;
		}
	}
	if (cb->nr_args == 2 && cb->vars[1].type.data == AIN_VOID) {
		bool wrapped_iface = type == AIN_WRAP && cb->vars[0].type.array_type
			&& (cb->vars[0].type.array_type->data == AIN_IFACE
			    || cb->vars[0].type.array_type->data == AIN_IFACE_WRAP);
		if (stride == 2 && (type == AIN_IFACE || type == AIN_IFACE_WRAP || wrapped_iface))
			return false;
		if (stride == 1 && (type == AIN_REF_INT || type == AIN_REF_FLOAT
		    || type == AIN_REF_BOOL || type == AIN_REF_LONG_INT)) {
			if (hll_self_slot < 0 || (size_t)hll_self_slot >= heap_size
			    || HEAP_REF(hll_self_slot) <= 0 || heap[hll_self_slot].type != VM_PAGE
			    || heap[hll_self_slot].page != *array)
				VM_ERROR("Array query: callback reference has no array owner");
			return true;
		}
	}
	VM_ERROR("Array query: unsupported callback argument type %d / stride %d", type, stride);
}

static bool array_query_callback_shape(struct page **array, int func, int stride)
{
	return array_callback_shape(array, func, stride, AIN_BOOL);
}

// Call the callback on one logical element and return its raw int result.
static int array_callback_call(struct page **array, int index, int stride, int func,
			       enum ain_data_type ret)
{
	struct ain_function *cb = &ain->functions[func];
	bool reference = array_callback_shape(array, func, stride, ret);
	struct page *before = *array;
	int size = before->nr_vars;
	int owner = hll_self_slot;
	bool tracked = owner >= 0 && (size_t)owner < heap_size && HEAP_REF(owner) > 0
		&& heap[owner].type == VM_PAGE && heap[owner].page == before;
	int saved_sp = stack_ptr;
	if (reference) {
		stack_push(owner);
		stack_push(index * stride);
	} else {
		stack_push(before->values[index * stride]);
		if (cb->nr_args == 2)
			stack_push(before->values[index * stride + 1]);
	}
	vm_call_nopop(func, cb->nr_args);
	int result = stack_pop().i;
	stack_ptr = saved_sp;
	// FFI passed a local page snapshot. A nested HLL can replace/free the
	// owner's page; refresh before touching it or outer FFI writes it back.
	if (tracked) {
		if ((size_t)owner >= heap_size || HEAP_REF(owner) <= 0 || heap[owner].type != VM_PAGE)
			VM_ERROR("Array query: callback released its array owner");
		*array = heap[owner].page;
	}
	if (*array != before || !*array || (*array)->type != ARRAY_PAGE || (*array)->nr_vars != size)
		VM_ERROR("Array query: callback changed array storage");
	return result;
}

static bool array_query_predicate(struct page **array, int index, int stride, int func)
{
	return array_callback_call(array, index, stride, func, AIN_BOOL) != 0;
}

/* ==== (B) Insert right before "Select by declared signature" (after
 * Array_FindIf; it needs array_query_value_type and array_erase_stride). ==== */

/* Sorted-range search: LowerBound / UpperBound / BinarySearch.
 *
 * Native engine (dohnadohna_dump_SCY.exe, Array dispatcher 0x644300):
 *   index 50 LowerBound(value)    0x644b33 -> 0x648e40 -> core 0x646c70
 *   index 51 LowerBound(func)     0x644b4e -> 0x648ef0 -> core 0x646c70
 *   index 52 UpperBound(value)    0x644b69 -> 0x648fa0 -> core 0x646cc0
 *   index 53 UpperBound(func)     0x644b84 -> 0x649050 -> core 0x646cc0
 *   index 54 BinarySearch(value)  0x644b9f -> 0x649100 -> core 0x646d10
 *   index 55 BinarySearch(func)   0x644bba -> 0x6491b0 -> core 0x646d10
 * All three cores run lo = 0, hi = length, mid = (lo + hi) / 2 over a
 * three-way comparator cmp(i) = sign(elem[i] - target):
 *   LowerBound:   cmp < 0 -> lo = mid + 1, else hi = mid; return lo.
 *   UpperBound:   cmp <= 0 -> lo = mid + 1, else hi = mid; return lo.
 *   BinarySearch: cmp == 0 -> return mid; cmp < 0 -> lo = mid + 1,
 *                 else hi = mid; miss returns -1.
 * An empty array returns 0 (bounds) or -1 (BinarySearch) before the
 * comparator is built, so the callback is never validated or called.
 *
 * The func overloads take a one-element comparator returning int; the key
 * is captured by the lambda (X_GETENV), never passed. Native wraps the
 * callback result without negation (functor 0x810eec -> 0x646590).
 * Every CN call site follows the same sign: IdArray@CompareFunc (fno 34982),
 * StringExtensions::Compare (fno 20957), FrameInfoCollection lambdas
 * (fno 36403, 36408) all return negative when the element sorts first.
 *
 * The value overloads accept only int, float and string elements (factory
 * 0x646290 switches on type 10/11/12, anything else raises an error):
 *   int    elem - target, 32-bit wrapping subtraction (0x64b9e0).
 *   float  -1 / 0 / 1 via comiss, NaN compares equal (0x64b950).
 *   string memcmp over the shorter length, then length (0x646440).
 */
enum array_bound_kind {
	ARRAY_LOWER_BOUND,
	ARRAY_UPPER_BOUND,
	ARRAY_BINARY_SEARCH,
};

struct array_bound_key {
	bool by_func;             // func overload: call the comparator
	int func;                 // comparator fno
	union vm_value value;     // value search target
	enum ain_data_type type;  // AIN_INT, AIN_FLOAT or AIN_STRING
	int stride;               // slots per logical element
};

static struct string *array_bound_string(int slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size || HEAP_REF(slot) <= 0
	    || heap[slot].type != VM_STRING || !heap[slot].s)
		VM_ERROR("Array.LowerBound: invalid string value");
	return heap[slot].s;
}

static int array_bound_compare(struct page **array, int index, const struct array_bound_key *k)
{
	if (k->by_func)
		return array_callback_call(array, index, k->stride, k->func, AIN_INT);
	union vm_value e = (*array)->values[index];
	switch (k->type) {
	case AIN_FLOAT:
		if (k->value.f > e.f)
			return -1;
		return e.f > k->value.f;
	case AIN_STRING: {
		const struct string *a = array_bound_string(e.i);
		const struct string *b = array_bound_string(k->value.i);
		int n = a->size < b->size ? a->size : b->size;
		int r = memcmp(a->text, b->text, n);
		if (r)
			return r < 0 ? -1 : 1;
		return (a->size > b->size) - (a->size < b->size);
	}
	default:
		return (int32_t)((uint32_t)e.i - (uint32_t)k->value.i);
	}
}

static int array_bound_search(struct page **array, enum array_bound_kind kind,
			      struct array_bound_key *k)
{
	const int miss = kind == ARRAY_BINARY_SEARCH ? -1 : 0;
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return miss;
	k->stride = k->by_func ? array_erase_stride(*array) : 1;
	int count = (*array)->nr_vars / k->stride;
	if (count <= 0)
		return miss;
	if (!k->by_func)
		k->type = array_query_value_type(*array);
	int lo = 0, hi = count;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		int c = array_bound_compare(array, mid, k);
		if (kind == ARRAY_BINARY_SEARCH && c == 0)
			return mid;
		if (kind == ARRAY_UPPER_BOUND ? c <= 0 : c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return kind == ARRAY_BINARY_SEARCH ? -1 : lo;
}

static int Array_LowerBoundValue(struct page **array, int value)
{
	struct array_bound_key k = { .by_func = false, .value = { .i = value } };
	return array_bound_search(array, ARRAY_LOWER_BOUND, &k);
}

static int Array_LowerBoundIf(struct page **array, int func)
{
	struct array_bound_key k = { .by_func = true, .func = func };
	return array_bound_search(array, ARRAY_LOWER_BOUND, &k);
}

static int Array_UpperBoundValue(struct page **array, int value)
{
	struct array_bound_key k = { .by_func = false, .value = { .i = value } };
	return array_bound_search(array, ARRAY_UPPER_BOUND, &k);
}

static int Array_UpperBoundIf(struct page **array, int func)
{
	struct array_bound_key k = { .by_func = true, .func = func };
	return array_bound_search(array, ARRAY_UPPER_BOUND, &k);
}

static int Array_BinarySearchValue(struct page **array, int value)
{
	struct array_bound_key k = { .by_func = false, .value = { .i = value } };
	return array_bound_search(array, ARRAY_BINARY_SEARCH, &k);
}

static int Array_BinarySearchIf(struct page **array, int func)
{
	struct array_bound_key k = { .by_func = true, .func = func };
	return array_bound_search(array, ARRAY_BINARY_SEARCH, &k);
}

/* ==== (C) Remove the old Array_BinarySearch (Array.c:1644-1662),
 * Array_LowerBound (1664-1678) and Array_UpperBound (2378-2392), then
 * repoint the export table. The names must stay in the table because
 * link_static_library() matches by name before calling the selector:
 *     HLL_EXPORT(BinarySearch, Array_BinarySearchValue),
 *     HLL_EXPORT(LowerBound, Array_LowerBoundValue),
 *     HLL_EXPORT(UpperBound, Array_UpperBoundValue),
 * ==== */
```

## selector
```c
/* src/hll/Array.c: in array_select_function(), directly after the existing
 *     if (!strcmp(f->name, "First"))
 *         return has_func ? (void*)Array_First : (void*)Array_First_NoPred;
 * AIN_REF_ARRAY_TYPE is a case-label macro, so it must stay in a switch. */
	/* Sorted-range search: value and comparator overloads share a name. */
	bool bound = !strcmp(f->name, "LowerBound") || !strcmp(f->name, "UpperBound")
		|| !strcmp(f->name, "BinarySearch");
	if (bound) {
		if (!f->arguments || f->nr_arguments != 2 || f->return_type.data != AIN_INT)
			return NULL;
		switch (f->arguments[0].type.data) {
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:
			break;
		default:
			return NULL;
		}
		if (!has_func && f->arguments[1].type.data != AIN_HLL_PARAM)
			return NULL;
		if (!strcmp(f->name, "LowerBound"))
			return has_func ? (void *)Array_LowerBoundIf : (void *)Array_LowerBoundValue;
		if (!strcmp(f->name, "UpperBound"))
			return has_func ? (void *)Array_UpperBoundIf : (void *)Array_UpperBoundValue;
		return has_func ? (void *)Array_BinarySearchIf : (void *)Array_BinarySearchValue;
	}

/* src/ffi.c link_static_library(): widen the existing First branch so the
 * selector sees these three names as well. */
				if (!strcmp(lib->name, "Array") && (!strcmp(ainlib->functions[i].name, "First")
				    || !strcmp(ainlib->functions[i].name, "LowerBound")
				    || !strcmp(ainlib->functions[i].name, "UpperBound")
				    || !strcmp(ainlib->functions[i].name, "BinarySearch"))) {
					extern void *array_select_function(const struct ain_hll_function *f);
					funcptr = array_select_function(&ainlib->functions[i]);
				}
```

## fixture_plan
已寫好並實跑。檔案：scratchpad/overload/bsearch/h/probe/bound_overload_fixture.inc。harness 是 scratchpad 內的副本，runtime_probe.c 只加了 include 與 "bound-overload" 模式；原 harness 未改動。

一、值版，用 hll_call 直接呼叫，arg3=1，int 陣列 {1,3,3,5}：
- LowerBound#0（decl 50）：key 0/1/3/4/5/6 預期 0/0/1/3/3/4。
- UpperBound#0（decl 52）：預期 0/1/3/3/4/4。
- BinarySearch#0（decl 54）：預期 -1/0/2/-1/3/-1。key=3 回 2 是因為原版半開二分先探到 mid=2。
- 空陣列（NULL page）：LowerBound 預期 0，BinarySearch 預期 -1。

二、comparator 版，走真 bytecode（lambda 需要 X_GETENV env，所以不用 hll_call 直呼）：
- alloc_struct(ain_get_struct("IdSet<string>"))，依序對 "d","b","a","c","b" 執行 call_method_args(35058 Set)。Set 內部會跑 Exists（BinarySearch#1，lambda 37474），再跑 LowerBound#1（lambda 37473）與 Insert。預期 m_list 為 ["a","b","c","d"]，共 4 筆，重複的 "b" 被擋下。
- 35059 Exists：\"a\"/\"c\"/\"d\" 預期 true；\"bb\"/\"0\"/\"z\" 預期 false。

三、enum 元素，arg3=1：
- IdSet<GameDialogType>：35342 Set 依序插入 5,1,3,1，預期 {1,3,5}。
- 35343 Exists：1/3/5 預期 true，2 預期 false。

結果：
- baseline（HEAD a8d92df 的 Array.c + ffi.c）rc=89，10 項 FAIL：m_list 為 d b a c b；Exists 全為 false；enum 版 m_list 為 5 1 3 1；BinarySearch#0 key=3 回 1。
- patched rc=0，0 項 FAIL。

回歸：patched 版跑既有 first-overload、observer、reentrancy、personality、assignment、heap-reuse、click、metadata、array-reinit、deleted-event，rc 與輸出末行和 baseline 完全相同（deleted-event 兩版都是 87）。

尚未覆蓋：
- 兩槽 iface 元素（arg3=65539，ResourceInfoCollection@Add、CKeyDataList@Insert）。
- wrap 元素（arg3=65538，ExSaverTree、FrameInfo）。
建議下一步用 ExSaverTreeNode@AddNode/IsExistNode 做 wrap 版 fixture。

## open
- 兩槽 iface 元素的 comparator 路徑只做了靜態核對：ResourceInfoCollection@Add 的 LowerBound#1（fno 23862，arg3=65539，ARG0 為 wrap<iwrap<IResourceInfo>>，ARG1 為 void）和 CKeyDataList@Insert（fno 23985，IKeyData）沿用 Find 既有的 stride==2 分支（array_erase_stride 要求 a_type 為 AIN_ARRAY/REF_ARRAY、struct_type>1 且 etype 為 3），尚未用 headless 實跑驗證。
- wrap 元素（arg3=65538：ExSaverTree、FrameInfo、MapNode）尚未 headless 驗證。Rufim 分支註解提到 FrameInfoCollection@GetIndexFromTime 在實機會 BinarySearch 失手（§5dy），可能與 FrameInfo 區間資料有關，不一定是本修正的範圍，需要實機追蹤。
- 值版型別判定沿用 array_query_value_type，它把 bool/enum 當成 int 放行，比原版寬鬆（原版值工廠 0x646290 遇到型別碼 10/11/12 以外會報錯）；錯誤訊息仍寫 Array.Find。CN 的 LowerBound#0 只有 int 陣列，實際上沒有影響。
- array_callback_shape 的 VM_ERROR 文字從 predicate 改成 callback；如果有測試或 log 比對依賴舊字串，需要同步修改。
- 原版 0x6473b0 在陣列物件為 null 時走錯誤路徑並回 0（BinarySearch 也回 0，見 0x649137）。xsystem4 的 NULL page 代表空陣列，因此我採用原版 length==0 分支的語義（bounds 回 0、BinarySearch 回 -1），沒有模擬 null 物件的錯誤路徑。

## artifacts
- <scratchpad>/overload/bsearch/bound-overload.patch
- <scratchpad>/overload/bsearch/Array.patched.c
- <scratchpad>/overload/bsearch/ffi.patched.c
- <scratchpad>/overload/bsearch/proposed_callback.c
- <scratchpad>/overload/bsearch/proposed_bound.c
- <scratchpad>/overload/bsearch/selector.c
- <scratchpad>/overload/bsearch/h/probe/bound_overload_fixture.inc
- <scratchpad>/overload/bsearch/build.py
- <scratchpad>/overload/bsearch/run-baseline.log
- <scratchpad>/overload/bsearch/run-patched.log
- <scratchpad>/overload/bsearch/sites.txt
- <scratchpad>/overload/bsearch/idx50-55.asm
- <scratchpad>/overload/bsearch/helpers50-55.asm
- <scratchpad>/overload/bsearch/cores.asm
- <scratchpad>/overload/bsearch/predfactory.asm

## 驗證 refuted=False residual=0.9
語義面我逐條獨立驗證，沒有推翻。
(1) EXE 反組譯，dump_SCY.exe，capstone：
- 核心 0x646c70：0x646ca3 做 test eax,eax 再 jns，負數時 lo=mid+1（0x646ca7），否則 hi=mid，最後回傳 lo。
- 0x646cc0：0x646cf5 做 jg，>0 時 hi=mid，<=0 時 lo=mid+1。
- 0x646d10：0x646d45 je 時回傳 mid（0x646d5e），jns 時 hi=mid，負數時 lo=mid+1，找不到時 0x646d57 or eax,-1。
- 三個核心都是 xor lo、hi=size 的半開區間，mid=(lo+hi)/2 用 cdq/sar。
- predicate 工廠 0x646570 設定 vtable 0x810eec。vtable 的 slot2 是 0x64b880，它呼叫 0x646590，0x646590 直接回傳 0x659f20 的 int，沒有取負。
- 值版 int comparator 0x64b9e0：elem(0x6451d0) sub target(0x659f20)，也就是 elem-target。
- float 0x64b950：comiss 的語義是 target>elem 回 -1，elem>target 回 1，unordered 回 0。
- string 0x646440：先以 0x41b480 做 unsigned byte 比較（sbb），前綴相同再比長度，較短的回 -1。三者都是 sign(elem-target)。
- 入口 0x648ef0 與 0x6491b0：size<=0 時分別回 0 與 -1。null 物件時 0x6473b0 走錯誤路徑，兩者都回 0。
(2) AIN：我用 python 掃描 60 個 #1 呼叫點的 lambda header，全部是 RETURN int，參數型別分布與調查者所列相符（arg3=1:2、2:41、65538:15、65539:2）。
(3) 呼叫端：IdArray@Add/FindIndex/IsExist（dump 1695893-1696075）和摘要描述一致。
(4) 我另外補了一條獨立於「Add/Find 自洽」的符號證據：StandNameFinder 建構時用 QuickSort#1(lhs S_LT rhs) 升冪排序（dump 1082463-1082480），GetFromStandName 的 lambda 在 obj<target 時回 -1（dump 1083012-1083050）。所以原版一定是「cmp<0 往右」且不取負，否則升冪陣列查不到。
程式碼面有兩個問題：主要缺陷是 2 槽 iface 路徑，它和 Array_Insert 不支援 2 槽的現況疊加後會致命；次要是值版型別判定與註解錯誤、selector 回 NULL 的退化。其餘 stack、ref、hll_func_obj 慣例都和既有 Find/Count 路徑相同，我沒有找到錯誤。

### code_bugs
- [高，致命退化風險] 兩槽 iface 元素的 comparator 路徑（LowerBound#1 arg3=65539：fno 23862 @dump 238792、fno 23985 @dump 296331）在現有儲存層下不可能正確，而且提議程式碼會把原本的「靜默垃圾值」變成 VM_ERROR。這兩處把 LowerBound 的結果交給 `CALLHLL Array Insert 65539`（dump 238809；全 AIN 共 5 處 Insert 65539）。Array_Insert（src/hll/Array.c:746-767）不處理 2 槽元素：hll_param_slot2 被丟掉，每個元素只插 1 槽，index 也沒乘 stride；陣列從 NULL 建立時 a_type=AIN_ARRAY_INT（Array.c:757）。因此 array_erase_stride（Array.c:602-608）回傳 1。第二次 Add 會走進 array_bound_compare → array_callback_call → array_callback_shape，條件是 nr_args=2、vars[1]=void、stride=1、type=AIN_WRAP(iface) 或 AIN_IFACE，結果掉到最後的 VM_ERROR("unsupported callback argument type")。如果 struct_type 碰巧 >1，stride 會變成 struct_type，元素配對就錯位。HEAD 的 Array_LowerBound(self, 23862) 只會回垃圾位置（多半是 append），不會中止。建議 array_bound_search 在 by_func 且 shape 不成立時先保守退回舊行為，或者在修好 Insert 的 2 槽支援之前，不要讓 selector 對 arg3 為 2 槽的呼叫選 If 版。可達性（DebugResourceInfoCollection@_Load fno 3393 → NEW ResourceInfoCollection @dump 238384；elkeditor CKeyDataList @dump 290562）：未驗證。
- [中，註解與「未解問題」陳述錯誤] 提議的 (B) 註解寫 value overload「only int, float and string (factory 0x646290 switches on type 10/11/12)」，這是錯的。我讀了 0x6463e4 的 byte table，型別 10、47(AIN_BOOL)、92(AIN_ENUM) 都映射到 case 0（vtable 0x810f40，slot2=0x64b9e0 整數減法），11→0x810df0（float 0x64b950），12→0x810db8（string），只有其他型別（例如 55 LONG_INT、91 ENUM2）才走 0x646320 錯誤路徑。所以 array_query_value_type 放行 bool/enum 和原版一致，只有 ENUM2 比原版寬鬆。
- [中，潛在] 值版型別只看 a_type：PushBack 從 NULL 建立的陣列 a_type 固定是 AIN_ARRAY_INT（Array.c:159）。這樣一來 string 陣列（arg3=2）會在 array_query_value_type（Array.c:1965-1983）因 array_elem_is_ref 觸發 VM_ERROR；float 陣列會被當成 int，用位元樣式相減，負數排序錯誤。CN 的 LowerBound#0 只有 2 處，都是 int 陣列（dump 498406/498450，arg3=1），BinarySearch#0 和 UpperBound 都沒有呼叫處，所以目前不會觸發。但註解宣稱的 float/string 忠實度實際上達不到。
- [低] 提議的 selector 在簽名不符時回傳 NULL（`!f->arguments || nr_arguments != 2 || return_type != INT`，或 !has_func 且 arg1 不是 HLL_PARAM）。ffi.c:1181 會因此不連結這些函式，執行時走 UNIMPL 路徑並 push 0（ffi.c:376-379）。BinarySearch 回 0 等於「在 index 0 命中」，比回 -1 更危險。建議改成回傳 Value 版或 VM_ERROR，而不是 NULL。CN 宣告都符合，所以本遊戲不受影響。
- [低，範圍外但影響「查找修好」的宣稱] StandNameFinder@GetFromStandName 的 BinarySearch#1（fno 36473 @dump 1083057）依賴 m_standInfo 已經依名稱排序：建構時用 QuickSort#1(lambda fno 36461：lhs.m29890() S_LT rhs.m29890()，@dump 1082463-1082480)。但 Array_QuickSort（Array.c:817-826）忽略 comparator，改用 heap slot 數值排序；libraries.txt:127-128 又宣告它回傳 wrap<?>，C 端卻是 void。所以這個 patch 之後 StandNameFinder 仍然查不到。
- [低，文字錯誤] 摘要寫 IdArray「14 組共 28 處」，實際是 17 種型別共 34 處：LowerBound#1 arg3=2 共 20 處，扣掉 ExTable 與 2 個 string 後剩 17；BinarySearch#1 arg3=2 共 21 處，扣掉 ExTable、StandNameFinder 與 2 個 string 後也是 17。這不影響程式碼。
- [資訊] 已確認沒有問題的項目：push 槽數（值型別與 ref struct 1 槽、iface 2 槽照 Find 慣例）、vm_call_nopop 對 AIN_REF_STRUCT/STRING/WRAP 的 heap_ref 與 callee 釋放平衡（vm.c:1718-1769）、stack_ptr=saved_sp 清掉殘留參數、function_return 以 base_sp 強制單一 int 回傳、hll_func_obj 與 hll_self_slot 和 arg3 由 ffi.c:397-405 與 802 在巢狀 HLL 前後保存還原、空陣列不驗證也不呼叫 callback、半開二分與原版核心逐指令一致。60 個 lambda 的 return_type 全為 int，參數型別（ref struct、string、wrap<T>、enum#92、iface+void）都落在 array_callback_shape 允許的分支。

### corrected
同意。LowerBound#1、UpperBound#1、BinarySearch#1 都是單元素三向 comparator：lambda(elem) 回傳 int，符號為 sign(elem-target)，不取負。三者都用半開二分，lo=0、hi=len、mid=(lo+hi)/2：
- LowerBound：cmp<0 時 lo=mid+1。
- UpperBound：cmp<=0 時 lo=mid+1。
- BinarySearch：cmp==0 時回傳 mid，找不到回 -1。
空陣列時 bounds 回 0、BinarySearch 回 -1，都不呼叫 callback。

值版的補充修正：原版值工廠接受型別 10(int)、47(bool)、92(enum)，三者走 32-bit wrapping 的 elem-target；11(float) 用 comiss，NaN 視為相等；12(string) 以 unsigned byte 比較較短長度，再比長度，而且元素字串先以 strlen 轉成 std::string，嵌入的 NUL 會截斷。其他型別（含 55 LONG_INT、91 ENUM2）走錯誤路徑。
證據：EXE VA 0x646c70/0x646cc0/0x646d10、0x810eec→0x64b880→0x646590、0x6463e4 byte table、0x64b9e0/0x64b950/0x646440/0x41b480。

### counter_evidence
唯一與調查摘要相悖的事實只有兩點，都不推翻主語義：
(a) 值工廠 0x646290 的 byte table（0x6463e4）把型別 47(bool) 和 92(enum) 也映射到 int comparator（0x810f40→0x64b9e0）。所以「原版只接受 int/float/string」「bool/enum 比原版寬鬆」這兩句是錯的。
(b) StandNameFinder 的 BinarySearch#1 在 xsystem4 仍會失手，原因是 Array_QuickSort（Array.c:817-826）忽略 comparator，不是本修正本身的語義錯誤。
另外，LowerBound#1 arg3=65539 的兩處（fno 23862/23985）因為 Array_Insert（Array.c:746-767）不支援 2 槽元素，提議實作可能從 HEAD 的靜默垃圾值退化成 VM_ERROR；這條是否實際可達，未驗證。
