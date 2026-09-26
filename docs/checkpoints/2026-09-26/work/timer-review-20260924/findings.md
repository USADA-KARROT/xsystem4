> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# CASTimer 單一殘留診斷（2026-09-24）

已獨立重現原首輪 `ctor=3 / dtor=3 / live=6 / baseline=5`。唯一多出的 heap slot 是 **CASTimerImp（struct 8），slot 18／seq 19，ref=3**，四欄值為 `[1015,0,5,5]`。它不是已釋放 timer wrapper，也不是合法的空陣列 owner。原 trace 和動態 probe 均在本目錄；未改 production source 或原 fixture。

本實驗使用實際 CN AIN、production VM 與實際 engine objects／libffi。fixture 清掉 `FUNC_FLAG_CASTIMER_MGR`、`FUNC_FLAG_CASTIMER_INST`，以執行原腳本；**不是現行遊戲 native timer interceptor 的驗證**。時鐘接可控 `system.GetTime`。AIN SHA-256、source／binary hashes 與完整建置命令見 `findings.json`、`build-optimized.json`。

## 第一個問題：Get 的額外引用

`timer.err` 的數值變化按「前一個 opcode 執行後」列印；下列事件均發生在連續 VM 執行中，並非 host 呼叫邊界歸因。`timer-residual-trace.txt` 保存帶原檔行號的摘錄。

| 原 bytecode／生命週期 | slot 18 引用變化 | 意義 |
|---|---:|---|
| f440 CALLHLL EmplaceBack `0x1c1bc` | 0→2 | array owner + 回傳 owner |
| f440 X_ASSIGN `0x1c1ec` | 2→3 | 現行 X_ASSIGN 再 retain |
| f440 DELETE `0x1c212` | 3→2 | 顯式 temporary cleanup |
| f440 X_ASSIGN(-1) `0x1c21a` | 2→1 | 現行 X_ASSIGN 再 release，建構暫時平衡 |
| f442 SP_INC `0x1c392` | 1→2 | GetObject 明確回傳 owned reference |
| f424 X_ASSIGN `0x1b732` | 2→3 | 把 return reference 放入 local，又 retain 一次 |
| f424 RETURN `0x1b764` | 3→2 | typed local cleanup 只 release 一次；Get 淨 +1 |
| 第二次 Get 同一組路徑 | 2→3→4→3 | 再淨 +1 |
| f423 Reset 的 SP_INC／X_ASSIGN／DELETE／X_ASSIGN(-1) | 3→4→5→4→3 | Reset 因顯式 cleanup 而淨 0 |
| 第三次 Get 同一組路徑 | 3→4→5→4 | 再淨 +1 |
| f439 CALLHLL PopBack `0x1c104` | 4→3 | GC 釋放最後 array owner，三個多餘引用留下 |

production `src/vm.c:4667` 的 X_ASSIGN 在 typed destination 為 ref-counted type 時，自動 `heap_ref(new_slot)`（4725）且 `heap_unref(old_slot)`（4727）。這与原腳本顯式 SP_INC／DELETE 管理重複。不能以補三次 unref、修改 Get 或省略 heap baseline 檢查來處理，因為同類 opcode 的其他路徑也受影響。

## 私有 counterfactual 證據

依主代理追加授權，`build_probe.py --xassign-transfer` 只在私有 `instrumented-vm.inc` 的 X_ASSIGN 中，讓 **AIN v14 跳過自動 retain/release**；原 bytecode 的 SP_INC、DELETE、typed local cleanup 完全保留。production source 和 engine objects 不改。基準 instrumented 檔另外保存為 `instrumented-vm-baseline.inc`。

optimized counterfactual 首輪得到 **ctor3／dtor3／live5／baseline5**，時鐘 7／10、洞重用、Reset 後 5、GC 全過。這直接支持 X_ASSIGN 的 raw transfer 修正，而不只是靜態推論。結果見 `timer-transfer.out` 和 `build-optimized-transfer.json`。

但要求的 20 輪沒有全過：第二輪第一個 Get 遇到另一個型別生命週期問題，預期 7 的 assert 失敗，exit 134。此私有 counterfactual 未另跑 ASan；主代理可於正式局部修正後統一跑 optimized／ASan。

## 第二個問題：清空後 EmplaceBack 丟失元素型別

`Array.c:494` 的 `Array_PopBack` 在最後元素被移走時，`free_page(a); *array=NULL; return;`（508 起）。NULL 是合法 empty 狀態，**不是洩漏或錯誤本身**，但此 HLL 目前只把 a_type、struct id、rank 存在 page 中，這條路徑會一併抹掉後續 EmplaceBack 所需的 metadata。

第二輪 f440 的 CALLHLL `0x1c1bc` 後，原 timerImpList owner slot 8 變成 **a_type=14（AIN_ARRAY_INT），不再是 ARRAY_STRUCT17／struct8**。`Array_EmplaceBack`（1760 起）對 NULL page 固定用 ARRAY_INT，無法從缺失 page 得到 struct8；GetObject 返回的值也不再是原 imp。隨後 method fallback 另配 struct8，與 array 元素脫離，第一個 Get 的 7ms 斷言失敗。`timer-transfer.err` 保存此連續 trace。

**最小修正建議：** PopBack 的 size0 路徑保留 typed zero-length page。其後既有一般縮小路徑已 `alloc_page(ARRAY_PAGE, a->a_type, new_size)` 並 `new_a->array = a->array`；移除 NULL 特殊分支即可讓 size0 一樣保留 metadata，無需在 EmplaceBack 依 arg3 猜 struct8。應驗證兩個 manager arrays 清空後的 a_type、rank、struct id，再跑 20 輪及 manager teardown live0。

以上只涵蓋 timer 所用單槽陣列與 script 路徑；未宣稱全部 v14 assignment、全部 Clear／Free 路徑、多槽 array 或 GUI 已修正。observer 的平行原 AIN fixture 可作另一組獨立 ownership 驗證。
