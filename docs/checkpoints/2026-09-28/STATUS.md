# 2026-09-28 新遊戲人物 ID assertion 修正

**最新狀態：delegate 呼叫的參數複製改為一格堆疊對一個參數變數（`2005274`，本機 commit，尚未推送）。修正前，兩槽參數（介面、option、ref int 等）後面的 void 伴隨變數被當成下一個參數：參數後的第一個區域變數被寫成 delegate page 的 slot，後續參數依錯的型別加參照。Tutorial 的 selector 因此讓 `ArrayExtensions::Select` 的 delegate page 被釋放，特殊客人收入函式則會釋放借用的 SpecialCustomer。修正照原版 `0x66dce0`／`0x657430`。新模式 `delegate-args` 在 `6421e6e` 上 3/4 失敗、修正後全過；48 個模式在預設與強制 GBK 兩種組態都 `VERDICT PASS`；150 秒 GUI MSG 88、assertion 0、堆疊溢位 0。前一組是 CN 文字依原版 GDI 字格排版（`9f81bd9`）。此前各組到 `6421e6e`（含 `9f81bd9` 與立繪與名牌 use-after-free 的 `9e30c0f`、`4d52a87`）已在遠端。從讀檔畫面讀一般存檔仍需 `system.Reset`（stub），記憶體成長與長時間穩定性未解決，尚非穩定可玩版。**

接續 [2026-09-26 交接](../2026-09-26/STATUS.md)（`22e9496`）。本 checkpoint 含三批引擎修正：人物 ID 的 `ff6fc2f`；Array overload 的 `3386e7d`..`763f5bd`；以及第二批 `4c7b820`..`6ec6258`（子元件查詢、Math、Sort、String、檔案與版面原型）。之後依序是成就通知、角色對白、存讀檔持久化與 GBK 字元規則；libsys4 在 GBK 字元規則一組由 `8c93946` 改為 `247f544`（使用者同意）。

## 根因

CN AIN 的 Array 庫對 `First` 有四個宣告：兩個 `First(ref array self)`，兩個 `First(ref array self, hll_func func)`。引擎只有一個 C 實作 `Array_First(array, func)`，所有宣告都綁到它。單參數呼叫的 libffi CIF 只傳一個參數，`func` 因此是前一次呼叫留在參數暫存器的值。`Array_First` 對越界的 `func` 直接回 -1；若殘值剛好是有效函式編號，則會把任意 AIN 函式當 predicate 執行。

9/26 追到的 -1 來源在更上游：

| 步驟 | 函式 | 指令 | 值 |
|---|---|---|---|
| 1 | f28943 `CreatorHelper::Lottery` | `CALLHLL Array Shuffle 2`（seed -1），緊接 `CALLHLL Array First#1 2` | First 回 -1 |
| 2 | 同上 | `.LOCALREF result; .LOCALREF selected; A_REF; CALLHLL Array PushBack 2` | result 內含 -1 |
| 3 | f28987 → f28986 → f28980 `WorkerCreator@Create` | `CALLHLL Array Concat 2` | p 內含 -1 |
| 4 | f36371 `ArrayExtensions::Take<string>` | `X_REF 1; A_REF; PushBack` | 第 0 項 -1 |
| 5 | f28980 foreach | `X_REF 2; X_REF 1; A_REF; NEW Personality 28149`（0x5e8b88） | ctor 收到 -1 |

9/26 的 [worker-followup](../2026-09-26/work/string-stage4-20260926/worker-followup.md) 建議從 Take 的所有權查起；Take、A_REF 與 Concat 其實都只是忠實傳遞 -1。

## 修正

`ffi.c` 的 `link_static_library` 已對 `Erase`、`IsExist`、`Numof`、`Count`、`Find` 依宣告選實作。`ff6fc2f` 對 `First` 做同樣的事：宣告含 `hll_func` 參數時綁 `Array_First`，否則綁新的 `Array_First_NoPred`，其回傳契約與 `At(0)` 相同。沒有依遊戲函式編號或名稱做特例。

## 驗證

