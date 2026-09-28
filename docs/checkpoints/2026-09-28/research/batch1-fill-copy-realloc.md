# fill-copy-realloc

## 調查摘要
Fill/Copy/Realloc 組結論：6 個宣告裡，Fill#0 [28]、Fill#1 [29]、Copy#0 [30] 在 CIF 下參數錯位或讀到殘值，是真缺陷；Copy#3 [33] 和 Realloc#0 [1] 的 CIF 與 C 原型逐位一致，CN 呼叫點的資料結果正確，但回傳值是殘值。所有 CN 呼叫點都立刻 POP，所以殘值回傳目前無害。

原版語義已用 SCY EXE 反組譯確認（跳表 0x644f18）：
- [28] 0x644769 = fill(self, 0, Numof(self), v)
- [29] 0x6447be = fill(self, i, len, v)
- [30] 0x6447f2 -> 0x6481a0 = copy(self, 0, src, 0, Numof(src))
- [31] 0x644816 -> 0x6481d0
- [32] 0x644846
- [33] 0x6448b8 -> 0x648210
- [1] 0x644383 -> CArrayPage vtbl+0x50 = 0x67f4d0
- [2] 0x6443b0 -> 0x647510：先 realloc，再只 fill 新增的尾段 [old, n)

fill helper 0x648120 與 copy helper 0x648210 會把範圍夾到陣列邊界，回傳夾後的 count（可能是 0 或負數），dispatcher 以 int（type 10）回傳。

候選修補分兩處：
- Array.c：新增 Array_Fill_All / Array_Fill_Range / Array_Copy_All / Array_Copy_To / Array_Copy_From / Array_Copy（5 參數）/ Array_Realloc_Fill，並擴充 array_select_function。
- ffi.c：Fill、Copy、Realloc 也改走 array_select_function。

驗證方式：在 scratch 用真 AIN + 真 ffi 建兩個 ASan probe，worktree 一行未改。baseline 11 個模式中 9 個 exit 88（FAIL），2 個 abort（destIndex>0 觸發 VM_ERROR）。candidate 11/11 PASS，另外 4 個既有回歸模式（first-overload、personality、heap-reuse、assignment）也全部 PASS。

另外找到 3 個問題，都不在本組綁定範圍內：
- Array_Copy 的 dst_i_wrap hack 是錯的：destIndex>0 時會把整數誤當 heap slot 查表，probe 實測 VM_ERROR 當掉。
- Array_Realloc 有數個 latent bug，都只影響 editor 呼叫點，詳見 open_questions。
- Fill 共用字串 slot 會造成別名：S_ASSIGN 會就地改寫 slot，改一個元素等於全部一起改。

## lambda
不適用：本組 6 個宣告都沒有 hll_func 參數（libraries.txt Array 段 index 1-2、28-33），沒有 lambda 呼叫點。

hll_param value 參數怎麼傳進 C（對應第 4 題）：
- CIF 型別：ain_to_ffi_type(AIN_HLL_PARAM) 為 ffi_type_pointer（src/ffi.c:1100）。
- hll_call 取值（src/ffi.c:654-672）：slot 數由 hll_arg3 & 0xFFFF 決定；etype 為 3、5、AIN_IFACE、AIN_OPTION、AIN_IFACE_WRAP 時是 2 槽，否則 1 槽。
  - 1 槽時 args[i] = &stack[sp]（8-byte union vm_value）。C 宣告成 int，拿到的是低 32 位 = .i；float 元素則是 .f 的位元樣式。
  - 2 槽時第 1 槽放進 heap_slots[i]（int 陣列，libffi 以 pointer 大小讀 8 bytes，高 4 bytes 是相鄰元素，C 的 int 參數會忽略），第 2 槽放進全域 hll_param_slot2。
- 呼叫後清理（ffi.c:715-721）：AIN_HLL_PARAM 不做 variable_fini。S_PUSH 產生的暫存字串 slot 沒有人 unref，這是既有慣例，與 PushBack 相同。
- Array_PushBack（Array.c:118-170）的慣例：value 原值寫入；array_elem_is_ref() 為真時 heap_ref(value)；2 槽時第二槽寫 hll_param_slot2。

CN 呼叫點實際的 hll_arg3：Fill#0 為 1；Fill#1 為 2（array<string>）與 1（array<int>）；Copy#0 為 1；Copy#3 為 2；Realloc 有 65539（array<IKeyData>，2 槽介面）、65538（array<ref CEmitter>）、2、1。全 AIN 的 Array CALLHLL arg3 只出現 {2:3602, 65538:1239, 1:371, 65539:236, 196610:24, 131074:2, 196611:1} 這幾種，依 ain_code.txt grep 統計。

