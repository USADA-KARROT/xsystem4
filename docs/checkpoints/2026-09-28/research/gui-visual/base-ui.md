# 據點畫面缺件（D1–D5、D7）與 SetButtonEnable（2026-09-29）

修正 commit：`6d39915`（接在 `22339c1` 之後，本機 commit，尚未推送）；審查後修正 `fb28975`（§10，接在 `1e56cd6` 之後，本機 commit，尚未推送）；第二輪審查後修正 `6582e38`（§11，接在 `ce5c75a` 之後，本機 commit，尚未推送）。原版 EXE 只做靜態反組譯（capstone），沒有執行。
證據等級：**已驗證**＝逐指令或執行結果直接佐證；**推定**＝靜態推論合理，但沒有逐指令或執行期確認；**未驗證**＝尚無證據。
路徑記號：`$PORT`＝`<PORT>`。並排圖、原版截圖與臨時追蹤工具都在 repo 外（`$PORT/reports/base-ui-20260929/`），不提交。差異編號 D1–D15 沿用 [visual-compare.md](visual-compare.md)。

---

## 0. 結論

1. **第一個失敗點**：`activity::detail::Load` 讀完 activity 後，立刻用 `NumofChild`／`GetChild` 走訪元件樹，把 EPartsType 17 的元件交給使用者元件管理器建立。xsystem4 的 pactex loader 用 `PE_SetParentPartsNumber` 設父元件，但這個函式只記下 `pending_parent`，要等下一次 `UpdateComponent` 才掛上，所以走訪看到的根元件沒有子元件，底列、金錢列、貼紙、Schedule、據點環節橫幅、教學的按鈕全部沒有建立。原版的 setter 立即掛上。（已驗證：追蹤與 fixture）
2. 掛上之後還有八個獨立缺陷依序擋住，全部照原版語義修正（§2）。其中讓「下一步」出現並能按的必要條件是：元件型別照原版表（17／24／27）、`GetActivityParts` 寫出名稱與編號、`GetUserComponentData`、`SetComponentType` 對低階元件只改一個狀態、`Array.Add` 兩槽、`MainEXFile.Col`。
3. 修正後，據點畫面的底列（存檔／讀取／物品／下一步、ToDo／Tips）、Day 1、¥10,000、三張 FEEL 貼紙、據點環節橫幅、教學覆蓋層兩頁都會出現，「成员」「商店」依原版顯示為停用（D3）。以點擊序列翻頁、關閉教學後按「下一步」，遊戲進入下一個畫面「今天要選哪件事做呢？」（階段選擇）。（已驗證：GUI）
4. 仍與原版不同：據點背景是黑的（教學的底圖與圖層沒有釋放，另有跨圖層繪製順序未驗證）、關閉教學後畫面停在 0.975 倍、頁點是黑色方塊、教學外框外有據點畫面溢出。這些各有獨立的原因，列在 §7 與 HANDOFF。其中黑背景與 0.975 倍的根因是場景物件不釋放，已由 §11 修正。

---

## 1. 修正前的呼叫鏈

進入據點：`DohnaDohna@RunHome` → `Scene::RunResult<SceneAzito>` → `SceneAzito@0`（`PhaseBar`）→ 2000 ms 後 `RunSubScene` → `Scene::RunResult<SceneHome>` → `SceneHome@0` → `SceneContext@Create("Scene/30_Home/Home/SceneHome", spot handler)`。spot handler 依 UC 名稱建立 FeelEventButtonCollection、MoneyView、HomeScheduleView、`Footer::CreateComponent`。之後 `@Run` → `FadeIn` → `ShowTutorial`（`Flags["主要"]==101` 時 `Tutorial::Run("Azito1")`）→ `Join`。

修正前（`22339c1`）的追蹤（臨時碼，未提交）：

```
ReadActivityFile("Scene/30_Home/SceneAzito") -> 1
GetComponentType(<root>, 1) -> 0          (根元件是レイアウトボックス，原版 8)
NumofChild(<root>): attached=0, pending=3
SKIP-NULLOBJ-METHOD SceneAzito@0 -> PhaseBar@Text::set
NumofChild(<SceneHome root>): attached=0, pending=11
SKIP-NULLOBJ-METHOD SceneHome@0 -> Footer@SetInfoText
SKIP-NULLOBJ-METHOD SceneHome@FadeIn -> MoneyView@FadeIn / Footer@FadeIn
```

`SetUserComponent` 0 次、spot handler 0 次。教學只剩 `SceneTutorial` 的灰色面板與空頁。

---

## 2. 根因與原版語義

| # | 缺陷（xsystem4 `22339c1`） | 原版 | 影響 | 狀態 |
|---|---|---|---|---|
| R0 | v14 `Parts_SetParentPartsNumber` 延到下一次更新才掛上 | case 167 `0x57def6` → `0x58f060`：父元件為 0 時 `0x53ab60` 脫離；否則 `0x58f310` 查父元件、`0x53afb0` 檢查後 `0x550d90` 立即加入。`NumofChild`（case 174 → `0x58f500`）直接回子元件 vector 的長度 | 讀檔後的走訪看不到子元件 | 已驗證 |
| R1 | loader 依結構猜型別：容器與無 CG 葉 0、有 CG 葉 1；UC 名稱存節點名 | `0x4eda70` 依 EPartsType 順序建 31 個 `部件タイプ` 名稱（按鈕 0 … レイアウトボックス 8 … ユーザコンポーネント 17、低等級部件 18、ＣＧ部件 19、文本部件 21、數字部件 24、ＣＧ判定部件 27 …，字串位於 `0x7d22b4` 等），`0x5b8f90` 以名稱的索引當型別；UC 讀 `ユーザコンポーネント名`（`0x4e50c0`），GetUserComponentName（case 694）回傳它 | `CallUserComponentEventWithChild` 要求 17；`CompParts` 要求 24（GetNumeral，MoneyView 有 nonnull 斷言）與 27（GetCGDetection，FooterButton 有 nonnull 斷言） | 已驗證 |
| R2 | v14 `SetComponentType` 一律把元件改成新型別並清掉狀態型別 | case 74 → `0x58b5c0` → `0x535e20`：型別相同（`0x536540`）不動；低階元件（外層 18 以上）收到 18 以上的型別時只改該狀態（`0x5653d0`）；其他才換掉 widget | `GetCGDetection` 的包裝 `SetComponentType(n, 27, 1)` 把 Detector 變成一般 CG，按鈕圖蓋掉按鈕文字 | 已驗證 |
| R3 | `GetActivityParts` 回 true 但不寫出名稱與編號 | case 36 `0x57c2f5` → `0x58a9c0` 寫出兩者 | `ActivityHelper::GetPartsNames` → `GetUser<FooterButton>("Button\d")` 取不到按鈕 | 已驗證 |
| R4 | `GetUserComponentData`／`SetUserComponentData` 未實作 | case 695／696 → `0x597ff0`／`0x598020`：`0x53e470` 取 UC widget（非 UC 時先 `SetComponentType(17,1)`，`0x5372c0`），查 +0x40 的 key/value；沒有時回空字串 | Footer 的按鈕文字（`數據` "Button" = "存檔,讀取,物品,下一步"）、教學按鈕文字 | 已驗證 |
| R5 | `Array.Add` 走單槽的 `Pushback`；`PushBack` 對 `X_A_INIT 0` 建的空陣列不設 stride；`PopBack` 只移一槽 | Array 跳表的 PushBack 與 Add 都指向 `0x644455` | `SceneParentStack@Push`（`Add 65539`）只存物件不存 vtable 槽，`GetAllInstances` 的元素數加倍、一半是 null，教學搬移場景父元件時對 null 呼叫方法 | Add 已驗證；stride／PopBack 依 xsystem4 陣列模型推定 |
| R6 | `MainEXFile.Col` 對平面 list 回傳第 0 項 table 的欄數（0） | `0x4ae006` → `0x4b00e0`：先 EX vt+0x2c（節點型別 5，list），回 list vt+8 = `0x478e50`（元素數）；找不到才 vt+0x28（型別 4，table）回欄數 | `EXHelper::GetStringArray` → `Tutorial::GetCgs` 回空陣列；`ItemPositionView` 以頁數 0 建構 `Create(-span, width)`，引擎以 `Incomplete framebuffer` 結束 | 已驗證 |
| R7 | pactex 的 `編輯上表示` 被忽略 | 解析器 `0x553dfa` 把 `表示`、`編輯上表示` 存在元件 +0xab、+0xac；每幀的處理 `0x53c3c5` 兩者都非 0 才處理 | SceneHome 的 `Background`（洋紅乘算色，D6 的一部分）、SceneTutorial 的灰色 `パネル_000`、教學裡隱藏的 Footer UC 被畫出 | 欄位位置已驗證；「子元件一起隱藏」依原版畫面推定 |
| R8 | 父元件的倍率不作用在子元件位置；倍率改變時子元件位置不更新；文字不套倍率 | `0x4e6d80` 每層乘 `S·R·T(pos)`（見 reverse-inherit.md） | 教學的 MoveParent（0.85 倍）只縮小元件本身；據點背景放大動畫的子元件錯位；FooterButton 文字（字級 49、倍率 0.5）畫成原尺寸 | 已驗證 |
| R9 | `數字部件` 的 `表示タイプ 2`（字型數字）沒有繪製 | — | Day「1」、¥「10,000」空白（D4） | 渲染細節未追原版 |
| R10 | `SetButtonEnable` 未實作，`IsButtonEnable` 恆回 false | case 212 → `0x590b70`：`0x53d990` 取按鈕 widget（非按鈕時先 `SetComponentType(0,1)`），+0xb0 不同時寫入並設 +0x218；case 213 → `0x590ba0` 回 +0xb0，沒有元件時 false。建構子 `0x5260c0` 預設 1。重建 `0x528870` → `0x529280`：停用時三個狀態換成 `<base>／無効`；每幀 `0x5285b0` 停用時清空 +0x1e0、+0x1f8（推定為游標、點擊音效） | 成员、商店沒有變灰（D3） | 已驗證（音效欄位用途推定） |

