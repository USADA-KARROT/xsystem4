# 堆積修正 D 與 v11 版本界線

2026-10-14。程式 `0830b29`，基準 `3c37271`，libsys4 固定 `247f544`。提交前、後的預設與 GBK 各 101 個模式 PASS；前、後各二十條實機路線完成。本文區分探針、流程結果與尚未確認的畫面差異。

## 修改與理由

D 將 `function_call()` 對傳值 struct、delegate 與 array 的額外 `heap_ref()` 限於需要保留原行為的版本；`<11` 採上游的轉交路徑，callee 返回時釋放傳入的那一份。ref-struct、ref-array 等 `AIN_REF_TYPE` 與 wrap 仍保留引用。

前一組 C 把上游式指派、functype 與字串引數規則套到全部 `<14`，範圍過大。上游沒有 `X_REF`／`X_ASSIGN`，無法作為 v11–v13 的對照；本分支的 `X_REF` 直接推入來源槽而不 retain，若字串引數也不 retain，callee 返回便可能釋放 caller 唯一持有的字串槽。此次將 C 的舊路徑縮至 `<11`，v11–v13 回到 **`f23883c` 當時、C 之前**的相關行為。

| 路徑 | `<11` | v11–v13 | v14 |
| --- | --- | --- | --- |
| 值 struct／delegate／泛型 array 引數 | 轉交、不額外 retain | 保留 `f23883c` 的 retain | 維持 bytecode 轉交、C borrowed 引數 retain |
| 舊式傳值 array 引數 | 轉交、不額外 retain | retain | 原有 retain 不變 |
| ref 類／wrap 引數 | retain | retain | retain |
| 無 TEMP 的字串引數 | 轉交 | 恢復 retain | 原有 retain 不變 |
| 帶 TEMP 的字串引數 | 清標記並轉交 | 同左 | 同左 |
| `ASSIGN`／`F_ASSIGN` | 只存值 | 恢復先 retain 新值、再 release 舊值 | 原有計數不變 |
| functype 初值／fini | 函式編號 0、不當成槽釋放 | 恢復空頁槽與計數 | 原有處理不變 |
| `X_ASSIGN` | 不以此作舊格式驗證 | 恢復 functype 計數 | 維持已準備所有權的轉交 |

`X_ASSIGN` 的外層條件仍為 `<14`，只讓 functype 在 `<11` 略過計數；不能把整個條件改為 `<11`，否則其他 v11–v13 參考型別也會失去原有計數。v14 的引數／指派所有權決策保持原樣。

另補 `exit_unref()` 的 NULL 字串 payload 檢查：NULL 不呼叫 `free_string()`，槽仍清零並歸還釋放清單；此防禦適用所有版本。A 的 array 新元素初始化與 B 的 TEMP 遮罩計數修正保留。

## 證據各自支持什麼

- **上游 `e8bd5ab`，程式對照**：`function_call()` 只為 `AIN_REF_TYPE` 增加引用；`ASSIGN`／`F_ASSIGN` 只存值；普通 functype 不配置或釋放 heap slot。這支持較舊 VM 路徑的實作選擇，不能證明每一版編譯器的所有權序列。
- **前身 `f23883c`，程式對照**：支持「v11–v13 恢復修改前分支行為」，包括字串引數、functype init/fini、ASSIGN 及 X_ASSIGN；本輪沒有將此版本另外執行並宣告 PASS。
- **歷史 v5 量測**：既有紀錄觀察到特定路線中分支活槽增加、上游持平，並曾出現堆積損壞。這是調查動機，不是本輪重跑結果，也不能推廣成每次重新綁定必定多留一個引用的實測。
- **本次合成探針，動態證據**：直接檢查候選規則下的引用數、槽世代、字串內容、stack、call frame 與釋放結果。它驗證選定的相容契約，沒有證明原版執行檔的語義。

採用 v11 是依型別與指令格式分界作出的保守選擇。沒有 v11–v13 真遊戲可驗，也未逐版確認所有 v10 以前編譯器的輸出。舊版號配泛型 array／wrap 的合成案例只守現有分支分派，不宣稱該格式實際包含這些宣告。

