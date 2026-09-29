# 字距修正：CN 的 GDI 字格（側審查 D1–D4，`9f81bd9`）

接續 [spacing.md](spacing.md) 的修法 B。`05d2441` 只做了修法 A（GBK 且無 .fnl 時打開 .fnl 的外框前進量）與 (字級+1)>>1 半形，側審查（[uaf.md](uaf.md)〈側審查〉）找到 D1–D4 四個缺陷。本組把整套原版公式搬進 xsystem4，四項一起修。

證據等級沿用 spacing.md：**已驗證**＝逐指令反組譯、程式碼或量測直接支持；**推定**＝靜態推論合理但沒有逐指令追完；**未驗證**＝目前沒有證據。

## 1. 原版公式與位址

CN 版沒有 .fnl，每個字用 GDI 畫進自己的字格，字格並排：

```
e      = max(min(ceil(太さ), 字級), min(ceil(縁取り), 字級))   ; 有號比較，ceil 後以 cvttss2si 取整
字寬   = 首位元組在 0x81..0xFE 時為字級，否則 (字級+1)>>1
字格   = 字寬 + 2e，字形畫在 x = e
前進量 = 字格 + 字距
```

| 位址 | 內容 | 等級 |
|---|---|---|
| `0x69c7a0` | 字寬函式。本組重新逐指令讀過：字型結構 `+4` 型別、`+8` 字級、`+0x1c` 太さ、`+0x20` 縁取り；型別 < 0x100（GDI）時只看字串的**第一個位元組**；字串長度為 0 時字寬是 0，但仍回傳 `0 + 2e`；回傳 true | 已驗證 |
| `0x69f590` → `0x69fb30` | `TextSurfaceManager.GetFontWidth` 的 HLL 入口（12 個參數，回傳 bool）。`0x69fb30` 用參數組出字型結構（型別、字級、顏色夾在 0..255、BoldWeight 放 `+0x1c`、EdgeWeight 放 `+0x20`），把**整個字串**交給 `0x69da70`（上鎖）→ `0x69c7a0` | 已驗證 |
| PartsEngine 跳表 `0x589714` case 729 → `0x586530` → `0x5987a0` → `0x69f230` | `Parts_SetFont`：`0x69f230` 把 BoldWeight 寫進 `+0x1c`、EdgeWeight 寫進 `+0x20`，也就是 `0x69c7a0` 讀的兩個欄位；再由 `0x5661e0` 交給文字元件 | 已驗證 |
| case 734 → `0x58681a` → `0x5989c0` → `0x5664c0` | `Parts_SetPartsFontBoldWeight`：同一個文字元件的太さ。`0x5664c0` 寫入 `+0x1c` 沒有逐指令追 | 推定 |
| case 531 → `0x583400` → `0x595840` | `SetMessageWindowTextFont`：參數順序與 `Parts_SetFont` 相同（少 State）。後續沒有追 | 推定 |
| `0x528aa0` | 另一個量字串大小的常式：寬 = Σ(`0x69da70` + 字距)，行高 = 偶數化字級 + 2e。本組沒有改行高 | 已驗證（僅記錄） |

AIN 端：`CASFont@GetFontWidth`（fno 20812）每次傳一個字、連同 `BoldWeight` 與 `EdgeWeight` 呼叫 `TextSurfaceManager.GetFontWidth`，回傳值被 `POP` 丟掉。

## 2. 修法

所有新行為都以 `gfx_text_cn_gdi()` 為條件：**GBK 字元規則（`sys4_get_string_charset() == SYS4_CHARSET_GBK`）且沒有 .fnl**。它在呼叫當下判斷，不依賴 `gfx_font_init` 已經跑過（`GetFontWidth` 可能比字型初始化早被呼叫）。SJIS 遊戲與有 .fnl 的遊戲走原本的路徑。

