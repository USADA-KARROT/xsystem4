# 對話與名牌字距調查（spacing，2026-09-29）

本組只讀調查。沒有修改 `$WT`，也沒有修改母片；沒有執行遊戲 GUI，也沒有執行 EXE（EXE 只做 capstone 靜態反組譯）。

> **修正進度**：修法 A 與半形寬在 `05d2441`；修法 B（太さ、ceil 後取 max、首位元組半形、`TextSurfaceManager.GetFontWidth`）與缺字 fallback 限定 CN 在 `9f81bd9`，修法、原版位址與驗證見 [spacing-fix.md](spacing-fix.md)。本文的行號與「現行」數值是 `6400e3c` 當時的狀態。

路徑記號：

- `$PORT` = `<PORT>`
- `$WT` = `$PORT/worktrees/xsystem4-cn-on-upstream`（HEAD `6400e3c`）
- `$FB` = `$PORT/claude-work/runs/save-fixes2-gui/framebuffer`
- `$ORIG` = `<原版實機截圖>`（原版繁中版實機截圖）
- `$EXE` = `dohnadohna_dump_SCY.exe`

證據等級：

- **已驗證**：有逐指令反組譯、原始資料或截圖量測直接支持。
- **推定**：靜態推論合理，但沒有逐指令追完，或沒有執行期確認。
- **未驗證**：目前沒有證據。

---

## 0. 結論摘要

1. **使用者看到的「字擠在一起」，主因是 xsystem4 在 CN 版少算了字的外框寬度，不是標點被當成半形。**（已驗證）
   - 原版每個字的寬度是「字級 + 2×外框」，前進量是「字寬 + 字距」。xsystem4 的 TTF 路徑只算「字級 + 字距」。
   - 所以有外框（縁取り）或粗細（太さ）的文字，每個字會少 2e px。
   - event 視窗（電視新聞、旁白）每字少 4 px：原版 25、xsystem4 21。
   - 名牌每字少 6 px：主視窗名牌原版 30、xsystem4 24；event 名牌原版 27、xsystem4 21。
   - 主對白視窗沒有外框，雙位元組字距與原版相同（都是 24）。
   - 截圖量測和上面的公式逐格吻合，見 §1。
2. **原版的字寬規則只看位元組數，不看字形。**（已驗證，`$EXE` 0x69c7a0）
   - GBK 首位元組在 0x81..0xFE 時，字寬 = 字級。
   - 否則字寬 = (字級+1)>>1。
   - 兩者都再加 2e，e = max(ceil(太さ), ceil(縁取り))，各自不超過字級。
   - GDI `TextOutA` 在固定大小的格子裡一次畫一個字。所以字型本身的 advance 不影響排版。
3. **xsystem4 沒有把「寬度不明確」的標點給成半形。**（已驗證）
   - `src/font_freetype.c:181,187` 的前進量只看碼點分類 `is_half_width`（`:45-57`），不讀字型的 glyph advance。
   - U+2026「…」、U+2014「—」、U+201C/D「“ ”」、U+00B7「·」都是全形 = 字級，與原版相同。
   - AIN 的 MSG1（726,885 字）與 STR0 用實際資料統計，兩種規則不一致的字是 **0 次**。
   - 唯一會用字型 advance 的 `size_char_kerning`（`font_freetype.c:190-210`）沒有任何呼叫者。
4. **半形字（數字、英文）也偏窄。**（已驗證）
   - xsystem4 用 `size/2` 浮點（25 px → 12.5），`parts_render_text` 又以 `int x += float` 逐字截斷（`src/parts/render.c:128,137`）。
   - 原版是整數 (25+1)>>1 = 13。
   - 例：event 視窗「10萬」，原版每個數字前進 13 px，xsystem4 只有 8.5，顯示時截成 8 px，數字壓到「萬」上。
