# 翻轉旗標作用在整棵元件樹（側審查 D5–D7，2026-09-29）

修正 commit：`0ab8476`（接在 `b5d8b9e` 之後，本機 commit，尚未推送）。原版 EXE 只做靜態反組譯（capstone），沒有執行。
證據等級：**已驗證**＝逐指令或執行結果直接佐證；**推定**＝靜態推論合理，但沒有逐指令或執行期確認；**未驗證**＝尚無證據。
路徑記號：`$PORT`＝`<PORT>`。分析工具與證據圖在 repo 外的 `$PORT/reports/reverse-inherit-20260929/`，截圖不提交。

---

## 0. 結論

1. `4a82758` 的 `SetComponentReverseLR/TB` 只讓 CG 類元件在自己的方框內翻轉。TEXT、FLAT、rect 等型別不讀旗標，子元件也不繼承父元件的翻轉。所以 `AdvStand@Move` 跨側時設在立繪 rect 根元件上的 ReverseLR、戰鬥 `PlayerViewPartsLayer@Reverse::set` 設在兩個 frame rect 上的翻轉，都沒有效果。（已驗證，原始碼與 fixture）
2. 原版把翻轉當成每一層變換的一部分（已驗證，逐指令）：
   - 旗標沿父元件鏈以 XOR 累積。
   - 翻轉以元件的**錨點**（自己的位置）為軸，鏡像自己的方框。
   - 父元件翻轉時，子元件的**位置**也以父元件的錨點為軸鏡像，子元件的繪製再跟著鏡像一次。
   - 點擊判定與 `Parts_GetPartsUpperLeftPos` 走同一個矩陣。
3. 修正照這個語義實作，CG、TEXT、FLAT、FLASH、3D 圖層與 alpha clipper 共用同一個變換，點擊判定的方框與像素反變換跟著翻轉。沒有翻轉時，矩陣與修正前逐運算相同。（已驗證：`reverse-inherit` 修正前 3/3 失敗、修正後全過；GUI 150 秒與修正前逐像素相同的畫面有 8 張）
4. D6（surface area 加翻轉會錯位）一併修好：翻轉軸改成錨點後，只顯示一部分的 CG 仍停在錨點上。D7（`parts-reverse` 沒測繪製與點擊判定）由新模式補上。
5. 開場 150 秒內**沒有**跨側移動（臨時追蹤：`SetComponentReverseLR` 40 次，全部打在剛建立、還沒有父子關係的立繪影像上）。白髮的珀爾諾從右側換到左側，是腳本另外 `■立繪('珀爾諾／基本／基本','左左')`，建立了一個新立繪（`InnerAddStand` 一律 `NEW AdvStand`），不是 `AdvStand@Move`。她在 `4a82758` 就已面向對話框，這次沒有變化。跨側移動的繪製改用臨時注入驗證（見 §5.3）。

---

## 1. 原版怎麼組合翻轉（`dohnadohna_dump_SCY.exe`）

### 1.1 每個元件的更新：`0x535260`

| 位址 | 內容 | 狀態 |
|---|---|---|
| `0x535482` | `0x579460`：把父元件累積好的 sprite 參數（`[ebp+0x74]`）複製一份 | 已驗證 |
| `0x5355c5`、`0x5355ce` | 讀自己的 `+0xa9`（ReverseTB）、`+0xaa`（ReverseLR），連同位置、`+0xa8`（OriginPosMode）、`+0xd8..+0xe8`（倍率、旋轉）當參數 | 已驗證 |
| `0x535752` | `0x579bb0`：把這一層套到複本上，結果存進自己的 `+0x250` | 已驗證 |
| `0x535b57`..`0x535bf4` | 以自己的 `+0x250` 為父參數，對 `[+0x68]..[+0x6c]` 的每個子元件遞迴呼叫 `0x535260` | 已驗證 |

這個更新對每個元件都跑，與元件型別無關。

### 1.2 一層的變換：`0x579bb0` → `0x4e6d80`（呼叫點 `0x579e78`）

`0x4e6d80` 的參數是位置（xmm2/xmm3）、倍率、旋轉 X/Y/Z、`+0xa0`/`+0xa4`、ReverseLR（`[ebp+0x28]`）、ReverseTB（`[ebp+0x2c]`）。

