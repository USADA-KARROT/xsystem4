> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 第四階段：新遊戲字串／ID 錯誤

1. 已完成：保存 baseline，捕捉第一筆字串異常與更早的錯型 page write。
2. 已完成有界定位：GUI 人物 ctor 收到 -1，caller 是 WorkerCreator 的 Take<string> 結果第0項；來源根因未解。
3. 已完成一項修正：delegate_copy_argument 保留 AIN_WRAP 引用；原始事件 bytecode 功能檢查通過、清理仍 live23。六項其他回歸通過。GUI 新遊戲仍失敗。
4. 已完成階段封存：移除臨時診斷、optimized/ASan build、正式候選 GUI、報告與證據。

階段已結束，但可玩性目標未完成。依使用者要求，回報額度後停止，下一階段待使用者決定。
起始週餘額38%；最近讀值17%；未兌換 reset。本階段未推送 Git。
