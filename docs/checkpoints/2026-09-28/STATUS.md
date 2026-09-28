# 2026-09-28 新遊戲人物 ID assertion 修正

**最新狀態：新遊戲不再停在 `Personality.jaf:27 assert(id != "")`，開場旁白可逐句點擊推進。Array 庫所有同名 overload 已改為依宣告選 C 實作。下一個卡點是點擊按鈕後 activity 事件分派無限遞迴（見文末）。尚非穩定可玩版，未測存讀檔或長時間遊玩。**

接續 [2026-09-26 交接](../2026-09-26/STATUS.md)（`22e9496`）。本 checkpoint 含兩批引擎修正：人物 ID 的 `ff6fc2f`，以及 Array overload 的 `3386e7d`..`763f5bd`（6 個 commit）。libsys4 仍固定於 `8c93946`。

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

## 下一個卡點：activity 事件分派無限遞迴

兩個版本在自動點擊後都出現 `_function_call: call stack overflow` 三萬餘次，約在 1.3 億指令時開始。呼叫鏈為 `CButtonParts@0 → CParts@Attach → DeletedEvent::add → FuncSet::get → CPartsMessageManager@GetFunctionSet`，底部堆疊塞滿 `activity::detail::CallUserComponentEventWithChild`。日誌因此各長到約 9–10 GB（已截為頭尾各 5 MB 保存於本機）。

初步線索：`PartsEngine.GetUserComponentName`、`GetChild`、`AddChild`、`IsExistChild` 等子元件 API 未實作，走 UNIMPL 一律回 0；其呼叫點包含 activity 事件分派（fno 679）。尚未驗證。

## 尚未處理

- 上述遞迴卡點。
- Array 以外的同型缺陷（全庫掃描）：`Math.Abs/Min/Max/Clamp` 的 float 版與多參數版綁到 int 版（122 處，例如 `Clamp(5.0, 0, 1)` 回 5）；`String.GetPart` 單參數版讀殘值（16 處）；`String.Match` 恆回 true（6 處）；`HashMap.Any` 單參數版；`Array.Sort`/`QuickSort` 忽略比較函式，且 C 端回 void 而宣告回 `wrap<?>`，殘值會被 DELETE。另有 `Array.Duplicate`、`TextFile.Read*` 的 C 原型與宣告不符而崩潰。語義調查進行中。
- `Array.First` 的 predicate 版逐實體 slot 走訪，兩槽介面陣列可能取錯；`Array.Realloc` 縮小時不釋放 ref 元素、在 page 無 struct_type 時誤用 `hll_arg3` 當 struct 編號。兩者都在運作中的路徑上，未改。
- DeletedEvent 23 個殘留 slot；人眼畫面（本環境無單一視窗擷取權限，只驗 framebuffer）；存讀檔；長時間穩定性。

## 重跑

Headless：照 [9/26 REPRODUCE](../2026-09-26/REPRODUCE.md) 建立隔離布局後，用本目錄的 `first-overload/runtime_probe.c` 取代 `work/string-stage4-20260926/probe/runtime_probe.c`，並把 `first_overload_fixture.inc` 放到同一目錄，再執行 `build_probe.py asan`：

```bash
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$XSYS4_REPRO_ROOT/work/string-stage4-20260926/probe/runtime-probe-asan" "/path/to/dohnadohna.ain" first-overload
```

修正前應 exit 88，修正後 exit 0。

GUI：`tooling/run-gui-bounded.py` 以 repo 根目錄為 cwd（讀 fonts／shaders），遇 assertion、ASan 或 VM error 字樣即停止：

```bash
RUN_HOLD_KEYS=13 python3 docs/checkpoints/2026-09-28/tooling/run-gui-bounded.py build/src/xsystem4 "$PWD" /path/to/game-copy /path/to/new-run-dir 60 --skip-title
```

`--skip-title` 會略過 SceneLogo 與 SceneTitle，直接走新遊戲；按住 Return 用來通過注意事項頁。遊戲資料必須是自行提供的合法副本，勿指向原始安裝目錄。