| 檔案 | 改動 | 對應缺陷 |
|---|---|---|
| `src/text.c` | 新增 `gfx_text_cn_gdi`、`gfx_text_cn_edge`（照 `0x69c7a0`，含 cvttss2si 的 INT_MIN 語義）、`gfx_text_cn_width`（首位元組規則）、`gfx_text_cn_style_edge`（太さ取 `max(bold_width, bold_weight)`，縁取り取四邊最大值）。CN 組態下：`_gfx_render_text` 的字寬依首位元組、`gfx_render_textf` 的每邊外框 = e（取 ceil 後取 max，不再是「外框 + ceil(bold_width)」）、`gfx_size_char` 與 `gfx_size_text` 用同一個字格；字形載入失敗的字仍佔一格（原版的字格與字形無關）。`gfx_font_init` 拿掉 `05d2441` 的兩個旗標 | D2、D3 |
| `include/gfx/font.h` | `struct text_style` 新增 `bold_weight`（v14 SetFont 的 BoldWeight，只有 CN 字格讀它）；`text_style_width`（每字貼圖寬）在 CN 組態 = (字寬 + 2e) × scale_x；刪除 `gfx_text_gdi_half_width` | D2 |
| `src/parts/text.c`、`src/parts/message_window.c` | `PE_SetFont`、`PE_SetMessageWindowTextFont` 保存 `bold_weight`。字形的粗細仍照舊由 `weight`（BoldWeight×1000）決定，沒有改 | D2 |
| `src/hll/TextSurfaceManager.c` | CN 組態下回傳 `0x69c7a0` 的結果：第一個字的字寬 + 2e，空字串為 2e。其他組態保留原本的位元組啟發式（逐字加總、半形 size/2、不算外框） | D1 |
| `src/font_freetype.c` | 缺字 fallback 只在 CN 組態啟用；`05d2441` 的 (字級+1)>>1 半形移到字格，`ft_font_get_glyph`／`ft_font_size_char` 回到 size/2 | D4 |

設計取捨：

- **太さ取 `max(bold_width, bold_weight)`**：原版只有一個太さ欄位，`Parts_SetFont` 與 `Parts_SetPartsFontBoldWeight` 都寫它。xsystem4 的 `PE_SetPartsFontBoldWeight` 與構成處理的文字（construction process）寫的是 `bold_width`，它同時會把字形加粗；`PE_SetFont` 沒有重設 `bold_width`（上游行為）。取兩者較大值，字格一定容得下實際畫出的加粗字形。只有「先 SetPartsFontBoldWeight 大值、再 SetFont 小值」這種順序會和原版不同，此時畫出來的字形也確實比較粗。
- **缺字仍前進一格**：只在 CN 組態。上游對載入失敗的字是整字略過（連右側外框也不加）。
- **GetFontWidth 的回傳值**：原版對型別 ≥ 0x100 且沒有 .fnl 時回 false（字寬照算）。AIN 不看回傳值，本組一律回 true，沒有模擬。

## 3. 驗證

### 3.1 headless：新模式 `text-metrics`

`harness/probe/text_metrics_fixture.inc`。探針沒有 GL context，所以：

- 樣式經真 AIN 宣告與 ffi 設定（`Parts_SetFont`、`Parts_SetTextCharSpace`、`Parts_SetPartsFontBoldWeight`、`SetMessageWindowTextFont`／`SetMessageWindowTextSpace`），再讀回元件上的 `text_style`。
- `fw`：經真 AIN 宣告呼叫 `TextSurfaceManager.GetFontWidth`（一字、空字串、三字）。
- `cell`：`text_style_width`，即 parts 文字每字的貼圖寬；`size`：`gfx_size_text("10萬")`。
- `adv`：`gfx_render_textf` 的回傳值，也就是 parts 文字排版用的前進量。為了不需要 GL，把字型換成「字形永遠載入失敗」的假字型：不會畫任何東西，但排版照跑。CN 字格必須仍前進一格；上游路徑會整字略過，所以 SJIS 組態下是 0（固定下來）。**字形存在時的分支只差在實際繪製，由 GUI 量測涵蓋。**
- `fb`：缺字 fallback。U+83C8 不在 VL Gothic、在 HanaMinA；透過 `build_probe.py` 在 repo 外複製的 `font_freetype.c` 上加的包裝函式詢問。
- 字型用 repo 的 `fonts/`（`$XS4_SRC/fonts`）。GBK 與 SJIS 各在一個 fork 出的子行程跑（`gfx_font_init` 每個行程只跑一次）。GBK＋.fnl 只測 `GetFontWidth`（探針沒有 .fnl 檔）。

12 組樣式取自 `dohnadohnaPact.afa`（spacing.md §2.5 的工具）：

