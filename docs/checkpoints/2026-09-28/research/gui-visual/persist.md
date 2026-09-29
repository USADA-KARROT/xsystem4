# 角色立繪與名牌「不會消失、一直疊加」調查（2026-09-29）

本調查只讀。沒有修改 `$WT`（HEAD `ffa8b63`，`src/` 與 `6400e3c` 相同、無未提交修改），沒有修改遊戲母片，沒有執行遊戲 GUI，也沒有執行 EXE（只用 capstone 靜態反組譯）。
路徑記號：`$PORT`＝`<PORT>`，`$WT`＝`$PORT/worktrees/xsystem4-cn-on-upstream`，`$DUMP`＝`<cn-dump>`，`$RUNS`＝`$PORT/claude-work/runs`，`$EXE`＝`dohnadohna_dump_SCY.exe`。
證據等級：**已驗證**＝有逐指令、原始碼、位元組或逐像素量測直接佐證；**推定**＝靜態推論合理但沒有逐指令或執行期確認；**未驗證**＝尚無證據。

---

## 0. 結論摘要

使用者看到的「角色出來後一直疊上去、講完話還留在畫面上」和「名牌殘影」，由兩層問題疊在一起造成：

1. **（A）修正前的 build：角色名永遠取不到，退場找不到人、換表情改錯人。**（已驗證；`6400e3c` 已修）
   - `AdvStandCollection::CharacterNameFromCgName`（fno 31677）以 `cgName.Split("／")` 取第 2 段當角色名。修正前的 `String_Split`（`7c1daaa:src/hll/String.c:560`）把分隔字串當**位元組集合**，並保留空段；「／」＝`A3 AF` 被切兩次，第 2 段永遠是空字串（「立繪／阿熊／基本／哀Ｄ」切成 8 段，At(1)=""）。
   - 結果：`■立繪去除`（EraseCharacter）以名字找人，永遠找不到，直接 return；`■立繪變更`（Change）以 `Find("")` 找人，永遠命中 `m_uiStands[0]`，把別人的表情 CG 換到第一個立繪上。
   - 截圖：`save-fixes2-gui` t15→t16 左側阿熊變成扎帕；t27、t28、t29、t30 同一個左後方立繪槽依序變成綺菈綺菈、阿熊、珀爾諾、綺菈綺菈（§2.2）。
   - GBK 規則的 build（`gbk-after`）已不再換錯人：t15 右側扎帕由「驚」換成「基本」，左側阿熊不變（§2.3）。
2. **（B）兩個 build 都有：對「已經在畫面上」的立繪與名牌做的退場、移動、隱藏，完全沒有作用。**（現象已驗證；xsystem4 內部機制未驗證）
   - 原版流程：退場＝250 ms「Alpha 255→0＋X±100」Motion（`AdvStand@MoveOut`）；換位＝200 ms X 位移（`AdvStand@Move`）；舊名牌＝150 ms「Alpha 255→0＋X−30」Motion，接著**無條件 `root.Show=false`**（`AdvNamePlate@Hide`）。
   - xsystem4 截圖：該退場的扎帕、珀爾諾留在原位，逐像素完全不變（區塊誤差 0.0～0.6）；該換位的阿熊、扎帕也不動；舊名牌的臉圖與名字不透明、不位移，直到下一次換名才消失。
   - 對照組都正常：**剛建立**的立繪淡入（`MoveIn`）、剛建立的名牌淡入（`FadeIn`）有半透明中間態；既有立繪換 CG（`SetCg`）有效。
   - 所以「疊加」＝舊立繪沒退場＋新立繪照常加上去。名牌殘影也是同一類問題，和 GBK、字型無關。
3. **（B）的 xsystem4 根因目前只縮小到一類，尚未定位到單一行。**
   - 已排除：Motion 字串解析（`gbk-vm` 模式已驗證 Hide／FadeIn 的解析樹正確）、`parts_set_show`／`parts_set_alpha` 的父子傳遞實作（`src/parts/parts.c:489-524` 正確遞迴）、名牌 activity 釋放（舊名牌會在下一次 Hide 時被刪掉）、事件沒觸發（這幾條路徑都用空的 `DG_NEW`，不靠結束事件）。
   - 共同點：失敗的呼叫都經由**長時間持有的 `parts::detail::CParts` 包裝物件**（`AdvStand.m_parent` → `CSpriteParts.m_parts`、`CActivityWrap.m_root`）。能成功的呼叫不是發生在物件剛建立時，就是走字串（activity handle）或 `CConstructionParts.number` 直接給號碼的路徑。
   - 旁證：engine.log 有 `heap_alloc_slot: skipped 1 in-use entries`（free list 內出現仍在使用的 slot，典型的重複釋放／釋放後再引用），而 VM 對無效 vtable 或 fno 0 的 `CALLMETHOD` 會**靜默略過**（`src/vm.c:2827`、`:4802-4808`）。
   - 推定機制：包裝物件的 heap slot 被提早釋放並重用，之後的 `Number::get`、`Show::set`、`Alpha::set` 打到錯的物件或變成空呼叫。**未驗證**，需要 §6.1 的追蹤才能定案。
