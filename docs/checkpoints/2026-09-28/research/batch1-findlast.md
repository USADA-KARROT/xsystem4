# findlast

## 調查摘要
FindLast 的四個宣告（Array index 43/45/47/49，libraries.txt:132/134/136/138）目前全部綁到同一個 Array_FindLast(array,value)（src/hll/Array.c:2308, export 在 :2643）。這個函式只做 int 比較。CN AIN 實際只呼叫 FindLast#2 [47] (self, hll_func)，共 4 處，hll_arg3 都是 65538（ain_code.txt:286657/350458/354081/1064192）。現況下 C 的 value 參數收到的是 lambda 的 fno，程式拿元素的 heap slot 去跟 fno 比，幾乎一律回 -1。所以 4 個呼叫處都拿到錯的 index。

SCY EXE 反組譯確認 FindLast 的語義：
- 走 0x644ab0 -> 0x648c80 -> 0x648d80 -> 反向掃描 0x646c10。
- begin 夾到 >= 0；end 若 >= length 就改成 length-1，而且 end 本身會被檢查（包含 end）。
- 從 end 往 begin 掃，第一個命中就回傳 index，找不到回 -1。-1 不代表「掃到尾」。

這點跟 Find 不對稱。Find 在 0x646bb0 是 end=min(end,length) 且 i<end，也就是不含 end。c3b5ff0 做的四個 Find 實作（Array.c:2000-2042，[begin,end)，begin 夾 0、end 夾 count）與原版一致，不需要改。FindLast 只能照 Find 的結構做，但區間必須改成含 end。Rufim 的 Array_ix_FindLast4 用不含 end 的寫法，與原版不符，不要照抄。

建議併入 array_query_function，不放 array_select_function，理由：
- 回傳是 int，走一般 ffi 回傳。array_select_function 處理的是 First 那種 ref hll_param 回傳。
- array_query_function 已經有 range 參數型別檢查、hll_param/hll_func 分流、第一參數必須是 ref array、回傳必須是 int 等驗證，FindLast 的四種宣告形狀跟 Find 完全相同。

需要改三處：
- Array.c 新增四個函式。
- array_query_function 加上 FindLast 分支。
- ffi.c:1176 的名稱條件加 "FindLast"。
另外把 HLL_EXPORT(FindLast, Array_FindLast) 改指 Array_FindLastValue，並刪掉舊的 Array_FindLast。舊函式對 string/float 元素只比 slot 號，也沒有型別檢查。

## lambda
4 處呼叫都是 CALLHLL Array FindLast#2 65538，呼叫前先推 PUSHSTRUCTPAGE（或 closure obj），再推 PUSH fno。lambda 都是 nr_args=1、回傳 bool。
- fno 23965（ain_code.txt:286619 起）：CEffectData@<lambda EraseEmitter(array<int>)(305,50)>；ARG0 emitterData : ref elkeditor::detail::CEmitterData；VAR1 dummy : wrap<array<ref CEmitterData>>；RETURN bool。函式內用 X_GETENV 讀 closure env。呼叫處 286657 把結果存進 local 6 index : int，後面接 Erase(list, index+1, n-index)（286680-286690 附近）。
- fno 24280（350448）：CEmitterList@<lambda GetLastSelectedNumber()(391,39)>；ARG0 emitter : ref elkeditor::detail::CEmitter；RETURN bool，內容是 CALLMETHOD 5321。呼叫處 350458 把結果當 GetLastSelectedNumber 的 int 回傳值直接 RETURN。
- fno 24298（354064）：CEmitterList@<lambda GetLastChildEmitterNumber(int)(996,44)>；ARG0 emitter : ref CEmitter；RETURN bool，用 X_GETENV 讀 env 裡的欄位 1 做 EQUALE。呼叫處 354081 存進 local 2 index，接著 `index == -1 ? emitterNumber : index`。這裡直接證明找不到時要回 -1。
- fno 36405（1064166）：FrameInfoCollection@<lambda GetEnablePrevious(int)(208,40)>；ARG0 obj : wrap<FrameInfo>；RETURN bool，讀 obj.field7 的 field1 或 field2。呼叫處 1064192 存進 local 3 frameIndex : int，接著 At#1(list, frameIndex) 65538，再用 PUSH -1/NOTE 判斷是否有值。
參數型別都是 AIN_REF_STRUCT 或 AIN_WRAP，stride=1，nr_args=1。這兩種型別都在 array_query_callback_shape（Array.c:1878-1914）的「push 元素值 1 槽」白名單內，跟已運作的 Find#2 65538（29 處）走同一條路徑，不需要新的 callback ABI。

