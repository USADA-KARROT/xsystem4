# 2026-09-28 新遊戲人物 ID assertion 修正

**最新狀態（2026-10-05）：`ec00a16` 讓 v14 pactex 文本部件載入預設 `[文本]`，依原版文字狀態 loader `0x5c2d20` 在 `0x5c2fac` 讀入。春銷配對頁出現「人材3/3」「人材4/4」「人材5/5」「顧客1/4」的文字與「剩餘時間」，系統選單的八個項目名稱與說明也出現，與 Wine 原版參考一致。預設／GBK 各62模式 PASS、sanitizer0；新探針修前4/5失敗、修後5/5通過。正常GUI150.233秒、MSG88（與前組逐位元組相同）、assert／overflow0。獨立審查無 High；Medium 的佔位文字露出與新增命中區已在可達畫面抽查。程式已推送雙源核對，libsys4維持247f544。**

[本組研究與驗證](research/gui-visual/text-default.md)。計數標籤的灰底（構築部件手順未執行、命令122等未支援）與緊密排版（レイアウトボックス屬性未載入）是另外兩個根因；等待使用者決定下一組。

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

## 翻轉作用在整棵元件樹（`0ab8476`）

側審查 D5–D7。`4a82758` 的 `SetComponentReverseLR/TB` 只讓 CG 類元件在自己的方框內翻轉，TEXT、FLAT、rect 不理會，子元件也不繼承。原版（只做靜態反組譯）的每個元件更新 `0x535260` 先複製父元件累積好的 sprite 參數（`0x579460`），再用 `0x579bb0` → `0x4e6d80` 套上自己這一層：旗標 XOR 進累積狀態（`+0x49`／`+0x48`），矩陣乘上 `S(reverse ? -1 : 1)·T(-origin)·S(scale)·R·T(pos)`。所以翻轉以元件的錨點為軸，父元件翻轉時子元件位置也以父元件錨點鏡像；點擊判定 `0x57a6b0` 與 `Parts_GetPartsUpperLeftPos`（`0x534bb0`）走同一個矩陣，FLAT 也沿父元件鏈收同樣的紀錄（`0x537b00`）。

| 項目 | 修正前（`b5d8b9e`） | 修正後 |
|---|---|---|
| `reverse-inherit` RI1：真 AIN 宣告的三層元件，父元件翻轉 | 子孫位置、`Parts_IsCursorIn`、`Parts_GetPartsUpperLeftPosX` 都不變（17 項失敗） | 以父元件錨點鏡像，XOR 往下累積，還原後回到原值 |
| RI2：繪製變換與像素判定的反變換（16 種翻轉組合、旋轉、倍率；TEXT／FLAT；surface area） | 沒有共用變換 | 80 點與獨立公式一致，surface area 仍在錨點（D6） |
| RI3：真 bytecode `AdvStand@Move` 跨側 + `Motion::EndAll` | rect 根被設成 1，但影像照舊不翻轉 | 影像以錨點鏡像 |
| 49 模式（預設／GBK） | — | `VERDICT PASS`／`VERDICT PASS` |
| 150 秒 GUI MSG／assertion／堆疊溢位 | 88／0／0 | 88／0／0（兩次），MSG 逐行相同，多張 framebuffer 逐像素相同 |

開場 150 秒沒有跨側移動（臨時追蹤：40 次呼叫都在剛建立的立繪影像上），珀爾諾換到左側是新立繪。rect 根的翻轉以臨時注入驗證：立繪以錨點精確鏡像（最大像素差 0），朝向與原版實機截圖中被翻轉的珀爾諾相同。詳見 [reverse-inherit.md](research/gui-visual/reverse-inherit.md)。

## 據點畫面的元件（`6d39915`）

visual-compare 的 D1–D5、D7：據點畫面沒有底列（含「下一步」）、日數與金錢、貼紙、據點環節橫幅與教學覆蓋層，遊戲停在據點。`activity::detail::Load` 讀完 activity 立刻用 `NumofChild`／`GetChild` 走訪元件樹，把 EPartsType 17 交給使用者元件管理器；xsystem4 的 loader 用 `PE_SetParentPartsNumber` 只記下 `pending_parent`，走訪看不到子元件。原版（只做靜態反組譯）`0x58f060` → `0x58f310` → `0x550d90` 立即掛上。之後依序擋住的缺陷都照原版修正：`部件タイプ` 名稱表（`0x4eda70`／`0x5b8f90`）與 UC 名稱、`數據`；低階元件的數字（24）與 ＣＧ判定（27）狀態；`SetComponentType` 對低階元件只改狀態（`0x535e20` → `0x5653d0`）；`GetActivityParts`（`0x58a9c0`）；`Get/SetUserComponentData`（`0x597ff0`／`0x598020`）；`Array.Add` 與 PushBack 同一處理（`0x644455`）；`MainEXFile.Col` 先回 list 元素數（`0x4b00e0`）；`編輯上表示`（+0xac，`0x53c3c5`）；父元件倍率作用在子元件位置與文字（`0x4e6d80`）；`表示タイプ 2` 的字型數字；`SetButtonEnable`／`IsButtonEnable`（`0x590b70`／`0x590ba0`，停用時 `／無効`）。