4. **面向問題是另一個獨立根因**：`SetComponentReverseLR` 是空函式（`src/hll/PartsEngine.c:1452`，`:1635` 註冊），詳見同目錄 `facing.md`。截圖 `save-fixes2-gui` t16 左右兩個扎帕朝同一方向，也可佐證。
5. **影響範圍**：全 AIN 有 `■立繪` 4,614 次、`■立繪去除` 711 次、`■立繪清空` 1,551 次、`■立繪變更` 5,054 次、`■立繪移動` 22 次、`○道白` 29,689 次。所有退場、換位與換講者都會受 (B) 影響。其他 UI 若在既有 parts 上做淡出或 `Show(false)`，推定也受影響（未驗證）。

| 項目 | 信心度 |
|---|---|
| (A) 根因與 `6400e3c` 修正有效 | 高 |
| (B) 現象（退場、換位、名牌隱藏都沒作用） | 高（逐像素） |
| (B) 原版正確行為（AIN 流程） | 高 |
| (B) xsystem4 機制＝長壽 CParts 包裝失效 | 中低（推定） |

---

## 1. 資料與方法

| 資料 | 用途 |
|---|---|
| `$RUNS/save-fixes2-gui/`（08:47，binary `c878d1f2…`，SJIS 字元規則，推定約等於 `ff77c18`） | 修正前畫面 |
| `$RUNS/gbk-after/`（10:38，binary `cdb52fad…`，含 GBK 字元規則的工作樹，推定等於 `6400e3c`） | 修正後畫面；另有 `save-fixes-gui`、`gbk-font-probe` 做交叉確認 |
| framebuffer `xsys4_tNN.png` | 第 N 張約在啟動後 2(N+1) 秒（`src/video.c:504-517`）。四次執行都跑到 MSG 89 |
| `engine.log` 的 `MSG`／`STAGE2_PERF` 行 | MSG 與時間的對應只能精確到 5 秒一格（見附錄 A），截圖再以對白內容與名牌核對 |
| `$DUMP/ain_code.txt`、`functions.txt`、`structures.txt`、`libraries.txt` | 流程追蹤。角色名以遊戲工作副本 AIN 的 STR0 還原（「皂┝」＝**扎帕**） |
| 遊戲工作副本 `dohnadohna.ain` STRT 區段 | 解析 vtable，確認介面槽號（`persist_tools/ifslots.py`） |
| `$EXE` | capstone 靜態反組譯 PartsEngine 的 Show、Alpha、ReverseLR 實作 |

量測工具 `persist_tools/shiftmatch.py`：在兩張截圖間，以平均絕對誤差（0–255）搜尋區塊的最佳位移。誤差 0 代表逐像素相同。

---

## 2. 現象的截圖證據

證據拼圖在 `persist_evidence/`：`stand_exit_gbk_after.png`、`stand_change_sjis.png`、`nameplate_residue.png`。

### 2.1 劇本：第一章開場 ADV（FUNC 34484）

完整時間線見 `persist_evidence/script_timeline_func34484.txt`。行號是 `ain_code.txt` 行號。

| 行 | 指令 | 原版預期的舞台 |
|---|---|---|
| 1348388 | `■立繪(阿熊／基本／基本, 左)` | 左：阿熊 pos1（X=220） |
| 1348400 | `■立繪(扎帕／基本／驚, 右)` | 右：扎帕 pos4（X=1060） |
| 1348415 | `■立繪變更(阿熊／基本／哀Ｄ)` | 阿熊換表情 |
| 1348420 | `■立繪(珀爾諾／基本／哀Ｂ, 右右)` | 右側已有 1 人：扎帕 `Move(3)`，X 1060→940；珀爾諾加在 pos5（X=1140） |
| 1348435 | `■立繪變更(扎帕／基本／基本)` | 扎帕換表情 |
| 1348449 | `■立繪清空(右)` | 扎帕、珀爾諾退場：X+100，250 ms 淡出 |
| 1348454 | `■立繪(綺菈綺菈／基本／樂Ｂ, 右)` | 右：只剩綺菈綺菈 pos4 |
| 1348470 | `■立繪(珀爾諾／基本／樂, 右右, 奧)` | 右：綺菈綺菈＋珀爾諾（後） |
| 1348519–1348524 | `■立繪去除(珀爾諾)`；`■立繪(珀爾諾, 左)` | 右側珀爾諾退場；左側阿熊 `Move(0)`（X 220→140）；珀爾諾加在 pos2（X=340） |
| 1348614–1348617 | `■立繪去除(綺菈綺菈, 左)`；`■立繪去除(珀爾諾)` | 兩人退場 |

位置與 X 座標取自 `GetStandPosFromString`（fno 31555）與 `AdvStand@GetXPos`（fno 31651）：pos0..5 → X 140、220、340、940、1060、1140。pos 小於 3 屬左側（`AdvStand@GetSide`，fno 31652）。

