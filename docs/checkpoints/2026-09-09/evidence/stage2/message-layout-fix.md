# Stage 2 — Message window 版面載入與有限 render 診斷

日期：2026-09-09。本輪 production 僅修改 `source/src/hll/pe_v14_activity.c` 和 `source/src/parts/message_window.c`，保留 pixel hit-test、queue、S_PLUSA 與其他 agent 修改。沒有啟動遊戲或 UI。

目前已確定的 loader 問題：信息窗口的 `種類別情報` 含 direct ＣＧ名、文字版面、キー待ちマーク、ルビ。舊 loader 將所有 non-leaf 子節點當作 DEFAULT/HOVER/CLICK 狀態，漏掉 direct window CG，反而將 key-wait marker CG 當成 DEFAULT 背景。因此 trace 顯示 type=CG 並不代表背景正確，origin offset 也會依錯誤小圖尺寸計算。

新增 `pactex_apply_message_window` 僅匹配 exact `メッセージウィンドウ`（SJIS）或 `信息窗口`（目前 CN GB18030）。讀取該 component 自身 type-info 的 direct CG、文本エリア、文本位置、font type/size/color/weight/edge、文字/行間隔，呼叫既有 message sidecar setters，然後提前返回，避免 nested key-wait/ruby 再落入 generic state loader。原始 AIN 文字不變；未寫入固定螢幕座標。

actual AFA metadata 由 `message-layout.log` 核對：

| Window | component position / origin | textarea x,y,w,h | Font | Letter / line |
|---|---|---|---|---|
| main | 662,607 / 5 | 130,62,940,181 | 25 white, edge 0 | -1 / 10 |
| mainB | 662,607 / 5 | 130,62,940,181 | 25 black, edge 0 | -1 / 10 |
| event | 640,720 / 8 | 360,600,940,181 | 25 white, edge 1.5 gray64 | -4 / 8 |
| plot | 640,360 / 5 | 80,90,1280,720 | 20 white, edge 1 black | -2 / 5 |

實機 parts 900068 的 (640,720) 與 event 視窗吻合；本輪 fixture 從 archive 讀取原始 metadata，沒有互換四個視窗的數值。

`parts_message_window_render_text` 新增 `XSYS4_STAGE2_TRACE` 下前 12 次非空 raw text 的 render 診斷：parts no、raw/plain bytes、line/char count、show/window_show/alpha/global z、background texture handle/dims/origin、textarea/origin mode、計算後 position、font face/size/RGBA、letter/line spacing。沒有每 frame 無上限輸出，也沒有輸出完整劇本文字。render hook 在全局 visibility/link gate 後；若文字被更早 Clear 或未通過 gate，此訊息不會出現。

有限驗證：

- 兩 production 檔 syntax-only 通過；activity 的 warnings 為既存 unused symbols。
- `validation/check_message_layout.py` 抽取 production helper，搭配真實 libsys4 AFA/EX/string，在 ASan/UBSan 下讀 actual archive 的四份 pactex，確認各視窗的 direct CG、area、font/color/edge/spacing，排除 key-wait CG 誤用。將同一份資料的 direct keys 轉成 SJIS 後再次通過；NULL/其他 ptype 不套用。結果 `validation/message-layout-result.json` test exit 0。
- fixture 首次 plot spacing 期望值誤填 0/0，依獨立 metadata log 修正為 -2/5 後四份全部通過；production 不需再修改。
- 既有 `message_window_fixture.c` 因診斷外部旗標增加 bool fixture binding，重新執行通過 raw/plain GB18030、composer、CG/text isolation、geometry、wait visibility、ownership。
- `git diff --check` 通過。所有 production source 已凍結並通知 root 統一 build/runtime。

可見性尚待整合驗收：前輪 S_PLUSA 修正後 setter 已非空，但 Wait 僅數毫秒就清頁，因此截圖無字無法單獨證明 renderer 失敗。本修正補齊確實遺漏的版面契約；必須等 ref-bool 等待修復後，在文字存續期間核對 Render trace 和實際畫面。不包含完整 markup 時序、ruby、word wrapping、save serialization、key-wait marker 圖形等其他功能。