另外兩項與原版一致性有關、這次沒有改（§7）：
- **只有作用中的 controller 接受輸入**：`0x53e690` 對每個 controller 呼叫 `0x53c5b0`，只有作用中的那一個帶輸入旗標，只有它進入輸入處理 `0x547c90`。（已驗證）
- **AddController(-1) 插在作用中 controller 之後**：`0x58a6b0` → `0x53d480` → `0x53d3a0`，位置 = 作用中者 + 1，再設為作用中。（已驗證）

---

## 3. 修正內容

| 檔案 | 內容 |
|---|---|
| `src/parts/parts.c` | v14 `PE_SetParentPartsNumber` 立即掛上（R0，未知父元件與循環忽略）；`PE_GetComponentType` 依狀態回 `component_state_type`；v14 `PE_SetComponentType` 照 `0x535e20`（R2）；`parts_child_pos` 乘父元件倍率，`parts_set_scale_x/y` 更新子元件位置（R8）；`parts_set_edit_hidden` 併入 `global.show`（R7）；字型數字 `parts_numeral_update_font`（R9）；按鈕 CG 與 `PE_SetButtonEnable`／`PE_IsButtonEnable`（R10）；UC data 存取；釋放時一併釋放 UC 名稱、按鈕 CG 名與 data |
| `src/hll/pe_v14_activity.c` | 31 個 `部件タイプ` 名稱表（GBK）；`pactex_apply_native_type` 設型別、UC 名與 `數據`（R1）；低階狀態建 24（字級、色、太さ、縁取り、桁數、コンマ表示、字間隔、ゼロパディング、全角、表示タイプ）與 27（CG 只作判定、可點擊）；讀 `編輯上表示`；按鈕 CG 改走 `parts_button_set_cg_name`；`GetActivityParts` 寫出名稱與編號（R3）。表外的名稱（例如 SJIS 版的名稱）維持原本的結構猜測 |
| `src/hll/PartsEngine.c` | v14 註冊 `Get/SetUserComponentData`（R4）、`SetButtonEnable`、`IsButtonEnable`（取代 stub） |
| `src/hll/Array.c` | `Add` 改走 `PushBack`；空的通用陣列在第一個兩槽元素時設 stride 2；`PopBack` 移除整個兩槽元素（R5） |
| `src/hll/MainEXFile.c` | `Col` 先回 list 元素數（R6） |
| `src/parts/render.c` | ＣＧ判定狀態不繪製；文字以元件錨點變換繪製（倍率、旋轉，R8） |
| `src/parts/input.c` | 停用按鈕不播游標與點擊音效（R10） |

---

## 4. 驗證

### 4.1 Headless：新模式 `base-ui`

`probe/base_ui_fixture.inc`，五個案例各自 fork（60 秒 alarm）。只用修正前就存在的函式；`probe_mainex_set` 由 `build_probe.py` 附加在 `MainEXFile.c` 的副本上。沒有遊戲資產與 GL。

| 案例 | 內容 |
|---|---|
| BU1 | 合成 GBK pactex（根 レイアウトボックス；Footer UC「Fotter」與 `數據`；按鈕；數字部件；ＣＧ判定部件＋ＣＧ部件；表外名稱；巢狀 レイアウトボックス）經正式 loader，再以真 AIN 宣告查：讀檔後立即 `NumofChild`／`GetChild`、各型別、`SetComponentType(Detector,27,1)` 後其他狀態不變、UC 名稱、`Get/SetUserComponentData`、`GetActivityParts` 的兩個輸出 |
| BU2 | `Parts_SetParentPartsNumber` 立即掛上、循環與未知父元件忽略、0 脫離；父元件 0.5 倍時子元件位置 |
| BU3 | `IsButtonEnable` 預設 true、切換、沒有元件時 false 且不建立；UC widget 經查詢變成按鈕型別 |
| BU4 | `Array.Add`／`PopBack`（0x10003）在 `X_A_INIT 0` 陣列上保留兩槽、`Numof`、參照計數；真 bytecode `SceneParentStack@Push`／`@Pop` |
| BU5 | `MainEXFile.Col` 對平面 list 與 table；真 bytecode `EXHelper::GetStringArray` |

| 版本 | 結果 |
|---|---|
| `22339c1`（暫存 clone 的 `before-check.sh 22339c1 base-ui`） | rc 88，5/5 失敗；BU2 在父元件循環後的更新迴圈卡住，由 alarm 停止（[輸出](base-ui/base-ui-before-22339c1.txt)） |
| `6d39915` | 5/5 通過，預設與 GBK 組態相同（[輸出](base-ui/base-ui-after-6d39915.txt)） |

`activity-text` 在修正前後都通過（修正前的 GBK 名稱只涵蓋文字與 CG 狀態）。

### 4.2 verify-step

50 個模式（原 49 個加 `base-ui`）在預設與 `XS4_PROBE_GBK=1` 兩種組態都 `VERDICT PASS`，sanitizer 診斷 0（[摘要](base-ui/verify-summary.txt)；`diff_sha 82ceebc56cf9` 即 `6d39915` 的 src/include 差異）。

### 4.3 gui-run（不回歸）

| 版本 | 秒數 | MSG | assertion | 堆疊溢位 | UNIMPL HLL | 峰值 RSS |
|---|---:|---:|---:|---:|---:|---:|
| `22339c1` | 150 | 88 | 0 | 0 | 2（SetButtonEnable） | 1.31 GB |
| `6d39915` | 150 | 88 | 0 | 0 | 0 | 1.16 GB |
| `6d39915` | 220 | 88 | 0 | 0 | 0 | 1.67 GB |

