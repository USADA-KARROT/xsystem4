> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 第二階段完成報告：生命週期與引用管理

2026-09-24。**本輪第二階段定義的8組驗收已全部通過。** 9/20留下的observer每輪4個物件、timer第一輪1個物件殘留均已消除。這是無畫面的原始腳本／引擎驗證，尚不能把它寫成遊戲不卡或已可玩。

## 實際修正

1. **v14賦值移交所有權。** 原始腳本已使用DELETE釋放舊值、SP_INC／A_REF準備新引用；VM的X_ASSIGN又加減引用，造成delegate多一份owner、timer Get每次多一份owner。改為移交已準備好的引用，保留較舊版本原行為。對共享陣列，X_A_INIT也不再重複釋放原腳本已DELETE的舊owner；實測外部持有者仍保持有效。
2. **陣列清空後保留元素型別。** v14 Array.PopBack最後一個元素移除後保留空的typed page，避免下一輪EmplaceBack把CASTimerImp當成int。這解決第一輪正確、第二輪計時失敗的問題。
3. **v14物件欄位先置空，交由原始NEW建構。** plain STRUCT欄位原先提前配置未執行建構式的子物件，生成的初始化脚本一開始DELETE它時，會對未註冊handle呼叫timer析構。改為null初值；wrap／繼承路徑未在此改寫。真實NEW CASClick後，內嵌timer建構／析構各一次且沒有system.Error。
4. **釋放的page slot確實回到free list。** VM_PAGE枚舉值是0，舊的ref==0且type==0判斷把正常page釋放誤判為重複釋放。移除錯誤判斷，保留入口與範圍檢查；deferred drain期间禁止pressure GC重建同一批槽位，避免析構重入時重複入列。

以上變更在隔離 work/stage2/source 的4個檔案；沒有針對遊戲函式編號或CASClick／CASTimer名稱新增production特例。

## 驗收結果

下列8組各跑optimized與ASan／UBSan，合計16次，全部exit0、無sanitizer診斷。

| 案例 | 實際結果 |
|---|---|
| heap-reuse | 1,000輪真實page slot回收／重用、generation變更、重複unref保護；exit_unref亦可回收；容量不成長、活躍物件歸零 |
| assignment | 新物件移交、共享引用搭配SP_INC、DELETE後改為null三種所有權片段均正確；結束歸零 |
| array-reinit | 原始manager建構式重新初始化4個共享陣列；外部owner保持ref1且generation不變，最後歸零 |
| metadata | 4個原始manager陣列型別／struct id／rank正確，初始化ref1、清理歸零 |
| observer | 原f20752與Join callback f36081連續20輪false→true→第三次不重複通知；每輪292次caller存活、16次setter檢查；每輪結束live0 |
| timer | 原腳本20輪、共60次建構／60次析構；7/10ms計時、Reset後5ms、內部handle洞重用與尾端清理通過；每輪回manager基準5，manager釋放後live0 |
| reentrancy | 原callback執行中由測試hook清除其delegate；20輪中obj/env僅由callback frame持有仍可完成，返回後釋放，live0 |
| click | 執行原AIN的NEW233,-1與CASClick腳本；keydown、349/350ms、49/50ms、keyup邊界正確，內嵌timer建構／析構各一次、live0 |

額外回歸：Array兩種build各928項通過；FFI各22次回呼／巢狀HLL及預期拒絕mutation案例通過；Meson的hashtable／instructions在兩個build各2項通過；完整引擎兩版建置成功，git diff --check通過。

## 證據與測試範圍

- 原始CN AIN SHA-256：beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947。沒有修改遊戲bytecode或玩家存檔。
- 真VM、page、heap、FFI及待驗證原始函式均實際執行；clock和key是可控host服務。若腳本呼叫system.Error，最終probe直接exit93，不能靠忽略錯誤通過。
- Observer使用最小資料種入與原Join回呼，尚未跑完整Join入口或動畫。v14欄位改為null後，fixture明確配置MotionSet／SectionParam作為輸入，不手動補漏釋放。
- assignment是受控operand搭配production opcode handler，與完整原AIN函式案例分開列出。self-clear的清除動作由hook注入，其後回呼內容、寫回與RETURN仍由原bytecode完成。
- timer腳本測試只在harness關閉既有native替代攔截；實際遊戲現有替代路徑與整合效果留待第三階段確認。
- LeakSanitizer未啟用；「歸零」指明確計數的VM活躍heap slots，不是宣稱整個程序所有C配置都已做洩漏證明。timeout／scenario unwind、reference cycles、大型存讀檔與完整遊戲未在本輪驗證。
- 無壓力GC／析構重入的強制動態案例；GC inhibit覆蓋其兩個入口由靜態審閱確認。已做實際配置回收、重複unref與原腳本析構測試。
- 早期失敗、私有候選實驗及版本差異保存在證據包；最終結果以results.json、各模式out/err與build manifests為準。

## 交付與下一步

- 第二階段-續作修正-2026-09-24.patch：相對9/20第二階段checkpoint的增量，不能單獨當成從原始上游開始的完整patch。
- 第二階段-續作證據-2026-09-24/：最終log、結果JSON、source/fixture hashes、修正前檔案、獨立審查與測試原始碼。包含修正後4個source檔供比對。
- 第二階段-重新測試-2026-09-24.command：在本機既有workspace重建與執行8組案例；成功需要全部exit0。證據副本僅供保存，重跑請使用原work路徑或此command。

建議下一步進第三階段：用短時間實際遊戲測試確認主畫面、進場／對話／按鍵流程，再量測FPS、幀時間與記憶體變化，確認本轮修正與現有timer替代路徑的整合。**依使用者分階段指示，目前停止，不開始GUI／FPS測試。**

最後查詢帳號週額度剩約63%，本輪開始為85%；這是帳號整體比例，不能視為本對話精確token帳。未使用任何reset。
