# 左側立繪朝向調查（2026-09-29）

> 後續：§2.2、§4、§6 列為未驗證的父子合成語義，已在 [reverse-inherit.md](reverse-inherit.md) 以靜態反組譯確認（沿父元件鏈 XOR，以錨點為軸鏡像，子元件位置也鏡像），並在 `0ab8476` 實作。

本調查只讀。沒有修改 `$WT`（`$PORT/worktrees/xsystem4-cn-on-upstream`，HEAD `ffa8b63`），也沒有修改遊戲母片。沒有執行遊戲 GUI，也沒有執行 EXE（EXE 只用 capstone 做靜態反組譯）。
路徑記號：`$PORT`＝`<PORT>`，`$DUMP`＝`<cn-dump>`，`$FB`＝`$PORT/claude-work/runs/save-fixes2-gui/framebuffer`，`$GAME`＝`$PORT/game-workcopy/多娜多娜 一起幹壞事吧`（只讀）。
證據等級：**已驗證**＝有逐指令、位元組或截圖量測直接佐證；**推定**＝靜態推論合理，但沒有逐指令或執行期確認；**未驗證**＝尚無證據。

---

## 0. 結論摘要

1. **原版會把左側立繪左右翻轉。**（已驗證，AIN 逐指令）
   - 設定位置時，`AdvStand@PosType::set` 會執行 `m_uiStand.Reverse = (Side == Left)`。位置 0..2（左左、左中、左右）屬於 Left。
   - 翻轉值一路傳到 `PartsEngine.SetComponentReverseLR(立繪零件編號, true)`，每個立繪的兩個影像零件（`m_partsStand`、`m_partsBuffer`）都會設定。
   - 立繪資源沒有左右兩版：608 個立繪 CG 名稱都是「立繪／角色／姿勢／表情」，沒有任何方向變體。所以翻轉只能在執行期做。
   - 原版 EXE 的 `SetComponentReverseLR` 會把旗標寫進 parts 物件 `+0xaa`（已驗證）。渲染時，這個旗標會和位置、倍率、旋轉一起放進每層 parts 的變換紀錄（已驗證它有被讀取；實際鏡像貼圖的那一步沒有逐指令追到，屬推定）。
2. **xsystem4 的 `SetComponentReverseLR` 是空函式，渲染器也沒有對應欄位。所以左側立繪永遠不翻轉。**（已驗證，原始碼）
   - `src/hll/PartsEngine.c:1452`：`static void PE_v14_SetComponentReverseLR(int n, bool r) { (void)n; (void)r; }`
   - 這個 stub 在 `:1635` 註冊。它是 commit `1dae88b`（"WIP: UNIMPL HLL batch port from fork — NOT VERIFIED"）加進來的。
   - `struct parts`（`src/parts/parts_internal.h:415-458`）沒有 reverse 欄位。`parts_render_cg`（`src/parts/render.c:144-173`）只認 `sprite_deform`。上游 xsystem4 也沒有實作這個 API。
   - stub 會靜默吞掉呼叫，所以 engine.log 沒有任何警告。
3. **截圖證實目前左側立繪沒有翻轉，面向和右側同一角色相同（朝畫面左方，背對對話框）。**（已驗證，NCC 量測）
   - t19：同一角色（粉髮）左右同框。用右側軀幹當樣板比對左側：原樣 NCC 0.673，鏡像只有 0.376。t20、t21 的結果相同。
4. **影響面很大。** 全遊戲共有 4,614 次「■立繪」呼叫，其中 2,403 次（52.1%）放在左側（「左」1,840、「左左」563）。這些立繪在 xsystem4 上全部面向錯誤。
5. **建議修法：** 實作 `SetComponentReverseLR/TB`、`GetComponentReverseLR/TB`，在 `struct parts` 加旗標，並在 `parts_render_cg` 與點擊判定裡，把旗標和 `sprite_deform` 的翻轉合併（XOR）。這個修法本身足以修好 ADV 立繪。父子繼承語義和 `AdvStand@Move` 跨側移動時的 Motion，仍屬未驗證（見 §4、§6）。