修正前後 88 句對白的雜湊相同；開場 40 張 framebuffer 中 22 張逐像素相同，其餘 18 張是淡入淡出與訊息出現的時間點不同（目視確認同樣的立繪、文字與視窗）。詳見 [gui-summary.json](base-ui/gui-summary.json)。

### 4.4 GUI：推進與原版比對

`gui-run.sh` 只存前 80 秒的 framebuffer，也只能點畫面中央。推進測試用 `6d39915` 的原始碼加一個臨時 patch（不提交）：延後截圖時間，並把 `XSYS4_AUTO_CLICK_SEQ` 傳進引擎。點擊序列：126 秒前每 1.2 秒點中央，134 秒點教學「下一頁」(237,677)，139 秒點「關閉」(1180,677)，143 秒點據點「下一步」(1180,677)。

| 時間 | 畫面 | 與原版（`$PORT/wine-reference-shots/base/`） |
|---|---|---|
| 126.6 s | 據點環節橫幅（PhaseBar）＋據點背景放大 | 原版 s052 有同一橫幅 |
| 128 s | 據點全畫面 | 與 `c00` 相比：Day 1、¥10,000、人材／庫房、灰色的成员／商店、三張貼紙與 FEEL 標籤、JUDIAN 直條、Schedule、底列四個按鈕與 ToDo 一致；**背景是黑的**（原版是模糊的酒吧） |
| 131 s | 教學第 1 頁 | 與 `c04` 相比：標題、縮到 0.85 倍的據點畫面、粉紅框與說明、ToDo 框、上一頁（灰）／下一頁（粉紅）／關閉（灰）一致；框內背景黑；框外左右有據點畫面溢出；頁點是黑色方塊 |
| 138.5 s | 教學第 2 頁 | 與 `c05` 相比：說明框、FEEL／ITEM／EVENT 圖示、「下一步」提示框一致；關閉變粉紅、下一頁變灰 |
| 141.5 s | 關閉後的據點 | 版面同 `c00`，但整體仍是 0.975 倍，背景黑，ToDo 與 Tips 兩行同時淡入淡出 |
| 143 s | 按「下一步」 | 追蹤：`FooterButton` 點擊 → `Footer` lambda → `SceneHome@Exit(7)` |
| 146 s 起 | 「今天要選哪件事做呢？」與兩個選項、返回鈕 | 原版參考截圖沒有這一頁 |

並排圖：`$PORT/reports/base-ui-20260929/implA/sbs_*.jpg`（上：原版，下：xsystem4）。

在階段選擇點左邊的選項（春銷）後，引擎在 `PE_AddController` 以「active controller is not at the top of the stack」結束（原因見 §7 第 1 項）。

---

## 5. D3：SetButtonEnable 的行為

- 預設啟用；`SetMenuState` 把成员與商店設成停用後，三個狀態換成 `系統／據點／按鈕／成員／無効` 等 CG，畫面與原版一致。
- 沒有 `<base>／無効` 時原版改用 `0x528c80` 以按鈕尺寸與顏色產生面板（給用 `SetButtonColor` 的按鈕），這個後備沒有實作，保留啟用時的 CG。
- ~~停用按鈕仍會送出點擊~~：審查指出原版點停用的「成员」「商店」沒有反應（Wine 參考 `deep/d035`、`d036`），`fb28975` 改為停用按鈕吃掉點擊、什麼都不送（§10.1）。原版在哪裡擋掉點擊仍**未驗證**。
- 教學的「關閉」「下一頁」變灰，是 FooterButton 自己的 `PartsStateEventProvider@Enable`（AIN 層），不經過這個 HLL。

---

## 6. 關於「頁籤點」

visual-compare D4 提到的上方中央頁籤點，所有 pactex 都沒有對應元件，只出現在手機串流的實機截圖（C_game_032），推定不是遊戲 UI（沿用靜態調查的結論，未驗證）。

---

## 7. 沒有修的部分（寫進 HANDOFF）

1. **場景物件沒有釋放，教學的圖層留下來。** 追蹤（臨時碼）顯示教學結束後 `SceneTutorial@1`、它的 `SceneContext@1` 都沒有執行：`SceneTutorial` 的 ref 是 4，全部來自 delegate page（按鈕事件的 `DG_NEW_FROM_METHOD`），形成循環。其他場景（ADV、成就通知）的 `SceneContext@1` 有執行。後果：
   - 教學的圖層（controller 3）不會被 `EraseLayer` 移除；教學底圖 `系統／新手教程／下地`（中央不透明黑）也留在 SceneHome 的圖層，關閉教學後據點背景一直是黑的。
   - 階段選擇之後 `AddController` 時作用中的 controller 不在最上層，引擎依現有檢查結束。原版此時會插在作用中者之後（`0x53d3a0`），但 xsystem4 的 controller 編號兼作 ID，插入會讓上層圖層的 ID 位移；臨時試做後遊戲在點選後停住，已撤回。
   - 修法方向：確認原版 delegate 是否持有物件的強參照（若否，xsystem4 的 delegate 需要改為弱參照並處理失效物件），並把 controller ID 與堆疊位置分開。
2. **輸入只給作用中的 controller**（`0x53e690`）。依原版實作後，教學期間點畫面空白處不會穿透到據點的按鈕；但因為第 1 項，教學結束後作用中的仍是留下來的教學圖層，據點變成無法點擊，所以這次沒有納入。目前教學期間點到據點按鈕所在的位置仍會觸發它們（修正過程的追蹤中，畫面中央的自動點擊曾觸發商店按鈕的 `SceneHome@Exit`）。
3. **跨 controller 的繪製順序**：教學第 1 頁原版框內看得到據點的模糊背景，xsystem4 依 controller 編號排序，SceneAzito（controller 1）畫在教學底圖（controller 2、z 1）下面。原版是否依 z 全域排序（`SetPartsZandClipper` 把場景父元件設成 z 1）**未驗證**。
4. **Motion 最後一幀**：MoveParent 回到 1 倍的動畫停在 0.975，Footer 的 ToDo／Tips 交替也停在中間；推定是 Motion 結束時沒有套用終值，未追。
5. **alpha clipper 不作用在子元件**：教學用 1280×720 的 clipper 裁掉框外，xsystem4 只裁元件本身，框外有據點畫面溢出。
6. **構築部件**：SceneAzito 的 `Bg`（命令 2 載入「背景／那由多」、28／27 模糊）仍未實作（D6）；`CASConstructionProcess::FillCircle` 的 type 102 未實作，`Create` 在 xsystem4 產生不透明黑底，頁點成為黑色方塊。
7. **型別**：這次只為低階元件建 CG、文字、數字、ＣＧ判定四種狀態；循環ＣＧ（20）、ゲージ（22、23）等狀態仍走舊的 CG 後備、回報外層型別 18，`GetHGauge` 的 nonnull 斷言在後續畫面可能觸發（未驗證）。矩形（25）、構築（26）已由 `fb28975` 補上（§10.4）。
8. **字型數字**：`SetNumeralFont`、`SetNumeralShowType` 等 HLL 仍是 stub；位置、對齊與原版逐像素比對未做。
9. **存檔**：新欄位（狀態型別、按鈕停用、UC data、字型數字設定、`編輯上表示`）都沒有寫進 parts 存檔。

---

## 8. 未驗證事項

- 原版擋掉停用按鈕點擊的位置（§10.1；行為依 Wine 參考截圖）。
- `編輯上表示 = 0` 讓子元件一起隱藏：只有 +0xab／+0xac 並列檢查的反組譯與原版畫面（教學裡沒有 Footer、SceneHome 的洋紅背景不可見）佐證。
- SJIS（日文版）的 `部件タイプ` 與 `編輯上表示` 拼法沒有原版表可對照，這些名稱維持舊行為。
- 字型數字的原版排版（對齊、字寬、全角）。
- 跨 controller 的繪製順序、delegate 的參照語義（§7）。
- 階段選擇之後的流程。

