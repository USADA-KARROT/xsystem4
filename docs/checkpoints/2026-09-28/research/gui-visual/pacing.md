# 流暢度：原版的 60 fps 限速、每幀一次推進與一次呈現（2026-09-30）

修正 commit：`44964f9`（接在 `f489d23` 之後，本機 commit，尚未推送）。原版 EXE 只做靜態反組譯（capstone），沒有執行。
證據等級：**已驗證**＝逐指令或執行結果直接佐證；**推定**＝靜態推論合理，但沒有逐指令或執行期確認；**未驗證**＝尚無證據。
路徑記號：`$PORT`＝`<PORT>`。量測用的臨時計時碼、執行紀錄與 framebuffer 都在 repo 外（`$PORT/reports/pacing-20260930/`），不提交。修正前的診斷（量測方法、長幀分類、A–I 各組執行）在 `$PORT/reports/pacing-20260929/`。

---

## 0. 結論

1. 使用者回報的「說不出的卡頓」有三個來源（2026-09-29 診斷）：沒有限速（邏輯迴圈 460–480 Hz、每個邏輯幀最多呈現三次、fps 在 120–520 間跳動）、元件引擎的時間每幀推進兩次、測試模式的 framebuffer 截圖在主執行緒壓 PNG（每張 174–220 ms）。另有換句時的內容突刺。
2. 本組照原版修正前三項，並處理換句突刺中的一項：
   - **限速**：照原版 `0x4676f0` 的算式實作（毫秒計時、16.666666 ms、保留截斷的小數），照 `0x4c5450` 的順序在 `SystemService.UpdateView` 等待後呈現一次；`ConfigOverFrameRateSleep`、`SleepByInactiveWindow`、略過已讀訊息時的跳幀都照原版與遊戲的設定值。
   - **一幀只推進一次**：元件時間只在 `PartsEngine.UpdateComponent`（遊戲自己的 passedTime）推進，`UpdateView` 不再推進；`system.Peek` 在 `UpdateView` 呈現時不再呈現。
   - **截圖**：讀回留在主執行緒，PNG 壓縮與寫檔移到背景執行緒；檔名、張數、間隔、位元組內容不變。
   - **對白文字**：`SetMessageWindowText` 不再每次都重新排版整段文字，改在繪製時排一次。
3. 結果（同條件前後各兩輪，§5）：
   - 一般遊玩：呈現 321–324 次／秒（浮動）→ 固定 58.7；AIN 時間倍率 0.744→0.97；元件時間倍率 1.67–1.70→0.97；呈現間隔標準差 3.6→1.0 ms；超過 33 ms 的間隔 17–18→7–8。
   - 測試模式：超過 50 ms 的間隔 40–42→2–3，有這種間隔的 5 秒時段 16–17/30→2/30，最長 241–253→72–75 ms，AIN 時間倍率 0.71→0.97。
   - MSG 88（一般遊玩 83→86）、assertion 0、堆疊溢位 0。測試模式在約 80 秒進入據點並顯示底列（修正前約 125 秒才講完開場 88 句）。（已驗證：GUI 量測）
4. fps 是 58.7 而不是 60：原版的算式在「睡眠結束於 1 ms 計時刻度」的平台上每幀是 17 個刻度（§1.4）。這是照原版算式的結果，不是誤差；原版在 Windows 上實際多少 fps 未驗證。
5. 沒修的：立繪 DCF 重複解碼、`heap_grow`、`SetWindowSetting` type 2 的對應、vsync、`PE_Update` 的零時間後備（§6、§7，HANDOFF 第 4 項）。

---

## 1. 原版語義

### 1.1 每幀的流程（AIN `view::detail::View_Update`）

`CalcPassedTime`（passedTime 限 0–50、scaled 限 0–150）→ …→ `parts::detail::Update`（內含唯一一處 `PartsEngine.UpdateComponent`）→ `time::detail::PlayTime_Update` → `_system::detail::ExecuteIntervals` → 若 `advengine::detail::IsSkip()` 為假：`ChipmunkSpriteEngine.TRANS_Update`（轉場中）或 `ChipmunkSpriteEngine.Update`，然後 `SystemService.UpdateView` → `system.Peek` → `parts::detail::UpdateMessage` → …。整個 AIN 只有這裡呼叫 `UpdateView`、`ChipmunkSpriteEngine.Update` 與 `UpdateComponent`。（已驗證：AIN）

