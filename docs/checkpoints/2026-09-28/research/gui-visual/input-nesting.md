# v14 子畫面返回後的輸入恢復

基準：`a6a8f5b`；修正：`a870409`。本組修正同步子畫面返回後無法繼續操作的問題，已完成兩組 58 模式、修正前對照、150 秒 GUI、Wine 同操作影格對照及獨立審查。

## 根因

標題的「成就」「讀取」與據點的系統選單，會在點擊 callback 內同步執行另一個 `SceneContext@Join`／`WaitForClick`。外層尚未結束，內層就再次呼叫 `BeginInput`。返回時，舊 `PE_EndInput` 把全域 `parts_began_click` 一律清成 false，連外層正在等候的輸入也關掉。

AIN 的 `parts::detail::WaitForClick`（FUNC 9196）在迴圈前呼叫一次 BeginInput，迴圈起點是 `0x2ec158`；離開時在 `0x2ec2a2` 呼叫 EndInput，並清除結束旗標。內層同時清掉點擊暫存與結束旗標後，外層繼續既有迴圈，不會再次經過 BeginInput。新點擊因此永遠進不了派送。

這不是遊戲腳本漏掉 BeginInput。原版使用巢狀計數，外層仍開啟時，內層 EndInput 會重新啟用外層輸入。

## AIN 呼叫鏈與宣告形狀

| 路徑 | 開啟子畫面的同步呼叫 | 返回 |
|---|---|---|
| 標題 → 成就 | callback 36744 → `SceneTitle@OpenSubScene<SceneAchievement>` 36745 → `Scene::Run<SceneAchievement>` 36766 → `SceneAchievement@Run` 34349 → Join 27192 | `SceneAchievement@Close` 34350 → Exit 27193 |
| 標題 → 讀取 | callback 36742 → `SceneTitle@OpenSubScene<SceneLoad>` 36743 → `Scene::Run<SceneLoad>` 36764 → `SceneLoad@Run` 34472 → Join 27192 | `SceneLoad@Close` 34476 → Exit 27193 |
| 據點 → 系統選單 | callback 36791 → `SceneHome@OpenAdvMenu` 31918 → `Scene::RunResult<SceneAdvButtonMenu, AdvShortcutType?, AdvButtonMenuType>` 36711 → `SceneAdvButtonMenu@Run` 31740 → Join 27192 | `SceneAdvButtonMenu@Close` 31737 → Exit 27193 |

三條路徑都保留外層 Join／WaitForClick。點擊派送發生在 `AFL_View_Update` → `view::detail::View_Update` → `parts::detail::UpdateMessage` → `CPartsMessageManager@Update`，回呼是同步執行。

**鑑賞模式是有效的對照**：標題 callback 36746 呼叫 `SceneTitle@Exit(3)`，先結束標題的 WaitForClick，之後 `TitleSceneSet::Open` 才開啟 `SceneOmake`；返回後建立新的標題。它沒有保留外層 Title.WaitForClick，故舊布林實作也能正常往返。這與先前 GUI 紀錄一致。

| API | AIN 宣告 | CALLHLL 站點 | arg3 | AIN wrapper |
|---|---|---:|---:|---|
| BeginInput | `void BeginInput(void)` | 1 | 0 | `parts::detail::BeginInput`，FUNC 9192 |
| EndInput | `void EndInput(void)` | 1 | 0 | `parts::detail::EndInput`，FUNC 9193 |

每個名稱都只有一種宣告形狀，不涉及 overload 或參數錯位。兩個 wrapper 各有五個直接 CALLFUNC 站點，位於 `CActivity@UpdateActivity`、`CActivityEditor@Run`、`CBezier2DEditor@Run`、`CRandTypeSelecter@Run` 與 `WaitForClick`；都是進入迴圈前 Begin、離開後 End。