| 項目 | 修正前（`22339c1`） | 修正後 |
|---|---|---|
| `base-ui` BU1：合成 pactex 讀檔後的走訪、型別、UC 名稱與數據、`GetActivityParts` | 子元件 0、型別全 0、名稱是節點名、數據與輸出都沒有 | 全部符合原版 |
| BU2：父元件立即掛上、循環與未知父元件、0 脫離、父元件倍率 | 更新前看不到子元件，循環讓更新迴圈卡住 | 通過 |
| BU3：`SetButtonEnable`／`IsButtonEnable` | 未實作／恆 false | 預設 true、可切換 |
| BU4：`Array.Add`／`PopBack` 兩槽、真 bytecode `SceneParentStack@Push`／`@Pop` | 只存一槽 | 兩槽、元素數正確 |
| BU5：`MainEXFile.Col`、真 bytecode `EXHelper::GetStringArray` | 0 | 3 |
| 50 模式（預設／GBK） | — | `VERDICT PASS`／`VERDICT PASS` |
| 150 秒 GUI MSG／assertion／堆疊溢位 | 88／0／0（UNIMPL `SetButtonEnable` 2 次） | 88／0／0，對白逐行相同；220 秒亦同 |
| 點擊序列推進 | 停在據點 | 教學兩頁、關閉、「下一步」→ 階段選擇 |

據點背景仍是黑的：教學結束後 `SceneTutorial` 被按鈕事件的 delegate 循環持有，`SceneContext` 不解構、教學圖層與底圖留下來；在階段選擇點選項後，引擎在 `PE_AddController` 停止。詳見 [base-ui.md](research/gui-visual/base-ui.md) 與 HANDOFF 第 4 項。

## 第二輪審查：delegate 不持有目標物件、A_REF 複製陣列（`6582e38`）

第二輪審查（`22339c1`..`ce5c75a` 與 `fb28975`）fix-first：高「按下新遊戲後標題按鈕仍可點」、中「標題構圖約 3 秒後被打亂」、低「標題→鑑賞模式→返回 VM_ERROR」「點擊派送略過系統元件」。高與第一個低的根因相同：`c3b5ff0` 讓 v14 delegate 對物件與環境都加參照，`SceneTitle`／`SceneTutorial`／`SceneOmake` 把自己的方法交給按鈕事件就形成循環，`SceneContext@1` 不執行、`EraseLayer` 不發生。原版（靜態反組譯）`DG_NEW_FROM_METHOD` `0x66bec0` → `0x6522c0`（只有 lambda 旗標的函式帶環境）→ `0x652210`（物件不加參照，delegate 登記在物件頁 +0x2c 的反向清單）；struct 釋放時 `0x6813c0` → `0x681f70` → `0x652970` 刪掉目標是它的項目。修正照此實作，並補上場景會釋放後才走到的四處：解構子內放掉的物件立即解構、`RemoveController`（`0x53fd90` → `0x53d500`）與 `ReleaseActivity`（`0x540050` → `0x55e400`）寫出 delegate index 清單、刪除錯讀 `<vtable>` 的 CParts 釋放特例、空的 on-cursor ＣＧ狀態（推定）。中：原版 `A_REF`（`0x66b790` → `0x6794d0` → `0x67ffb0`）複製陣列，xsystem4 v14 共用頁，`ArrayExtensions::GetReverse<IRectParts&>` 的 `result = source` 就地反轉 `TitleCharacterView.m_uiCharacters`。

