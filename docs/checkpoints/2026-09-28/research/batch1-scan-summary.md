# 全庫掃描

全 HLL 庫同型缺陷掃描完成，全程唯讀。worktree HEAD a8d92df 沒有改動；harness 也沒有改動。報告在 <PORT>/reports/hll-overload-scan-2026-09-28.md。

方法：
- libraries.txt 以 byte 解析，得 36 庫、1701 宣告，與 runtime 的 ain->libraries 逐筆比對，0 筆不符。
- ain_code.txt 以 byte 掃描，得 11590 個 CALLHLL，(Lib,Name,#N) 全部能對回宣告 index。
- 自建唯讀探針：#include production ffi.c，依 production 順序跑 _PreLink（略過 MsgSkip）→ link_libraries → _PostLink，列出每個宣告實際綁到的函式指標與依 AIN 建立的 CIF 型別。以 nm 與 dsymutil/dwarfdump 還原 C 原型，並用 ffi_call 依 AIN 的 CIF 實測；可能崩潰的案例放 fork 子程序跑。
- 以 capstone 反組譯原版 EXE 的 Array case 34–39。

結果一：同名且形狀不同的宣告共 38 組（Array 24、Math 4、String 8、HashMap 1）。已知五組以外，新發現 196 次呼叫確定走錯實作，另有 22 次屬於形狀可用但回傳或語義錯（△）。
- Math.Abs/Min/Max/Clamp，122 次：float 版與 3/4 參數版都綁到 int 版 C 函式。Abs 綁 libc abs（Math.c:292）；Math_Min、Math_Max、Math_Clamp 在 Math.c:88/98/103。Math_MinF/MaxF/ClampF 只以 MinF/MaxF/ClampF 名稱匯出，CN AIN 沒有這些名稱，所以永遠綁不到。
  - arm64 實測 float 版恆回第一個參數：Abs(-3.5)=-3.5、Clamp(5.0,0,1)=5、Min(5.0,2.0)=5。int 3/4 參數版只比前兩個：Min(3,2,1)=2。
  - FLOAT_EQUALS（fno 20536，ain_code.txt:831372-831388）因此在 v1<v2 時恆判相等。
  - Clamp#1 有 35 處，含 EasingCalculator::GetByType<float>、TimerCallback@TimeRate::get、GameConfig@GameSpeedRate::set 等，結果是不夾值。
- Array.Sort 與 Array.QuickSort，不在已知清單：
  - Sort#0 [34] (self,hll_func) 41 次、QuickSort#1 [39] 10 次，comparator 被丟掉（Array.c:770/819）。
  - 四個多載的 C 都是 void，AIN 回傳 wrap<?>。hll_call 在 ffi.c:774-789 推入殘值，bytecode 把它存進 dummy local 後 DELETE。探針實測殘值 r.i=1805442416 與 1。殘值落在 [2,heap_size) 時會對任意物件 heap_unref。
  - 字串陣列按 heap slot 編號排序。
  - EXE 證據：dispatcher case 34/35/38/39 = 0x644907/0x644927/0x644972/0x64498b，各自呼叫 0x648340/0x648420/0x648700/0x6487e0；Sort(self) 內部呼叫 AscSort 的實作 0x648500。
- String.GetPart#0 [21]，16 次：綁到三參數的 String_GetPart（String.c:328），length 讀殘值，屬 UB。呼叫點（例：CEffectData::GetFileName，L285611）顯示語義是從 index 取到字尾。
- String.Match#0 [17]，6 次：綁到 String_Match(self, int ml_slot, regex)（String.c:261），實作轉交 SearchAll 的寫死 tokenizer，完全不看 regex，恆回 true。探針實測 Match("FooterButton_1","^zzz$")=1。
  - 受影響的是 ActivityHelper::GetUser<FooterButton/RivalShopView/SkillSelector/BattleBonusView/BattleResultPlayerView/RoundBonusButton> 裡 Where 的過濾（fno 37567-37577），實際上不再過濾。
  - regex 指標的低 32 位被當成 ml_slot，落在 heap 範圍內時可能覆寫任意 page（未實測到）。
- HashMap.Any#0 [7]，1 次：C 讀殘值 key。探針實測非空 map 回 0。

已知五組交叉核對：runtime 綁定與呼叫數全部吻合，已知組走錯共 142 次。另外補三點：
- Equals#0 [40] 的 wrap<array> 以 sint32 傳入，C 端 page** 解參考，探針實測 SIGSEGV（Array.c:2399）。也就是形狀正確的版本本身就會崩潰。
- Max#1 讀殘值 func，殘值落在函式範圍時會呼叫任意 AIN 函式。
- Fill#1 參數順序錯位。

結果二：未綁定且 AIN 有呼叫的共 98 個宣告、172 個靜態呼叫點，全部走 UNIMPL。其中 74 個是 C 表無此名、24 個是 HLL_TODO_EXPORT（NULL）。主要集中在 PartsEngine 的 component 屬性、child 系列，以及 HTTPDownloader 和 FileOperation。GetUserComponentName 的呼叫點包含 runtime 的 activity 事件分派（fno 679）。

結果三（延伸，非多載）：DWARF 全量比對找到同類的 CIF 與 C 原型不符，探針實測以下三項 SIGSEGV：
- Array.Duplicate，14 處（Array.c:2232）
- TextFile.ReadAll/Read/ReadLine，8 處（hll.h:71）
- Array.Equals#0（見上）

另有 VSFile.ReadString（含 gamesave）、FileOperation.GetFileList/GetFolderList（C 多讀 out 參數，回傳 bool 被當 array slot）、SYSTEMONLY_GetStructPageList（void 回傳殘值，進入存檔路徑）、SealEngine 三個 string 回傳函式回 int、LayoutBox 的 float/int 錯位。

修法建議：把 array_select_function 推廣成「名稱＋參數數＋float 標記」的全庫選擇器，可參照 Rufim 的 HLL_EXPORT_N/HLL_EXPORT_F。void 對 wrap/int 的回傳改回 -1，或回已 heap_ref 的 slot。wrap<ref> 參數的 C 原型改收 int slot。DWARF 比對腳本可作為回歸檢查。

未驗證：production（-O2）的殘值數值；Match 原版是全匹配還是 search；Sort 是否 stable；原版 wrap<?> 回傳物件的內容；CASTimer 的 Min#1 是否被 native 替換遮蔽。

注意：20:15:19 另有其他程序改動 harness 的 probe/runtime_probe.c 並新增 array_overload_fixture.inc，不是本掃描造成的。

## cases
- **Math.Abs** wrong_calls=33 | 兩個宣告都綁到 HLL_EXPORT(Abs, abs)，即 libc int abs(int)（Math.c:292）；fabsf 只以 AbsF 名稱匯出 | [7] int Abs(int value) (#0 32 次); [8] float Abs(float value) (#1 33 次) | CIF F:F 對上 C 的 int。arm64 探針實測 Abs(-3.5) = -3.5（原值直接回傳）。呼叫點含 FLOAT_EQUALS（fno 20536，L831372-831388）、BuffIcon@GetValueString、PlayerParamDetailView、CASTaskParts@GetMotionValue
- **Math.Min** wrong_calls=24 | 全部綁到 Math_Min(int,int)（Math.c:88，匯出在 :304）；Math_MinF 只以 MinF 名稱匯出 | [25] int(int,int) (#0 57); [26] float(float,float) (#1 13); [27] int(int,int,int) (#2 1); [28] float×3 (#3 4); [29] int×4 (#4 1); [30] float×4 (#5 5) | 探針：Min(5.0,2.0)=5、Min(3,2,1)=2、Min(4,3,2,1)=3，float 版恆回 x。L18423 CASTimerManager@UpdateRate 為 Min(10.0f, …)，錯誤時恆回 10.0（可能被 native timer 替換遮蔽，未驗證）
- **Math.Max** wrong_calls=30 | 全部綁到 Math_Max(int,int)（Math.c:98，匯出在 :306） | [31] int(int,int) (#0 90); [32] float×2 (#1 18); [33] int×3 (#2 2); [34] float×3 (#3 5); [35] int×4 (#4 2); [36] float×4 (#5 3) | 探針：Max(1.0,7.0)=1、Max(1,2,3)=2、Max(1,2,3,4)=2。FLOAT_EQUALS 的分母用 Max#1；另見 Player@TiredRate::get
- **Math.Clamp** wrong_calls=35 | 兩個宣告都綁到 Math_Clamp(int,int,int)（Math.c:103，匯出在 :316）；Math_ClampF 只以 ClampF 名稱匯出 | [37] int(int,int,int) (#0 62); [38] float(float,float,float) (#1 35) | 探針：Clamp(5.0,0,1)=5、Clamp(-2.0,0,1)=-2，完全不夾值。遊戲層呼叫點：EasingCalculator::GetByType<float>、Motion::Executer@OnUpdate、TimerCallback@TimeRate::get、GameConfig@GameSpeedRate::set、ScrollBase、CameraCalculator、DecisionTimer 等
- **String.GetPart** wrong_calls=16 | 兩個宣告都綁到 String_GetPart(string**, int begin, int length)（String.c:328）；String_GetPartChar（:317）未匯出 | [21] string GetPart(ref string self, int index) (#0 16); [22] string GetPart(ref string self, int index, int length) (#1 76) | [21] 的 length 讀 x2 殘值，屬 UB：length<=0 時回空字串，小正數時截斷。呼叫點語義為取到字尾，例：CEffectData::GetFileName 的 name.GetPart(FindLast("\\")+1)（L285611）。探針 3 次碰巧都正確，不可依賴
- **String.Match** wrong_calls=6 | 兩個宣告都綁到 String_Match(string**, int ml_slot, string* regex)（String.c:261），實作轉交 String_SearchAll（:171）的寫死 tokenizer | [17] bool Match(ref string self, string regex) (#0 6); [18] bool Match(ref string self, wrap<array<string>> matchList, string regex) (#1 0) | regex 指標的低 32 位被當 ml_slot，C 的 regex 參數是殘值，結果恆回 true。探針實測 Match("FooterButton_1","^zzz$")=1。使用點是 6 個 GetUser<T> lambda（fno 37567-37577，L1717751-1718071）的 Where，實際上不過濾。ml_slot 落在 heap 範圍內時可能覆寫任意 page（未實測到）
- **HashMap.Any** wrong_calls=1 | 兩個宣告都綁到 HashMap_Any(int id, string* key)（HashMap.c:153，匯出在 :255） | [7] bool Any(int id) (#0 1); [8] bool Any(int id, string key) (#1 1) | [7] 的 key 讀殘值。探針：先呼叫 Any[8](id,"zzz")，再呼叫 Any[7](id)，對非空 map 回 0（期望 1）；殘值若是無效指標會崩潰。呼叫點：utility::detail::CHashMap@Any（fno 20966，L846648）
- **Array.Sort** wrong_calls=41 | 兩個宣告都綁到 void Array_Sort(page**)（Array.c:770，依 .i 做插入排序；匯出在 :2427） | [34] wrap<?> Sort(self, hll_func) (#0 41); [35] wrap<?> Sort(self) (#1 12) | #0 的 comparator 被丟掉。兩個宣告都是 void 對 wrap<?>：殘值被推入後 DELETE，探針實測 r.i=1805442416。#1 有 4 次是 arg3=2，按 slot 排序（△）。EXE：#34→0x644907→0x648340（使用 func）；#35→0x644927→0x648420，其內部呼叫 AscSort 的實作 0x648500
- **Array.QuickSort** wrong_calls=10 | 兩個宣告都綁到 void Array_QuickSort(page**, int comparator)（Array.c:819，comparator 被 (void) 丟掉，qsort 依 .i 排序） | [38] wrap<?> QuickSort(self) (#0 10); [39] wrap<?> QuickSort(self, hll_func) (#1 10) | #1 的 comparator 被忽略。#0 的 C 多讀 comparator 但沒使用（ABI 上無害）；10 次都是字串陣列，按 heap slot 排序（△），GetUser<T> 也用到。回傳 void 對 wrap<?>，探針實測殘值 r.i=1。EXE：#38→0x648700（值比較器 0x645dd0），#39→0x6487e0（謂詞比較器 0x645f70）
- **String.Search** wrong_calls=0 | 兩個宣告都綁到 String_Search(string**, int, string*)（String.c:159） | [14] bool Search(ref string self, string regex) (#0 0); [15] bool Search(ref string self, wrap<array<string>>, string regex) (#1 0) | 潛在：[14] 的 C 多讀 1 個參數，情況同 Match#0。CN 目前 0 呼叫
- **String.PadLeft/PadRight/Trim/TrimStart/TrimEnd** wrong_calls=0 | String_PadLeft/PadRight(string**,int)（String.c:340/354）；String_Trim/TrimStart/TrimEnd(string**)（:424/431/438） | PadLeft [23](self,int) #0 0、[24](self,int,int) #1 0；PadRight [25]/[26] 0/0；Trim [29](self) #0 13、[30](self,string) #1 0；TrimStart [31]/[32] 0/0；TrimEnd [33]/[34] 0/0 | 潛在：第 2 參數版的 paddingChar 或 charList 被忽略（ABI 上無害，語義錯）。Trim#0 的 13 次形狀正確

## unimplemented_called
- FileOperation.DeleteFile[1] x7 | C 表無此名、無動態註冊；編輯器/除錯；例：activityeditor::detail::CAEFileTreeForm@CreateFile fno2310 L138086
- PartsEngine.GetUserComponentName[694] x7 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：activity::detail::CallUserComponentEventWithChild fno679 L28242
- InputString.IsOpenIME[6] x4 | C 表無此名、無動態註冊；編輯器/除錯；例：parts::detail::CPartsTreeNode@OnKeyUpEditLabel fno8892 L508728
- FileOperation.CopyFolder[3] x3 | C 表無此名、無動態註冊；編輯器/除錯；例：activityeditor::detail::CAEFileTreeForm@PasteFolderImp fno2316 L138524
- FileOperation.OpenFolder[11] x3 | C 表無此名、無動態註冊；編輯器/除錯；例：activityeditor lambda fno22484 L135264
- HTTPDownloader.IsRun[2] x3 | HLL_TODO_EXPORT(NULL) @hll/HTTPDownloader.c:30；網路服務（CAS*NetService）；例：CASGameNetService@Connect fno7732 L476714
- HTTPDownloader.UTF8ToSJIS[10] x3 | C 表無此名、無動態註冊；網路服務；例：CASGameNetService@Connect fno7732 L476762
- PartsEngine.GetChild[176] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@GetChild fno14221 L637670、PartsHelper::GetPartsChildren fno26913
- PartsEngine.GetComponentAlphaClipper[150] x3 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:606；遊戲/引擎層可達；例：fno8364 L488068
- PartsEngine.GetComponentDrawFilter[122] x3 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:589；遊戲/引擎層可達；例：fno8336 L487745
- PartsEngine.GetComponentReverseLR[143] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8356 L487971
- PartsEngine.GetComponentReverseTB[142] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8357 L487982
- PartsEngine.GetComponentRotateX[130] x3 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:597；遊戲/引擎層可達；例：fno8344 L487833
- PartsEngine.GetComponentRotateY[131] x3 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:598；遊戲/引擎層可達；例：fno8345 L487844
- PartsEngine.GetComponentTextureAddressType[164] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8368 L488112
- PartsEngine.GetComponentTextureFilterType[162] x3 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:608；遊戲/引擎層可達；例：fno8366 L488090
- PartsEngine.GetMotionEndFrame[206] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8400 L488521
- PartsEngine.IsComponentEnableClipArea[134] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8348 L487877
- PartsEngine.IsComponentMessageWindowEffectLink[108] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8626 L492820
- PartsEngine.IsComponentMipmap[166] x3 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:610；遊戲/引擎層可達；例：fno8370 L488134
- PartsEngine.Parts_GetTextLineSpace[740] x3 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CTextParts@LineSpace::get fno15876 L678473
- HTTPDownloader.GetReadSize[4] x2 | HLL_TODO_EXPORT(NULL) @hll/HTTPDownloader.c:32；網路服務；例：CASGameNetService@Connect fno7732 L476734
- HTTPDownloader.ReadAllString[5] x2 | C 表無此名、無動態註冊；網路服務；例：CASGameNetService@Connect fno7732 L476743
- HTTPDownloader.SJISToUTF8[11] x2 | C 表無此名、無動態註冊；網路服務；例：CASGameNetService@SendCommandImpl fno7728 L476578
- HTTPDownloader.Stop[3] x2 | HLL_TODO_EXPORT(NULL) @hll/HTTPDownloader.c:31；網路服務；例：CASGameNetService@Connect fno7732 L476728
- PartsEngine.AddChild[171] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@AddChild fno14210 L637276
- PartsEngine.ClearChild[170] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@ClearChild fno14216 L637573
- PartsEngine.GetChildIndex[175] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@GetChildIndex fno14223 L637750
- PartsEngine.InsertChild[172] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@InsertChild fno14214 L637468
- PartsEngine.IsComponentSubColorMode[116] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@SubColorMode::get fno14050 L633176
- PartsEngine.IsExistChild[169] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@IsExistChild fno14218 L637611
- PartsEngine.Parts_GetComment[182] x2 | C 表無此名、無動態註冊；編輯器/除錯；例：AFL_Debug_GetPartsComment fno4097 L257466
- PartsEngine.Parts_SetPartsCGDetectionSurfaceArea[792] x2 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:819；遊戲/引擎層可達；例：CCGDetectionParts@SurfaceArea::set fno9639 L536608
- PartsEngine.Parts_SetPartsRectangleDetectionSurfaceArea[788] x2 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:817；遊戲/引擎層可達；例：CRectParts@SurfaceArea::set fno14688 L651237
- PartsEngine.RemoveChild[173] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：parts::detail::CParts@RemoveChild fno14217 L637592
- PartsEngine.SetButtonEnable[212] x2 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:632；遊戲/引擎層可達；例：CButtonParts@Enable::set fno9278 L528909
- PartsEngine.SetComponentMessageWindowEffectLink[107] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8625 L492811
- PartsEngine.SetComponentMipmap[165] x2 | HLL_TODO_EXPORT(NULL) @hll/PartsEngine.c:609；遊戲/引擎層可達；例：fno8369 L488125
- PartsEngine.SetComponentReverseTB[140] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8355 L487962
- PartsEngine.SetComponentTextureAddressType[163] x2 | C 表無此名、無動態註冊；遊戲/引擎層可達；例：fno8367 L488103。另有 4 個同為 2 次、排在第 40 名之後：SetComponentTextureFilterType[161]、SetMotionData[204]、SetNumeralFont[767]、SetUserComponentName[693]

<PORT>/reports/hll-overload-scan-2026-09-28.md
<scratchpad>/overload/hllscan/enum_probe.c
<scratchpad>/overload/hllscan/abi_probe.c
<scratchpad>/overload/hllscan/build_one.py
<scratchpad>/overload/hllscan/bind.tsv
<scratchpad>/overload/hllscan/abi.out
<scratchpad>/overload/hllscan/abi.err
<scratchpad>/overload/hllscan/audit2.json
<scratchpad>/overload/hllscan/sev.txt
<scratchpad>/overload/hllscan/unbound_why.json
<scratchpad>/overload/hllscan/ext_sites.txt
<scratchpad>/overload/hllscan/exe_disasm.py
<scratchpad>/overload/hllscan/calls.json
<scratchpad>/overload/hllscan/libs.json