信心度：原版有翻轉＝高；xsystem4 根因＝高；只做葉節點翻轉就能讓立繪與原版一致＝中高（缺原版實機截圖對照）。

---

## 1. 現象與截圖證據

使用者回報：「在左邊出現的角色應該都要面向對話框」。

### 1.1 使用的截圖

來源是 `$FB/xsys4_tNN.png`（1280×720，每 2 秒一張）。另一次執行 `save-fixes-gui/framebuffer` 的畫面序列相同。它的 t19、t20 用同一樣板量測，結果是原樣 0.673／0.663、鏡像 0.376／0.365，結論一致。

| 截圖 | 區域 | 內容 | 觀察 |
|---|---|---|---|
| t12–t15 | 左側 x≈40–430 | 阿熊（綠髮）。腳本 `■立繪('阿熊／基本／基本','左',…)`，也就是 LeftCenter，GetXPos=220 | 未翻轉（xsystem4 沒有任何翻轉路徑，畫面就是資源原本的朝向） |
| t16、t17 | 左側 x≈0–560 與右側 x≈880–1280 | 兩個金髮角色（扎帕的 CG，見 §1.3 附註） | 左右兩個人的刺青袖都在畫面右側，臉都朝畫面左方，代表同向，左側沒有鏡像 |
| t19–t21 | 左側 x≈0–560 與右側 x≈900–1280 | 兩個粉髮角色（綺菈綺菈的 CG） | 領帶、外套條紋左右同向（NCC 量測如下） |
| t25 之後 | 左側 x≈0–360 | 珀爾諾（白髮）。腳本位置是「左左」，GetXPos=140 | 臉朝畫面左方，和她在 t16 右側（「右右」）時同向 |

### 1.2 量測（`tools/facing_check.py`，以灰階 NCC 比對原樣與鏡像樣板）

| 樣板（A 圖、方框） | 搜尋區（B 圖） | 原樣 NCC | 鏡像 NCC | 判定 |
|---|---|---|---|---|
| t19 右側粉髮軀幹 `990,330,1140,520` | t19 左半 `0,200,560,640` | **0.673** @(164,324) | 0.376 | 同向（未翻轉） |
| 同上 | t20 左半 | **0.663** | 0.365 | 同向 |
| 同上 | t21 左半 | **0.659** | 0.358 | 同向 |
| t16 右側金髮背心 `960,230,1080,500` | t16 左半 `0,150,560,600` | **0.333** | 0.228 | 同向（差距較小，姿勢不同） |
| 同上 | t17 左半 | **0.334** | 0.228 | 同向 |

左側最佳位置 x=164 和右側樣板 x=990 相差 826 px。這和 GetXPos 的 LeftCenter(220)／RightCenter(1060) 差 840 px 相符，所以左側那一組確實是 LeftCenter 槽位。

證據圖在 `facing/` 目錄。這些圖由本機 GUI 證據裁切而來，請勿提交。
- `facing/e1_t12_akuma_left.png`：t12 左側阿熊現況，旁邊是同區域的鏡像示意。示意圖的背景也一起被鏡像，只能用來看角色朝向。
- `facing/e2_t16_blonde_left_vs_right.png`：t16 左側金髮、右側金髮原樣、右側金髮鏡像三張並排。
- `facing/e3_t19_pink_torso_ncc.png`：t19 的樣板框（青）、原樣最佳位置（綠）、鏡像最佳位置（紅）。

### 1.3 附註：t16–t24 左側為何出現「別人的立繪」

腳本順序是：
1. `阿熊 左`
2. `扎帕 右`
3. `■立繪變更(阿熊…)`
4. `珀爾諾 右右`
5. `■立繪變更(扎帕／基本／基本)`
6. ……
7. `■立繪變更(綺菈綺菈／基本／驚)`