## proposed_code
```c
/*
 * Place this block immediately before array_select_function() in
 * src/hll/Array.c (after Array_FindIf), delete the old Array_Copy /
 * Array_Fill (current lines 2096-2138), and change the export entry
 * HLL_EXPORT(Fill, Array_Fill) to HLL_EXPORT(Fill, Array_Fill_Range).
 * HLL_EXPORT(Copy, Array_Copy) and HLL_EXPORT(Realloc, Array_Realloc) stay;
 * ffi.c overrides the pointer per declaration via array_select_function.
 */

/*
 * Fill / Copy / Realloc overloads.
 *
 * The CN AIN declares several prototypes under each of these names and ffi.c
 * builds the libffi CIF from the declaration, so every prototype needs its own
 * C entry point (see array_select_function). Semantics follow the native Array
 * dispatcher in dohnadohna_dump_SCY.exe (dispatcher 0x644300, jump table
 * 0x644f18, index = libraries.txt Array index):
 *   [1]  Realloc(self, n)             0x644383 -> CArrayPage vtbl+0x50 (0x67f4d0)
 *   [2]  Realloc(self, n, value)      0x6443b0 -> 0x647510: resize, fill [old, n)
 *   [28] Fill(self, value)            0x644769 -> fill(self, 0, Numof(self), value)
 *   [29] Fill(self, i, len, value)    0x6447be -> fill(self, i, len, value)
 *   [30] Copy(self, src)              0x6447f2 -> 0x6481a0: copy(self, 0, src, 0, Numof(src))
 *   [31] Copy(self, d, src)           0x644816 -> 0x6481d0: copy(self, d, src, 0, Numof(src))
 *   [32] Copy(self, src, s, len)      0x644846 -> copy(self, 0, src, s, len)
 *   [33] Copy(self, d, src, s, len)   0x6448b8 -> copy(self, d, src, s, len)
 * The fill helper (0x648120) and the copy helper (0x648210) clamp the range
 * to the array bounds and return the clamped count, which can be zero or
 * negative; the dispatcher returns it as int (type 10).
 */

// VM slots per logical element for the current call (2 for iface/option).
static int array_call_stride(void)
{
	return array_elem_is_2slot() ? 2 : 1;
}

static int array_call_numof(struct page *a)
{
	return (a && a->type == ARRAY_PAGE) ? a->nr_vars / array_call_stride() : 0;
}

// wrap<array> arguments arrive as the inner heap slot (ffi.c: wrap<ref_type>
// is ffi_type_sint32).
static struct page *array_wrap_page(int slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size || heap[slot].type != VM_PAGE)
		return NULL;
	struct page *p = heap[slot].page;
	return (p && p->type == ARRAY_PAGE) ? p : NULL;
}

// Store one fill value at physical index phys. Strings get a distinct heap
// slot per element: the native setter copies string content (0x646e04 ..
// 0x646e28), and S_ASSIGN through At() rewrites a string slot in place, so a
// shared slot would alias every filled element. Other heap objects keep the
// previous shared-reference behaviour (no CN call site exercises them).
static void array_fill_store(struct page *a, int phys, int value)
{
	int v = value;
	if (array_elem_is_ref() && value > 0 && (size_t)value < heap_size) {
		if (heap[value].type == VM_STRING && heap[value].s)
			v = vm_string_ref(heap[value].s);
		else
			heap_ref(value);
	}
	int old = a->values[phys].i;
	a->values[phys].i = v;
	if (array_elem_is_2slot())
		a->values[phys + 1].i = hll_param_slot2;
	if (array_elem_is_ref() && old > 0)
		heap_unref(old);
}

// Native fill helper 0x648120.
static int array_fill_range(struct page **array, int start, int count, int value)
{
	if (!array)
		return 0;
	struct page *a = *array;
	if (a && a->type != ARRAY_PAGE)
		return 0;
	int numof = array_call_numof(a);
	if (start < 0) {
		count += start;
		start = 0;
	}
	if ((long long)start + count > numof)
		count = numof - start;
	int stride = array_call_stride();
	for (int i = 0; i < count; i++)
		array_fill_store(a, (start + i) * stride, value);
	return count;
}

// [28] int Fill(ref array self, hll_param value)
static int Array_Fill_All(struct page **array, int value)
{
	return array_fill_range(array, 0, array ? array_call_numof(*array) : 0, value);
}

// [29] int Fill(ref array self, int index, int length, hll_param value)
static int Array_Fill_Range(struct page **array, int index, int length, int value)
{
	return array_fill_range(array, index, length, value);
}

// Native copy helper 0x648210.
static int array_copy_range(struct page **dst, int dst_i, struct page *src, int src_i, int count)
{
	if (!dst)
		return 0;
	struct page *d = *dst;
	if (d && d->type != ARRAY_PAGE)
		return 0;
	// Same normalisation order as 0x648240 .. 0x6482fe.
	while (src_i < 0 || dst_i < 0) {
		if (src_i < 0) {
			dst_i -= src_i;
			count += src_i;
			src_i = 0;
		} else {
			src_i -= dst_i;
			count += dst_i;
			dst_i = 0;
		}
	}
	int s_num = array_call_numof(src);
	int d_num = array_call_numof(d);
	if ((long long)src_i + count > s_num)
		count = s_num - src_i;
	if ((long long)dst_i + count > d_num)
		count = d_num - dst_i;
	if (count <= 0)
		return count;
	// Overlapping ranges inside one page copy backwards (0x64829d .. 0x6482dc).
	bool backward = d == src && dst_i > src_i && dst_i < src_i + count;
	int stride = array_call_stride();
	for (int k = 0; k < count; k++) {
		int e = backward ? count - 1 - k : k;
		if (stride == 1) {
			// Per-element array_copy keeps the existing element copy rule
			// (vm_copy by a_type) and its bounds checks.
			array_copy(d, dst_i + e, src, src_i + e, 1);
			continue;
		}
		int di = (dst_i + e) * stride, si = (src_i + e) * stride;
		int nv = src->values[si].i, old = d->values[di].i;
		if (array_elem_is_ref() && nv > 0)
			heap_ref(nv);
		d->values[di] = src->values[si];
		d->values[di + 1] = src->values[si + 1];
		if (array_elem_is_ref() && old > 0)
			heap_unref(old);
	}
	return count;
}

// [30] int Copy(ref array self, wrap<array> src)
static int Array_Copy_All(struct page **dst, int src_wrap)
{
	struct page *src = array_wrap_page(src_wrap);
	return array_copy_range(dst, 0, src, 0, array_call_numof(src));
}

// [31] int Copy(ref array self, int destIndex, wrap<array> src)
static int Array_Copy_To(struct page **dst, int dst_i, int src_wrap)
{
	struct page *src = array_wrap_page(src_wrap);
	return array_copy_range(dst, dst_i, src, 0, array_call_numof(src));
}

// [32] int Copy(ref array self, wrap<array> src, int srcIndex, int length)
static int Array_Copy_From(struct page **dst, int src_wrap, int src_i, int count)
{
	return array_copy_range(dst, 0, array_wrap_page(src_wrap), src_i, count);
}

// [33] int Copy(ref array self, int destIndex, wrap<array> src, int srcIndex, int length)
// destIndex is a plain int in every declaration; the old "dst_i_wrap" heap
// lookup would rewrite any destIndex > 0 that happens to be a live page slot.
static int Array_Copy(struct page **dst, int dst_i, int src_wrap, int src_i, int count)
{
	return array_copy_range(dst, dst_i, array_wrap_page(src_wrap), src_i, count);
}

// [2] void Realloc(ref array self, int numof, hll_param value): resize as [1],
// then fill only the added tail [old, numof) (0x647510 -> 0x648120).
// Array_Realloc counts physical slots, so the logical size is scaled here.
static void Array_Realloc_Fill(struct page **array, int numof, int value)
{
	if (!array)
		return;
	int old = array_call_numof(*array);
	Array_Realloc(array, numof * array_call_stride());
	if (numof > old)
		array_fill_range(array, old, numof - old, value);
}

static bool array_arg_is_func(const struct ain_hll_function *f, int i)
{
	enum ain_data_type t = f->arguments[i].type.data;
	return t == AIN_HLL_FUNC || t == AIN_HLL_FUNC_71;
}

static bool array_arg_is_wrap_array(const struct ain_hll_function *f, int i)
{
	return f->arguments[i].type.data == AIN_WRAP && f->arguments[i].type.array_type
	    && f->arguments[i].type.array_type->data == AIN_ARRAY;
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
	int n = f->nr_arguments;
	enum ain_data_type ret = f->return_type.data;
#define ARG(i) (f->arguments[i].type.data)
	if (!strcmp(f->name, "First")) {
		bool has_func = false;
		for (int i = 0; i < n; i++)
			has_func = has_func || array_arg_is_func(f, i);
		return has_func ? (void *)Array_First : (void *)Array_First_NoPred;
	}
	if (!strcmp(f->name, "Realloc")) {
		if (ret != AIN_VOID || n < 2 || ARG(1) != AIN_INT)
			return NULL;
		if (n == 2)
			return (void *)Array_Realloc;
		if (n == 3 && ARG(2) == AIN_HLL_PARAM)
			return (void *)Array_Realloc_Fill;
		return NULL;
	}
	if (!strcmp(f->name, "Fill")) {
		if (ret != AIN_INT)
			return NULL;
		if (n == 2 && ARG(1) == AIN_HLL_PARAM)
			return (void *)Array_Fill_All;
		if (n == 4 && ARG(1) == AIN_INT && ARG(2) == AIN_INT && ARG(3) == AIN_HLL_PARAM)
			return (void *)Array_Fill_Range;
		return NULL;
	}
	if (!strcmp(f->name, "Copy")) {
		if (ret != AIN_INT)
			return NULL;
		if (n == 2 && array_arg_is_wrap_array(f, 1))
			return (void *)Array_Copy_All;
		if (n == 3 && ARG(1) == AIN_INT && array_arg_is_wrap_array(f, 2))
			return (void *)Array_Copy_To;
		if (n == 4 && array_arg_is_wrap_array(f, 1) && ARG(2) == AIN_INT && ARG(3) == AIN_INT)
			return (void *)Array_Copy_From;
		if (n == 5 && ARG(1) == AIN_INT && array_arg_is_wrap_array(f, 2)
		    && ARG(3) == AIN_INT && ARG(4) == AIN_INT)
			return (void *)Array_Copy;
		return NULL;
	}
#undef ARG
	return NULL;
}
```