另外檢查 `SetEnableInput`／`IsEnableInput`：各有一個 HLL wrapper，但本 AIN 沒有直接 CALLFUNC 呼叫這兩個 wrapper。`InputDisabler` 是透過顯示／隱藏高 Z 矩形阻擋點擊，沒有呼叫 SetEnableInput。因此本組不修改這兩個既有 stub。

## 原版語義與位址

PartsEngine dispatcher 為 `0x57b900`，跳表 `0x589714`。BeginInput 的 index 14 → `0x57bc2d` → `0x58a720`；EndInput 的 index 15 → `0x57bc37` → `0x58a750`。

全域 PE instance 的 `+0x1fc` 是輸入深度，同一欄位也是 controller manager 的 `+0xb0`。

| API | 原版行為 |
|---|---|
| BeginInput `0x58a720` | 已有深度時先結束目前輸入暫存；啟動所有 controller 的輸入；深度加一 |
| EndInput `0x58a750` | 結束目前輸入暫存；若原本深度大於一，重新開始剩餘外層輸入；深度減一，最低為零 |

全 controller 開始／結束 helper 為 `0x53e830`／`0x53e8b0`，包含一般 controller 與 overlay。

- 開始的 `0x546030` 清除 pressed、hover、drag 目標與輸入暫存，並在 `0x546100..0x54618c` 讀取左／右／中鍵當下的按下狀態。**巢狀 Begin 及內層 End 恢復外層都必須重新採樣**，避免仍按住的關閉鍵被當成另一個 DOWN。
- 結束的 `0x5461a0` 清目標與手勢狀態。`0x577af0` 清低階元件的 hover／pressed 等狀態；開始路徑也經 `0x538060`、`0x564cc0` 清 hover。這不等於無條件覆寫所有部件的顯示狀態。
- Begin／End **不清除 HLL 訊息佇列**。原版 End 反而可能因 hover／鍵釋放新增事件；本組保留既有佇列。
- manager 建構路徑 `0x53cfdc` 將深度設零。現碼的初始化／reset 也須清深度，避免重新開始後沿用已不存在的外層。

## 實作

修改限於 `src/parts/input.c`、`src/parts/parts.c` 與內部宣告。

- v14 以 `v14_input_depth` 記錄未結束的 BeginInput 層數；`parts_began_click` 保持現有呼叫端介面，反映是否仍有輸入範圍。
- 每次 Begin／End 依原版規則清 click number、pressed、hover、drag 與背景點擊暫存。開始或恢復外層時採樣左鍵，最外層結束時關閉輸入；未配對 End 不會造成負深度。
- 只還原舊 hover／pressed 目標的顯示狀態，保留無關部件由腳本選定的狀態及既有 state lock；狀態清除會標記重繪。
- `PE_Init`／`PE_Reset` 呼叫 `parts_reset_input` 清深度。
- v13 以下仍執行原本的布林 Begin／End；新增 reset 對這些版本不做事。
- 不改 AIN、不新增 VM_ERROR、不動 controller ID／排序，也不改 submodule 或存檔格式。

## 新探針 `input-nesting`

探針使用合成矩形、正式 `PE_UpdateInputState` 與 v14 訊息佇列，不需要 GL、遊戲資產或檔案。IN1–IN5 透過真 AIN HLL 宣告呼叫 API；IN6 執行原始 AIN 的兩個小 wrapper。新增 reset 函式以 dlsym 取得，舊引擎也能編譯 before-check。

| Case | 驗證內容 |
|---|---|
| IN1 | 兩層 Begin，內層 End 後外層仍收到真正點擊訊息；最外層 End 後停止 |
| IN2 | 三層、深度歸零、不配對 End 與之後建立新範圍 |
| IN3 | 第一次及巢狀 Begin 時已按住左鍵，不產生虛假的新 DOWN |
| IN4 | 內層 End 時已按住左鍵，恢復外層不重複點擊；下一次新按下仍可用 |
| IN5 | Begin／End 清 stale click number，但保留已排入的 HLL 訊息；按住時移到另一部件不觸發新點擊 |
| IN6 | 不改 bytecode，真 AIN Begin／End wrapper 反覆開關八次，外層仍可操作 |
| IN7 | v13 的兩次 Begin、一次 End 仍關閉 latch，維持舊行為 |
| IN8 | manager reset 清除兩層深度，保留佇列與無關部件的腳本指定狀態；新的 Begin／End 不復活舊範圍 |

