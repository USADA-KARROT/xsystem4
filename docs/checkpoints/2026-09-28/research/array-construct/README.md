# struct 陣列的建構、釋放與重入

2026-10-10。G15；審查修正提交 `3f65915`，主線以 `0f0af6c` 與 `3ab99a1` 整合，HEAD 為 `3ab99a1`。`git diff 3f65915 3ab99a1 -- src include docs/checkpoints/2026-09-28/harness` 為空，這三個目錄內容相同。主線乾淨提交後 default／GBK verify **各 87 模式 PASS、san=0**。提交後 11 條 GUI 路線已完整重錄並查看代表畫面；程式推送後經 ls-remote 與 GitHub branches API 雙源核對。

## 症狀與根因

`Array.Alloc`、`Array.Realloc` 建立值 struct 元素時只配置物件，沒有執行建構子，還多持有一次參照。春銷的 `MobViewCollection@0` 雖然配置了七個 `MobView`，卻沒有建立它們的 CG 元件、計時器與移動。這裡的路人是背景行走的人形；粉紅色顧客剪影的走位是另一個尚未修復的問題。

舊的 `Alloc` 還會保留舊元素；`Realloc` 縮短沒有釋放移除的元素。前者與原版「全部清空後重新建構」不同，後者加上多餘參照，使元素與成員無法正常釋放。每幀 `CallPartsUpdateEvent` 的暫存 struct 清單也受影響。

G15 初版補上建構與釋放後，審查又修正三項問題：建構子釋放並重用陣列 slot 時的回傳與回寫、解構子執行期間的舊長度與待刪元素可見性，以及未知 operand 的既有回傳形狀。這些分別有測例，不能只靠路人出現判定正確。

## 原版依據（靜態反組譯）

| 項目 | 位址 | 內容 |
| --- | --- | --- |
| `Array.Alloc` | HLL 跳表 `0x644f18[0]` → `0x647470` → vtable `+0x4c` 的 `0x67f4a0` | 先呼叫 `+0x54` 清空，成功才 `0x67fe20(n, 1)` 配置並建構 |
| 清空 | `0x67f5a0` → `0x67ec50`；迴圈 `0x67ec70`–`0x67ec7b` | 從最後一個元素往前呼叫 `0x680270`；全部完成後才在 `0x67ec7d` 清長度、`0x67ec84` 清配置旗標 |
| 重建 | `0x67fe20` → `0x41a430` → `0x656970` | 先配置完整長度，再依索引順序給元素預設值；配置的緩衝區不清零 |
| `Array.Realloc` | `0x67f4d0`；同長度 `0x67f50b`、增長 `0x67f50f`–`0x67f542` | 同長度不動；增長先擴容，再對新尾段逐一呼叫 `0x680170(i, 1)` |
| 縮短 | `0x67f554`–`0x67f570`；縮容 `0x67f572`–`0x67f57f` | 逆序釋放移除的元素後才縮容；合法解構子在這段期間仍能讀到舊長度及尚未輪到的元素 |
| struct 預設值 | `0x656970`、跳表 `0x656aa4`，型別 13 → `0x656a12` → `0x679b30(struct, 1)` | 有無參建構子就執行；沒有則給成員預設值 |
| 元素釋放 | `0x680270` → `0x656c10`；`0x656c58`–`0x656c63` | 每次檢查索引與目前長度；元素不等於 `-1` 才呼叫 `0x679f90`，沒有先把當前格改成 `-1` |
| `Array.EmplaceBack` | 跳表 `[11]` → `0x6476e0` → `0x67f4d0` | 以新增一個元素的 Realloc 路徑建構；建構子可觀察到增長後的長度 |

以上只讀取原版機器碼，沒有執行 dump EXE。原版在建構前取得元素位址，建構完成後才寫回（`0x656a20`）；建構子重入並搬動同一陣列緩衝區時，不能據此宣稱任意操作都有原版定義的安全結果。

## 修正

`src/hll/Array.c` 的共用 helper 只接手 AIN v14、operand 恰為 2、頁型別為值 struct 陣列、struct 編號有效、rank 不大於 1，且能辨認原陣列 heap slot 的情形。

