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

## 2026-09-25 使用者明確授權桌面操作後重試

- 已重試測試 App 與 Finder 讀取；兩者均被 Computer Use 核准拒絕，無詳細原因。
- 沒有成功看見桌面、啟動遊戲或取得 FPS，無引擎變動。
- 使用者無須自行做遊戲操作；目前缺少桌面工具實際放行。

## 2026-09-26 實機重試

- 桌面工具可用；直接工作區 runner 啟動 optimized／ASan，均實際看到警告頁→Enter到標題→點新遊戲轉場。
- 兩輪在 Personality.jaf:27 空ID斷言後由runner停止，並有大量string doublefree warning及free-list異常；未進ADV，驗收失敗。
- 個別run的analysis.md/summary.json已完成；原始source diff hash保持不變，production未改。
- ASan title畫面已保存，兩個child已結束；修正runner error_log+exit0不能回報成功的漏洞。
- 起始週餘額42%，交付前38%；未重置額度，停在本輪驗證，不開始下一階段。