`AdvStandCollection@Change` 用 `CharacterNameFromCgName` 找角色，但在這次 GUI 使用的建置中，這個函式對所有立繪都回空字串（見 `reports/gbk-20260929/ain.md` §0 第 4 點，String.Split 的 GBK 問題）。結果每次都找到第一個立繪，也就是左側阿熊的槽位，並把它換成別人的 CG。所以 t16 左側是「扎帕／基本／基本」，t19 左側是「綺菈綺菈／基本／驚」，但它們占用的仍是 LeftCenter 槽位。這屬於推定：由腳本順序和畫面比對得出，沒有執行期追蹤。

這屬於「疊上去」問題的範圍，不影響本報告的結論：只要槽位在左側，原版就該翻轉。

---

## 2. 原版的正確行為

### 2.1 AIN 流程（`$DUMP/ain_code.txt`，已驗證）

```
■立繪(cgName, pos, motion, z)                      FUNC 31544 (0x7b38)  L1146853
 └ GetStandPosFromString(pos)                      FUNC 31555           L1147380
 └ AdvStandCollection@Add → InnerAddStand          FUNC 31653 / 31654   L1153647 / L1153716
     ├ NEW AdvStand（AdvStand@0 FUNC 31625 L1152617：先 PosType::set(4=RightCenter)，Reverse=false）
     ├ stand.SetCg(cgName)
     ├ stand.PosType::set(pos)                     FUNC 31633           L1152797
     │    m_posType = pos
     │    m_uiStand.Reverse::set( Side::get() == 0 )   ← 0 = AdvStandSide.Left
     ├ stand.MoveIn(easeType)
     └ stand.IsFront::set(isFront)
AdvStand@Side::get → AdvStand@GetSide(pos)         FUNC 31634 → 31652   L1153622：pos >= 3 ? Right(1) : Left(0)
AdvStandImage@Reverse::set                         FUNC 31693           L1155979：存舊值 → Reverse=value → postset(舊值)
AdvStandImage@Reverse::postset                     FUNC 31694           L1156006：
     m_partsStand(成員3).Core → IParts vtable[135](Reverse)
     m_partsBuffer(成員5).Core → IParts vtable[135](Reverse)
```

`AdvStand@PosType::set` 本體（只列關鍵指令）：
```
PUSHSTRUCTPAGE; PUSH 1; .LOCALREF value; X_ASSIGN 1        ; m_posType = value
.STRUCTREF AdvStand m_uiStand; PUSH 31693                  ; AdvStandImage@Reverse::set
PUSHSTRUCTPAGE; PUSH 31634; CALLMETHOD 0                   ; Side::get
PUSH 0; EQUALE                                             ; == Left
CALLMETHOD 1
```

**vtable 解析（已驗證）**：用 `tools/ainvtable.py` 解析 AIN 的 STRT 區段（v14 vmethods）得到下表。
- 整條鏈是 `IConstructionParts[13]` → `IParts[135]` → `CSpriteParts.m_parts[135]` → `CParts@ReverseLR::set` → `CALLHLL PartsEngine SetComponentReverseLR(Number, value)`。
- IParts 的 135 號槽在每個實作結構中都是 `ReverseLR::set`，例如 CButtonParts vm[47+135]、CFormGroupParts vm[12+135]。

| 結構 | 槽 | 函式 |
|---|---|---|
| `parts::detail::CConstructionParts`（struct 340，IConstructionParts@0） | vm[13] | `CConstructionParts@Core::get`（FUNC 10450，L554653：Sprite::get → ISpriteParts vt33） |
| `parts::detail::CSpriteParts`（struct 358，IParts@0、ISpriteParts@316） | vm[349]=316+33 | `CSpriteParts@Core::get` |
| 同上 | vm[135] | `CSpriteParts@ReverseLR::set`（FUNC 15250，L664531：轉呼叫 `m_parts` vt135） |
| `parts::detail::CParts`（struct 354） | vm[135] | `CParts@ReverseLR::set`（FUNC 14109，L634745：`Number::get` 後 `CALLHLL PartsEngine SetComponentReverseLR`） |