- `Alloc(n)` 逆序釋放全部舊元素，再建立 n 個新元素；`Realloc(n)` 保留前段、只建構新增尾段，縮短時逆序釋放；同長度不動。
- 新頁的長度在建構前發布。每個元素以 `vm_construct_struct` 建立，陣列只持有一個參照；`EmplaceBack` 的有效回傳值另持有一個參照。
- 每次 VM 回呼前後都重取陣列頁，以 slot 配置序號辨認原 allocation。物件已建好但原陣列沒有位置時，釋放該物件；不拿失效頁指標繼續寫入。
- `src/ffi.c` 保存 `AIN_REF_ARRAY` 的配置序號，回寫前再核對，避免覆蓋同 slot 的替代配置。

### 審查後的三項修正

1. **EmplaceBack 回傳替代元素（Medium）**：初版 helper 雖然在 owner 被重用時停止建構，最後仍可能讓呼叫端從替代頁取元素。替代頁若是 primitive 陣列，FFI 還會把 struct 的一槽回傳改成兩槽，並錯誤增加參照。現在以獨立輸出保存本次建出的元素，所有回呼後再驗 owner、索引及元素配置序號。已知 operand 1／2 的回傳形狀在呼叫前固定；object 失效回一槽 `-1`，primitive 失效維持兩槽 `(-1, -1)`，不觸碰替代配置。
2. **析構前發布新頁（Medium）**：初版先換新頁再析構，令只讀 `Numof` 的合法解構子也看到新長度，其他尚未刪除的元素提前消失。現在保留舊頁與長度，逆序逐個移除當前格的持有後才 `heap_unref`；每次回來重取同一 allocation 的頁。所有釋放完成後才改長度。`Alloc(2)` 的解構子呼叫 `Free` 後，外層仍可建立要求的兩個新元素。
3. **未知 operand 的 NULL 頁回傳退化（Medium）**：提前固定 ABI 的第一版修訂使 operand 0／-1、存活 `VM_PAGE` 但頁為 NULL 的既有 fallback 只回一槽。現在未知 operand 在原 slot／配置序號仍有效時，沿用呼叫後 ARRAY_PAGE 的判斷；fallback 新建 int 陣列仍回兩槽 `(owner, 0)` 並持有 owner。替代 allocation 不參與判斷；已知 operand 1／2 仍固定形狀。

## 腳本影響面

`Alloc`／`Realloc` 共 77 個呼叫點：operand 1 的 int／float／bool 為 20 處；operand 2 為 42 處，其中值 struct 21 處、string 16 處、巢狀陣列 5 處；`0x10002`／`0x10003` 的 ref／wrap 為 5 處；`0x30002`／`0x30003` 的 option 為 10 處。這個 77 不包括另外 19 處 `EmplaceBack`。

`ref array<T>` 是接收者的傳遞方式，不等於元素是 ref；NameGenerator 的三個接收者仍屬本次的值 struct 陣列。

| 呼叫者 | 元素 | 新增建構的可觀察內容 |
| --- | --- | --- |
| `CASEffectKeyCancelChecker@Init` | `CASEffectKeyCancelKey` | key 為 `-1`、firstKeyDownCancel 為 false；隨後逐筆 Set |
| `CMenuView@_CreateItems` | `CASClick` | `-1`／`INT_MIN` 初值與 CASTimer；此 Alloc 在除錯分支 |
| `menu::detail::Init` | `選擇エリア_t` | 字串、兩個文字設定、游標 Z 偏移 1 |
| 系統按鈕設定 | `CSystemButton` | partsNumber 0、兩個空 delegate |
| `CMessageKeyControl@InitJoyClick` | `CASJoyClickAssignedKey` | 50 個元素各建 CASTimer，含未指派碼 `-1`、350／50 ms 按壓節奏初值；正常 Prepare 路徑可達 |
| `parts::detail::CallPartsUpdateEvent` | `SPartsUpdateData` | 每幀暫存清單的空 Event delegate；隨後 Copy 覆寫 |
| `sound::detail::Init` | `CASBGM` | 3 個物件的 volume 1、transitionType 3、transitionTotalTime 500、保留資訊 `-1` |
| `SaveAdvSoundData` 第一處 | `SADVBGMData` | 空名稱字串，之後逐欄填值 |
| `SaveAdvSoundData` 第二處 | `SBackSE` | 空 PlayList／FilterName，之後 Copy |
| `CBrushController@GetCircleCreatePosList` | `CASVector3D` | 有空預設建構子；編輯器呼叫點 |
| `CBrushController@EraseObject` | `SBrushObjectDistance` | 無建構子，int／float 預設值；編輯器呼叫點 |
| `_system::detail::Init` | `CASJoypadCallback` | 空 callback delegate |
| `ExTree@Init` | `ExTreeLine` | 兩個字串、含六個空陣列的 ExValueOverride、format 陣列 |
| `ExTree@AddLine` | `ExTreeLine` | 同上，只建新增尾段 |
| `ExTree@AddLine#1` | `ExTreeLine` | 同上，只建新增尾段 |
| `NameGenerator@LoadElement` | `NameElement` | 四個空字串；所有資料欄後續從 EX 回填 |
| `NameGenerator@LoadElementKana` | `NameElementKana` | 一個空字串；後續從 EX 回填 |
| `NameGenerator@LoadElement2` | `NameElementCustomer` | 四個空字串；後續從 EX 回填 |
| `TitleCharacterView@Attach` | `CharacterPositionCalculator` | maxSpeed 2、speed 0、speedDelta 0.1、減速表及兩個 CASPosF；呼叫者是外層 Attach，不是其內的 lambda |
| `MobViewCollection@0` | `MobView` | 七個路人的 CG 元件、計時器、亂數、Motion 及 begin-update 事件 |
| `SkillPanel@CalcSubOffset` | `SkillLineSubOffset` | 無建構子，兩個 int 仍為 0 |