| 位址 | 動作 |
|---|---|
| `0x4e6d9f`..`0x4e6dd6` | `[state+0x49] ^= ReverseLR`、`[state+0x48] ^= ReverseTB`（累積旗標，XOR） |
| `0x4e6e1b`..`0x4e6e5a` | `0x72e4a0` 建 `S(LR ? -1 : 1, TB ? -1 : 1, 1)` |
| `0x4e6e5f`..`0x4e6f0e` | `+0xa0`/`+0xa4` 不為 0 時再乘 `T(-a0, -a4)`（`0x5c3ef0`） |
| `0x4e6f13`..`0x4e6f60` | `0x5c3ae0` 建本層 `S(scale) · R · T(pos)` |
| `0x4e6f71`、`0x4e6f82` | 兩次 `0x72e730`：`tmp = 翻轉 · 本層`，再 `tmp · 父矩陣`，存回 `state+8` |

`0x72e730` 的輸出第 r 列是 `Σ A[r][k] · B[k]`，也就是 A·B（先套 A 再套 B）。所以一個頂點依序經過：自己的翻轉、`T(-a0,-a4)`、倍率、旋轉、位移，再經過父元件的整串變換。翻轉在位移之前，軸就是元件的錨點（位置）。

`+0xa0`/`+0xa4` 在 PartsEngine 的 API 裡找不到 setter，xsystem4 也沒有對應欄位，視為 0。（推定）

### 1.3 方框、點擊判定與左上角

| 位址 | 內容 | 狀態 |
|---|---|---|
| `0x5b71b0`、`0x5b7200` | 原點模式的偏移：模式 1/4/7 → 0，2/5/8 → −w/2，3/6/9 → −w；Y 同理（1–3、4–6、7–9） | 已驗證（跳表 `0x5b71dc`/`0x5b71e8`） |
| `0x57a6b0` | 點擊判定：以上述偏移為方框左上角，四個角經 `0x5c3910` 用累積矩陣轉到螢幕，再用 `0x57aa60` 做兩個三角形的點內判定 | 已驗證 |
| `0x534bb0` | `Parts_GetPartsUpperLeftPos`：方框左上角（原點模式偏移）經 `0x57b6a0` 轉到螢幕 | 已驗證 |
| `0x534510` | 絕對位置 X：點 (0,0)（錨點）經同一矩陣轉到螢幕 | 已驗證 |

所以方框在本地座標是 `[q, q + 尺寸]`（q＝原點模式偏移），翻轉以 0（錨點）為軸。xsystem4 的 `calculate_offset` 與原版的原點模式偏移相同。

### 1.4 FLAT

`0x525db0`（FLAT 的矩陣更新）在 `0x525e83` 呼叫 `0x537b00`，沿 `[parts+0x7c]` 把每一層的 40 位元組紀錄（第 0、1 byte 是 ReverseLR、ReverseTB，接著 `+0xa0`、`+0xa4`、位置、倍率、旋轉）收成陣列，`0x4d2130` 反轉成根到葉，再逐筆用同一個 `0x4e6d80` 套上（`0x525ee3`）。`0x51ac20` 先收 FLAT 自己的 key，再在 `0x51ad4f` 接上元件鏈。FLAT 的翻轉因此與其他型別相同。（已驗證）

TEXT、FLASH 的繪製是否也用 `+0x250` 的矩陣，沒有逐指令追到。`0x535260` 對所有型別都算這個矩陣，推定一樣適用。（推定）

### 1.5 AIN 端的使用者

| 呼叫 | 翻轉對象 | 說明 |
|---|---|---|
| `AdvStand@PosType::set` → `AdvStandImage@Reverse::postset` | 兩個影像元件（葉） | `Reverse = (Side == Left)`，見 [facing.md](facing.md)。`4a82758` 已處理 |
| `AdvStand@Move`（FUNC 31645） | `AdvStandImage.m_parent`（rect 根，`RootParts`） | 跨側時 `Motion::Create(RootParts, "Section:AdvStand [Time:200|ReverseLR:0 1]")`（到左）或 `"…ReverseLR:1 0"`（到右）；不重設葉的旗標 |
| `Motion::Executer@SetPartsValue`（FUNC 27019） | Motion 目標 | `ReverseLR`（TargetType 19）＝`Math.Round(value) != 0`，是絕對設定，不是切換 |
| `PlayerViewPartsLayer@Reverse::set`（FUNC 33600） | `m_uiFrame`、`m_uiPreviousFrame` 兩個 rect | 戰鬥，rect 底下的子元件要跟著鏡像 |