### 2.2 修正前（`save-fixes2-gui`）：換表情改錯人（問題 A）

| 截圖 | 區域 | 觀察 | 原版應為 |
|---|---|---|---|
| t15 | 左 x40–330 | 阿熊（雙手抱胸＝哀Ｄ） | 同左 |
| t16（MSG 33–35，緊接在 1348435 的 `■立繪變更(扎帕／基本)` 之後） | 左 x40–330 | **扎帕（基本、咧嘴）**，右側仍是扎帕（驚）＋珀爾諾 | 左側是阿熊；右側扎帕換成「基本」 |
| t27（MSG 51–53） | 左後 x0–330 | 左後方立繪顯示綺菈綺菈 | 左後方應是阿熊 |
| t28（MSG 54） | 同上 | 同一槽變成阿熊（`變更(阿熊／樂)`） | — |
| t29（MSG 55） | 同上 | 同一槽變成珀爾諾，左側出現兩個珀爾諾 | — |
| t30（MSG 56） | 同上 | 同一槽又變回綺菈綺菈 | — |

這一槽就是 `m_uiStands[0]`（阿熊）。每一次 `■立繪變更` 都改到它，右側的綺菈綺菈表情反而從不變化。這就是使用者看到的「角色一直疊上去」的主要來源之一（在這個 build）。

### 2.3 修正後（`gbk-after`）：退場與換位完全沒有作用（問題 B）

| 截圖對 | 區塊（x0–x1, y0–y1） | 原版應有的變化 | 量測結果 |
|---|---|---|---|
| t13→t14 | 扎帕頭部 1040–1200, 0–90 | 珀爾諾進場時扎帕 `Move(3)`：X −120 | 同位置誤差 **0.0**，最佳位移 dx=0 |
| t16→t17 | 扎帕頭部 1000–1180, 0–90 | `■立繪清空(右)`：X+100，淡出到 0 | 誤差 **0.6**，dx=0（±2 px 時誤差 31.7） |
| t17→t27 | 扎帕頭部 1130–1250, 0–60 | 已退場，不應存在 | 誤差 **0.0**：之後 20 秒都留在原位 |
| t22→t23 | 右側珀爾諾 1180–1270, 230–330 | `■立繪去除(珀爾諾)`：退場 | 誤差 **0.0**；t23 左右各有一個珀爾諾 |
| t22→t23 | 阿熊頭部 150–330, 20–170 | `Move(0)`：X −80 | 誤差 **0.0** |
| t30（約 62 秒，MSG 58–59；在 MSG 60 重新登場之前） | 右側綺菈綺菈、左側珀爾諾 | 1348614–1348617 兩人都已退場 | 仍在畫面上（目視）；右側另有扎帕與舊珀爾諾 |

`save-fixes2-gui` 的同類量測：t13→t14 扎帕誤差 7.0、dx=0；t16→t17 誤差 0.4、dx=0。結果一致。

對照組：`gbk-after` t12 扎帕 `MoveIn` 淡入時是半透明的，t14 珀爾諾進場也正常。所以「新立繪的 Motion」有作用，「既有立繪的 Motion」沒有。
既有立繪換 CG 有作用：`gbk-after` t14→t15 扎帕從「驚」換成「基本」。

### 2.4 名牌殘影（問題 B）

`nameplate_residue.png`，區域 x270–520、y470–550。

| 截圖 | 新講者 | 觀察 |
|---|---|---|
| `save-fixes2` t13 | 扎帕「＊＊」 | 正常 |
| t14 | 珀爾諾「＊＊＊」淡入中 | 新臉圖半透明；前兩顆「＊」是舊名牌，**不透明**；第三顆是新名牌，半透明 |
| t16 | 扎帕「＊＊」 | 顯示「＊＊＊」：第三顆是上一張（珀爾諾）的殘留 |
| t17 | 綺菈綺菈「＊」 | 顯示「＊＊」。上上一張的第三顆「＊」**已不見** |
| t18 | 珀爾諾淡入 | 新臉圖與新名字半透明，底下舊的綺菈綺菈臉圖與「＊」不透明 |
| t37→t38 | 綺菈綺菈→阿熊 | 「阿熊綺□」。第 3–4 格（x408–456, y500–540）最佳位移 dx=0（誤差 12.9，其他位移 ≥55）：舊名牌沒有左移 30 px |
| `gbk-after` t27→t28 | 同上 | 「阿熊綺□」仍在（dx=0，誤差 17.5，其他位移 ≥55）。GBK 修正後照樣殘留 |

結論：
- 舊名牌（臉圖＋名字）在 Hide 之後完全不透明、不位移。
- 它存活到**下一次**換名才被刪掉（t17 證明上上一張已經消失）。
- 新名牌的 FadeIn 正常。

---

## 3. 原版的正確行為

### 3.1 AIN：立繪（全部已驗證，逐指令）

結構（`structures.txt:9548-9575`）：