這 21 處的建構子及可辨認的成員解構子，靜態核對沒有找到直接重入「正在 resize 的同一陣列」。這個陰性結果不取代 runtime 重入防護。

`MobView.Reset` 取三次 RAND、一次 RandF，Run 再取兩次 RAND；七個初始物件合計 35 次 RAND、7 次 RandF，後續每輪 Motion 又會 Reset／Run。每個路人還註冊 begin-update 事件，以 timer、Sin 與 SetPos 更新垂直位置。因此相同亂數種子不再保證修前後的顧客、交易與戰鬥順序相同；不能把任何一次戰鬥未打完都歸因於亂數。

路人沒有腳本解構子，collection 的 ReleaseComponent 也不主動移除事件。現有 v14 delegate 是弱 target，target 釋放後會清 entry，下一次更新再刪除 Empty delegate；這提供既有清理途徑，但反覆進出春銷後的事件、timer、Motion、CG 元件數是否回到基準，仍待實測。每幀暫存清單的參照修正有靜態及探針依據；本輪沒有重測 MB 或每幀效能，不承接舊報告的記憶體下降數字作為新驗證。

## 驗證

- `array-construct` 為 15 案（AC15 是 2026-10-11 `c40182c` 新增的；本節以下的修前紀錄與「14 案」是當時的）：基本建構與所有權、Realloc 前段／尾段及逆序釋放、無建構子預設值、同陣列重入、兩個真實腳本呼叫者、版本與形狀守衛、四種 owner 失效／替換、合法解構子的舊長度與待刪元素、primitive／struct／未知 operand 的回傳槽數、解構子內層修改的安全界線、解構子在尾端新增元素時的最終長度與遺失的物件。
- AC8–AC11、AC13 直接走 `hll_call`，在恢復 stack pointer 前量回傳槽數，避免包裝函式自行清 stack 掩蓋 ABI 錯誤。
- 最終 14 案 fixture 正式 before-check：主線修前 `f517ca8` 為 **12／14 失敗**，含 AC4／AC8–AC11 共 **5 份 ASan use-after-free 報告**；摘要 `san=10` 是關鍵字匹配數，不能寫成 10 個獨立錯誤。未審初版 `b63ce19` 為 **5／14 失敗**（AC4／8／9／12／14），san=0。兩次均已恢復來源且工作樹乾淨；本段恢復狀態由執行者確認。前手七案及早期 13 案結果保留為歷史證據，詳見 [before／after](before-after.txt)。
- 本輪 v2 focused AC1–AC14 全過；default／GBK 各 87 模式 `VERDICT PASS`，sanitizer 0。紀錄對應 `b63ce19` 加 src/include diff SHA256 `b6f1ff216cd607e9cd7333d3e3d41a09a4e287904c1834560a0befb5ae693421`。修正已提交為 `3f65915`，並已確認主線 `3ab99a1` 的 src/include/harness 相同；主線 `3ab99a1`（diff 空）的提交後 default／GBK 也各 87 模式 PASS、san=0，14 案全過。
- 既有 `arg-ownership` AO1 的已知殘留更新紀錄由 4 筆改為 1 筆，反映暫存元素多餘參照移除；AO4 每輪 1 筆的既有機制仍在。
- 前手曾對第一版進行突變測試；本輪三項審查修正沒有重做突變測試，不把舊紀錄當作目前差異的完整覆蓋證明。