**位置字串對照**：由原始 AIN 的 SWI0 #552 與 STR0 位元組驗證（已驗證），GetXPos 取自 FUNC 31651。

| 腳本字串 | AdvStandPos | Side | GetXPos | 原版翻轉 |
|---|---|---|---|---|
| 左左 | 0 LeftLeft | Left | 140 | 是 |
| 左、左中 | 1 LeftCenter | Left | 220 | 是 |
| 左右 | 2 LeftRight | Left | 340 | 是 |
| 右左 | 3 RightLeft | Right | 940 | 否 |
| 右、右中 | 4 RightCenter | Right | 1060 | 否 |
| 右右 | 5 RightRight | Right | 1140 | 否 |
| 其他（預設） | 1 LeftCenter | Left | 220 | 是 |

**全遊戲統計**（`tools/standpos_stats.py`，掃描 CODE 中所有 `CALLFUNC 0x7b38` 前 4 個 `S_PUSH`）：

| 位置字串 | 次數 |
|---|---|
| 左 | 1,840 |
| 右 | 1,756 |
| 左左 | 563 |
| 右右 | 454 |
| 空字串 | 1 |
| **合計** | **4,614** |

空字串那一次依預設值會落在左側，這裡沒有計入 2,403。

「■立繪移動」共 22 次：左中 6、右中 6、右右 3、左左 3、左 3、左前 1。「左前」不在 STRSWITCH 表內；它是傳給 `AdvStandPos::Parse` 還是 `GetStandPosFromString`，沒有追蹤，屬未驗證。

**資源不分左右版**（已驗證）：用 `tools/afaextract.py` 只讀 INFO 區段，`dohnadohnaCG.afa` 共有 608 個名稱含「立繪」。其中 607 個是 4 段式「立繪／角色／姿勢／表情」，1 個是 3 段式（`立繪／フラット／基本`）。沒有任何名稱含「左」「右」「反」或 L/R 標記。`AdvStandImage@Build`（FUNC 31688）只做 `CutCGCopy` 與 NSFW `TileCGBlend`，沒有鏡像處理。

截圖中，右側立繪（扎帕、珀爾諾在「右／右右」）都朝畫面左方，也就是朝中央的對話框。由此推定資源本身的朝向就是「放在右側時面對對話框」，放到左側時則需要翻轉。這和 AIN 的邏輯一致。

### 2.2 跨側移動（`AdvStand@Move`，FUNC 31645，L1153296）

`AdvStand@Move` 用 200 ms 的 X 位移動畫移動立繪。如果新舊位置的 Side 不同，它會對 `m_uiStand.RootParts`（父 rect 零件）加上 Motion：
- 移到右側：`"Section:AdvStand [Time:200|ReverseLR:1 0]"`
- 移到左側：`"Section:AdvStand [Time:200|ReverseLR:0 1]"`

之後直接寫入 `m_posType`，**不會**重新呼叫 `PosType::set`，所以葉節點的 `Reverse` 保持不變。

Motion 目標型別 `Motion::TargetType` 含 `ReverseLR`，最後推定也會走 `IParts.ReverseLR::set`。`Motion::Executer@SetPartsValue` 確實有呼叫 vt135，但具體分支沒有追。

父、子兩層的 ReverseLR 如何合成（XOR 或覆蓋），這裡沒有驗證。如果是 XOR，「右→左」的結果正確，「左→右」會留下翻轉，看起來像原版的 bug 或另有語義。**未驗證**。這條路徑只出現在 22 次「■立繪移動」中的跨側呼叫，影響很小。

### 2.3 原版 EXE（`dohnadohna_dump_SCY.exe`，capstone 靜態）

PartsEngine dispatcher `0x57b900` 用 `cmp ecx,0x363` 後 `jmp [ecx*4+0x589714]` 分派，跳表索引就是 `libraries.txt` 的 PartsEngine 宣告序。這個對應已由 [481] SetLayoutBoxReturn 的 float 參數形狀交叉確認，見 `reports/overload-20260928/batch2-partial.txt`。