5. **「……」「——」「“”」的觀感差異，另一部分來自字形設計（字型），不是前進量。**
   - 主視窗「……」的點距：原版是均勻的 8 px；xsystem4 用 VL Gothic，內部 9 px、兩字交界處只有 6 px，所以交界看起來擠。
   - 「——」：VL Gothic 的 U+2014 是滿格橫線。原版畫成兩段半格短線（推定原版字型是 MS Gothic，其 U+2014 為半形字形），見 §2.4。
   - 這一類差異在 ±1–3 px 之內，屬於字型選擇問題。
6. **缺字（例：U+83C8「菈」）改用 fallback 字型，不會改變字距，前提是前進量繼續依碼點／位元組決定。**（高信心）
   - 目前缺字畫成 VL Gothic 的 `.notdef`，是 432/1000 em 的窄框。前進量仍是全形，所以「綺□」後面空出約 14 px。
   - 改成 fallback 字型後會補滿這個空隙，字距不變。
   - 風險：如果 fallback 改用字型的 glyph advance，HanaMinA 的“ ”是 13/25 的比例寬，會讓引號真的變窄，見 §4。

---

## 1. 現象與截圖證據

量測工具：`spacing_tools/measure_spacing.py`（只讀）。

- xsystem4 截圖是 1280×720，直接量測。
- 原版截圖是 2436×1126 的串流畫面。遊戲區域 x0=272，縮放 1063/720。先重取樣回 1280×720 再量測，誤差約 ±1 px。
- 下表的「段落」是白字或黃字核心的欄位區間。字距用每個字的起點差計算。

### 1.1 event 視窗（有外框 1.5）：字黏在一起

| 截圖 | 區域 | 內容 | 量測 |
|---|---|---|---|
| `$FB/xsys4_t04.png` | x360–1000, y636–659 | MSG 5 第 2 行「　朗朗晴空下，準確重現萬老先生身姿的銅像」（20 格） | 「朗」起點 382，「像」終點 782 → 每格 **21** px |
| `$ORIG/C_game_002.png` | 同句 y640–664 | 同上 | 「朗」起點 391，「像」終點 858 → 每格 **25** px |
| `$FB/xsys4_t05.png`、`t07`、`t10` | y590–700 | MSG 7–9、15–16、24–25 | 字與字的外框相連，無法切出單字；「10萬」重疊（t10） |
| `$ORIG/C_game_003/005/007.png` | 同上 | 同句 | 每字可清楚分開；「下面插播一段廣告」的字起點 390、415、440、465、490、515、540、565 → **25** px |

同一句的行長差 76 px（858 對 782），等於 19 × 4 px，符合「每字少 2e = 4 px」。

### 1.2 名牌（外框 3）：「＊＊＊」擠在一起

| 截圖 | 區域 | 量測（「＊」起點） | 每字 |
|---|---|---|---|
| `$FB/xsys4_t15.png`（主視窗名牌） | x360–470, y505–545 | 369、393、417 | **24** |
| `$ORIG/C_game_010.png`（同場景） | 同上 | 374、404、434 | **30** |
| `$FB/xsys4_t10.png`（event 名牌） | x340–460, y560–595 | 358、379、400 | **21** |
| `$ORIG/C_game_007.png` | 同上 | 364、391、418 | **27** |

角色名牌也一樣。`t19` 的「珀爾諾」字與字的黑框相連。`t25`、`t34`、`t37` 的「綺□ 綺□」方框後有空隙，那是缺字，見 §4；`t22` 的同一個空隙露出舊名牌的「爾」。

`t19` 放大後，「珀」左側的黑框比上方和右側細（約 1 px 對 3 px）。推定是字形畫在每字貼圖的 x=0，左側外框被裁掉（未驗證，見 §3 R6）。

### 1.3 主對白視窗（無外框）：雙位元組字距與原版一致