設定預設值：`CASConfigData` 的 `SleepByInactiveWindow = 0`、`ConfigOverFrameRateSleep = 1`；`_system::detail::Init` 以 `SYSTEM_SetConfigOverFrameRateSleep`、`SYSTEM_SetConfigSleepByInactiveWindow`、`SYSTEM_SetConfigFrameSkipWhileMessageSkip(IsFrameSkipMode())` 套用；`View_Update` 每幀以 `SYSTEM_SetReadMessageSkipping` 設定是否在略過已讀訊息。設定畫面可以改前兩項。（已驗證：AIN）

### 1.2 `SystemService.UpdateView`（case 6 → `0x4c5450`）

```
if (engine+0x4c /* SleepByInactiveWindow */ && !window->IsActive())   // vtable +0x40
    Sleep(50);
if (!limiter(engine+0x38))   // 0x4676f0
    Sleep(1);
device->Present();           // vtable +0x7c；0x6e3190 以 SyncInterval=[+0xc4]!=0 呼叫，推定為 0
```
（已驗證：逐指令；`IsActive` 與 `Present` 的名稱為推定）

### 1.3 限速器 `0x4676f0`

限速器物件在 Chipmunk 引擎 `+0x38`：`+0x40` 計時器、`+0x44` 上次時間、`+0x48` 累積值；旗標 `+0x4d` OverFrameRateSleep、`+0x54` 正在略過已讀、`+0x55` 略過時跳幀、`+0x56` 取消跳幀。建構子 `0x467eb0` 設 `+0x4c = 0`、`+0x4d = 1`、`+0x55 = 1`、跳幀上限 `+0x5c = 10`。

```
if (!OverFrameRateSleep) return false;
if (!Invalidate && FrameSkipCfg && ReadSkipping) return false;   // 不更新上次時間
used = (float)(double)(uint32)(timer() - last) + carry;           // 原版先把 used 存回 +0x48
if (16.666666f > used) {                                          // 常數 0x8132ec
    ms = __ftoi(16.666666f - used);                                // 截斷（0x785120）
    Sleep(ms);                                                     // 0x7bd288
    carry = 16.666666f - ((float)(double)(uint32)ms + used);       // 截掉的小數
    last = timer(); return true;
}
carry = 0; last = timer(); return false;
```
（已驗證：逐指令）。計時器的預設實作是 `timeGetTime`（毫秒，`0x467ab0`）；限速器用的 `+0x40` 在建構子設為 null、之後指派，是否同一實作為推定。

注意保留的小數是**加到**下一幀已用的時間上：下一幀會少睡這段小數。這讓「精確睡眠」時的平均週期是 16.0 ms（62.5 fps），不是 16.67 ms。

### 1.4 Sleep 的精度與實際 fps

`0x41b360` 以 `timeBeginPeriod(最小值)` 把計時精度設為 1 ms（已驗證）。Windows 的 `Sleep(n)` 在到期時間之後的第一個計時中斷醒來（推定，依 Windows 計時器的設計，未在 Windows 上量測），`timeGetTime` 在同一個中斷前進，所以睡醒時讀到的正好是刻度值。在這個模型下，工作時間 w（不超過 16 ms）時：`used = floor(w) + carry`，`ms = 16 - floor(w)`（carry 為 .666666 時）或同值（carry 為 0 時），醒來的刻度是 `ceil(w + ms) = 17`，每幀固定 17 ms，約 58.8 fps。若 Windows 的計時中斷是 0.9765625 ms（1024 Hz），17 個刻度是 16.6 ms，約 60 fps。原版在 Windows 上的實際 fps 未驗證。

macOS 的 `SDL_Delay`／`nanosleep` 沒有這個刻度，而且常晚醒：本機實測 `SDL_Delay(13)` 平均晚 4.8–5.8 ms（背景行程、螢幕休眠時）。2026-09-29 診斷的原型 PACE=1（原版算式 + `SDL_Delay`）在 GUI 得到 58.0 fps、p95 18.3 ms。

---

## 2. xsystem4 修正前（`f489d23`）