| 索引 | 宣告 | 分支 → 實作 | 行為 | 狀態 |
|---|---|---|---|---|
| [140] | `SetComponentReverseTB(int,bool)` | `0x57da2e` → `0x58dfe0` | 找 parts（`0x540250`）後寫 `byte [parts+0xa9]` | 已驗證 |
| [141] | `SetComponentReverseLR(int,bool)` | `0x57da5e` → `0x58e070` | 找 parts 後在 `0x58e0b6` 寫 `byte [parts+0xaa]`；查無 parts 就不動作 | 已驗證 |
| [143] | `GetComponentReverseLR(int)` | `0x57dab8` → `0x58e1a0` | 在 `0x58e1e8` 讀 `byte [parts+0xaa]` 並回傳 bool | 已驗證 |

在整個程式碼區段做線性反組譯（133 萬條指令）。以 parts 指標為基底的 `+0xaa`／`+0xa9` byte 存取只有下列幾處（`[esp+0xaa]` 區域變數與兩處 `movq` 不算）：
- `0x58e0b6`、`0x58e026`（上表的 setter）
- `0x58e1e8`、`0x58e148`（getter）
- `0x4e8563`、`0x4e8572`：由參數整批寫入，推定是狀態還原或整批設定
- `0x5355c5`、`0x5355ce`：複製到渲染參數後呼叫 `0x537870`
- `0x537b8f`、`0x537ba6`：在 `0x537b00` 裡把 ReverseLR、ReverseTB 放在一筆 40 位元組紀錄的第 0、1 個 byte，後面接位置、`+0xa0`／`+0xa4`，以及 `+0xd8..+0xe8` 的浮點數（推定是倍率與旋轉）。這筆紀錄用 `0x51ae90`（vector push_back）推進去，再沿 `[parts+0x7c]` 遞迴，`+0x7c` 推定是父 parts。

所以原版把 ReverseLR 當成每一層 parts 變換的一部分。最後在頂點或 UV 階段如何鏡像，沒有逐指令追到，屬推定。

---

## 3. xsystem4 的根因

| 位置 | 內容 | 狀態 |
|---|---|---|
| `src/hll/PartsEngine.c:1452` | `PE_v14_SetComponentReverseLR` 是空函式，參數直接丟掉 | 已驗證 |
| `src/hll/PartsEngine.c:1605-1640`（`pe_v14_register_batch`），`:1635` | 註冊上述 stub。`GetComponentReverseLR`、`SetComponentReverseTB`、`GetComponentReverseTB` 都沒有註冊。GetComponentReverseLR 的缺漏在 `$WT/docs/checkpoints/2026-09-28/research/hll-overload-scan.md:262` 已有記錄 | 已驗證 |
| `src/parts/parts_internal.h:404-458` | `struct parts_params` 與 `struct parts` 沒有 reverse 欄位 | 已驗證 |
| `src/parts/render.c:144-173`（`parts_render_cg`） | 只依 `parts->sprite_deform`（1=左右、2=上下；來自 `PE_SetPartsCG*` 的參數，`src/parts/parts.c:1222/1239/1258`）決定是否翻轉 | 已驗證 |
| `src/parts/render.c:608-611` | `PARTS_CONSTRUCTION_PROCESS`（立繪的 `m_partsStand`／`m_partsBuffer`）也走 `parts_render_cg` | 已驗證 |
| `src/parts/input.c:84-87` | 點擊判定只處理 `sprite_deform` | 已驗證 |
| 上游 `~/xsystem4-dev/xsystem4-upstream`（`e8bd5ab`） | 也沒有 `SetComponentReverseLR`。只有 FLAT 關鍵影格有 `reverse_lr`（`src/parts/flat.c:447-466`，用 scale -1 實作） | 已驗證 |

在 xsystem4 上，AIN 端的 `PosType::set` → `Reverse::set` → `postset` 會照常執行（推定：VM 對 property 與 CALLMETHOD 的支援已被其他流程大量使用）。但最後的 HLL 呼叫被 stub 吞掉，零件沒有任何狀態變化。stub 不會發出警告，所以 engine.log（兩次 GUI 執行）裡查不到任何 Reverse 相關訊息。這也是這個問題一直沒被發現的原因。