| 截圖 | 區域 | 內容 | 量測 |
|---|---|---|---|
| `$FB/xsys4_t15.png` | x360–720, y634–662 | MSG 32「　也即將揭曉……”」 | 也 386、即 410、將 434/435、揭 459、曉 482 → **24** px；「……」的點在 508、517、526、532、541、550；「”」556–563 |
| `$ORIG/C_game_010.png` | 同句 y638–664 | 同上 | 也 388、即 412、將 436、揭 460、曉 484 → **24** px；點在 509、517、525、533、541、549；「”」557–562 |

- 主視窗的字距與原版相同，差 1–2 px 是重取樣造成的整體位移。
- 「……」的點距：原版 8、8、8、8、8 均勻；xsystem4 是 9、9、**6**、9、9。兩個「…」交界處較擠，這是字形差異（§2.4）。
- 「“」：xsystem4 在 375–382，原版在 377–383。到下一個字的距離兩者都是 4 px，沒有差異。

### 1.4 半形字

- 原版 `C_game_007` event 視窗「內10萬」：「1」起點約 490、「0」502、「萬」516 → 半形每字 **13** px（= 13 + 4 − 4）。
- xsystem4 `t10` 的同一句：數字與「萬」重疊，無法切分。依程式碼計算是 12.5 − 4 = 8.5，繪製時截成 8 px。

---

## 2. 原版正確行為（EXE、AIN、pactex）

### 2.1 字寬函式 0x69c7a0（TextSurfaceManager 的字寬）（已驗證，逐指令）

```
ebx = font            ; +4 type, +8 size, +0x1c bold(float), +0x20 edge(float)
esi = min(ceil(bold), size)          ; 0x69c7ab–0x69c7c7，ceil = 0x4c6f60 → 0x784450
ebx = min(ceil(edge), size); e = max(esi, ebx)                  ; 0x69c7c0–0x69c7dc
if type < 0x100 (GDI):
    if ch 為空: w = 0
    elif 首位元組 ∈ [0x81,0xFE] (或 SJIS 殘留 [0xE0,0xEF]，已被涵蓋): w = size
    else: w = (size+1) >> 1                                       ; 0x69c819–0x69c82a
    *out = w + 2e                                                 ; 0x69c830 lea ecx,[edi+ebx*2]
```

- `0x784450` 是 CRT 的 ceil：正數有小數時加 1.0，常數在 0x7cb470 = 1.0。
- fnl 分支（type ≥ 0x100）與 fnl 載入失敗的退回分支（0x69c86c、0x69c8a3、0x69c964）一樣會加 2e。CN 沒有 .fnl，所以實際走 GDI 分支。
- 繪字 0x69b9d0：
  - 依同一規則，建立寬 size 或 (size+1)>>1 的格子（0x69bad8–0x69bae7）。
  - `TextOutA(hdc, 0, 0, ch, len)` 一次畫一個字（0x69bb70）。
  - 之後依 ceil(太さ)、ceil(縁取り) 做擴張（0x69bb82–0x69bc73 呼叫 0x69b1b0/0x69b290）。
  - **字形的實際寬度不參與排版**；字形超出格子的部分會被格子裁切。

### 2.2 前進量 = 字寬 + 字距

- **parts 文字**（名牌等），繪字函式 0x4fc7e0（已驗證）：
  - 0x4fc826 取 `[info+0x58]` 為字距，0x4fc831 取 `[info+0x5c]` 為行距。
  - 每字取字形貼圖寬（0x5b23c0），畫在 x（0x4fcb58）。
  - 接著 `x += 貼圖寬 + 字距`（0x4fcb99–0x4fcba4）。
  - 換行時 `y += 行高 + 行距`，x 回到起點（0x4fc94c–0x4fc95a）。