Headless 使用 9/26 的真 AIN／真 ffi 探針（[重跑方式](#重跑)），新增 `first-overload` 模式。對 `["alpha","beta","gamma"]` 先 `Shuffle(-1)` 再 `First#1`，與 Lottery 的呼叫形狀相同。

| 版本 | First#1 結果 | 對照 At(0) |
|---|---|---|
| `22e9496` | 20 輪皆 -1（[輸出](first-overload/before-22e9496.txt)） | 20 輪皆正確 |
| `ff6fc2f` | 20 輪皆為存活字串（[輸出](first-overload/after-ff6fc2f.txt)） | 20 輪皆正確 |

其他 10 種既有模式在 `ff6fc2f` 的結果與 9/26 相同：9 種 exit 0、`deleted-event` 仍為 23 個殘留 slot／exit 87；ASan/UBSan 診斷 0（[摘要](first-overload/regression-ff6fc2f.txt)）。

GUI 以 optimized build、隔離 home／saves、`--skip-title`、按住 Return（`XSYS4_HOLD_KEYS=13`）無人值守執行：

| 版本 | 秒數 | 停止原因 | MSG | 最深 base frame |
|---|---:|---|---:|---|
| `22e9496` | 1.0 | Personality assertion | 0 | — |
| `ff6fc2f` 前身（相同 patch，另一路徑建置） | 75.3 | 時限 | 2 | `DohnaDohna@RunTurnStart` |
| 同上，第二輪 | 70.3 | 時限 | 2 | 同上 |
| `ff6fc2f` | 60.2 | 時限 | 2 | 同上 |

MSG 為開場旁白前兩句。「時限」表示 runner 在指定秒數後送 SIGTERM，不代表遊戲流程已完成。本輪沒有取得遊戲視窗的畫面截圖；桌面截圖只抓到前景其他應用程式，已刪除。

修正後日誌仍有：`X_ASSIGN 2 past end of page` clamp 3 次、`heap_alloc_slot: free list exhausted/corrupt` 1 次（啟動早期）、`Failed to load WAV -1` 3 次、`CrayfishLogViewer.SetSaveFolderName` 未實作 1 次。修正前的執行 1 秒即停，無法比較這些警告是否新出現；因果尚未查。

## Array overload 批次（`3386e7d`..`763f5bd`）

First 的缺陷不是單一個案。v14 Array 庫有 15 個名稱各有多個宣告（參數數不同，或多一個 `hll_func`），`link_static_library` 只按名稱綁到一個 C 函式，libffi 卻依宣告建立呼叫介面。結果是讀暫存器殘值、參數錯位、忽略 predicate，或 C 端根本沒 push 回傳值。

五組語義各由一位調查者以原版 EXE（`dohnadohna_dump_SCY.exe`，Array 跳表 0x644f18）反組譯確認，再由一位反駁者獨立複核；實作採用確認後的語義，並照反駁意見修正。

| commit | 範圍 | CN 呼叫數 | 修正前 |
|---|---|---:|---|
| `3386e7d` | ffi 改為對 Array 呼叫單一 `array_select_function(宣告, 預設)`；不認得的形狀保留原綁定 | — | 行為不變 |
| `5387ea4` | LowerBound / UpperBound / BinarySearch 的值版與比較函式版 | 60 | 函式編號被當搜尋值；字串比 slot 編號 |
| `ee44795` | FindLast 四種（反向掃描、end 包含在內） | 4 | 函式編號被當值，恆回 -1 |
| `d293820` | Min / Max / Last(pred) | 33 | Min 完全不 push 回傳值；Max 單參數讀殘值；Last 忽略 predicate |
| `6e6ed53` | Any(pred)、Unique、UniqueSorted、Equals | 64 | Any 只回非空；Unique 只刪相鄰且比 slot；Equals 解參考 slot 編號崩潰 |
| `763f5bd` | Fill、Copy、Realloc(n, value) | 19 | 參數錯位或殘值；Copy 的 destIndex 被當 wrap handle 越界 |

Headless（[fixtures 與摘要](array-overload/)，[探針總表](array-overload/probe-summary-763f5bd.txt)）：每組 fixture 使用真 AIN 的資料與 lambda（例如 IdSet、CASPos、FrameDamage、SMotionAlphaParam），修正前均失敗或崩潰，修正後全過，heap 回到基準。形狀檢查（所有不帶 predicate 的 overload，int 與字串陣列）修正前 5 項 stack 不符或崩潰，修正後 0 項。既有 10 種模式結果不變，`deleted-event` 仍為 23 個殘留 slot。

設計原則：新程式碼不得比舊綁定更糟。遇到不支援的 callback 形狀（例如 Insert 建出的兩槽介面陣列），印有限次數警告並回傳安全值，不停止 VM。

GUI（`--skip-title`、按住 Return、每 1.2 秒自動點擊畫面中央，120 秒）：

| 版本 | 推進 | assertion | Array 警告 | 結束 | 峰值 RSS |
|---|---|---:|---:|---|---:|
| `a8d92df` 等價建置 | 開場旁白 11 句 | 0 | 0 | SIGTERM 後未退出，被 SIGKILL | 687 MB |
| `763f5bd` | 開場旁白 11 句 | 0 | 0 | 正常退出 | 492 MB |

## 第二批（`4c7b820`..`6ec6258`）

| commit | 範圍 | CN 呼叫數 | 修正前 |
|---|---|---:|---|
| `4c7b820` | PartsEngine `GetChild`、`GetChildIndex`、`IsExistChild`、`Get/SetUserComponentName` | — | `GetChild` 未實作回 0，activity 事件分派把 0 當根元件，無限遞迴至堆疊溢位 |
| `3ca107c` | Math `Abs/Min/Max/Clamp` 的 float 版與 3、4 參數版 | 122 | 全綁 int 兩參數版：`Clamp(5.0, 0, 1)` 回 5、`Min(3, 2, 1)` 回 2 |
| `fa185e4` | Array `Sort/AscSort/DescSort/QuickSort/Remain` | 78 | 比較函式被忽略；C 回 void 而宣告回 `wrap<?>`，殘值被 DELETE；`GetUser<T>` 等 11 處拿到 -1 |
| `8329dc0` | String `GetPart`、`Trim` 系列、`Pad` 雙參數版 | 105 | `GetPart(index)` 讀殘值；`Trim` 會改動來源字串 |
| `9ca9200` | `FileOperation.GetFileList/GetFolderList`、`TextFile.Read*`、`VSFile.ReadString`、SealEngine 字串回傳、LayoutBox float | 36 | 啟動時的 `GetFileList` 在資料夾存在時崩潰；讀檔類解參考 slot 編號崩潰 |
| `6ec6258` | String `Match/Search`（ECMAScript 子集，無回溯） | 6 | `Match` 恆回 true，`GetUser<T>` 的名稱過濾失效 |

`link_static_library` 現在對每個庫呼叫一次 `hll_select_overload(庫名, 宣告, 預設)`，由各庫依宣告形狀選 C 實作；不認得的形狀一律保留原綁定。語義同樣由原版 EXE 反組譯確認、每組配一位反駁者。反駁推翻或修正的部分已照改：Copy 長度夾限、HashMap `Free` 語義、`GetStructPageList` 在實機只配 1 個 slot 等。

Headless 全部 33 個模式（[總表](array-overload/probe-summary-6ec6258.txt)）：除既有的 `deleted-event` 外全數通過，0 個 sanitizer 診斷；每組都以上一個 commit 的原始碼做對照，修正前失敗或崩潰。

GUI（150 秒自動點擊）：`4c7b820` 前對白停在第 11 句並有三萬餘次堆疊溢位；之後推進到第 88 句、無溢位，畫面到據點場景（背景、立繪、對話框框體、說話者頭像正確）。第二批其餘 commit 之後結果相同，配置器警告數也相同，沒有新的回歸。

## 成就通知（`2914b40`）

名稱查找本來就成功；失敗原因是低階部件沒有建立各狀態的正確型別。普通狀態應為文字 21，移入與按下應為空 CG 19；getter 原本全部回 0。原版 `0x5b8bc0` 依狀態名稱建立部件，`0x4df5b0`／`0x533d80` 分別回 21／19。相關 HLL 都只有一種宣告，本組沒有更動 ABI 綁定。

新增 `activity-text` 合成 fixture：基準 `2167bbc` exit 86，修正後 exit 0；驗證具名查找、混排狀態、未知分支覆蓋、主文字與 ruby 分離、raw setter、無效 state 回退與 v13 相容。完整 34 模式 `VERDICT PASS`，sanitizer 0；deleted-event 仍為預期 exit 87。

GUI 基準在 120.865 秒因成就斷言停止；修正後跑滿 150.367 秒，MSG 88、assertion 0、堆疊溢位 0，後段 heartbeat 進入 SceneAzito 場景迴圈。已查看 framebuffer：背景、人物、對話框及頭像可見，角色對白仍空白。通知動畫、據點互動與存檔往返未驗證；不能將本次通過解讀為可玩版。

[研究、原版位址、反駁紀錄及重跑工具](research/achievement-text/README.md) · [34 模式摘要](research/achievement-text/verify-summary.txt) · [GUI 摘要](research/achievement-text/gui-summary.json)。截圖、遊戲資產及完整 dump 均留在 repo 外；libsys4 仍為 `8c93946`。程式修正已推送，git ls-remote 與 GitHub branches API 均確認 `2914b40344dce283812d0817517f0959b748791d`。

## 角色對白與陣列生命週期（`1540b85`）

訊息模型清空後，Array.Free 丟棄型別資訊、Clear 換成整數空頁，使下一個 EmplaceBack 回傳兩槽整數參照，而非呼叫端期待的一槽 CMessageText。原版 `0x67ec50` 清空內容但保留 descriptor 與 stride；本次在 v14 保留空頁型別，並補空集合 Find/Count 早退，避免新增 VM_ERROR。

另依原版 ShallowCopy `0x658d40`／`0x67f2e1`，補上 rank 1、具體 struct/string 元素的獨立 owner。兩個陣列仍共享同一元素；刪除任一陣列不再提早釋放另一份的資料。未知形狀、NULL 及舊版本維持原路徑，完整 wrap descriptor 並未重做。

新增 `dialogue-model` 與 `dialogue-copy`：在 `6855c2f` 皆 exit86、sanitizer 0；修正後皆 exit0，完整 36 模式 `VERDICT PASS`。既有 deleted-event 仍為預期 exit87。

正式 GUI 跑滿 150.223 秒、MSG 88、assertion 0、堆疊溢位 0，後段進入 RunHome／SceneAzito。已視讀正常 framebuffer `xsys4_t18.png`，角色正文可見。僅修 typed clear 的中間版本曾出現 PlayerCollection 斷言；合併 owner 修正後未再出現。逐畫格比對也排除了曾懷疑的後續部件蓋字，沒有修改渲染排序。

峰值 RSS 1,898,332,160 bytes。存檔往返、據點所有互動、長時間穩定性及所有中文切字均未驗證，不以本次短測宣稱已可完整遊玩。

[研究與原版位址](research/dialogue-text/README.md) · [36 模式摘要](research/dialogue-text/verify-summary.txt) · [GUI 摘要](research/dialogue-text/gui-summary.json)。修正已推送；git ls-remote 與 GitHub branches API 均確認 `1540b85d5b7621f50416cf8849526f34df2d1c91`。libsys4 維持 `8c93946`。

## 存讀檔持久化（`173ff1d`）

`Array.SYSTEMONLY_GetStructPageList` 原本是空函式，AIN 卻宣告回傳 `array<int>`，所以 `system.SerializeStruct` 拿到殘值、回 true 卻沒寫檔；`DeserializeStruct` 把讀回的物件放進用完即丟的清單。新增 `src/serialize_struct.c`：依原版 `0x65f160`／`0x65c910`／`0x65f3b0`／`0x65c250` 寫出 v9、解析 v7..v9、依成員 name2 就地載入，並實作存檔註解。GetStructPageList 依 `0x64a2d0` 回傳 struct handle 清單。所有新綁定依宣告形狀選用，舊形狀保留原綁定。

兩份反駁照改的部分：ffi 在 HLL 重入 VM 後從參數複本釋放 by-value 參數（讀檔時建構子會覆寫參數槽）；`delete_page_vars` 不再把 int 類 option 值當 heap slot 釋放；巢狀記錄參照在套用前檢查；陣列先換新頁再釋放舊元素；寫檔改用同步後的暫存檔再 rename。不同意的兩項與理由見研究文件。

新增 `save-list`、`save-roundtrip`、`save-comment` 三個模式：72a33e5 上 9/9、16/18、5/6 個案例失敗（舊寫出器在 `savedata.c:125` ASan BUS），修正後全過；完整 39 模式 `VERDICT PASS`，deleted-event 仍為預期 exit 87，sanitizer 0。原版存檔的本機副本：AFConfig、Collection、AFCommon、AFInfo 讀入後重存逐位元組相同；SaveData1000／5000 可在 headless 讀入 LocalGame。

GUI：Run 1 新存檔 150.389 秒，寫出 Collection.asd；Run 2 以 Run 1 的存檔加上標記作種子，150.225 秒，AFConfig／Achievement（原版檔副本）／Collection 讀入成功、無警告，AFConfig 的 `<ConfigVoiceMutedByNsfw>` 被遊戲由 1 改回 0 並重寫，Collection 重寫後標記仍在且已讀事件沒有重複。兩次都是 MSG 88、assertion 0、堆疊溢位 0，已查看 framebuffer。PE_Save／PE_Load 與成就通知在兩次執行中都沒有觸發，讀檔後的成就通知仍未驗證。

**審查後修正（`6b65b12`）**：值型別 option（`option<int>` 等）在 ASSIGN、X_OP_SET、刪除與複製時一律不當 heap slot；讀檔前先驗證 struct 定義數，損壞檔不再崩潰；讀檔新建沒有建構子的 struct 時照原版預設初始化成員。`save-fixes` 5 個案例在 `ce2cd59` 上 5/5 失敗、修正後全過；40 模式 `VERDICT PASS`；150 秒 GUI MSG 88、assertion 0、堆疊溢位 0，峰值 RSS 降到約 1.77 GB。一般 `NEW` 建立無建構子 struct 時成員仍為 null，另列待辦。

**第二輪審查修正（`ff77c18`）**：讀檔新建無建構子 struct 時照原版填 `<vtable>`（Player 22 項、BattleSkill 51 項，否則讀檔後戰鬥的介面分派會叫錯函式）；三槽 option 的 none 改為原版的 (-1, -1, 1)；值型別 option 的區域變數不再配置 slot（每次呼叫漏一個）。`save-fixes` 增為 8 案例，F6..F8 在 `64bb9be` 失敗、修正後全過；41 模式 `VERDICT PASS`；150 秒 GUI MSG 88、assertion 0、堆疊溢位 0，峰值 RSS 降到約 1.44 GB。

[研究、原版位址、反駁處理與重跑](research/save-persistence/README.md) · [39 模式摘要](research/save-persistence/verify-summary.txt) · [GUI 摘要](research/save-persistence/gui-summary.json)

## GBK 字元規則（`6400e3c`，libsys4 `247f544`）

CN 原版的 String 庫、C_REF／C_ASSIGN 與排版都以首位元組 `0x81..0xFE` 判斷雙位元組，字元碼為 `(lead<<8)|trail`，越界回安全值（dispatcher `0x684120`；Length `0x6868f0`、GetPart `0x6882c0`、Split `0x6892f0`、C_REF `0x67dbb0`、C_ASSIGN `0x67dc10`）。xsystem4 與 libsys4 原本都用 SJIS 規則，大部分漢字被拆成兩個字，C_REF 回 little-endian 碼且越界中止，`%D` 輸出 SJIS 全形數字，iarray 讀回時 6,143 筆非 ASCII STR0 字串中有 703 筆多一個 `FF`。

libsys4 新增執行期開關 `sys4_set_string_charset()`（預設 SJIS，舊本體不改），各字元函式加 GBK 分支；`8181da6` 先以測試記錄 SJIS 現況。xsystem4 在舊偵測成立且嚴格判定（SJIS 邊界分數、SJIS 非法而 GBK 合法的字串數、UTF-8 比例）也成立時才開啟；String 各函式的 GBK 分支照原版語義（Erase、FindLast、Replace 不改 self、Split 的 containsMode、ToLower／ToUpper、ToInt／ToFloat、SearchAll）；iarray 讀取在 GBK 模式下是寫入的精確反函數；渲染器的 GB18030 切字不再越過 NUL。`ain_is_gb18030` 與其使用點不變。

驗證：真正的修正前組態（`7c1daaa` + libsys4 `8c93946`，submodule 暫時切回）下 `gbk-string` 41 案失敗並以 ASan 中止、`gbk-vm` 18 案失敗、`gbk-detect` 缺函式；修正後全過。`sjis-chars` 的 91 行輸出修正前後逐行相同，`test/Run/test.ain` 輸出相同。44 模式在預設與 `XS4_PROBE_GBK=1` 下都 `VERDICT PASS`；libsys4 四個單元測試在 ASan／UBSan 下通過。三次 150 秒 GUI（新存檔、以修正前存檔為種子、HanaMinA 字型探針）MSG 88、assertion 0、堆疊溢位 0，MSG 內容與修正前相同。修正後名牌仍缺字，HanaMinA 探針則完整顯示，確認是字型問題；修正前同一角色兩種表情同時出現的立繪情況在修正後消失（推定改善，未與原版對照）。

[研究、原版位址、反駁處理與重跑](research/gbk-string-rules/README.md) · [驗證摘要](research/gbk-string-rules/verify-summary.txt) · [GUI 摘要](research/gbk-string-rules/gui-summary.json)

## 立繪與名牌的 use-after-free（`9e30c0f`）

使用者在 GUI 中看到：角色講完話不退場、一直疊加，名牌留下前一個名字，劇本放在右側的立繪停在左邊。追蹤結果：立繪的 sprite 在第一次登場時就被釋放了。釋放點是 `Motion::Create` 與 `Motion::Executer@0` 的 `RETURN`，被釋放的是 `IParts` 參數。原因是 v14 呼叫端以 `X_REF 2` 借用傳入介面參數，被呼叫端的 local page 在返回時卻會釋放它，而 `function_call` 沒有替 `AIN_IFACE` 加參照。這個 slot 被重用之後，`AdvStand@MoveOut`／`Move`、`AdvNamePlate@Hide` 讀 vtable 得到函式號 -1，VM 就靜默略過呼叫。

原版 `0x657430` 複製參數時，會替 REF、WRAP、87、IFACE、REF_ENUM 加參照；option 若有值，就依內含型別分派。修正照這套規則處理 `AIN_IFACE` 與 option 參數，delegate 路徑也一樣。

| 項目 | 修正前 | 修正後 |
|---|---|---|
| `iface-arg`（6 案） | 6/6 失敗，rc 89 | 全過 |
| 46 模式（預設／GBK） | — | `VERDICT PASS`／`VERDICT PASS` |
| 150 秒 GUI MSG／assertion／堆疊溢位 | 88／0／0（兩次） | 88／0／0（三次） |
| 名牌疊字（第 12–39 張 framebuffer） | 11、12 張 | 0 張 |
| `heap_alloc_slot` 警告／次 | 15 | 0 |
| 峰值 RSS | 1.76／2.05 GB | 1.33／1.38／1.33 GB |
| GUI 追蹤：-1 方法呼叫 | 334 | 34（皆為 Tutorial 的 null 物件） |

同源而一併消失的還有 `Motion::ExecuterCollection` 的 vtable 讀取越界與 free list 損壞警告。另案（delegate 伴隨槽被當成參數〔已由 `2005274` 修正〕、STRUCT 參數多加參照、Executer 與 CParts 不釋放）與側審查的字寬缺陷 D1 見 [uaf.md](research/gui-visual/uaf.md) 與 [HANDOFF.md](HANDOFF.md) 第 4 項。

## 字距：原版 GDI 字格（`9f81bd9`）

側審查指出 `05d2441` 的四個缺陷：`TextSurfaceManager.GetFontWidth` 仍用 size/2 且不算外框，腳本量到的寬度比實際繪字窄（backlog 量 22 px、畫 24 px，D1）；外框沒取 ceil、太さ與外框相加而非取 max，`PE_SetFont` 也沒保存太さ（event 視窗每字 24 px，原版 25，D2）；半形看 Unicode 不看首位元組（D3）；缺字 fallback 沒有限定 GBK（D4）。

原版 `0x69c7a0` 對 GDI 字型：e = max(min(ceil 太さ, 字級), min(ceil 縁取り, 字級))，字寬 = 首位元組 0x81..0xFE 時為字級、否則 (字級+1)>>1，回傳字寬 + 2e；`GetFontWidth` 的 HLL 入口 `0x69fb30` 只看第一個字，空字串回 2e；`Parts_SetFont`（跳表 case 729 → `0x69f230`）把 BoldWeight 與 EdgeWeight 存在它讀的兩個欄位。修正以 `gfx_text_cn_gdi()`（GBK 字元規則且無 .fnl）為條件，讓繪字、每字貼圖寬、`gfx_size_text` 與 `GetFontWidth` 共用這個字格；`PE_SetFont`／`PE_SetMessageWindowTextFont` 把太さ存進新的 `text_style.bold_weight`；缺字 fallback 只在這個組態啟用。

| 項目 | 修正前（`ca2ebff`） | 修正後 |
|---|---|---|
| `text-metrics`（12 組 pactex 樣式，GBK 161 項＋SJIS 97 項） | GBK 131 項失敗、SJIS 1 項失敗（fallback），rc 86 | 全過；SJIS 109 行與 `4a82758` 逐行相同 |
| 47 模式（預設／GBK） | — | `VERDICT PASS`／`VERDICT PASS` |
| 150 秒 GUI MSG／assertion／堆疊溢位 | 88／0／0（`9e30c0f` 參考執行） | 88／0／0，MSG 內容相同，峰值 RSS 1.30 GB |
| event 視窗／event 名牌／主視窗名牌／主對白（px／字） | 24／27／30／24（event 名牌依側審查與公式，其餘為 `9e30c0f` 執行的 framebuffer） | 25／27／30／24（原版 25／27／30／24） |
| backlog：GetFontWidth + 字距 對 實際前進 | 22 對 24 | 24 對 24 |

backlog、DungeonSelector、成就通知的畫面沒有走到；太さ的字形粗細、行高、PE_Save 不保存 `bold_weight` 等限制見 [spacing-fix.md](research/gui-visual/spacing-fix.md)。

## delegate 參數的伴隨槽（`2005274`）

uaf.md〈另案〉的第一項。v14 的兩槽參數在變數表裡占兩個變數：參數本身與一個 void 伴隨變數，delegate 的 `nr_arguments` 也把伴隨變數算在內。`delegate_call` 複製兩槽參數時已寫入兩個變數，之後又把伴隨變數當成下一個參數，於是之後每個變數都拿到下一格，最後一格讀到 `DG_CALLBEGIN` 放在參數上方的 delegate page slot。原版的 DG_CALL 處理常式 `0x66dce0` 從堆疊取 `nr_arguments` 個值，交給 `0x657430`，第 i 格進第 i 個變數，並依該變數的型別決定是否加參照；上游 xsystem4 原本也是一格對一個變數。修正照這個做法，option 規則改在全部複製完才套用。

| 項目 | 修正前（`6421e6e`） | 修正後 |
|---|---|---|
| `delegate-args` DA1：Tutorial selector lambda（真 rect） | 區域變數 2 = delegate page slot，呼叫後 delegate page 被釋放 | 區域變數 2 是自己的初值，delegate page ref 不變 |
| DA2：真 `ArrayExtensions::Select` 走四個真 rect | selector 只跑 2 次，2 個結果為 null | 跑 4 次，4 個都存活 |
| DA3：特殊客人收入函式（option, void, wrap, int） | 借用的 SpecialCustomer 被釋放；int 被當成 slot 加參照 | 兩者 ref 不變，回傳值相同 |
| 48 模式（預設／GBK） | — | `VERDICT PASS`／`VERDICT PASS` |
| 150 秒 GUI MSG／assertion／堆疊溢位 | 88／0／0（追蹤執行） | 88／0／0（兩次，其中一次為追蹤執行），MSG 內容相同 |
| GUI 追蹤：Select 的 `DG_CALL` 遇到 ref 0 delegate page | 1 | 0 |
| GUI 追蹤：-1 方法呼叫 | 34 | 34（分布相同，屬 null 物件的另案） |

特殊客人事件與編輯器路徑在 150 秒 GUI 中走不到，只有 headless 驗證。詳見 [delegate-args.md](research/gui-visual/delegate-args.md)。

## 下一批卡點

- **使用者回報的畫面問題**（[調查與進度](research/gui-visual/README.md)）：左側角色翻轉（`4a82758`）、字型缺字與字距（`05d2441`、`9f81bd9`）、立繪與名牌不退場（`9e30c0f`）已修正。剩下側審查 D5–D7（翻轉不作用在 TEXT／FLAT 與子元件等）；立繪站位與原版實機截圖逐格對照尚未做；backlog 等畫面的字距只有 headless 驗證。

- **從讀檔畫面讀一般存檔**：要經過 `system.Reset`，目前是 stub；`SceneLoad@Load` 之後不會重新啟動，也就讀不到 SaveData。`Ａ＿標題界面返回＿確認沒有` 在 Reset 之後的 Peek 迴圈可能卡住（未在執行中驗證）。
- **記憶體**：heap 在 120 秒內長到 1730 萬個 slot。`9e30c0f` 後 150 秒峰值 RSS 約 1.3–1.4 GB，配置器警告消失；STRUCT／DELEGATE／ARRAY 參數多加的參照、`Motion::Executer` 與 `CParts` 不釋放仍是成長來源候選。

## 尚未處理

- 上述卡點。
- 存讀檔的後續：DeleteSaveFile 在檔案不存在時原版回 true；`init_struct_slot` 把 enum 陣列配成 `AIN_ARRAY`，刪除時會把 enum 值當 slot 釋放（R2）；每次經 `AFL_GameSave_*` 包裝呼叫留下一個帶 TEMP 旗標的 A_REF 暫存字串；EnemyNamePostfixGenerator／FixedRandomValue 建構後刪除會殘留 slot；PE_Save／PE_Load 不保存 `component_type_from_state`，讀檔後成就通知未驗證。
- GBK 字元規則的另案：字型 fallback（名牌等缺字，約 11.4% 的對白）、名牌殘影、舊偵測誤判 SJIS 遊戲、`utf2sjis`／`sjis2utf` 轉碼、`Int.ToCharacter` 與 ReplaceRegex（見 [研究文件](research/gbk-string-rules/README.md)）。
- `Array.First` 的 predicate 版逐實體 slot 走訪；兩槽介面陣列（`Insert` 不保留兩槽）；`Array.Realloc` 縮小不釋放、誤用 `hll_arg3` 當 struct 編號；`Array.Duplicate`（僅編輯器）；`HashMap.Any/Empty/Free`（CN 無法執行到）；PartsEngine `AddChild/InsertChild/RemoveChild/ClearChild`。
- DeletedEvent 23 個殘留 slot；人眼畫面（本環境無單一視窗擷取權限，只驗 framebuffer）；長時間穩定性。

## 重跑

驗證環境在 [harness/](harness/README.md)：一次設定兩棵建置樹，之後一個指令跑全部 48 個模式、對任一舊版本做修正前對照，或做無人值守 GUI 執行。所有輸出寫在 repo 外。

```bash
export XS4_GAME=/path/to/game-workcopy XS4_MASTER_GAME=/path/to/original
bash docs/checkpoints/2026-09-28/harness/setup.sh
bash docs/checkpoints/2026-09-28/harness/verify-step.sh <tag>
```

`array-overload/` 與 `first-overload/` 內的探針原始碼是當時快照，已過時。遊戲資料必須是自行提供的合法副本，勿指向原始安裝目錄。

接手請先讀 [HANDOFF.md](HANDOFF.md)；兩輪調查報告在 [research/](research/)。
