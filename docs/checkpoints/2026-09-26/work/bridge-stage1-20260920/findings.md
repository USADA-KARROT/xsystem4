> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 發現

- 現有 source 共有先前的 18 檔修改，保留；baseline 為 484f4bc 加 9/9 checkpoint 的工作內容。
- 原版 jump table：Numof #20 與 Count #22 同一入口；predicate #21／#23 同一入口。range Find 採 begin/end exclusive，且先 clamp begin>=0/end<=length；首命中回原陣列 logical index。
- 原 runtime table 已以 libno/fno 區分，這轮修的是 C function pointer 選擇與缺少 callback／range 行為。

- 第一版經獨立審查抓到 callback 可能釋放 self 快照、ref primitive 槽形態與 value comparator 過度寬鬆，已在本輪修正並加入拒絕路徑測試。型別已被抹除的 generic page 不能可靠推回 float/int，明確報錯。
- Numof／Count predicate 共用實作；Find begin/end exclusive 正確；callback storage mutation 明確拒絕而非模擬未證實語義。legacy typed ref-array/HLL_FUNC_71 未取得完整整合證據。函式本體未執行真實 AIN，不可宣稱生命週期核心已通過。
