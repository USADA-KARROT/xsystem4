> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 第一階段：Array Numof／Count／Find 重載修正

使用者授權：每一階段完成後查剩餘百分比並停止，等使用者決定是否繼續。這輪只做第一階段，不進行 observer／Join／timer 新核心或遊戲畫面整合。

1. [complete] 固定 baseline、核對原版 count／range 行為與 AIN 宣告。
2. [complete] 依簽名分流 Numof／Count／Find，實作 predicate 計數與範圍搜尋，保留已知槽位與巢狀 HLL 上下文。
3. [complete] 實際宣告／FFI 邊界回歸、sanitizer 與整引擎建置、獨立審查。
4. [complete] 輸出本階段 patch／證據／繁中結果；查剩餘額度並停止。第二階段未開始，等使用者指示。

起始週額度：使用 63%，剩餘 37%。修改對象為本任務隔離副本 work/stage2/source；原始本機專案、遊戲、存檔和 GitHub 保留。兩個待修檔案已保存 baseline。

## 已知限制
這輪不宣稱解決上次遊戲卡住；不重寫全部 bridge／VM。所有語義採本機 AIN、既有原版 dump 與現有程式交叉核對。