- `AdvStandCollection { array<wrap<AdvStand>> m_uiStands; array<wrap<AdvStand>> m_uiHideStand; }`
- `AdvStand { string m_cgName; AdvStandPos m_posType; wrap<AdvStandImage> m_uiStand; iwrap<ISpriteParts> m_parent; bool IsFront; }`

**登場** `■立繪`（31544）→ `AdvStandCollection@Add`（31653）：
- 以 `GetCountOfSide(side)` 決定動作：0 人時放在預設位置；1 人時先 `MoveCurrentStand`（31656，舊人 `Move` 到另一格），再加入；2 人以上時不動作。
- `InnerAddStand`（31654）依序執行 `NEW AdvStand` → `SetCg` → `PosType::set`（同時設定 `Reverse = (Side==左)`）→ `MoveIn` → `IsFront` → `m_uiStands.PushBack`。

**退場** 有三條路徑，最後都走同一個函式：
- `■立繪去除`（31545）→ `EraseCharacter`（31658）：以 `FindIndexFromCharacterName` 找人，找不到就 return。
- `■立繪清空`（31546）→ `EraseSide`（31660，以 lambda 依邊找）。
- `EraseAll`（31659）。

三條路徑都呼叫 `EraseIndex`（31669）：
1. `AddToHideStand(stand)`（31670）
2. `m_uiStands.Erase(index)`

`AddToHideStand` 本體（ain_code 1154742–1154763）：
1. `m_uiHideStand.EraseAll(obj => !obj.IsMotion)`：清掉**上一批**已播完的退場立繪。
2. `m_uiHideStand.PushBack(target)`
3. `target.MoveOut(ResolveMotion(easeType, side))`

**`AdvStand@MoveOut`**（31642）對 `m_parent.Core`（ISpriteParts[33]＝`CSpriteParts@Core::get`，回傳自己的 IParts 面）建立下列 Motion：

```
"Section:AdvStand [    Time:250  | Alpha:255 0  | X:<from> <to> <ease>  | Y:<from> <to> <ease>]"
```

- 位移由 `GetMoveFrom`（31643）決定：類型 2 為 X−100，3 為 X+100，1 為 Y+100，4 為 Y−500，5 不動。預設左側用 2、右側用 3（`GetDefaultEaseType`，31678）。
- 若 `IsSkip`（`AdvObject@IsSkipAdv`＝全文略過或已讀略過），會再呼叫 `Motion::EndSection("AdvStand")`，直接跳到終態。

**`AdvStand@Move`**（31645）："Section:AdvStand [Time:200 | X:<from> <to>]"。跨側移動時另對影像 root 加 `ReverseLR:1 0` 或 `0 1`。

**`AdvStand@MoveIn`**（31641）："Section: AdvStand [Time:250 | Alpha:0 255 | X:… | Y:…]"。

**真正刪除 parts**（推定，依 AIN 物件生命週期）：
1. 下一次 `AddToHideStand` 時，已播完的 AdvStand 被移出 `m_uiHideStand`，引用歸零。
2. 連鎖到 `m_parent`（CSpriteParts）與 `m_parts`（`NEW CParts(number, autoRelease=1)`，見 `AFL_Parts_CreateSprite` fno 7955、`CSpriteParts@0` fno 15103）。
3. `CParts@1`（fno 14000）在 `AutoRelease` 為真時呼叫 `IParts[2]`＝`CParts@Release`（14003）→ `parts::detail::Release(number)`。

就算這一步延後，退場 Motion 結束時 alpha 已是 0，畫面上看不到。

**`■立繪變更`**（31551）→ `AdvStandCollection@Change`（31661）：
- 以 `Find(CharacterNameFromCgName(cg))` 找人，找到才 `AdvStand@Change` → `AdvStandImage@Change`。
- action 為 1 時先建交叉淡出緩衝（`[Alpha:255 0|Time:200]`），否則 `SetCg` 加上動作 Motion。

Motion 的參數最後由 `Motion::Executer@SetPartsValue`（27019）寫進 parts：
- 類型 0 → `IParts[63]`＝`CParts@Alpha::set` → `SetComponentAlpha`
- 類型 1、2 → `IParts[41]`、`IParts[44]`＝X、Y
- 類型 19 → `IParts[135]`＝`ReverseLR::set`

槽號由 STRT vtable 解析，見 `persist_evidence/iparts_slots.txt`。

### 3.2 AIN：名牌（已驗證，逐指令）

`AdvNamePlate { IActivity m_act; IActivity m_actHide; string m_currentName; }`（`structures.txt:9531`）

- **`AdvNamePlate@Show`**（31608）：`m_currentName == name` 時直接 return；不同時依序 `Hide` → `Create` → `FadeIn`。