## selector
```c
/* src/ffi.c link_static_library(): route Fill/Copy/Realloc through the
 * declaration-based selector as well (currently only "First"). */
				if (!strcmp(lib->name, "Array") && (!strcmp(ainlib->functions[i].name, "First")
				    || !strcmp(ainlib->functions[i].name, "Fill")
				    || !strcmp(ainlib->functions[i].name, "Copy")
				    || !strcmp(ainlib->functions[i].name, "Realloc"))) {
					extern void *array_select_function(const struct ain_hll_function *f);
					funcptr = array_select_function(&ainlib->functions[i]);
				}

/* src/hll/Array.c array_select_function(): branches added (full function is
 * in proposed_code). Unknown shapes return NULL -> left unlinked, same
 * convention as array_erase_function / array_query_function. */
	if (!strcmp(f->name, "Realloc")) {          // [1] (self,int)  [2] (self,int,hll_param)
		if (ret != AIN_VOID || n < 2 || ARG(1) != AIN_INT)
			return NULL;
		if (n == 2)
			return (void *)Array_Realloc;
		if (n == 3 && ARG(2) == AIN_HLL_PARAM)
			return (void *)Array_Realloc_Fill;
		return NULL;
	}
	if (!strcmp(f->name, "Fill")) {             // [28] (self,hll_param)  [29] (self,int,int,hll_param)
		if (ret != AIN_INT)
			return NULL;
		if (n == 2 && ARG(1) == AIN_HLL_PARAM)
			return (void *)Array_Fill_All;
		if (n == 4 && ARG(1) == AIN_INT && ARG(2) == AIN_INT && ARG(3) == AIN_HLL_PARAM)
			return (void *)Array_Fill_Range;
		return NULL;
	}
	if (!strcmp(f->name, "Copy")) {             // [30]..[33]
		if (ret != AIN_INT)
			return NULL;
		if (n == 2 && array_arg_is_wrap_array(f, 1))
			return (void *)Array_Copy_All;
		if (n == 3 && ARG(1) == AIN_INT && array_arg_is_wrap_array(f, 2))
			return (void *)Array_Copy_To;
		if (n == 4 && array_arg_is_wrap_array(f, 1) && ARG(2) == AIN_INT && ARG(3) == AIN_INT)
			return (void *)Array_Copy_From;
		if (n == 5 && ARG(1) == AIN_INT && array_arg_is_wrap_array(f, 2)
		    && ARG(3) == AIN_INT && ARG(4) == AIN_INT)
			return (void *)Array_Copy;
		return NULL;
	}
/* Note: the new version also requires arguments[0] to be AIN_REF_ARRAY or
 * AIN_REF_ARRAY_TYPE, and moves the has_func scan into the First branch.
 * Probe fc-selector confirms that under the real AIN, First#0(67) and
 * First#2(69) are still bound, and that decls 1, 2, 28-33 are all non-NULL
 * with distinct entry points. */
```

