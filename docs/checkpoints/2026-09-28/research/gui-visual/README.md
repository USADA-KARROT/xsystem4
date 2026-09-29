# 使用者回報的畫面問題（2026-09-29）

使用者觀看 GUI 測試時回報三個現象：左側角色沒有面向對話框、角色與名牌講完話後不消失而一直疊加、部分文字字距太擠。另有已知的字型缺字（例如「菈」顯示成方框）。本目錄是唯讀調查的結果；修正進度見文末。截圖證據與原版實機截圖都留在 repo 外。

| 問題 | 根因 | 信心 | 報告 |
|---|---|---|---|
| 左側角色朝向 | 原版把放在左側（位置 0–2）的立繪左右翻轉：`AdvStand@PosType::set` 設 `Reverse`，最後呼叫 `PartsEngine.SetComponentReverseLR`（EXE 寫入元件 `+0xaa`）。xsystem4 的 `PE_v14_SetComponentReverseLR` 是空函式，Get 與 TB 版本沒有註冊，`struct parts` 也沒有翻轉欄位。全遊戲 4,614 次登場中 52% 在左側 | 高 | [facing.md](facing.md) |
| 立繪與名牌不消失 A | GBK 修正前，`String.Split` 把「／」拆成兩個位元組，從立繪檔名取不到角色名：「立繪去除」找不到人，「立繪變更」一律改到第一個立繪。已由 `6400e3c` 修正 | 高 | [persist.md](persist.md) |
| 立繪與名牌不消失 B | 已在畫面上的立繪與名牌，退場（250 ms 淡出並外移 100 px）、換位（200 ms 移動）、名牌隱藏（150 ms 淡出並左移 30 px）都沒有作用；新登場的淡入正常。根因：v14 函式呼叫的介面參數（`AIN_IFACE`）與有值的參照型 option 參數是借用傳入，被呼叫端返回時卻會釋放，`function_call` 與 delegate 路徑沒有替它們加參照。`Motion::Create`／`Motion::Executer@0` 的 `IParts` 參數因此把 `AdvStand.m_parent` 的 sprite 與名牌 root 提早釋放，slot 被重用後方法呼叫拿到函式號 -1 被略過 | 高（headless 與 GUI 皆已重現並修正） | [uaf.md](uaf.md)、[persist.md](persist.md) |
| 字距 | 原版每字前進 = 字寬 + 字距，雙位元組字寬 = 字級、單位元組 = (字級+1)>>1，兩者再加 2×max(ceil(太さ), ceil(縁取り))（`0x69c7a0`）。xsystem4 只在載入 .fnl 時才把外框算進前進量（CN 沒有 .fnl），也沒保存太さ，半形字寬用浮點 size/2 再逐字截斷。有外框的文字每字少 4–6 px；562 組文字樣式中 274 組偏窄。無外框的主對白視窗字距與原版相同。「……」「——」看起來擠是字型差異（VL Gothic 對 MS Gothic） | 高 | [spacing.md](spacing.md) |
| 字型缺字 | 預設 gothic 字型 VL Gothic 缺 107 個對白用字（例如 U+83C8「菈」），約 11.4% 的對白有方框；repo 內的 HanaMinA 全部都有。建議在 `ft_font_get_glyph` 對 `FT_Get_Char_Index == 0` 的字改用 HanaMinA 描繪，前進量繼續依碼點決定，字距才不會變 | 高 | [spacing.md](spacing.md) §4 |

修正順序建議見 [HANDOFF.md](../../HANDOFF.md) 的任務清單。

## 修正進度（2026-09-29）