| 項目 | 修正前（`ce5c75a`） | 修正後 |
|---|---|---|
| `title-review` TR1：真 bytecode `SceneTutorial@RegisterEvent` | 場景 ref 1→5，擁有者放手後仍活著，項目留在按鈕事件 | ref 不變、場景釋放、項目刪除 |
| TR2：解構子內放掉的物件 | 外層解構子結束前沒有解構 | 立即解構 |
| TR3：`RemoveController`／`ReleaseActivity` 清單 | 0 筆 | delegate index 37、41／53 |
| TR4：真 bytecode `GetReverse<IRectParts&>`／`<ActionTarget&>`／`GetSort<string>` | 來源被改、結果與來源同一頁 | 來源不變 |
| TR5：系統遮擋層／只擋游標的元件 | 點擊落到後方元件 | 遮擋層接收／全畫面點擊 |
| TR6：空的 on-cursor 狀態 | 進入狀態 1（元件消失） | 停在普通狀態 |
| TR7：CParts 頁釋放 | 釋放號碼等於 `<vtable>` slot 的元件 | 不動 |
| 53 模式（預設／GBK） | — | `VERDICT PASS`／`VERDICT PASS` |
| 150 秒 GUI MSG／assertion／峰值 RSS | 88／0／607 MB（`fb28975`） | 88／0／506 MB，對白逐行相同 |
| 標題 9–36.5 秒 | 13 秒起角色被放到對稱位置 | 構圖不變，與 Wine `run2` 相同 |
| 新遊戲後點舊按鈕位置 | 打到舊按鈕 900018（16 次），約 38 秒斷言 | 全畫面點擊，75 秒內 MSG 68 |
| 標題 ↔ 鑑賞模式 | 第一次返回 `PE_AddController` VM_ERROR | 往返兩次後開新遊戲，無錯誤 |
| 據點 | 背景黑、0.975 倍、階段選擇後 VM_ERROR（`fb28975`） | 背景出現、倍率 1.0、進入春銷 |

仍未修：據點環節／春銷環節的橫幅文字在色帶離開後留在畫面上（`fb28975` 的 68 秒影格已如此，之前被教學底圖蓋住）、返回鈕在人材一覽與 YesNo 對話框取消後沒有反應、controller ID 與位置分開、輸入只給作用中的 controller、struct 值陣列與 `X_SET` 的內容複製。詳見 [base-ui.md](research/gui-visual/base-ui.md) §11 與 HANDOFF 第 4 項。

## 第三輪審查：透過元件與解構子黑名單（`80db27d`）

第三輪審查（`6d39915`..`7cb6ba3` 與 `6582e38`）fix-first：高「pactex 不讀 `オン指針透過`，裝飾擋住後方按鈕」、中「解構子黑名單的指令數含巢狀解構」、低「人材 → 返回 → 下一步後 `Executer.jaf:55`」「卡死與斷言路徑沒有記錄」。原版（靜態反組譯）：解析器 `0x553650`（區塊在元件 +0x84）把 `點擊許可`／`オン指針透過`／`鼠標指針ピクセル判定` 以 `== 1` 讀到 +0x1a4／+0x1a5／+0x1ad（與 `SetClickable` `0x58f830`、`SetPassCursor` `0x58f700`、`SetPartsPixelDecide` `0x5903d0` 寫的欄位相同）；輸入更新 `0x546890` 以 `0x545e10` 與判斷式 `0x546e20` 找一個元素，當懸停與按下的目標。黑名單來自 v14 fork（`14c2618`），`vm_call` 指令上限從未啟用，原版沒有。

| 項目 | 修正前（`7cb6ba3`） | 修正後 |
|---|---|---|
| `third-review` RV1：pactex `オン指針透過` 1／0／缺少／2 | 全部讀成 0 | 1、0、0、0 |
| RV2：按鈕前的穿透裝飾／擋游標裝飾 | 點擊變全畫面點擊、按鈕不懸停 | 點擊與懸停到按鈕；擋游標的仍擋住 |
| RV3：可點擊但穿透游標的元件在前 | 前後兩個都懸停 | 只有前者懸停並收到點擊 |
| RV4／RV5：長拆除之後（真 `CBackLogUnit@1`／`CActivityWrap`） | 同型別的解構子被略過、`Release` 不執行 | 照常執行 |
| 54 模式（預設／GBK） | — | `VERDICT PASS`／`VERDICT PASS` |
| 格點比對（ce5c75a 規則對本組） | 標題約 77 點、人材一覽約 35 點、STATUS 各 9 點不同（審查者） | 標題、鑑賞模式、據點、人材一覽、STATUS、環節選擇 0 點；只剩模態背景與 InputDisabler |
| 人材卡 (70,350) | target 0 | 進入 STATUS（原版 Wine `deep/d026`） |
| 標題色帶 (300,340)／(1100,620) | target 0／0 | 鑑賞模式／退出遊戲 |
| 環節卡角色小圖 (350,420) | target 0 | 選到春銷環節 |
| 人材 → 返回 → 下一步 | `CActivityWrap@1` 列入黑名單，約 110.6 秒 `Executer.jaf:55` | 無黑名單、無斷言；Footer 在第二個 `SceneHome` 解構時刪除 |
| 春銷 Start 與關閉春銷教學 | 黑名單、`Executer.jaf:55` 兩次、對元件 0 的 `PE_SetPartsRotateY` 28,720 次 | 都沒有；只剩已知的 `DecisionTimerView.jaf:19` |
| 150 秒 GUI MSG／assertion／峰值 RSS | 88／0／506 MB（`6582e38`） | 88／0／537 MB，對白雜湊不變 |