- **破折號合併** 0x4fc6c0：n 個「81 5C」的長度 = n × (w + 字距) − 字距，w 取自 0x69da70（= 0x69c7a0）。這也印證「前進 = w + 字距」。（已驗證）
- **訊息視窗**：
  - `SetMessageWindowTextSpace`（HLL case 535 → 0x5959b0）把 LetterSpace 存在視窗 +0x1bc、LineSpace 存在 +0x1c0（已驗證）。
  - 0x4f21fd 把它交給文字元件的 0x5666d0，存在 +0x40。`Parts_SetTextCharSpace`（case 737 → 0x598a80）也呼叫同一個 0x5666d0（已驗證）。
  - 行寬 0x5bcd20 = Σ字寬 + (n−1) × 字距（已驗證，0x5bcde0–0x5bcdee）。
  - 排版 0x5bd0c6–0x5bd0ea 做 `x += 字寬 + [param+0x6c]`（推定 +0x6c 為字距，0x5bc9d0 從 [edi+0x38] 複製而來）。
  - 行高 0x5bce00 = max(偶數化 size + 2e, 各字高)（已驗證）。
- **字形在格子中的位置**：原版字形位於每字貼圖的 (e, e)。這是用截圖推定的（中）：
  - 主視窗名牌原版「＊」比 xsystem4 右移 5 px。
  - event 名牌右移 6 px。
  - 扣掉字形寬差的一半後，剩下約 3 px = e。
  - 貼圖合成 0x69c290 沒有逐指令追。

### 2.3 AIN 端也用同一個字寬（已驗證）

- `CASFont@GetFontWidth(chara)`（fno 20812）呼叫 `TextSurfaceManager.GetFontWidth`，一次傳一個字，連同 BoldWeight 與 EdgeWeight。
- 呼叫者：
  - `parts::detail::TextParts_CalcSize`：寬 = Σ(GetFontWidth + charSpace)
  - `menu::detail::CMenuView@CalcMaxWidth`
  - `infoview::detail::SplitText_Width`
  - `FontProperty@GetRenderedWidth`
  - `PartsHelper::GetFontRenderSize#1`
- 所以原版 AIN 的置中與折行計算也是「字級 + 2e」。

### 2.4 字型

- `CreateFontIndirectA`（0x731640）：
  - `lfCharSet = 0x86`（GB2312）
  - `lfPitchAndFamily = 1`（FIXED_PITCH）
  - 字型名稱是 GBK 編碼的「ＭＳ ゴシック」（0x80fab0）
  - 以上沿用 `$PORT/reports/gbk-20260929/exe.md` §4.2，本組重新確認了 0x69bb70 附近。
- 原版實際畫出的字型，**推定為 MS Gothic**（中，未在 Windows 上驗證）。依據如下：
  - 原版「——」畫成兩段約 11 px 的短線，中間有空隙（`C_game_007` y642–664：588–598、614–622）。MS Gothic 的 U+2014 是半形字形（advance 13/25，ink 0–12）；SimSun 的 U+2014 是滿格（1–23）。
  - 「“」的位置（原版 377–383）符合 MS Gothic（15–22），不符合 SimSun（12–20）。
  - 早先 Wine trace 記錄的是「Requested: MS Gothic charset=134」（`<原版工作檔>/fontdata_test.txt`）。
  - 「……」的點位 SimSun 吻合得更好（3/11/19），MS Gothic 差 1 px。GDI 的 hinting 可能造成 ±1 px，所以這一項不能單獨定案。
- 候選字型的量測：`spacing_tools/glyph_ink.py`。

### 2.5 各視窗與名牌的實際設定（pactex，已驗證）

以 `spacing_tools/pactex_dump.c` 讀取 `dohnadohnaPact.afa`，再用 `pactex_text_styles.py` 整理。

| 元件 | 字級 | 太さ | 縁取り | 字距 | 原版雙位元組 | xsystem4 雙位元組 | 原版單位元組 | xsystem4 單位元組 |
|---|---|---|---|---|---|---|---|---|
| AdvMessageWindow_main／mainB | 25 | 0 | 0 | −1 | **24** | 24 | 12 | 11.5（截成 11） |
| AdvMessageWindow_event | 25 | 0 | 1.5 | −4 | **25** | 21 | 13 | 8.5（截成 8） |
| AdvMessageWindow_plot | 20 | 0 | 1 | −2 | **20** | 18 | 10 | 8 |
| AdvNamePlate（主） | 28 | 0 | 3 | −4 | **30** | 24 | 16 | 10 |
| AdvNamePlate_event | 25 | 0 | 3 | −4 | **27** | 21 | 15 | 8.5 |

