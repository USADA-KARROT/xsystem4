# Stage 2：有限 message-window 修復

Production source 已於交付主 agent 建置前凍結；未啟動遊戲／UI、未提交 Git commit。主 agent 負責 normal／ASan 整合建置及中文畫面驗收。

## 已實作

- 新增 `src/parts/message_window.c`，以每個 parts 物件擁有的 sidecar 保留 raw text、CG name、獨立文字資料、TextArea／字體／字距和 marker visibility。
- SetMessageWindowText 不再把 DEFAULT 背景轉成 PARTS_TEXT；CG setter 不會釋放獨立文字。GetMessageWindowText 保留 AIN composer 的「讀回原文 → 追加 → 設回」流程；GetMessageWindowCGName 不再呼叫具狀態重置副作用的 parts_get_cg。活動預先設定的 CG 可直接讀取，不建立 sidecar／改 type。
- sidecar 在 parts_release 時釋放字串及 glyph textures，避免 parts 編號重用留下舊文字。
- render.c 在同一 window 的背景之後繪製 sidecar text，沿用其 show／alpha／色調／z。即使背景為空也會到文字 hook；原本的 visibility gate 仍控制整個 window。
- Message TextArea 用背景左上角加區域偏移決定文字位置。renderer 接收明確 Point，不再疊加由普通 parts origin 算出的 sidecar offset。保留 origin mode，暫按 Rufim 已觀察的 Dohna 0／1／7 模式在區域上緣畫字；沒有硬編對話螢幕位置。
- 依 AIN composer 中可見的 `${time ...}`／`${font ...}` 和對應結束標籤，產生不包含這些控制碼的 plain text；raw text 完整保留。以 byte runs 複製，沒有經 SJIS string_push_back，避免破壞 GB18030 二／四 byte 字元。
- SetKeyWaitShow 改為保留獨立 marker visibility，不再以 PE_SetShow 隱藏整個對話 window；沒有新增人工 overlay 或捏造 marker 圖像。

`pe_v14_message.c` 已與 local agent 協調接手文字區段，保留其 queue／GetMessageVariableString ABI 修復與既有低量 before/after trace。PartsEngine.c 仍包含先前完成的 CG getter ABI 修復。

## 改動檔案

唯一新增 production 檔：`src/parts/message_window.c`。其餘為 `include/parts.h`、`src/meson.build`、`src/parts/parts_internal.h`、`src/parts/parts.c`、`src/parts/render.c`、`src/hll/PartsEngine.c`、`src/hll/pe_v14_message.c`。完整可重現 patch 由主 agent 統一收集，須含新增檔；pe_v14_message.c 同時含 local agent 修復，不應把整檔 diff 都歸給本項。

## 驗證

1. 使用 normal-build 實際 compile flags 對新 production 模組做 syntax-only，exit 0；參數／結果：`validation/message-window-syntax.json`。
2. `validation/message_window_fixture.c` 直接包含正式 sidecar 模組，以最小 parts lookup／glyph 產生替身避免啟動 SDL／GL；使用真實 libsys4 strings，ASan＋UBSan。編譯／測試 exit 0、無 sanitizer findings；結果與編譯參數：`validation/message-window-result.json`。
3. Fixture 驗證原背景 getter 只讀、文字不改 CG type/name、CG 更新不刪文字、composer 累積、GB18030 二／四 byte 原樣保存、time/font 控制碼不當 glyph、未知／未封閉片段保持原樣、TextArea readback、沒有 double origin、隱藏 marker 不隱藏 window、100 次 clear／回傳字串釋放後 EMPTY_STRING refcount 回到基線。

這不是完整 glyph／GPU、遊戲或快照 round-trip 測試。限於本輪最小入口，未實作逐字時間、行內 font style runs、ruby、自动換行、marker CG／動畫、sidecar 的存讀序列化；未知標籤保留，不默默丟棄文字。尚未移植活動 pactex 的 message 專屬字型／TextArea 初始化，若本輪腳本不送 setter，應按真實 pactex 資料補初始化，再驗證座標和字體，不應加固定座標補丁。

下一步只以主 agent 的實機 trace 和可見第一段中文／等待行為决定必要補缺口。