仍未修：系統選單關閉後、成就返回標題後輸入不再派送，春銷 Start 的 `DecisionTimerView.jaf:19` 斷言（HANDOFF 第 4 項）；`點擊許可` 沒有讀；v14 不送 MouseEnter／Leave（人材卡懸停不變黃）；擋游標的元件收到按下時原版送什麼沒有追到。詳見 [base-ui.md](research/gui-visual/base-ui.md) §12。

## v14 ClipArea（`190c1c8`）

七個裁切 HLL、pactex 的 SJIS／GBK 裁切屬性與共用 parts shader 已接通。原版 SetArea `0x58dc90` 只在矩形改變時自動啟用；繪製 `0x535a27` 依錨點、本地倍率與朝零截斷建立螢幕矩形，`0x579ec0` 取祖先交集。因此未採用最初交接建議的「最近祖先／box 反矩陣」方案。v14 的 XPE v4 保存裁切，舊 v3 可讀、v13 writer 維持 v3。

`clip-area` 四案例修正前全失敗、修正後全通過；兩種組態的 57 模式皆 `VERDICT PASS`。GUI 150.267 秒、MSG 88 與基準逐行相同，assertion 0、overflow 0。已比較 Wine 開場與標題逐幀擦入，也視讀角色正文與據點教學；原版粉金閃光、特殊 ComboBox 的繼承裁切重設與非標準 3D／畫面比例尚未涵蓋。[完整證據與限制](research/gui-visual/clip-area.md)。

## 子畫面返回後的輸入恢復（本組修正 `a870409`）

標題成就／讀取與據點系統選單，會從點擊 callback 同步進入第二層 `WaitForClick`；舊 `EndInput` 用單一 bool 關閉輸入，連仍在等待的外層也停掉。原版 `BeginInput 0x58a720`／`EndInput 0x58a750` 保持巢狀深度，內層結束後恢復外層；`0x546030` 重新採樣目前按鍵，避免按住返回鈕時觸發重複點擊。本組照此清除 session 暫存、保留訊息佇列並補初始化／reset；v13 保留原本行為。

| 驗證 | 結果 |
|---|---|
| 預設／GBK 全部 58 模式 | `final-v3` 兩組 `VERDICT PASS`，sanitizer 0 |
| `input-nesting` 修正前 `a6a8f5b`／修正後 | 7/8 失敗（v13 防回歸案例通過）／8/8 通過 |
| 調整觀察時機的既有 `third-review` | `a6a8f5b` 仍 5/5 PASS，原 hover／state assertions 保留 |
| 正式 GUI | 150.323 秒、MSG 88、assertion 0、overflow 0；峰值 RSS 507,871,232 bytes |
| 成就、讀取返回 | 修正前第一次返回後新點擊不再派送；修正後各往返兩次，再開始新遊戲 |
| 據點系統選單返回 | 修正前返回後不再派送；修正後開關兩次，再進出庫房 |
| Control 快進診斷 | `13,17` 持續按鍵，50 秒 run 約 7 秒到據點教學；MSG 88 雜湊與正常 run 相同，無 assertion／overflow；不取代固定 150 秒回歸 |
| Wine 原版同操作對照 | 成就／讀取／據點選單的重複往返均與修正版相同；轉場與穩定影格已對照，既有外觀差異保留紀錄 |

本組沒有修 `system.Reset`、XPE 的輸入深度持久化、controller ID／輸入限制、MouseLeave／KeyUp 派送。系統選單缺少標籤、左半黑底仍是既有外觀問題。下一組處理春銷 Start 的 `GetHGauge` 斷言；人材狀態頁與 YesNo 的返回不在本組三條 GUI 驗證路徑內，不宣稱一併解決。[完整研究](research/gui-visual/input-nesting.md)。