- 截圖量得的 24／25／30／27（§1）與上表逐格相同。
- 全遊戲 562 組文字樣式（558 組「文本裝飾」加 4 個訊息視窗）：
  - 現行只有 **288** 組與原版一致。
  - 274 組偏窄，涵蓋 93 個 pactex，包括 StaffList、戰鬥結果、存讀檔、BackLog、ADV 按鈕選單、人材與店鋪畫面等。
  - 偏窄的原因：130 組有外框，174 組有太さ（0.3–1.0 → ceil 後 e=1）。

---

## 3. xsystem4 根因（檔案:行號，HEAD `6400e3c`）

### R1（主因）：外框與粗細沒有計入前進量

- `src/text.c:50` 的 `gfx_text_advance_edges = false`。只有載入 .fnl 時，`src/font_fnl.c:240-241` 才把它設成 true。
  - 上游 xsystem4 在 e68ff81「fnl: adjust for edge width in layout」已經處理 fnl 遊戲。日文版多娜多娜有 fnl，所以會走這條路。
  - CN 沒有 .fnl，於是退回 TTF 且旗標為 false。
- `src/text.c:314-316`：`edge_advance = 0`。
- `src/text.c:259,285-286`：前進 = `glyph->advance × scale + font_spacing`，沒有外框。
- `src/parts/text.c:107-112`：
  - 每字貼圖寬 = `ceilf(text_style_width)` = size + 左右外框，有算外框。
  - 但 `ch->advance` 是 `gfx_render_textf` 的回傳值，沒有外框。
  - `src/parts/render.c:137` 用 `ch->advance` 前進，所以下一個字的外框會蓋到上一個字。
- 就算打開旗標，`text.c:315` 是 `edge_width + ceilf(bold_width)`，edge 沒有取整。1.5 會變成每邊 1.5（合計 3），原版是 ceil 後 2（合計 4）。
- 太さ：`PE_SetFont`（`src/parts/text.c:225`）只把 bold_weight×1000 寫進 `ts.weight`，`bold_width` 維持 0（`src/parts/parts.c:233`）。原版的 e 會取 ceil(太さ)。

### R2：半形寬與截斷

- `src/font_freetype.c:181,187`：半形 = `size/2`（float）；原版 = `(size+1)>>1`（int）。
- `src/parts/render.c:128,137`：`int x; x += ch->advance;` 逐字截斷。25 px 主視窗的半形字是 11.5 → 11，原版 12。
- 分類依據 `is_half_width(Unicode)`（`font_freetype.c:45-57`），原版依位元組數。
  - GB18030 模式下，單位元組一定解碼成 <0x80（0x80、0xFF 解碼失敗時回 '?'），雙位元組一定 ≥0x80。
  - 所以實務上只差在 GBK 拼音字母 A8A1–A8BA（→U+00E0 等，xsystem4 會當半形）。AIN 實測 0 次（`advance_charstats.py`）。

### R3：TextSurfaceManager.GetFontWidth 不含外框

- `src/hll/TextSurfaceManager.c:11-38`：半形 `size/2`、全形 `size`，最後 `(int)`，完全不看 bold 與 edge。
- AIN 的 `TextParts_CalcSize` 等（§2.3）因此算出偏小的寬度，置中與折行都會跟原版不同（推定，未做 GUI 對照）。

### R4（次要，中）：行寬含尾端字距

- `src/parts/text.c:112`：`line->width += ch->advance`，最後一個字也加了字距。
- 原版訊息視窗行寬是 Σw + (n−1)·字距（0x5bcd20）。
- 字距為負時，xsystem4 的 parts 尺寸會小一個字距（例如 4 px），影響置中或右對齊。原版 parts 文字路徑的尺寸計算未逐指令確認。