IN8 是在七個初版 case 之後補上的生命週期與狀態回歸檢查。IN7 預期舊版也通過，不應把 before-check 的「模式失敗」描述成每個 case 都必須失敗。IN6 不是完整 SceneContext.Join／WaitForClick 測試；完整巢狀 callback 由 GUI 驗證。

既有 `third-review` 的 RV2／RV3 原本在 helper 先 EndInput，再檢查懸停。本組恢復原版 EndInput 清 hover 的語義後，這個觀察時機不再正確：呼叫端改為在輸入範圍內執行原有 hover／state assertions，最後才 EndInput。RV2 的遮擋元件移回按鈕、RV3 的目標移至空白處，各自在同一範圍連續測量，避免重設先清除舊懸停而掩蓋問題。所有原有 assertions 保留，Begin／End 仍成對。

## 驗證與審查

以下為最終原始碼的驗證；未驗證的延伸路徑列於文末。

| 項目 | 結果 |
|---|---|
| 預設 58 模式 verify-step | `codex-input-final-v3`：VERDICT PASS，sanitizer 0 |
| GBK 58 模式 verify-step | `codex-input-final-v3-gbk`：VERDICT PASS，sanitizer 0；SJIS 91 行與基準逐位元相同 |
| before-check `a6a8f5b input-nesting third-review` | 新模式 7/8 失敗（IN7 v13 控制組通過）；既有 third-review 5/5 PASS。回復 `a870409` 後新模式 8/8 PASS，sanitizer 0 |
| 正式 GUI 150 秒、MSG 88、assertion／overflow | `codex-input-final-g150`：150.323 秒，MSG 88，兩者皆 0；正常時限退出，峰值 RSS 507,871,232 bytes |
| 成就往返後再次開啟／開始新遊戲 | 55.216 秒：兩次往返，五個預排點擊全派送，最後畫面進入 Episode 1；基準第一次返回後即無後續派送 |
| 讀取往返後再次開啟／開始新遊戲 | 55.267 秒：兩次往返，五個預排點擊全派送，最後畫面進入 Episode 1；不代表實際讀取存檔已修好 |
| 據點系統選單反覆開關後繼續操作 | 140.268 秒、MSG 88：92／100 秒選單往返後，108／116 秒再往返，124／132 秒庫房往返；基準 100 秒返回後無後續派送 |
| Wine 原版同操作與逐幀比對 | 成就／讀取各兩次往返後可開新遊戲；據點選單兩次往返後可進出庫房，與修正版的場景到達順序相同。採樣方法與既有外觀差異見下節 |
| 靜態反駁審查 | 兩位獨立審查者複核；最終無阻擋項。補齊 reset 探針、hover dirty 與既有測試連續性後才重跑兩組驗證；最終提交 diff 與測試紀錄一致 |
| 修正提交與遠端雙源確認 | `a87040975b2f29f45920931a7128ea9151eb60f4`；推送前兩來源皆為 `a6a8f5b`，推送後 ls-remote／GitHub branches API 皆等於修正 SHA |

[兩組驗證摘要](input-nesting/verify-default.txt)／[GBK 摘要](input-nesting/verify-gbk.txt)，[新探針預設](input-nesting/probe-default.txt)／[GBK](input-nesting/probe-gbk.txt)，[GUI 數值與點擊紀錄](input-nesting/gui-summary.json)。驗證於提交前執行，摘要的基準 HEAD 加上 `diff_sha` 表示本組原始碼。

