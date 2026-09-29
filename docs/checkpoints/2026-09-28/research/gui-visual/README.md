# 使用者回報的畫面問題（2026-09-29）

使用者觀看 GUI 測試時回報三個現象：左側角色沒有面向對話框、角色與名牌講完話後不消失而一直疊加、部分文字字距太擠。另有已知的字型缺字（例如「菈」顯示成方框）。本目錄是唯讀調查的結果，還沒有任何程式修正。截圖證據與原版實機截圖都留在 repo 外。

| 問題 | 根因 | 信心 | 報告 |
|---|---|---|---|
| 左側角色朝向 | 原版把放在左側（位置 0–2）的立繪左右翻轉：`AdvStand@PosType::set` 設 `Reverse`，最後呼叫 `PartsEngine.SetComponentReverseLR`（EXE 寫入元件 `+0xaa`）。xsystem4 的 `PE_v14_SetComponentReverseLR` 是空函式，Get 與 TB 版本沒有註冊，`struct parts` 也沒有翻轉欄位。全遊戲 4,614 次登場中 52% 在左側 | 高 | [facing.md](facing.md) |
| 立繪與名牌不消失 A | GBK 修正前，`String.Split` 把「／」拆成兩個位元組，從立繪檔名取不到角色名：「立繪去除」找不到人，「立繪變更」一律改到第一個立繪。已由 `6400e3c` 修正 | 高 | [persist.md](persist.md) |
| 立繪與名牌不消失 B | 已在畫面上的立繪與名牌，退場（250 ms 淡出並外移 100 px）、換位（200 ms 移動）、名牌隱藏（150 ms 淡出並左移 30 px）都沒有作用；新登場的淡入正常。推定與長期持有的元件包裝物件（`AdvStand.m_parent`、`CActivityWrap.m_root`）被提早釋放或重用有關：兩份日誌都有達上限的 `heap_alloc_slot: skipped in-use entries`。**原因未驗證**，下一步是用 `XSYS4_TRACE_FNO` 追蹤實際傳入的元件號碼 | 現象高、原因中低 | [persist.md](persist.md) |
| 字距 | 原版每字前進 = 字寬 + 字距，雙位元組字寬 = 字級、單位元組 = (字級+1)>>1，兩者再加 2×max(ceil(太さ), ceil(縁取り))（`0x69c7a0`）。xsystem4 只在載入 .fnl 時才把外框算進前進量（CN 沒有 .fnl），也沒保存太さ，半形字寬用浮點 size/2 再逐字截斷。有外框的文字每字少 4–6 px；562 組文字樣式中 274 組偏窄。無外框的主對白視窗字距與原版相同。「……」「——」看起來擠是字型差異（VL Gothic 對 MS Gothic） | 高 | [spacing.md](spacing.md) |
| 字型缺字 | 預設 gothic 字型 VL Gothic 缺 107 個對白用字（例如 U+83C8「菈」），約 11.4% 的對白有方框；repo 內的 HanaMinA 全部都有。建議在 `ft_font_get_glyph` 對 `FT_Get_Char_Index == 0` 的字改用 HanaMinA 描繪，前進量繼續依碼點決定，字距才不會變 | 高 | [spacing.md](spacing.md) §4 |

修正順序建議見 [HANDOFF.md](../../HANDOFF.md) 的任務清單。