- **`AdvNamePlate@Hide`**（31609）：
  1. `m_actHide = m_act`。舊的 `m_actHide` 在此被 DELETE，**上上一張名牌在這裡釋放**。
  2. `m_act = null`；`m_currentName = ""`。
  3. 若 `m_actHide` 非 null：
     - `Motion::Create(m_actHide.Root, "Section:AdvNamePlate[Time:150|X:0 -30 EaseOutQuad Rel|Alpha: 255 0]")`
     - 若 IsSkip，`Motion::EndSection("AdvNamePlate")`
     - **`m_actHide.Root.Show = false`**（`IParts[60]`＝`CParts@Show::set` → `SetComponentShow`）

  `IParts[60]` 為 Show::set 已驗證兩種方式：STRT vtable；以及 `Create` 內 `FaceBase.[60](臉圖存在)` 的用法。所以原版舊名牌最晚在同一幀就被隱藏，150 ms 動畫只是附帶效果。

- **`AdvNamePlate@Create`**（31610）：
  - `m_act = AFL_Activity_Create("Scene/10_Adv/Main/AdvNamePlate")`。activity 以 `activity::detail::GetFreeName` 取唯一名稱。
  - `root.Z = 10000`；`P_信息窗口表示連動設定(root, 1)`（→ `SetComponentMessageWindowShowLink`）
  - `FaceBase.Show = 有無臉圖`；`GetText("Name").Text = name`

- **`AdvNamePlate@FadeIn`**（31614）：IsSkip 時 return；否則 `Motion::Create(m_act.Root, "Section:AdvNamePlate[Time:150|X:-30 0 EaseIn Rel|Alpha:0 255]")`。

- `m_root` 由 `CActivityWrap@Load`（fno 524）以 `GetParts("ルート部件")` → `AFL_Parts_Wrap(number)` 取得。activity 解構 `CActivityWrap@1`（522）→ `Release` → `AFL_Activity_Release(handle)` → `PartsEngine.ReleaseActivity`。

### 3.3 原版 EXE（capstone 靜態）

PartsEngine dispatcher `0x57b900`：`cmp ecx,0x363`，跳表 `0x589714`，共 868 項。項數等於 AIN 的 PartsEngine 函式數，所以分支號＝AIN 函式索引（已驗證）。

| 函式（索引） | 分支 → 實作 | 行為 | 狀態 |
|---|---|---|---|
| `SetComponentShow`（101） | `0x57d2fd` → `0x58c7a0` | 找到元件後寫 `byte [comp+0xab] = show`（`0x58c7e6`）；找不到不動作 | 已驗證 |
| `SetComponentAlpha`（109） | `0x57d465` → `0x58cc60` | 夾在 0..255，寫 `dword [comp+0xb0]`（`0x58ccd0`） | 已驗證 |
| `SetComponentReverseLR`（141） | `0x57da5e` → `0x58e070` | 寫 `byte [comp+0xaa]`（`0x58e0b6`）；與 `facing.md` 一致 | 已驗證 |
| 繪製前篩選 | `0x533750–0x533769` | `+0xab`（Show）、`+0xac`（ShowEditor）都為真且 `+0xb0`（Alpha）≠0 才畫 | 已驗證 |
| 子元件可見性 | `0x549ed0–0x549fa0` | 走訪子元件陣列 `[+0xbc,+0xc0)`；子可見＝`Show && ShowEditor && (旗標+0xad ? 呼叫者傳入的父可見 : 1)`，結果寫 `[+0xa8]`，再遞迴 `0x54b4c0` | 讀取與合成已驗證；`+0xad` 的語義與遞迴細節推定 |
| Alpha 對子元件的合成 | — | 沒有找到合成點 | 未驗證 |

原版 Motion 系統是 AIN 程式（`Motion::*`），不在 EXE 內。EXE 只提供上面這些 setter。所以「淡出有沒有傳到子元件」取決於 EXE 渲染時是否繼承父層的 show 與 alpha。父層 show 會繼承（見上表）；alpha 屬推定，理由是原版 FadeIn 只對 root 動畫。

---

## 4. xsystem4 的根因

### 4.1 （A）角色名擷取（已驗證；`6400e3c` 已修）

- 修正前 `src/hll/String.c:560`（`7c1daaa`）的 `String_Split`：逐位元組比對分隔字元（`text[i] == seps[j]`），並保留空段。
- 模擬（`persist_tools/charname_sim.py`）：

| CG 名稱 | 修正前 At(1) | 原版 At(1) |
|---|---|---|
| 立繪／阿熊／基本／哀Ｄ | ""（8 段） | 阿熊（4 段） |
| 立繪／綺菈綺菈／基本／樂Ｂ | ""（8 段） | 綺菈綺菈（4 段） |
| 立繪／珀爾諾／基本／哀Ｂ | ""（8 段） | 珀爾諾（4 段） |

  全部 595 個立繪 CG 的結果見 `gbk-20260929/ain.md`。
- 影響點：`CharacterNameFromCgName`（31677）；`AdvStand@CharacterName::get`（31629）；`Find#1`（31673）、`FindIndexFromCharacterName`（31676）的 lambda；`EraseCharacter`、`Change`、`Move`、`Action`、`Replace`。
- 現況：`$WT` 的 `String_Split` 在 `src/hll/String.c:823`，已改成 GBK 字元集合。`gbk-after` 截圖證實 `Change` 對象正確。`EraseCharacter` 是否真的找得到人，截圖無法直接分辨（退場本來就被 B 擋住），屬推定。