---

## 4. 建議修法與影響範圍

**建議修法（最小可行）**：

1. `src/parts/parts_internal.h`：在 `struct parts` 加 `bool reverse_lr, reverse_tb;`（放在 `draw_filter` 附近）。`parts_alloc`（`src/parts/parts.c:76-80`）用 `xcalloc`，預設就是 false（已確認）。
2. `src/hll/PartsEngine.c:1452`：改成真正的實作，例如
   ```c
   static void PE_SetComponentReverseLR(int n, bool r)
   { struct parts *p = parts_try_get(n); if (p && p->reverse_lr != r) { p->reverse_lr = r; parts_dirty(p); } }
   ```
   再補上 `SetComponentReverseTB`、`GetComponentReverseLR`、`GetComponentReverseTB`，並在 `pe_v14_register_batch`（`:1605` 起）註冊。原版遇到不存在的 parts 時，setter 不動作（`0x58e0ad` 的 `test ecx,ecx`），getter 回 false（`0x58e1e4` 的 `xor bl,bl`）。
3. `src/parts/render.c:157-173`：把翻轉判斷改成「`sprite_deform` 的翻轉 XOR `reverse` 旗標」，例如
   ```c
   bool fx = (parts->sprite_deform == 1) != parts->reverse_lr;
   bool fy = (parts->sprite_deform == 2) != parts->reverse_tb;
   glm_translate(mw_transform, (vec3){ fx ? common->w : 0, fy ? common->h : 0, 0 });
   glm_scale(mw_transform, (vec3){ fx ? -common->w : common->w, fy ? -common->h : common->h, 1 });
   ```
   原本 `default:` 的 `WARNING("Invalid sprite_deform")` 要保留。翻轉仍在零件自己的方框內進行，位置和原點不變。
4. `src/parts/input.c:84-87`：鏡像規則同上。立繪不可點擊，但其他 UI 可能會用到。
5. 選配：`src/parts/debug.c:397` 的 JSON 輸出加上欄位。`src/parts/save.c:554/604` 的 parts 序列化如果要保存旗標，需要提高 version 維持相容。CN 是否會走到 parts 存讀檔，未驗證。

**暫不處理（未驗證）**：
- 父 parts 的 ReverseLR 是否以 XOR 傳給子 parts，並把子 parts 的位置鏡像到父框內。EXE 的變換堆疊顯示每層都有旗標，但合成方式沒有追到。
- ADV 立繪的旗標設在葉節點（construction parts），所以只做葉節點就夠了。只有 `AdvStand@Move` 跨側移動時的 Motion 會作用在父 rect。

**影響範圍**：`SetComponentReverseLR` 是所有 `IParts.ReverseLR::set` 的共同出口。實作之後，下列呼叫 vt135 的 AIN 函式也會開始生效。清單由 `PUSH 135 / ADD` 後接 `CALLMETHOD 1` 的樣式掃出，屬推定，每一處的接收者沒有逐一確認是 IParts：
- `AdvStandImage@Reverse::postset`（ADV 立繪，本問題）
- `Motion::Executer@SetPartsValue`（Motion 的 ReverseLR 目標，含 `AdvStand@Move`）
- `FrameLayerImages@SetCoreParam`、`BossLayerImages@SetCoreParam`（事件 CG 圖層；`FrameInfoCgLayer` 從資料讀入 `ReverseLR`）
- `DamageCutInView@Run`、`EffectView@CreateCg`、`EffectView@SetReverse`（戰鬥或特效演出）
- `PlayerViewPartsLayer@Reverse::set`
- activityeditor 系列（編輯器，實際執行不到）

這些演出現在同樣沒有翻轉。實作後會更接近原版，但也可能讓其他還沒對齊的差異浮現，需要分別回歸。

---

## 5. 驗證方法