## fixture_plan
fixture 位置：<scratchpad>/overload/fill-copy-realloc/probe/fill_copy_fixture.inc。它掛在 scratch 複製的 runtime_probe.c 上，main 加了一行 `if(!strncmp(argv[2],"fc-",3))return test_fill_copy(argv[2]);`，重用 first_overload_fixture.inc 的 fo_find_lib、fo_find_fn、fo_make_string_array。

建置與執行：
- 建置用 scratch 的 build.py：沿用 asan-build 的 compile_commands 與 ninja link 命令。base 用 worktree 原始 ffi.c 與 hll_Array.c.o；cand 用 scratch/cand 的 Array.c 與 ffi.c。
- 執行：`ASAN_OPTIONS=detect_leaks=0 ./runtime-probe-{base,cand} <game-workcopy dohnadohna.ain> <mode>`。
- 真 AIN 的宣告索引為 Fill=28,29、Copy=30..33、Realloc=1,2、First=67,69。

測試點（輸入 -> 預期；結果記為 base / cand exit）：
1. fc-selector：列出 decl 1、2、28-33、67、69 的 fun 指標，要求非 NULL 且 28≠29、30≠33、1≠2。base 88（同名共用一個指標），cand 0。
2. fc-fill0：重現 475905 CDrawMovie@Release。int[1,1,1,1,1] 以 Fill#0(value=0)、arg3=1 呼叫，預期 [0,0,0,0,0]、ret 5。base 維持不變、ret 殘值；cand PASS。
3. fc-fill1-int：重現 831929 CASConfigData@0。int[50] 全 0，以 Fill#1(0,50,-1)、arg3=1 呼叫，預期全 -1、ret 50。base 全 0；cand PASS。
4. fc-fill1-str：重現 376271 SSystemInfo@0。string["a","b","c"] 以 Fill#1(0,3,S"0")、arg3=2 呼叫，預期三個元素都是 "0"、ret 3；再用 heap_string_assign 改寫 e0，e1、e2 必須仍是 "0"（驗證沒有共用 slot 造成別名）。base 不變；cand PASS。
5. fc-fill-clamp：int[5] 依序呼叫 Fill#1(3,10,7)、(-2,4,8)、(7,1,9)，預期 [8,8,0,7,7]、ret 2,2,-2（對應原版 0x648120 的夾值規則）。base 得 [0,7,7,7,7]，吻合錯位推導；cand PASS。
6. fc-copy0：重現 153716。dst int[3]、src int[9,8,7,6] 以 Copy#0 呼叫，預期 [9,8,7]、ret 3。base 為 no-op；cand PASS。
7. fc-copy3：重現 524459/524466。dst 空 page 先 Realloc#0(3)，再 Copy#3(dst,0,src[4,5,6],0,3)、arg3=2，預期 [4,5,6]、ret 3。base 內容正確但 ret 是殘值；cand PASS。
8. fc-copy3-str：重現 738876。string 陣列以 Copy#3 呼叫、arg3=2，預期內容 x/y 且 dst slot 與 src slot 不同。base 只有 ret 錯；cand PASS。
9. fc-copy3-dest1：dst int[4]、src[5,6,7] 以 Copy#3(1,src,0,3) 呼叫，預期 [0,5,6,7]、ret 3。base 在 page.c:784 VM_ERROR 後 abort（exit 134）；cand PASS。
10. fc-copy-overlap：同一陣列 [1,2,3,4,5] 以 Copy#3(1,self,0,4) 呼叫，預期 [1,1,2,3,4]、ret 4。base abort；cand PASS。
11. fc-realloc：[1,2] Realloc#0(4) 預期 [1,2,0,0]；[1,2] Realloc#1(4,9) 預期 [1,2,9,9]；再 Realloc#1(1,5) 預期 [1]。base 在 #1 處 FAIL；cand PASS。