執行紀錄以各自工作目錄的 `XS4_WORK` 為根：`logs/before/f517ca8-array-construct.txt`（主線工作目錄）、`logs/before/b63ce19-array-construct.txt`（G15 工作目錄，最新為 14 案）、`logs/codex-g15-review-fix-v2-20261010/array-construct.txt`、`logs/verify/codex-g15-review-v2-20261010/summary.txt`、`logs/verify/codex-g15-review-v2-20261010-gbk/summary.txt`（以上為提交前歷史）；最終主線驗證為 `logs/verify/codex-g15-final-20261010/summary.txt` 與 `logs/verify/codex-g15-final-20261010-gbk/summary.txt`。這些是本機驗證紀錄的相對名稱，不隨本文附上遊戲資料。

## 獨立審查

三項成立的 Medium 已處理；v2 獨立靜態複審沒有剩餘或新增 High／Medium blocker。審查範圍包括 Array、FFI、heap／page 的配置序號與所有權，以及 fixture；審查者沒有獨立重跑測試。此結論不代表完整 native 等價或所有實機路徑都已驗收。

## 提交前實機執行（歷史）

本輪提交前 v2 的三條限時執行已結束，種子為 20261008、binary SHA256 為 `e5db64ce1676dbdad69d664d4fcc49301a3ff3af0012584c2da883182b5baf2d`：

| 路線 | 時間 | MSG | PNG | 結束方式 |
| --- | ---: | ---: | ---: | --- |
| normal150 | 150.382 秒 | 88 | 40 | duration，exit 0 |
| haruuri | 182.314 秒 | 629 | 120 | duration，exit 0 |
| fightr | 350.217 秒 | 681 | 162 | duration，exit 0；主審者確認 t100／t150 已回地圖 |

三條已完成執行的 assert／VM error、stack overflow、past-end 搜尋均為 0；fightr 對白逐位元組等同 baseline，SHA256 為 `3525dc699b4d6f6f4dc4068faeb2a108f12539a63026fb0f20307b7da761e748`。對白雜湊、輸入設定與統計方法見 [GUI 摘要](gui-summary.json)。GUI 使用 optimized binary，不能把以上錯誤計數當作 GUI sanitizer 覆蓋。限時執行完成及對白雜湊吻合，不代表背景路人的亮度、速度、人數與原版等價，也不代表整個遊戲已完成驗收。fightr 回地圖有畫面確認，未量化戰鬥逐幀與原版一致性。

[default](verify-default.txt) 與 [GBK](verify-gbk.txt) 已換成主線 `3ab99a1`、diff 空的最終原始摘要，提交後各 87 模式 PASS、san=0、array-construct 14 案全過；最終提交、來源一致性與兩個基底的正式 14 案 before-check 已補齊。早期 b63ce19 的 13 案紀錄仍是歷史資料，不與新結果混用。提交後回歸見下一節；完整原版畫面等價仍未驗證。主線 baseline 與前手舊版結果不替代本輪修後驗證。

## 主線提交後實機與新基準

程式 `3ab99a1`，固定種子 20261008；11 條均 duration、exit 0、錯誤／堆疊溢位 0。

| 路線 | 秒 | MSG | PNG |
| --- | ---: | ---: | ---: |
| normal150 | 150.322 | 88 | 40 |
| haruuri | 182.296 | 629 | 120 |
| fightr | 350.343 | 681 | 162 |
| sysmenu | 30.297 | 88 | 10 |
| garage | 35.387 | 88 | 14 |
| config | 45.325 | 88 | 24 |
| backlog | 45.264 | 88 | 24 |
| target | 85.402 | 88 | 112 |
| savedialog | 148.329 | 629 | 16 |
| battle | 282.289 | 681 | 130 |
| noskip | 405.303 | 261 | 106 |

主審者看過開場／據點、選單、庫房、設定、回顧、顧客選擇、存檔確認、春銷至第2天、戰鬥教學及不快進對白；fightr 確認戰鬥結束回地圖。這些紀錄作為 G15 後新路線基準。存檔確認不代表實際存讀檔成功，不快進路線沒有涵蓋完整遊戲。詳細雜湊與各次二進位見 [GUI 摘要](gui-summary.json)。

