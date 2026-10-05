# 春銷對手與顧客卡片：VM 初始化及 delegate 回傳修正

研究日期：2026-10-05。修正前基準 `057153b`，libsys4 `247f544`。
程式修正 `8b71b7d` 已通過下列驗證及獨立審查，並完成推送雙源核對。文件版本以本報告所在 commit 為準。

## 問題與兩個根因

春銷開始時，兩隊對手為 0/0，顧客總數雖存在卻沒有顧客卡片。兩條資料鏈各有可獨立重現的 VM 問題。

1. `LocalGame@2`（f27419）以 `X_A_INIT 1` 建立長度 2 的 `array<WorkerCollection>`。舊 VM 把 1 當成「每元素兩槽」，得到四個 -1，沒有建立商店或執行建構子。初期人才配置腳本 f26536 的九次附加（第一隊四次、第二隊五次）因此全數跳過。
2. `CustomerCollection.GetOrdered`（f27588）透過 `Select<Customer&, int>`（f36150）呼叫 selector f36151。`Shuffle` 能產生四個索引，selector 也執行四次，但舊 `delegate_return_slots()` 對所有 `AIN_WRAP` 回傳都補成兩槽；`wrap<Customer>` 實際只需一槽。多出的 0 令後續 `Array.PushBack` 的陣列和值錯位，真正結果陣列仍為空。

因此本組先修資料建立與 VM 回傳語義。普通角色使用 CG；自訂角色才使用 Construction，不能以既知 Construction 缺口解釋普通卡片全部消失。

## AIN 呼叫鏈

| 路徑 | 關鍵函式及作用 |
| --- | --- |
| 對手資料 | `LocalGame@2` f27419 → 人才附加 f30851 → `RivalShop` f27407 → 有商店才建立 Worker |
| 對手 UI | `GetRivalShops` f32493 → `InitRivalShop` f32492 → `RivalShopView.Set` f32678 |
| 對手顯示 | `UpdateWorker` f32680：有 Worker 才 Show=true，否則隱藏；CG f32681 |
| 顧客來源 | `CustomerCollection.Count` f27589 讀 IdArray；與排序結果數量分開 |
| 顧客排序 | `Shuffle` f27587 → `GetOrdered` f27588 → Select f36150 → selector f36151 |
| 顧客 UI | `CreateView` f32532 以 `X_A_SIZE` 遍歷結果，逐筆呼叫 `CustomerViewSet@0` f32520 |

修正前追蹤已確認兩個 RivalShop UI 都被找到，但 Set 接到空商店；顧客索引及回呼各四次，結果陣列卻為空。正式修正前後結果見下方驗證紀錄。

## 原版靜態語義

原版解殼傾印僅供靜態反組譯，沒有執行。

| 位址 | 已確認語義 |
| --- | --- |
| `0x66e420` | `X_A_INIT` handler；讀目的陣列宣告及 size |
| `0x679432` → `0x653420` | 每元素槽數來自完整宣告，不是指令 immediate |
| `0x66e697` | 比較 immediate 是否為 0；它是預設初始化旗標 |
| `0x67fee0` | 旗標 0 路徑配置 size 個元素；其邏輯長度同樣是 size |
| `0x67f4a0` → `0x67fe20` → `0x656970` | 非零旗標逐元素作型別預設初始化 |
| `0x656a12` → `0x679b30` → `0x679be8` | 值 struct 配置獨立物件並呼叫真建構子 |
| `0x6569a9`／`0x6569fc` | 數值／bool／enum 寫 0；string 建立字串 slot |
| `0x66db38` → `0x671e90` | `DG_CALLBEGIN` 使用 delegate 完整回傳型別 |
| `0x66dd7c` → `0x672150` | `DG_CALL` 依同一型別取回正確槽數 |
| `0x6537e0` | wrap 的內部 struct／string／delegate／array 為一槽；primitive reference 為兩槽 |