| 樣式 | 字級／太さ／縁取り／字距 | e | 原版前進 2B／1B | `ca2ebff` 繪字（公式推算） | `ca2ebff` GetFontWidth 2B／1B | 修正後 GetFontWidth | 修正後前進 |
|---|---|---:|---|---|---|---|---|
| AdvMessageWindow_main | 25／0／0／−1 | 0 | 24／12 | 24／12 | 25／12 | 25／13 | 24／12 |
| AdvMessageWindow_event | 25／0／1.5／−4 | 2 | 25／13 | 24／12 | 25／12 | 29／17 | 25／13 |
| AdvMessageWindow_plot | 20／0／1／−2 | 1 | 20／10 | 20／10 | 20／10 | 22／12 | 20／10 |
| AdvNamePlate | 28／0／3／−4 | 3 | 30／16 | 30／16 | 28／14 | 34／20 | 30／16 |
| AdvNamePlate_event | 25／0／3／−4 | 3 | 27／15 | 27／15 | 25／12 | 31／19 | 27／15 |
| BackLog SYS_普通文本 | 25／0／1／−3 | 1 | 24／12 | 24／12 | 25／12 | 27／15 | 24／12 |
| DungeonSelector TextName | 22／0／2／−4 | 2 | 22／11 | 22／11 | 22／11 | 26／15 | 22／11 |
| SceneAchievementNotify TextAchievement | 32／0／2.7／−2 | 3 | 36／20 | 35.4／19.4 | 32／16 | 38／22 | 36／20 |
| SceneAchievementNotify Caption | 40／0／3.5／−7 | 4 | 41／21 | 40／20 | 40／20 | 48／28 | 41／21 |
| WorkerParamView TextPersonality1 | 25／0.8／0／−3 | 1 | 24／12 | 22／10 | 25／12 | 27／15 | 24／12 |
| AdvCaption Caption | 56／0.5／3／−6 | 3 | 56／28 | 56／28 | 56／28 | 62／34 | 56／28 |
| （合成）SetPartsFontBoldWeight 2＋縁取り 4 | 19／2／4／0 | 4 | 27／18 | 31／22 | 19／9 | 27／18 | 27／18 |

- D1 的例子：backlog 在 `ca2ebff` 量到 25 − 3 = 22 px、畫 24 px；修正後量到 27 − 3 = 24 px，與繪字一致。
- 「`ca2ebff` 繪字」一欄是依當時程式（字寬 + 2×(縁取り + ceil(bold_width)) + 字距）推算的，不是探針量到的值；探針在 `ca2ebff` 上走假字型時是整字略過（見下）。

修正前後（輸出全文在 [spacing-fix/](spacing-fix/)）：

| 版本 | GBK 檢查 | SJIS 檢查 | rc |
|---|---|---|---|
| `4a82758`（`05d2441` 之前） | 161 項中 142 項失敗（含 U+83C8 沒有 fallback） | 全過 | 86 |
| `ca2ebff`（修正前） | 161 項中 131 項失敗（通過的 30 項：整數外框且無太さ的 cell 與 size、無外框樣式的部分 fw、GBK 的 fb、GBK＋.fnl） | 1 項失敗：U+83C8 在 SJIS 也 fallback（D4） | 86 |
| `9f81bd9` | 全過（GBK 12 樣式 × 13 項、fb 2 項、GBK＋.fnl 3 項） | 全過（12 樣式 × 8 項、fb 1 項） | 0 |

- SJIS 的 109 行輸出，`9f81bd9` 與 `4a82758` **逐行相同**；與 `ca2ebff` 只差 fallback 那一行。也就是 SJIS 組態回到 `05d2441` 之前的行為。
- `ca2ebff` 的 `adv` 失敗有一部分來自假字型：當時對載入失敗的字整字略過，所以量到的是 0 或只有外框。`fw`、`cell`、`size` 則是真實數值的差異。

### 3.2 verify-step

47 個模式（46 個既有模式 + `text-metrics`）在預設組態與 `XS4_PROBE_GBK=1` 都是 `VERDICT PASS`，sanitizer 0，`deleted-event` 仍是預期的 87（[verify-summary.txt](spacing-fix/verify-summary.txt)，head `9f81bd9`）。

### 3.3 GUI（150 秒）

`gui-run.sh`，optimized build，樹與 `9f81bd9` 相同（`b215e6d` 只差 commit 訊息）：150.35 秒跑滿、MSG 88、assertion 0、堆疊溢位 0、ASan 0、字形載入失敗 0，峰值 RSS 1,304,625,152 bytes。MSG 內容的 sha256 與 `9e30c0f` 的參考執行相同（[gui-summary.json](spacing-fix/gui-summary.json)）。