| 問題 | commit | 驗證 |
|---|---|---|
| 左側角色朝向 | `4a82758`：`struct parts` 加 `reverse_lr`／`reverse_tb`，Set／Get 的 LR／TB 四個函式照原版語義實作（未知號碼 setter 忽略、getter 回 false），CG 繪製與像素點擊判定和 `sprite_deform` 的翻轉 XOR | `parts-reverse`：`da2c22f` 上 3 項失敗，修正後全過；45 模式 `VERDICT PASS` |
| 字型缺字 | `05d2441`：`ft_font_get_glyph` 在字型沒有該字、另一套 TrueType 有時改用它的字形，前進量不變。`9f81bd9` 限定只在 GBK 字元規則且無 .fnl 時啟用（側審查 D4） | 150 秒 GUI：名牌完整顯示「綺菈綺菈」；`text-metrics`：SJIS 組態不再 fallback |
| 字距（修法 A 與半形寬） | `05d2441`：GBK 字元規則且無 .fnl 時，外框計入前進量，半形寬改為 (字級+1)>>1 | 150 秒 GUI：名牌與有外框視窗的字距與原版截圖一致；主對白視窗不變 |
| 字距（修法 B，側審查 D1–D3） | `9f81bd9`：同一組態下照原版 `0x69c7a0` 的字格排版：e = max(ceil 太さ, ceil 縁取り)（各自不超過字級），字寬依首位元組，字格 = 字寬 + 2e、前進量 = 字格 + 字距；`TextSurfaceManager.GetFontWidth` 回傳同一個字格（`0x69fb30`）；`PE_SetFont` 與 `PE_SetMessageWindowTextFont` 保存太さ。SJIS 與有 .fnl 的遊戲不變 | 新模式 `text-metrics`（12 組 pactex 樣式）：`ca2ebff` 上 131 項失敗、SJIS 1 項失敗，修正後全過，SJIS 輸出與 `05d2441` 之前逐行相同；47 模式兩種組態 `VERDICT PASS`；150 秒 GUI MSG 88，event 視窗 25 px／字（原為 24）、名牌 30／27、主對白 24，與原版截圖一致。詳見 [spacing-fix.md](spacing-fix.md) |
| 立繪與名牌不消失 B | `9e30c0f`：`function_call` 對 `AIN_IFACE` 參數、以及判別槽為 0 且內含參照／wrap／介面的 option 參數加參照；`delegate_copy_argument` 與 `delegate_call` 套用同樣規則（原版 `0x657430`） | `iface-arg`：`7e16dee` 上 6/6 失敗，修正後全過；46 模式在預設與 GBK 組態 `VERDICT PASS`。150 秒 GUI 修正後三次 MSG 88，講完話的立繪會退場，名牌疊字 0 張（修正前 11、12 張），`heap_alloc_slot` 警告 15→0，峰值 RSS 1.76／2.05→1.33／1.38 GB。詳見 [uaf.md](uaf.md) |
| delegate 參數的伴隨槽（uaf.md 另案） | `2005274`：`delegate_call` 改為一格堆疊對一個參數變數（原版 `0x66dce0` → `0x657430`），不再把兩槽參數的 void 伴隨變數當成下一個參數；option 規則在全部複製完才套用 | 新模式 `delegate-args`：`6421e6e` 上 DA1–DA3 失敗（Tutorial selector 的 delegate page 被釋放、Select 四個元素只跑兩次、特殊客人收入函式釋放借用的 SpecialCustomer），修正後全過；48 模式兩種組態 `VERDICT PASS`；150 秒 GUI MSG 88、assertion 0、堆疊溢位 0，追蹤中 Select 遇到 ref 0 delegate page 1→0 次。詳見 [delegate-args.md](delegate-args.md) |
| 翻轉作用在整棵元件樹（側審查 D5–D7） | `0ab8476`：旗標移到 `parts_params`（local＝自己、global＝沿父元件鏈 XOR），照原版 `0x535260` → `0x4e6d80` 以錨點為軸鏡像自己的方框，父元件翻轉時子元件位置也以父元件錨點鏡像；CG、TEXT、FLAT、FLASH、3D 圖層與 alpha clipper 共用同一變換，矩形與像素點擊判定、`Parts_GetPartsUpperLeftPos` 跟著翻轉；surface area 的錯位（D6）一併消失 | 新模式 `reverse-inherit`：`b5d8b9e` 上 3/3 失敗（RI1 17 項），修正後全過，其中 RI3 以真 bytecode 跑 `AdvStand@Move` 跨側與 `Motion::EndAll`；49 模式兩種組態 `VERDICT PASS`；150 秒 GUI MSG 88、assertion 0、堆疊溢位 0，8 張 framebuffer 與修正前逐像素相同。開場沒有跨側移動（追蹤 40 次呼叫都在葉元件），以臨時注入確認 rect 根翻轉會讓立繪以錨點精確鏡像、朝向與原版翻轉的立繪相同。詳見 [reverse-inherit.md](reverse-inherit.md) |
| 據點畫面缺件（D1–D5、D7）與 SetButtonEnable | `6d39915`：v14 父元件立即掛上（`0x58f060`），pactex 依原版 `部件タイプ` 名稱表設型別（UC 17 與其名稱、數據；低階的數字 24、ＣＧ判定 27），`SetComponentType` 對低階元件只改狀態、`GetActivityParts`／`Get/SetUserComponentData` 實作，`Array.Add` 兩槽、`MainEXFile.Col` 回 list 元素數，`編輯上表示`、父元件倍率作用在子元件位置與文字、字型數字、`SetButtonEnable`／`IsButtonEnable` | 新模式 `base-ui`：`22339c1` 上 5/5 失敗，修正後全過；50 模式兩種組態 `VERDICT PASS`；150／220 秒 GUI MSG 88、assertion 0、堆疊溢位 0；點擊序列下底列、Day／金錢、貼紙、橫幅、教學兩頁與原版截圖一致，「下一步」進入階段選擇；背景仍黑等差異見 [base-ui.md](base-ui.md) |