立繪的結構：`m_parent`（sprite，負責 X 位移）→ rect 根（1280×720，原點模式 8，位置 0,0）→ 兩個影像（原點模式 8，位置 0,0）。rect 根與影像的錨點重合，所以 rect 翻轉等於影像以自己的底部中央為軸翻轉。

依 AIN，放在左側（葉已翻轉）的立繪再移到右側時，rect 被設成 0，XOR 結果仍是翻轉，也就是在右側面向畫面外。這是原版的行為（推定，沒有原版實機畫面佐證），本修正照做，`reverse-inherit` RI3 記錄了這個結果。

---

## 2. 修正（`0ab8476`）

| 位置 | 內容 |
|---|---|
| `parts_internal.h` | `reverse_lr`／`reverse_tb` 從 `struct parts` 移到 `struct parts_params`：`local` 是元件自己的旗標（getter 回傳這個，與原版讀 `+0xaa` 相同），`global` 是沿父元件鏈的 XOR |
| `parts.c` `parts_child_pos` | 子元件的全域位置＝父元件錨點＋父元件 global 翻轉作用在本地位置上（鏡像）。`parts_update_global_pos` 與 `parts_combine_params` 都用它 |
| `parts.c` `parts_set_reverse` | 設本地旗標，更新自己與子孫的 global 旗標，再重算子元件位置 |
| `render.c` `parts_anchor_transform` | `T(global pos) · 祖先翻轉 · Rz · S(global scale) · 自己的翻轉`（祖先翻轉＝global XOR local）。沒有翻轉時與修正前的運算序列相同 |
| `render.c` `parts_box_transform` | 上式再乘 `T(origin_offset)`：方框中一點（相對左上角）到螢幕。CG、3D 圖層、alpha clipper 使用 |
| `render.c` TEXT／FLAT／FLASH | TEXT 以錨點為軸鏡像每個字（仍不旋轉、不縮放）；FLAT 的根矩陣與 FLASH 改用 `parts_anchor_transform` |
| `render.c` `sprite_deform` | 回到上游的寫法：在自己的方框內翻轉，與 reverse 無關 |
| `input.c` `parts_screen_to_box` | 像素點擊判定的反變換：平移 → 祖先翻轉 → 反旋轉 → 除倍率 → 自己的翻轉 → 減原點偏移 |
| `input.c` `parts_hittest` | 矩形判定改用 `parts_screen_hitbox`（方框放到錨點並依 global 翻轉鏡像）。像素判定有翻轉時取像素中心（光柵化也取中心；沒翻轉的元件照舊取左上角，倍率 1 時是同一個 texel） |
| `parts.c` `PE_GetPartsUpperLeftPosX/Y` | 方框左上角經同樣的翻轉（原版 `0x534bb0`），翻轉時是鏡像後的那一角 |
| `PartsEngine.c` | setter 改呼叫 `parts_set_reverse`；getter 回傳 `local` |
| `debug.c` | JSON 的 local/global 參數加上兩個旗標 |

沒改的簡化（修正前就存在，與本項無關）：父元件的倍率與旋轉不作用在子元件位置上；TEXT 不旋轉、不縮放；非像素的矩形判定不計倍率與旋轉；翻轉旗標不寫進 parts 存檔（PE_Save，與 `4a82758` 相同）。

---

## 3. fixture：`reverse-inherit`

`harness/probe/reverse_inherit_fixture.inc`，三個案例各自 fork。