### 4.2 （B）對既有 parts 的 Motion 與 Show 無效（現象已驗證，機制未驗證）

| 呼叫 | 目標 | 取得目標的路徑 | 結果 |
|---|---|---|---|
| `MoveIn`（新立繪） | 新 sprite | 剛 `NEW` 的 `AdvStand.m_parent.Core` | 有效（t12 半透明） |
| `FadeIn`（新名牌） | 新 activity root | 剛建立的 `CActivityWrap.m_root` | 有效（t14、t18 半透明） |
| `SetCg`（既有立繪換表情） | `CConstructionParts` | 直接用結構內的 `number` 欄位 | 有效（t15） |
| 舊名牌 activity 刪除 | activity | 以 handle **字串** `ReleaseActivity` | 有效（t17） |
| `Move`（既有立繪） | 既有 sprite | 長壽的 `m_parent` → `CSpriteParts.m_parts`（`ref CParts`） | **無效** |
| `MoveOut`（既有立繪） | 同上 | 同上 | **無效** |
| `Hide` 的 Motion 與 `Show=false`（舊名牌） | 舊 activity root | 長壽的 `CActivityWrap.m_root`（`CParts`） | **無效** |
| 退場立繪最終刪除（`CParts@1` → AutoRelease → Release） | 同上 | 同上 | **沒有發生**：扎帕 20 秒後仍在。期間至少有一次 `AddToHideStand`（MSG 44 的 `■立繪去除(珀爾諾)`；GBK build 找得到人屬推定），它應該清掉已播完的扎帕 |

**已排除**（已驗證）：

- Motion 字串解析：`docs/checkpoints/2026-09-28/research/gbk-string-rules/after-6400e3c.txt:88-90`。`Motion::GetCompiled` 對 Hide、FadeIn 兩個字串在兩種字元規則下得到相同的樹，Alpha 255→0 與 X 0→−30 都有解析出來。
- show、alpha 父子傳遞：`src/parts/parts.c:489-524` 的 `parts_set_show`／`parts_set_alpha` 會立即遞迴更新子節點的 global 值；`src/parts/render.c:584-587` 以 `global.show` 決定是否繪製。只要 `SetComponentShow(舊 root 號碼, false)` 真的被呼叫，舊名牌就會消失。
- HLL 綁定：`SetComponentShow`、`SetComponentAlpha` 綁到 `PE_SetShow`、`PE_SetAlpha`（`src/hll/PartsEngine.c:574`、`:578` → `src/parts/parts.c:1752`、`:1757`），沒有被 v14 stub 覆蓋。
- 結束事件：Hide、MoveOut、Move 都傳空的 `DG_NEW`，流程不依賴事件。
- Skip 模式：若 IsSkip 為真，終態是 alpha 0，應該「消失得更快」，和觀察相反。

**推定機制**（未驗證）：長壽的 `parts::detail::CParts` 包裝頁被提早釋放並重用，或內容失效。之後經由它的呼叫：

1. `Number::get`（`IParts[31]`）讀到別的物件的值；或
2. X_REF 的 v14 vtable 後備分支在索引越界時推 0（`src/vm.c:4802-4808`），`CALLMETHOD` 遇到 fno 0 或哨兵函式時靜默略過並猜測回傳槽數（`src/vm.c:2827` 起）。

兩種情況都會讓 Show、Alpha、X 設定落空，而且不留警告。
另外 `parts_get()`（`src/parts/parts.c:154`）遇到不存在的號碼會**自動新建一個空 parts**。打錯號碼的 setter 同樣不會有任何可見效果或警告。

旁證：
- `save-fixes2-gui` 與 `gbk-after` 的 engine.log 各有 10 次 `heap_alloc_slot: skipped 1 in-use entries`（已達列印上限 10 次，實際次數未知）與 5 次 `free list exhausted/corrupt`（`src/heap.c:533-553`）。free list 裡出現 ref≠0 的 slot，代表有 slot 被重複放進 free list，或釋放後又被引用。
- 同一個 VM 過去也出過「呼叫錯方法但不警告」的問題（`ff77c18` 提交說明提到 BattlePlayer）。

可能的源頭（都**未驗證**，列出供追蹤）：
- `src/vm.c:4822-4903` X_ASSIGN 的 v14 所有權規則：不加引用、不釋放舊值，完全信任 bytecode 的 DELETE／SP_INC 配對。
- `src/vm.c:1924-2045` `function_return` 的堆疊平衡強制與 WRAP 例外。
- `src/vm.c:4758-4770` X_REF 對 wrap box 的自動解包啟發式。
- `.LOCALDELETE` 與區域頁釋放是否重複 unref。

### 4.3 （C）面向（另案，已驗證）

`src/hll/PartsEngine.c:1452` 的 `PE_v14_SetComponentReverseLR` 是空函式，詳見 `facing.md`。