framebuffer 量測（`spacing_tools/measure_spacing.py` 同一組區域；原版截圖重取樣回 1280×720，誤差約 ±1 px；字形起點會因字型不同差 1–3 px，所以比較字距與字形中心）：

| 位置 | `05d2441` 之前 | `05d2441`（`9e30c0f` 執行） | 本修正 | 原版實機截圖 |
|---|---:|---:|---:|---:|
| event 視窗 MSG 5 第 2 行（`t04`） | 21 | 24（387→459，3 字 72 px） | **25**（「朗」388 起，每字 25，「像」起點 838、終點 860，18 個間隔） | 25（391 起，末字終點 858） |
| event 名牌「＊＊＊」（`t10`） | 21 | —（該張沒有名牌） | **27**（361、388、415） | 27（364、391、418） |
| 主視窗名牌「＊＊＊」（`t15`） | 24 | 30 | **30**（372、402） | 30（374、404） |
| 主視窗對白 MSG 32（`t15`） | 24 | 24 | **24**（386、410、435、459、482） | 24 |
| event 視窗「支持市內10萬商家付款」（`t10`） | 數字與「萬」重疊 | — | 數字各 13，「萬」起點 514 | 「萬」起點 516，其他字也一致偏 1–2 px |
| 角色名牌「綺菈綺菈」（`t29`） | 「綺□」 | 完整 | 完整，字距 30，左側外框完整 | — |

- 「＊」的字形中心：event 名牌本修正 367.5、原版 368；主視窗名牌 378.5、原版 379。起點差 3 px 是 VL Gothic 的「＊」比 MS Gothic 寬（14 px 對 9 px）。
- 「……」的點位（508、517、526、532、541、550）與修正前相同，那是字型差異（spacing.md §2.4），不在本組範圍。
- 截圖與原版實機截圖都留在 repo 外。

## 4. 未驗證事項與限制

- **backlog、DungeonSelector、成就通知的畫面**：測試腳本走不到這些畫面。量字寬與繪字一致只在 headless 驗證，折行與裁切沒有在畫面上確認。
- **太さ的字形粗細**：本組只把太さ算進字格。字形本身仍依 `weight`（BoldWeight×1000）選一般或粗體；原版推定是依 ceil(太さ) 擴張（spacing.md §2.1，`0x69bb82`–`0x69bc73` 未逐指令追）。有太さ的樣式字距已對，粗細可能和原版不同（未驗證）。
- **垂直方向**：行高（原版 `0x5bce00`、`0x528aa0` 是偶數化字級 + 2e）與字形的 y = e 都沒有改。
- **行寬含最後一個字距**（spacing.md R4）沒有改。
- **PE_Save／PE_Load**：`iarray_write_text_style` 的格式沒有改，所以 `bold_weight` 不會被存進 parts 的存檔；讀回後，有太さ的樣式會少算 2 px，直到 AIN 重新呼叫 SetFont。這條路徑在目前的 GUI 執行中沒有觸發過（HANDOFF 第 3 項）。
- **GBK＋.fnl**：探針沒有 .fnl 檔，只驗證了 `GetFontWidth` 保持原樣；其他路徑是程式碼層級（`gfx_text_cn_gdi()` 在 `config.fnl_path` 非空時為 false）。
- **四位元組 GB18030**：xsystem4 當成一個字（全形），原版 GBK 會拆成兩個雙位元組字。AIN 的 STR0 與 MSG1 中是 0 筆（`research/gbk-string-rules/`），沒有處理。
- **全樣式對照**：`dohnadohnaPact.afa` 的 1,644 組字級 > 0 的樣式（含 ruby 與訊息視窗等）中，依 `05d2441` 的公式有 248 組與原版不同（外框小數 90、太さ較大 158），依本組公式為 0。這是公式模擬，不是逐一執行（推定）；和側審查的 1,472／60／151 用的篩選不同。
- `9f81bd9` 尚未經獨立反駁者審查（HANDOFF 流程第 3 步）。

## 5. 重跑

```bash
H=docs/checkpoints/2026-09-28/harness
bash $H/before-check.sh ca2ebff text-metrics     # 修正前：rc=86
bash $H/before-check.sh 4a82758 text-metrics     # 05d2441 之前：rc=86，SJIS 行與修正後相同
bash $H/verify-step.sh <tag>                     # 47 模式
XS4_PROBE_GBK=1 bash $H/verify-step.sh <tag>-gbk
bash $H/gui-run.sh <name> 150
```