---

## 9. 臨時工具（未提交）

- 追蹤 patch：沿用 `$PORT/reports/base-ui-20260929/` 的 XS4DBG 追蹤（HLL／函式呼叫、SKIP 記錄），另加 parts 傾印（controller、z、show、貼圖中心像素、文字字形）、指定函式監看、struct 參照掃描、文字繪製矩陣記錄；全部只套在 repo 外的除錯副本。
- 點擊序列與延後截圖：同上，只在除錯副本。
- 修正前的 before-check 與對照 GUI 在 `22339c1` 的暫存 clone 執行，不動 worktree。
- `fb28975` 一輪（§10）：repo 外的除錯副本加 `XS4DBG_SHOT_START/STEP/MAX`（延後截圖）、`XS4DBG_SIZE`（`PE_GetPartsSize`／`PE_SetPos` 在 `AnimateText@AdjustPos` 內的值與呼叫鏈）、`XS4DBG_HLL=<fno>`（指定函式內的 HLL 呼叫與堆疊深度）、`XS4DBG_PPC`（`Motion::PartsParamCollection@0` 結束時的 Time／DelayTime）；runner 用審查者的 `RUN_AUTO_CLICK_SEQ` 版本。`before-check.sh 1e56cd6` 在 worktree 以 `git stash push -- src include` 暫存本組修改後執行，結束後 `stash pop`。

---

## 10. 審查後修正（`fb28975`，2026-09-30）

獨立審查 `6d39915` 的結論是 fix-first（中 2、低 3），流暢度 `44964f9` 的審查是 ship（低 1）。本節照審查項目逐一修正，並記錄追查中新找到的根因。臨時追蹤碼只套在 repo 外的除錯副本；並排圖在 `$PORT/reports/base-ui-review-20260930/`（不提交）。

### 10.1 中：停用按鈕仍送出點擊

- **現象**：Day 1 據點點灰色的「成员」會進入 MEMBER（`SceneHome@Exit(1)`）；原版點停用的「成员」「商店」沒有反應（Wine `deep/d035`、`d036`，懸停也沒有反應 `d012`、`d013`）。（已驗證：GUI）
- **AIN**：`SceneHome@RegisterEvent` 的點擊 lambda（FUNC 36795–36798）直接呼叫 `SceneHome@Exit`，`CPartsMessageManager@CallDelegate` 與 `CPartsFunctionSet@CallFunctionMouseClick` 都不看 Enable；`SceneHome@SetMenuState` 對停用狀態只呼叫 `IButtonParts` 的 Enable::set(false)。所以只能由引擎不送點擊。（已驗證：AIN）
- **原版位置未找到**：按鈕 widget 的 +0xb0 只有這些讀取點：重建 `0x528870`／`0x529280`（選 `／無効` CG）、每幀 `0x5285b0`（清空內部元件的游標、點擊音效字串）、`0x529490`（依啟用與否取 CG 尺寸，推定）、存檔 `0x526ab0`（資料子物件 +0xa4 起的 +0xc）、HLL `0x590b70`／`0x590ba0`。輸入處理 `0x547c90` 一帶沒有直接讀它。原版實際在哪裡擋**未驗證**。
- **修法**：v14 點擊派送找到的最前方目標若是停用按鈕，就吃掉這次點擊：不送 MouseClick、不播音效、也不改送全畫面點擊與 `g_EndPartsBusyLoop`（`src/parts/input.c`）。只看目標自己（按鈕 widget 在 xsystem4 沒有內部子元件；SceneHome 的 ButtonStatus 沒有子元件）。

### 10.2 中：ＣＧ判定部件的點擊判定

- **現象**：人材 → 第一張卡 → STATUS 頁的「返回」按不動。按下的那一幀 `parts_update_mouse` 已把 Detector 切成 CLICKED，派送再用 `states[2]`（空的 ＣＧ部件）的判定區，結果落空，改送全畫面點擊。（已驗證：審查者 r2 與本組 fixture）
- **修法**：派送改用 `PARTS_STATE_DEFAULT` 的判定區，與懸停（`parts_update_mouse` 既有的「Always use DEFAULT state hitbox」）一致。原版低階 widget 的判定用哪個狀態沒有逐指令確認（推定：原版此處能按）。

### 10.3 低：Array 對兩槽元素（0x10003）的處理，以及頁首／橫幅右偏的根因

- **真正的根因比審查指出的更前面**：`Array.At`／`First`／`Last` 回傳 `ref hll_param`，對兩槽元素（介面）只推一槽。AIN 以兩槽接收（`X_MOV 4 2; X_ASSIGN 2` 寫進介面型別的區域變數，例如 `SceneParentStack@Get`、`AnimateText@AdjustPos`），少一槽會把下面的值（例如 `AdjustPos` 累加中的寬度）當成介面的一半吃掉。`AdjustPos` 的 `totalWidth` 因此變成 0，所有字從錨點往右排，於是據點環節橫幅與選單頁首右偏約半個字串寬（73 px），並蓋到副標題。只修 `Last` 的索引（審查者的試做）不夠，這解釋了「修正後頁首沒有變化」。（已驗證：追蹤與 fixture BR5）
- 追蹤（除錯副本）：成就頁首 6 個字的 `AdjustPos` 修正前位置 x = 0, 20, …, 100，修正後 −50, −30, …, 50；進入 `Last` 前的堆疊深度比進入 `First` 前少 1。
- **修法**：兩槽元素時 `At` 推（物件，vtable 偏移），物件加參照；越界或空是（−1, 0）。`Last` 改成 `At(元素數 − 1)`（元素而不是最後一槽）；`First` 無 predicate 版本原本就走 `At`。
- **一併檢查的其他 Array 函式**（CN AIN 中 0x10003 的呼叫：PushBack 61、Where 24、Numof 24、At#1 16、Count 14、Add 13、Last#1 11、Free 11、First#3 11、Empty 8、IsExist#1 5、Insert 5 等）：
  | 函式 | 修正前 | 修正後 |
  |---|---|---|
  | `First(pred)`（11 處，含 `Motion::PartsParamCollection@0`、`Motion::EasingArgumentAnalyzer`、成就、戰鬥） | 逐槽呼叫 predicate（偶數槽傳 (物件, 0)、奇數槽把 vtable 偏移當物件），命中時推一槽 | 逐元素、傳整個元素，命中時照 `At` 推兩槽；predicate 形狀不支援時警告並回 none |
  | `EraseAll(pred)`（3 處，戰鬥） | 逐槽判斷、逐槽刪 | 逐元素判斷、整個元素刪除並釋放物件 |
  | `Concat`（2 處，資源搜尋） | 對第二槽（vtable 偏移）也加參照；空陣列不設步幅 | 只對物件加參照；空的通用陣列設步幅 2 |
  | `Reverse`（1 處，`ArrayExtensions::GetReverse<IRectParts&>`，由 `TitleCharacterView@FadeIn` 呼叫；原寫 `GetSort<BattleBonus>` 有誤，§11.2 更正） | 逐槽反轉，拆散（物件, 偏移） | 以元素為單位反轉 |
  | `Insert`（5 處，資源集合、編輯器） | 只存一槽、索引以槽計 | 在元素索引存兩槽；空的通用陣列設步幅 2 |
  | Where、Numof／Count、IsExist(pred)、Find(pred)、Any(pred)、Erase、LowerBound(pred)、Sort／QuickSort(pred)、Realloc、PopBack、Clear、Free、Empty | — | 已依步幅處理，未改 |