它和本報告相關的地方：
- `AdvStand@Move` 跨側時的 `ReverseLR` Motion 也會落到這個空函式。
- 修好 (B) 之後，跨側換位才會真的發生，屆時翻轉也要正確。

---

## 5. 建議修法與影響範圍

1. **（A）已由 `6400e3c` 修正，保留回歸測試。** 建議在 `gbk-vm` 模式加一條：以真 AIN bytecode 執行 fno 31677，確認「立繪／阿熊／基本／哀Ｄ」回傳「阿熊」，並對 595 個立繪 CG 全部比對。影響範圍：`■立繪去除` 711、`■立繪變更` 5,054、`■立繪移動` 22、`■立繪替換` 134、`■立繪動作` 417 次呼叫。

2. **（B）先加診斷定位，再修源頭。** 在根因未定前，不建議在引擎端硬塞「退場就直接刪 parts」之類的特例，那只會掩蓋記憶體錯誤。
   - 診斷（暫時性、以環境變數開啟，不改預設行為）：
     - `src/vm.c:2827` 的哨兵略過分支，與 `:4802-4808` 的 vtable 後備分支：印出呼叫者 fno、struct 頁 slot、`page->index`（結構型別）、`heap[slot].seq`。
     - `PE_SetShow`、`PE_SetAlpha`、`PE_SetPos`（`src/parts/parts.c:1742-1760`）：在 `parts_get` 會自動新建（號碼不存在）或號碼 ≤0 時印警告。
     - 以 `-DDEBUG_HEAP` 建置（`src/heap.c` 已有 `alloc_addr`／`free_addr` 紀錄）。對 `skipped in-use` 的 slot 印出最後一次釋放的 bytecode 位址。
   - 修正方向依診斷結果而定：
     - 包裝頁被提早釋放：找出多出來的 unref，修在 VM 所有權規則，也就是 §4.2 列的幾處。
     - 號碼正確但效果被覆蓋：改查 Motion 執行器。
   - 影響範圍：
     - 所有立繪退場（`■立繪去除` 711、`■立繪清空` 1,551）、換位與換講者（`○道白` 29,689 次呼叫中換名的那些）。
     - 推定還包括所有對長壽 `IParts` 包裝做淡出、`Show(false)`、位移的 UI（選單關閉、面板隱藏、場景轉場），未驗證。
     - 這是 VM 層問題，修正前後都要跑完整 44 個探針模式（含 `XS4_PROBE_GBK=1`），並比對 SJIS 遊戲（`test/Run/test.ain`）輸出不變。

3. **（C）** 依 `facing.md` 實作 `SetComponentReverseLR/TB` 與對應的 Get。

---

## 6. 驗證方法

### 6.1 定位 (B) 的 GUI 追蹤（使用現有工具，不改程式）

- `XSYS4_TRACE_FNO=14034`（`CParts@Show::set`）：42 秒後逐指令印出堆疊頂端。
  - 在 `CALLHLL PartsEngine SetComponentShow` 前，堆疊頂兩格是（號碼, 值）。
  - 找「值＝0」的那幾筆，比對號碼是否 ≥900000（activity parts 起始號，`pe_v14_activity.c:79`），以及是否等於當時舊名牌的 root。
- `XSYS4_TRACE_FNO=31609`（`AdvNamePlate@Hide`）：確認 `IFNZ 0x66c578` 分支有走，並記下 `Root::get` 回傳的 struct 頁 slot。
- 再以 `XSYS4_TRACE_FNO=31614`（FadeIn）記下同一張名牌建立時的 root slot。兩者 slot 相同但 `Number` 不同，就證實包裝頁被重用。
- 立繪用 `XSYS4_TRACE_FNO=27007`（`Motion::Executer@0`）：比對同一個立繪 `MoveIn` 與 `MoveOut` 時傳進 `AFL_Parts_Wrap` 的號碼。
- 以 `$WT/docs/checkpoints/2026-09-28/harness/gui-run.sh` 執行，輸出在 repo 外。

### 6.2 headless fixture 建議（新模式 `adv-persist`，可行性未驗證）

1. **包裝物件壽命**：以真 AIN 執行 `AFL_Parts_CreateSprite`（7955），把結果存進 `NEW AdvStand`（31625）的成員。之後執行多次無關的 AIN 呼叫與函式返回，再經 `m_parent.Core.Number`（`ISpriteParts[33]` → `IParts[31]`）讀號碼。
   - 通過條件：號碼等於建立時的號碼，`parts_try_get` 有效，heap slot 的 `seq` 沒有變。
2. **名牌**：依 `activity_text_fixture.inc` 的方式載入真 pactex `AdvNamePlate`。以 fno 31608 先後執行 `Show("A")`、`Show("B")`。
   - 通過條件：`IsComponentShow(A 的 root)==false` 立即成立（Hide 會設 Show=false）；B 的 root 可見；再一次 `Show("C")` 後，A 的 parts 全部被釋放。
