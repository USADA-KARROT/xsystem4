> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# ASan GUI 短測：未通過（2026-09-26）

run `retry-asan-20260926` 執行 **33.587 秒**，runner記錄 `stop_reason=error_log`、exit0。與optimized使用相同source diff `b77b872a3860ccd5f9921b412f64b870b1d7b37ea5189193793300b3621190c0`，但不同ASan binary。仍重現string重複釋放、Personality腳本斷言及free-list異常，**不是因exit0而通過**。

- PERF：6個完整視窗，覆蓋30.006秒，11033次present；加權367.689/秒，max interval 1296.697ms，max swap 8.543ms。這是instrumented build的呈現數據，不與optimized數值直接推論速度差距。
- 每窗p95間隔範圍4.412–15.776ms，不是全場p95；超過50ms的視窗1個，超過1秒的視窗1個，不是精確長幀次數。
- `--echo-message`已開，MSG **0筆**。這份log不支持已推進ADV。
- string double-free警告 **4650行**；free-list exhausted/corrupt **1行**；`Personality.jaf:27: assert(id != "")`為同一斷言的兩行輸出。X_ASSIGN越界clamp 3行，WAV -1失敗 7行。
- 日志中找到的ASan／UBSan error或summary行為 **0**。這只表示此run未吐出那些sanitizer診斷；字串防護已在libsys4自身偵測並忽略重複free，不能把無ASan報錯當成無所有權錯誤。`detect_leaks=0`也未做LeakSanitizer驗收。
- runner回報child峰值RSS **1110196224 bytes（1058.766MiB）**。包括ASan開銷，非live AIN heap，也非RSS時間曲線。

最後PERF報告在SDL tick 31109ms；不包含未完成尾窗或之後沒有下一次present的停頓。沒有bounded callback trace，因此本run不能量出哪個回呼造成停頓。日志支持與optimized相同的可重現失敗形態，尚不足以把string double-free、空id、free-list異常三者的因果順序定論。

本分析只讀 `engine.log`、`run.json`；詳細數字見 `summary.json`。沒有GUI操作、process控制或production修改。binary SHA `912f1e62315d419c2835a873b52b410c99a983061d99062459d7fa1b7f04ddcd`。