| 位置 | 行為 | 結果 |
|---|---|---|
| `PartsEngine.UpdateComponent`（v14） | 推進元件時間，距上次呈現 ≥16 ms 就 `scene_render` + `gfx_swap` | 每幀可能呈現 |
| `ChipmunkSpriteEngine.Update` | 場景 dirty 就畫並呈現 | 同上 |
| `SystemService.UpdateView` | 用自己的時鐘**再推進一次**元件時間；≥16 ms 就畫並呈現，否則 `SDL_Delay(1)` | 元件時間 1.67–1.81 倍（本組量測）；無限速 |
| `system.Peek` | 每次都畫並呈現（v14 移植時加的，上游只處理事件） | 每幀多一次呈現 |
| `gfx_swap` 的截圖 | 主執行緒讀回並壓 PNG | 每張 174–220 ms 的停頓 |
| `SetMessageWindowText` | 每次都重新排版整段文字（每字一張貼圖） | 每句約 3.5 次、每次約 1 ms（最長 5 ms）。診斷報告寫「一幀 120–167 次」，本組以臨時計時只量到每句 3–4 次 |

AIN 的 passedTime 以毫秒計，每幀在 `CalcPassedTime` 的讀取之間丟掉一點時間（推定），幀越短丟越多：無限速時只累積到實際時間的 0.71–0.74 倍，AIN 的動畫慢 26–29% 且隨 fps 浮動。元件引擎另有 `PE_Update` 的「passedTime 為 0 就改用實際時鐘」後備，修正前 150 秒內觸發 1,033–2,865 次。

---

## 3. 修正

| 檔案 | 內容 |
|---|---|
| `src/frame_pacing.c`、`include/frame_pacing.h`（新） | `frame_pacing_limiter_step`：原版 `0x4676f0` 的算式（同樣的 float 運算與截斷）。`frame_pacing_limit`：旗標、以 `SDL_GetTicks` 當毫秒計時器、睡眠。`frame_pacing_sleep(n)`：1 ms 刻度的 Sleep，到 `SDL_GetTicks` 前進 n+1 時返回（到期後的第一個刻度）；先睡一半剩餘時間、最後 2 ms 以 0.2 ms 輪詢，避免 macOS 晚醒。`frame_pacing_wait`：`0x4c5450` 的 Sleep(50)、限速、Sleep(1)。`frame_pacing_draw_frame`：`0x468a10` 的略過時十幀畫一幀，不畫的幀重設限速器。呈現歸屬：`UpdateView` 呼叫後由它呈現；兩次 `UpdateComponent` 之間沒有 `UpdateView`（ADV 略過）或 500 ms 沒有 `UpdateView` 時，交回舊的呈現路徑 |
| `SystemService.UpdateView` | 本幀已由 `UpdateComponent` 推進就不再推進（沒有時才用自己的時鐘推進，保留給不呼叫 `UpdateComponent` 的情形）；本幀已由 `ChipmunkSpriteEngine.Update`／`TRANS_Update` 畫好就不再畫；`glFlush` → 等待 → 呈現一次；略過時不畫的幀不呈現 |
| `ChipmunkSpriteEngine.Update`（`sact_Update`） | `UpdateView` 呈現時：照原版在這裡畫（或依跳幀略過），不呈現 |
| `TRANS_Update` | `UpdateView` 呈現時：畫場景與轉場（`effect_render`），不呈現 |
| `PartsEngine.UpdateComponent`（v14） | 推進後標記本幀已推進；`UpdateView` 呈現時不再自己呈現（其餘情形保留每 16 ms 呈現） |
| `system.Peek` | `UpdateView` 呈現時只處理事件 |
| `video.c` | 截圖：讀回後交給背景執行緒寫檔（`gfx_write_pixels_async`，一次一張、依序）。原本寫「結束時等待」不正確：引擎一律經 `sys_exit`（`_exit`）結束，`atexit` 不會執行，`fb28975` 才在 `vm_exit`、VM 錯誤、`sys_error` handler 與 `main` 結尾明確等待（審查指出，見 base-ui.md §10.5）；`gfx_save_texture` 拆出 `gfx_write_pixels`。`STAGE2_PERF` 每 5 秒另印超過 33／50 ms 的間隔數（`gt33`／`gt50`）。vsync 開啟時限速器不睡（呈現本身已等待；原版不開 vsync） |
| `parts/message_window.c` | 文字、字型、字距改變時只標記，繪製時才排版一次（排版結果只有繪製會讀） |

