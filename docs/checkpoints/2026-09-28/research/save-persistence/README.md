# 存讀檔持久化（2026-09-29）

程式修正為 `173ff1d`（`System: Persist SerializeStruct data in the native v9 format`）。`AFL_GameSave_StructSave`／`StructLoad` 與存檔註解已經能寫出、讀回原版格式的 `serialize_struct` 檔。成就、Collection、設定、共有存檔與一般存檔欄位共用這條路徑。libsys4 仍為 `8c93946`，沒有修改遊戲 AIN。

原版語義全部來自原版 EXE 解殼傾印的靜態反組譯，沒有執行 EXE。原版存檔只用唯讀方式複製成副本後解析。未驗證的部分集中在文末。

## 修正前的狀態

- `Array.SYSTEMONLY_GetStructPageList` 在 C 端是 void 的空函式，AIN 卻宣告回傳 `array<int>`。ffi 因此把回傳暫存器的殘值推上堆疊。
- `system.SerializeStruct` 多半拿到 NULL，回 true 卻沒寫檔。拿到殘值 page 時會走舊 v7 寫出路徑，遇到 v14 型別就 VM_ERROR 或崩潰（fixture R7：`savedata.c:125` ASan BUS）。
- `system.DeserializeStruct` 把讀回的 struct 放進用完即丟的清單 `values[0]`，呼叫端的物件完全沒變，新物件也洩漏。
- `Write/ReadSerializeStructComment` 是 stub：寫入恆回 true、讀取恆回 false，存檔清單的每一格都讀不到註解。

## 原版語義與位址

| 項目 | 位址 | 語義 |
|---|---|---|
| GetStructPageList | Array case 83 `0x644ecd` → `0x65a620` → `0x64a2d0` | 回傳新配置、ref 1 的 `array<int>`，內容是每個元素的 struct handle（取元素第 0 個 dword，`0x67ac30` 要求 CStructPage，`IVMStruct vt+0x14` 取 handle），不增加 struct 的參照。任一元素無效時整批丟棄，回空陣列（`0x64a3f1`）。元素數依頁面 stride（`0x67ee60`），dispatcher 不讀 arg3 |
| SerializeStruct | system case 13 `0x6938ea` → `0x694170` → `0x65f160` | 空清單回 false、不寫檔（`0x65f1a5`）。整個檔案先在記憶體組好再寫出；任何失敗靜默回 false |
| DeserializeStruct | case 14 `0x693946` → `0x694240` → `0x65c910` | 空清單不讀檔。先解析（`0x65cb20`），再以每個 handle 的區域複本就地載入（`0x65d390`）；套用中失敗不回滾。`0x673220` 是序列化物件的解構子 |
| struct 就地載入 | `0x65d9e0` | 依 AIN 成員 name2（`member+0x18`，讀寫兩端同一欄）與 `(type, 值型別)` 對應欄位；`<vtable>` 與空名欄位略過；不比對 struct 名稱。目標為空時依檔案內的 struct 名稱建立（`0x679d60` → `0x679b30(index, 1)`）。option 成員成功載入後，把下一個欄位的原始值寫入旗標槽（`0x65dbd4`） |
| 字串 | `0x65d480` | 就地改寫字串內容，handle 不變；`0x7fffffff` 代表空字串 |
| 陣列 | `0x65d5b0` | 依檔案維度重建；元素以目的陣列的元素型別載入，不看檔案的 elem_type。重建時元素先依宣告型別初始化（`0x67fe20(n, 1)` → `0x656970`：struct 以宣告型別建立並呼叫建構子，參照類為 -1） |
| 寫出 | `0x6605c0`／`0x660070`／`0x660190` | wrap／iface／ref 寫 -1，delegate 與 void 寫 0；option 值槽以內層值型別編碼，旗標槽寫原值；wrap 內層不在對照表時 type2 為 -1（`0x653660`）。排列順序（由原版實檔推得，未逐指令確認）：欄位依序處理，每個欄位的 keyval 在值編碼後立即附加，所以巢狀 record、字串與陣列排在前面；陣列在元素之後編號；struct 定義依 AIN 索引排序 |
| 註解 | `0x65f3b0`（寫）、`0x65c250`（讀） | 位於同一檔案的 `h14`，`h15` 為旗標。寫入需版本 > 7；讀取需版本 > 8，`h15 == 0` 或長度 0 時回 true 且不改輸出。SerializeStruct 會把 `h15` 歸零 |