### R5（非根因）：字型字形差異

- VL Gothic 的「…」點距是 9 px（@25），原版 8 px。
- VL Gothic 的 U+2014 滿格（原版推定半形）。
- VL Gothic 的“”寬 8 px（原版 7）。
- 這些只會造成 ±1–3 px 的觀感差，不影響前進量。

### R6（未驗證）：字形在貼圖中沒有偏移 e

- `src/parts/text.c:105` 算了 `ch->off`，但 `src/parts/render.c:134` 沒有使用。
- `gfx_render_textf(&ch->t, 0, 0, …)`（`:110`）在旗標為 false 時把字形畫在 x=0，左側外框可能被貼圖左緣裁掉（見 §1.2 的「珀」）。
- 打開 R1 的前進修正後，`_gfx_render_text` 會先 `pos_x += edge_spacing`（`text.c:259`），水平方向會自然偏移。
- 垂直方向（原版推定是 y=e）沒有檢查。

---

## 4. 缺字 fallback 是否影響字距

- 現況（已驗證）：
  - VL Gothic 沒有 U+83C8。`FT_Load_Char` 載入 glyph 0，也就是 `.notdef`：advance 432/1000，ink 在 28 px 下約 1–9 px。
  - 但前進量來自 `font_freetype.c:181`（碼點分類），仍是全形，所以「綺□」後面空 14–19 px。
  - 這個空隙在名牌殘影時會露出舊名牌的字（例：「綺□爾綺□」，見 `ain.md` §0.1）。
- 如果在 `ft_font_get_glyph`（`font_freetype.c:137-183`）裡，對 `FT_Get_Char_Index==0` 的字改用另一個 FT_Face 載入字形：
  - 只要 `glyph->advance`、`ft_font_size_char` 和貼圖寬（`text_style_width` → `gfx_size_char`）繼續依碼點或位元組決定，**字距完全不變**。這是本組的建議做法。
  - fallback 字形會畫進同一個格子。例：HanaMinA 的菈在 28 px 下 ink 1–26，會補滿空隙。
  - 垂直位置用各自 face 的 bitmap_top，基線對齊，可能有 ±1–2 px 差（未驗證）。
- **會影響字距的做法（避免）**：
  - 用 fallback 字型的 glyph advance（`FT_Get_Advance`／`size_char_kerning`）當前進量。HanaMinA 的 U+201C/D advance 是 13/25，MS Gothic 的 U+2014 是 13/25。
  - MSG1 中「“」「”」各 27,640 次，會真的變成半形擠在一起。
- 整套換成繁中字型（例如 `--font-gothic` 指定台灣字型）：
  - 前進量不變。
  - 但台灣字型把「，。“”」置中在格子裡，和原版（日式，逗號句號靠左、引號貼字）的位置不同，觀感會改變。
  - 保留 VL Gothic、只對缺字 fallback，最接近原版。缺字統計見 `$PORT/reports/gbk-20260929/ain.md` §5（107 字、影響 11.4% 句子）。

---

## 5. 建議修法與影響範圍

修改都在 `$WT`，由負責修改的代理執行，本組沒有動。

另一個代理正在改字串程式碼。`src/text.c` 與 `src/parts/text.c` 在 `6400e3c` 剛因 GBK 規則改過，建議等那批合入後再改，並共用同一個 GBK 位元組判斷。

### 修法 A（最小，一行）

- 在 `gfx_font_init`（`src/text.c:78-97`）中，於 `ain_is_gb18030 && !config.fnl_path` 時設 `gfx_text_advance_edges = true`。
- 理由：上游對日文版（fnl）本來就開這個旗標；原版 EXE 的 fnl 與 GDI 兩條分支都加 2e。
- 效果：
  - 名牌（外框 3）、plot（1）完全對齊原版。
  - event 視窗 21 → 24（原版 25，差 1）。
  - 太さ 樣式仍偏窄。
  - 562 組樣式中，一致的從 288 組增加到 360 組。