參照的兩槽是位置表示；不應把所有 WRAP 稱為 option 的 value/has_value 配對。interface wrap 另由真 AIN metadata 與 caller 的 `X_ASSIGN 2` 控制確認，不能壓成一槽。

## `X_A_INIT` 全宣告統計

靜態共有 2,186 處：immediate 0 為 2,147，immediate 1 為 39，沒有其他值。這是 VM 指令統計，與 CALLHLL arg3 不同。

| immediate 1 的宣告元素 | 次數 |
| --- | ---: |
| 值 struct：CASVector3D | 7 |
| 值 struct：PersonalityIcon | 4 |
| 值 struct：ProfileLine | 3 |
| 值 struct：RankGauge | 2 |
| 值 struct：BattlePlayerCollection | 2 |
| 值 struct：SASPair<string,string>／StateMark／WorkerCollection／_CSystemSoundChannel | 各 1，共 4 |
| float | 7 |
| int | 3 |
| bool | 1 |
| string | 2 |
| interface ICGParts | 2 |
| option<wrap<Worker>> | 1 |
| ref CEmitterKeyFrameLine | 1 |
| **合計** | **39** |

39 處均已核對宣告；其中兩個 float 全域由真 AIN metadata 補證。七個 float 包含 CASMatrix 的四個巢狀列，**不能因分類為 float 就宣稱本組已修好這四處**。

immediate 0 的 2,147 處也已完成宣告分類（文字宣告 2,129，真 AIN metadata 補齊全域 18）：int 403、float 45、bool 3、string 881、enum 154、具名值型別 239、巢狀 array 5、option 14、ref object 68、wrap<interface> 148、wrap<普通參照> 187。具名值型別此表合併 struct 與 interface。

## Delegate 回傳範圍

全 AIN 有 1,779 個 delegate 宣告、2,043 個 `DG_CALLBEGIN` 靜態點。WRAP 回傳範圍為 44 宣告、33 呼叫點：

| 回傳形狀 | 宣告數 | 呼叫點 | caller 接收 |
| --- | ---: | ---: | --- |
| wrap<普通 struct> | 26 | 19 | 全部 `X_ASSIGN 1` |
| wrap<iwrap<interface>> | 18 | 14 | 全部 `X_ASSIGN 2` |
| wrap<primitive>／wrap<array> | 0 | 0 | 此 AIN 無此類回傳；只可另作合成控制 |

普通 struct 中七個、interface 中四個是沒有實際 call site 的泛型宣告。全部 44 個型別均以真 AIN metadata 核對；33 處 caller 均檢查回傳 label 後的接收槽數。

代表位置：Customer delegate 1370，resume `0x9a98d2` 接一槽；IBattleSkill delegate 1374，resume `0x9a9aec` 接兩槽；IResourceInfo delegate 599，resume `0x5118c2` 接兩槽。

**不可直接將 delegate helper 替換為共用 `ain_return_slots_type()`。** 目前共用 helper 先以 `struc >= 0` 回一槽，對 interface wrap 仍有風險。本組限縮 delegate 已確認形狀，保留 interface／未知形狀及 v13 原行為，不擴大修改共用 helper。

## 相關 HLL 宣告形狀

下表是相關資料鏈的全 AIN 靜態 CALLHLL 統計；本組沒有新增 HLL adapter。

