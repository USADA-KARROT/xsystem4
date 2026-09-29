# 2026-09-28 新遊戲人物 ID assertion 修正

**最新狀態：存讀檔持久化已接上（`173ff1d`，審查後修正 `6b65b12`、`ff77c18`）：AFL_GameSave_StructSave／StructLoad 以原版 v9 格式寫出並就地讀回，存檔註解可讀寫，原版引擎的 AFConfig／Collection／AFCommon／AFInfo 讀入後重存逐位元組相同。兩次 150 秒 GUI（新存檔、重用存檔）皆 MSG 88、assertion 0、堆疊溢位 0，第二次確認設定與 Collection 的讀回狀態；41 個 headless 模式通過。從讀檔畫面讀一般存檔仍需 `system.Reset`（stub），記憶體成長與長時間穩定性未解決，尚非穩定可玩版。**

接續 [2026-09-26 交接](../2026-09-26/STATUS.md)（`22e9496`）。本 checkpoint 含三批引擎修正：人物 ID 的 `ff6fc2f`；Array overload 的 `3386e7d`..`763f5bd`；以及第二批 `4c7b820`..`6ec6258`（子元件查詢、Math、Sort、String、檔案與版面原型）。libsys4 仍固定於 `8c93946`。

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

## 下一批卡點

- **從讀檔畫面讀一般存檔**：要經過 `system.Reset`，目前是 stub；`SceneLoad@Load` 之後不會重新啟動，也就讀不到 SaveData。`Ａ＿標題界面返回＿確認沒有` 在 Reset 之後的 Peek 迴圈可能卡住（未在執行中驗證）。
- **記憶體**：heap 在 120 秒內長到 1730 萬個 slot，峰值 RSS 約 1.9 GB；配置器偶有「free list 耗盡或損壞」警告。兩版數字相同，屬既有問題。

## 尚未處理

- 上述卡點。
- 存讀檔的後續：DeleteSaveFile 在檔案不存在時原版回 true；`init_struct_slot` 把 enum 陣列配成 `AIN_ARRAY`，刪除時會把 enum 值當 slot 釋放（R2）；每次經 `AFL_GameSave_*` 包裝呼叫留下一個帶 TEMP 旗標的 A_REF 暫存字串；EnemyNamePostfixGenerator／FixedRandomValue 建構後刪除會殘留 slot；PE_Save／PE_Load 不保存 `component_type_from_state`，讀檔後成就通知未驗證。
- String 庫的字元規則：CN 原版全面用 GBK 首位元組 0x81..0xFE，xsystem4 的 `Length`、`Find`、`GetPart` 等用 SJIS 規則，中文會被切錯。要一次改齊（Length 110 處、GetPart 76 處），並涉及 libsys4。
- `Array.First` 的 predicate 版逐實體 slot 走訪；兩槽介面陣列（`Insert` 不保留兩槽）；`Array.Realloc` 縮小不釋放、誤用 `hll_arg3` 當 struct 編號；`Array.Duplicate`（僅編輯器）；`HashMap.Any/Empty/Free`（CN 無法執行到）；PartsEngine `AddChild/InsertChild/RemoveChild/ClearChild`。
- DeletedEvent 23 個殘留 slot；人眼畫面（本環境無單一視窗擷取權限，只驗 framebuffer）；長時間穩定性。

## 重跑

驗證環境在 [harness/](harness/README.md)：一次設定兩棵建置樹，之後一個指令跑全部 41 個模式、對任一舊版本做修正前對照，或做無人值守 GUI 執行。所有輸出寫在 repo 外。

```bash
export XS4_GAME=/path/to/game-workcopy XS4_MASTER_GAME=/path/to/original
bash docs/checkpoints/2026-09-28/harness/setup.sh
bash docs/checkpoints/2026-09-28/harness/verify-step.sh <tag>
```

`array-overload/` 與 `first-overload/` 內的探針原始碼是當時快照，已過時。遊戲資料必須是自行提供的合法副本，勿指向原始安裝目錄。

接手請先讀 [HANDOFF.md](HANDOFF.md)；兩輪調查報告在 [research/](research/)。
