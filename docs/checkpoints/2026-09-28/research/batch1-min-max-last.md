# min-max-last

## 調查摘要
結論：Max/Min/Last 的 12 個宣告中，除了 Last#0/#1 以外全部綁錯，而且 CN 有實際呼叫的 5 種全部出錯。已用 SCY EXE 反組譯確認原版語義。

1) Max#1（2 處）→ Array_Max(array, func)：和 First 同一型缺陷。CIF 只有 1 個參數，func 讀的是暫存器殘值。殘值剛好落在 [0,nr_functions) 時，會把元素 push 給任意 AIN 函式去跑；否則走 int 路徑，用原始 int 值比較（string 陣列比的是 heap slot 編號；checkpoint 2026-09-28/first-overload/after-ff6fc2f.txt:3-5 裡 Max#1 每輪回傳 gamma/beta/alpha 不定，就是證據）。另外平手取第一個（原版取最後一個）；v14 value 分支沒有 heap_ref(hll_self_slot)，Array_At 有；空陣列的 ref 分支 push 0，不是 -1。

2) Min#1（1 處）→ Array_Min(array)：函式回傳 int 最小值，一個 slot 都沒 push。宣告是 ref hll_param，ffi 會丟掉 C 回傳值（ffi.c:791），所以呼叫端 X_MOV/X_ASSIGN 會吃到錯位的 stack。唯一呼叫處 ain_code.txt:829477 跑在 array<float> 上，而且 hll_arg3=1，所以不能拿 arg3 區分 int 和 float。
   Min#3（12 處）綁到同一個函式：comparator 被忽略，同樣不 push，stack 損壞。

3) Max#3（14 處）→ Array_Max 把 comparator 當成 1 參數 key scorer：push (elem, 0) 當成 (lhs, rhs)，rhs 是 slot 0。bool 結果被當分數用 `>` 比，結果錯誤，甚至可能崩潰。

4) Last#3（7 處）→ Array_Last(self)：predicate 被忽略，一律回傳最後一個元素。影響 BattleContext@CalcBattleResult、FrameInfoCollection 三處、FrameDamageCalculator、CMessageTextModel@GetLastVoiceName 等戰鬥和訊息核心路徑。

5) 呼叫處 lambda 形狀：Min#3/Max#3 的 26 個呼叫處全部是 2 參數 comparator，形式為 (lhs, rhs) -> bool，body 都是 key(lhs) < key(rhs)（LT 或 S_LT）；Max 也傳 less，不是 greater。Last#3 的 7 處全部是 1 參數 predicate -> bool。

6) EXE 語義（dispatcher 0x644300；Last 71-74、Min 75-78、Max 79-82；#0/#1、#2/#3 經薄 wrapper 走同一個 helper）：
   - Min loop 0x646af0：if less(e[j], e[best]) best = j → 平手取第一個最小值。
   - Max loop 0x646b50：if !less(e[j], e[best]) best = j → 平手取最後一個最大值。這點和 Rufim 及 std::max_element 都不同。
   - comparator 以 lambda(lhs=e[j], rhs=e[best]) 呼叫（參數表由 0x645c80 依序建立）。
   - 無 pred 的預設 less（0x645dd0）：int/bool 用 signed <，float 用 <，string 用 unsigned byte strcmp；其他型別只記一筆 log（「配列ソート出來ない型」），less 永遠為 false。
   - Last(pred)：0x648c80 → 0x646c10，從 n-1 往回掃，第一個命中就回傳，找不到回傳 -1。
   - 空陣列和 -1 都由 0x6499e0 轉成 null ref。

提案：新增 Array_MinNoPred / Array_MaxNoPred / Array_MinPredicate / Array_MaxPredicate / Array_LastPredicate，以及 Array_LastNoPred（邏輯索引版）。結果一律交給 Array_At 產生 ref 契約。predicate 沿用 array_query_callback_shape/array_query_predicate；comparator 另寫 shape 檢查和 less 呼叫，owner 的 refresh 規則照 query 路徑。array_select_function 擴充到 Last/Min/Max，形狀不符時退回舊綁定；ffi.c:1172 的條件也要一起擴充。已在 scratch 副本套用並用專案 include/flags 跑 clang -fsyntax-only -Wall -Wextra：0 error，只有既有的 Array_AnyPredicate unused 警告。runtime fixture 尚未執行（未驗證）。

## lambda
全部取自 <cn-dump>/ain_code.txt。「行號」是 CALLHLL 那一行，fno 是緊接在前的 PUSH。完整抽取結果見 scratch sites.txt。