| HLL 宣告 | arg3 → 次數 |
| --- | --- |
| `Array.Clear(ref array<hll_param>)` | 1→3，2→5，65538→7，65539→1 |
| `Array.Add(ref array<hll_param>,hll_param)` | 1→17，2→84，65538→72，65539→13，131074→2 |
| `Array.PushBack(ref array<hll_param>,hll_param)` | 1→89，2→2144，65538→204，65539→61，196610→3 |
| `Array.Shuffle(ref array<hll_param>,int)` | 1→3，2→10，65538→10 |
| `Array.ShallowCopy#1(ref array<hll_param>)→array<?>` | 2→60 |
| `Array.At#1(ref array<hll_param>,int)→ref hll_param` | 1→16，2→125，65538→110，65539→16 |
| `Array.Where(ref array<hll_param>,hll_func)→array<?>` | 1→2，2→44，65538→87，65539→24，196610→3 |
| `Array.QuickSort(ref array<hll_param>)→wrap<?>` | 無 callback 形狀 2→10 |
| `Array.Numof(ref array<hll_param>)` | 1→62，2→297，65538→136，65539→24，共 519 |

ShallowCopy／At 各有兩個同形宣告，上表只有 #1 被實際呼叫。全 AIN 的 `X_A_SIZE` 另有 1,248 處；其 generic stride 與 Numof／At 的 arg3 解讀不可混為一談。

## 實作及探針範圍

- v14 能從目的宣告判定的 int／bool／enum／float／string／值 struct 陣列，可在非零旗標走既有 concrete 表示；值 struct 額外執行各自的真建構子。
- 其餘 generic array 的 stride、初始 -1、rank、解構路徑保留基準行為。尚未實作完整 reference／interface／option 的 native 初始化與 companion-slot 解構。
- 巢狀 CASMatrix 的目的元素宣告傳遞仍有既有缺口；本組不宣稱修好 nested array，也不以調整測試預期掩蓋此限制。
- delegate 區分普通物件一槽與 interface／primitive reference 兩槽，未知形狀保留原綁定。
- `working-cards` 的六個探針範圍：真 LocalGame 指令及 WorkerCollection 建構子；真宣告的直接 scalar 初始化；真 Customer Shuffle／GetOrdered 與 PushBack 堆疊；44 個回傳型別；真 interface delegate／Select；generic interface／合成 nested-option 保留原 fallback。
- 審查後已在探針程式補重新初始化、釋放及參照平衡，並以 live slot 數核對解構結果；六個案例最終全部通過。WC3 僅容許既有的 empty delegate 與 anonymous wrap box 各一個未回收 placeholder，其餘資料物件與陣列均須釋放；此處沒有宣稱零洩漏。

## 正式驗證紀錄

| 項目 | 結果 |
| --- | --- |
| 最終修正 commit／文件 commit | `8b71b7d0df6c7377dd12924efc4937623fcad379`；文件隨本報告所在 commit |
| before-check 基準、模式、失敗數及 sanitizer | `before-check.sh 057153b working-cards`：WC1–4 失敗，WC5–6 通過，exit 1、sanitizer 0；自動還原修正版後 6/6 PASS、src/include dirty=0 |
| 修正後 working-cards case 結果及 sanitizer | WC1–WC6 全部通過、exit 0、sanitizer 0 |
| 兩組 verify-step（default／GBK） | `codex-working-cards-final`／`-gbk`：各 60 模式 VERDICT PASS、sanitizer 0；deleted-event 仍預期 exit 87，其餘 exit 0 |
| 正常速度 GUI 150 秒、MSG／assert／overflow | 150.390 秒，時限停止、exit 0、MSG 88、assert／overflow 0、40 PNG；88 MSG 與上一組正常 GUI 逐位元組相同 |
| 春銷兩隊人數、顧客卡片、互動 | 目標 GUI 92.239 秒、MSG 88、assert／overflow 0、112 PNG；兩隊 4/4、5/5，兩張顧客卡可見。64.016 秒點擊 (166,400) 被元件 900914 擋住，未證實卡片互動成功 |
| Wine 同路線／逐幀比較與仍存在的差異 | 沿用本日上一組原版 218 張影格序列；最終 t48／t50／t55 對四個固定卡框共 12/12 邊界吻合。沒有重新啟動 Wine，未驗證動畫時間等價；具體差異見下節 |
| 審查結論、push 後 ls-remote／gh api SHA | 兩位獨立審查者：無未處理 High／Medium。兩源與本機均為 `8b71b7d0df6c7377dd12924efc4937623fcad379` |

