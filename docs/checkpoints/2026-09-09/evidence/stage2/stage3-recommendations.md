# 第三目標建議：完成一段 ADV 的穩定操作與存讀檔循環

2026-09-09，依 Stage 2 隔離副本目前原始碼與既有測試紀錄唯讀整理。本輪未修改 production、未重建、未啟動遊戲。主線已完成 O0／O2 短程效能、真實單擊翻頁及 ASan/UBSan 驗收；具體數據以第二目標主報告為準。free-list候選因logo regression已回退，以下以交付來源仍有的缺口安排下一步。

**Stage 2 應定位為「開場 ADV 關鍵相容性修復與有限驗證」，不能稱為完整可玩版。** 第三目標建議先選定一段包含連續翻頁、視窗切換、回到標題及存讀檔的固定流程，讓結果可重現，再按該流程實際遇到的 API 補缺口。

## 已知限制

| 項目 | 已確認的範圍與缺口 |
|---|---|
| 訊息文字 | sidecar 保留原文供 composer 串接，獨立於背景 CG；目前同步顯示全文。`${time}`／`${font}` 標籤僅移除，未實作逐字時序及局部字型效果；`IsFixedMessageWindowText` 固定 true。Ruby 尚未實作，未知控制碼沒有完整解析。 |
| 視窗版面與效果 | 已依四份實際 pactex 載入背景、文字區域及基本字型。尚無完整自動換行、文字區域裁切及所有 origin mode 的行為；縮放／旋轉也未全面適配。Flat 訊息視窗動畫未實作，Flat name getter 回空字串；等待圖示目前只保留 show 旗標，沒有圖形／動畫。這些缺口不代表每一個都已在開場造成故障。 |
| Array 與引用生命週期 | 本輪修正與 fixture 只覆蓋特定 Erase overload、型別／stride 和已觸發的資料流；不能推論整個 Array HLL 或 VM ownership 正確。Insert、Duplicate、EraseAll／Remain、Unique 及排序仍需依 AIN 宣告檢查多槽元素、別名、自複製、重複引用、callback 返回後的引用與 stack 平衡。QuickSort 現行程式明確忽略 comparator，只作整數排序，屬已知功能限制。 |
| Heap 與效能 | normal-perf 已記錄約 26.5 秒停頓；新 GC 政策移除停用 cycle sweep 時無收益的 mark，並限制壓力 GC 觸發頻率，fixture 通過，實機改善幅度仍由主線量測。這不會自動修好所有保留引用與 cycle。既有 ASan dump 有約 179 萬個非空項目、約 2,531 萬個陣列值槽，需追查 timer／click／delegate 等物件的增量及釋放；snapshot 不能單獨證明全部都是洩漏。 |
| 存讀檔與復原 | 寫出 ResumeSave dump 不等於玩家存讀檔通過。`parts/save.c` 現行格式保存 states 與 message_window bool，沒有新 sidecar 的 raw text／版面／字型／等待旗標；需確認遊戲 load 路徑是否重新建立這些狀態。SaveBackScene 仍是回報成功的未實作入口，縮圖亦未實作，不能宣稱 backlog／畫面快照已可用。 |

程式依據：`source/src/parts/message_window.c:58`、`:105`、`:196`、`:209`；`source/src/hll/pe_v14_message.c:286`、`:351`；`source/src/hll/PartsEngine.c:1516`；`source/src/hll/Array.c:732`、`:774`、`:805`、`:1974`、`:2079`；`source/src/parts/save.c:542`。效能／ownership 細節見 `performance-findings.md`、`ownership-review.md`。

## 建議的驗收順序

1. **先收斂 Stage 2 最終結果。** 固定同一 AIN、遊戲資產與 source patch 身分；O0／O2 各跑同一段真實輸入流程，確認中文字保持等待、一次點擊只推進一次、清頁與下一頁正確。比較長幀、GC 次數／耗時、RSS 與 heap 使用量；ASan／UBSan 用於正確性驗證，其速度不作一般版效能結論。若仍有秒級停頓，先取樣定位，暫不擴展劇情範圍。

2. **建立有限的記憶體與 Array 驗收。** 同一段至少完成三次「進入 → 約 20–30 次翻頁 → 返回」循環，並觀察約 30 分鐘。按物件型別與配置來源比較活躍數量，不只看 heap capacity；已結束 observer／timer／click callback 的數量應能回落，不能每次循環固定增加。對實際使用的 Array API 補 production fixture，要求引用守恆、元素順序、空陣列、自複製及多槽 stride 正確。完整 root／mark coverage 未建立前，不能直接重新啟用 cycle sweep。

3. **驗證玩家存讀檔與重新啟動。** 使用隔離存檔，在等待文字頁存檔，推進後讀回，再關閉引擎重新載入；核對原文、背景、位置／字型、等待狀態、下一次點擊及必要音訊。至少三個存檔點、各兩次往返；若依 AIN 重建 sidecar，需證明該路徑必定执行，否則設計有版本號的保存／復原格式。縮圖、backlog、ResumeSave／ResumeLoad 分別標示測試結果，不能互相代替。

4. **按實際場景補文字效果。** 選有 ruby、逐字、局部字型、換行或 Flat 的最小片段，先核對 AIN 呼叫與 Rufim 語意，再做單一功能修復及畫面比較。每项完成後重跑同一組翻頁和存讀檔流程，確保保留原始文字、控制碼不外露、文字未越界、快進／完成通知仍正確。

第三目標的完成條件應是上述固定流程有可重現的畫面、输入、效能、記憶體及存讀檔結果，並附尚未覆蓋的場景清單。只有開場成功顯示或短時間無 sanitizer 報錯，仍不足以證明長時間可玩或沒有洩漏。