- **連帶效果：Motion 的 Time**。`Motion::PartsParamCollection@0` 以 `First(pred)`（predicate 是 `X_ICAST Motion::TimeParam`）找 TimeParam 再 `Time::set`／`DelayTime::set`；修正前堆疊錯位，Time 永遠停在預設 1000。修正後 GUI 追蹤（除錯副本，開場 60 秒）Time 分布為 250×14、200×8、500×4、150×4、1000×4、700、350、30、2500、100。HANDOFF 第 6 項「名牌淡出 Motion 的 `<Time>` 是 1000、字串寫 `Time:150`」推定就是這個原因（fixture BR4 以真 bytecode 確認 Time 150、Delay 20）。所有 motion 變成各自的長度，開場到據點環節橫幅由約 78 秒提早到約 67 秒；與原版的時間軸沒有對照（未驗證）。

### 10.4 低：矩形部件（25）與構築部件（26）

- **現象**：鑑賞模式 `ThumbnailPage.jaf:57/58` 的 `GetRect("ThumbnailArea"/"ThumbnailBase")`、人材狀態頁 `StandView.jaf:52` 的 `GetConstruction("PlayerC")` nonnull 斷言。`CActivityWrap@CompParts(name, type, 1)` 要求 `GetComponentType(n, 1) == type`，loader 不認得這兩種狀態，回外層 18。（已驗證：AIN）
- **修法**：loader 認得 `矩形部件`／`構築部件`：
  - 矩形：`PE_SetPartsRectangleDetectionSize` 建不繪製的矩形狀態，大小取 `左上／右上／左下／右下` 四角的外框。遊戲的 216 個矩形全是 `矩形模式 1`、`左上 (0, 0)`，四角與模式的原版語義沒有逐指令確認（推定）。
  - 構築：`手順リスト` 為空（177 個中 9 個，例如 PlayerC）建空的構築狀態，由遊戲自己填；有步驟的 168 個（例如設定畫面、SceneAzito 的 Bg：命令 2 載入 CG、27／28 模糊）仍走舊的 CG 後備（第一個 `ＣＧ名`），只回報型別 26，畫面不變。
  - 兩者的 `サーフェイスエリア` 在全部 pactex 都是 (0, 0, 0, 0)，不處理。
  - `SetComponentType(n, 25/26, s)` 對低階元件建對應狀態（`CRectParts@0`／`CConstructionParts@0` 會呼叫；型別已相同時照原版 `0x535e20` 不動）。
- 矩形現在有判定區（`點擊許可 = 0` 所以不接點擊，但 `オン指針透過 = 0` 會擋住後方元件的懸停，推定與原版相同）。對其他畫面懸停外觀的影響**未驗證**。

### 10.5 低（流暢度審查）：結束時等待背景截圖

- `sys_exit` 是 `_exit`，`atexit(gfx_wait_pixels_writes)` 從不執行；結束前還在寫的 framebuffer PNG 會遺失。`vm_exit`、`_vm_error`、`sys_error` 的 handler（`error_handler`）與 `main` 結尾改為明確等待；寫檔執行緒自己出錯時不等待自己。`gui-run.sh` 到時限送 SIGTERM 的情況仍會遺失正在寫的那一張（未修）。
- `pacing.md` §3 中「結束時等待」的說法已更正（`44964f9` 的 commit 訊息也這樣寫，不改寫歷史，以本節為準）；同一審查指出 `pacing.md` §7「設定畫面切換會打開 vsync」沒有 AIN 證據，一併更正。

### 10.6 驗證

- **新模式 `base-ui-review`**（`probe/base_ui_review_fixture.inc`，BR1–BR7 各自 fork）：BR1 停用按鈕的點擊、BR2 只有普通狀態有判定區的偵測元件、BR3 兩槽 `At`／`First`／`Last`、BR4 兩槽 `First(pred)`／`EraseAll`／`Concat`／`Reverse`／`Insert` 與真 bytecode `Motion::PartsParamCollection@0`、BR5 真 bytecode `AnimateText@AdjustPos`、BR6 合成 pactex 的 25／26、BR7 `vm_exit`／`VM_ERROR` 後 PNG 完整。
  | 版本 | 結果 |
  |---|---|
  | `1e56cd6`（`before-check.sh 1e56cd6 base-ui-review`） | rc 88，7/7 失敗；BR5 因堆疊錯位 heap-buffer-overflow（[輸出](base-ui/base-ui-review-before-1e56cd6.txt)） |
  | `fb28975` | 7/7 通過（[輸出](base-ui/base-ui-review-after.txt)） |
- **verify-step**：52 個模式在預設與 `XS4_PROBE_GBK=1` 兩種組態都 `VERDICT PASS`，sanitizer 0（[摘要](base-ui/review-verify-summary.txt)；`diff_sha 148345b22f5a` = `fb28975` 相對 `1e56cd6` 的 src/include 差異）。
- **gui-run 150 秒**（`fb28975` optimized build）：MSG 88（對白雜湊與 `44964f9` 相同）、assertion 0、堆疊溢位 0、UNIMPL 0、framebuffer 40 張（最後一張 PNG 完整）、58.67 fps、>50 ms 間隔 3 個、最長 64.5 ms、峰值 RSS 607 MB（[摘要](base-ui/review-gui-summary.json)）。
- **自動點擊重現審查者的情境**（除錯副本只加延後截圖，其餘同 `fb28975`）：
  | 情境 | 結果 | 原版 |
  |---|---|---|
  | 關閉教學後點「成员」(190,378)、「商店」(480,348)、再點「成员」 | 三次都是 `(disabled)`，據點畫面到 128 秒都不動 | `d035`、`d036` 不動 |
  | 人材 (150,198) → 第一張卡 (70,350) → 返回 (1180,678) | 目標 900762（Detector），回到人材一覽；無 StandView 斷言 | `d027` 回到一覽 |
  | 標題 → 鑑賞模式 (152,296) | 進入 IMAGES 頁，無 ThumbnailPage 斷言 | 無原版截圖 |
  | 據點環節橫幅 | 帶 y 316–399，文字 x 542–736（中心 639.0） | `run3/s052a` 帶 y 316–399，文字 x 544–735（中心 639.5）；修正前 617–811（714） |
  | 人材頁首、成就頁首 | 標題在左、副標題在右且有間距 | 人材與 `d023` 相同版面；成就頁沒有原版截圖 |

### 10.7 沒有修的部分與新發現

1. **回到人材一覽後再按「返回」沒有反應**：STATUS 返回一覽之後，127–145 秒的點擊都沒有進入派送（`PE_BeginInput` 沒有再被呼叫）；`1e56cd6` 的審查者 r3 在 130 秒也一樣，不是本組造成。期間每幀都有 `PE_SetPartsRotateY`（未實作）呼叫。原因未查（原版 `d028` 可以返回據點）。
2. **三槽 option 回傳被截成兩槽**：`function_return` 依 `ain_return_slots_type` 把 `option<T>` 一律當兩槽，`option<wrap<iwrap<T>>>`（原版 `0x653420` 算 3 槽）的回傳只留最上面兩槽。受影響的有 `SceneParentStack@Get`（沒有呼叫者）、`PlayerAction@InnerAction::get`、`BattleSkillCollection@Find`、`SceneBattle@GetAvailableBattleSkill`、`BadgeSlot@FindSkillFromBadgeId` 等 13 個函式（多在戰鬥）。在 fixture 試做時發現，影響面大，沒有併入本組。
3. 已知而未變的差異：據點與人材一覽的背景是黑的、關閉教學後停在 0.975 倍（Motion Time 修正後仍然如此）、人材一覽右上的 TP／房間欄沒有出現、卡片數值欄位與原版不同。
4. 原版擋掉停用按鈕點擊的位置、原版低階 widget 用哪個狀態判定點擊、矩形四角與 `矩形模式` 的原版語義、motion 長度改變後與原版的時間軸對照，都**未驗證**。