## proposed_code
```c
/* Array.FindLast overloads (CN AIN Array index 43/45/47/49).
 * Native SCY dump: dispatcher 0x644a12/0x644a61/0x644ab0/0x644aff ->
 * 0x648ac0/0x648ba0/0x648c80/0x648d80 -> reverse scan 0x646c10.
 * 0x646c10: begin = max(begin, 0); end = (end >= length) ? length - 1 : end;
 * for (i = end; i >= begin; i--) if (match(i)) return i; return -1.
 * So FindLast's end is INCLUSIVE, unlike Find (0x646bb0: end = min(end,
 * length), i < end). The full-range wrappers pass begin=0, end=length.
 * end < 0 or begin > end yields -1; -1 does not mean "to the end".
 * Value/predicate matching reuses the Find helpers so element typing and
 * the callback ABI stay identical to Find/IsExist/Erase.
 */
static int Array_FindLastValueRange(struct page **array, int begin, int end, int value)
{
	const struct page *src = array ? *array : NULL;
	if (!src || src->type != ARRAY_PAGE)
		return -1;
	enum ain_data_type type = array_query_value_type(src);
	int count = src->nr_vars;
	if (begin < 0) begin = 0;
	if (end >= count) end = count - 1;
	union vm_value needle = {.i = value};
	for (int i = end; i >= begin; i--) {
		if (array_query_value_equal(src->values[i], needle, type))
			return i;
	}
	return -1;
}

// [43] FindLast(self, hll_param search). Not called by the CN AIN.
static int Array_FindLastValue(struct page **array, int value)
{
	return Array_FindLastValueRange(array, 0, INT_MAX, value);
}

// [49] FindLast(self, int begin, int end, hll_func). Not called by the CN AIN.
static int Array_FindLastIfRange(struct page **array, int begin, int end, int func)
{
	if (!array || !*array || (*array)->type != ARRAY_PAGE)
		return -1;
	int stride = array_erase_stride(*array);
	array_query_callback_shape(array, func, stride);
	int count = (*array)->nr_vars / stride;
	if (begin < 0) begin = 0;
	if (end >= count) end = count - 1;
	for (int i = end; i >= begin && *array && (*array)->type == ARRAY_PAGE
	     && i < (*array)->nr_vars / stride; i--) {
		if (array_query_predicate(array, i, stride, func))
			return i;
	}
	return -1;
}

// [47] FindLast(self, hll_func). CN AIN: FindLast#2, 4 call sites, hll_arg3 65538.
static int Array_FindLastIf(struct page **array, int func)
{
	return Array_FindLastIfRange(array, 0, INT_MAX, func);
}

/* Placement: right after Array_FindIf (Array.c:2042), before
 * array_select_function, so the Find helpers are already declared.
 * Delete the old int-only Array_FindLast (Array.c:2307-2318) and change the
 * export at Array.c:2643 to HLL_EXPORT(FindLast, Array_FindLastValue); the
 * entry only anchors the name match in link_static_library, the pointer is
 * replaced by array_query_function. */
```