### 修法 B（完整，建議）

- 在 CN 且無 fnl 的「GDI 規則」下，依原版公式計算：
  - `e = max(min(ceilf(bold), size), min(ceilf(edge), size))`
  - 雙位元組 `w = size`，單位元組 `w = (size+1)/2`（整數）
  - 字形畫在 x = e
  - 前進 = `w + 2e + 字距`
- 改動點：
  - `src/text.c:314-316`：`edge_advance` 在此模式改成 e（ceil、取 max，不是相加）。bold 從 `ts->bold_width` 取；parts 文字從 `ts->weight/1000` 還原 PE_SetFont 的 bold_weight，或讓 `PE_SetFont` 同時保存 bold。
  - `src/font_freetype.c:181,187`（或在 `_gfx_render_text` 依 `text.c:263` 的位元組數判斷）：半形改 `(size+1)/2`。
  - `src/hll/TextSurfaceManager.c:11-38`：
    - 改成 0x69c7a0 的公式（`w + 2e`），用到 `bold_weight`、`edge_weight`。
    - 原版只看第一個字；AIN 都只傳一個字，所以逐字相加也可以。
  - （可選，先驗證）`src/parts/text.c:112`：行寬去掉最後一個字距。
- 效果：562 組全部與原版一致（`pactex_text_styles.py` 的公式模擬）。

### 修法 C（字型，獨立）

- 保留 VL Gothic，在 `ft_font_get_glyph` 對缺字 fallback，前進量不改，見 §4。
- 「……」「——」的字形差異要靠換字型才能消除，屬於視覺選擇，交給使用者決定。字型檔不可放進 repo。

### 影響範圍

- 走 `_gfx_render_text` 的所有 CN 文字都會改變：
  - 訊息視窗：event、plot 變寬；main 只有半形變寬。
  - 名牌。
  - 93 個 pactex 中的 274 組 UI 文字樣式。
- 主對白的雙位元組字距不變。
- parts 尺寸變大 → AIN 置中（AnimateText／AdjustPos、TextParts_CalcSize）會一起改變，這與原版一致。
- 訊息視窗沒有自動折行，只靠劇本的換行。原版的寬度本來就這麼寬，不會超出。
- 風險：修法 A 是全域旗標，也會影響 SACT2／DrawGraph 等路徑。v14 CN 沒有使用這些路徑（推定），日文版上游行為也一樣。

---

## 6. 驗證方法

### 6.1 headless fixture（建議新增，不需 GUI）

- 仿照 `docs/checkpoints/2026-09-09/evidence/stage2/validation/message_window_fixture.c` 的編譯方式，把 `src/text.c`、`src/font_freetype.c`、`src/parts/text.c` 連進 fixture。
- 以 stub 取代 `gfx_init_texture_*`、`gfx_draw_glyph*`、`gfx_delete_texture`、`parts_set_dims`。
- 載入 `fonts/VL-Gothic-Regular.ttf`，並設 `ain_is_gb18030 = true`。
- 用 §2.5 的五組樣式，對以下 GBK 位元組字串呼叫 `parts_text_append`，再斷言每個 `ch->advance`：
  - 「『下面插播一段廣告，」
  - 「10萬」
  - 「綺菈綺菈」
  - 「……”」

| 樣式 | 雙位元組 | 單位元組 | 現行（應失敗） |
|---|---|---|---|
| main | 24 | 12 | 24／11.5 |
| event | 25 | 13 | 21／8.5 |
| plot | 20 | 10 | 18／8 |
| NamePlate | 30 | 16 | 24／10 |
| NamePlate_event | 27 | 15 | 21／8.5 |