| 案例 | 內容 | `b5d8b9e`（修正前） | `0ab8476` |
|---|---|---|---|
| RI1 | 以真 AIN 宣告（`SetComponentPos`、`Parts_SetParentPartsNumber`、`Parts_SetPartsRectangleDetectionSize`、`SetComponentOriginPosMode`、`SetComponentReverseLR/TB`）建三層元件。父元件 LR 翻轉後，子孫位置以父元件錨點鏡像，`Parts_IsCursorIn` 的方框與 `Parts_GetPartsUpperLeftPosX` 跟著鏡像；子元件再翻轉時孫元件不再相對它鏡像（XOR）；再加 TB；全部還原後回到原值；getter 回傳自己的旗標 | FAIL（17 項：位置、點擊、左上角） | PASS |
| RI2 | `parts_box_transform`（dlsym）在父／子各 4 種翻轉組合、旋轉 30°、倍率 1.5×0.5 下，方框 5 個點的螢幕座標等於獨立算的「錨點＋父翻轉·R·S·子翻轉·(點＋原點偏移)」，`parts_screen_to_box` 反算回原點（共 80 點）；TEXT 與 FLAT 路徑（`parts_anchor_transform`）；surface area 的 CG 在父元件翻轉時仍以錨點為中心（D6） | FAIL（函式不存在） | PASS |
| RI3 | 真 bytecode：`NEW AdvStand`、`AdvStand@Move(LeftCenter)`、`Motion::EndAll`。rect 根經 `Motion::Executer@SetPartsValue` → `CParts@ReverseLR::set` → HLL 設成 1，影像的本地旗標仍是 0，但影像方框的 x=0 畫在錨點＋200（鏡像）；移回右側後復原；放在左側再移到右側的立繪仍是鏡像（原版的 XOR 結果） | FAIL（`parts_box_transform` 不存在；rect 的旗標在修正前也已被設成 1） | PASS |

探針不跑遊戲的 alloc 函式，所有全域變數是 -1。RI3 照原版 alloc（FUNC 0）的 `NEW Motion::ExecuterCollection` 設好 `Motion::Instances::executer`，否則 `Motion::Create` 會對 -1 呼叫 `Add`，Motion 不會執行。立繪 CG 在 headless 載不到，RI3 把兩個影像的方框設成 400×600。

像素判定本身需要 GL 貼圖，探針做不到。RI2 驗的是它使用的反變換 `parts_screen_to_box`。

---

## 4. verify

- `before-check.sh b5d8b9e reverse-inherit`：rc=91，3/3 失敗，sanitizer 0。
- `verify-step.sh`：49 個模式在預設與 `XS4_PROBE_GBK=1` 兩種組態都 `VERDICT PASS`（`deleted-event` 照舊 rc 87），diff sha `50bde4395fb7`。
- 既有的 `parts-reverse`（setter／getter）不變，照舊通過。

---

## 5. GUI

### 5.1 150 秒不回歸

| 執行 | 版本 | MSG | assertion | 堆疊溢位 | `heap_alloc_slot` | 峰值 RSS |
|---|---|---:|---:|---:|---:|---:|
| `dgargs-after-1-2005274`（實作者 A，修正前） | `2005274` | 88 | 0 | 0 | 0 | 1.09 GB |
| `revinh-after-1` | `0ab8476` 的內容（提交前的工作樹，diff sha 相同） | 88 | 0 | 0 | 0 | 1.10 GB |
| `revinh-after-2-0ab8476` | `0ab8476` | 88 | 0 | 0 | 0 | 1.13 GB |

- 修正前後 88 行 MSG 內容逐行相同。
- 40 張 framebuffer 中有 8 張與修正前某一張逐像素相同（例如修正後 t15／t18／t24／t33／t39 對修正前 t14／t17／t23／t32／t38）。其餘差異來自對白與動畫的時序。修正前 t14 與修正後 t15 左側立繪（阿熊）區域的位移估計是 0.00 px、NCC 1.0000。
- t24 等畫面左側的珀爾諾（葉翻轉）與修正前逐像素相同。
- 第二次（已提交版本）MSG 同樣逐行相同，4 張 framebuffer 與修正前逐像素相同；只看左側 430×410 px 區域時，含左側珀爾諾的 t25 等 5 張與修正前逐像素相同，其餘差異落在對白框與名牌的時序。

### 5.2 開場沒有跨側移動（臨時追蹤，未提交）

在 `PE_v14_SetComponentReverseLR` 加 `XS4DBG_REVERSE` 追蹤，150 秒：
- 共 40 次呼叫：`r=1` 6 次、`r=0` 34 次，對象全部是「沒有子元件、還沒有父元件」的元件（立繪影像在 `AdvStand@0` 建構中被設旗標，父子關係要到下一次 `PE_UpdateComponent` 才建立）。
- 沒有任何一次打在有子元件的 rect 上，也就是沒有 `AdvStand@Move` 跨側。