檔案格式：`"GD" 01 01`、u32 原始長度、zlib level 1。內文依序為 key `serialize_struct`、16 個 int32 檔頭（v7 為 14 個）、group、records、globals（`type, type2, value, x, name`，共 n+1 筆）、strings、arrays（`rank, dims[rank], nflat, {n, elem_type, v[n]}`）、keyvals、struct 定義（`name, nfield, {type, type2, name2}`），最後是選用的註解。

「寫出順序」一列是實作時比對原版實檔才確認的：先前的設計假設 keyval 依 record 連續排列，但原版 AFConfig.asd 的 record 索引顯示是逐欄即時附加。改用這個順序後，四個原版檔的重存結果與原檔逐位元組相同（見下文）。

## 修法

| 檔案 | 內容 |
|---|---|
| `src/serialize_struct.c`、`include/serialize_struct.h`（新） | v9 寫出（`ss_serialize_file`）、v7..v9 解析、結構驗證、就地載入（`ss_deserialize_file`）、註解讀寫（`ss_write_comment_file`／`ss_read_comment_file`）、`ss_struct_slot`、`XSYS4_TRACE_SAVE` 追蹤。容器沿用 libsys4 的 `savefile_read`／`savefile_write`，讀取前自行檢查原始長度上限，避免損壞值讓 `xmalloc` 中止 |
| `src/hll/Array.c` | `Array_SYSTEMONLY_GetStructPageList_v14`；`array_select_function` 只在宣告為 `array<int> (ref array)` 而且 system 的兩個消費者都是 v14 形狀時才選用，否則保留原本的空函式 |
| `src/hll/system.c` | `system_SerializeStruct_v14`、`system_DeserializeStruct_v14`、`system_WriteSerializeStructComment_v14`、`system_ReadSerializeStructComment_v14`（`wrap<string>` 以 int slot 接收）、`system_select_function`、`system_struct_list_consumers_supported`。存檔名拒絕絕對路徑與 `..` 路徑段；沒有存檔資料夾時回 false |
| `src/hll/hll_shape_select.c` | system 庫交給 `system_select_function` |
| `src/vm.c`、`include/vm.h` | `vm_construct_struct`：比照 NEW 在 `orig_ctor == -1` 時的路徑建立 struct 並呼叫無參數建構子；NEW 本身不變 |
| `src/ffi.c` | `hll_call` 在呼叫前複製參數槽；呼叫結束時堆疊位置未變就從複本釋放 by-value 參數。會自行推回傳值的 `AIN_REF_HLL_PARAM` 函式維持原本行為 |
| `src/page.c` | `delete_page_vars` 不再把 int／float／bool／enum 的 option 值當成 heap slot 釋放。`6b65b12` 把同一判斷（`variable_option_is_value`）擴及 ASSIGN、X_OP_SET 與 `copy_page`，見下節 |

不支援的形狀一律保留原綁定。新程式碼沒有 `VM_ERROR`／`ERROR`／`assert`；失敗時印有限次數的警告，回 false 或空陣列。

## 設計與反駁的處理

設計文件與兩份反駁報告（語義、所有權）留在 repo 外。以下是逐項處置。

**採納並實作**