## Wine 影格與 Control 快進

原版僅執行專用 Wine 工作副本，執行前已備份存檔。每個動作截取操作前畫面及 20 張連續視窗影格，間隔另加 0.1 秒，含擷取開銷約 4.1–4.4 秒；xsystem4 使用既有操作 run 的每秒 framebuffer。以開啟、轉場、穩定畫面、返回、再次開啟逐階段對照，未做等時間軸或逐像素一致性判定。這不是 60 fps 全畫格錄影。

- 原版成就、讀取各兩次往返，最後按「開始」進 Episode 1；修正版分別在兩個隔離存檔 run 重現相同到達順序。
- 原版 Control 按住 15 秒後抵達據點教學，下一頁／關閉後開關系統選單兩次，再進出庫房；修正版一般速度的相同流程也成功。
- 原版系統選單有文字與半透明背景；xsystem4 仍缺選項標籤且左側黑色。修正前第一次、修正後第一次與第二次選單穩定 framebuffer 的 RGB 完全相同，這些差異不是本組新增。
- 原版庫房沒有殘留的「據點環節」字樣，xsystem4 仍有。標題子畫面列表亦不同：Wine 使用原有存檔，xsystem4 使用隔離空存檔；列表內容等價性未驗證，不能只以存檔不同解釋全部差異。

原版視窗若因繼承的 `LC_ALL=C.UTF-8` 使中文路徑讀取失敗，單次啟動同時指定 `LC_ALL`、`LC_CTYPE`、`LANG` 為 `zh_CN.UTF-8`；本輪如此啟動成功，沒有改系統設定。[動作與影格採樣摘要](input-nesting/wine-comparison.json)。遊戲截圖與完整影格留在 repo 外。

xsystem4 的 Control 快進另做獨立 50.351 秒 run：`RUN_HOLD_KEYS=13,17 RUN_AUTO_CLICK=`，約 7 秒抵達據點教學，MSG 88、assertion／overflow 0。快進與正常回歸的 MSG SHA256 都是 `94815c53e0e06c171b181160e4f634683309b1558fb4cdc4f3d5c61496ca50bc`。操作方法見 [harness](../../harness/README.md#control-快進診斷)，不取代正式 150 秒回歸。

## 限制與仍待處理

1. **完整輸入事件尚不等價**：現有 v14 MouseLeave／KeyUp 派送沒有在本組補完。原版 End 所發 type `0x12` 事件推定為 KeyUp，但本組沒有以 AIN 型別名稱另證。
2. **controller 規則仍有差異**：原版 `0x53e690` 只讓作用中的 controller 接收輸入；現碼仍掃描全部 parts。原版 controller ID 與 vector index 分開，Add 可插入中間、Remove 以 ID 查找，現碼仍有以 index 代替 ID 及 explicit index 的 VM_ERROR。配置／劇情回顧的 AddController 錯誤屬另一組。
3. **ResumeLoad 輸入深度未驗證**：原版 manager save `0x53ea5c` 寫出深度，load `0x53ebe5` 還原。現有 XPE 沒有保存輸入範圍，本組未擴大格式。一般讀檔仍需要尚未實作的 `system.Reset`；開關讀取畫面成功不等於讀檔成功。
4. **手勢與座標暫存未完整還原**：本組只映射現碼已有的左鍵、hover、drag 與 click 暫存；原版右／中鍵、滑動與每個座標欄位尚未逐一對應。
5. **其他可玩性卡點不在本組**：春銷 Start 的 `GetHGauge` 斷言、訊息視窗系統 UI、長時間穩定性仍須各自驗證。
6. **系統選單的既有畫面差異**：本組基準與修正後第一次選單，以及修正後第二次選單的穩定影格 RGB 完全相同；選項文字缺失、左半背景黑色仍存在。恢復輸入不等於修好這些外觀問題。