AIN 也吻合：開場的珀爾諾依序是 `■立繪('珀爾諾／基本／哀Ｂ','右右')`（CODE `0x7a20d0`）、`('珀爾諾／基本／樂','右右')`（`0x7a21fc`）、`('珀爾諾／基本／基本','左左')`（`0x7a2340`）。`■立繪` 呼叫 `AdvStandCollection@Add` → `InnerAddStand`，後者一律 `NEW AdvStand` 再 `PosType::set`，所以左左是新立繪、葉翻轉。全遊戲 22 個 `■立繪移動` 都不在開場（最早的 CODE 位址 `0x7cc5d6`）。

### 5.3 跨側翻轉的繪製（臨時注入，未提交）

150 秒內走不到跨側移動，所以另建臨時版本：每次 `PE_UpdateComponent` 後，把「有兩個原點模式 8 子元件、自己也是原點模式 8、位於右半邊」的 rect 根設成 ReverseLR（`XS4DBG_FLIP_RIGHT_ROOTS`）。這相當於 `AdvStand@Move(右→左)` 結束時 rect 的狀態，只是沒有 X 位移。執行 90 秒，注入 5 次。

| 量測 | 結果 |
|---|---|
| 注入版 t16 的珀爾諾臉部（90×80 px）對照一般版 t16，以 x=1140（右右的錨點）為軸的像素鏡像 | 最大差 0（以 1139、1141 為軸時一半以上像素不同） |
| 原版實機截圖 `C_game_015` 左側珀爾諾（原版翻轉）頭髮區為樣板，搜尋一般版 t16 右側 | 原樣 NCC 0.489、鏡像 0.667：反向 |
| 同一樣板搜尋注入版 t16 右側 | 原樣 NCC 0.624、鏡像 0.436：與原版翻轉後的立繪同向 |

也就是說，rect 根的翻轉會讓底下的立繪影像以錨點為軸精確鏡像，朝向與原版被翻轉的立繪相同。跨側移動後的立繪（在左側、rect 翻轉、葉不翻轉）因此會面向對話框。注入與追蹤程式碼沒有提交，執行後已還原並重建。

---

## 6. 未驗證事項

- 真正的 `AdvStand@Move` 跨側移動在 GUI 裡沒有走到（開場沒有；`■立繪移動` 在較後面的劇情），只有 headless RI3（真 bytecode）與 §5.3 的注入驗證。沒有原版實機的跨側移動畫面可對照。
- 放在左側再移到右側的立繪仍保持鏡像（XOR），是依 AIN 與 `0x4e6d80` 推得的原版行為，沒有原版畫面佐證。
- 戰鬥 `PlayerViewPartsLayer@Reverse` 與其他使用者（`FrameLayerImages@SetCoreParam`、`EffectView@SetReverse` 等）在 GUI 走不到，未驗證畫面。
- TEXT、FLASH 在原版是否也經 `+0x250` 的矩陣繪製（推定）；TEXT 的翻轉沒有 GUI 畫面驗證。
- `+0xa0`/`+0xa4` 的額外原點偏移沒有建模（找不到 setter，推定為 0）。
- 父元件的倍率與旋轉仍不作用在子元件位置上（修正前就有的簡化），父元件同時翻轉又縮放、旋轉時與原版不同。
- 像素點擊判定在有翻轉時取像素中心，沒有 GUI 案例（開場的立繪不可點擊）。
- 翻轉旗標不寫進 parts 存檔；CN 是否走 PE_Save／PE_Load 未確認。
- 尚未經獨立反駁者審查；長時間穩定性未測。

---

## 7. 重跑

```bash
H=docs/checkpoints/2026-09-28/harness
bash $H/before-check.sh b5d8b9e reverse-inherit   # 修正前：rc=91，3/3 失敗
bash $H/verify-step.sh <tag>                      # 49 模式
XS4_PROBE_GBK=1 bash $H/verify-step.sh <tag>-gbk
bash $H/gui-run.sh <name> 150
```

靜態反組譯沿用 `reports/gbk-20260929/tools/disas.py`；本次另寫的 xref（找 call/jmp 目標）、函式起點、線性掃描與影像位移估計工具放在 `$PORT/reports/reverse-inherit-20260929/tools/`。