- F1（致命，ffi 重入）：DeserializeStruct 會在 HLL 呼叫中執行建構子，覆寫 ffi 事後要釋放的參數槽。改在 ffi 保存參數複本。fixture R16 以攔截函式在 HLL 內執行建構子重現：72a33e5 上 fileName／list／dest／新 struct 的 ref 為 1/1/1/0，並出現 `double free of slot 6`；修正後為 0/0/1/1。R5 在真實的 Collection 載入中也檢查同一件事。
- F2：巢狀 record、字串、陣列參照在套用前依檔案型別逐一檢查範圍，套用時再檢查一次。R9 有 0、nrec、-2、`0x7fffffff` 四種巢狀變體。
- F3：`delete_page_vars` 的 option 修正（R17）。
- F5：`l_struct` 改成單一出口。R9 的「套用期間遇到未知 struct 名稱」變體檢查失敗後 live slot 不變。
- F6：暫存檔寫入後檢查 fflush、fsync（Windows 為 `_commit`）與 fclose，再 rename；Windows 用 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`，失敗時保留暫存檔。
- F7：Array 選擇器連動 system 宣告（L1 會把 system 宣告改成別的形狀來檢查）。
- F8：陣列先換上新頁再釋放舊元素；struct 成員先設為 -1 再 unref。
- F9：深度上限 64、寫出 record 上限 2^20；讀入時 struct 載入次數不超過 `2*nrec+64`、陣列元素不超過 `2*元素總數+64`。
- F12：追蹤行統計「ref > 1 時仍就地改寫」的字串數（`shared_str`）。兩次 GUI 都是 0。
- F13：fixture 另外印出 `heap[i].ref != 0` 的數量。每次經 bytecode 包裝呼叫會多出一個帶 TEMP 旗標的 A_REF 暫存字串，這是既有問題，本組沒有處理。
- F14：文件改為「元素數依頁面 stride；xsystem4 需要 arg3 區分 X_A_INIT 的槽數與 struct 編號」。實作與 Numof／At／Erase 共用 `array_erase_stride`。
- F15：R12 只預期 struct 成員保持共享。
- F16：沒有舊頁時，新陣列依 X_A_INIT 的對應選型別（enum → `AIN_ARRAY_INT`）。
- F17：沒有存檔資料夾時回 false 並警告。
- 語義反駁 1：巢狀陣列 `79<79|80<…>>` 非空時，寫出與讀入都明確回 false 並警告；檔案中 rank ≥ 2 的陣列被走到時失敗。沒有實作 rank ≥ 2 的格式，因為原版維度與 flat 的排列細節尚未逐指令確認。
- 語義反駁 2：陣列中的 struct 元素先以宣告型別建立（有無參數建構子就呼叫），再就地載入，不再依檔案裡的 struct 名稱建立。
- 語義反駁 4：C1 的判據改為「除了 h15 從 0 變 1，`[0, h14)` 不變」。
- 語義反駁 5、6、7、8(c)：wrap 表外內層回 -1；records[0] 不檢查 idx；字串寫到第一個 NUL；巢狀 option 的槽數依 `0x653452` 計算。

**不同意或部分不同意（附查證理由）**

- F4「只有 1 槽、讀入時保留的元素陣列（如 `79<82<13>>`）整個保持不動」：部分不同意。舊元素沒有釋放確實是缺陷，已改成依宣告型別釋放。但語義反駁報告逐指令讀過的 `0x656970` 對 82 等參照型別把新元素設為 -1，原版會重建成 n 個 -1。若保持不動，陣列會繼續指向讀檔前的舊物件，與讀檔後的狀態不一致，所以照原版重建。CN 只有 `option<BattleContext>` 之下有這種陣列。
- F10「拒絕含 `/` 或 `\` 的名稱」：部分不同意。安全問題在於離開存檔資料夾，所以只拒絕絕對路徑與 `..` 路徑段；子資料夾仍在存檔資料夾內，照舊允許（資料夾不存在時寫檔失敗、回 false）。
- F3 附帶建議「R2（`79<92>` 的 enum 值被當 slot 釋放）同一批修」：不同意在本組修改。R2 的根源是 `init_struct_slot` 把 enum 陣列配成 `AIN_ARRAY`；改它會改變所有 enum 陣列的 EmplaceBack／At 回傳形狀，影響面超出存讀檔。讀檔沿用舊頁的 `a_type`，而 CN 由建構子建立的 enum 陣列經 X_A_INIT 已是 `AIN_ARRAY_INT`（fixture 實測 GameConfig 兩個 enum 陣列都是 14），所以讀檔不會放大 R2。fixture 在拆除前清空仍為 `AIN_ARRAY` 的 enum 陣列，並在註解標明原因。
- 語義反駁 2(c)(d)：原版重建陣列時先釋放舊元素（先跑解構子）、再建新元素；檔案值為 -1 的 struct 元素也會先建構再釋放。本實作先建新頁再釋放舊頁（F8，失敗時舊陣列保持不動），-1 元素直接設為 -1，不執行建構子與解構子。CN 存檔樹 97 種 struct 沒有解構子（反駁者 E3 實測），差別只剩 -1 元素的建構子副作用，而重現這個副作用沒有實益。
- 語義反駁 3（`option<wrap<…>>`）：反駁只要求記錄。本實作進一步偏離原版：檔案旗標為 some、而目的值槽沒有有效 handle 時，不寫旗標，保留目的端原狀並警告，避免 some(-1)。目的端已有 handle 時照原版寫旗標（R18）。
- 語義反駁 8(a)(b)：option 元素陣列非空時，讀入依原版回 false；寫出也回 false（原版會交錯寫出值與旗標，自己讀不回）。2 槽元素陣列讀入時保持不動（原版重建為 -1）。CN 只有 `m_achievements` 屬於這種，它的 name2 是空的，原版本來就不會載入。

**與原版刻意不同之處**：解析或結構驗證失敗時目的端完全不動；多一層防禦性的 wrap box 解包；寫檔用暫存檔加 rename；檔案 root 為 -1 或 globals 少於清單時回 false（原版會 unref 呼叫端的 struct）；上述 option 旗標保護；深度與工作量上限。

## 審查後修正（`6b65b12`）

`173ff1d` 之後的兩位審查者各找到可重現的缺陷，已全部修正。新增 `save-fixes` 模式（5 個案例），在修正前的 `ce2cd59` 上 5/5 失敗，修正後全過。

| 案例 | 缺陷 | 修正前（`ce2cd59`） | 修正 |
|---|---|---|---|
| F1 | 讀檔器先寫入 struct 定義數才檢查；數值大於剩餘位元組時，定義表是 NULL，清理時解參照 NULL。遊戲開機就會讀 Achievement／Collection／AFConfig，損壞檔會讓每次啟動都崩潰 | signal 6（UBSan：`serialize_struct.c:788` null member access） | 檢查通過後才寫入計數；清理迴圈另加 NULL 保護 |
| F2 | `option<int>` 等值型別的第一格是純數值，但只有 `delete_page_vars` 知道。讀檔原樣寫入後，遊戲的 `WorkerHistory@SetIncome`（`X_OP_SET`）覆寫時會 unref「編號等於舊整數」的無關 slot | 無關字串被提前釋放（ref 0） | ASSIGN、X_OP_SET、`delete_page_vars`、`copy_page` 共用 `variable_option_is_value`，值型別 payload 一律不 ref、不 unref、不深拷貝 |
| F3 | 同上：`X_OP_SET` 對值型別 payload 做 ref，刪除時卻不再 unref，每次指派都洩漏一個參照 | 無關字串 ref 停在 2 | 同上 |
| F4 | `copy_page` 把 `option<int>` 的整數當 slot，深拷貝那個編號的 page，複本的值被換成新 slot 編號 | 複本的值從 3 變成 6 | 同上 |
| F5 | 讀檔時新建沒有 STRT 建構子的 struct（例如 BattleContext），成員停在 null。原版 `0x679b30(index, 0)` → `0x656970(member, 0)` 會預設初始化 | `m_actions` 為 -1 | `vm_construct_struct` 對沒有建構子的 struct 做原版預設初始化：struct 成員遞迴建立（不呼叫其建構子）、字串為空字串、delegate 為新物件、option 為 none（旗標 1） |

F2 到 F4 的 ref 計數問題在修正前的 GUI 就已發生：CN 以 `X_OP_SET` 寫 `option<int>` 的地方包括 `WorkerHistory@SetIncome`、`WorkerCollection@SetLimit`、`SelectableIndexArray.m_selected` 等。`72a33e5` 以前 ref 與 unref 雖然對稱，但都作用在編號恰為該整數的無關 slot 上；`173ff1d` 只改了刪除端，才變成不對稱。

驗證：`6b65b12` 上完整 40 模式 `VERDICT PASS`，sanitizer 0，deleted-event 仍為預期 87。150 秒 GUI（新存檔）跑滿 150.376 秒，MSG 88、assertion 0、堆疊溢位 0，寫出 AFBGMMode／AFCGMode／Collection；峰值 RSS 1,770,766,336 bytes，比 `173ff1d` 的 1,908,981,760 低。已查看 framebuffer：場景、人物、對話框、頭像與正文正常。說話者名牌的中文名有缺字方框，推測與 String 的 GBK 字元規則有關，未驗證。

F5 只改讀檔用的 `vm_construct_struct`。一般 `NEW` 建立沒有建構子的 struct 時仍把成員留在 null，與原版不同；這是引擎層的既有差異，另列為待辦。GC 標記仍把值型別 option 的第一格當參照，只會讓無關物件晚一點回收，不會造成錯誤釋放，本組沒有改。

## Headless 驗證

| 模式 | 72a33e5 | 173ff1d |
|---|---|---|
| save-list（9 案例） | rc 1，9/9 失敗（例如 L2 回傳殘值 1801514144） | rc 0，全部通過 |
| save-roundtrip（18 案例） | rc 1，16/18 失敗；R7 在舊寫出器 `savedata.c:125` ASan BUS；R11 與 R15（未設定時略過）為對照 | rc 0，全部通過 |
| save-comment（6 案例） | rc 1，5/6 失敗；C3（檔案不存在）為對照 | rc 0，全部通過 |
| 完整 39 模式 | — | `VERDICT PASS`；deleted-event 仍為預期的 exit 87；sanitizer 0 |

案例重點：R1 逐欄位檢查 v9 格式（zlib `78 01`、檔頭 1000/9/0x40/1、records 位移 98、globals 兩筆、`<vtable>` 與 name2 原始位元組）；R2、R3 以真 `AFL_GameSave_Struct*` bytecode 往返並檢查就地載入、子物件 slot、陣列 metadata 與 live slot；R4 的 `m_achievements` 寫成 `(79,79,'')`、elem 89、值全 -1，讀回後不變；R5 讀入時建立 `IdArray<int, ReachedInfo>`，建構子在 HLL 呼叫中執行；R6 涵蓋 `(86,10)`、`(0,0,'')`、`(86,12)`、`(86,13)` 與 delegate；R9 共 22 種損壞或構造檔；R13 驗證寫檔失敗時舊檔不變、沒有殘留暫存檔。

[修正前逐案例](before-72a33e5.txt) · [修正後逐案例](after-173ff1d.txt) · [39 模式摘要](verify-summary.txt)

## 原版存檔相容（本機副本，未提交）

設定 `XS4_SAVE_ORIG_COPY` 指向原版存檔的複本資料夾後執行（[結果](original-compat.txt)）：

- R15：原版 AFConfig、Collection、Achievement、AFCommon、AFInfo 都讀入成功。把讀入後的物件重存，AFConfig（2263 bytes）、Collection（1444）、AFCommon（248）、AFInfo（465）的內文與原檔逐位元組相同。Achievement 不同，因為探針物件的 `m_achievements` 是空的（原版有 65 個介面元素，只寫 -1，讀入端依原版略過）。
- `save-localgame`（選用）：原版 SaveData1000 與 SaveData5000 讀入 `LocalGame`，建立 1109／1014 個 struct，回 true。重存內容小於原檔，已逐項歸因：xsystem4 不會把 `<vtable>` 陣列填成方法表（虛擬呼叫直接查 AIN `vmethods`），原版的 BattleSkill／Player `<vtable>` 則有 13107／286 個元素；`BattleContext` 的四個欄位在原版檔中 name2 為空，依原版語義不會載入。拆除後殘留 9 個 slot，逐一單獨建構再刪除確認，全部來自既有的建構子／解構子失衡（EnemyNamePostfixGenerator 8 個、FixedRandomValue 1 個），不是讀檔造成。
- 這個模式需要讀工作副本的 EX（`dohnadohnaEx.ex`），因為 LocalGame 的子物件建構子會查 EX。

## GUI 兩次執行

使用 `173ff1d` 的 optimized 建置、`RUN_TRACE_SAVE=1`，其餘條件與先前的正式測試相同（`--skip-title`、按住 Return、每 1.2 秒點擊）。

| | Run 1（新存檔資料夾） | Run 2（重用種子） |
|---|---|---|
| 秒數／停止原因 | 150.389／時限 | 150.225／時限 |
| MSG／assertion／堆疊溢位 | 88／0／0 | 88／0／0 |
| 峰值 RSS | 1,908,981,760 | 2,016,313,344 |
| 讀檔（追蹤行） | Achievement、Collection：fail(read)，檔案不存在 | AFConfig、Achievement、Collection：ok |
| 寫檔（追蹤行） | Collection.asd ok | AFConfig.asd ok、Collection.asd ok |
| DeserializeStruct 警告／`SAVE ... fail` | 0／0（不存在屬正常路徑） | 0／0 |
| 結束時的存檔 | AFBGMMode、AFCGMode（PassRegister，既有）、Collection | 同左，加上 AFConfig、Achievement |

兩次使用同一個執行檔（sha256 `a1e711d4…`）。在 commit 之前，以相同原始碼（版本字串不同）先各跑過一次，結果相同。

- Run 1（新存檔資料夾）：啟動時讀 Achievement.asd／Collection.asd 失敗（檔案不存在，依原版靜默回 false）；第一個 ADV 事件前寫出 Collection.asd（269 bytes）。以唯讀解析確認為 v9、`h15=0`、struct 定義依 AIN 索引排序、已讀事件 1 筆。150 秒內沒有觸發成就，所以沒有 Achievement.asd；AFCommon／AFConfig／AFInfo 與 SaveData 也沒有觸發，與 AIN 分析一致。
- 種子：Run 1 的存檔資料夾複本，加上 `save-seed` 在已讀事件清單依排序插入 `ZZ_PERSIST_PROBE`、另寫 `<ConfigVoiceMutedByNsfw>=1` 的 AFConfig.asd，並放入原版 Achievement.asd 的副本（驗證 GUI 讀原版引擎的檔案）。
- Run 2（重用種子）：AFConfig／Achievement／Collection 三個檔案都讀入成功，日誌沒有任何 DeserializeStruct 警告。可觀察的狀態差異：
  1. AFConfig.asd 在啟動後數秒內被重寫（與啟動前清單相比 mtime 晚 5.8 秒），`<ConfigVoiceMutedByNsfw>` 由 1 變 0。依 AIN 分析，這是讀入設定 → `RestoreVoiceOnStart` 看到旗標 → 清除 → `GameConfig@Save` 的路徑（沒有逐步追蹤 bytecode）；旗標只有從檔案讀回才會是 1，所以這一項同時證明了讀入、就地套用與寫出。
  2. Collection.asd 被遊戲重寫，內容與種子逐位元組相同：標記保留，而且 Run 1 已讀的事件被判定為已存在，沒有重複加入。對照：第一次準備種子時把標記附加在尾端，破壞了 `UniqueArray` 的排序（BinarySearch／LowerBound，strcmp），結果同一事件被重複加入。這反過來證明遊戲確實使用了讀回的清單；修正種子後不再重複。
  3. Achievement.asd 沒有變化（期間沒有成就事件）。
- 兩次都沒有回歸：MSG 88、assertion 0、堆疊溢位 0，沒有 `SAVE ... fail`。已查看兩次的 framebuffer（Run 1 `xsys4_t39.png`、Run 2 `xsys4_t37.png`），場景、立繪、對話框與角色正文都可見。Run 2 的 `xsys4_t18.png` 與上一組正式測試已視讀的畫格 sha256 完全相同。

PE_Save／PE_Load 的部件狀態型別（`2914b40` 的 `component_type_from_state`）：兩次執行都沒有 ResumeSave／ResumeLoad（沒有 `.vsf`、快照檔或相關日誌），PE_Load 沒有被觸發；Run 2 也沒有成就通知。因此「讀檔後成就通知 GetText 是否再次失敗」仍未驗證。StructLoad 路徑不經過 PE_Save／PE_Load。

[GUI 摘要](gui-summary.json)。截圖、日誌、存檔都留在 repo 外。

## 未驗證事項

- 原版引擎讀 xsystem4 寫出的檔案：只以格式推論（四個檔案重存後逐位元組相同），EXE 不可執行。
- 從讀檔畫面讀一般存檔：需要 `system.Reset`，目前仍是 stub，GUI 無法走到。一般存檔只有 headless 讀入驗證（`save-localgame`），沒有驗證讀入後的遊戲行為。
- 存檔清單畫面的註解顯示、手動存檔與自動存檔的 GUI 路徑（150 秒內不會觸發；自動存檔第 1 回合跳過）。
- 成就完成時的 Achievement.asd 寫出（150 秒內沒有觸發），以及讀檔後成就通知的 GetText（見上）。
- rank ≥ 2 陣列、option 元素陣列、2 槽元素陣列的原版語義細節；戰鬥中存檔再讀回（`option<wrap<…>>` 與 `79<82<13>>`）。
- `0x669cb0` 是否就是「呼叫建構子」（推論高）；有參數的建構子在讀檔時不呼叫，只建立 struct。
- 其他 v14 遊戲是否同樣寫 v9、同樣依 name2 對應。
- Windows 分支（`MoveFileExW`、`_commit`）沒有在 Windows 上編譯或執行。
- DeleteSaveFile 在檔案不存在時原版回 true（`0x5c69f0`），本組未修改。

## 重跑

```bash
export XS4_GAME=/path/to/game-workcopy XS4_MASTER_GAME=/path/to/original
H=docs/checkpoints/2026-09-28/harness
bash $H/verify-step.sh <tag>                                    # 39 模式
bash $H/before-check.sh 72a33e5 save-list save-roundtrip save-comment
RUN_TRACE_SAVE=1 bash $H/gui-run.sh save-r1 150                  # Run 1
cp -Rp "$XS4_WORK/runs/save-r1/saves" "$XS4_WORK/seeds/save-r2"
(cd "$XS4_WORK/probe" && XS4_SAVE_SEED_DIR="$XS4_WORK/seeds/save-r2" ./runtime-probe-asan "$XS4_GAME/dohnadohna.ain" save-seed)
RUN_SAVE_SEED="$XS4_WORK/seeds/save-r2" RUN_TRACE_SAVE=1 bash $H/gui-run.sh save-r2 150
```

原版存檔相容（選用）：`XS4_SAVE_ORIG_COPY=<原版存檔的複本資料夾>`，再執行 `runtime-probe-asan <ain> save-roundtrip` 或 `save-localgame`。複本資料夾不可是原始存檔資料夾本身，也不可提交。
