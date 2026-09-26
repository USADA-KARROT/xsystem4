> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# Personality 空字串斷言：有界唯讀分析

2026-09-26。未改 production、未啟動 GUI。讀取既有 CN dump，另以目前 libsys4 parser 直接讀原 AIN，產出 `personality-metadata.tsv`；`personality_metadata.c` 為可重跑唯讀 probe。這次只執行 metadata probe，尚未執行 VM fixture。

## 已定位的斷言

- f28149 `void Personality@0(string id)`，address **0x5c33f2**；struct706 `Personality` 只有 field0 `string Id`。
- generated init f28150 `Personality@2`，address **0x9ed0fa**，只 DELETE 舊 Id、S_PUSH 空字串、X_ASSIGN/POP。
- ctor 先呼叫 f28150，再將 local0 id 經 A_REF 複製到 Id；接著再次 A_REF local0，與空字串 S_NOTE（**0x5c3444**），最後 ASSERT（**0x5c3458**）。斷言測的是參數 id；不是 EX lookup 的結果驗證，也不是直接測 struct.Id。
- ctor 本身完全不呼叫 EX/DataTable/HLL。`Personality@GetValueFromTable` f28151 才是後续屬性查表。

## 原 AIN 的四個直接 NEW 呼叫點

| caller | address / NEW site | id 的直接來源 |
|---|---|---|
| f27605 `CustomerEffect@LoadFromEx` | 0x5ae8fa / **0x5ae9b8** | `EX_SA2String` f6100：效果資料表、目前 effect id、欄名 `personality`、default 空字串；只在效果 type=4 分支建構 |
| f28947 `CustomerCreator@Create` | 0x5e60e0 / **0x5e67a4** | params 的 FetishTypes getter f28951 → `CreatorHelper::Lottery` → local5 array<string> → foreach 的 `[array,index]` → X_REF1/A_REF |
| f28980 `WorkerCreator@Create` | 0x5e8530 / **0x5e8b88** | params field4 FixedPersonalities 的 A_REF → Concat(CreatePersonalities f28986) → Take<string>(3) → foreach X_REF1/A_REF |
| f32260 `PersonalityDetailView::CreateComponent` | 0x6b8fec / **0x6b9008** | **0x6b9002 的 S_PUSH 非空 AIN 常數，直接 NEW；完全沒有 EX lookup** |

Worker 的 f28986 可由 params field22 PersonalityCreateMethod 名稱建立 delegate，或 fallback f28987 InnerCreatePersonalities。故不能把 Worker 分支一律歸因單一 EX 呼叫。Customer/Worker 的 foreach 二槽是元素位置 `[array,index]`，應記錄解參考前後的元素 slot、seq 與 heap.type，不能只看顯示字串。

目前舊 run 沒有 ASSERT caller backtrace，不能認定實際是四者哪一個。尤其若 caller=f32260，資料表缺 id 可先排除，應看常數、參數與字串生命週期。

## EX 路徑的精確邊界

`Personality::GetIds` f28153@0x5c3592：EX_Height f6086 得 row count，再逐 row 呼叫 EX_RA2String f6103，以欄名 `id`、default 空字串取值，Array.PushBack<string>，最後 A_REF 返回。

其表名 AIN bytes 為 `ccd8e1e794b593fe`，完整比對應用 bytes，勿拿舊 SJIS 顯示的亂碼當 lookup key。這是 GetIds 的資料來源；不表示所有 constructor 呼叫必經 GetIds。

- f6100 EX_SA2String@0x22603a：MainEXFile.GetColAtFormatName、GetRowAtStringKey → f6094 EX_A2String。
- f6103 EX_RA2String@0x226298：GetColAtFormatName + row 參數 → f6094。
- f6094 EX_A2String@0x225ae4：MainEXFile.A2String；row/col 參數由腳本重排。
- production `src/hll/MainEXFile.c` 支援 table / list-item-table；lookup 找不到、row/col 越界或 cell 非 EX_STRING，會返回 caller default 的 string_ref。故空字串可能是合法 fallback，必須同時記錄 table bytes、row、col、cell type、default/returned bytes 才能認定原因。
- 舊 run line6 顯示 EX 載入 126 blocks，只證明檔案開啟成功，不能證明每個特定表／欄 lookup 成功。此有界鏈未見獨立名為 DataTable 的 native 層；主要原生邊界是 MainEXFile。

## 舊 run 能證明什麼

`work/gui-stage3-20260924/runs/retry-20260926/engine.log`：首個 Double free warning 在 line28，遠早於 Personality ASSERT line10728；其後才出現 heap freelist corrupt 訊息。日誌不能單憑先後認定哪個 double free 導致空 id，也不能將 Personality 當第一個根因。root 正在捕捉首筆錯誤；應優先修首個可重現壽命錯誤，再看這個斷言是否仍存在。

## 最小可執行重現與取證建議

附 `personality_fixture.inc`，可整合既有 real-VM headless harness：在正常 init_probe 後執行 `test_personality_literal()`。它直接執行原 AIN 的 S_PUSH@0x6b9002、NEW@0x6b9008，並讓完整 f28149/f28150 bytecode 跑完；不呼叫 f32260 後半的 UI，不初始化 EX、不 mock VM callback。20 輪檢查 Id bytes、stack/callstack、heap baseline 與常數 string refcount。**此檔是未執行的建議 fixture，不能列為 PASS。**

literal fixture 通過也只排除孤立建構式的必現錯誤，不能排除 GUI 在更早路徑已破壞字串。若需第二層，才在 headless 單獨初始化 MainEXFile module、執行 f28153，逐項核對非空 id；勿為此執行完整 GUI/init_libraries。

GUI 精確取證點：f28149 entry、A_REF@0x5c3424、X_ASSIGN@0x5c3426、A_REF@0x5c343c、S_NOTE@0x5c3444。每點記錄 caller fno/return IP、local0 slot/seq/type、string ptr/ref/size、Id slot 與比較結果。這能區分「傳入已空」「傳遞／賦值後毀損」「比较層錯誤」。若空值由 EX 返回，再啟用以上限定 EX trace；不要先擴大資料表或 timer 研究。
