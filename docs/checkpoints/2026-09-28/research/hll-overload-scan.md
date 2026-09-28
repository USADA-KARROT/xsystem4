# HLL 同名多宣告（overload）缺陷全庫掃描 — 2026-09-28

- 對象：xsystem4 worktree `worktrees/xsystem4-cn-on-upstream`，分支 `wip/post-checkpoint-2026-07-06`，HEAD `a8d92df`（唯讀，未改任何檔案；`git status` 只有既存的 `?? subprojects/.wraplock`）。
- 遊戲：多娜多娜繁中版 AIN v14（`game-workcopy/.../dohnadohna.ain`），dump `<cn-dump>/{libraries.txt,ain_code.txt}`。
- 臨時檔/探針原始碼：`<scratchpad>/overload/hllscan/`（session scratchpad，可能被清）。

## 0. 結論摘要

1. **36 個 HLL 庫中，同名且形狀不同的宣告共 38 組**（Array 24 組、Math 4、String 8、HashMap 1；另有 Array.ShallowCopy/At 兩組同簽名重複，無害）。
2. **Array 已知五組以外的新發現（依走錯呼叫數）**：
   - `Math.Abs/Min/Max/Clamp` 的 float 與 3/4 參數多載全部綁到 int 版 C 函式：**122 次呼叫走錯**。arm64 探針實測 float 版一律回傳第一個參數原值（`Abs(-3.5)=-3.5`、`Clamp(5,0,1)=5`、`Min(5,2)=5`），int 3/4 參數版只比前兩個。直接打壞 `FLOAT_EQUALS`（fno 20536）、`Motion::EasingCalculator::GetByType<float>`、`GameConfig@GameSpeedRate::set`、`TimerCallback@TimeRate::get` 等 35 處 float Clamp。
   - `Array.Sort`/`Array.QuickSort`（不在已知清單）：`Sort#0(self,hll_func)` 41 次、`QuickSort#1(self,hll_func)` 10 次 comparator 被忽略；`Sort#1`/`QuickSort#0` 共 22 次雖然形狀可用，但 C 為 `void`、AIN 回傳 `wrap<?>`，殘值被推上堆疊並在 dummy local 上 `DELETE`；字串陣列會按 heap slot 編號排序。原版 EXE 四個多載各走不同 case（VA 0x644907/0x644927/0x644972/0x64498b）。
   - `String.GetPart#0(self,index)` 16 次：綁到三參數 `String_GetPart(self,begin,length)`，`length` 讀暫存器殘值（UB）。呼叫點語義是「從 index 取到字尾」。
   - `String.Match#0(self,regex)` 6 次：綁到 `String_Match(self,int ml_slot,regex)`，regex 指標被當 slot 讀取，C 自己的 regex 參數是殘值；探針實測 `Match("FooterButton_1","^zzz$")=1`。受影響的是 `ActivityHelper::GetUser<FooterButton/RivalShopView/SkillSelector/BattleBonusView/BattleResultPlayerView/RoundBonusButton>` 的 `Where` 過濾，實際上會變成不過濾。
   - `HashMap.Any#0(id)` 1 次：綁到 `HashMap_Any(id,key)`，key 讀殘值；探針實測非空 map 回 0。
   - 新發現合計 **196 次呼叫確定走錯實作**，另有 22 次屬於「形狀可用，但回傳或語義錯」（△）。
3. **未綁定但 AIN 有呼叫**：98 個宣告、172 個靜態呼叫點，全部走 UNIMPL（pop 參數、非 void 推 0）。前 40 名見第 5 節；以 PartsEngine component 屬性 getter/setter、child 操作與 HTTPDownloader 為主。
4. **延伸發現（不是多載問題，但屬同一類「AIN CIF ≠ C 原型」）**：用 DWARF 全量比對 1595 個已綁定宣告後，另有數個會解參考 `wrap<ref>` slot 的函式，探針實測 SIGSEGV：`Array.Duplicate`（14 處）、`Array.Equals#0`（3 處）、`TextFile.ReadAll/Read/ReadLine`（8 處）。另外 `VSFile.ReadString`（5 處，含 gamesave）、`FileOperation.GetFileList/GetFolderList`（16 處）、SealEngine 三個 string 回傳函式屬同型問題。見第 6 節。

## 1. 方法與證據來源

