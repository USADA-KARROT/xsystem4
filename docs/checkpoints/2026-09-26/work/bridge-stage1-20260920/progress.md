> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 進度

2026-09-20：開始第一階段；起始週額度剩 37%。讀取既有研究與 checkpoint，保存 ffi.c／Array.c baseline；原版語義查證與本機實作並行。完成後按使用者要求停止。

完成：Numof／Count／Find 8 重載修正、callback shape／owner guard、明確 value 型別判斷。91 個實際 predicate 呼叫宣告盤點，補上 wrapped interface。normal/ASanUBSan 各 928 checks，12 預期錯誤；full production FFI+CIF 各 22 callback→nestedHLL，self Free 預期拒絕，無 sanitizer 報錯。完整 O2／ASan 引擎編譯成功、source 身份一致。獨立重審在 CN v14 8 個宣告範圍內無剩餘 blocker。

交付 outputs/第一階段-Array介面修正-2026-09-20.md、增量 patch 及證據包。Patch 正向套用可重現當前兩檔、反向 check 通過。沒有啟動遊戲、沒有更新 App bundle、沒有推送 GitHub。測試 callback body 是替身，真實 VM 的 observer/Join/timer 保留第二階段。完成後停止等待使用者，不能自行接續下一階段。

交付前即時額度：used 76%，remaining 24%；本階段開始 remaining 37%，期間下降 13 個百分點，屬帳號共用用量，非精確本任務 token。未使用 reset。
