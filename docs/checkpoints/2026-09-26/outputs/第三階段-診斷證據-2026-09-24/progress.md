> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 進度

- 本階段起始週用量 37%，剩 63%。
- 已完成啟動路徑與 PERF 只讀審查，準備 fresh binary GUI baseline。
- 建立新 App，baseline 於 AppKit 註冊階段退出；無 GUI/FPS 結果。
- 使用正式 Computer Use 啟動也遭拒絕，停止 GUI 嘗試。
- 準備 90 秒手動入口與只讀摘要工具；補做 native timer 的有限 headless 診斷。
- 本輪不改引擎，不把第二階段的通過結果外推至 GUI。
- native-audit 完成，診斷重現三個 native helper 既有問題；詳見 README/result.json。
- 最新帳戶週用量 48%，剩約 52%，未使用重置額度。本階段 GUI 驗證未完成；交付報告與手動測試入口後停下。