## 11. 第二輪審查後修正（`6582e38`，2026-09-30）

第二輪審查（`22339c1`..`ce5c75a` 整體、`fb28975` 本身）都是 fix-first：高 1（標題按鈕在按下新遊戲後仍可點）、中 1（兩槽 `Reverse` 修對後標題構圖約 3 秒後被打亂）、低 2（鑑賞模式返回時 VM_ERROR、點擊派送略過系統元件）。四項追到同一類根因：**v14 的 delegate 對目標物件是強參照，場景物件因此從不釋放**，以及 **A_REF 對陣列不複製**。本節記錄原版語義（全部為靜態反組譯）、修法、連帶暴露的問題與驗證。臨時追蹤碼只在 repo 外的除錯副本；並排圖在 `$PORT/reports/title-review-20260930/`（不提交）。

### 11.0 結論

1. **高：標題按鈕仍可點** 的根因不是 `6d39915` 的元件位置，而是 `SceneTitle` 從不釋放：它把自己的方法註冊成按鈕事件與更新事件（`DG_NEW_FROM_METHOD`），xsystem4 v14 的 delegate 對目標物件加參照，形成循環；`Scene::RunResult<SceneTitle, TitleResult>` 放手後 `SceneTitle` 與它的 `SceneContext` 都活著，`SceneContext@1` 不執行，`EraseLayer` 不發生，標題圖層（controller 2）的按鈕一直留著。`6d39915` 讓子元件位置乘上父元件的 3 倍，這些看不見的按鈕才移到畫面中央而被點到。原版的 delegate **不持有目標物件**（§11.1），修正後按下新遊戲，標題按鈕隨圖層釋放。（已驗證：反組譯、fixture TR1、GUI）
2. 同一個循環也讓教學（`SceneTutorial`）、鑑賞模式、成就等所有場景不釋放。修正後：關閉教學後據點背景出現、不再停在 0.975 倍（與 Wine `base/b04` 相同），「下一步」→ 階段選擇 → 選卡片可以進入春銷，原本在 `PE_AddController` 結束的路徑走得通；鑑賞模式返回不再 VM_ERROR（低 1）。（已驗證：GUI）
3. 場景真的會被釋放後，暴露並修正四個相依的問題，都照原版：解構子內的釋放要立即完成（§11.1.2）、`RemoveController`／`ReleaseActivity` 的清單是 delegate index（§11.1.3）、一個錯讀成員的 CParts 釋放特例（§11.1.4）、`InputDisabler` 的全螢幕矩形終於會被釋放後才看得到的懸停與點擊規則（§11.1.5、§11.4）。
4. **中：構圖被打亂**：原版 `A_REF` 對陣列一律**複製**（§11.2）。依原版改為複製可確定元素語義的陣列（數字、字串、通用 wrap／介面／option 元素）；struct 值陣列與巢狀陣列沿用共用頁（影響面見 §11.2），並更正 §10.3 表格中 `Reverse` 的呼叫者。修正後標題構圖 25 秒內不變，與 Wine run2 相同。

### 11.1 高：delegate 不持有目標物件

#### 11.1.1 delegate 項目只持有環境

**原版（已驗證：反組譯）**

- 指令分派：`0x667f3d` 以位元組表 `0x668adc` 取索引、跳表 `0x668920` 分派。`DG_NEW_FROM_METHOD`（0xfd）→ `0x66bec0`；`A_REF`（0x79）→ `0x66b790`；`X_SET`（0x104）→ `0x66f870`；`DG_CALL` → `0x66dce0`（與 delegate-args.md 相同，交叉確認對應正確）。
- `0x66bec0` 彈出函式號與物件，`0x679460` 配置 delegate 頁（建構子 `0x651d80`：型別 5、參照數 +0x1c = 1、子物件 vtable `0x811094`），再以子物件 vtable[1]（`0x6522a0`：先清空再加入）加入項目。
- 加入（`0x6522c0`）：函式號 ≤ 0 不加；**環境**只有在函式描述（每筆 0x6c 位元組）+0x54 的 lambda 旗標成立時才取目前的 local page，否則為 -1；接著 `0x652210(obj, fun, env)`：
  - `0x67ac30(obj)` 取得物件頁（型別 4 才算 struct），把**這個 delegate 的 heap index** 推入物件頁 +0x2c 的清單（`0x418940`）；
  - `env != -1` 才 `0x679f10`（參照 +1）；
  - **物件本身不加參照**。
- 物件釋放：`0x679f90` 在參照數為 1 時依型別分派，struct 走 `0x67b0d0` → vtable[1] `0x6813c0`：以 `0x669cb0` 執行解構子、`0x656cf0` 釋放成員，最後 `0x681f70`：對 +0x2c 清單中（去重後）每個 delegate 呼叫 `0x652970`，由後往前刪掉目標是這個物件的項目（vtable[3]，同時釋放環境）。
- 清空 delegate（`0x652620`）時對每個項目以 `0x681df0` 把自己從物件的清單移除，並釋放環境。
- AIN 佐證：4,030 個名稱含 `<lambda` 的函式中，1,031 個有 `is_lambda` 旗標，其中 1,030 個用到 `X_GETENV`；沒有旗標的 2,999 個都不用 `X_GETENV`（以 FUNC 區塊掃描，推定與原版 +0x54 為同一旗標）。

**xsystem4 修正前**：`c3b5ff0` 讓 v14 delegate 對物件與環境都加參照（當時是為了避免回呼時物件已被釋放），並對每個 `DG_NEW_FROM_METHOD` 都把目前 local page 當環境。於是「物件把自己的方法交給別人保管」就是循環：`SceneTitle`、`SceneTutorial`（HANDOFF 已記錄 ref 4 全部來自按鈕事件）、`Motion::Executer`（uaf.md 另案）、`CParts`（`DeletedEvent` 的 lambda）都不會釋放。

**修法**

- `src/page.c`：v14 的 delegate 項目 (物件, 函式, 環境) 只持有環境；`variable_type` 對 delegate 頁只把第三格當參照，複製與刪除 delegate 頁因此只動環境。新增物件 → delegate 的對照表（`delegate_page_register`）：delegate 頁存進 heap slot 時（`heap_set_page`／`heap_alloc_page`，所有加入項目的路徑都經過這裡）登記其中的 struct 目標；struct 頁最後一個參照放掉、解構子執行完後，`delegate_target_freed` 刪掉所有目標是它的項目並釋放環境（`0x681f70` → `0x652970`）。清單中失效的 delegate 在使用時略過、清單變長時清理。ResumeLoad 讀回 heap 後重建對照表。
- `src/vm.c`：`DG_NEW_FROM_METHOD` 只對 `is_lambda` 的函式帶環境。物件為 -1 時沿用舊的「以 local page 當物件」（不持有，影響只在 `this` 無意義的靜態 lambda）。
- 回呼執行期間，呼叫框本來就會暫時持有物件與環境（`delegate_obj_ref`），目標在回呼中被放掉也不會提早釋放。

#### 11.1.2 解構子放掉的物件立即解構

- 場景會釋放以後，第二次進入鑑賞模式時頁首文字與按鈕都不見：`SceneCgMode` 的 `CActivityWrap@1` → `AFL_Activity_Release` 先移除使用者元件（`CallUserComponentReleaseEventWithChild` → `RemoveComponent`），再 `ReleaseActivity` 釋放整棵元件樹。xsystem4 的 `heap_unref` 在釋放途中把新歸零的物件排入佇列，等外層物件整個處理完才處理，所以被移除的 `Header` 元件要到 `ReleaseActivity` 之後才解構；那時它自己的 activity 元件已被上層一併釋放，`Header` 的 activity 找不到子元件、`Caption1/2` 從未從元件管理器移除，第二次進入時沿用舊的（元件號碼指向已釋放的元件）。（已驗證：追蹤）
- 原版是 C++ 參照計數，歸零即解構。修正：解構子執行期間，由它的程式碼放掉的物件開一個巢狀的釋放迴圈立即處理（`heap_set_defer_frees`，只處理本層排入的項目），外層的佇列不受影響。解構子巢狀深度上限由 4 提高到 64（原版沒有上限；超過時會印警告，不再默默略過）。只作用於 v14。