## 下一批卡點

- **使用者回報的畫面問題**（[調查與進度](research/gui-visual/README.md)）：左側角色翻轉（`4a82758`）、字型缺字與字距（`05d2441`、`9f81bd9`）、立繪與名牌不退場（`9e30c0f`）、翻轉作用在整棵元件樹（`0ab8476`，側審查 D5–D7）、據點畫面的元件（`6d39915`）已修正。場景物件因 delegate 循環不解構的問題已由 `6582e38` 修正，據點背景出現、階段選擇可進入春銷；透過元件擋住按鈕與解構子黑名單由 `80db27d` 修正。系統選單、成就與讀取返回的輸入恢復已由 `a870409` 修正；人材一覽、YesNo 返回仍待重測。之後：春銷 Start 的 GetHGauge 斷言、橫幅文字殘留、controller ID 語義、輸入限作用中 controller、跨 controller 繪製順序、Motion 終值、alpha clipper 繼承、構築部件（據點背景模糊）見 HANDOFF 第 4 項。立繪站位與原版實機截圖逐格對照尚未做；跨側移動與戰鬥翻轉只有 headless 與臨時注入驗證；backlog 等畫面的字距只有 headless 驗證。

- **從讀檔畫面讀一般存檔**：要經過 `system.Reset`，目前是 stub；`SceneLoad@Load` 之後不會重新啟動，也就讀不到 SaveData。`Ａ＿標題界面返回＿確認沒有` 在 Reset 之後的 Peek 迴圈可能卡住（未在執行中驗證）。
- **記憶體**：heap 在 120 秒內長到 1730 萬個 slot。`9e30c0f` 後 150 秒峰值 RSS 約 1.3–1.4 GB，配置器警告消失；`6582e38` 之後場景物件、`Motion::Executer` 與 `CParts` 會釋放，150 秒峰值 RSS 499–541 MB（`fb28975` 607 MB）；STRUCT／DELEGATE／ARRAY 參數多加的參照仍是成長來源候選。

## 尚未處理

- 上述卡點。
- 存讀檔的後續：DeleteSaveFile 在檔案不存在時原版回 true；`init_struct_slot` 把 enum 陣列配成 `AIN_ARRAY`，刪除時會把 enum 值當 slot 釋放（R2）；每次經 `AFL_GameSave_*` 包裝呼叫留下一個帶 TEMP 旗標的 A_REF 暫存字串；EnemyNamePostfixGenerator／FixedRandomValue 建構後刪除會殘留 slot；PE_Save／PE_Load 不保存 `component_state_type`（`6d39915` 前名為 `component_type_from_state`）、按鈕停用、UC data、字型數字設定與 `編輯上表示`，讀檔後成就通知與據點畫面未驗證。
- GBK 字元規則的另案：字型 fallback（名牌等缺字，約 11.4% 的對白）、名牌殘影、舊偵測誤判 SJIS 遊戲、`utf2sjis`／`sjis2utf` 轉碼、`Int.ToCharacter` 與 ReplaceRegex（見 [研究文件](research/gbk-string-rules/README.md)）。
- `Array.First` 的 predicate 版逐實體 slot 走訪；兩槽介面陣列（`Insert` 不保留兩槽）；`Array.Realloc` 縮小不釋放、誤用 `hll_arg3` 當 struct 編號；`Array.Duplicate`（僅編輯器）；`HashMap.Any/Empty/Free`（CN 無法執行到）；PartsEngine `AddChild/InsertChild/RemoveChild/ClearChild`。
- DeletedEvent 23 個殘留 slot；人眼畫面（本環境無單一視窗擷取權限，只驗 framebuffer）；長時間穩定性。

## 重跑

驗證環境在 [harness/](harness/README.md)：一次設定兩棵建置樹，之後一個指令跑全部 58 個模式、對任一舊版本做修正前對照，或做無人值守 GUI 執行。所有輸出寫在 repo 外。

```bash
export XS4_GAME=/path/to/game-workcopy XS4_MASTER_GAME=/path/to/original
bash docs/checkpoints/2026-09-28/harness/setup.sh
bash docs/checkpoints/2026-09-28/harness/verify-step.sh <tag>
```

`array-overload/` 與 `first-overload/` 內的探針原始碼是當時快照，已過時。遊戲資料必須是自行提供的合法副本，勿指向原始安裝目錄。

接手請先讀 [HANDOFF.md](HANDOFF.md)；兩輪調查報告在 [research/](research/)。