快進探索不取代正常速度 150 秒回歸。員工隨機名字／能力差異也不能直接視為畫面回歸；需分別核對資料數量、元件建立與實際顯示。灰底、Ready 殘留、背景等其他差異須逐項記錄，不能因卡片出現而宣稱春銷完全等同原版。

## 獨立審查與限制

兩位獨立審查者確認主因及最小修法。初版若把 generic 陣列預設值／stride 一起擴大，可能使 option discriminator 或非零 interface offset 被舊解構器當成物件參照；最終已還原全部 generic 行為，避免本組新增上述誤釋放路徑。生命週期測試、v13 plain-struct 控制及 string 初始化／釋放也已補上。沒有未處理的 High／Medium 程式審查項目。

本組不修 CASMatrix 巢狀宣告、generic companion-slot 解構、共用一般函式回傳 helper，以及空 Select 也會留下的兩個既有未回收 placeholder（empty delegate 與 anonymous wrap box）。SJIS 字元測試的 91 行 `SJIS ` 輸出，baseline／default／GBK 最終組逐位元組相同。


### 可公開重跑證據與版本對應

- [預設 60 模式](working-cards-verify-default.txt)、[GBK 60 模式](working-cards-verify-gbk.txt)、[修正前後六案例](working-cards-before-after.txt)、[GUI 與卡框摘要](working-cards-gui-summary.json)。
- 完整 verify 發生在提交前，摘要記錄 `head=057153b diff_sha=3d571556f24f`。測試後另以 SHA256 manifest 核對四個程式／fixture／設定檔未變，提交的修正與受測內容相同；before-check 還原後再次六案例 PASS。
- 正常 GUI 僅持續 Return（13），不使用 Control；目標探索持續 Return+Control（13,17），沿既有春銷路線操作。快進探索不替代正常回歸。
- before-check 必須先提交程式，才以舊版 src/include 搭配新 fixture 跑。此腳本自身 exit 0 不表示修前通過，要看模式 exit 1 與四個 FAIL，並核對還原後 src/include dirty=0。

### 最終畫面與下一個卡點

原版視窗 1280×772，上方 52 px 為視窗／選單；引擎 framebuffer 為 1280×720。比較時只換算座標，沒有產生裁切素材或提交圖片。最終 t48／t50／t55 名目為 58／59／61.5 秒，四個固定色外框與原版 f0040 一致：

| 元件 | 遊戲區域外框 [x0,y0,x1,y1) | 尺寸 |
| --- | --- | --- |
| 對手左 | [413,227,525,560) | 112×333 |
| 對手右 | [573,227,685,560) | 112×333 |
| 顧客前 | [844,118,1028,525) | 184×407 |
| 顧客後 | [1044,118,1228,525) | 184×407 |

修正前同區域沒有對應大外框；最終卡片退出教學後可見，顧客進度後來由 1/4 變為 2/4。不能把此變化歸因於被阻擋的卡片點擊。兩份影格序列取樣起點與間距不同，未驗證相同動畫時序、FPS、隨機角色資料或整體逐像素一致。

**仍可見差異**：中央「春銷環節」字幕未退場、人材／顧客計數缺標籤與灰底、剩餘時間字樣及量表灰底缺漏、卡片部分附加文字／指示點缺漏。最終影格中的人物圖也與原版不同；隨機資料與立繪選擇各占多少，尚未定位，不能單憑不同圖片判定原因。完整春銷／結算與卡片互動仍未驗證。

**建議下一組**：先定位春銷字幕退場及點擊被擋是否相關，核對原版生命週期、命中元件及 AIN 呼叫鏈；待使用者授權再動手。UI 標籤／灰底、原訂訊息視窗 UI、一般讀檔 system.Reset 和長時間穩定性列為後續。