註解與文件同步收斂三種過強說法：編譯器以 SP_INC／DELETE 管所有權、每次重綁必多留一份引用，均標示為程式推論；帶 TEMP 的槽是「在此未釋放，稍後 GC 可能回收」，不稱為永不釋放。

## 探針變更

新增 `heap-version-boundary`，六案各自 fork：

| 案例 | 檢查 |
| --- | --- |
| HB1 | 合成 `X_REF → CALLFUNC → RETURN`，v11／12／13／14 保留 caller 原字串的槽、世代與內容；callee 入口 2、返回 1，stack 哨兵與 frame 平衡。 |
| HB2 | 六版本的 functype 初值、兩種 fini、三元素 functype 陣列初始化與拆除。 |
| HB3 | 六版本的 ASSIGN／F_ASSIGN：string、struct、delegate、functype、舊式 int array；不同槽與自我指派、新舊引用及頁拆除。 |
| HB4 | 僅 v11–v14 的 X_ASSIGN。v11–v13 包含 functype 計數；v14 先準備新值引用並釋放舊值，再重用舊槽為無關見證槽，檢查指令不額外計數或誤釋放它。 |
| HB5 | 六版本的值參數、ref-struct／ref-array／wrap 守衛、TEMP 字串及 borrowed 兩態，檢查實際 CALLFUNC／RETURN 的入口、返回與 stack。 |
| HB6 | 一般與 TEMP 標記的 NULL 字串 payload：exit_unref 後槽清零，立即重用且世代改變。 |

`v13-heap` 保留八案，舊路徑改用 5／10，14 為守衛；移除舊版代表上的 X_ASSIGN。VA2 新增 `again == e[N-1]` 與世代改變的斷言。VC3 改用合成宣告並真的 RETURN，不再只看入口或借用遊戲的函式形狀。

`arg-ownership` 保留 G1 的 v13 **2→1**，新增合成 G2 的 v5／10 **1→0**，共六案。G1 只在建立框時使用 v13，執行既有函式前恢復載入版號，不能當作 v13 全程實測；HB5 另有全程維持 v13 的合成案例。新增模式已接入 harness，總數由 100 增至 101。

## 已取得的驗證

新 fixture 接在未改 production 的 `3c37271`：

- `heap-version-boundary` 六案全失敗，沒有環境錯誤。HB1 的 v11／12／13 入口引用實際為 1、期望 2；返回後 caller 字串同世代引用歸零、期望 1，直接重現 M1。HB6 的 NULL payload 觸發 UBSan，子行程以 signal 6 結束。
- `arg-ownership` 只有新增 G2 失敗：v5／10 實際 2→1、期望 1→0；原 G1 仍為 2→1 並通過。
- 更新後 `v13-heap` 八案通過。

候選修正的 `codex-hd-impl` 與 `codex-hd-impl-gbk` 紀錄均為 **101／101 PASS、san=0**；最佳化建置與 ASan probe 建置／連結成功。這包括 HB 六案、arg-ownership 六案與 v13-heap 八案。PASS 依各模式既定的預期 exit code 判定，並非全部模式都要求 exit 0。

獨立程式與 fixture 靜態審查均未發現確定的 High／Medium；結論限於這次修改與讀過的路徑，不能替代遊戲相容性驗證。

## 限制與待完成

HB5 的非字串多使用 NULL page 見證槽，直接涵蓋兩種舊式傳值 array、ref-struct 與 ref-array-int，沒有逐一動態驗完所有 ref／array 型別、深層內容、delegate 環境或析構重入。borrowed 案直接設旗標；既有 AO3 才經實際 `vm_call()` 驗 v14 struct。HB 使用正式指令 handler，未涵蓋整個 decode loop 或各版本編譯器產生的呼叫序列。

既有 `FT_ASSIGNS`、`vm_copy()`、GC 型別表、`exit_unref()` 遞迴頁成員的型別集合、DG_CALL 與其他 callback 路徑，沒有因本次修正一併解決。

## 提交後驗證與實機界限