回歸：cand 跑既有 first-overload、personality、heap-reuse、assignment 全部 exit 0。

移入正式 harness 時：把 fill_copy_fixture.inc 放進 probe 目錄，並在 runtime_probe.c 加 include 與 fc- 分派。

## open
- Array_Realloc（Array.c:1610-1645）是 Realloc#0 的綁定，CIF 正確，但和原版語義有差距。這次沒有改它，只在 [2] 外包一層。差距如下：(a) 在 page 為 NULL 時建 AIN_ARRAY_INT 空頁、不建 struct；原版未初始化時走 Alloc(n)（0x67f4d0 -> vtbl+0x4c）。(b) 縮小時沒有 unref 被截掉的 ref 元素，會洩漏；原版逐一解構（0x67f554-0x67f570 呼叫 0x680270）。(c) new_size<0 直接 return；原版 n<=0 清空。(d) 字串新元素是 0；原版預設建構，推測是空字串，未驗證（0x680170）。要不要一併修，請決定。
- Array_Realloc 與 Array_Alloc 在 page 沒有 struct_type 時，會把 hll_current_arg3 & 0xFFFF 當 struct index（Array.c:1627-1628、98-99）。但 CN AIN 的 Array arg3 只有 {1,2,0x10002,0x10003,0x20002,0x30002,0x30003} 這幾種，低 16 位不是 struct index。例如 348627 Realloc 65538 的對象是 array<ref elkeditor::detail::CEmitter>（structures.txt:2451），會被建成 struct #2 instance。另外 generic AIN_ARRAY page 的 array.struct_type 存的是 elem_slots（vm.c:4960、4975），同樣會被 Realloc 誤當 struct index。已知受影響的只有 elkeditor 與 sealtool（array<array<SAction>>）這類 editor 呼叫點；對遊玩流程的影響未驗證。
- 296106 Realloc 65539 的對象是 array<elkeditor::detail::IKeyData>（2 槽介面）。Array_Realloc 以物理槽計數，Realloc(n) 只會得到 n/2 個邏輯元素。候選的 Array_Realloc_Fill 已經換算 stride，但 [1] 本身沒有換算。editor 限定，要不要改請決定。
- 原版 Alloc（vtbl+0x4c = 0x67f4a0）是先呼叫 +0x54（推測為清空）再重新 alloc(n, init)，不保留舊元素。worktree 的 Array_Alloc 則刻意保留舊元素（Array.c:64-68 註解）。這不屬於本組，建議 Alloc 負責組用反組譯 0x67f4a0 / 0x67fe20 確認。
- hll_param 的字串暫存（S_PUSH）在 ffi.c:715-721 不做 variable_fini，每次呼叫洩漏一個 ref。這是既有慣例，PushBack 也一樣。候選 Fill 也沒有 unref value slot，因為 .LOCALREF 推入的可能是變數本身的 slot，unref 會釋放活物件。v14 下 hll_param 的所有權契約未驗證。
- 候選 Fill 只把 VM_STRING 改為逐元素複製；其他 heap 物件（值型 struct、巢狀 array、wrap）仍沿用舊的共用 ref 加 heap_ref。原版 0x646d70 在這些型別分支上的語義沒有逐一反組譯（型別 switch 表在 0x647064/0x647088），未驗證。CN 的 Fill 呼叫點只有 int 與 string。
- 候選 Copy 在 stride 1 時沿用 array_copy 的元素複製規則，也就是依 a_type 做 vm_copy。generic AIN_ARRAY 會被 array_type() 當成 AIN_STRUCT 深拷貝，這對 array<ref T> 是否正確未驗證。CN 的 Copy 呼叫點是 array<int>、array<string>、array<SPartsUpdateData>（值 struct），在 probe 中皆正確。
- 附帶發現（不屬於本組）：Array 跳表 index 27 Any(self,hll_func) 與 index 57 IsExist(self,hll_func) 同樣指向 0x64473a，即 Any(pred) 與 IsExist(pred) 共用同一段原生實作；index 12 Concat 與 13 AddRange 也同為 0x6444ca。可轉給 Any 組。
- baseline probe 在 VM_ERROR 路徑同時出現 ASan heap-buffer-overflow（_vm_error、instrumented-vm.inc:5580），疑似錯誤訊息格式化的既有問題，與本組無關，未追查。