1. **headless 呼叫計數**（不需要看畫面）：在新實作裡加一行受環境變數控制的 trace，例如 `XSYS4_TRACE_REVERSE=1` 時輸出 `ReverseLR n=%d r=%d`。
   - 用 `$PORT/scripts/run-gui-bounded.py`，照這次 run.json 的相同參數（`--skip-title`、自動點擊 1200 ms）跑 20 秒。
   - 預期：第一個 `■立繪('阿熊／基本／基本','左')` 之後，會對 2 個零件各出現一次 `r=1`；`扎帕 右` 則是 `r=0`。
   - 全程 `r=1` 的次數應該和「左側 ■立繪 次數 × 2」同一量級。
2. **GUI 截圖比對**（修正後重跑同一腳本）：用 `tools/facing_check.py` 對新截圖做同樣量測。
   - t19 等效畫面的左側粉髮軀幹應該變成「**鏡像** NCC ≥ 0.6、原樣 NCC ≤ 0.4」，也就是和今天的結果對調。
   - t12 等效畫面的阿熊、t25 之後的珀爾諾（左左）應該朝畫面右方。
   - 右側立繪應該完全不變：拿修正前後同一影格的右半部做逐像素比對，差異應該只來自動畫時序。
3. **渲染單元檢查**（選配）：建立一個左右不對稱的 CG parts，設定 `reverse_lr=1` 後渲染到 FBO，讀回像素，檢查第 x 欄等於原圖的第 w-1-x 欄。再和 `sprite_deform=1` 同時設定，確認兩者互相抵消（XOR）。
4. **原版對照**（未做）：目前沒有原版實機截圖。如果能在 Windows 或 CrossOver 上跑原版到同一段開場（阿熊「左」、扎帕「右」），截一張圖用同一工具比對，就能把 §2.3 的「推定」升級。

---

## 6. 信心度與未驗證事項

| 項目 | 信心度 | 依據 |
|---|---|---|
| 原版把 Side=Left 的立繪設成 ReverseLR=true | 高 | AIN 逐指令加上 vtable 解析，EXE 的 setter 寫入欄位已驗證 |
| xsystem4 不翻轉的根因是 `PartsEngine.c:1452` 的 stub，加上渲染器沒有欄位 | 高 | 原始碼與截圖量測 |
| 目前截圖左側立繪沒有翻轉 | 高 | t19–t21 NCC 0.66–0.67 對 0.36–0.38，t16/t17 0.33 對 0.23 |
| 只做葉節點翻轉就能讓 ADV 立繪與原版一致 | 中高 | 旗標設在葉節點；缺原版實機截圖 |
| 父子合成語義、`AdvStand@Move` 跨側 Motion 的最終狀態 | 低（未驗證） | 只有 EXE 變換堆疊的間接證據 |
| t16–t24 左側槽位被 Change 換成別人 CG 的成因 | 中（推定） | 腳本順序、gbk 報告的 CharacterNameFromCgName 結果、畫面比對 |

---

## 附錄：工具（全部唯讀，放在 `tools/`）

| 檔案 | 用途 |
|---|---|
| `tools/ainvtable.py` | 解析 AIN v14 STRT 區段，列出結構成員、介面偏移與 vmethods。例：`ainvtable.py $GAME/dohnadohna.ain CConstructionParts --vm --fnames $DUMP/functions.txt` |
| `tools/standpos_stats.py` | 統計所有「■立繪／■立繪移動」的位置字串，並列出指定 CG 的呼叫 |
| `tools/afaextract.py` | 只讀 AFA INFO 區段，列出或抽出檔案。本次只用 `list` |
| `tools/facing_check.py` | 灰階 NCC 朝向比對（原樣對鏡像樣板） |
| `tools/facing_evidence.py` | 產生 `facing/e1..e3` 證據圖 |

EXE 反組譯沿用 `reports/gbk-20260929/tools/disas.py`（`/opt/homebrew/bin/python3.14`）。AIN 函式本體沿用同目錄的 `funcbody.py`、`brief.py`、`callers.py`、`ainraw.py`。
