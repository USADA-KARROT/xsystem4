# v14 浮點左上角座標與春銷顧客走位

2026-10-10，主線提交 `298d6ce`（獨立支線 `b5e357a`），已推送並以 ls-remote／GitHub branches API 雙源確認。六案修前全失敗；主線提交後 default／GBK 各90模式PASS、san0，獨立審查無High／Medium。四條GUI完整結束，root已確認第一位顧客走向左方店前並縮淡。

## 原因與腳本路徑

春銷粉紅顧客是 CustomerViewSet 的 Stand，與背景 MobView 路人不同。MoveOut 經 CParts.UpperLeftPos → AFL_Parts_GetUpperLeftPos → PartsEngine.Parts_GetPartsUpperLeftPos 讀取畫面角點。舊綁定是空 stub，腳本初始化為零的輸出沒有被填入；以錯誤起點計算出的終點偏向右方。

腳本先由配對邏輯決定店家，再呼叫 MoveOut；不是直接取游標所在女生的座標。GetRealPos 對三家店依序給192／500／628。MoveOut 用「local anchor + 目標畫面中心 − 目前畫面中心」得到 local X，做1秒橫移後再縮小、淡出。空 getter 遺漏父座標等變換，無法正確算目前中心。

宣告與具名直接呼叫點已全檔統計；不把介面動態派送當成只有這幾個使用者：

| API | 宣告形狀 | 直接 CALLHLL | AFL wrapper 直接 CALLFUNC |
| --- | --- | ---: | ---: |
| Parts_GetPartsUpperLeftPos | void(int, wrap<float>, wrap<float>, int) | 1 | 3 |
| Parts_GetPartsUpperLeftPosX | float(int, int) | 1 | 1 |
| Parts_GetPartsUpperLeftPosY | float(int, int) | 1 | 1 |

## 原版靜態證據

- dispatcher `0x57d05f`／`0x57d0b4`／`0x57d0ef` 分別連到組合 `0x58bf80`、X `0x58c020`、Y `0x58c0e0`。組合輸出由 `0x65a510` 取 float 參照地址，C ABI 應是 float*，四個宣告引數佔六個 VM slots。
- 三者共用 `0x534bb0`。origin半尺寸先在整數階段向零截斷，再轉浮點、經 `0x57b6a0` 變換。`0x534c9d`／`0x534cb3` 直接 movss 寫X再Y，沒有先取整。因此正尺寸17的半尺寸偏移是−8，但變換結果可以保有小數。
- 查找 `0x540250` → `0x53a0b0` 缺件不建立元件；組合 getter 不改 caller 輸出，scalar各回0.0。輸出地址相同時最後留下Y。
- 此處取得原始角點經矩陣的結果，不是旋轉後 AABB 的最小座標。

## 修正與相容界線

抽出保留float的共用角點計算；已知v14組合宣告綁 void(int,float*,float*,int)，scalar選擇器只接精確 float(int,int)。新增查找不建立缺少的 parts，先算入locals再寫X、Y。原有 int API仍用原本lroundf，v13計算分支不變。未知宣告保留原綁定，不新增VM_ERROR，不改submodule或存檔格式。

既有 parts-transform 的 v14 float HLL 斷言改成未取整矩陣值，因舊斷言與原版float回傳不符；component-position的int斷言不變，新UL2另守住正負整數rounding。

## 新探針

`parts-upper-left` 每案隔離執行，使用真 HLL／FFI：

1. 非零父座標、origin8、組合與scalar一致、輸出alias、stack/refcount。
2. 奇數尺寸、倍率產生小數、正負座標、既有int API。
3. 315度旋轉、雙翻轉，角點不能變成AABB minimum。
4. 缺件不改輸出／scalar0／不建立元件、不同state尺寸；非法state僅作安全守衛。
5. 真AFL wrapper加三店目的地公式，檢查移動後角點；沒有執行完整MoveOut或Motion。
6. scalar X的版本／回傳型別／arity宣告守衛；Y與組合unknown shape主要由靜態審查保證。

獨立支線修前0ded2b3六案全失敗，修後兩組各88模式PASS。主線正式before-check用整合前c3c2fe0的src/include：parts-upper-left為6/6失敗（rc1）、修正浮點期望後的parts-transform為2/11失敗（rc92），皆san0，還原後dirty0。主線提交後兩組各90模式PASS、san0，G15／G16／音訊模式都保留。見[修前後](before-after.txt)、[default](verify-default.txt)、[GBK](verify-gbk.txt)。

## 未驗證與限制

- 第一位顧客到第一店的慢速完整走位已看圖，另有快進春銷與戰鬥路線；但三店全部GUI、精確緩動曲線／速度、取消Motion的所有時點與完整逐幀原版等價仍未驗。UL5不等於完整MoveOut測試。
- 沿用既有2D surface-area／矩陣模型；原版camera／3D投影、`0x696ea0`的非預設全域比例、極端非有限變換未實作或驗證。不能宣稱所有投影模式完全等價。
- 原版把state交內容物處理，未追完所有 subtype 的非法state語義。新增state守衛是本機安全界線。
- 既有PostLink以GetMessageUniqueID作整批入口；不宣稱所有只宣告此API的其他AIN都能註冊。parts_try_get仍可能增加空hash entry，但不建立parts。
- v13／未知形狀沿用原綁定不等於已驗所有遊戲或所有未知ABI。

## 提交後實機與原版比較

| 路線 | 秒 | MSG | PNG |
| --- | ---: | ---: | ---: |
| slow | 104.290 | 88 | 450 |
| normal150 | 150.283 | 88 | 40 |
| haruuri | 182.301 | 629 | 120 |
| fightr | 350.213 | 681 | 162 |

全部duration／exit0／error0。slow對白與已審支線慢速路線相同，其餘三路與音訊組基準逐位元組相同。整合後production patch-id及兩份座標fixture與已審支線一致；harness清單衝突保留G15／G16／音訊／新座標四組，沒有復活過時G15七案文件。

慢速路線關閉教學後不再點ButtonNext，每0.1秒截圖。root親自開第一位顧客的移動序列及Wine原版／修後／修前三欄：修前往右靠近後排，修後往左到第一家店（約x192）再縮小淡出，與既有Wine w04_run_20同一階段一致。人物外觀／數值與實際時間不同，Wine序列每0.7秒一張；沒有新啟動Wine，也不宣稱完整逐幀或逐像素等價。

另看正常開場、Day2據點及戰後回地圖，沒有發現本次新增的畫面退化。這些結論只涵蓋所列路線，其他場景仍需後續核對。

## 本輪效能觀察

由STAGE2_PERF選取起點在30秒之後的完整約5秒區間，以總presents／總時間計算。正常／春銷／戰鬥平均分別58.528／58.450／58.207 FPS，修前音訊基準58.518／58.475／58.171 FPS，未見本次明顯效能下降。戰鬥最低單一約5秒區間48.902 FPS，仍有22個超過50ms的間隔；平均值不代表沒有卡頓，也不代表全遊戲效能已完成驗收。這些執行含自動操作與framebuffer截圖，沒有與原版做同條件效能基準。