| 步驟 | 作法 | 證據 |
|---|---|---|
| 宣告解析 | python 以 byte 讀 `libraries.txt`，依 `--- <Lib> ---` 分段、GB18030 解碼，0-based index | 36 庫、1701 宣告；與 runtime `ain->libraries` 逐筆比對名稱與參數數，**0 筆不符** |
| 呼叫統計 | python byte 級掃 `ain_code.txt` 的 `CALLHLL <Lib> <Name>[#N] <arg3>` | 11590 個 CALLHLL、1562 個 (Lib,Name,#N)；所有 (Lib,Name,#N) 都能對回宣告 index |
| 實際綁定 | **自建唯讀列舉探針**：`#include` production `src/ffi.c`，依 production 順序跑 `_PreLink`（略過 MsgSkip）→ `link_libraries()` → `_PostLink`，印出每個 AIN 宣告實際的 `libraries[i][j].fun` 與依 AIN 建立的 CIF 型別；符號名以 `nm` 取得 | `scratchpad/.../hllscan/enum_probe.c`、`bind.tsv`；36 庫全部 linked，1595 個已綁定、106 個未綁定 |
| C 原型 | 對探針 binary 跑 `dsymutil` 與 `dwarfdump`，以 low_pc 對應綁定位址，取出 C 參數與回傳型別 | `dwarf_subs.json`、`audit2.json`；libc 函式（abs 等）依 `Math.c:289-298` 人工補 |
| ABI 實測 | 自建 `abi_probe.c`：依 AIN 宣告的 CIF 直接 `ffi_call` 已綁定函式；可能崩潰的案例放在 fork 子程序跑 | `abi.out`、`abi.err`（UBSan 位址） |
| 原版語義 | capstone 反組譯 `dohnadohna_dump_SCY.exe` Array dispatcher case 34–39 | 第 3.5 節 VA |

探針建置方式：沿用 `fixtures/probe-harness/work/stage2/asan-build` 的 compile_commands 與 `string-stage4-20260926/probe/build-asan.json` 的 link_command，只把 `ffi.c.o`/`system4.c.o` 換成 scratchpad 內自編的物件，輸出也寫在 scratchpad。**沒有改動 harness 或 worktree。** asan-build 的物件時間戳（1790596753）晚於 worktree 最後修改的原始檔（1790596736），因此代表 HEAD 的程式。註：20:15:19 另有其他程序改動 harness 的 `probe/runtime_probe.c`，並新增 `array_overload_fixture.inc`；那不是本掃描造成的，本掃描也沒有使用這兩個檔案。

綁定規則（程式證據）：
- `src/ffi.c:1155-1192` `link_static_library`：每個 AIN 宣告 i 取 C 表**第一個同名**項目（`ffi.c:1162`、`1183-1184` break）；只有 Array 的 Erase/IsExist/First/Numof/Count/Find 經 `array_*_function` 分流。CIF 永遠依 AIN 宣告建立（`link_static_library_function`）。
- `src/ffi.c:1263` `static_library_replace`：在 `_PreLink` 改 C 表第一個同名項目。`src/ffi.c:1276-1294` `static_library_register`：在 `_PostLink` 只補**第一個**同名 AIN 宣告，而且只補 NULL 的（`ffi.c:1287-1289`）。
- `_ModuleInit` 內沒有任何 `static_library_register` 或 `static_library_replace`（grep：register 只出現在 PartsEngine.c 的 `pe_v14_register_batch`、pe_v14_activity.c、pe_v14_message.c、pe_v14_prelink.h，都在 `_PostLink` 路徑），所以不跑 `_ModuleInit` 不影響綁定結果。
- UNIMPL：`src/ffi.c:303-382`，依 AIN 型別 pop 參數，非 void 時 `stack_push(0)`（`ffi.c:378-380`）。
- 回傳：`AIN_STRING/AIN_HLL_PARAM` 把 `r.ref` 當 `struct string*`（`ffi.c:761-764`）；`AIN_WRAP` 直接 `stack_push(r)`（`ffi.c:774-789`，只有 EmplaceBack 特判）；`AIN_REF_HLL_PARAM` 忽略 C 回傳（`ffi.c:791`）。

限制：
- 呼叫數是 bytecode **靜態呼叫點數**，不是執行頻率。
- 殘值行為在 arm64 + libffi 3.5.2 + ASan build 上實測；production（-O2）的殘值內容不同，本報告只能主張「未定義／不可依賴」，具體數值未驗證。

## 2. 同名多宣告總表（runtime 綁定 + 判定）

判定符號：✓ 正確；✗ 走錯實作；△ 形狀可用但回傳或語義錯；潛在 = 目前 0 呼叫。粗體為本次新發現。Array/String 的簽名省略 self 型別。

| 庫.名稱 | #N → [index] 簽名 | 呼叫數 | runtime 實際綁定（C 原型） | 判定 |
|---|---|---:|---|---|
| Array.Realloc | #0 → [1] `void Realloc(self, int)` | 13 | `Array_Realloc(page **, int)` | ✓ |
| Array.Realloc | #1 → [2] `void Realloc(self, int, hll_param)` | 0 | `Array_Realloc(page **, int)` | 潛在：value 被忽略（0 呼叫） |
| Array.Erase | #0 → [15] `bool Erase(self, int, int)` | 124 | `Array_Erase(page **, int, int)` | ✓ 已分流 |
| Array.Erase | #1 → [16] `bool Erase(self, hll_func)` | 45 | `Array_EraseIf(page **, int)` | ✓ 已分流 |
| Array.Erase | #2 → [17] `bool Erase(self, wrap<array<hll_param> >)` | 3 | `Array_EraseValues(page **, int)` | ✓ 已分流 |
| Array.Numof | #0 → [20] `int Numof(self)` | 519 | `Array_Numof(page **)` | ✓ 已分流 |
| Array.Numof | #1 → [21] `int Numof(self, hll_func)` | 7 | `Array_CountIf(page **, int)` | ✓ 已分流 |
| Array.Count | #0 → [22] `int Count(self)` | 128 | `Array_Numof(page **)` | ✓ 已分流 |
| Array.Count | #1 → [23] `int Count(self, hll_func)` | 12 | `Array_CountIf(page **, int)` | ✓ 已分流 |
| Array.Any | #0 → [26] `bool Any(self)` | 37 | `Array_Any(page **)` | ✓ |
| Array.Any | #1 → [27] `bool Any(self, hll_func)` | 46 | `Array_Any(page **)` | ✗ 已知：謂詞被忽略 |
| Array.Fill | #0 → [28] `int Fill(self, hll_param)` | 3 | `Array_Fill(page **, int, int, int)` | ✗ 已知：C 多讀 start/count 殘值 |
| Array.Fill | #1 → [29] `int Fill(self, int, int, hll_param)` | 2 | `Array_Fill(page **, int, int, int)` | ✗ 已知：參數順序錯位（C=value,start,count） |
| Array.Copy | #0 → [30] `int Copy(self, wrap<array<hll_param> >)` | 1 | `Array_Copy(page **, int, int, int, int)` | ✗ 已知：C 多讀 3 參數；void 回傳殘值 |
| Array.Copy | #1 → [31] `int Copy(self, int, wrap<array<hll_param> >)` | 0 | `Array_Copy(page **, int, int, int, int)` | 潛在（0 呼叫） |
| Array.Copy | #2 → [32] `int Copy(self, wrap<array<hll_param> >, int, int)` | 0 | `Array_Copy(page **, int, int, int, int)` | 潛在（0 呼叫） |
| Array.Copy | #3 → [33] `int Copy(self, int, wrap<array<hll_param> >, int, int)` | 3 | `Array_Copy(page **, int, int, int, int)` | △ 形狀一致；C void → AIN int 回傳殘值 |
| Array.Sort | #0 → [34] `wrap<?> Sort(self, hll_func)` | 41 | `Array_Sort(page **)` | **✗ 新：comparator 被忽略＋void 回傳殘值** |
| Array.Sort | #1 → [35] `wrap<?> Sort(self)` | 12 | `Array_Sort(page **)` | **△ 新：void 回傳殘值；非 int 元素以 .i 排序** |
| Array.QuickSort | #0 → [38] `wrap<?> QuickSort(self)` | 10 | `Array_QuickSort(page **, int)` | **△ 新：C 多讀 comparator（未使用）；字串以 heap slot 排序；void 回傳殘值** |
| Array.QuickSort | #1 → [39] `wrap<?> QuickSort(self, hll_func)` | 10 | `Array_QuickSort(page **, int)` | **✗ 新：comparator 被 `(void)` 忽略＋void 回傳殘值** |
| Array.Equals | #0 → [40] `bool Equals(self, wrap<array<hll_param> >)` | 3 | `Array_Equals(page **, page **)` | ✗ 已知組；另：wrap→`page**` 解參考 SIGSEGV（探針） |
| Array.Equals | #1 → [41] `bool Equals(self, wrap<array<hll_param> >, hll_func)` | 0 | `Array_Equals(page **, page **)` | 潛在（0 呼叫） |
| Array.Find | #0 → [42] `int Find(self, hll_param)` | 13 | `Array_FindValue(page **, int)` | ✓ 已分流 |
| Array.Find | #1 → [44] `int Find(self, int, int, hll_param)` | 12 | `Array_FindValueRange(page **, int, int, int)` | ✓ 已分流 |
| Array.Find | #2 → [46] `int Find(self, hll_func)` | 72 | `Array_FindIf(page **, int)` | ✓ 已分流 |
| Array.Find | #3 → [48] `int Find(self, int, int, hll_func)` | 0 | `Array_FindIfRange(page **, int, int, int)` | ✓ 已分流 |
| Array.FindLast | #0 → [43] `int FindLast(self, hll_param)` | 0 | `Array_FindLast(page **, int)` | 潛在（0 呼叫） |
| Array.FindLast | #1 → [45] `int FindLast(self, int, int, hll_param)` | 0 | `Array_FindLast(page **, int)` | 潛在（0 呼叫） |
| Array.FindLast | #2 → [47] `int FindLast(self, hll_func)` | 4 | `Array_FindLast(page **, int)` | ✗ 已知：func 編號被當搜尋值 |
| Array.FindLast | #3 → [49] `int FindLast(self, int, int, hll_func)` | 0 | `Array_FindLast(page **, int)` | 潛在（0 呼叫） |
| Array.LowerBound | #0 → [50] `int LowerBound(self, hll_param)` | 2 | `Array_LowerBound(page **, int)` | ✓ |
| Array.LowerBound | #1 → [51] `int LowerBound(self, hll_func)` | 30 | `Array_LowerBound(page **, int)` | ✗ 已知：func 當 key |
| Array.UpperBound | #0 → [52] `int UpperBound(self, hll_param)` | 0 | `Array_UpperBound(page **, int)` | 潛在（0 呼叫） |
| Array.UpperBound | #1 → [53] `int UpperBound(self, hll_func)` | 0 | `Array_UpperBound(page **, int)` | 潛在（0 呼叫） |
| Array.BinarySearch | #0 → [54] `int BinarySearch(self, hll_param)` | 0 | `Array_BinarySearch(page **, int)` | 潛在（0 呼叫） |
| Array.BinarySearch | #1 → [55] `int BinarySearch(self, hll_func)` | 30 | `Array_BinarySearch(page **, int)` | ✗ 已知：func 當 key |
| Array.IsExist | #0 → [56] `bool IsExist(self, hll_param)` | 49 | `Array_IsExistValue(page **, int)` | ✓ 已分流 |
| Array.IsExist | #1 → [57] `bool IsExist(self, hll_func)` | 59 | `Array_IsExist(page **, int)` | ✓ 已分流 |
| Array.Unique | #0 → [61] `void Unique(self)` | 10 | `Array_Unique(page **)` | ✓（int；字串以 slot 比較） |
| Array.Unique | #1 → [62] `void Unique(self, hll_func)` | 2 | `Array_Unique(page **)` | ✗ 已知：謂詞被忽略 |
| Array.UniqueSorted | #0 → [63] `void UniqueSorted(self)` | 4 | `Array_UniqueSorted(page **)` | ✓（同上註） |
| Array.UniqueSorted | #1 → [64] `void UniqueSorted(self, hll_func)` | 0 | `Array_UniqueSorted(page **)` | 潛在（0 呼叫） |
| Array.First | #0 → [67] `ref hll_param First(self)` | 0 | `Array_First_NoPred(page **)` | ✓ 已修 ff6fc2f |
| Array.First | #1 → [68] `ref hll_param First(self)` | 45 | `Array_First_NoPred(page **)` | ✓ 已修 ff6fc2f |
| Array.First | #2 → [69] `ref hll_param First(self, hll_func)` | 0 | `Array_First(page **, int)` | ✓ |
| Array.First | #3 → [70] `ref hll_param First(self, hll_func)` | 108 | `Array_First(page **, int)` | ✓ |
| Array.Last | #0 → [71] `ref hll_param Last(self)` | 0 | `Array_Last(page **)` | 潛在（0 呼叫） |
| Array.Last | #1 → [72] `ref hll_param Last(self)` | 104 | `Array_Last(page **)` | ✓ |
| Array.Last | #2 → [73] `ref hll_param Last(self, hll_func)` | 0 | `Array_Last(page **)` | 潛在（0 呼叫） |
| Array.Last | #3 → [74] `ref hll_param Last(self, hll_func)` | 7 | `Array_Last(page **)` | ✗ 已知：謂詞被忽略 |
| Array.Min | #0 → [75] `ref hll_param Min(self)` | 0 | `Array_Min(page **)` | 潛在（0 呼叫） |
| Array.Min | #1 → [76] `ref hll_param Min(self)` | 1 | `Array_Min(page **)` | ✓ |
| Array.Min | #2 → [77] `ref hll_param Min(self, hll_func)` | 0 | `Array_Min(page **)` | 潛在（0 呼叫） |
| Array.Min | #3 → [78] `ref hll_param Min(self, hll_func)` | 12 | `Array_Min(page **)` | ✗ 已知：謂詞被忽略 |
| Array.Max | #0 → [79] `ref hll_param Max(self)` | 0 | `Array_Max(page **, int)` | 潛在（0 呼叫） |
| Array.Max | #1 → [80] `ref hll_param Max(self)` | 2 | `Array_Max(page **, int)` | ✗ 已知：C 讀殘值 func，落在函式範圍時會呼叫任意函式 |
| Array.Max | #2 → [81] `ref hll_param Max(self, hll_func)` | 0 | `Array_Max(page **, int)` | 潛在（0 呼叫） |
| Array.Max | #3 → [82] `ref hll_param Max(self, hll_func)` | 14 | `Array_Max(page **, int)` | ✓ 形狀一致 |
| HashMap.Any | #0 → [7] `bool Any(int)` | 1 | `HashMap_Any(int, string *)` | **✗ 新：C 讀殘值 key** |
| HashMap.Any | #1 → [8] `bool Any(int, string)` | 1 | `HashMap_Any(int, string *)` | ✓ |
| Math.Abs | #0 → [7] `int Abs(int)` | 32 | `abs(int)`（libc，`Math.c:292`） | ✓ |
| Math.Abs | #1 → [8] `float Abs(float)` | 33 | `abs(int)`（libc，`Math.c:292`） | **✗ 新：float→int abs，回傳原值** |
| Math.Min | #0 → [25] `int Min(int, int)` | 57 | `Math_Min(int, int)` | ✓ |
| Math.Min | #1 → [26] `float Min(float, float)` | 13 | `Math_Min(int, int)` | **✗ 新：回傳 x** |
| Math.Min | #2 → [27] `int Min(int, int, int)` | 1 | `Math_Min(int, int)` | **✗ 新：忽略 z** |
| Math.Min | #3 → [28] `float Min(float, float, float)` | 4 | `Math_Min(int, int)` | **✗ 新：回傳 x** |
| Math.Min | #4 → [29] `int Min(int, int, int, int)` | 1 | `Math_Min(int, int)` | **✗ 新：忽略 z,w** |
| Math.Min | #5 → [30] `float Min(float, float, float, float)` | 5 | `Math_Min(int, int)` | **✗ 新：回傳 x** |
| Math.Max | #0 → [31] `int Max(int, int)` | 90 | `Math_Max(int, int)` | ✓ |
| Math.Max | #1 → [32] `float Max(float, float)` | 18 | `Math_Max(int, int)` | **✗ 新：回傳 x** |
| Math.Max | #2 → [33] `int Max(int, int, int)` | 2 | `Math_Max(int, int)` | **✗ 新：忽略 z** |
| Math.Max | #3 → [34] `float Max(float, float, float)` | 5 | `Math_Max(int, int)` | **✗ 新：回傳 x** |
| Math.Max | #4 → [35] `int Max(int, int, int, int)` | 2 | `Math_Max(int, int)` | **✗ 新：忽略 z,w** |
| Math.Max | #5 → [36] `float Max(float, float, float, float)` | 3 | `Math_Max(int, int)` | **✗ 新：回傳 x** |
| Math.Clamp | #0 → [37] `int Clamp(int, int, int)` | 62 | `Math_Clamp(int, int, int)` | ✓ |
| Math.Clamp | #1 → [38] `float Clamp(float, float, float)` | 35 | `Math_Clamp(int, int, int)` | **✗ 新：回傳 value 不夾** |
| String.Search | #0 → [14] `bool Search(self, string)` | 0 | `String_Search(string **, int, string *)` | 潛在：C 多讀 1 參數（0 呼叫） |
| String.Search | #1 → [15] `bool Search(self, wrap<array<string> >, string)` | 0 | `String_Search(string **, int, string *)` | ✓ 形狀（0 呼叫） |
| String.Match | #0 → [17] `bool Match(self, string)` | 6 | `String_Match(string **, int, string *)` | **✗ 新：regex 指標被當 ml_slot；regex 殘值；恆回 true** |
| String.Match | #1 → [18] `bool Match(self, wrap<array<string> >, string)` | 0 | `String_Match(string **, int, string *)` | ✓ 形狀（0 呼叫） |
| String.GetPart | #0 → [21] `string GetPart(self, int)` | 16 | `String_GetPart(string **, int, int)` | **✗ 新：C 多讀 length 殘值（UB）** |
| String.GetPart | #1 → [22] `string GetPart(self, int, int)` | 76 | `String_GetPart(string **, int, int)` | ✓ |
| String.PadLeft | #0 → [23] `string PadLeft(self, int)` | 0 | `String_PadLeft(string **, int)` | ✓（0 呼叫） |
| String.PadLeft | #1 → [24] `string PadLeft(self, int, int)` | 0 | `String_PadLeft(string **, int)` | 潛在：paddingChar 被忽略（0 呼叫） |
| String.PadRight | #0 → [25] `string PadRight(self, int)` | 0 | `String_PadRight(string **, int)` | ✓（0 呼叫） |
| String.PadRight | #1 → [26] `string PadRight(self, int, int)` | 0 | `String_PadRight(string **, int)` | 潛在（0 呼叫） |
| String.Trim | #0 → [29] `string Trim(self)` | 13 | `String_Trim(string **)` | ✓ |
| String.Trim | #1 → [30] `string Trim(self, string)` | 0 | `String_Trim(string **)` | 潛在：charList 被忽略（0 呼叫） |
| String.TrimStart | #0 → [31] `string TrimStart(self)` | 0 | `String_TrimStart(string **)` | ✓（0 呼叫） |
| String.TrimStart | #1 → [32] `string TrimStart(self, string)` | 0 | `String_TrimStart(string **)` | 潛在（0 呼叫） |
| String.TrimEnd | #0 → [33] `string TrimEnd(self)` | 0 | `String_TrimEnd(string **)` | ✓（0 呼叫） |
| String.TrimEnd | #1 → [34] `string TrimEnd(self, string)` | 0 | `String_TrimEnd(string **)` | 潛在（0 呼叫） |

## 3. 新發現逐項（Array 已知五組以外）

### 3.1 Math.Abs / Min / Max / Clamp（int 與 float 同名；3、4 參數同名）

宣告（`libraries.txt:423-454`，Math index）：Abs [7] int、[8] float；Min [25] int×2、[26] float×2、[27] int×3、[28] float×3、[29] int×4、[30] float×4；Max [31]–[36] 同一模式；Clamp [37] int×3、[38] float×3。

C 端：`src/hll/Math.c:292` `HLL_EXPORT(Abs, abs)`、`:304` `HLL_EXPORT(Min, Math_Min)`、`:306` `Max`、`:316` `Clamp`。`Math_Min(int,int)`（`Math.c:88`）、`Math_Max(int,int)`（`:98`）、`Math_Clamp(int,int,int)`（`:103`）。float 版 `Math_MinF/MaxF/ClampF`（`:93/:117/:110`）和 `fabsf` 只以 `MinF/MaxF/ClampF/AbsF` 名稱匯出，CN AIN 沒有這些名稱，所以**永遠不會被綁到**。

| #N → index | 呼叫 | CIF → C | 探針實測（abi.out） | 判定 |
|---|---:|---|---|---|
| Abs#0 → [7] int | 32 | i:i → abs | Abs(-7)=7 | ✓ |
| Abs#1 → [8] float | 33 | F:F → abs(int) | **Abs(-3.5) = -3.5** | ✗ |
| Min#0 → [25] | 57 | ii:i → Math_Min | Min(5,2)=2 | ✓ |
| Min#1 → [26] float×2 | 13 | FF:F → Math_Min(int,int) | **Min(5.0,2.0) = 5** | ✗ |
| Min#2 → [27] int×3 | 1 | iii:i → Math_Min(int,int) | **Min(3,2,1) = 2** | ✗ |
| Min#3 → [28] float×3 | 4 | FFF:F | **= 3（回傳 x）** | ✗ |
| Min#4 → [29] int×4 | 1 | iiii:i | **Min(4,3,2,1) = 3** | ✗ |
| Min#5 → [30] float×4 | 5 | FFFF:F | **= 4（回傳 x）** | ✗ |
| Max#0 → [31] | 90 | ii:i | — | ✓ |
| Max#1 → [32] float×2 | 18 | FF:F | **Max(1.0,7.0) = 1** | ✗ |
| Max#2 → [33] int×3 | 2 | iii:i | **Max(1,2,3) = 2** | ✗ |
| Max#3 → [34] float×3 | 5 | FFF:F | **= 1** | ✗ |
| Max#4 → [35] int×4 | 2 | iiii:i | **Max(1,2,3,4) = 2** | ✗ |
| Max#5 → [36] float×4 | 3 | FFFF:F | **= 1** | ✗ |
| Clamp#0 → [37] | 62 | iii:i | Clamp(5,0,1)=1 | ✓ |
| Clamp#1 → [38] float×3 | 35 | FFF:F | **Clamp(5.0,0,1)=5；Clamp(-2.0,0,1)=-2** | ✗ |

**走錯：122 次。** 機制：float 參數放在 s0..s3，int 版 C 函式讀 w0..w3（殘值），int 結果寫 w0；libffi 依 CIF 從 s0 取 float 回傳值，而 s0 仍是第一個參數。因此 float 版等於恆回第一個參數。以上已在 arm64 實測。

#N 對應的 bytecode 證據：`ain_code.txt:18423` `PUSH 1092616192`（10.0f）後接 `CALLHLL Math Min#1 0`；`:350184` `ITOF / F_SUB` 後接 `CALLHLL Math Abs#1 0`、再接 `FTOI`；`:81661` `ITOF` 後接 `CALLHLL Math Clamp#1 0`。

影響點（呼叫點所在函式，節錄；完整清單在 scratchpad `ext_sites.txt`）：
- `FLOAT_EQUALS`（fno 20536，`ain_code.txt:831372-831388`）：`Abs(v1-v2) / Max(Abs(v1),Abs(v2)) <= 4.77e-7`。Abs 與 Max 都錯，只要 v1<v2（正數）結果就是負值，恆判為相等。共 6 個 `CALLFUNC FLOAT_EQUALS`。
- float Clamp#1 用在遊戲層：`Motion::EasingCalculator::GetByType<float>`（fno 36049，L1718845）、`Motion::Executer@OnUpdate`（fno 27012）、`TimerCallback@TimeRate::get`（fno 27290/27291）、`GameConfig@GameSpeedRate::set`（fno 30707）、`ScrollBase@UpdateWheelPosition/Clip`（fno 31166/31171）、`CameraCalculator@GetCamera`（fno 29015）、`DecisionTimer@Update`（fno 32695）、`MiniMapView@SetMapPos`（fno 33008）、`CharacterPositionCalculator@Update` 等。實際效果是不夾值，比例或進度可能超出 [0,1]。
- float Min#1：`CASTimerManager@UpdateRate`（fno 444，L18423）為 `Min(10.0f, <method 435>())`，錯誤時恆回 10.0。此函式可能被 xsystem4 的 CASTimer native 替換遮蔽（`FUNC_FLAG_CASTIMER_MGR`），**實際影響未驗證**。`FontProperty@GetXScaling`（fno 26883）、`parts::detail::CTextParts@SetScaling`（fno 15898）也使用 Min#1。
- float Max#1：`Player@TiredRate::get`（fno 27738/27739）、`Result*ViewCollection@SetPartsPos`（fno 32731/32749）。
- float Abs#1：`PlayerParamDetailView@OnAddExp/MotionGauge`（fno 32086/32093）、`BuffIcon@GetValueString`（fno 33471）、`CASTaskParts@GetMotionValue`（fno 20681）、`_system::detail::SetLowLevelScale/CompareLowLevelScale`。

參考：Rufim 以 `HLL_EXPORT_F(Abs, fabsf)`、`HLL_EXPORT_F(Clamp, Math_ClampF)` 分流（`scratchpad/rufim/xsystem4/src/hll/Math.c:348-375`）。

### 3.2 String.GetPart#0（[21] `GetPart(self,int index)`）— 16 次

- 綁定：[21] 和 [22] 都綁到 `String_GetPart(string**, int begin, int length)`（`src/hll/String.c:328`；匯出在 `:520`）。[21] 的 CIF 只有 2 參數，C 讀到的 `length` 是 x2 殘值。`length<=0` 時回空字串，小正數時截斷，大正數或越界時碰巧得到正確結果。
- 註解說「[21] single char」的 `String_GetPartChar`（`String.c:317`）是未被匯出的死碼，**而且語義本身就錯**。呼叫點顯示 [21] 的語義是「從 index 取到字尾」：
  - `elkeditor::detail::CEffectData::GetFileName`（fno 4674，`ain_code.txt:285611`）：`name.GetPart(name.FindLast("\\")+1) + ".txtex"`
  - `menu::detail::CTestMenuPanel@EraseKey`（fno 6760，L411674）：`partsName.GetPart(key.Length())`
  - `CAECopyPropertyDialog@GetPartsName`（fno 1382，L81716）：`text.GetPart(1)`
  - Rufim 同樣實作為取到字尾（`rufim/.../String.c:235-249`）。
- 探針：連續 3 次 `GetPart[21]("dir\\file",4)` 都得到 "file"，代表這次殘值碰巧是大正數。**結果不可依賴，production 的殘值未驗證**。
- 其餘 15 處呼叫點：`L132653/237767/274086/286442/476661/1015166/1015187/1018367/1018434/1028123/1028144/1194401/1200237`（所在函式見 `ext_sites.txt` 的同名段）。

### 3.3 String.Match#0（[17] `Match(self,string regex)`）— 6 次

- 綁定：[17] 和 [18] 都綁到 `String_Match(string **self, int ml_slot, string *regex)`（`String.c:261`），實作轉呼叫 `String_SearchAll`（`:171`）。SearchAll 是寫死的「方括號 tokenizer」，**完全不看 regex**：只要字串非空且不全是 `]` 就回 true。它還會把 token 陣列寫回 `ml_slot`：slot 落在 heap 範圍內時 `heap_set_page` 或 `wrap_set_slot`，否則 free。
- [17] 的第 2 個實參是 regex 的 `struct string*`，C 取它的低 32 位當 `ml_slot`。**如果這個低 32 位剛好落在 [1, heap_size)，就會覆寫任意 heap page**（未實測到；探針 heap_size=4096 時沒有命中）。
- 探針：`String.Match[17]("FooterButton_1","^zzz$") = 1`（期望 0）。
- 呼叫點：6 個 lambda `ActivityHelper::GetUser<FooterButton|RivalShopView|SkillSelector|BattleBonusView|BattleResultPlayerView|RoundBonusButton>` 的 `(15,48)`（fno 37567/37569/37571/37573/37575/37577，`ain_code.txt:1717751-1718071`）。外層 `GetUser<T>(act, nameRegex)`（fno 36620，L1717730 起）是 `names = GetPartsNames(act).Where(obj => obj.Match(nameRegex)); names.QuickSort(); return GetUser<T>#1(act, names)`，regex 常值為 `"Button\\d"`、`"RivalShop\\d"`、`"Skill\\d"`、`"Bonus\\d"`、`"Player\\d"`、`"Button.*"`（L1120231 等 `S_PUSH`）。**Where 會變成不過濾**，GetUser 回傳 activity 的全部 parts，而且 `QuickSort#0` 還按 slot 排序（見 3.5）。
- 語義：regex_match（全匹配）還是 search，**未驗證**（Rufim 用 regex_match；EXE 未反組譯）。

### 3.4 HashMap.Any#0（[7] `Any(int id)`）— 1 次

- 綁定：[7] 和 [8] 都綁到 `HashMap_Any(int id, struct string *key)`（`src/hll/HashMap.c:153`，匯出在 `:255`）。註解 `:151-152` 已經寫明「when called with 1 arg, key is undefined」。
- 探針：先呼叫 `Any[8](id,"zzz")`，再呼叫 `Any[7](id)`（map 內有 1 筆 "alpha"），得到 **0**（期望 1）。殘值 key 若是無效指標則會崩潰。
- 呼叫點：`utility::detail::CHashMap@Any`（fno 20966，`ain_code.txt:846648`）。

### 3.5 Array.Sort / Array.QuickSort（不在已知五組）

| #N → index | 呼叫（arg3 分布） | 綁定 | 判定 |
|---|---|---|---|
| Sort#0 → [34] `(self, hll_func)` | 41（65538:30, 2:6, 1:4, 65539:1） | `Array_Sort(page**)`（`Array.c:770`）：插入排序 `.i`，comparator 被丟掉 | ✗ |
| Sort#1 → [35] `(self)` | 12（1:8, 2:4） | 同上 | △ int 正確；arg3=2 的 4 次按 heap slot 排序 |
| QuickSort#0 → [38] `(self)` | 10（2:10，全是字串/值型） | `Array_QuickSort(page**, int comparator)`（`Array.c:819`）：C 讀未傳的 comparator（`(void)` 不使用），`qsort` 依 `.i` 排序 | △ ABI 無害，但字串按 slot 排序 |
| QuickSort#1 → [39] `(self, hll_func)` | 10（65538:6, 2:3, 65539:1） | 同上，comparator 被 `(void)` 丟掉 | ✗ |

- **回傳值**：AIN 四個多載都宣告 `wrap<?>`，C 都是 `void`。`hll_call` 的 AIN_WRAP 分支直接 `stack_push(r)`（`ffi.c:774-789`），bytecode 會把它 `X_ASSIGN` 進 dummy local，之後 `.LOCALDELETE`，也就是 `DELETE` → `heap_unref(slot)`（`vm.c:2550-2553`）。例：`ain_code.txt:145503-145513`。探針實測回傳殘值：`Sort[35]` 得 r.i=1805442416（堆疊位址低位）、`QuickSort[38]` 得 r.i=1。`heap_unref` 會擋掉 `slot<=1` 或 `>=heap_size`（`heap.c:619`），**殘值一旦落在 [2,heap_size) 就會對任意 live 物件減 ref**。production 的殘值未驗證。同型問題：`Array.AscSort`[36]（4 次）、`Array.Remain`[19]（1 次），見第 6 節。
- **原版 EXE 語義**（dispatcher 0x644300，跳表 0x644f18；本次以 capstone 反組譯 `0x644907-0x6449ab`）：
  - #34 Sort(self,func) → 0x644907：push `[args+0x28]`（func），call **0x648340**
  - #35 Sort(self) → 0x644927 → call **0x648420**，其內部 call **0x648500**（= #36 AscSort 的實作），並把 0x18 bytes 的結果物件搬進回傳緩衝區
  - #38 QuickSort(self) → 0x644972 → **0x648700**（內部用 0x645dd0 與 0x646a00）
  - #39 QuickSort(self,func) → 0x64498b：push func，call **0x6487e0**（內部用 0x645f70 與 0x646a00）
  - Sort(func) 0x648340 用 0x645f70 與 **0x646980**，QuickSort 用 **0x646a00**，兩者排序演算法不同（推定 stable 與 quick，**未驗證**）。0x645dd0 與 0x645f70 推定為「值比較器」與「謂詞比較器」工廠，**未逐指令驗證**。
  - 結論：原版四個多載的實作各自獨立；predicate 版會使用 func；`Sort(self)` 等同 `AscSort`。回傳的 `wrap<?>` 是 0x18 bytes 的物件，**內容（是否為 self 的 wrap）未驗證**。
- comparator lambda 形狀：`Sort#0` 例 fno 22529（`ain_code.txt:145487-145491`）為 `(a:int, b:int) → bool`；`QuickSort#1` 例 fno 23848（`ain_code.txt:237262-237267`）為 `(lhs: wrap<iwrap<IResourceInfo>>, <void>, rhs: wrap<iwrap<IResourceInfo>>, <void>) → bool`，即 `nr_args=4`，iface 元素佔 2 槽，函式本體以 `S_LT` 作 less-than 比較（L237301 前）。實作時 push 慣例要依 `ain->functions[fno].vars[]` 決定，不能只看 arg3。
- 受影響的遊戲層呼叫點（Sort#0）：`PlayerCollection@GetBattlePlayers/AdjustOrder`（fno 36170/36176）、`WorkerCollection@GetReplaceTarget/AdjustOrder`（fno 36224/36225）、`ActionTargetFinder@Sort`（fno 36288）、`EnemyAvailableSkillFinder@Get/GetDamagedPlayerOrder`（fno 36338/36340/36344）、`MapStructure@LoadNode`（fno 36505）、`ArrayExtensions::GetSort<BattleBonus>`（fno 37566）、`Shop@AddPurchasedItem`（fno 27880）等。QuickSort#1：`Schedule@GetLatest`、`WorkerCollection@GetAllInstancesOrdered/GetWorked`、`PlayerActionStreamCalculator@CalcStreams`、`ItemStock@ToItem`、`MapStructure@CalcZPosition`、`LocalSave@GetAvailablSaveObjects`、`ActionFrameController@GetAllImages`。QuickSort#0：`ActivityHelper::GetUser<*>`、`ProfileInfo@Load`、`StandNameFinder`。

### 3.6 潛在（0 呼叫）的同型多載

`String.Search`[14]（C 多讀 1 個參數）、`String.PadLeft/PadRight`[24]/[26]（paddingChar 被忽略）、`String.Trim/TrimStart/TrimEnd`[30]/[32]/[34]（charList 被忽略）、`Array.Realloc`[2]、`Copy`[31]/[32]、`Equals`[41]、`FindLast`[43]/[45]/[49]、`UpperBound`[52]/[53]、`BinarySearch`[54]、`UniqueSorted`[64]、`Last`/`Min`/`Max` 的非使用序號。CN 目前沒有呼叫，但通用分流修法應一併涵蓋。

## 4. 已知 Array 五組的交叉核對（runtime 綁定）

runtime 綁定與 orchestrator 提供的清單一致（表 A）。呼叫數也逐一吻合：Realloc#0=13、Any#0=37/#1=46、Fill#0=3/#1=2、Copy#0=1/#3=3、Equals#0=3、FindLast#2=4、LowerBound#0=2/#1=30、UpperBound=0、BinarySearch#1=30、Unique#0=10/#1=2、UniqueSorted#0=4、First#1=45/#3=108、Last#1=104/#3=7、Min#1=1/#3=12、Max#1=2/#3=14。已分流的 Erase/IsExist/Numof/Count/Find/First 在 runtime 確實綁到各自的實作（例：Numof[21] 與 Count[23] 綁 `Array_CountIf`，Find[46] 綁 `Array_FindIf`）。

DWARF 比對另外看出下列細節，修正這幾組時需要一併處理：
- **Equals#0 [40]**：`wrap<array>` 的 CIF 是 sint32（`ffi.c` 的 `link_static_library_function` 對 wrap<ref> 用 sint32），C 端是 `Array_Equals(page**, page**)`（`Array.c:2396`），會 `*b` 解參考 slot 編號。探針 fork 子程序得到 **SIGSEGV**，UBSan 報在 `Array.c:2399:26`，位址 0x4d2。因此 Equals#0 的 3 次呼叫不只是謂詞問題，**形狀正確的版本本身就會崩潰**。
- **Max#1 [80] `(self)`**：`Array_Max(page**, int func)`（`Array.c:2156`）讀 x1 殘值當 func。殘值落在 `[0, nr_functions)` 時，會把任意 AIN 函式當 score callback 以 `vm_call_nopop` 呼叫（`Array.c:2187-2199`）。
- **Fill#1 [29] `(self,index,length,value)`**：C 是 `Array_Fill(array, value, start, count)`（`Array.c:2123`），參數順序整組錯位。
- **Fill/Copy 回傳**：AIN 是 `int`，C 是 `void`，會推殘值（第 6 節表中的 S1）。
- 已知組的走錯合計：Any#1 46、Fill#0 3、Fill#1 2、Copy#0 1、Equals#0 3、FindLast#2 4、LowerBound#1 30、BinarySearch#1 30、Unique#1 2、Last#3 7、Min#3 12、Max#1 2，**共 142**（First 已修，不計入）。

## 5. 宣告存在、C 端沒有綁定、且 AIN 有呼叫（UNIMPL）

以 runtime 綁定表為準（已套用 `_PreLink` 的 replace 與 `_PostLink` 的 register）：**98 個宣告、172 個靜態呼叫點**。原因分兩類：C 表沒有這個名稱（74 個，全 src 以 grep 查無名稱），或 C 表是 `HLL_TODO_EXPORT`，fun 為 NULL（24 個）。UNIMPL 行為（`ffi.c:303-382`）：依 AIN 型別 pop 參數；非 void 推 0；string 回傳推 slot 0，後續 `heap_get_string` 對非 VM_STRING 會回 EMPTY（`heap.c:812-821`），所以多半被當空字串，**逐一影響未驗證**。

「分類」欄的判斷方式：呼叫點所在函式的命名空間全部屬於 editor/debug（activityeditor、elkeditor、stageeditor、sealtool、modelviewer、menu::detail::CTest*、CPartsTreeNode、AFL_Debug*）時標為「編輯器/除錯」，否則標為「遊戲/引擎層可達」。這是近似分類；以 GB18030 亂碼名出現的函式推定為 AFL_Parts_* 包裝。

| # | 宣告（index） | 呼叫數 | 未綁定原因 | 分類 | 呼叫點（所在函式 fno / dump 行） |
|---:|---|---:|---|---|---|
| 1 | FileOperation[1] `bool DeleteFile(string FileName);` | 7 | C 表無此名；無動態註冊 | 編輯器/除錯 | activityeditor::detail::CAEFileTreeForm@CreateFile fno2310 L138086; activityeditor::detail::CAEFileTreeForm@PasteImp fno2314 L138301; activityeditor::detail::CAEFileTreeForm@PasteFileImp fno2315 L138415 …共7處 |
| 2 | PartsEngine[694] `string GetUserComponentName(int Number);` | 7 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | activity::detail::CallUserComponentEventWithChild fno679 L28242; activityeditor::detail::CInstanceItem@Release fno1038 L60954; activityeditor::detail::CInstanceItem@Set fno1041 L61296 …共7處 |
| 3 | InputString[6] `bool IsOpenIME(void);` | 4 | C 表無此名；無動態註冊 | 編輯器/除錯 | parts::detail::CPartsTreeNode@OnKeyUpEditLabel fno8892 L508728; parts::detail::CPartsTreeNode@OnKeyUpEditLabel fno8892 L508734; parts::detail::CPartsTreeNode@OnFixedEditLabel fno8893 L508908 …共4處 |
| 4 | FileOperation[3] `bool CopyFolder(string DestFolderName, string SrcFolderName);` | 3 | C 表無此名；無動態註冊 | 編輯器/除錯 | activityeditor::detail::CAEFileTreeForm@PasteFolderImp fno2316 L138524; activityeditor::detail::CAEFileTreeForm@<lambda : activityed fno22509 L139320; elkeditor::detail::CEmitterData@CopyResouce fno4702 L288606 |
| 5 | FileOperation[11] `bool OpenFolder(string FolderName);` | 3 | C 表無此名；無動態註冊 | 編輯器/除錯 | activityeditor::detail::CAEFileTreeForm@<lambda : activityed fno22484 L135264; AFL_Debug_OpenFolder fno4102 L257521; elkeditor::detail::CMainMenu@<lambda : elkeditor::detail::CM fno23918 L277511 |
| 6 | HTTPDownloader[2] `bool IsRun(void);` | 3 | C 表 HLL_TODO_EXPORT(IsRun, HTTPDownloader_IsRun) @hll/HTTPDownloader.c:30（NULL） | 網路服務（CAS*NetService） | CASGameNetService@Connect fno7732 L476714; CASNetService@IsRun::get fno7739 L476964; CASNetService@IsRun::get#1 fno7740 L476972 |
| 7 | HTTPDownloader[10] `bool UTF8ToSJIS(wrap<string> pIString);` | 3 | C 表無此名；無動態註冊 | 網路服務（CAS*NetService） | CASGameNetService@Connect fno7732 L476762; CASNetService@GetResult fno7746 L477053; CASNetService@ReadString fno7750 L477140 |
| 8 | PartsEngine[176] `int GetChild(int Number, int ChildIndex);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8612 L492665; parts::detail::CParts@GetChild fno14221 L637670; PartsHelper::GetPartsChildren fno26913 L991051 |
| 9 | PartsEngine[150] `int GetComponentAlphaClipper(int Number);` | 3 | C 表 HLL_TODO_EXPORT(GetComponentAlphaClipper, PartsEngine_GetComponentAlphaClipper) @hll/PartsEngine.c:606（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8364 L488068; parts::detail::CParts@AlphaClipper::get fno14128 L635414; parts::detail::CParts@AlphaClipper::get#1 fno14129 L635433 |
| 10 | PartsEngine[122] `int GetComponentDrawFilter(int Number);` | 3 | C 表 HLL_TODO_EXPORT(GetComponentDrawFilter, PartsEngine_GetComponentDrawFilter) @hll/PartsEngine.c:589（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8336 L487745; parts::detail::CParts@DrawFilter::get fno14065 L633599; parts::detail::CParts@DrawFilter::get#1 fno14066 L633618 |
| 11 | PartsEngine[143] `bool GetComponentReverseLR(int Number);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8356 L487971; parts::detail::CParts@ReverseLR::get fno14107 L634775; parts::detail::CParts@ReverseLR::get#1 fno14108 L634794 |
| 12 | PartsEngine[142] `bool GetComponentReverseTB(int Number);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8357 L487982; parts::detail::CParts@ReverseTB::get fno14110 L634832; parts::detail::CParts@ReverseTB::get#1 fno14111 L634851 |
| 13 | PartsEngine[130] `float GetComponentRotateX(int Number);` | 3 | C 表 HLL_TODO_EXPORT(GetComponentRotateX, PartsEngine_GetComponentRotateX) @hll/PartsEngine.c:597（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8344 L487833; parts::detail::CParts@RotationX::get fno14080 L634035; parts::detail::CParts@RotationX::get#1 fno14081 L634054 |
| 14 | PartsEngine[131] `float GetComponentRotateY(int Number);` | 3 | C 表 HLL_TODO_EXPORT(GetComponentRotateY, PartsEngine_GetComponentRotateY) @hll/PartsEngine.c:598（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8345 L487844; parts::detail::CParts@RotationY::get fno14083 L634092; parts::detail::CParts@RotationY::get#1 fno14084 L634111 |
| 15 | PartsEngine[164] `int GetComponentTextureAddressType(int Number);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8368 L488112; parts::detail::CParts@WrapMode::get fno14134 L635528; parts::detail::CParts@WrapMode::get#1 fno14135 L635547 |
| 16 | PartsEngine[162] `int GetComponentTextureFilterType(int Number);` | 3 | C 表 HLL_TODO_EXPORT(GetComponentTextureFilterType, PartsEngine_GetComponentTextureFilterType) @hll/PartsEngine.c:608（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8366 L488090; parts::detail::CParts@TextureFilter::get fno14131 L635471; parts::detail::CParts@TextureFilter::get#1 fno14132 L635490 |
| 17 | PartsEngine[206] `int GetMotionEndFrame(string MotionName);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8400 L488521; (GB18030 亂碼名) fno8403 L488630; parts::detail::CParts@Motion#1 fno14194 L636904 |
| 18 | PartsEngine[134] `bool IsComponentEnableClipArea(int Number);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8348 L487877; parts::detail::CParts@Clip::get fno14089 L634206; parts::detail::CParts@Clip::get#1 fno14090 L634225 |
| 19 | PartsEngine[108] `bool IsComponentMessageWindowEffectLink(int Number);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8626 L492820; parts::detail::CParts@MessageWindowEffectLink::get fno14243 L638163; parts::detail::CParts@MessageWindowEffectLink::get#1 fno14244 L638182 |
| 20 | PartsEngine[166] `bool IsComponentMipmap(int Number);` | 3 | C 表 HLL_TODO_EXPORT(IsComponentMipmap, PartsEngine_IsComponentMipmap) @hll/PartsEngine.c:610（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8370 L488134; parts::detail::CParts@Mipmap::get fno14137 L635585; parts::detail::CParts@Mipmap::get#1 fno14138 L635604 |
| 21 | PartsEngine[740] `int Parts_GetTextLineSpace(int Number, int State);` | 3 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8131 L484767; parts::detail::CTextParts@LineSpace::get fno15876 L678473; parts::detail::CTextParts@LineSpace::get#1 fno15877 L678483 |
| 22 | HTTPDownloader[4] `int GetReadSize(void);` | 2 | C 表 HLL_TODO_EXPORT(GetReadSize, HTTPDownloader_GetReadSize) @hll/HTTPDownloader.c:32（NULL） | 網路服務（CAS*NetService） | CASGameNetService@Connect fno7732 L476734; CASNetService@GetResult fno7746 L477036 |
| 23 | HTTPDownloader[5] `bool ReadAllString(wrap<string> pIString);` | 2 | C 表無此名；無動態註冊 | 網路服務（CAS*NetService） | CASGameNetService@Connect fno7732 L476743; CASNetService@GetResult fno7746 L477046 |
| 24 | HTTPDownloader[11] `bool SJISToUTF8(wrap<string> pIString);` | 2 | C 表無此名；無動態註冊 | 網路服務（CAS*NetService） | CASGameNetService@SendCommandImpl fno7728 L476578; CASNetService@PostSync fno7744 L477011 |
| 25 | HTTPDownloader[3] `void Stop(void);` | 2 | C 表 HLL_TODO_EXPORT(Stop, HTTPDownloader_Stop) @hll/HTTPDownloader.c:31（NULL） | 網路服務（CAS*NetService） | CASGameNetService@Connect fno7732 L476728; CASNetService@Cancel fno7745 L477029 |
| 26 | PartsEngine[171] `void AddChild(int Number, int ChildNumber);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8607 L492606; parts::detail::CParts@AddChild fno14210 L637276 |
| 27 | PartsEngine[170] `void ClearChild(int Number);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8606 L492595; parts::detail::CParts@ClearChild fno14216 L637573 |
| 28 | PartsEngine[175] `int GetChildIndex(int Number, int ChildNumber);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8611 L492652; parts::detail::CParts@GetChildIndex fno14223 L637750 |
| 29 | PartsEngine[172] `void InsertChild(int Number, int Index, int ChildNumber);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8608 L492619; parts::detail::CParts@InsertChild fno14214 L637468 |
| 30 | PartsEngine[116] `bool IsComponentSubColorMode(int Number);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | parts::detail::CParts@SubColorMode::get fno14050 L633176; parts::detail::CParts@SubColorMode::get#1 fno14051 L633195 |
| 31 | PartsEngine[169] `bool IsExistChild(int Number, int ChildNumber);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8605 L492584; parts::detail::CParts@IsExistChild fno14218 L637611 |
| 32 | PartsEngine[182] `string Parts_GetComment(int Number);` | 2 | C 表無此名；無動態註冊 | 編輯器/除錯 | AFL_Debug_GetPartsComment fno4097 L257466; debug::detail::GetPartsComment fno4211 L264257 |
| 33 | PartsEngine[792] `bool Parts_SetPartsCGDetectionSurfaceArea(int Number, int X, int Y, int Width, int Height, int State);` | 2 | C 表 HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea) @hll/PartsEngine.c:819（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8638 L493139; parts::detail::CCGDetectionParts@SurfaceArea::set fno9639 L536608 |
| 34 | PartsEngine[788] `bool Parts_SetPartsRectangleDetectionSurfaceArea(int Number, int X, int Y, int Width, int Height, int State);` | 2 | C 表 HLL_TODO_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea) @hll/PartsEngine.c:817（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8570 L492124; parts::detail::CRectParts@SurfaceArea::set fno14688 L651237 |
| 35 | PartsEngine[173] `void RemoveChild(int Number, int ChildNumber);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8609 L492630; parts::detail::CParts@RemoveChild fno14217 L637592 |
| 36 | PartsEngine[212] `void SetButtonEnable(int Number, bool Enable);` | 2 | C 表 HLL_TODO_EXPORT(SetButtonEnable, PartsEngine_SetButtonEnable) @hll/PartsEngine.c:632（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8253 L486579; parts::detail::CButtonParts@Enable::set fno9278 L528909 |
| 37 | PartsEngine[107] `void SetComponentMessageWindowEffectLink(int Number, bool Link);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8625 L492811; parts::detail::CParts@MessageWindowEffectLink::set fno14245 L638146 |
| 38 | PartsEngine[165] `void SetComponentMipmap(int Number, bool Mipmap);` | 2 | C 表 HLL_TODO_EXPORT(SetComponentMipmap, PartsEngine_SetComponentMipmap) @hll/PartsEngine.c:609（NULL） | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8369 L488125; parts::detail::CParts@Mipmap::set fno14139 L635568 |
| 39 | PartsEngine[140] `void SetComponentReverseTB(int Number, bool Reverse);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8355 L487962; parts::detail::CParts@ReverseTB::set fno14112 L634815 |
| 40 | PartsEngine[163] `void SetComponentTextureAddressType(int Number, int Type);` | 2 | C 表無此名；無動態註冊 | 遊戲/引擎層可達 | (GB18030 亂碼名) fno8367 L488103; parts::detail::CParts@WrapMode::set fno14136 L635511 |

第 40 名之後還有 4 個宣告同樣 2 次：PartsEngine `SetComponentTextureFilterType`[161]、`SetMotionData`[204]、`SetNumeralFont`[767]、`SetUserComponentName`[693]。其餘 54 個各 1 次，包括 AFAFactory 的搜尋 API 10 個、HTTPDownloader Read*/Encode/Decode 6 個、MsgSkip `Get/SetAdvFlag`、OutputLog `BackupTab/RestoreTab`、PartsEngine Activity EX/BG、ComponentAbsolute*、Scroll/CheckBox LinkNumber、`UpdateMatrix` 等。完整清單在 `scratchpad/.../hllscan/unbound_why.json`。

重點：
- `PartsEngine.GetUserComponentName`[694]：呼叫點包含 `activity::detail::CallUserComponentEventWithChild`（fno 679，`ain_code.txt:28242`），屬 runtime 的 activity 事件分派。UNIMPL 回空名稱，可能讓 user component 事件找不到目標（**未驗證**）。
- PartsEngine 的 child 系列（`AddChild/ClearChild/InsertChild/RemoveChild/GetChild/GetChildIndex/IsExistChild`）由 `parts::detail::CParts@*` 與 `PartsHelper::GetPartsChildren`（fno 26913）使用。
- `HLL_TODO_EXPORT` 的 24 個，名稱已經在 C 表裡，但 fun 是 NULL（例：`PartsEngine.c:597-610` 的 `GetComponentRotateX/RotateY/TextureFilterType/Mipmap`，`HTTPDownloader.c:30-32` 的 `IsRun/Stop/GetReadSize`）。

## 6. 延伸：非多載的「AIN CIF ≠ C 原型」（有呼叫且為 S1）

作法：對 1595 個已綁定宣告，比對 runtime CIF（`bind.tsv` 第 9 欄）與 DWARF 取得的 C 原型（`audit2.json`）。S1 的定義：C 多讀未傳的參數；float 與非 float 的暫存器類別錯；CIF 為 sint32 但 C 是指標；AIN 回傳 string 但 C 不回指標；AIN 回傳 float 但 C 不回 float；AIN 回傳非 void 但 C 是 void。全部 S1/S2 共 99 列，完整清單在 `scratchpad/.../hllscan/sev.txt`；有呼叫的 74 列，其中多載相關的已列在第 3、4 節。int 與指標之間的寬度差（例：hll_param 以指標 CIF 傳給 C 的 int）在 arm64 小端序、低 32 位取值的情況下無害，已經排除。

每一列都人工檢查過 C 本體，確認 C 是否真的使用錯位的參數或回傳：

| 宣告 | 呼叫 | 問題 | C 是否使用 → 後果 | 證據 |
|---|---:|---|---|---|
| Array.Duplicate[3] `(self, wrap<array> src)` | 14 | wrap CIF sint32，C 是 `page **src` | `*src` 解參考 → **SIGSEGV（探針）** | `Array.c:2230-2234`；UBSan 報在 `Array.c:2232:23`，位址 0x4d2 |
| FileOperation.GetFileList[12] / GetFolderList[14] `array<?>(string)` | 8+8 | C 為 `(string*, page **out)`，多讀 out；回傳 bool 被當 array slot | `get_file_list` 對殘值 `*out` 讀寫；探針回傳 r.i=1（slot 1 = global page） | `FileOperation.c:147-216` |
| VSFile.ReadString[10] `(wrap<string>)` | 5 | CIF sint32，C 是 `string **` | `*str` 讀寫 → 會崩潰（推定，未實測） | `VSFile.c:215-235`；呼叫點含 `gamesave::detail::*`（fno 6195，L386560/386571）、`config::detail::CAS3DSetting@Load` |
| TextFile.ReadAll[5] / Read[7] / ReadLine[8] | 4+2+2 | wrap<string> CIF sint32，C 是 `int *text_out` | `wrap_set_string(text_out)` → **SIGSEGV（ReadAll 探針）** | `TextFile.c:83-101/121/143`；UBSan 報在 `hll.h:71:12`；呼叫點含 `ExtableFormatLoader@Load`（fno 26719）、`AFL_TextFile_ReadAll` |
| Array.SYSTEMONLY_GetStructPageList[83] `array<?>(self)` | 4 | C 為空的 void，回傳殘值 | 殘值直接作為 `system.SerializeStruct` 的 structPageList（`ain_code.txt:385480-385484`）；`system_SerializeStruct` 使用 `hll_self_slot`（`system.c:181-184`），可能繞過了這個問題，**未驗證** | `Array.c:1864-1866`；呼叫點為 `AFL_GameSave_StructSave/StructLoad/Serialize/Deserialize` |
| Array.AscSort[36] / Remain[19] `wrap<?>` | 4+1 | C 是 void，殘值被推上堆疊後 DELETE | 同 3.5 | `Array.c:2290`、`:2335` |
| PartsEngine.GetLayoutBoxReturnSize[483] `float(int)` | 3 | C 回 int | 回傳 s0 殘值 | `parts/layoutbox.c:89` |
| PartsEngine.SetLayoutBoxReturn[481] `(int,bool,float)` | 2 | float 放 s0，C 以 int 讀 w2 | wrap_size 是殘值 | `parts/layoutbox.c:72-78` |
| ADVEngine.GetArgumentValue_string[17] `(wrap<string>,int,int)` | 1 | CIF sint32，C 是 `page **` | 參數為字串時 `*value_ref` 解參考 | `ADVEngine.c:515-526` |
| SystemService.GetCPUInfo[58] | 1 | wrap<string> CIF sint32，C 是 `int *` | `*vendor = 0` 寫入 slot 位址 | `SystemService.c:685-686` |
| SealEngine.GetInstanceInfoText[118] / GetInstanceMaterialInfoText[119] / Tool_CreateFBXAscii[247] `string(...)` | 1+1+1 | C 回 int（自行配置 heap slot） | `ffi.c:761-764` 把 slot 編號當 `struct string*` | `SealEngine.c:1034/1057/1837` |
| SystemService.SetAntiAliasingMode[24]、PopSystemMessageString[55]、PartsEngine.SaveThumbnail[19]、RemoveAllActivityParts[34] | 2/4/1/1 | AIN 回 bool，C 是 void | bool 為 x0 低 byte 殘值 | `SystemService.c:586/614`、`pe_v14_message.c:351`、`pe_v14_activity.c:998` |

確認無害（C 不使用錯位的參數）：`system.ReadSerializeStructComment`[16]、`ReadGroupSaveComment`[12]（`(void)`，`system.c:171/324`）、`ADVEngine.GetFunctionList`[0]（空 stub）、`AnteaterADVLogList.Save/Load`[13]/[14]（v14 先 return，`AnteaterADVEngine.c:208/255`）、`FileDialog`[0]–[2]（stub）、`EXWriter.SaveToString`[5]（stub）、`SealEngine.GetPathLine/GetOptimizedPathLine`[222]/[223]（stub）、`SystemService.Save/Load`[65]/[66]（stub）、`SystemService.PopSystemMessage`[54]（S2，stub）。

## 7. 修法建議（沿用現有慣例）

1. **把 `array_select_function` 推廣成全庫的宣告選擇器**。現在 `link_static_library` 只替 Array 的 6 個名稱特判（`ffi.c:1164-1180`）。建議兩種做法擇一：
   - 在 `static_library` 的項目加上可選的 `nr_args` 與 float 標記，`link_static_library` 先找「名稱 + 參數數 + 第一參數是否 float」最精確的項目，找不到才退回只比名稱（等同 Rufim 的 `HLL_EXPORT_N` 與 `HLL_EXPORT_F`，見 `rufim/.../HashMap.c:277-278`、`Math.c:348-375`）；
   - 或比照 `array_*_function`，替 Math、String、HashMap 各寫一個 `*_select_function(const struct ain_hll_function*)`，從 ffi.c 呼叫。
2. 各項實作：
   - Math：Abs(float)→`fabsf`；Min、Max、Clamp 補 float 版與 3、4 參數的 int、float 版（`Math_MinF/MaxF/ClampF` 已經存在）。
   - String：GetPart(self,index) 回傳到字尾；Match、Search 的 2 參數版要真的做 regex（語義見第 8 節）；Pad*、Trim* 的第 2 參數版補上。
   - HashMap：Any(id) 回 `count>0`。
   - Array：Sort(pred) 與 QuickSort(pred) 以 `vm_call_nopop(func, nr_args)` 做 less-than 比較（push 慣例參照 `Array_First`；iface 元素佔 2 槽，見 3.5 的 fno 23848）；Sort(self) 等同 AscSort；無謂詞排序需要依元素型別比較（字串比內容，不比 slot）。
3. **void 對 `wrap<?>` 或 `int` 的回傳**：C 端改成回 int。最保守是回 `-1`，因為 `DELETE`/`heap_unref` 對 -1 不動作（`vm.c:2550-2553`）。若要回 self 的 wrap，必須先 `heap_ref`，才能抵銷 bytecode 的 DELETE（比照 `ffi.c:776-786` 的 EmplaceBack）。原版回傳物件的內容未驗證。
4. **wrap<ref> 參數**：C 原型改成 `int slot`，比照 ffi.c 的 wrap<ref>→sint32 慣例，自行解析 slot（Array_Duplicate、Array_Equals、TextFile_*、VSFile_ReadString、ADVEngine_GetArgumentValue_string、SystemService_GetCPUInfo）。
5. **防回歸**：把本次的 DWARF 比對（`audit.py` + `severity.py`）做成可重跑的檢查，在 link 表變動後檢查「C 參數數 > CIF 參數數」「F/非 F 錯位」「sint32→指標」「void→非 void 回傳」四類。
6. 探針 fixture 建議（本階段未改 harness，下列僅供設計）：
   - Math float 多載：沿用本報告的 16 個數值，預期值已列在表中。
   - `String.GetPart(s, i)` 應回到字尾。
   - `String.Match(s, "^zzz$")` 應為 false。
   - `HashMap.Any(id)` 應與先前呼叫的 key 無關。
   - `Array.Sort(structArr, lambda)` 的順序應依 comparator。
   - 對字串陣列跑 `QuickSort(self)` 應得字典序。
   - Sort 回傳值應為 -1，或已 ref 的 slot。

## 8. 未驗證事項

- production（非 ASan、-O2）build 的暫存器殘值實際數值，以及 Sort/QuickSort/AscSort 回傳殘值是否曾落在 [2, heap_size)。
- `String.Match` 原版是全匹配（regex_match）還是 search；原版 `Sort(pred)` 是否為 stable；原版 `wrap<?>` 回傳物件的內容。EXE 只確認到 dispatcher 分流（3.5）。
- `CASTimerManager@UpdateRate` 的 Min#1 是否被 native timer 替換遮蔽。
- `SYSTEMONLY_GetStructPageList` 的殘值回傳是否被 `system_SerializeStruct` 的 `hll_self_slot` 路徑繞過。
- `VSFile.ReadString`、`FileOperation.GetFileList` 在實機是否崩潰（本次只有 ReadAll、Duplicate、Equals 做了實際的崩潰測試）。
- 未綁定函式在遊戲中的實際影響（第 5 節只有靜態呼叫點與命名空間分類）。

## 9. 產物與重現

`<scratchpad>/overload/hllscan/` 內的檔案：

| 類別 | 檔案 |
|---|---|
| 解析 | `parse_libs.py` → `libs.json`；`count_calls.py` → `calls.json`；`dups.py` |
| 綁定列舉 | `enum_probe.c`、`build_one.py`、`enum-probe` → `bind.tsv`/`bind.json`（`#BASE` 行後，每列為 lib、idx、名稱 hex、nargs、linked、偏移、CIF、AIN 回傳型別） |
| DWARF 比對 | `parse_dwarf.py` → `dwarf_subs.json`；`audit.py` → `audit.json`；`severity.py` → `audit2.json`/`sev.txt`；`bodies.txt` |
| ABI 實測 | `abi_probe.c` → `abi.out`/`abi.err` |
| 未綁定 | `unbound_called.json`、`unbound_why.json`、`unbound_ctx.json`、`tableU.md` |
| 呼叫點 | `ext_sites.txt` |
| EXE | `exe_disasm.py`（venv：`scratchpad/venv`） |

重跑方式：`python3 build_one.py <asan-build> <hllscan> enum_probe.c enum_probe.o enum-probe`，再執行 `ASAN_OPTIONS=detect_leaks=0 ./enum-probe <AIN>`。
