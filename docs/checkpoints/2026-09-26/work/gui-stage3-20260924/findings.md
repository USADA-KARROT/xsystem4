> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 已知與待驗證

- 舊 stage2 App 內執行檔是 9 月 9 日版本；本輪建立新 App。
- 正常 GUI 仍使用 native CASTimer 截取；第二階段 harness 執行原始 AIN 方法。
- PERF 僅在成功 present 後記錄；最後一幀之後的永久卡住不會 flush。
- STOP_ON_GAME_ERROR 舊環境變數無 consumer；runner 必須自行辨識錯誤。
- shell ps 在目前 sandbox 不可用；僅取 runner 自身 child 結束的 peak RSS，不聲稱即時記憶體曲線。

## 本輪實際結果

- baseline-native 使用新的 optimized binary，source diff SHA256 b77b872a3860ccd5f9921b412f64b870b1d7b37ea5189193793300b3621190c0。
- 0.621 秒退出（exit 134），C backtrace 位於 HIServices/_RegisterApplication → AppKit → SDL 初始化 → gfx_init，未進遊戲 VM 啟動。
- PERF 0 windows、MSG 0：本輪 FPS 未量到，不能記為 0 FPS，也不能用此 run 判斷遊戲是否改善。
- peak RSS 207421440 bytes 是啟動失敗 child 的值，非遊戲正常執行記憶體。
- desktop tool get_app_state 嘗試正常啟動測試 App，回覆「Computer Use was not approved to use Xsystem4-stage3-test-launcher」。沒有提供更細拒絕原因，沒有再試其他 UI 或繞過此拒絕。
- 已交付 outputs/第三階段-90秒畫面測試.command，供使用者直接測試；每次 fresh save/home、時限與 error stop、同一次 child 的 wait4 peak RSS、當前 source snapshot。
- native-audit 已實測 production helper：初始 Rate=1.40129846e-45；setter2.5 後讀取2.5；有效新 handle Check=false；Release 後 active=true。非 GUI/method dispatch 整合驗收。
