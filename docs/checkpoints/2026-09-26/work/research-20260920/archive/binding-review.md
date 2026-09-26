> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# Array 重載綁定的獨立靜態審查 — 2026-09-20

**確有值得先處理的 bridge 缺口：本輪抽查 Numof、Count、Find 三組，各自不同簽名綁到同一個未完整分流的 C 實作。** 這是具體實作風險，不等於 37 組同名 API 全有錯，也尚未證明它就是點擊／等待／換頁故障的根因。沒有修碼或執行遊戲。

資料宣告和靜態 call-site 數取自本輪原始 AIN 探針 `probes/cn-surface.json`；下列索引均屬 **Array library index 4**。call-site 是靜態存在的 CALLHLL 位置數，不代表某場景執行次數或可達性。原始碼路徑基底為 `work/stage2/source/src/`。

## 實際綁定機制

- `ffi.c:1138–1159` 逐一處理 AIN function index，以 `strcmp(name)` 尋找第一個 static export，然後 `break`。因此 runtime table 本身仍以 `libno/fno` 區分；**遺失重載區分的是選 C function pointer 的步驟**，不是整個 runtime 不認 index。
- `ffi.c:1106–1132` 依各 AIN 宣告建立獨立的 libffi CIF，含參數數量、類型及回傳類型；但不核對該宣告是否真的符合被選到的 C 實作。正確的 AIN CIF 不會自動補出 C 函式沒實作的 callback 語意。
- 此段已有 `Array.Erase`、`Array.IsExist` 的專用 signature selector，見 `ffi.c:1147–1153`。因此「整個 bridge 永遠只看名字、完全沒有重載處理」是不正確的說法。

## 三組抽樣

| 重載 | AIN index／參數形態／靜態位置 | 綁到的實作 | 可直接由程式碼確認的問題 |
|---|---|---|---|
| Numof | #20 `(array)`：519；#21 `(array,HLL_FUNC)`：7 | `Array_Numof(struct page **self)`；`Array.c:401–415`，export :2193 | #21 的 callback 無對應 C 參數，函式內沒有執行 predicate。回傳值只由陣列大小和元素 stride 決定。 |
| Count | #22 `(array)`：128；#23 `(array,HLL_FUNC)`：12 | `Array_Count(struct page **self)`；`Array.c:1800–1805`，export :2221 | #23 的 callback 同樣不被使用；函式直接回 `nr_vars`。註解提到 predicate，實際函式沒有處理它。 |
| Find | #42 `(array,HLL_PARAM)`：13；#44 `(array,int,int,HLL_PARAM)`：12；#46 `(array,HLL_FUNC)`：72；#48 `(array,int,int,HLL_FUNC)`：0 | `Array_Find(struct page **array,int value)`；`Array.c:1861–1874`，export :2212 | 只從索引 0 起逐一比較 `src->values[i].i == value`。#46 的 function number 被當數值比對，沒有呼叫 predicate；#44 的第二參數整數落入 C 的 `value`，後兩參數不被讀取。#48 有相同結構風險，但探針未見靜態 CALLHLL 位置。 |

Numof/Count 的 predicate 重載不應僅因「多傳參數」就宣稱必然崩潰：本輪能確定的是被選中的實作無法執行 callback。Find 的四參數版則還有明確的參數意義錯位；兩個 int 各自代表的精確區間契約仍應由 AIN caller／原版行為確認，無須先假定便可看出真實待比較值或 callback 沒有進入 C 的比較邏輯。

`ffi.c:618–634` 將 `AIN_HLL_FUNC` 的兩個 VM slots 拆為 closure/object 與 function number，保留前者到 `hll_func_obj`，把後者交給 C。因此 #46 的問題不是「完全沒有 callback 資料」，而是 `Array_Find` 收到數字後採用數值搜尋，沒有呼叫 VM callback。

## 已有類型分流，應保留與驗證

- `Array_Numof` 會依 `array_elem_is_2slot()` 修正邏輯元素數；helper 在 `Array.c:57–63` 使用 CALLHLL 的 `arg3` 低位判斷 interface/option 等類型。這處已防止把 struct id 誤當 stride，不能說 Numof 所有重載／所有元素類型都不支援。
- `ffi.c:393–406`、`:783–786` 保存／還原 `arg3`、array self slot、callback object、generic 第二 slot 的上下文，可支援 HLL→VM→HLL 巢狀呼叫。
- `ffi.c:654–671` 依 `arg3` 處理 `AIN_HLL_PARAM` 的一／兩 slot 參數；`:1106–1132` 對部分 wrap 依內層類型選 pointer 或 sint32。這些是真實已存在的 marshal 邏輯。
- **上述元素類型／slot 分流不能替代 overload 分流**：本輪三個函式沒有根據參數個數或 predicate 宣告另選實作；Numof 的 stride 修正也不會替 Count/Find 執行 predicate。

## 對新原生核心的意義

優先建立可核對簽名的 HLL bridge 是有根據的：讓 `(AIN hash, library index, function index, normalized signature)` 決定呼叫處理器，保留 `arg3` 作 generic 元素上下文，而不是把所有差異塞回同名 C 函式。啟動時應驗證處理器宣告、參數形狀、回傳形狀及「尚未實作」狀態。

這個結果**支持先建立 bridge 的契約與最小測試，並不要求先重寫整個 VM**。現有 Erase/IsExist 的 signature selector 也證明可先用局部修正驗證重載問題，再評估是否需要全新核心。

最小驗證應覆蓋：同一陣列的無 predicate 與 false/部分匹配 predicate 計數、Find 的值版與 callback 版、區間版不得搜尋區間外，以及 callback 巢狀呼叫後 `arg3/closure` 上下文恢復。這是後續建議，本輪沒有宣稱已完成這些測試。

## 本輪來源快照

| 檔案 | SHA-256 |
|---|---|
| `work/stage2/source/src/ffi.c` | `4416e2ff2ea0900b96deea1c8a916061e021d2dd3c7a6d2a5d94565bad6ec8da` |
| `work/stage2/source/src/hll/Array.c` | `9d58815317c646ea06d2aef6415318d72f77dbd979a91af3c9011c217c966d5d` |
| `work/research-20260920/probes/cn-surface.json` | `5eac39b555d17ee4ead5c6ff189207dc0887ed9d4c63cd1cfd007e945bcc3e68` |
| `work/research-20260920/probes/overload-surface.json` | `062a8db26053a5afb11dc9dc208e3120c3fab019429b0a16389b7c6f59581f6c` |
