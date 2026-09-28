# Array.ShallowCopy / Where 的原版型別與生命週期

2026-09-28，僅靜態反組譯原版解殼傾印、唯讀檢查 AIN 宣告與呼叫。不執行原版、不建置、不修改移植程式或遊戲資料。

## 結論

原版 ShallowCopy 並非保持原元素型別後只複製 slot：它建立 `array<wrap<T>>`，使結果元素參照來源內容。對 value struct，結果元素是原 struct 的 heap slot，必須增加參照計數。Where 再收到這個 wrap 陣列時，也會建立持有參照的新結果。

因此目前移植 `Array_ShallowCopy` 把原頁型別及 raw slots 原封複製、沒有 `heap_ref`，確實與原版不符。刪除暫存複本可能提早釋放來源元素。是否因此造成角色文字消失，仍需現場 fixture 與 GUI 驗證。

## 宣告與呼叫統計

Array 跳表 `0x644f18`：

| 宣告 index | 宣告 | 原版分支 / 實作 | AIN 靜態呼叫數 |
|---|---|---|---:|
| 4 | array<?> ShallowCopy(ref array<hll_param> self) | `0x6443fc` → `0x647610` | 0 |
| 5 | 同宣告 ShallowCopy#1 | `0x644415` → `0x647660` → `0x647610` | 60，全部 arg3=2 |
| 58 | array<?> Where(ref array<hll_param> self, hll_func func) | `0x644c04` → `0x649260` | 160 |

Where arg3 分布：1=2、2=44、65538=87、65539=24、196610=3。這是 CALLHLL 靜態出現次數，不代表執行次數。

兩個 ShallowCopy 宣告在原版共用語義；#1 只是額外檢查來源物件後轉呼叫 #0，沒有另一套複製規則。

## ShallowCopy 位址證據

`0x647610` 經來源物件 vtable+0x30 取得 owner slot，呼叫 `0x658d40`。

`0x658d40`：

1. `0x658d82..0x658d88` 取得來源 CArrayPage。
2. `0x658d95` 呼叫 `0x67ff60`；結果非 0 時走失敗返回。本次沒有放寬這個原版限制。
3. `0x658da4..0x658daf` 將來源元素 descriptor 傳給 `0x652c90`。
4. `0x652cc7` 將結果 descriptor 的 type 設為 `0x52` (82，WRAP)，`0x652cd5..0x652d1d` 保留原附加型別號碼並把原 descriptor 放進巢狀型別資料。
5. `0x658dc2` 以此新 descriptor 呼叫 `0x679350` 建立新 array；`0x658e0e` 以來源邏輯長度配置新元素。

依來源元素型別的兩個主要分支：

| 原元素型別 | 分支 | 新 wrap 元素 |
|---|---|---|
| STRING12、STRUCT13、DELEGATE63、ARRAY79 | `0x658e33` | 來源元素 heap slot |
| INT10、FLOAT11、BOOL47、ENUM92、WRAP82、OPTION86 | `0x658e76` | 來源 array owner ＋ i × 原元素實體寬度 |

第一分支在 `0x658e4e` 讀來源元素 slot，`0x658e59` 呼叫目標 vtable+0x40。CArrayPage IVMArray vtable 是 `0x811c28`，此 entry 對應 `0x67f290`。其 `0x67f2e1` 呼叫 `0x679f10` 增加新 slot 的 refcount，再由 `0x67f2f6` 釋放原 slot，最後寫入新值。`0x679f81` 的 `inc [page+0x1c]` 是實際增計數位置。

第二分支也使用同一 +0x40 setter 保留來源 array owner，接著 +0x44 寫入 offset，不能用單槽 int 的普通複製取代。

wrap<STRUCT> 的有效參照型別也可核對：`0x67ee20` 發現 stored descriptor 為 WRAP82 時轉 `0x653660`；其 STRUCT13 分支 `0x6536bb` 回 REF_STRUCT21。這是保留原 struct 物件身分的參照，不是複製 struct 內容。

## Where 位址證據

`0x649260` 透過 callback 判斷每個邏輯 index，`0x649312..0x649319` 收集通過者 index。然後 `0x649339` 呼叫 `0x658c80`：

- `0x658cbb..0x658cc5` 使用來源原 descriptor 建立結果 array，沒有把 wrap 退回 value struct。
- `0x658d02` → `0x6800a0`；`0x6800ab..0x6800c8` 檢查型別碼、附加型別號碼與巢狀型別相等。
- `0x6800e2` 配置通過元素數，`0x6800fd` 對每個選中 index 呼叫 `0x6801d0`。
- `0x6801d0` 最後呼叫通用 typed element copy `0x656d60`。

`0x656d60` 對 WRAP82、REF_STRUCT21、REF_STRING20、REF_ARRAY80、IFACE89 等走 `0x656f7c`：取得 descriptor 實體寬度，複製所有槽，`0x656fa7` 增加第一槽 owner 的 refcount，`0x656fbb` 釋放舊 owner。相應解構分支 `0x656c58` 會對第一槽呼叫 `0x679f90`。

需區分：Where 直接處理 value STRUCT13 時走 `0x656eb6` 的 struct 內容複製；本次角色路徑先 ShallowCopy，Where 看到的是 wrap<STRUCT>，應走共享參照分支。不能用原版 value struct 的深複製規則解釋該角色路徑，也不能把所有 Where 結果一律改成共享引用。

## 建議驗證與限制

- 最小因果 fixture：含字串 field 的 value struct 陣列 → ShallowCopy → Where → 刪除第一個暫存 copy → 原陣列與 Where 結果仍能讀原元素 → 刪除 Where 結果後原陣列仍有效。另檢查修改 wrap 所指 struct field 是否反映至來源。
- 可再用 Where 挑出某一個來源元素，來源 EraseAll 移除該元素後，保留的 Where 結果是否仍可讀；這能覆蓋 root 描述的後續生命週期。
- primitive 的 ShallowCopy 是 owner+offset 參照；測試不能只檢查複製後值相同，應檢查 source mutation 與副本參照、刪除任一方後另一方的可讀性。
- 本文未逐一證明所有巢狀 array、delegate、option、interface 等形狀在移植 VM 的 storage/ABI 表示。未知形狀應保留既有綁定，避免把目前角色路徑的 struct 修法擴大套用。
- 新 clone 的現有 ShallowCopy／Where 程式碼只是用來核對差異；沒有修改、build 或執行它，GUI 修復結果未驗證。