## artifacts
- <scratchpad>/overload/fill-copy-realloc/cand/block.c
- <scratchpad>/overload/fill-copy-realloc/cand/selector.c
- <scratchpad>/overload/fill-copy-realloc/cand/Array.c
- <scratchpad>/overload/fill-copy-realloc/cand/ffi.c
- <scratchpad>/overload/fill-copy-realloc/cand/array.diff
- <scratchpad>/overload/fill-copy-realloc/cand/ffi.diff
- <scratchpad>/overload/fill-copy-realloc/probe/fill_copy_fixture.inc
- <scratchpad>/overload/fill-copy-realloc/probe/runtime_probe.c
- <scratchpad>/overload/fill-copy-realloc/build.py
- <scratchpad>/overload/fill-copy-realloc/runtime-probe-base
- <scratchpad>/overload/fill-copy-realloc/runtime-probe-cand
- <scratchpad>/overload/fill-copy-realloc/run-base-fc-*.log
- <scratchpad>/overload/fill-copy-realloc/run-cand-*.log
- <scratchpad>/overload/fill-copy-realloc/realloc.asm.txt
- <scratchpad>/overload/fill-copy-realloc/realloc-helpers.asm.txt
- <scratchpad>/overload/fill-copy-realloc/fill-copy.asm.txt
- <scratchpad>/overload/fill-copy-realloc/fill-copy-helpers.asm.txt
- <scratchpad>/overload/fill-copy-realloc/copy-tail.asm.txt
- <scratchpad>/overload/fill-copy-realloc/int-return.asm.txt
- <scratchpad>/overload/fill-copy-realloc/vt-alloc-realloc.asm.txt
- <scratchpad>/overload/fill-copy-realloc/elem-set.asm.txt
- <scratchpad>/overload/fill-copy-realloc/elem-copy.asm.txt
- <scratchpad>/overload/fill-copy-realloc/vt.py

## 驗證 refuted=True residual=0.85
我用自己的 capstone 腳本（scratchpad/overload/fill-copy-realloc-refute/x86d.py）重新反組譯並逐條核對，另外讀了 Array.c、ffi.c、page.c、vm.c 與 ain_code.txt 的 22 個呼叫點。

以下部分成立：
- 參數錯位推導成立。ffi.c 的 CIF 依 AIN 宣告建立：wrap<array> 對應 sint32（ffi.c:1131-1141），hll_param 對應 pointer（ffi.c:1100）。舊的 Array_Fill(array, value, start, count) 對到 Fill#1 會錯位成 value←index、start←length、count←value；對到 Fill#0／Copy#0 會讀到暫存器殘值。
- dispatcher 參數映射成立。0x644769、0x6447be、0x6447f2、0x644816、0x644846、0x6448b8 的 arg 偏移（+0x28/+0x50/+0x78/+0xa0）與推導一致；0x6443b0 對應 0x647510，流程是先 Numof，再 vtbl+0x50，最後 fill(old, n-old)。
- fill helper 0x648120 的夾限與回傳 count 已逐行確認。copy helper 0x648210 的負索引正規化迴圈（0x648240、0x6482ea、0x6482f8）、重疊時反向複製的條件（0x648292-0x6482a6），以及 count<=0 直接返回，也都確認無誤。
- 0x646d70 的 type 12（字串）分支確實經 vtbl+0x3c 依內容複製；heap_string_assign（heap.c:860）先 free 再 string_ref，所以逐元素 vm_string_ref 的修法是安全的。

以下部分被推翻或需修正：
- copy helper 的第一道夾限。0x648262 讀的是 [esp+0x10]，而 0x648220 存進去的是 ecx=dst，不是 src；原版在 src 溢出時改用 dst.Numof - srcIndex 夾 count。調查者寫的「依 src.Numof 夾 count」與原版不符，候選碼 cand/Array.c:2163-2164 照這個錯誤描述實作。
- Realloc 的「殘值回傳」說法不成立，因為兩個宣告都是 void。
- 值型 struct 的 Fill 共用 ref，與原版 type 13 的逐元素指派不符。

