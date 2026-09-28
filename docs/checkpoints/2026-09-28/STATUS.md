# 2026-09-28 新遊戲人物 ID assertion 修正

**最新狀態：新遊戲不再停在 `Personality.jaf:27 assert(id != "")`。以 `--skip-title` 直入新遊戲，60–75 秒實機執行皆無 assertion，出現開場旁白 MSG，呼叫堆疊到 `DohnaDohna@RunTurnStart`。尚非穩定可玩版：本輪未做人眼畫面驗證，也未測存讀檔或長時間遊玩。**

接續 [2026-09-26 交接](../2026-09-26/STATUS.md)（`22e9496`）。引擎修正為 `ff6fc2f`，只動 `src/ffi.c` 與 `src/hll/Array.c`，26 行新增。libsys4 仍固定於 `8c93946`。

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

## 尚未處理

- **同型綁定缺陷。** Array 庫另有多個名稱的同名 overload 綁到單一 C 實作：`Max` 單參數版與 First 同型；`Last`、`Min`、`Any`、`Unique` 的 predicate 版忽略 predicate；`LowerBound`、`BinarySearch`、`FindLast` 的 predicate 版把函式編號當搜尋值；`Fill`、`Copy` 部分宣告的參數個數或順序與 C 實作不符。依 CN AIN 靜態統計約 140 處呼叫走錯實作。`Array_AnyPredicate` 已寫好但從未被綁定。下一步是逐一確認語義並改為宣告驅動分流。
- DeletedEvent 23 個殘留 slot 不變。
- 人眼畫面、第一回合之後的流程、存讀檔、長時間穩定性皆未驗證。

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
