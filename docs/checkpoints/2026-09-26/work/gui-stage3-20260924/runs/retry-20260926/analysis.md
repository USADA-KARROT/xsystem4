> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# optimized GUI 短測：未通過（2026-09-26）

run `retry-20260926` 執行 **35.577 秒**，runner記錄 `stop_reason=error_log`、exit0。exit0不能視為功能通過：記錄有大量重複釋放、腳本斷言及free-list异常。這份分析只讀log／metadata，沒有操作GUI或程序。

| 項目 | 已量測結果 |
|---|---|
| PERF覆蓋 | 6 個完整視窗，共 30.008 秒、15989次present |
| 加權present頻率 | 532.824/秒；vsync=0，包含可能重複的畫面 |
| 長間隔 | max 82.017ms；1個視窗至少含一次>50ms，沒有已報告>100ms視窗 |
| 每窗p95範圍 | 4.134–15.964ms；不是全場p95 |
| Swap成本 | max 11.922ms、加權平均 0.531ms |
| ADV訊息 | `--echo-message`已開，但 **MSG 0筆**；不支持已推進ADV |
| RSS | runner回報整個child峰值 655507456 bytes（625.141MiB）；不是RSS曲線或live AIN heap |

警告與失敗證據：

- `Double free of string object (ignored)` **10627行**，第一筆在engine.log第28行，最後在10733行；不是可忽略的清潔運行。
- 第10728–10729行是同一項 `Personality.jaf:27: assert(id != "")` 斷言的兩行輸出，不能計成兩個獨立失敗。
- 第10730行出現跳過in-use free-list entry，接著第10731行 `free list exhausted/corrupt`（slot12302350、capacity2097152）；可確認allocator當時發現異常，單靠log不能證明起因。
- 另有2行X_ASSIGN越界clamp、7行WAV -1載入失敗，以及 `SaveData/User/` 不存在。沒有 `system.Error` 或 `VM_CALL_TIMEOUT` 日志；這不抵銷上述失敗。

最後PERF只到SDL tick **30624ms**，不足5秒的尾窗與最後沒有下一次present的停頓不會flush；不要用其max82ms排除退出前尾段卡頓，也不要把SDL tick與runner elapsed直接相減當精確未報告時長。6個視窗報告當下均shown／input-focus且未minimized，不能代表window內每一幀狀態。

主代理回報的目視過程為「警告頁→Return到標題→點新遊戲轉場→退出」；這是主代理UI觀察，不是本分析從FPS推導。**這輪的主要可驗證結果是GUI能啟動並呈現，但進度與記憶體正確性未通過；高present頻率不是通過依據。**

完整機讀數據見 `summary.json`；原始來源為本目錄 `run.json`、`engine.log`。source diff SHA `b77b872a3860ccd5f9921b412f64b870b1d7b37ea5189193793300b3621190c0`，binary SHA `13aa27858821a4bfe75004ec2e8572a462898cb508c2b1bac0dd109dd9137986`。optimized binary即使帶ASAN_OPTIONS環境也不是ASan建置，不能把沒有ASan診斷當作記憶體安全證據。