## selector
```c
/* 不放 array_select_function，改在 array_query_function（Array.c:2059）裡擴充。
 * 把原本的 `if (strcmp(f->name, "Find")) return NULL;` 到函式結尾換成： */
	bool last = !strcmp(f->name, "FindLast");
	if (!last && strcmp(f->name, "Find"))
		return NULL;
	bool range = f->nr_arguments == 4;
	if (range) {
		if (f->arguments[1].type.data != AIN_INT || f->arguments[2].type.data != AIN_INT)
			return NULL;
	} else if (f->nr_arguments != 2) {
		return NULL;
	}
	enum ain_data_type arg = f->arguments[range ? 3 : 1].type.data;
	if (arg == AIN_HLL_PARAM) {
		if (last)
			return range ? (void *)Array_FindLastValueRange : (void *)Array_FindLastValue;
		return range ? (void *)Array_FindValueRange : (void *)Array_FindValue;
	}
	if (arg == AIN_HLL_FUNC || arg == AIN_HLL_FUNC_71) {
		if (last)
			return range ? (void *)Array_FindLastIfRange : (void *)Array_FindLastIf;
		return range ? (void *)Array_FindIfRange : (void *)Array_FindIf;
	}
	return NULL;

/* src/ffi.c:1176-1177 名稱條件加 FindLast： */
				if (!strcmp(lib->name, "Array") && (!strcmp(ainlib->functions[i].name, "Numof")
				    || !strcmp(ainlib->functions[i].name, "Count") || !strcmp(ainlib->functions[i].name, "Find")
				    || !strcmp(ainlib->functions[i].name, "FindLast"))) {
					extern void *array_query_function(const struct ain_hll_function *f);
					funcptr = array_query_function(&ainlib->functions[i]);
				}
/* 宣告形狀不符時回 NULL，funcptr 為 NULL 就不 link，直接 fail-fast。這跟 Find 現行行為一致。 */
```

## fixture_plan
照 first_overload_fixture.inc 的寫法：用 fo_find_lib 和 fo_find_fn 依「名稱、參數數、是否含 hll_func、第幾個宣告」找 index。參數推法：hll_func 推兩槽 (-1, fno)（ffi.c:618-633），string 元素的 hll_arg3=2。predicate 用真 AIN 裡不依賴 env 的 string lambda：fno 36080（ain_code.txt:998464，`obj == ""`）或 fno 36373（1056875，同一個 body）。
測試陣列 A = ["a","","b","","c"]（AIN_ARRAY_STRING，rank 1），另外準備 E = 空陣列（NULL page）。
1) FindLast#2 [47]：FindLast(A, (-1,36080)) 預期回 3。對照 Find#2 [46]（fo_find_fn(lib,"Find",2,1,0)）預期回 1。修正前的現況：比 slot==36080，預期得到 -1，所以修正前這項應該 FAIL。
2) FindLast#3 [49] 包含 end 的判別：
   - (0,3,pred) -> 3。如果寫成不含 end 會得到 1，這一項是主要判別點。
   - (0,10,pred) -> 3（end 夾到 length-1）
   - (0,2,pred) -> 1
   - (-5,0,pred) -> -1
   - (4,10,pred) -> -1
   - (0,-1,pred) -> -1（-1 不代表掃到尾）
   - (2,1,pred) -> -1
   對照 Find#3 [48]：(0,3,pred) -> 1；(1,1,pred) -> -1，因為 Find 不含 end。
3) FindLast#0 [43]：B = ["x","y","x"]，search 用內容同為 "x" 但 slot 不同的新字串，預期回 2（證明比的是字串內容，不是 slot 號）；search "z" 預期回 -1。
4) FindLast#1 [45]：B 配 (0,1,"x") -> 0；(0,2,"x") -> 2；(1,1,"y") -> 1（包含 end）；(3,5,"x") -> -1。
5) 對空陣列 E，四個版本都回 -1，stack_ptr 不變。
每次 hll_call 之後都 assert stack_ptr == sp，並 unref 暫存字串與陣列。
ref struct 路徑（65538）可以另外加一項：用 fno 36131（1006837，wrap<StockCount>，`obj.field1 == 0`）自建 STRUCT_PAGE（至少 2 個 vars），陣列元素放 page slot，hll_arg3=65538。需要先查 StockCount 的 struct 編號，這項標為未驗證、可選。
預期結果：修正前 [47]、[49]、[45] 三項都 FAIL，修正後全部 PASS。