[Min#3，12 處：全部是 (lhs, rhs) -> bool，body 為 key(lhs) < key(rhs)]
- 355090 fno 24300：ref CEmitterKey x2 -> bool，method 5380 LT；arg3=2
- 355152 fno 24301：ref CEmitterKey x2，method 5389 LT；arg3=2
- 1045828 fno 36303：wrap<BattlePlayer> x2，method 28537 LT；arg3=65538（GetPlayerAfterEmptyPos）
- 1046373 fno 36310：wrap<BattlePlayer> x2，28537 LT；65538（InnerAlign）
- 1089761 fno 36502：wrap<MapNode> x2，Pos.get 之後取 member0，LT；65538（LoadNode）
- 1095955 fno 36539：ref BattleVoicePlayingInfo x2，member1 LT；arg3=2
- 1097634 fno 36548：wrap<SaveObject> x2，Param.get 後 method 30492，S_LT（字串比較）；65538（DeleteOldAutoSave）
- 1199157 fno 36876：wrap<WorkerEventHint> x2，30433 LT；65538
- 1246293 fno 37050 / 1246305 fno 37051：ref CASPos x2，member0 / member1 LT；arg3=2（MapEdgeView@GetMinPos）
- 1251250 fno 37071 / 1251262 fno 37072：同上，MiniMapEdgeView

[Max#3，14 處：同樣是 less (lhs < rhs)，沒有任何 greater]
- 285279 fno 23961：ref CEmitterData x2，4955 LT；65538
- 355214 fno 24302：ref CEmitterKey x2，5389 LT；2
- 1017248 fno 36155：ref Equipment x2，27647 LT；2
- 1026053 fno 36203：wrap<Weapon> x2，30401 LT；65538
- 1031119 fno 36226：wrap<Worker> x2，28012 LT；65538（GetEmptyOrder）
- 1046336 fno 36309：wrap<BattlePlayer> x2，28537 LT；65538
- 1050753 fno 36337：wrap<BattlePlayer> x2，28537 LT；65538
- 1059289 fno 36387：ref MatchingResult x2，member1 LT；2
- 1089835 fno 36503 / 1089918 fno 36504：wrap<MapNode> x2，Pos member0 / member1 LT；65538
- 1246372 fno 37052 / 1246384 fno 37053：ref CASPos x2，member0 / member1 LT；2
- 1251329 fno 37073 / 1251341 fno 37074：同上，MiniMapEdgeView

[Last#3，7 處：全部是 1 參數 predicate，回傳 bool]
- 421796 fno 24394：ref message::detail::CMessageText，member0 == 1；arg3=2
- 470743 fno 24706：int frame，frame < env[3]（X_GETENV closure）；arg3=1
- 1034650 fno 36251：wrap<PlayerAction>，Player.get 後呼叫 method 28526；65538
- 1054104 fno 36357：ref FrameDamage，回傳 member0；arg3=2
- 1062830 fno 36397 / 1063465 fno 36399 / 1063770 fno 36401：wrap<FrameInfo>，iface member 8/5/9 的 method；65538

[無 pred]
- Min#1 829477：array<float>，arg3=1
- Max#1 1021218、1059613：array<int>，arg3=1

所有呼叫端都以 `.LOCALREF <dummy>; DELETE; ... X_MOV 3 2; X_ASSIGN 1` 消費 1 個 ref，並以 PUSH -1 EQUALE 檢查 null。
EXE 參數順序：loop 0x646af0 / 0x646b50 先 push &best、再 push &j，functor op(p1=&j, p2=&best)；0x645c80 依序加入 e[p1]、e[p2]，所以 lambda 收到 lhs=e[j]、rhs=e[best]。

## proposed_code
```c
/*
 * 插入位置：src/hll/Array.c 的 array_select_function（目前第 2045 行註解之前）。
 * 這個 block 依賴 Array_At(429)、array_erase_stride(602)、
 * array_query_callback_shape(1879)、array_query_predicate(1916)，都已經定義在前面。
 *
 * Array.Last / Min / Max overloads.
 *
 * The CN AIN declares each name four times (libraries.txt Array 71-82):
 *   Last(self) x2, Last(self, hll_func) x2, Min(...) x4, Max(...) x4.
 * Name-only linking bound all of them to one C function, so the CIF built
 * from the declaration disagreed with the C prototype (see First, ff6fc2f).
 *
 * Native reference: dohnadohna_dump_SCY.exe, Array dispatcher 0x644300,
 * jump table 0x644f18. Paired declarations (#0/#1, #2/#3) reach the same
 * helper through thin wrappers (e.g. 72 -> 0x649f20 -> 0x649ec0).
 *   Last()      71/72 -> 0x649ec0: index numof-1, result via 0x6499e0
 *   Last(pred)  73/74 -> 0x649f40 -> 0x648c80 -> 0x646c10:
 *               scan from numof-1 down to 0, first hit wins, else -1
 *   Min()       75/76 -> 0x649fb0: default less 0x645dd0 + loop 0x646af0
 *   Min(less)   77/78 -> 0x64a070: delegate less 0x645f70 + loop 0x646af0
 *   Max()       79/80 -> 0x64a140: default less 0x645dd0 + loop 0x646b50
 *   Max(less)   81/82 -> 0x64a200: delegate less 0x645f70 + loop 0x646b50
 * Loop 0x646af0: best = 0; for j in 1..n-1: if less(e[j], e[best]) best = j
 *   -> first minimum on ties.
 * Loop 0x646b50: best = 0; for j in 1..n-1: if !less(e[j], e[best]) best = j
 *   -> LAST maximum on ties (not std::max_element).
 * less(a, b) calls the lambda as lambda(lhs = e[a], rhs = e[b]) (arg list
 * built by 0x645c80 in that order). Every CN Min/Max site passes a
 * 2-argument bool lambda "key(lhs) < key(rhs)" (fno 24300, 36303, 37050...).
 * Default less (0x645dd0) switches on element type: int/bool signed <
 * (0x64bef0), float < (0x64be70), string unsigned byte strcmp < 0
 * (0x64bdc0). Any other type logs "cannot sort type" and uses an
 * always-false less (0x49c5c0), so Min() yields e[0] and Max() e[n-1].
 * An empty array returns index 0 or -1, which 0x6499e0 turns into a null ref.
 *
 * Results use Array_At, which owns the ref hll_param contract: 2 slots
 * [owner, index] (owner retained on v14) for value elements, 1 retained
 * heap slot for ref elements, typed null when the index is out of range.
 */

enum array_order_kind {
	ARRAY_ORDER_NONE,
	ARRAY_ORDER_INT,
	ARRAY_ORDER_FLOAT,
	ARRAY_ORDER_STRING,
};

// Element type for the default less. hll_arg3 is 1 for both int and float
// (Min#1 at ain_code.txt:829477 runs on array<float>), so the page type
// decides; an erased or struct type falls back to the native always-false.
static enum array_order_kind array_order_value_kind(const struct page *a)
{
	if (a->array.rank > 1 || array_elem_is_2slot())
		return ARRAY_ORDER_NONE;
	switch (a->a_type) {
	case AIN_INT: case AIN_ARRAY_INT: case AIN_BOOL: case AIN_ARRAY_BOOL:
	case AIN_ENUM: case AIN_ENUM2:
		// Generic allocation can default to ARRAY_INT even for heap elements.
		return array_elem_is_ref() ? ARRAY_ORDER_NONE : ARRAY_ORDER_INT;
	case AIN_FLOAT: case AIN_ARRAY_FLOAT:
		return ARRAY_ORDER_FLOAT;
	case AIN_STRING: case AIN_ARRAY_STRING:
		return ARRAY_ORDER_STRING;
	default:
		return ARRAY_ORDER_NONE;
	}
}

static const char *array_order_text(int slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size || HEAP_REF(slot) <= 0
	    || heap[slot].type != VM_STRING || !heap[slot].s)
		return "";
	return heap[slot].s->text;
}

static bool array_order_value_less(const struct page *a, int lhs, int rhs,
				   enum array_order_kind kind)
{
	union vm_value x = a->values[lhs], y = a->values[rhs];
	switch (kind) {
	case ARRAY_ORDER_INT:
		return x.i < y.i;
	case ARRAY_ORDER_FLOAT:
		return x.f < y.f;
	case ARRAY_ORDER_STRING:
		// strcmp compares as unsigned char, matching the native byte loop.
		return strcmp(array_order_text(x.i), array_order_text(y.i)) < 0;
	default:
		return false;
	}
}

// Validate a Min/Max comparator. Returns true when elements are passed as
// by-reference primitives ([owner, index] per argument, 4 slots total).
static bool array_order_callback_shape(struct page **array, int func, int stride)
{
	if (func < 0 || func >= ain->nr_functions)
		VM_ERROR("Array.Min/Max: invalid comparator %d", func);
	struct ain_function *cb = &ain->functions[func];
	if (cb->address >= ain->code_size || cb->return_type.data != AIN_BOOL
	    || !cb->vars || cb->nr_vars < cb->nr_args || stride != 1)
		VM_ERROR("Array.Min/Max: unsupported comparator signature %d / stride %d", func, stride);
	if (cb->nr_args == 2) {
		for (int k = 0; k < 2; k++) {
			switch (cb->vars[k].type.data) {
			case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
			case AIN_ENUM: case AIN_ENUM2: case AIN_STRING: case AIN_REF_STRING:
			case AIN_STRUCT: case AIN_REF_STRUCT: case AIN_WRAP:
				break;
			default:
				VM_ERROR("Array.Min/Max: unsupported comparator argument type %d",
					 cb->vars[k].type.data);
			}
		}
		return false;
	}
	enum ain_data_type t = cb->nr_args == 4 ? cb->vars[0].type.data : AIN_VOID;
	if ((t == AIN_REF_INT || t == AIN_REF_FLOAT || t == AIN_REF_BOOL || t == AIN_REF_LONG_INT)
	    && cb->vars[1].type.data == AIN_VOID && cb->vars[2].type.data == t
	    && cb->vars[3].type.data == AIN_VOID) {
		if (hll_self_slot < 0 || (size_t)hll_self_slot >= heap_size
		    || HEAP_REF(hll_self_slot) <= 0 || heap[hll_self_slot].type != VM_PAGE
		    || heap[hll_self_slot].page != *array)
			VM_ERROR("Array.Min/Max: comparator reference has no array owner");
		return true;
	}
	VM_ERROR("Array.Min/Max: unsupported comparator shape %d (%d args)", func, cb->nr_args);
}

// less(e[lhs], e[rhs]) through the game's lambda. Same stack and owner
// refresh discipline as array_query_predicate.
static bool array_order_less(struct page **array, int lhs, int rhs, int func, bool reference)
{
	struct ain_function *cb = &ain->functions[func];
	struct page *before = *array;
	int size = before->nr_vars;
	int owner = hll_self_slot;
	bool tracked = owner >= 0 && (size_t)owner < heap_size && HEAP_REF(owner) > 0
		&& heap[owner].type == VM_PAGE && heap[owner].page == before;
	int saved_sp = stack_ptr;
	if (reference) {
		stack_push(owner);
		stack_push(lhs);
		stack_push(owner);
		stack_push(rhs);
	} else {
		stack_push(before->values[lhs]);
		stack_push(before->values[rhs]);
	}
	vm_call_nopop(func, cb->nr_args);
	bool less = stack_pop().i != 0;
	stack_ptr = saved_sp;
	if (tracked) {
		if ((size_t)owner >= heap_size || HEAP_REF(owner) <= 0 || heap[owner].type != VM_PAGE)
			VM_ERROR("Array.Min/Max: comparator released its array owner");
		*array = heap[owner].page;
	}
	if (*array != before || !*array || (*array)->type != ARRAY_PAGE || (*array)->nr_vars != size)
		VM_ERROR("Array.Min/Max: comparator changed array storage");
	return less;
}

// Index of the extreme element, or -1 for a missing/empty array.
static int array_order_extreme(struct page **array, bool has_func, int func, bool want_max)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return -1;
	int stride = array_erase_stride(*array);
	int n = (*array)->nr_vars / stride;
	if (n <= 0)
		return -1;
	bool reference = false;
	enum array_order_kind kind = ARRAY_ORDER_NONE;
	if (has_func) {
		reference = array_order_callback_shape(array, func, stride);
	} else {
		kind = array_order_value_kind(*array);
		if (kind == ARRAY_ORDER_NONE) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				WARNING("Array.%s: element type %d has no default order (native: always-false less)",
					want_max ? "Max" : "Min", (*array)->a_type);
			}
		}
	}
	int best = 0;
	for (int j = 1; j < n; j++) {
		bool less = has_func
			? array_order_less(array, j, best, func, reference)
			: array_order_value_less(*array, j * stride, best * stride, kind);
		if (want_max ? !less : less)
			best = j;
	}
	return best;
}

static int Array_MinNoPred(struct page **array)
{
	return Array_At(array, array_order_extreme(array, false, 0, false));
}

static int Array_MaxNoPred(struct page **array)
{
	return Array_At(array, array_order_extreme(array, false, 0, true));
}

static int Array_MinPredicate(struct page **array, int func)
{
	return Array_At(array, array_order_extreme(array, true, func, false));
}

static int Array_MaxPredicate(struct page **array, int func)
{
	return Array_At(array, array_order_extreme(array, true, func, true));
}

// Last without a predicate, in logical elements. Identical to Array_Last for
// 1-slot elements; for 2-slot (iface/option) arrays Array_Last indexes the
// physical last slot (nr_vars - 1), which is the second half of an element.
static int Array_LastNoPred(struct page **array)
{
	struct page *a = (array && *array) ? *array : NULL;
	if (!a || a->type != ARRAY_PAGE)
		return Array_At(array, -1);
	return Array_At(array, a->nr_vars / array_erase_stride(a) - 1);
}

// Last(pred): last element whose predicate is true (native 0x646c10 scans
// backwards and stops at the first hit). Predicate ABI is shared with
// First/Find/Count via array_query_callback_shape.
static int Array_LastPredicate(struct page **array, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return Array_At(array, -1);
	int stride = array_erase_stride(*array);
	int n = (*array)->nr_vars / stride;
	if (n <= 0)
		return Array_At(array, -1);
	array_query_callback_shape(array, func, stride);
	for (int i = n - 1; i >= 0; i--) {
		if (!*array || (*array)->type != ARRAY_PAGE || i >= (*array)->nr_vars / stride)
			break;
		if (array_query_predicate(array, i, stride, func))
			return Array_At(array, i);
	}
	return Array_At(array, -1);
}
```

## selector
```c
/* (1) src/hll/Array.c：以下內容取代現有 array_select_function（Array.c:2045-2057），並放在上面 proposed_code block 之後。
 * Array_Max / Array_Min 定義在更後面（2156 / 2321），所以需要前置宣告。 */
static int Array_Max(struct page **array, int func);
static int Array_Min(struct page **array);

/* Select by declared signature, never by a game's function index. */
// Select First/Last/Min/Max from the declared argument list. Unknown
// shapes keep the legacy binding so other AINs do not lose the link.
void *array_select_function(const struct ain_hll_function *f)
{
	bool has_func = false;
	for (int i = 0; i < f->nr_arguments; i++) {
		enum ain_data_type t = f->arguments[i].type.data;
		if (t == AIN_HLL_FUNC || t == AIN_HLL_FUNC_71)
			has_func = true;
	}
	if (!strcmp(f->name, "First"))
		return has_func ? (void*)Array_First : (void*)Array_First_NoPred;
	bool shape = f->nr_arguments == (has_func ? 2 : 1)
		&& f->return_type.data == AIN_REF_HLL_PARAM;
	if (!strcmp(f->name, "Last")) {
		if (!shape)
			return (void*)Array_Last;
		return has_func ? (void*)Array_LastPredicate : (void*)Array_LastNoPred;
	}
	if (!strcmp(f->name, "Min")) {
		if (!shape)
			return (void*)Array_Min;
		return has_func ? (void*)Array_MinPredicate : (void*)Array_MinNoPred;
	}
	if (!strcmp(f->name, "Max")) {
		if (!shape)
			return (void*)Array_Max;
		return has_func ? (void*)Array_MaxPredicate : (void*)Array_MaxNoPred;
	}
	return NULL;
}

/* (2) src/ffi.c link_static_library（ffi.c:1172-1175）：把 First 條件擴充為四個名稱 */
				if (!strcmp(lib->name, "Array") && (!strcmp(ainlib->functions[i].name, "First")
				    || !strcmp(ainlib->functions[i].name, "Last")
				    || !strcmp(ainlib->functions[i].name, "Min")
				    || !strcmp(ainlib->functions[i].name, "Max"))) {
					extern void *array_select_function(const struct ain_hll_function *f);
					funcptr = array_select_function(&ainlib->functions[i]);
				}

/* 備註：HLL_EXPORT(Last/Max/Min, ...) 保持原樣（Array.c:2416/2446/2644），讓名稱比對繼續命中；
 * 實際函式指標由 selector 覆寫。如果只想修有呼叫的 overload、不動 Last#1 104 處，
 * 可以把 Last 無 pred 分支改回 (void*)Array_Last，stride=1 時兩者等價。 */
```

## fixture_plan
新增 overload_order_fixture.inc，寫法照 first_overload_fixture.inc：用 fo_find_lib/fo_find_fn 以 (name, nr_args, has_func, nth) 找宣告，nth=1 取 #1/#3；透過真 ffi 的 hll_call(lib, fn, arg3) 呼叫。HLL_FUNC 參數 push 2 slot：(-1, fno)。每次呼叫都 assert stack 淨增量：value 元素 2 slot、ref 元素 1 slot。取回 ref 後比對「元素身分」，也就是 [slot, idx] 的 idx 或 heap slot，不只比值。struct 元素用真 AIN 結構（依名稱在 ain->structures 找 CASPos / FrameDamage / CMessageText 的 index），配置 struct page 後填 member。

A. Max#1（decl 80，arg3=1）
   - array<int> [3,9,1,9,1] → 預期 [owner, 3]（平手取最後一個）。現行實作回 idx1，或在殘值落入範圍時呼叫任意函式。
   - 空陣列 → [-1, 0]。
   - HEAP_REF(owner) 呼叫前後 +1，與 Array_At 一致。
   - array<string>（AIN_ARRAY_STRING，arg3=2）["b","gamma","a","gamma"] → 預期第 4 個元素的 heap slot（strcmp、平手取最後一個）。對照 after-ff6fc2f.txt 裡現行版依 slot 編號亂選的輸出。
B. Min#1（decl 76）
   - array<int> [3,1,9,1] → [owner, 1]（平手取第一個）。
   - AIN_ARRAY_FLOAT [-2.0,-1.0] → idx0。int bit 比較會錯選 idx1。
   - 空陣列 → [-1, 0]。
   - 現行版的判定條件：stack 淨增量 0，也就是缺 push。
C. Min#3 / Max#3（decl 78 / 82，arg3=2），用真 lambda fno 37050（lhs.m0 < rhs.m0）與 37051（m1），元素為 CASPos (x,y)：[(7,1),(2,9),(2,3),(7,3)]。
   - Min#3+37050 → 元素 1。
   - Max#3+37052（同 body，m0）→ 元素 3，驗「平手取最後一個」；Rufim/std 會給元素 0。
   - Min#3+37051（m1）→ 元素 0。
   - Max#3+37053（m1）→ 元素 1。
   - 回傳為 1 slot，且 HEAP_REF(elem) +1。
   - 另在 lambda 入口用 probe_entry 記錄 (lhs, rhs) slot 順序，應為 (e[j], e[best])，呼叫次數 n-1。
D. Min#3 在 65538 wrap 元素上：需要 BattlePlayer 等實體與 CALLMETHOD 28537，建構成本高。改用 fno 36539（ref BattleVoicePlayingInfo，只讀 member1，arg3=2）當第二組 struct 測資：member1 = [5,2,2] → Min 回傳元素 1。
E. Last#3（decl 74）
   - fno 36357（ref FrameDamage，回傳 member0），arg3=2：member0 = [1,0,1,0] → 元素 2；全 0 → -1。
   - fno 24394（ref CMessageText，member0 == 1）：[1,1,0] → 元素 1。
   - 現行版的判定條件：一律回傳最後一個元素。
   - 不用 24706：它需要 X_GETENV closure env。
F. Last#1（decl 72）回歸：與現行 Array_Last 對同一組 int/string 陣列逐次比對，結果相同。若能構造 iface 陣列（a_type AIN_ARRAY、struct_type=2、arg3=65539），另記錄 Array_Last 與 Array_LastNoPred 的差異，作為待決項。
G. 錯誤路徑：Min#3 傳 1 參數 predicate（例如 36357）應 VM_ERROR "unsupported comparator shape"；在子行程中執行，驗 exit code。
H. 最後以 live_slots() 在釋放所有測資後應為 0，做 ASan build（build_probe.py asan），檢查 refcount 與 UAF。

## open
- v14 下 ArrayExtensions::Select<float,...> 產生的 array<float> page，其 a_type 是否真的是 AIN_ARRAY_FLOAT？若被抹成 AIN_ARRAY 或 AIN_ARRAY_INT，Min#1（ain_code.txt:829477）的 float 比較會退化成 int bit 比較，負值時錯誤。hll_arg3 對 int 和 float 都是 1，無法區分。未驗證，fixture B 需要用真路徑產生的 page 確認。
- 2-slot iface 陣列（arg3=65539）的 Last#1 共 11 處，包含 AnimateText@AdjustPos ain_code.txt:1111242，是 LOAD 標題置中的路徑。現行 Array_Last 用實體索引 nr_vars-1，stride>1 時會取到 vtable offset 而非 page。呼叫端以 X_MOV 4 2 / X_ASSIGN 2 消費，是否需要 2 slot 回傳也未知。runtime stride 與正確回傳寬度都未驗證；提案 Array_LastNoPred 只修索引，沒有動回傳寬度。
- 既有 Array_First 的 predicate 分支（Array.c:334-337）與 Array_Max 的 value 分支都沒有 v14 heap_ref(hll_self_slot)，但 Array_At（Array.c:448-450）有。呼叫端之後會 DELETE dummy，推論 owner refcount 會少 1。First#3 有 1 處 arg3=1（ain_code.txt 待定位）。是否構成 UAF 未以 runtime 驗證，建議另開一項修 First 的 predicate 分支，一樣改走 Array_At。
- 原版無 pred 的 Min/Max 對 enum 元素的 runtime 型別碼是否為 10（int）未驗證。提案把 AIN_ENUM/ENUM2 當 int 處理，理由是比照 Array.Find；原版 0x645dd0 只接受 10/11/12/47。
- SCY dump 與目前受保護 EXE 的逐位元一致性未獨立證實（見 exe-findings.md 未知 1）。本組語義結論是以 dump 為準。
- Rufim 參考實作的 Max 平手取第一個（ix_extreme 用 less(best,i)），與原版 0x646b50 的 cmove 行為（平手取最後一個）相反。若之後要對照 Rufim 的行為，要記得這個差異。
- Min#1 與 Max#3 的部分呼叫處屬於 editor namespace（stageeditor/elkeditor/modelviewer），遊戲本體是否會跑到未驗證；BattlePlayerCollection、MapStructure、LocalSave、FrameInfoCollection、BattleContext 則確定屬於遊戲邏輯。

## artifacts
- <scratchpad>/overload/maxminlast/proposed.c
- <scratchpad>/overload/maxminlast/selector.c
- <scratchpad>/overload/maxminlast/proposed.diff
- <scratchpad>/overload/maxminlast/Array.patched.c
- <scratchpad>/overload/maxminlast/sites.txt
- <scratchpad>/overload/maxminlast/sites.py
- <scratchpad>/overload/maxminlast/entries66_83.asm
- <scratchpad>/overload/maxminlast/helpers.asm
- <scratchpad>/overload/maxminlast/order-loops-and-comparators.asm
- <scratchpad>/overload/maxminlast/argbuild.asm
- <scratchpad>/overload/maxminlast/rd.py

## 驗證 refuted=False residual=0.85
以下是我逐項獨立核對的結果（SCY dump、capstone，腳本在 scratchpad/overload/min-max-last/xdis.py）。

1. 跳表 0x644f18 的 67-82 逐一核對無誤。71→0x649ec0；72→0x649f20→call 0x649ec0；73→0x649f40；74→0x649f80→call 0x649f40；75→0x649fb0；76→0x64a050→0x649fb0；77→0x64a070；78→0x64a110→0x64a070；79→0x64a140；80→0x64a1e0→0x64a140；81→0x64a200；82→0x64a2a0→0x64a200。

2. Min 迴圈 0x646af0：[esp+0x10]=best、[esp+0x14]=j。先 push &best，再 push &j，所以 op(p1=&j, p2=&best)。`test al,al; cmovne esi,edi` 表示 less 為真時 best=j，平手取第一個最小值。Max 迴圈 0x646b50 的結構完全相同，只是改成 `cmove esi,edi`，也就是 !less 時 best=j，平手取最後一個最大值。n<=1 時直接回傳 0。

3. 參數順序：delegate 的 _Do_call 0x64bd40 依序 push *p2、*p1，再 call 0x645f90。0x645f90 呼叫 0x645c80(p1, array, p2)，後者先加入 e[p1]、再加入 e[p2]。所以 lambda 收到 (lhs=e[j], rhs=e[best])，確認。

4. 預設 less：
   - int 0x64bef0 是 `setl`，對應 val(p1)<val(p2)，signed。
   - float 0x64be70 是 `comiss xmm0(p2), p1; seta`，對應 p1<p2，NaN 時為 false。
   - string 0x64bdc0 是 unsigned byte 比較，p1<p2 時為真。
   - unsupported 的 vtable 0x810e7c，其 +8 是 0x49c5c0（`xor al,al; ret 8`），less 恆為 false。
   - 型別表 byte 0x645f14：10/47/92 → int，11 → float，12 → string，其他都走 log。調查者漏了 92（AIN_ENUM，見 libsys4 ain.h:90）。

5. Last(pred)：0x649f40 → 0x648c80（xor edx,edx，begin=0；push size，end=n）→ 0x648d80（n<=0 回傳 -1）→ 0x646c10（end>=n 時 esi=n-1，dec 迴圈，第一個 true 就 return，否則 `or eax,-1`）。0x6499e0 的 `test ebx,ebx; js` 與 `cmp ebx,size; jge` 會把 -1 或「空陣列的 0」轉成 null。以上全部和提案一致。

6. lambda 簽名（dump 註解，scratch lambdas.txt）：26 個 Min#3/Max#3 呼叫處都是 2 參數，參數型別為 ref struct 或 wrap，回傳 bool。7 個 Last#3 呼叫處都是 1 參數，回傳 bool。fno 36303 的 body 是 `lhs.m28537() < rhs.m28537()`，確認是 less。

7. 現況缺陷也都確認：
   - Array_Max（Array.c:2156-2219）：空陣列 ref 分支 push 0；value 分支沒有 v14 heap_ref；comparator 路徑 push (e,0) 並把回傳值當分數。
   - Array_Min（Array.c:2321-2332）：一個 slot 都不 push。ffi.c 的 AIN_REF_HLL_PARAM 分支不 push C 回傳值。
   - Array_First 的 predicate 分支（334-337）確實缺 heap_ref(hll_self_slot)。

8. 提案程式碼的 stack 與 ref 契約：
   - array_order_less 的 push/pop 與 saved_sp 還原和 array_query_predicate 同型。
   - vm_call_nopop（vm.c:1718）會對 REF/STRUCT/WRAP/STRING 參數做 heap_ref，並在 local page 釋放時平衡。
   - closure（fno 24706 X_GETENV）靠 vm_call_nopop 設的 env_page 取到呼叫端 frame，可行。
   - ffi 在巢狀呼叫時會保存並還原 hll_current_arg3/hll_self_slot/hll_func_obj（ffi.c:399-405、798-802），所以迴圈結束後的 Array_At 讀到的 arg3 是正確的。
   - Array_At 的 index*struct_type 條件和 array_erase_stride 相同，logical 與 physical 索引換算一致。
   - 空陣列回傳 -1 → Array_At 產生 null，和原版一致。

9. Min#1 在 array<float> 上的 a_type 疑慮：Select<float,...>（ain_code.txt:917441-917509）一開頭就 `X_A_INIT 0`；X_A_INIT 依宣告把 array<float> 映射成 AIN_ARRAY_FLOAT（vm.c:4915-4940），之後 PushBack 對非 NULL page 不改 a_type，Where 也保留 src a_type（Array.c:285）。因此推論 a_type 為 FLOAT，但 A_REF 複製是否保留 a_type 未驗證。此外該呼叫處屬於 stageeditor namespace。

結論：語義沒有被推翻。程式碼對 CN 實際有呼叫的 Min#1/Min#3/Max#1/Max#3/Last#3（arg3 只有 1、2、65538）看起來正確。主要風險是順手改動 Last 無 pred 的路由：65539 的回傳寬度原本就錯，這次也沒修到。

### code_bugs
- [中] Last 無 pred 改接 Array_LastNoPred 會擴大影響面，卻修不好 65539。CN 的 Last#1 在 arg3=65539 有 11 處，例如 ain_code.txt:1111242 AnimateText@AdjustPos。這些呼叫端用 `X_MOV 4 2 / X_ASSIGN 2` 消費，也就是期待 2 槽。可是 Array_At（Array.c:437-460）遇到 65539 時 array_elem_is_ref() 為真（hll_current_arg3>=0x10000），只 push 1 槽 values[phys]。舊的 Array_Last（Array.c:465-493）同樣只 push 1 槽，而且推的是 values[nr_vars-1]（vtoff），會對任意 slot 做 heap_ref。新舊兩版的 stack 寬度都錯，新版只是把 heap_ref 的對象改成 page slot，stack 一樣錯位。同樣的 1 槽/2 槽不符也出現在 At#1 65539（16 處）、First 65539 與 First#3 196610（0x30002，同樣是 X_MOV 4 2）。建議：這一刀讓 Last 無 pred 維持 (void*)Array_Last，2-slot 回傳寬度另開一項處理；或者在 Array_At 裡讓 2-slot 元素走 [hll_self_slot, phys_index] 2 槽路徑，再用 fixture 驗證。
- [低] array_order_value_kind 用 a_type 判斷元素型別並不可靠。Array_PushBack 對 NULL page 建頁時，a_type 一律寫成 AIN_ARRAY_INT（Array.c:158）；Array_Where 空結果也是 AIN_ARRAY_INT（Array.c:238）。用 PushBack 從零建起的 array<string>（arg3=2）因此會落到 `ARRAY_INT && array_elem_is_ref()` → NONE，less 恆為 false：Max 回傳最後一個、Min 回傳第一個，原版則會做 strcmp 比較。CN 的無 pred 呼叫處只有 int/float（Max#1 ×2、Min#1 ×1），所以目前沒有實際影響。若要補強，可以對 values[0] 檢查 heap[].type==VM_STRING 來判斷字串。
- [低] array_order_value_kind 裡的 `case AIN_ENUM: case AIN_ENUM2:` 是死碼，因為 array page 的 a_type 是陣列型別；X_A_INIT 在 vm.c 約 4934 行已把 AIN_ENUM 元素映射成 AIN_ARRAY_INT。另外，原版 0x645dd0 的型別表（byte table 0x645f14）只把 10/11/12/47/92 對到 less，92=AIN_ENUM→int。91=AIN_ENUM2 走 unsupported（always-false）。提案卻把 ENUM2 當 int，和原版不同；CN 沒有呼叫處。
- [低] array_order_extreme 在 has_func 時，只要 n>=1 就先跑 array_order_callback_shape。n==1 時原版根本不呼叫 lambda（loop 0x646af0/0x646b50：edi=1、cmp ebp,edi、jle 直接 return 0），xsystem4 這邊卻可能因為 shape 不支援而 VM_ERROR。另外 stride!=1（iface 陣列配 comparator）一律 VM_ERROR，原版則支援。CN 的 Min#3/Max#3 arg3 只有 2 或 65538，所以不會碰到。
- [低] array_order_text 遇到無效的 string slot 時默默回傳 ""，和 array_query_value_equal（Array.c:1985-1998）遇到無效字串就 VM_ERROR 的慣例不一致，會把資料損壞藏起來。
- [文件錯誤，非程式 bug] 調查摘要寫「所有呼叫端都以 X_MOV 3 2; X_ASSIGN 1 消費 1 個 ref」，這是錯的。arg3=1 的呼叫處用的是 `X_MOV 4 2 / X_ASSIGN 2`，也就是 2 槽：Min#1 ain_code.txt:829477、Max#1 1059613/1021218、Last#3 470743。提案的程式透過 Array_At，在 arg3=1 時 push [self, idx] 兩槽，因此程式本身是對的，只有敘述需要更正。
- [未驗證] 提案宣稱 clang -fsyntax-only 為 0 error，我沒有重跑；runtime fixture 也還沒跑。

### corrected
同意主要語義，再補充下列修正與細節：
- Min(self)/Min(self,less)：best=0；j 從 1 到 n-1，若 less(e[j],e[best]) 則 best=j，平手取第一個。
- Max(self)/Max(self,less)：best=0；j 從 1 到 n-1，若 !less(e[j],e[best]) 則 best=j，平手取最後一個。
- lambda 以 (lhs=e[j], rhs=e[best]) 呼叫。
- 預設 less：int/bool/AIN_ENUM(92) 用 signed <，float 用 <（NaN 時為 false），string 用 unsigned byte strcmp<0。其他型別（含 ENUM2=91、LONG_INT=55、struct、iface）less 恆為 false，因此 Min 回傳 e[0]、Max 回傳 e[n-1]。
- Last(pred)：從 n-1 往 0 掃，第一個命中就回傳，找不到回傳 -1。
- 空陣列、無效陣列或 -1 一律經 0x6499e0 轉成 null ref。
- 回傳 ref 的寬度由 arg3 決定：1 → 2 槽 [owner, idx]；2 與 0x10002 → 1 槽；0x10003 與 0x30002 → 呼叫端消費 2 槽（現行 Array_At 只給 1 槽，既有缺陷，不在本組範圍）。

### counter_evidence
這次沒有找到能推翻語義的證據，下列是對提案敘述的更正：
(a) 0x645dd0 的型別表：type 92（AIN_ENUM）→ 0x811020（int less）。依據是 byte table 0x645f14 的 [82]=0，也就是 92-10。提案寫「只接受 10/11/12/47」，不完整。
(b) 消費寬度不是全部 1 槽。arg3=1 的呼叫處用 X_MOV 4 2/X_ASSIGN 2，例如 ain_code.txt:829477 之後、1059613 之後、470743 之後。
(c) 65539 的 Last#1/At#1 呼叫處消費 2 槽（統計：At#1 65539 16 處、Last#1 65539 11 處，全部是 X_MOV 4 2 X_ASSIGN 2），Array_At 卻 push 1 槽。這是既有缺陷，提案的 Array_LastNoPred 沒有修到。
(d) after-ff6fc2f.txt:3-5 的 Max#1 其實是確定性行為：每輪都回傳 heap slot 編號最大的那個（slot 4），並不是「不定」。這符合「殘值落在範圍外 → raw int 比較 heap slot」的解釋。
