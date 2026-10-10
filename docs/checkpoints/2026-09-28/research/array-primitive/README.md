# v14 純量 Array.Alloc：清空資料並保護物件參照

2026-10-10。程式 `f6b6c0f` 已推送並以兩個遠端來源核對。只處理已知的單維純量頁；提交後預設／GBK各91模式PASS、san0。

## 問題與原版依據

舊的Array.Alloc保留前段資料，縮短時對捨棄的正整數呼heap_unref。座標／浮點位元若碰巧等於活著的heap slot，會錯扣無關物件的參照。新探針以真CASConstructionProcess@SetPosList先填3點再改1點，證明修正前的xsystem4把最後一個整數座標當成物件釋放；參數為合成，尚未證明一般GUI路線確實觸發這次縮短。

原版靜態反組譯：Alloc HLL 0x647470呼陣列虛表+0x4c（0x67f4a0），先+0x54清空（0x67ec50），再0x67fe20建立並初始化。初始化兩級跳表0x656aa4／0x656a80把int(10)、float(11)、bool(47)、enum(92)導向0x6569a9寫零；釋放跳表0x656c88／0x656c78導向0x656c50，完全不把資料當handle。未執行原版dump。

修正只接v14、operand1、ARRAY_PAGE、rank1、elem_slots不大於1，且容器為int／float／bool或其ref型別；清空後保留陣列metadata並全寫零。沒有VM回呼，舊頁僅free_page，不釋放純量內容。Realloc保留前段的語義不變。

## 呼叫盤點與界線

AIN只有一個兩引數Alloc宣告，直接呼叫64處：operand1 19處、2 32處、0x10002 3處、0x30002 9處、0x30003 1處。operand1的宣告形狀為int9、float6、bool2、enum1、array<int>1；巢狀陣列不可只憑operand1判成純量。Realloc有兩/三引數宣告，13個直接call全部呼兩引數版本：operand1 1處、2 10處、0x10002 1處、0x10003 1處；填值版本沒有直接call。不包含間接/動態派送的推算。

本組保留v13、未知operand、無頁、generic與多槽／多維形狀的舊行為。GameContext的enum陣列有generic表示的界線，未藉本組修正；負個數仍no-op（原版清空），string、nested、wrap與非option Realloc的其他釋放缺口亦未修。不能稱完整Array相容。

## 探針與既有斷言

新array-primitive七案：AP1–4涵蓋int／float／bool／ref容器的縮短、等長、擴大、零長度與metadata保留；以活slot編號當普通數值，確認物件參照不變。float使用subnormal位元；bool的非0/1型樣是合成防誤釋放壓力值，未稱遊戲會產生。AP5確認Realloc保留前段、增加處補零及負Alloc原行為；AP6保護v13、未知operand、generic、rank0/2、多槽與無頁；AP7執行真SetPosList。不是完整遊戲情境或所有陣列型別的測試。

兩處舊斷言依原版修正：array-construct AC7的int Alloc應清零，另保留Realloc仍保存5/7的檢查；option-array G1的41筆相容表只把int Alloc [9,0]改為[0,0]，其他40筆不變。首次完整回歸因後者失敗，查證後才更正，沒有跳過模式。

獨立靜態審查與複審無High／Medium；metadata保留的Low建議已補。審查者自行核對原版跳表與FFI回寫，不把測試通過當作語義證據。

## 驗證與畫面

七案修前5/7失敗、修後全過；刻意破壞清零、釋放與5項守衛的七種突變全部被偵測。正式before另確認AC7與option-array G1的更新斷言在舊程式各失敗一組，還原乾淨。提交前後各跑兩組完整verify與實機。

提交後normal150／haruuri／fightr／config均跑滿預定時長、exit0、無新錯誤；MSG分別88／629／681／88，與各自基準逐位元組相同。root親自看開場、Day2據點、戰後回地圖，以及設定第一頁的原版／本次／先前版本並排。設定頁的模糊背景、選項佈局維持，原有滑桿把手、核取方塊等控制項差異仍在；本組不修UI。Day2與戰後參考的隨機事件／隊伍／數值不同，不能當完全相同存檔狀態或逐像素等價。沒有新啟Wine、沒有新聽測。

這是記憶體安全與Alloc語義的修正，未宣稱一般GUI已實際觸發該誤釋放，或整體遊戲已完整相容。

## 本輪效能觀察

以STAGE2_PERF開始於30秒後的視窗按時長加權；包括自動操作與截圖成本，不能代表整個遊戲或原版效能。

| 路線 | 加權FPS | 最低5秒視窗FPS | 最大視窗p95間隔(ms) | 間隔逾50ms次數 |
|---|---:|---:|---:|---:|
| normal150 | 58.527 | 56.659 | 18.426 | 5 |
| haruuri | 58.471 | 55.106 | 24.987 | 10 |
| fightr | 58.174 | 49.088 | 26.894 | 22 |