## open
- 原版在 array object 為 NULL 時（0x6473b0 回 false）FindLast 和 Find 都回 0，不是 -1（0x648ad5/0x648c95/0x648dbb 的 `xor eax,eax`）。但 0x6473b0 在 NULL 時會先組一段長度 0x3b 的錯誤字串（0x6473e0），可能其實是報錯或丟例外，未驗證。xsystem4 的 NULL page 代表空陣列，不是原版的 NULL object，所以建議維持 -1。
- array_query_value_type 對 ref 元素會 VM_ERROR，所以 FindLast#0/#1 在 ref struct 陣列上會直接報錯。原版值比較器工廠 0x646020 對 struct 元素怎麼比未驗證。CN AIN 沒有呼叫這兩個 overload，影響為零，行為照 Find。
- 原版 predicate 版在 length<=0 時會先回 -1，不建立 callback（0x648dc6）。提案沿用 Find 的順序，先做 array_query_callback_shape：遇到非法 fno，就算陣列是空的也會 VM_ERROR。原版在這情況下不會報錯，差異只出現在錯誤輸入。
- 4 個呼叫處有 3 個在 elkeditor（特效編輯器）和 FrameInfoCollection，是否在正常遊玩流程中執行未驗證，所以修正的實際玩家可見影響未量測。

## artifacts
- <scratchpad>/overload/FindLast/proposed_findlast.c
- <scratchpad>/overload/FindLast/findlast-dispatch.asm.txt
- <scratchpad>/overload/FindLast/fn_0x646c10.asm.txt
- <scratchpad>/overload/FindLast/fn_0x648ac0.asm.txt
- <scratchpad>/overload/FindLast/fn_0x648b00.asm.txt
- <scratchpad>/overload/FindLast/fn_0x648ba0.asm.txt
- <scratchpad>/overload/FindLast/fn_0x648c80.asm.txt
- <scratchpad>/overload/FindLast/fn_0x648d80.asm.txt
- <scratchpad>/overload/FindLast/fn_0x6473b0.asm.txt

## 驗證 refuted=False residual=0.9
我自己用 capstone 反組譯 SCY dump（腳本在 scratchpad/overload/findlast-refute/disx.py，沒有改動調查者 overload/FindLast/ 內的檔案），逐條核對：

1. 跳表 Array-jump-table.json 的 43=0x644a12、45=0x644a61、47=0x644ab0、49=0x644aff，偶數 index 44/46/48 是 Find 的 range/pred 版（0x648b00/0x648c40/0x648cc0）。這與 libraries.txt:88 起 Array 段第 43-49 行交錯的 Find/FindLast 宣告一致。

2. 0x646c10 反向掃描：0x646c1a-0x646c1e 做 `xor edi,edi; test esi,esi; cmovns edi,esi`，得到 begin=max(begin,0)。0x646c21-0x646c29 做 `mov esi,[esp+0x10]; cmp esi,eax; jl; lea esi,[eax-1]`，也就是 end>=len 時 end=len-1。0x646c2c 在 end<begin 時直接回 -1。0x646c34-0x646c52 用 predicate(&i) 檢查，命中就回 esi（0x646c5b），否則 `dec esi; cmp esi,edi; jge`。先檢查 end 再遞減，所以 end 確實包含在內。對照 0x646bb0（Find）：`cmp edi,eax; cmovg edi,eax` 讓 end=min(end,len)，迴圈是 inc 後 `jl`，所以 Find 是 [begin,end)。這個不對稱確實存在。

3. 0x644a61 與 0x644aff 的 dispatcher 呼叫慣例：[edi+0x28]=arg1(begin) 放進 edx；[edi+0x50]=arg2(end) 經 0x659f20 取 int 後 push；&arg3 在 +0x78，先 push。0x648ba0 和 0x648d80 把 [ebp+8]=end 與 callback 傳給 0x646c10，對應 [esp+0x10]=end、[esp+0x14]=callback。