CN 的實際呼叫點（153651、153716、376271、475885、475905、524466、738876、739883、831929）都不會走進上述分歧路徑，所以候選修補對遊玩流程的主要修正（Fill#0、Fill#1 真正填值、Copy#0 真正複製、字串不再別名）仍然有效。但語義描述有一條可證偽的錯誤，依預設立場判為 refuted。

### code_bugs
- array_copy_range 第一道夾限（cand/Array.c:2163-2164，`if (src_i + count > s_num) count = s_num - src_i;`）與原版不符。EXE 0x648250-0x64826f 的流程是：先比較 srcIndex+count 與 src->Numof（ecx=[ebp+8]=src），超出時改用 [esp+0x10]，也就是 dst（0x648220 存入）的 Numof，令 count = dst.Numof - srcIndex，不是 src.Numof - srcIndex。反例一：dst n=3、src n=5、Copy#2(dst, src, 2, 10)。原版 count=1、回 1；候選 count=3、回 3。反例二：dst n=5、src n=3、Copy#2(dst, src, 1, 10)。原版 count=4，會讀 src[3]、src[4] 越界，經 0x6451d0 走錯誤路徑（呼叫 0x7285d0 報『配列要素取得失敗』類訊息後回 0），dst 因此被寫入 0，回 4；候選回 2。要照原版實作，必須自己擋掉 src 越界讀取，不能交給 array_copy，否則會 VM_ERROR。CN 的 3 處 Copy#3 與 1 處 Copy#0 都是 srcIndex=0、length=Numof(src)，不會觸發這條分支，所以目前遊玩流程無差異。
- array_fill_store（提議碼：Fill/Copy/Realloc 區塊中的 array_fill_store）只對 VM_STRING 逐元素複製，其餘 heap 物件共用同一個 slot 加 heap_ref。但 arg3=2 同時涵蓋值型 struct 陣列（AIN_ARRAY_STRUCT）。原版 0x646d70 的 type 13（struct）分支在 0x646e38-0x646e76：先用 vtbl+0x1c 取出該元素既有的物件，再以來源 struct 呼叫 vtbl+4 逐一指派，每個元素各自是一份值，不是共用一個 ref。候選對 array<值struct> 做 Fill 或 Realloc#1 時，所有元素會別名到同一個物件，跟當初字串 slot 的別名是同一類缺陷。vtbl+4 的確切語義是從呼叫形狀推論，未逐行驗證。建議改依 array_type(a->a_type) 走 vm_copy，同 page.c:804 array_fill 的做法；但不要照抄該函式結尾的 variable_fini(v)，那會釋放 hll_param。CN 的 Fill 呼叫點只有 int 和 string，現況不受影響。
- Array_Realloc_Fill 直接沿用 Array_Realloc（Array.c:1626-1637）。當 arg3==2 或 arg3>=0x10000 且 struct_type>=0 時，新增尾段會先 alloc_struct 再 heap_ref 建出 struct 實例，建構子已經執行，隨後又被 fill 覆寫。arg3>=0x10000 時 struct_type 取自 arg3&0xFFFF，並不是真正的元素型別，例如 65538 會建成 struct #2。2 槽元素（例如 65539）時 Array_Realloc 以物理槽計數，每個物理槽都配一個 struct；fill 只 unref 第 0 槽，第 1 槽被 hll_param_slot2 覆寫但沒有 unref，造成洩漏。原版 0x647510 的做法是 vtbl+0x50 Realloc(n) 後直接 0x648120 fill(old, n-old)，沒有丟棄式建構。CN 沒有 Realloc#1 呼叫點，屬 latent。
- 調查摘要有事實錯誤：『Realloc#0 [1] … 回傳值是殘值、所有 CN 呼叫點都立刻 POP』。Realloc 兩個宣告都是 void（libraries.txt Array 段 [1][2]），ffi.c:753 起 AIN_VOID 不 push 任何值，所以不存在殘值回傳。CN 的 Realloc 呼叫點後面也沒有 POP，例如 ain_code.txt 524459 CALLHLL Array Realloc 2 的下一行 524460 就是 .LOCALREF list。只有 Fill/Copy（int）的殘值結論成立。
- stride==1 的 Copy 逐元素呼叫 array_copy（page.c:775-802），它依 array_type(dst->a_type) 做 vm_copy。對 generic AIN_ARRAY 頁，array_type 回 AIN_STRUCT（page.c:195-202）；vm_copy 會把 int 值當 heap slot 用 vm_copy_page 深拷貝（vm.c:950-960），variable_set 也會把舊值當 slot 做 unref。baseline 的 Copy#0 是 no-op，候選則讓 153716 真的執行複製。該處 m_numberList 是 array<int>（structures.txt:850），經 page.c:450-455 會成為 AIN_ARRAY_INT，所以安全。但 int 或 enum 元素落在 generic AIN_ARRAY 頁時會破壞 heap refcount：page.c:450 的 switch 沒有 AIN_ENUM，X_A_INIT 在 arg!=0 時也保持 generic。未實測，屬風險。
- 驗證覆蓋缺口有三：(a) fc-selector 只檢查 First decl 67 與 69，CN 實際呼叫的是 First#1(68) 與 First#3(70)。四者簽名成對相同，風險低，但沒有直接證據。(b) Copy#1[31]、Copy#2[32] 只驗到綁定，沒有行為 probe。(c) 沒有任何 probe 測 srcIndex+length > src.Numof 且 dst/src 大小不同的情況，也就是第 1 點原版與候選分歧的地方。
- 邊界 UB（次要）：array_fill_range 的 `count += start` 與 array_copy_range 的 `dst_i -= src_i`／`count += src_i`，在 INT_MIN 附近會 signed overflow；x86 原版是 wrap-around。CN 不會觸發。

