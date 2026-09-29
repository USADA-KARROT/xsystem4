# 使用者回報的畫面問題（2026-09-29）

使用者觀看 GUI 測試時回報三個現象：左側角色沒有面向對話框、角色與名牌講完話後不消失而一直疊加、部分文字字距太擠。另有已知的字型缺字（例如「菈」顯示成方框）。本目錄是唯讀調查的結果；修正進度見文末。截圖證據與原版實機截圖都留在 repo 外。

| 問題 | 根因 | 信心 | 報告 |
|---|---|---|---|
| 左側角色朝向 | 原版把放在左側（位置 0–2）的立繪左右翻轉：`AdvStand@PosType::set` 設 `Reverse`，最後呼叫 `PartsEngine.SetComponentReverseLR`（EXE 寫入元件 `+0xaa`）。xsystem4 的 `PE_v14_SetComponentReverseLR` 是空函式，Get 與 TB 版本沒有註冊，`struct parts` 也沒有翻轉欄位。全遊戲 4,614 次登場中 52% 在左側 | 高 | [facing.md](facing.md) |
| 立繪與名牌不消失 A | GBK 修正前，`String.Split` 把「／」拆成兩個位元組，從立繪檔名取不到角色名：「立繪去除」找不到人，「立繪變更」一律改到第一個立繪。已由 `6400e3c` 修正 | 高 | [persist.md](persist.md) |
| 立繪與名牌不消失 B | 已在畫面上的立繪與名牌，退場（250 ms 淡出並外移 100 px）、換位（200 ms 移動）、名牌隱藏（150 ms 淡出並左移 30 px）都沒有作用；新登場的淡入正常。推定與長期持有的元件包裝物件（`AdvStand.m_parent`、`CActivityWrap.m_root`）被提早釋放或重用有關：兩份日誌都有達上限的 `heap_alloc_slot: skipped in-use entries`。**原因未驗證**，下一步是用 `XSYS4_TRACE_FNO` 追蹤實際傳入的元件號碼 | 現象高、原因中低 | [persist.md](persist.md) |
| 字距 | 原版每字前進 = 字寬 + 字距，雙位元組字寬 = 字級、單位元組 = (字級+1)>>1，兩者再加 2×max(ceil(太さ), ceil(縁取り))（`0x69c7a0`）。xsystem4 只在載入 .fnl 時才把外框算進前進量（CN 沒有 .fnl），也沒保存太さ，半形字寬用浮點 size/2 再逐字截斷。有外框的文字每字少 4–6 px；562 組文字樣式中 274 組偏窄。無外框的主對白視窗字距與原版相同。「……」「——」看起來擠是字型差異（VL Gothic 對 MS Gothic） | 高 | [spacing.md](spacing.md) |
| 字型缺字 | 預設 gothic 字型 VL Gothic 缺 107 個對白用字（例如 U+83C8「菈」），約 11.4% 的對白有方框；repo 內的 HanaMinA 全部都有。建議在 `ft_font_get_glyph` 對 `FT_Get_Char_Index == 0` 的字改用 HanaMinA 描繪，前進量繼續依碼點決定，字距才不會變 | 高 | [spacing.md](spacing.md) §4 |

修正順序建議見 [HANDOFF.md](../../HANDOFF.md) 的任務清單。

## 修正進度（2026-09-29）

| 問題 | commit | 驗證 |
|---|---|---|
| 左側角色朝向 | `4a82758`：`struct parts` 加 `reverse_lr`／`reverse_tb`，Set／Get 的 LR／TB 四個函式照原版語義實作（未知號碼 setter 忽略、getter 回 false），CG 繪製與像素點擊判定和 `sprite_deform` 的翻轉 XOR | `parts-reverse`：`da2c22f` 上 3 項失敗，修正後全過；45 模式 `VERDICT PASS` |
| 字型缺字 | `05d2441`：`ft_font_get_glyph` 在字型沒有該字、另一套 TrueType 有時改用它的字形，前進量不變 | 150 秒 GUI：名牌完整顯示「綺菈綺菈」 |
| 字距（修法 A 與半形寬） | `05d2441`：GBK 字元規則且無 .fnl 時，外框計入前進量，半形寬改為 (字級+1)>>1 | 150 秒 GUI：名牌與有外框視窗的字距與原版截圖一致；主對白視窗不變。太さ仍未計入前進量（修法 B 未做） |
| 立繪與名牌不消失 B | 未修 | 見下 |

### 追蹤結果：站錯邊與不退場可能同源

以臨時追蹤（未提交）跑 80 秒 GUI，`SetComponentReverseLR` 共收到 28 次呼叫，對象都是頂層元件（沒有父元件與子元件），型別為未初始化或組合型立繪（`PARTS_CONSTRUCTION_PROCESS`，繪製走 CG 路徑）。只有一組立繪收到「翻轉」，其餘 13 組都是「不翻轉」。

AIN 的規則是 `AdvStand@GetSide`：位置 0–2 為左側、3–5 為右側，`Reverse = (Side == Left)`。所以收到「不翻轉」的立繪，劇本是放在右側；它們卻出現在畫面左邊，推定是換位移動沒有生效、停在初始位置。這與「已在畫面上的立繪退場、換位、隱藏都無效」是同一個現象，應一起追查，先確認 AdvStand 的位置是以 Motion 還是直接設定座標、以及該呼叫實際作用在哪個元件號碼上。

### 追蹤結果二：退場、換位、名牌隱藏無效的直接原因（2026-09-29）

以臨時追蹤（未提交）各跑 90 秒 GUI：

1. **設定從未送出**：每個新立繪登場時，淡入（Alpha 0→255）與移入（X 120→220 或 1160→1060）都送到它的元件號碼；之後同一號碼再也沒有收到任何 Alpha、位置或顯示設定。`ReleaseParts` 在 90 秒內一次都沒有被呼叫。所有記錄到的設定都打在存在的元件上，不是打錯號碼。
2. **VM 靜默略過方法呼叫**：CALLMETHOD 拿到函式號 -1 而略過的次數：`Motion::Executer@0` 92、`Motion::Create` 53、`AdvNamePlate@Hide` 28、`AdvStand@IsMotion::get` 10、`AdvStand@Move` 8、`AdvStand@MoveOut` 6。另有 `Motion::ExecuterCollection` 的 vtable 讀取越界。
3. **被讀取的物件 slot 已經是字串**：`AdvStand@MoveOut` 以 `m_parent`（`wrap<iwrap<ISpriteParts>>`，成員 3）取得介面物件後讀它的 `<vtable>`（成員 0）；追蹤到的 slot（例如 9020347、9518975）當時的 heap 型別是 `VM_STRING`，不是 page。也就是立繪的 sprite 物件已被釋放，slot 又被配置給字串（use-after-free）。之後讀 vtable 失敗，函式號變成 -1，呼叫被略過，Motion 與 Show 都沒有執行。`Motion::Executer@0` 也有同樣的型別不符。

結論：根因是 `wrap<iwrap<T>>` 成員（以及 Motion 執行器）所持有物件的參照計數錯誤，物件在仍被持有時就被釋放。下一步應以 headless fixture 重現：用真 AIN 建立 `AdvStand` 並經 `AFL_Parts_CreateSprite` 設定 `m_parent`，跑過一般的函式返回與 DELETE 之後檢查該 slot 的 ref 與型別，再往 X_ASSIGN（v14 不加參照）、`.LOCALREF` 暫存的 DELETE、`function_return` 與 wrap box 解包逐一排查。這也可能與 `heap_alloc_slot: skipped in-use entries` 警告及記憶體成長有關。