程式提交 `0830b29` 後，正式 before-check 再次於 `3c37271` 重現上述 HB 六案與 G2 失敗，還原後原始碼乾淨。預設與 GBK 各 101 模式再度 PASS、san=0；提交前後共四組結果見 [verify.tsv](verify.tsv)，修前結果見 [before.tsv](before.tsv)。首次未改程式的 baseline 曾因 frame-pacing 計時超出容許範圍失敗，保留紀錄後未改程式重跑才通過，沒有放寬門檻。

提交前後各二十條路線均按預期退出或由時限結束，沒有新的堆疊溢位、已追蹤錯誤或殘留遊戲行程；normal150 的 MSG 均為 88。各路線的對白、click 比較與 binary hash 見 [routes.tsv](routes.tsv)。`noskip` 按牆鐘截止，只比較共同前段。這些結果不是完整遊戲或逐格等價的驗證。

提交前 `fightr` 完成戰鬥返回地城、MSG 681 與基準相同，但 171 次 click 中 56 次 target 不同，敵人排序也不同。額外讀取的歷史紀錄中，同一舊 binary、同種子與同排程重跑，按排程對齊仍有 51/170 target 差異及敵序差異；這證明固定種子不保證完整輸出重現，**不是 51 或 56 次差異的容許閾值，也不是目前基準已重跑的證明**。本次差異尚未歸因；獨立審查沒有找到足以歸因本 patch 的回歸證據，允許繼續原定流程。首次文件推送時額外控制尚未執行；之後使用者授權的乾淨基準／候選各一條已完成，結果與 binary 自動重建限制見[追加對照](extra-controls.md)。戰鬥狀態或 RNG 根因仍未確認。

原版並排沿用既有 Wine 擷取：Day1 系統選單、設定的信息速度分頁、Day2 地城第二節點。共同 UI 位置與元件可見；設定的滑桿把手仍缺少，中文字型與標頭字型仍不同。本輪沒有新啟動 Wine、沒有同步逐格錄影。戰鬥並排僅能比較共同版面，敵序與部分數值不同，不能作完全同狀態驗收。存讀檔／重置抽圖顯示讀取清單縮圖、返回標題與讀檔後繼續到 Day2；未延伸到未走過的場景。

此次未重跑戰國蘭斯長路線；沿用前一輪的 v5 調查動機與上游對照，不能宣稱那是此提交的新量測。v11–v13 沒有真遊戲可驗。Windows／Linux／Android、長時遊玩、音訊聽測與 APEG 影像都未驗。

獨立程式、fixture 與路線證據審查的界限見 [review.md](review.md)。本組沒有改亂數、繪圖或遊戲資產。


### 提交後路線觀察

提交後的 fightr 於時限結束、exit 0／錯誤 0，MSG 681 與基準相同，末段畫面確認戰鬥結束並返回地城；與基準 63/171、與提交前 66/171 個 target 不同（兩邊皆 171 筆，沒有缺筆造成的序列位移）。約190秒畫面中的敵序與既有基準相同、我方選中角色與提交前相同，教學遮罩狀態仍有差異。這些是實際觀察，不是固定種子等價證明；根因仍未確認。提交前後來自相同 production 內容但 binary hash 不同，沒有把它冒充同 binary 控制重跑。

提交後 noskip 按405秒時限結束，MSG 226，相對基準230行是逐位元組相同的前段；提交前為224行。差別只在此比較到的截止位置，未據此宣稱所有時間或音訊行為都相同。

提交後 mirror 的原始 click 紀錄為21筆、基準18筆。按既定操作時間與位置核對後，18個排程操作各自唯一對應且 target 全同；另有3筆不在排程的 `(left)` 放開事件。程式判斷它們沒有命中原按鈕，不送元件 click，但仍送全畫面訊息；來源未確認，不能稱完全無事件，也不能說由真人造成。兩邊VM啟動皆4次、51條SAVE診斷逐位元組相同、MSG88相同、沒有錯誤。末段同頁並排正常到春銷準備，未宣稱像素等價；這次不把原始21筆直接改寫成18筆以掩蓋差異。