### 追蹤結果：站錯邊與不退場可能同源

以臨時追蹤（未提交）跑 80 秒 GUI，`SetComponentReverseLR` 共收到 28 次呼叫，對象都是頂層元件（沒有父元件與子元件），型別為未初始化或組合型立繪（`PARTS_CONSTRUCTION_PROCESS`，繪製走 CG 路徑）。只有一組立繪收到「翻轉」，其餘 13 組都是「不翻轉」。

AIN 的規則是 `AdvStand@GetSide`：位置 0–2 為左側、3–5 為右側，`Reverse = (Side == Left)`。所以收到「不翻轉」的立繪，劇本是放在右側；它們卻出現在畫面左邊，推定是換位移動沒有生效、停在初始位置。這與「已在畫面上的立繪退場、換位、隱藏都無效」是同一個現象，應一起追查，先確認 AdvStand 的位置是以 Motion 還是直接設定座標、以及該呼叫實際作用在哪個元件號碼上。

### 追蹤結果二：退場、換位、名牌隱藏無效的直接原因（2026-09-29）

以臨時追蹤（未提交）各跑 90 秒 GUI：

1. **設定從未送出**：每個新立繪登場時，淡入（Alpha 0→255）與移入（X 120→220 或 1160→1060）都送到它的元件號碼；之後同一號碼再也沒有收到任何 Alpha、位置或顯示設定。`ReleaseParts` 在 90 秒內一次都沒有被呼叫。所有記錄到的設定都打在存在的元件上，不是打錯號碼。
2. **VM 靜默略過方法呼叫**：CALLMETHOD 拿到函式號 -1 而略過的次數：`Motion::Executer@0` 92、`Motion::Create` 53、`AdvNamePlate@Hide` 28、`AdvStand@IsMotion::get` 10、`AdvStand@Move` 8、`AdvStand@MoveOut` 6。另有 `Motion::ExecuterCollection` 的 vtable 讀取越界。
3. **被讀取的物件 slot 已經是字串**：`AdvStand@MoveOut` 以 `m_parent`（`wrap<iwrap<ISpriteParts>>`，成員 3）取得介面物件後讀它的 `<vtable>`（成員 0）；追蹤到的 slot（例如 9020347、9518975）當時的 heap 型別是 `VM_STRING`，不是 page。也就是立繪的 sprite 物件已被釋放，slot 又被配置給字串（use-after-free）。之後讀 vtable 失敗，函式號變成 -1，呼叫被略過，Motion 與 Show 都沒有執行。`Motion::Executer@0` 也有同樣的型別不符。

結論：根因是 `wrap<iwrap<T>>` 成員（以及 Motion 執行器）所持有物件的參照計數錯誤，物件在仍被持有時就被釋放。下一步應以 headless fixture 重現：用真 AIN 建立 `AdvStand` 並經 `AFL_Parts_CreateSprite` 設定 `m_parent`，跑過一般的函式返回與 DELETE 之後檢查該 slot 的 ref 與型別，再往 X_ASSIGN（v14 不加參照）、`.LOCALREF` 暫存的 DELETE、`function_return` 與 wrap box 解包逐一排查。這也可能與 `heap_alloc_slot: skipped in-use entries` 警告及記憶體成長有關。

### 修正結果（`9e30c0f`）

上面兩段追蹤的現象都來自同一個根因：介面參數在函式返回時多被釋放一次。headless 以真 `AdvStand` 重現了 MoveIn 之後 sprite 被釋放（`Motion::Executer@0` 的 RETURN 2→1、`Motion::Create` 的 RETURN 1→0）；修正後 MoveIn／Move／MoveOut 每一步 `m_parent` 都是同一個存活的 sprite。GUI 追蹤中，`AdvStand@Move`／`MoveOut`／`IsMotion::get` 與 `AdvNamePlate@Hide` 的 -1 呼叫全部消失，`Motion::ExecuterCollection` 的 vtable 越界與 `heap_alloc_slot` 警告也隨之消失。「劇本放在右側卻停在左邊」的立繪，推定是換位或退場沒有生效的結果；修正後 `Move` 不再被略過，畫面上也不再有殘留的舊立繪，但與原版實機截圖逐格對照尚未做。根因、原版依據、before/after 與另案清單見 [uaf.md](uaf.md)。