- 其他斷言：
  - 「菈」在 fallback 前後的 advance 都等於雙位元組值。
  - 行寬 = Σw + (n−1)·字距（若採用 R4）。
  - `TextSurfaceManager_GetFontWidth`：
    - (「萬」, 25, bold 0, edge 1.5) = 29；(「1」, 同) = 17
    - (「萬」, 25, 0, 0) = 25；(「1」, 同) = 13
    - (「綺」, 28, 0, 3) = 34
- 公式側可以先用 `spacing_tools/pactex_text_styles.py` 對照（它已經印出原版值與現行值）。

### 6.2 GUI 比對

- 用 `save-fixes2-gui` 相同的 run 設定重跑，再執行 `python3 spacing_tools/measure_spacing.py`。
- 預期：
  - `t04` MSG 5 第 2 行 20 格的字距為 25：「朗」約 387、「像」終點約 858–860（原版 858）。
  - event 名牌「＊」起點相距 27；主視窗名牌相距 30。
  - `t15` 主視窗仍是 24，「……」點位不變（只有換字型才會變）。
  - `t10`「10萬」的數字各前進 13，不再與「萬」重疊。
  - 加上 `--crops <scratch 目錄>` 可以輸出放大比對圖。圖片含遊戲素材，只放 scratch，勿提交。

---

## 7. 信心度

| 項目 | 信心 |
|---|---|
| 原版字寬 = (DBCS ? size : (size+1)>>1) + 2·max(ceil 太さ, ceil 縁取り) | 高（逐指令，0x69c7a0、0x784450） |
| parts 文字前進 = 字寬 + 字距 | 高（0x4fcb99–0x4fcba4、0x4fc6c0） |
| 訊息視窗前進 = 字寬 + 字距；行寬 Σw+(n−1)·字距 | 高（0x5bcd20 逐指令）／中（0x5bd0de 的 +0x6c 對應）；截圖 24/25 逐格吻合 |
| xsystem4 少算 2e（R1），各元件數值 | 高（程式碼加截圖 21/24/21 逐格吻合） |
| 半形 size/2 與 int 截斷（R2） | 高（程式碼）；GUI 只有原版側的 13 px 量測 |
| 寬度不明確標點沒有被當半形 | 高（程式碼加 AIN 全量統計 0 筆） |
| 原版字型為 MS Gothic | 推定（中），未在 Windows 驗證 |
| 原版字形位於 (e,e)；xsystem4 左外框被裁 | 推定（中）／未驗證 |
| 缺字 fallback 不改字距（前進依碼點） | 高（程式碼結構）；fallback 字形的垂直位置未驗證 |
| TextSurfaceManager 偏小導致 AIN 置中／折行差異 | 推定（程式碼層級已驗證，GUI 未對照） |

---

## 附錄：工具（`$PORT/reports/gui-visual-20260929/spacing_tools/`，全部只讀）

| 檔案 | 用途 | 執行 |
|---|---|---|
| `measure_spacing.py` | 量測 xsystem4 截圖與原版截圖的字元起點（§1） | `python3 measure_spacing.py [--crops DIR]` |
| `pactex_dump.c` | 用 libsys4 讀 `dohnadohnaPact.afa` 的 pactex 樹 | 編譯指令在檔頭；`pactex_dump Pact.afa --all > all.raw` |
| `pactex_text_styles.py` | 列出所有文字樣式與訊息視窗的字級、外框、字距，計算原版與現行前進量（§2.5） | `python3 pactex_text_styles.py all.raw` |
| `advance_charstats.py` | AIN MSG1／STR0 的前進規則不一致統計、半形字與寬度不明確標點次數 | `python3 advance_charstats.py dohnadohna.ain` |
| `glyph_ink.py` | 候選字型（VL Gothic、HanaMinA、MS Gothic、SimSun）標點字形的 ink 位置（§2.4） | `python3 glyph_ink.py` |

EXE 反組譯沿用 `$PORT/reports/gbk-20260929/scripts/pe.py` 載入器（capstone，`/opt/homebrew/bin/python3.14`）。