#### 11.1.3 `RemoveController`／`ReleaseActivity` 回傳 delegate index

- 原版 `RemoveController`（PartsEngine 跳表 4，`0x57ba2a` → `0x53fd90`）：`0x53d5f0` 依 ID（controller +0x40）找位置，`0x53d500` 以 `0x5386c0` 遞迴收集每個元件的 **+0x88**，刪除 controller，`0x53f850` 把清單寫入 AIN 的陣列。+0x88 是 `SetEventID` 寫入的 delegate index（`0x57cb71` → `0x58b180` → `0x56d580` 寫 +0x88 與 +0x8c 的 unique ID；`GetDelegateIndex` 讀 `0x56d620`）。`ReleaseActivity`（跳表 26，`0x57be7d` → `0x540050` → `0x55e400`）以 `0x56d620` 收集同一個值。（已驗證）
- AIN：`parts::detail::EraseLayer` 與 `activity::detail::Release` 把清單交給 `CPartsMessageManager@ReleaseFunctionSetList` → `ReleaseFunctionSetWithoutGC`（`CallFunctionDeleted` 觸發 `DeletedEvent`，`CPartsFunctionSet@Release`）。
- xsystem4 修正前：v14 的 `RemoveController` 包裝完全不寫清單（註解寫「只是診斷用」），舊的共用實作寫的是元件號碼（上游寫的是 delegate index）；`ReleaseActivity` 也不寫。`DeletedEvent` 因此從未觸發，函式集合也從未歸還。修正後兩者都寫 delegate index。

#### 11.1.4 移除 CParts 的釋放特例

`delete_struct` 有一段從 v14 fork 帶來的特例：`parts::detail::CParts` 頁釋放時，把**成員 0** 當元件號碼釋放。成員 0 其實是 `<vtable>` 陣列（號碼是成員 1 `<Number>`），等於釋放「號碼剛好等於那個陣列 heap slot」的元件。以前 CParts 被 delegate 循環留住，很少走到；修正 §11.1 後每幀都有 CParts 釋放，heap slot 長到 90 萬以上就可能釋放到真正的元件。原版由解構子 `CParts@1` 自己判斷 `<AutoRelease>` 再呼叫 `Release`，特例刪除。

#### 11.1.5 空的懸停狀態

- 修正 §11.1 後 `InputDisabler`（`Motion::Join` 期間的全螢幕矩形）在 Join 結束後被釋放；修正前它們永遠留著（`z = 2147483647`），在第一次 Join 之後吃掉所有懸停，也是點擊派送必須略過 1000001000 以上元件的原因。懸停因此第一次真正作用，教學頁的 `Image0` 在游標下被切到 on-cursor 狀態：pactex 給它三個 ＣＧ部件 狀態，只有普通狀態在執行期設了 CG，on-cursor 是空的，教學頁就在游標下消失。（已驗證：追蹤）
- 全遊戲 pactex 的低階元件中，1,759 個 on-cursor 狀態沒有一個有 ＣＧ名，699 個普通狀態有；原版不會讓這些元件在游標下消失。修正：`parts_set_state` 把「沒有 CG 的 CG 狀態」當成未初始化，停在下一層狀態（推定：原版的狀態切換本身沒有逐指令追到）。只作用於 v14。懸停與點擊一樣不理會隱藏或完全透明的元件。

### 11.2 中：A_REF 對陣列複製

**原版（已驗證：反組譯）**

- `A_REF`（`0x66b790`）→ `0x6794d0` 依頁型別：字串建新字串；**陣列**以 `0x679350` 建新陣列再由 `0x67ffb0` 複製；struct 建新 struct 並以 vtable[1] 指派；delegate 建新 delegate 並以 `0x652710` 加入全部項目。四種都是複製。
- `0x67ffb0`：元素型別 int／float／bool／long（10、11、47、92）直接複製記憶體；其他逐元素 `0x6801d0` → `0x656d60`：字串（12）複製內容、struct（13）值指派、delegate（63）與陣列（79）複製、wrap／參照 wrap／介面（82、87、89）走 `0x656f7c`：複製各槽、對新物件加參照（**共用物件**）。
- 陣列指派 `X_SET`（`0x66f870`）也是把來源內容以 `0x67ffb0` 複製進目的陣列。

**xsystem4 修正前**：v14 的 `A_REF` 對 struct 與陣列都回傳同一頁（加參照）。`ArrayExtensions::GetReverse<IRectParts&>` 的 `result = source`（`.LOCALREF source; A_REF; X_ASSIGN 1`）因此與 `TitleCharacterView.m_uiCharacters` 共用同一頁，`Reverse` 就地反轉了角色陣列；`UpdateCharacterPos` 以同一個 index 取 `m_characterPos`，每個角色被放到對稱位置。`fb28975` 之前逐槽反轉把陣列弄壞、`UpdateCharacterPos` 等於沒作用，反而看起來正確。

**修法與範圍**：`A_REF` 對 v14 一維陣列：數字與字串陣列照 `copy_page` 複製（字串每個元素一個新的字串 slot）；通用陣列（wrap、介面、option 元素）建新頁放相同的值，並對頁面拆除時會釋放的每個值加一次參照，元素因此共用（與原版 wrap／介面元素相同）。**沒有改**的部分：struct 值陣列（原版逐元素值複製）、巢狀陣列、struct 值本身的 `A_REF`、`X_SET` 的內容複製，仍共用頁。理由：xsystem4 的 v14 陣列不保留元素型別，通用陣列的 `array.struct_type` 同時被當成元素槽數與 struct 編號（`Array_Alloc` 對 0x10002 會寫入 2），無法可靠判斷元素是否為 struct 值；struct 值的深複製在 xsystem4 會連同 wrap 成員一起複製（`vm_copy_page`），風險大於收益。AIN 中 `A_REF` 約 1.7 萬處，可由前一指令判定是陣列的至少約 770 處（字串陣列 494、wrap 陣列 102、數字 59、介面 31、struct 值約 80）。後續見 §11.6。

### 11.3 低：鑑賞模式返回時 VM_ERROR

- 不是 `fb28975` 引入：`22339c1` 的返回鈕點不到（點擊目標 0），`6d39915` 讓它點得到之後才走到這條路徑。原因與 §11.1 相同：`SceneOmake`、第二個 `SceneTitle` 的圖層沒有釋放，下一個 `AddController` 時作用中的 controller 不在最上層。修正 §11.1 後，標題 ↔ 鑑賞模式可以往返兩次再開新遊戲，沒有錯誤；第二次進入時的頁首與按鈕由 §11.1.2、§11.1.3 修正。
- 原版 controller 的語義與 xsystem4 仍不同，沒有改：`AddController(-1)` 插在作用中者之後、`RemoveController` 刪掉作用中者後改由它下面那一層作用（`0x53d500`），ID 與位置分開；xsystem4 仍以位置當 ID、刪除後改由最上層作用。已測的路徑都是後進先出，沒有碰到差異（未驗證其他路徑）。

### 11.4 低：點擊派送與懸停一致