## 背景路人的原版／修前／修後並排

主審者（root）已親自開啟並排圖核對：原版與修後在春銷卡片後方都有半透明背景路人，`f517ca8` 修前沒有該 MobView 路人，但原有粉紅顧客仍在。三張取相同流程狀態（顧客 1／4、人材 3／3、兩個等待顧客、無教學或結算），隨機角色與顧客不同，不能稱為同一次配對。

此觀察確認背景 MobView 出現在正確場景；沒有逐像素比較、速度／亮度／每影格人數量測，也未驗長時間退出後的 event／timer 清理。粉紅顧客走向與音訊仍由其他修正處理。本文件只記錄觀察，不附遊戲畫面。

## 與原版的差異與保留範圍

- **重入安全契約**：析構前只把當前格設 `-1`，避免遞迴 Free 重複釋放；其他待刪元素仍可見。原版沒有先清當前格，這項是安全措施。解構子替換元素或縮短到保留前綴以內時，保留內層變更並停止外層 resize。（2026-10-11，`c40182c`）停止之前會把外層已清空的格移除，不留 −1。解構子在尾端新增元素（PushBack／EmplaceBack，陣列變得比原來長）不算內層替換，這一點與原版相同、不是安全契約：外層只讀一次長度（`0x67f504`、`0x67ec60`），以舊索引釋放原本的元素，然後只改大小（Realloc `0x67f572`–`0x67f57f` → `0x41a430`，它沒有任何釋放元素的呼叫；Alloc 的清空在 `0x67ec7d` 把長度歸零後重建），陣列結束在要求的長度。新增的元素被截掉，既不解構也不釋放，物件就此遺失——連同它的成員與解構子本來會歸還的資源，是原版也有的洩漏；本移植照做，陣列的那一個參照留在物件上。因為沒有腳本回呼，被截掉的元素不可能再改這個陣列。探針 AC15（五輪：EmplaceBack、Alloc、解構子另外 Erase 的移植界線、PushBack、兩個解構子各新增兩個）。遊戲沒有這種呼叫點。owner 已釋放／被重用的 null 回傳，也是安全界線，不能宣稱 native 在所有同形情況都如此。
- **未建構格與失敗**：尚未建構的格是 `-1`，原版緩衝區未初始化；建構失敗保留舊有未建構物件 fallback，原版會失敗並報錯。負個數保留早退，原版清空。帶填充值的 Realloc 多載未做 native 核對。
- **版本與形狀**：v13 不走新的 struct helper 或 v14 EmplaceBack ABI 判斷；string、巢狀陣列、ref／wrap、option、泛型頁、無法辨認 owner 的呼叫保留既有路徑。未知 operand 保留上述有效 allocation 的 fallback，並非推斷成值 struct。FFI 的 `AIN_REF_ARRAY` 配置序號回寫防護本身不限定 v14。
- **SJIS**：本組不修改編碼偵測、字格或 SJIS 規則；default／GBK 回歸含既有 SJIS 守衛，但沒有另外以實際 v13／SJIS 遊戲跑完整 GUI。
- **其他元素種類的既有缺口**：string／巢狀陣列的預設物件、ref／wrap 的 null 預設、primitive Alloc 的保留／縮短行為及非 struct Realloc 的元素釋放，沒有擴進本組。保留 fallback 不等於已符合原版。
- **存檔**：本組不改格式，沿用第 9 版；舊存檔／resume 中未建構的陣列元素與多餘參照如何恢復，尚未驗證。
- **尚未驗證的呈現與成本**：手把清單新增的 50 個 timer 與按壓節奏、選擇肢游標 Z、標題角色速度、路人亮度／速度／人數的量化對照、重複春銷的完整清理及每幀建構成本。
- **聲音仍未解決**：CASBGM 建構子沒有直接呼叫音效引擎，但其音量與轉場初值會被後續播放／存檔讀取；沒有完成聽測，也不能把新增初值寫成已修好使用者回報的音樂問題。
- **粉紅顧客走位仍未解決**：`Parts_GetPartsUpperLeftPos` 的組合 getter 空殼與既有 X／Y getter 的浮點精度問題另行調查，本組沒有修改這些 getter，不列為 G15 修復成果。