沒有為單一畫面加特例。限速、跳幀與非作用中視窗的行為都跟隨遊戲自己的設定值。

---

## 4. Headless 驗證（`frame-pacing`）

新模式 `frame-pacing`（`harness/probe/frame_pacing_fixture.inc`），五個案例各自 fork：

| 案例 | 內容 |
|---|---|
| FP1 | `frame_pacing_limiter_step` 對六組固定輸入、32 位元計時器回繞、以及與獨立寫出的參考算式逐位元比對 20,000 步；模擬 2.3–11.3 ms 的工作：刻度式 Sleep 每幀正好 17.000 ms，精確 Sleep 平均 16.000 ms |
| FP2 | 實際時鐘：`Sleep(n)` 在第 n+1 個毫秒刻度返回；經真 AIN 的 `ChipmunkSpriteEngine.SYSTEM_Set*` 宣告：限速時約 17 ms／幀，OverFrameRateSleep 關閉或略過中只 Sleep(1)，略過中 30 幀畫 3 幀、不畫的幀重設限速器，取消跳幀或關閉跳幀時限速器照常，`SleepByInactiveWindow` 開啟時（探針沒有視窗＝非作用中）等 50 ms |
| FP3 | 呈現歸屬：`UpdateView` 第一次呼叫後接手；兩次 `UpdateComponent` 之間沒有 `UpdateView`、或 500 ms 沒有 `UpdateView` 時交回 |
| FP4 | 經真 AIN 宣告的 `UpdateComponent(16)` → `ChipmunkSpriteEngine.Update` → `SystemService.UpdateView`，幀間隔 40 ms：Alpha motion 每幀只前進 16（16、32）；沒有 `UpdateComponent` 的幀由 `UpdateView` 以自己的時鐘前進（約 +40）。用略過中的幀，所以不需要 GL |
| FP5 | 背景寫檔的呼叫約 0.13 ms（同步寫檔約 27 ms），三張 PNG 與同步寫出的逐位元組相同，依序完成 |

| 版本 | 結果 |
|---|---|
| `f489d23`（`before-check.sh`） | 5/5 失敗：新函式都不存在（[輸出](pacing/frame-pacing-before-f489d23.txt)）。修正前的 `UpdateComponent` 與 `UpdateView` 每 16 ms 會繪製，探針沒有 GL，所以行為面的比較由 GUI 量測負責（修正前元件時間倍率 1.67–1.81） |
| `44964f9` | 5/5 通過（[輸出](pacing/frame-pacing-after-44964f9.txt)） |

`verify-step.sh`：51 個模式在預設與 `XS4_PROBE_GBK=1` 兩種組態都 `VERDICT PASS`，sanitizer 0（[摘要](pacing/verify-summary.txt)）。

---

## 5. GUI 量測

同一台機器、同一個遊戲工作副本，修正前（`f489d23`）與修正後的原始碼各自加上同一份臨時計時碼（每次呈現記一行、每 5 秒記 AIN 與元件時間的總和，不提交），交錯執行，每次 150 秒、`--skip-title`：

- **測試模式**：與 `gui-run.sh` 相同（按住 Return、每 1.2 秒點畫面中央、每 2 秒截 framebuffer、`--echo-message`）。
- **一般遊玩**：關閉 `--echo-message`、不截圖、不按住按鍵、每 3 秒點一次。

