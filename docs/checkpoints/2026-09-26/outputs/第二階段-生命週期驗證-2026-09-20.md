> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 第二階段：真實腳本與生命週期驗證（部分完成）

2026-09-20。**本階段尚未全數通過，不能據此宣稱遊戲已不卡、可玩或可安全整合。** 修改保存在本任務隔離的 work/stage2/source；這次未推送 GitHub，未啟動遊戲視窗。

## 這次確實做到了什麼

從第一階段的受控回呼測試，提升為讀取原始 CN AIN、執行實際 production VM、FFI、heap/page 及腳本函式。沒有改寫 AIN bytecode；只加測試計數、控制時鐘並種入最小資料狀態。

| 項目 | 修正與實測結果 | 驗收狀態 |
|---|---|---|
| 陣列初始化 | X_A_INIT 保留原宣告的 bool/int、float、string、struct 元素型別與 struct id；移除多餘引用。原 timer manager 建構的四個陣列型別正確、ref=1，釋放後 live=0。實測覆蓋 bool/struct，其他映射尚未逐一動態覆蓋。 | 本案例通過 |
| observer／Join 回呼 | 執行原 f20752 與 f36081；每輪 false→true、寫回 caller 的 End 與 captured isFinish、第三次不再呼叫，連續20輪均成立。每輪292次caller存活檢查、16次setter檢查。 | 行為通過；清理仍失敗 |
| delegate 引用管理 | 建立／複製／追加／移除／清空／刪除對稱管理 obj/env 引用，追加保留env；callback frame持有引用並在退出時釋放。新增的create/copy/append/plusa/erase/clear/delete、obj==env及pre14弱引用 smoke均通過。 | 局部通過 |
| timer | 修正 EmplaceBack 的 primitive wrap 回傳為可寫入的兩槽位置與所有權，At/Last補相應引用；移除 CASTimer 預設析構黑名單。原腳本成功計時7/10ms、Reset後5ms、重用handle洞、三個物件各析構一次並清空兩個陣列。 | 第一輪行為通過；清理仍失敗 |

## 尚未通過的兩個主要項目

1. **observer 每輪仍殘留4個活躍heap slots，20輪累積80。** 修正前每輪殘留9個；修正後 collection、capture environment、motion 根物件均ref=0。剩餘是SectionParam(struct617)、string及兩個空delegate，指向一般初始化／assignment所有權仍需追查。不可把它們手工釋放後冒充引擎清理通過。
2. **timer 第一輪結束後 live=6，初始化基準=5，仍多1個slot。** 三次ctor/dtor、時間與handle重用已過，但測試明確以exit77停止。因此原計畫20輪／60次生命週期並未完成，不得把測試程式中的預定次數當成實際結果。

建議下一次仍續做第二階段：先精確定位上述殘留物件的建立及最後持有者，再修一般初始化／assignment的配對所有權。完成後才擴到完整Join入口、自身clear的callback、CASClick按鍵重複，最後才進入第三階段遊戲畫面／效能驗證。

## 驗證與證據界線

- optimized、ASan/UBSan兩個完整引擎均成功編譯；最終三種probe在兩種build共6次執行，結果一致。metadata exit0；observer exit78、timer exit77明確表示尚有殘留，並非全綠。
- 最終有邊界檢查的probe沒有ASan/UBSan診斷；LeakSanitizer未啟用，洩漏用VM活躍slots實測。這不等同沒有記憶體問題。
- 第一階段回歸：兩種build各928項Array檢查；FFI各22次回呼／巢狀HLL及預期拒絕陣列變動案例均通過。FFI fixture新增heap_ref替身以連結新bridge路徑，仍屬受控測試，不能替代真VM結果。
- Join是種入其capture後執行真回呼f36081，**沒有跑完整f27031入口或遊戲動畫**。observer建立使用production delegate helper。
- timer原始脚本測試只在harness關閉既有native timer攔截；一般遊戲仍可能走替代實作。本輪並未驗證其完整互動。
- 未涵蓋callback執行中自行clear的動態案例、reference cycle回收、舊存檔相容性、完整遊戲輸入／FPS。
- 開發中出現的早期失敗保留在原始logs：包括harness未設savedir時初始化無關模組、以及未接受「空陣列page=NULL」的probe空指標。最終改用只link服務並修正probe邊界；不能把這些harness錯誤冒稱遊戲bug。

## 交付與重跑

- 第二階段-生命週期修正-2026-09-20.patch：**僅本階段**相對第一階段結束狀態的增量；尚屬未完成驗收的研究修補。
- 第二階段-生命週期證據-2026-09-20/：最終results.json、逐case logs、source hashes、編譯命令、技術metadata、delegate審查與實作說明、probe原始碼。
- 第二階段-重新測試.command：在本機現有隔離workspace重建、重跑。runner返回0代表與已知結果一致，**不是第二階段全過**；請讀results.json的stage2_fully_passed=false。證據副本中的build腳本保留作存檔，請由原workspace工具路徑執行。

## 額度與停止點

起始週額度剩24%；最後查詢剩約3%（帳號整體使用量，並非本對話精確token數，也不是可保證的剩餘工作量）。未使用重置額度。現在保存部分成果並停止，不進第三階段，等待使用者決定是否繼續第二階段。
