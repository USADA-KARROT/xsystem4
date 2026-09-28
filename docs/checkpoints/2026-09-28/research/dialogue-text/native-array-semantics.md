# Array.Free / Clear / EmplaceBack / At 原版語義

2026-09-28。只讀原版解殼傾印，以 x86 靜態反組譯確認；未執行原版 EXE，未修改遊戲資產或移植程式。本文件不含遊戲文字或完整 AIN dump。

## 已確證

### Free 與 Clear 保留陣列型別與物件

Array dispatcher `0x644300`，跳表 `0x644f18`。宣告 index 6 (Free) 與 index 7 (Clear) 共用分支 `0x64442e`：由 `0x65a620` 取得陣列物件，檢查非空後呼叫 `vtable+0x54`。dispatcher 沒有清除 caller 的陣列變數。

RTTI `CArrayPage@sys43vm` 對應完整物件 vtable `0x811c0c`，其 `IVMArray` 介面位於物件 `+0x20`，介面 vtable `0x811c28`。介面 `+0x54` 是 `0x67f5a0`：已配置時改以完整物件呼叫 vtable+4，即 `0x67ec50`。

`0x67ec50` 從最後一個元素倒序呼叫 `0x680270`，後者以元素位址、原元素 descriptor 和 heap manager 呼叫 `0x656c10`。迴圈結束後只把完整物件 `+0x14` 的資料長度設 0、`+0x44` 的 allocated flag 設 0。沒有清掉：

- 完整物件及其 owner slot。
- `+0x2c..+0x43` 的元素型別 descriptor；其中 `+0x30` 是型別碼、`+0x34` 是 struct 等附加型別號碼。
- `+0x48` 的元素實體寬度。

因此將 Free 實作成丟掉 typed page、再於 EmplaceBack 默認 int，與原版不符。等價移植應保留型別資訊；不要求 C 的配置布局照抄原版。

### EmplaceBack 保留型別並建立元素

宣告 index 11，分支 `0x6444b1` → `0x6476e0`。

1. `0x647730` 讀取現有元素型別 (`vtable+0`)，`0x64773d` 讀取邏輯元素數 (`vtable+0x0c`)。
2. `0x647750..0x647758` 呼叫 `vtable+0x50` 重新配置 count+1。此 vtable entry 為 `0x67f4d0`。
3. 清空後 allocated flag 為 0，`0x67f4d0` 轉 `vtable+0x4c` 即 `0x67f4a0`，接著 `0x67fe20`。後者用保留的 `+0x48` 寬度配置，並以原 descriptor 呼叫 `0x656970` 初始化元素。
4. `0x656970` 的 STRUCT (13) 分支為 `0x656a12`：使用 descriptor struct number 呼叫 `0x679b30`，把新 heap slot 寫入元素。REF_STRUCT (21) 的預設初始化分支是 `0x6569b8`，寫入 -1。不能把 value struct 與 ref struct 的初始化混為一談。

回傳 wrap 依元素型別分流，而非一律兩槽：

| 元素型別 | EmplaceBack 分支 | 回傳位置來源 |
|---|---|---|
| int / float / bool / enum (10/11/47/92) | `0x64777e` | array owner (`vtable+0x30`)、最後 index |
| string / struct 及其 ref (12/13/20/21) | `0x6477d3` | 元素 heap slot (`vtable+0x24`)、offset -1 |
| option (86) | `0x64778d` | array owner、index × descriptor 寬度 |
| ref primitive / interface 等兩槽形狀 | `0x6477b3` | 元素的兩個槽 (`vtable+0x24/+0x28`) |

`0x647888..0x647892` 呼叫 `0x658150` 建立 typed reference。`0x65818b..0x65818c` 對有效 owner/元素 heap slot 呼叫 `0x679f10`；`0x679f81` 明確增加被參照 page 的 refcount。這支持可寫 wrap 結果必須持有其參照，不能只複製 slot 而少掉 ownership。

### At 與 At#1 的查找語義相同

- index 65 (At)：`0x644cc0` → `0x6499e0`。
- index 66 (At#1)：`0x644ce7` → `0x649dd0` → `0x6499e0`。`0x649dd0` 是薄包裝，不另外改變查找或加入 option none 規則。

`0x6499e0` 先檢查陣列及 index 範圍，再依原元素型別建立 reference：

- int：`0x649a62`；bool：`0x649a9b`；float：`0x649ad4`。使用 array owner + index。
- string / ref string：`0x649b57` → `0x6583e0`。使用元素 heap slot。
- struct / ref struct：`0x649b6f` → `0x658430`。使用元素 heap slot。
- option：`0x649b0d`，使用 array owner + index × descriptor 寬度。

上述 reference 建構函式同樣會對所參照 page 增加 refcount。At#1 傳回失效元素，不能單憑 overload 名稱解釋；應檢查清空後的陣列型別與元素初始化是否已先出錯。

## 限制與待驗證

- 這份證據確定 Free/Clear 保留型別、倒序釋放元素，及 EmplaceBack/At 的上述分流；沒有證明移植端所有 ARRAY_PAGE、嵌套 array、option、interface、delegate 的解構與寬度處理均正確。
- `0x656c10` 對擁有 heap slot 的元素呼叫 `0x679f90` 釋放參照；完整每一種 variant 的 destructor 行為未逐一核對。修改既有通用解構器須另做相應測試。
- root 的現場 trace 顯示角色文字資料已進入上游函式，但 At#1 取不到有效訊息。這支持將 typed array lifecycle 列為修正候選；本文件本身不宣稱 GUI 已修復，必須由 before fixture、headless PASS 與 GUI 圖像完成因果驗證。