### corrected
大部分同意，只有一處推翻。

同意的部分：
- [1] Realloc(n)：vtbl+0x50。
- [2] Realloc(n, v)：0x647510，先 old=Numof，再 Realloc(n)，然後 fill(start=old, count=n-old, v)。n<old 時不填。回傳 void。
- [28] Fill(v)：0x644769 → fill(0, Numof, v)，回傳 int count；陣列無效時回 0。
- [29] Fill(i, len, v)：0x6447be → fill(i, len, v)，回傳 int count。
- fill helper 0x648120：start<0 時 count+=start、start=0；start+count>Numof 時 count=Numof-start；逐元素用 0x646d70 設值；回傳 count，可能是 0 或負數。
- 0x646d70 的元素設值方式：int 直接設；string 依內容複製；struct（type 13）逐元素指派，每個元素各自是一份值。這一點與候選「非字串共用 ref」的做法不符。
- [30] 0x6481a0 = copy(dst, 0, src, 0, Numof(src))。
- [31] 0x6481d0 = copy(dst, d, src, 0, Numof(src))。
- [32] = copy(dst, 0, src, s, len)，dst 無效時回 0。
- [33] = copy(dst, d, src, s, len)。

推翻的部分是 copy helper 0x648210 的夾限步驟，正確流程如下：
1. dst 無效時回 0。
2. 迴圈直到兩個索引都 >=0：srcIndex<0 時 dstIndex-=srcIndex、count+=srcIndex、srcIndex=0；dstIndex<0 時 srcIndex-=dstIndex、count+=dstIndex、dstIndex=0。
3. 若 srcIndex+count > **src**.Numof，則 count = **dst**.Numof - srcIndex。這是原版的怪癖，不是 src.Numof - srcIndex。
4. 若 dstIndex+count > dst.Numof，則 count = dst.Numof - dstIndex。
5. count<=0 時直接回傳 count。
6. dst==src 且 srcIndex < dstIndex < srcIndex+count 時反向逐元素複製，否則正向（0x6470e0）。
7. 若第 3 步讓讀取超出 src 範圍，0x6451d0 會報錯並取 0 值寫入 dst，未驗證是否致命。
8. 回傳 count。

受影響的只有 [32] 和 [33] 在 srcIndex+length > src.Numof 且 dst、src 大小不同時；[30]、[31] 與 CN 全部呼叫點都不會觸發。

另外修正一個事實：Realloc 兩個宣告都是 void，沒有殘值回傳，CN 呼叫點也沒有 POP。

### counter_evidence
一、copy helper 夾限（SCY EXE 以 capstone 重新反組譯）：
- 0x648220 `mov [esp+0x10], eax`，此時 eax=ecx=dst（呼叫端 0x6448f8 `mov ecx,eax`，eax 為 arg0 的 getArray 結果）。
- 0x648250 `mov ecx,[ebp+8]`（src）後 `call [eax+0xc]`，取得 src.Numof。
- 0x64825b-0x648260 比較 srcIndex+count 與 src.Numof，超過時往下走。
- 0x648262 `mov ecx,[esp+0x10]`（dst）後呼叫 Numof。
- 0x64826b-0x64826d `ebx = eax - edi`，即 count = dst.Numof - srcIndex。

二、src 越界讀取：0x6451d0 的 vtbl+0x10 回 NULL 時，組好訊息字串 0x7e1fa0（含 `(Page : %d, Index : %d)` 等格式）交給 0x7285d0，之後 `xor eax,eax` 回 0。

三、值型 struct 的 Fill：0x646d70 的型別跳表（0x647088 索引表、0x647064 目標表）中 type 13 指向 0x646e38。該段先以 vtbl+0x1c 取出元素物件，再以 0x67ac30(value+0x20) 取得來源 struct，呼叫元素的 vtbl+4。這是逐元素指派，不是共用 ref。

四、Realloc 回傳：libraries.txt Array 段 [1][2] 都宣告為 void。ffi.c 的 AIN_VOID 分支不 push。ain_code.txt 524459 的 Realloc 後面緊接 524460 `.LOCALREF list`，沒有 POP。

五、候選 probe log（run-cand-fc-selector.log）只列出 First#0=67、First#2=69，沒有 68／70；fill_copy_fixture.inc 沒有 Copy#1、Copy#2 的行為模式，也沒有 src 溢出且大小不同的模式。