- 修正前 v14 派送一律略過 1000001000 以上與不可點擊的元件，對話框的全螢幕遮擋層擋不住點擊。略過系統元件的起因是 §11.1.5 的 `InputDisabler` 洩漏；原版 `InputDisabler@SetPartsParam` 把兩個 1280×720 的矩形設成 `EnableInputProcess` = 1、`Clickable` = 1、`Transparent` = 0（AIN），也就是要擋住並接收點擊。
- 修正：由前往後，第一個顯示中、命中、可點擊或不透過游標（オン指針透過 0）的元件決定結果：可點擊的收到 MouseClick；只擋游標的（圖片、面板）讓點擊不再往後傳，改送全畫面點擊。與懸停選擇游標下元件的規則相同。原版派送本身沒有逐指令追到，「擋游標的元件把點擊變成全畫面點擊」是推定（ADV 以全畫面點擊推進，背景圖不能吞掉點擊）。

### 11.5 驗證

- **新模式 `title-review`**（`probe/title_review_fixture.inc`，TR1–TR7 各自 fork）：TR1 真 bytecode `SceneTutorial@RegisterEvent` 的弱目標、TR2 解構子內的巢狀解構（真 `CBackLogUnit@1`）、TR3 `RemoveController`／`ReleaseActivity` 的 delegate index 清單、TR4 真 bytecode `GetReverse<IRectParts&>`／`GetReverse<ActionTarget&>`／`GetSort<string>` 不改來源、TR5 系統元件與只擋游標的元件的點擊、TR6 空的 on-cursor 狀態、TR7 CParts 釋放不動 `<vtable>` 號碼的元件。
  | 版本 | 結果 |
  |---|---|
  | `ce5c75a`（`before-check.sh ce5c75a title-review`） | rc 89，7/7 失敗（[輸出](base-ui/title-review-before-ce5c75a.txt)） |
  | `6582e38` | 7/7 通過（[輸出](base-ui/title-review-after.txt)） |
- `observer`、`reentrancy` 兩個既有模式原本斷言「delegate 對物件加參照」，改成弱目標語義（物件參照數不變、環境保留；回呼中 Clear 時物件由集合根與暫時的 pin 持有）。改後的 fixture 在 `ce5c75a` 上失敗（`observer_fixture.inc:67`、`delegate_reentrancy_fixture.inc:101` 的斷言），本組通過。
- **verify-step**：53 個模式在預設與 `XS4_PROBE_GBK=1` 兩種組態都 `VERDICT PASS`，sanitizer 0（[摘要](base-ui/review2-verify-summary.txt)；`diff_sha dd3cfd8d779a` = 本組 src/include 相對 `ce5c75a` 的差異）。
- **gui-run 150 秒**（optimized build）：MSG 88，對白與 `fb28975` 的 150 秒執行逐行相同；assertion 0、堆疊溢位 0、UNIMPL 0、VM 錯誤 0；framebuffer 40 張（最後一張 PNG 完整）；58.82 fps、>50 ms 間隔 1 個、最長 70.6 ms；峰值 RSS 506 MB（`fb28975` 607 MB）（[摘要](base-ui/review2-gui-summary.json)）。
- **逐格截圖**（`gui-run.sh` 新增 `RUN_FROM_TITLE=1`、`RUN_AUTO_CLICK_SEQ`、`RUN_SHOTS`；`ce5c75a` 以 `git archive` 加上同一個截圖排程測試設施建置）：
  | 情境 | 本組 | `ce5c75a` | 原版 |
  |---|---|---|---|
  | 標題 9–36.5 秒，每 0.5 秒一張 | 每張與 12 秒那張的平均差 ≤ 9.7（只有背景動畫） | 13.0 秒起角色被放到對稱位置（差 60.8，最大 65.5） | Wine `run2/f000`–`f630`（約 12 分鐘）構圖不變，與本組相同 |
  | 新遊戲（14 秒）後連點畫面中央，62–68 秒點原本四個標題按鈕的位置 | 900016 之後只有 ADV 系統元件與全畫面點擊；75 秒內 MSG 68；無錯誤 | 之後每次點擊都打到舊的標題按鈕 900018（16 次），MSG 0，約 38 秒 `FooterButton.jaf:53/54`、`CActivityWrap.jaf:20` 斷言 | 按下新遊戲後標題按鈕消失 |
  | 標題 → 鑑賞模式 → 返回，兩次，再開新遊戲 | 900019、900102、900118、900201、900214；第二次進入頁首與按鈕都在；進入開場；無錯誤 | 第一次返回（約 24.7 秒）`PE_AddController` VM_ERROR 結束 | 無原版截圖 |
  | 開場 → 據點 → 教學翻頁、關閉 → 下一步 → 選卡片 | MSG 88；關閉教學後據點背景出現、倍率 1.0；約 94 秒「今天要選哪件事做呢？」、約 104 秒春銷環節；無錯誤 | （`fb28975`）背景黑、0.975 倍；選卡片後 `PE_AddController` VM_ERROR | Wine `base/b04` 版面相同（原版背景有模糊，見 §11.6） |
  | 人材 → 卡片 → 研修（開出 YesNo 對話框）→ 點對話框按鈕以外 → 取消 | 對話框外的點擊由遮擋層接收（全畫面點擊，對話框不關）；取消回到 STATUS | 沒有在 GUI 重跑；TR5 在 `ce5c75a` 上派送略過遮擋層，點擊落到後方元件 | 無原版截圖 |
  並排圖在 `$PORT/reports/title-review-20260930/`（不提交）。

### 11.6 沒有修的部分與新發現

1. **A_REF 只複製一部分陣列**（§11.2）：struct 值陣列、巢狀陣列、struct 值本身的 `A_REF`、`X_SET` 的內容複製仍共用頁。要照原版 `0x67ffb0` 逐元素複製，v14 陣列要先保留元素型別。
2. **controller 語義**（§11.3）：ID 與堆疊位置分開、`AddController(-1)` 插在作用中者之後、移除後由下一層作用（`0x53d500`）、輸入只給作用中的 controller（`0x53e690`）都沒有實作。
3. **返回鈕在某些路徑沒有反應**：STATUS → 一覽後的「返回」（§10.7 第 1 項），以及 YesNo 對話框取消後 STATUS 的「返回」，都是 `PE_BeginInput` 沒有再被呼叫、點擊進不了派送。本組沒有改變這個行為，原因未查。
4. **據點環節／春銷環節的橫幅文字留在畫面上**：橫幅的色帶離開後文字仍在原位（據點畫面、人材、春銷畫面都看得到）。`fb28975` 已經如此（150 秒執行的 68 秒影格：色帶離開、文字留著），之前被沒有釋放的教學黑色底圖蓋住；原版沒有（Wine `base/b04`）。`PhaseBar@FadeIn(false)` 以 `Motion::Create` 把 `Clipper` 移到 X 1280，文字是 `AnimateText` 的子元件，推定是 clipper 移動或裁切沒有作用在文字上（未驗證）。
5. 據點背景沒有模糊（原版有；有步驟的構築部件仍走 CG 後備，§10.4）。YesNo 對話框後方是黑底，與原版沒有對照（未驗證）。
6. 推定而沒有逐指令確認的：空的 on-cursor 狀態停在下一層（§11.1.5）、只擋游標的元件把點擊變成全畫面點擊（§11.4）、`is_lambda` 與原版 +0x54 為同一旗標。`EnableInputProcess` 仍是 stub。
7. 物件為 -1 的 `DG_NEW_FROM_METHOD` 仍以 local page 當物件；加入項目時以 `delegate_contains` 去重，與原版的加入流程是否一致未驗證。
8. 29 個介面實作的 vtable 偏移大於 1（最大 316），通用兩槽陣列拆除時會對偏移槽做 unref（修正前就存在的風險；GUI 追蹤中低號 slot 的參照數 120 秒內穩定為 1）。
9. 時間軸：`InputDisabler` 會被釋放、點擊不再穿透後，Join 期間的點擊由遮擋層接收；開場到據點的時間與原版沒有對照。
10. 記憶體：峰值 RSS 150 秒 506 MB（開場）、499 MB（據點到春銷）、541 MB（人材與對話框），`fb28975` 為 607 MB；heap 成長本身沒有處理。