4. 全範圍版本：0x648c80 在 0x6473b0 回非 NULL 後 push &func，接著 `call [eax+0xc]`（length）後 push，再以 edx=0 呼叫 0x648d80（0x648ca5-0x648caa），得到 begin=0、end=length，最後夾成 len-1。0x648ac0 值版本的結構相同，走 0x648ba0。0x648d80 在 0x648dc3-0x648dca 對 length<=0 回 -1。0x648ba0 呼叫 0x646020（值比較工廠），0x648d80 呼叫 0x6461e0（predicate 工廠）。

5. ain_code.txt 的 4 個呼叫處都是 `CALLHLL Array FindLast#2 65538`（行 286657/350458/354081/1064192）。前面分別推 PUSHSTRUCTPAGE / .STRUCTREF 陣列 / PUSH fno。lambda 23965/24280/24298/36405 都是 1 個參數、RETURN bool，ARG0 型別是 ref CEmitterData、ref CEmitter、ref CEmitter、wrap<FrameInfo>。354081 之後有 `.LOCALREF index; PUSH -1; EQUALE`，證明找不到時的哨兵值是 -1。沒有 FindLast#0/#1/#3 的呼叫（grep 只找到 String FindLast 和 SYS_FindLast）。

6. 程式碼：array_query_value_type、array_query_value_equal、array_query_callback_shape、array_query_predicate（Array.c:1879-1998）是 Find 已經在用的輔助函式。提案的四個函式只是把迴圈方向和 end 夾值改成原版 0x646c10 的寫法，逐行與反組譯一致。selector 的改法保留原本 array_query_function 的第一參數 ref array、回傳 int、range 參數必須是 int 等檢查，能正確分流 hll_param / hll_func / hll_func_71。

Rufim 的 Array_ix_FindLast4（rufim/xsystem4/src/hll/Array.c:892-904）用 `i < end`，不含 end，與原版不符。調查者說不要照抄是對的。

我找不到能推翻語義的證據，程式碼也沒有會影響 CN 正確性的 bug。剩下的都是低嚴重度的說明錯誤或錯誤輸入時的行為差異。