| 執行 | MSG | 呈現/秒 | p50 ms | p95 ms | 標準差 | >33 ms | >50 ms | 最長 ms | >50 ms 時段 | AIN 時間倍率 | 元件時間倍率 | 零時間後備 | 峰值 RSS MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 測試 修正前 #1 | 88 | 408.6 | 1.87 | 7.86 | 5.63 | 60 | 42 | 253.0 | 17/30 | 0.715 | 1.8 | 2433 | 1028 |
| 測試 修正前 #2 | 88 | 416.6 | 1.84 | 7.56 | 5.53 | 56 | 40 | 240.6 | 16/30 | 0.712 | 1.81 | 2865 | 1352 |
| 測試 修正後 #1 | 88 | 58.7 | 17.0 | 17.29 | 1.22 | 12 | 2 | 71.6 | 2/30 | 0.971 | 0.971 | 0 | 621 |
| 測試 修正後 #2 | 88 | 58.7 | 17.0 | 17.29 | 1.3 | 14 | 3 | 74.5 | 2/30 | 0.971 | 0.971 | 0 | 622 |
| 一般 修正前 #1 | 83 | 324.1 | 2.12 | 12.76 | 3.57 | 18 | 0 | 50.0 | 0/30 | 0.744 | 1.703 | 1384 | 1143 |
| 一般 修正前 #2 | 83 | 320.8 | 2.11 | 13.28 | 3.68 | 17 | 1 | 52.9 | 1/30 | 0.744 | 1.672 | 1033 | 1254 |
| 一般 修正後 #1 | 86 | 58.7 | 17.0 | 17.29 | 0.98 | 8 | 1 | 52.8 | 1/30 | 0.972 | 0.972 | 0 | 516 |
| 一般 修正後 #2 | 86 | 58.7 | 17.0 | 17.3 | 0.99 | 7 | 1 | 52.5 | 1/30 | 0.971 | 0.971 | 0 | 543 |

「修正後 #1」不含訊息視窗延後排版，「#2」是 `44964f9`。原始表與 `STAGE2_PERF` 交叉檢查見 [pacing/gui-measure.md](pacing/gui-measure.md)。

- fps、p50／p95／p99、標準差、長幀數與最長幀都以每次呈現的間隔計算；「>50 ms 時段」是 5 秒時段中出現超過 50 ms 間隔的個數。
- AIN 時間倍率＝`UpdateComponent` 收到的 passedTime 總和÷實際時間；元件時間倍率＝`PE_Update` 推進的時間總和÷實際時間。
- 修正前 p95 較小是因為大部分間隔只有 1–2 ms；分布的寬度看標準差與長幀數。
- 修正後超過 33 ms 的間隔大多是 33.2–35 ms（一幀的工作超過一個週期）；剩下超過 50 ms 的有 1–3 個，推定是換句時的 CG 解碼等內容突刺（§7，未逐一歸因）。40 張截圖不再造成長幀。
- 對白推進較快：測試模式修正前約 125 秒講完開場 88 句，修正後約 78 秒（按住 Return 時的推進依遊戲時間，修正前遊戲時間只有實際的 0.71 倍）。一般遊玩 150 秒內 83→86 句。
- 峰值 RSS 修正後較低（1.0–1.4 GB → 0.5–0.6 GB），推定是每秒執行的邏輯幀少了八成、暫時配置跟著減少；沒有另外追查。
- 環境：Apple Silicon Mac mini；量測期間螢幕休眠並鎖定，另有一個 Wine 行程佔用約 110% CPU（前後相同）。swap 時間與實際亮螢幕時的表現未驗證。

**標題畫面與據點**（修正後，framebuffer 目視；圖留在 repo 外）：
- 不加 `--skip-title` 跑 40 秒：AliceSoft 標誌、警告畫面、標題選單依序出現，與修正前相同；淡出的中間色不同（淡入淡出速度改變）。
- 測試模式第 39 張（約 80 秒）：據點畫面，底列（存檔、讀取、物品、下一步、ToDo）、Day 1、金錢計數中、三張 FEEL 貼紙、JUDIAN、「據點環節」橫幅，背景可見。
- 延後截圖（82–162 秒）：之後一直停在教學覆蓋層「據點環節的基本要點」（測試腳本只點畫面中央，不會按教學的按鈕），框內縮小的據點畫面與底列都在，框內背景是黑的（`6d39915` 已知問題）。
- 一般遊玩加截圖 84 秒：對白文字逐句正常顯示，與修正前同一句的畫面目視一致；修正後 #1 與 #2 在 6 張有文字的截圖上，對白區域逐像素相同。

**`gui-run.sh`**（`44964f9` 的 optimized build，150 秒，無臨時計時碼）：MSG 88、assertion 0、堆疊溢位 0、UNIMPL 0、framebuffer 40 張；`STAGE2_PERF` 29 個時段平均 58.6 fps、p95 中位數 17.29 ms、最長 74.2 ms、超過 33／50 ms 的間隔 17／3 個（2 個時段）、vsync 0；第 39 張（約 80 秒）是據點畫面與底列。