3. **立繪退場**：`AdvStandCollection@Add` 之後 `EraseCharacter`，推進 Motion 300 ms。
   - 通過條件：`GetComponentAlpha(sprite)==0`，或該 sprite 已不可見。
   - 為避開 GL，CG 用不存在的名稱。這一點是否可行未驗證。

### 6.3 GUI 截圖比對（修正後）

重跑 150 秒 GUI，用 `persist_tools/shiftmatch.py` 重量同樣的區塊（§2.3、§2.4）。通過條件：

- t16→t17 扎帕頭部：原位置誤差明顯上升（被背景取代），不能再是 dx=0 且誤差 <1。
- t22→t23：右側珀爾諾區塊變成背景；阿熊區塊最佳位移約 dx=−80。
- t13→t14：扎帕最佳位移約 dx=−120。
- 名牌第 3–4 格：換名後不能再以 dx=0 與前一張相符，新名字短於舊名字時該處應為名牌底色。
- 目視：`gbk-after` t23–t32 右側只剩腳本指定的角色，不再同時出現兩個珀爾諾或扎帕。

注意：原版淡出 150–250 ms，2 秒一張的截圖只能看到終態。若要看中間態，要縮短截圖間隔。

---

## 7. 信心度

| 結論 | 等級 | 信心 |
|---|---|---|
| 原版退場＝MoveOut 淡出＋位移，隨後由 m_uiHideStand 清除 | 已驗證（AIN） | 高 |
| 原版名牌 Hide 有 Motion，並立即 `Show=false` | 已驗證（AIN＋vtable） | 高 |
| EXE 的 Show、Alpha、ReverseLR 是寫入 +0xab、+0xb0、+0xaa 的 setter，繪製前檢查 Show 與 Alpha | 已驗證（反組譯） | 高 |
| EXE 子元件可見性會合成父層可見性 | 讀取已驗證，語義推定 | 中 |
| (A) 修正前換錯人、退場找不到人，`6400e3c` 已修 | 已驗證（原始碼＋模擬＋截圖） | 高 |
| (B) 既有立繪與名牌的退場、換位、隱藏沒作用（兩個 build） | 已驗證（逐像素） | 高 |
| (B) 根因＝長壽 CParts 包裝失效（提早釋放重用或 vtable 落空） | 推定 | 中低 |
| (C) ReverseLR 空函式 | 已驗證（原始碼） | 高 |

---

## 8. 未驗證清單

- (B) 的確切失效點：包裝頁是否被重用、`Number::get` 回傳什麼、`SetComponentShow` 實際收到的號碼。
- `EraseCharacter` 在 GBK build 是否真的找到人並把立繪移入 `m_uiHideStand`。
- 原版 EXE 的 alpha 是否繼承到子元件；`+0xad` 旗標的語義。
- 按住 Return（`XSYS4_HOLD_KEYS=13`）是否讓 `IsSkipAdv` 變真。`gbk-after` 的 MSG 60–89 在約 5 秒內全部跑完，推定進入略過模式。
- 其他 UI（選單、面板）是否有同類殘留。
- MSG 與截圖的時間對應只精確到 5 秒一格，個別截圖的 MSG 是以對白內容與名牌核對。

---

## 附錄 A：工具與輸出（全部唯讀）

| 檔案 | 用途 |
|---|---|
| `persist_tools/script_timeline.py` | 從 `ain_code.txt` 列出劇本函式的 CALLFUNC 與 MSG 順序（GBK 亂碼還原） |
| `persist_tools/ifslots.py`、`ainvtable.py` | 解析 AIN STRT vtable，把介面槽號對應到函式（`ainvtable.py` 複製自 `tools/`） |
| `persist_tools/shiftmatch.py` | 兩張截圖區塊的最佳位移與平均絕對誤差（需要 numpy，用 `/usr/bin/python3`） |
| `persist_tools/charname_sim.py` | CharacterNameFromCgName 在兩套 Split 規則下的模擬 |
| `persist_tools/exe_field_scan.py` | capstone 線性掃描 EXE 中存取 `[reg+disp]` 的指令 |
| `persist_tools/make_evidence.py` | 產生 `persist_evidence/*.png` |
| `persist_evidence/script_timeline_func34484.txt` | 第一章開場 ADV 的指令時間線（角色名已還原） |
| `persist_evidence/iparts_slots.txt` | IParts 316 個槽 → CParts 方法 |
| `persist_evidence/*.png` | 截圖證據拼圖（由本機 framebuffer 裁切，勿提交） |

MSG 與時間的對應（`STAGE2_PERF` 下界，秒）：

| MSG 範圍 | `save-fixes2-gui` | `gbk-after` |
|---|---|---|
| 26–29 | 25 | 20–25 |
| 30–35 | 30 | 25–30 |
| 36–39 | 35 | 35 |
| 40–44 | 40–45 | 35–40 |
| 45–50 | 45–50 | 45–50 |
| 51–58 | 55–60 | 55–60 |
| 59–89 | 65–110 | 60–65 |