### code_bugs
- [低，說明不實，CN 無影響] 提案說「宣告形狀不符時回 NULL，funcptr 為 NULL 就不 link，直接 fail-fast」，這不成立。ffi.c:1181-1182 在 funcptr 為 NULL 時只是跳過 link_static_library_function，但 found 仍設為 true。之後 hll_call 在 ffi.c:311 的 `!libraries[libno][fno].fun` 分支只印 rate-limited 的 WARNING("UNIMPL HLL")，把參數 pop 掉，再 push 0（ffi.c:335-380）。所以結果是靜默回 0，不會停下來。對 FindLast/Find 來說，0 是合法 index，比回 -1 更糟。CN 的四個 FindLast 宣告（libraries.txt:132/134/136/138）形狀都符合，array_query_function 一定回非 NULL，實際影響是零。但這個現象要照實記錄。如果真的要 fail-fast，應在 link 時對 Array 的 query 名稱回 NULL 就 ERROR。
- [低，與原版有差異] Array_FindLastIfRange 先呼叫 array_query_callback_shape，再處理長度。原版 0x648d80 在 0x648dc3-0x648dca 對 length<=0 直接 `or eax,-1`，不會建立 callback（0x6461e0）。所以空陣列配上非法 fno 或不支援的 lambda 簽名時，提案會 VM_ERROR，原版則回 -1。調查者已自己列出這點。CN 的 4 個 lambda（fno 23965/24280/24298/36405）都是 nr_args=1、回傳 bool，參數型別是 ref struct 或 wrap<struct>，都在 Array.c:1893-1896 白名單內，不會觸發。若要逐位元組一致，可在 shape check 前加 `if ((*array)->nr_vars / stride <= 0) return -1;`。
- [低，範圍擴大] ffi.c 名稱條件加入 "FindLast" 會套用到所有遊戲。以前任何 FindLast 宣告都會 link 到 int 比較版；現在其他 AIN 的 FindLast 形狀若不同（例如回傳型別不是 int、參數數量不是 2 或 4），就會變成未 link，靜默回 0（見第一點）。CN 不受影響。上游其他遊戲是否有這種宣告，未驗證。
- [證據瑕疵，不影響結論] 摘要說 Find#2 65538 有 29 處，實際以 python byte 級精確比對 `CALLHLL Array Find#2 65538`，ain_code.txt 有 42 處。這 42 處的 lambda ARG0 同時涵蓋 `ref <struct>`（CEmitterData/CPartsTreeNode 等）與 `wrap<struct>`（wrap<Worker>/wrap<Player>/wrap<FrameInfoCollection> 等），所以「FindLast#2 的 ref CEmitterData / ref CEmitter / wrap<FrameInfo> 跟 Find#2 走同一條 callback 路徑」這個論點仍然成立。
- [無 bug，已核對] push 槽數與 ref 計數：hll_arg3 65538 取 &0xFFFF=2，array_elem_is_2slot（Array.c:60-63）為假，stride=1。array_query_predicate（Array.c:1916-1945）push 1 槽元素值，vm_call_nopop（vm.c:1750-1768）對 AIN_REF_TYPE/AIN_WRAP 做 heap_ref，lambda 返回時由 local page 釋放。stack 上的副本沒有 ref，由 stack_ptr=saved_sp 還原，所以是平衡的。bool 回傳用 stack_pop().i != 0 讀取，與 Find 相同。hll_func_obj 由 ffi.c:618-627 設定，巢狀 hll_call 會在 ffi.c:401/802 儲存並還原，predicate 內再呼叫 HLL 不會污染它。lambda 23965 用到 PUSHSTRUCTPAGE（struct_page 由 vm.c:1738-1739 設定）和 X_GETENV（env_page 由 vm.c:1740-1741 設定），呼叫處在 EraseEmitter 本體內，env 正確。
- [無 bug，已核對] 邊界：Array_FindLastValueRange 與 Array_FindLastIfRange 在 count=0 且 end=INT_MAX 時會得到 end=-1，迴圈不執行，回 -1。end<0 或 begin>end 時回 -1。begin<0 夾成 0。這些都與 0x646c1a-0x646c2e 逐條一致。INT_MAX 已由 Array.c:17 引入 <limits.h>。新函式放在 Array_FindIf（Array.c:2039-2042）之後，位於 array_query_function（2059）與匯出表（2643）之前，宣告順序可以編譯。舊的 Array_FindLast 只在 Array.c:2308 定義、2643 匯出，沒有其他引用（grep src/ include/），可以安全刪除。

### corrected
同意。補充一處更正：宣告形狀不符時，ffi 並不會 fail-fast。它會變成 UNIMPL，發出 WARNING，然後靜默回 0（ffi.c:311、335-380）。CN 的四個宣告形狀都符合，不受影響。

### counter_evidence
能拿來反對調查者的只有以下幾點，沒有一點推翻語義：
(a) ffi.c:311-380 顯示 funcptr 為 NULL 時會靜默 push 0，不是 fail-fast，調查者的說法錯。
(b) Find#2 65538 實際有 42 處，不是 29 處。
(c) 空陣列配上非法 fno 時的報錯順序與原版 0x648dc6 不同（調查者已自承）。
(d) 原版在 array object 為 NULL 時回 0（0x648ad5/0x648c95/0x648dbb 的 xor eax,eax），提案回 -1。0x6473b0 在 NULL 分支會組長度 0x3b 的字串（0x6473e0 push 0x3b; push 0x7e21c8），是否因此報錯或丟例外，未驗證。xsystem4 的 NULL page 代表未配置的空陣列，原版的空陣列物件照理非 NULL，所以回 -1 比較合理，但這點未驗證。
(e) 值比較工廠 0x646020 對 struct 元素如何比較，未反組譯，未驗證。CN 沒有呼叫 #0/#1，影響為零。