---

## 6. 與原版的差異（刻意保留或尚未確認）

- **vsync**：xsystem4 開啟 vsync 時（命令列或 `SetWindowSetting`）限速器不睡，避免雙重等待。原版推定不開 vsync（`0x6e3190` 的 SyncInterval 旗標沒有寫 1 的地方）。
- **非作用中視窗**：以 SDL 的輸入焦點代表原版的 `IsActive`（推定）。預設設定不睡。
- **略過中不畫的幀**不呈現；原版照樣 Present（內容是上一次畫的）。畫面相同，只少了呈現次數。
- **沒有 `UpdateView` 的情形**（ADV 略過、500 ms 沒有 `UpdateView`）沿用舊路徑：`UpdateComponent` 每 16 ms 呈現、`system.Peek` 每次呈現，不限速。原版在 ADV 略過時不畫也不呈現（`View_Update` 跳過 `ChipmunkSpriteEngine.Update` 與 `UpdateView`）；舊路徑是為了不呼叫 `UpdateView` 的迴圈保留的，沒有改。
- **`PE_Update` 的零時間後備**（v14 且 passedTime 為 0 時改用實際時鐘，距上次後備的時間）沒有改。修正後四次量測都是 0 次觸發（修正前 1,033–2,865 次），但在 ADV 略過等無限速的情形仍會觸發，且它的時間起點是上一次觸發而不是上一幀，可能重複計時。原版沒有這個後備；要不要移除需要先確認沒有場景依賴它。
- 轉場（`TRANS_Update`）現在每幀都先畫場景再畫轉場（修正前只在 dirty 時畫，但 `UpdateView` 每 16 ms 也會畫）。開場的淡入淡出目視正常；與修正前的逐幀比對未做（推進速度不同，同編號的截圖不是同一時點）。

---

## 7. 沒有處理的突刺（HANDOFF 第 4 項）

- **立繪 DCF 重複解碼**：換表情時 libsys4 的 `dcf_extract` 每次都重新解碼底圖（1481×1935，約 21–25 ms），150 秒內 CG 解碼 200 次、只有 80 張不同，重複解碼共 291 ms（診斷 D4）。底圖快取要改 libsys4（`dcf_get_base_cg`），本組不改 submodule；在 xsystem4 端可以對「名稱→解碼後的 cg」做 LRU（每張 11.5 MB），或把解碼移到背景執行緒（`SetPartsCG` 的同步語義會變）。
- **`heap_grow`**：一次觸碰全部新 slot（約 21 ms，偶發）。
- **`SetWindowSetting` type 2**：AIN 的 type 2 是「全螢幕失焦時最小化」，xsystem4 當成 `WAIT_VSYNC`。原本寫「在設定畫面切換會打開 vsync」沒有 AIN 證據：審查者靜態確認 type 2 唯一的呼叫者 `config::detail::SetMinimizeByFullScreenInactive` 只被 `AFL_Config_SetMinimizeByFullScreenInactive` 呼叫，而後者在 AIN 內沒有呼叫者，推定遊戲不會打開 vsync，只有 WindowSetting.json 的 `wait_vsync` 能開。SDL 2.32 的 macOS vsync 在螢幕休眠時會卡住（診斷 E/E2：一次 swap 阻塞 159 秒）。依 AIN 函式名推定，未反組譯確認。
- 其他：事件處理偶發 23.8 ms、貼圖上傳。

---

## 8. 未驗證

- 原版在 Windows 上的實際 fps（§1.4 的刻度模型是推定）。
- 限速器計時器 `+0x40` 是 `timeGetTime`（推定）。
- 亮螢幕、前景視窗時的 swap 與 macOS 晚醒程度；App Nap 的影響。
- 60 Hz 顯示器上實際看到的平順度（58.8 fps 無 vsync，每秒約 1.2 次同一畫面停留兩個更新週期，推定）。
- 設定畫面切換「超過幀率時休眠」「非作用中時休眠」的畫面操作。
- 影片播放、`SYS_PEEK` 其他呼叫點（遊戲結束、`BeginWaitMessage`）、跳過已讀訊息（Ctrl）時的畫面。
- 訊息視窗延後排版：開場 88 句正常顯示；backlog 與其他使用訊息視窗的畫面沒有逐一確認。
