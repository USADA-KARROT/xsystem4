# 舊版引擎（v14 之前）等價性稽核報告

- 日期：2026-10-12
- 對象：分支 `wip/post-checkpoint-2026-07-06` 的 `81e2a7d`；比對基準為分出來時的上游 `e8bd5ab`
- 方式：唯讀。只用 `git diff`／`git show`／`git log -S`／讀檔。沒有建置、沒有執行、沒有拿任何 v14 之前的 `.ain` 驗證。
- 行號是 HEAD `81e2a7d` 的行號，由 diff 的 hunk 推算，標「約」的可能差幾行。

## 先講結論

`SYS_EXIT`、`SYS_PEEK` 不是孤例。把 v14 核心移進來的 `14c2618` 在共用路徑上留了很多沒有版本條件的改動，另外幾個後續 commit 也有。依我讀到的程式碼，**v14 之前的遊戲目前很可能連文字都顯示不對（第 1 條），而且參考計數、委派、解構子的模型都跟上游不同（第 2 到 9 條）**。這些都是從程式碼推導出來的，沒有實機佐證；每一條的信心另外標示。

依預定的順序做完的範圍：`src/vm.c`（既有指令與呼叫路徑全部看過，純新增的大段只看頭尾）、`src/heap.c`、`src/page.c`、`src/ffi.c` 的 `hll_call`、`src/input.c`、`src/system4.c`、`src/video.c`，`src/hll/` 做了清單並挑了幾個看。沒看的範圍列在最後，其中 `src/parts/` 最大。

---

## 一、會改變 v14 之前遊戲行為的改動

### 1. 日文 SJIS 遊戲會被標成 GB18030，文字改用 GB18030 解碼

- 位置：偵測在 `src/system4.c` 約 677–680（`sc.legacy > 5` 就設 `ain_is_gb18030 = true`）；計分在 `src/hacks.c` 的 `gb18030_detect_strings()`。使用處：`src/text.c` 263（`char_to_code`）、300、313、342；`src/hll/StoatSpriteEngine.c` 99、112；`src/hll/CharSpriteManager.c` 156、169；`src/parts/text.c` 32、45；`src/parts/parts.c` 1329；`src/asset_manager.c` 172；`src/hll/MainEXFile.c` 134。
- 引入：偵測 `6e73d5e`；`text.c` 的使用 `9a6e664`。
- 上游：沒有這個旗標，文字一律用 SJIS 解碼。
- 現在：`legacy` 的算法是對前 100 個 AIN 字串逐**位元組**找「`0xA1`–`0xDF` 之後接 `>= 0x40`」。SJIS 平假名的第二個位元組（「い」`82 A2` 到「め」`82 DF`）幾乎都落在這個範圍，後面再接一個字就命中。只要 100 個字串裡有 6 個這樣的，旗標就打開。嚴格判定（`strict`）不會通過，所以字串規則仍是 SJIS、`game_to_utf8()` 也還走 SJIS，但上面列的使用處**只看 `ain_is_gb18030`，不看嚴格判定**。旗標沒有任何地方設回 `false`。
- 對舊遊戲的後果：用 Unicode 字型時，`char_to_code()` 把 SJIS 位元組當 GB18030 解，畫出來是別的字（亂碼）；字元步進改用 GB18030 規則，半形片假名（`A1`–`DF`）被當成雙位元組的前導；PartsEngine 的數字改寫成 GBK 全形。
- 多娜多娜：會走到，這段就是為它寫的（它的嚴格判定會通過）。
- 建議：嚴格判定沒通過就不要設 `ain_is_gb18030`；或把上列使用處改成看 `game_charset_is_gbk()`。
- 信心：high（程式碼與 SJIS 編碼表）。「實際的舊遊戲前 100 個字串會不會命中 6 個」沒有實測，但含日文的字串表幾乎一定會。

### 2. `ASSIGN`／`F_ASSIGN` 依變數型別做參考計數

- 位置：`src/vm.c` 約 2891–2925。引入：`14c2618`。
- 上游：`stack_pop_var()[0] = val; stack_push(val);`，不碰參考計數。
- 現在：變數型別是字串、結構、委派、`functype`、陣列、任何 `ref` 型別時，對新值 `heap_ref`、對舊值 `heap_unref`。沒有版本條件。
- 對舊遊戲的後果：
  - `functype` 變數在 v14 之前存的是**函式編號**。現在會把函式編號當成堆積槽號去加減參考計數，等於隨機一個物件被多加或多減（多減會提早釋放）。
  - `ref` 物件變數重新綁定時，舊版位元碼自己用 `DELETE`／`SP_INC` 管計數，再用 `ASSIGN` 寫入；現在等於每次多加一次、多減一次。變數最後持有的物件會多一個參考（不釋放、解構子不跑）。**這一點是依我對 System4 位元碼慣例的認識推論，沒有拿舊遊戲的反組譯確認。**
- 多娜多娜：會走到，v14 依賴這段。
- 建議：整段包在 `ain->version >= 14`，之前的版本恢復上游兩行。
- 信心：`functype` 部分 high；`ref` 重新綁定部分 medium。

### 3. `AIN_FUNC_TYPE` 被當成堆積參考

- 位置：`src/page.c` `variable_initval()` 約 124–130、`variable_fini()` 約 158。引入：`14c2618`。
- 上游：`functype` 落到 `default`，初值 0；`variable_fini` 不處理它。
- 現在：初值是新配置的一個堆積槽（頁面為 NULL）；`variable_fini` 會 `heap_unref(值)`。沒有版本條件。
- 對舊遊戲的後果：沒賦值的 `functype` 變數不再是 0（腳本判斷「是不是空的」會錯，`CALLFUNC2` 會去呼叫那個編號的函式）；變數離開作用域或結構被刪時，拿函式編號去 `heap_unref`，隨機物件被多減一次。每個 `functype` 區域變數、成員、陣列元素還各洩漏一個槽。
- 多娜多娜：推定會（這個 case 是為 v14 加的），沒有驗證。
- 建議：兩處都加 `ain->version >= 14`。
- 信心：high。

### 4. 函式呼叫對「傳值」的結構、委派、陣列參數多加一次參考

- 位置：`src/vm.c` `function_call()` 約 1549–1560。引入：`14c2618`；`dfa141b` 之後只有 v14 不加。
- 上游：只有 `AIN_REF_TYPE` 參數 `heap_ref`。傳值的結構／陣列是呼叫端用 `SR_REF`／`A_REF` 複製出來的，所有權交給被呼叫的函式，返回時釋放（複本的解構子會跑）。
- 現在（v14 之前）：`AIN_STRUCT`、`AIN_DELEGATE`、`AIN_ARRAY_TYPE` 參數再 `heap_ref` 一次。`SR_REF`、`A_REF` 在 v14 之前仍然複製。
- 對舊遊戲的後果：每次傳值呼叫洩漏一份複本，堆積只增不減；複本的解構子不再執行。
- 多娜多娜：走同一個函式，但結構／委派走「已交付」的分支，不加。
- 建議：v14 之前恢復上游的 switch（只處理 `AIN_REF_TYPE`）。
- 信心：high。

### 5. 委派呼叫不再複製參數，結束時也不釋放

- 位置：`src/vm.c` `delegate_call()` 約 1916–1950，`delegate_copy_argument()` 約 1796–1818。引入：`14c2618` 起改成「借用」；現行形式 `d3d08e8`。
- 上游：每呼叫一個項目，用 `vm_copy()` 給被呼叫者一份（字串／結構深複製，`ref` 型別 `heap_ref`）；全部呼叫完再對堆疊上的原值 `variable_fini`。
- 現在（v14 之前）：`delegate_copy_argument()` 在 `ain->version < 14` 時原值照搬；結束時只 `stack_pop`。但被呼叫者的區域頁返回時照樣對每個參數 `variable_fini`。
- 對舊遊戲的後果：
  - `ref` 型別參數：每呼叫一個項目，被參考的物件少一個參考。只有一個項目的委派也會發生。物件會提早釋放。
  - 字串／結構參數：第一個項目返回時就被釋放；第二個項目拿到已釋放（或已被別人重用）的槽。
  - 沒有項目的委派：字串／結構參數洩漏。
- 多娜多娜：走同一個函式，走 v14 的分支。
- 建議：v14 之前恢復上游的 `vm_copy` 與結束時的 `variable_fini`。
- 信心：high（程式碼推導）。

### 6. `DG_STR_TO_METHOD` 改成 v11 之後的堆疊形式

- 位置：`src/vm.c` 約 5125–5160；`function_matches_delegate()` 被刪。引入：`14c2618`。
- 上游：從堆疊取委派型別編號，再取字串；找不到或簽名不符回 0。
- 現在：只取一個值當字串槽。v11 之前委派型別編號在堆疊上（`initialize_instructions()` 對 v11 之前設 `nr_args = 0`），所以取到的是型別編號。
- 對舊遊戲的後果：把型別編號當字串槽讀，還對它 `heap_unref`（隨機槽被多減）；真正的字串留在堆疊上（堆疊多一格、字串洩漏）；找不到時回 -1 而不是 0；不再檢查簽名。
- 多娜多娜：會走到。
- 建議：`ain->version < 11` 走上游的寫法，把 `function_matches_delegate()` 加回來。
- 信心：high。哪些 v14 之前的遊戲用到這個指令沒有查。

### 7. 函式返回時強制堆疊平衡

- 位置：`src/vm.c` `function_return()` 約 2205–2325，`ain_return_slots_type()` 約 1302。引入：`14c2618`。
- 上游：不檢查堆疊。
- 現在：返回時把堆疊修成「`base_sp` 加上回傳值格數」。v14 之前格數固定算 1（非 void）或 0。
- 對舊遊戲的後果：**疑問**——v14 之前回傳 `ref int`／`ref float`／`ref bool`／`ref lint` 的函式，回傳值在堆疊上是兩格（頁面、索引）。若是如此，這裡會截成一格，呼叫端的堆疊就錯了。我沒有用舊遊戲的位元碼確認這類函式回傳確實是兩格。
- 多娜多娜：會走到。
- 建議：v14 之前跳過整段平衡修正（上游就是不修）。
- 信心：medium，寫成疑問。

### 8. 釋放改成佇列；解構子裡的釋放在 v14 之前被延後

- 位置：`src/heap.c` `heap_unref()` 約 636–730；`src/page.c` `delete_struct()` 699（`ain->version >= 14 ? heap_set_defer_frees(false) : true`）。引入：`14c2618`；v14 才改回巢狀的那行是 `6582e38`。
- 上游：參考歸零就立刻刪，遞迴進行；解構子裡釋放的東西當下就解構。
- 現在（v14 之前）：歸零的槽先進佇列；在一次刪除進行中（包括解構子執行中）歸零的，等外層那個刪完才處理，而且後進先出。
- 對舊遊戲的後果：解構子的執行順序與時機改變。解構子裡 `delete` 的子物件、解構子裡的區域物件，都不會在當下解構；成員的解構順序與上游相反。腳本若依賴順序，結果不同。
- 多娜多娜：走同一個函式；解構子內已改回巢狀。
- 建議：`delete_struct()` 對所有版本都在解構子期間關掉延後（也就是拿掉 `: true` 的分支），或確認原版 v14 之前的行為後再決定。
- 信心：high（行為不同是確定的）；實際影響多大沒有評估。

### 9. `array_pushback()` 擴張後的新元素沒有初始化就被釋放

- 位置：`src/page.c` 約 886–916。引入：`14c2618`。
- 上游：經 `realloc_array()` 擴張，新元素先給初值，再由 `variable_set()` 換成推入的值。
- 現在：`malloc_size()` 夠就直接 `nr_vars + 1`，不夠就 `xrealloc` 成兩倍；新位置不初始化。接著 `variable_set()` 先對那個位置做 `variable_fini()`。
- 對舊遊戲的後果：元素是字串、結構、陣列這類型別時，會拿殘留值去 `heap_unref`。先 `PopBack` 再 `PushBack` 的用法最危險：殘留值正是剛彈出的槽號，而釋放清單後進先出，新推入的值很可能就用同一個槽，於是新值被釋放。整數／浮點陣列不受影響。
- 多娜多娜：不確定 v14 是否還經過這個函式。
- 建議：擴張後先把新元素設成 -1（或恢復上游的 `realloc_array` 路徑）。這條不分版本都該修。
- 信心：「沒初始化就釋放」high；出事的機率 medium。

### 10. `vm_execute()` 巢狀超過 10 層直接返回

- 位置：`src/vm.c` 約 5748–5757。引入：`14c2618`。
- 上游：沒有上限。
- 現在：深度超過 10 就印警告後 `return`，不執行。此時 `vm_call()` 已經推了呼叫框、取走了參數，框留在呼叫堆疊上，回傳值也沒有。
- 對舊遊戲的後果：建構子、解構子、HLL 回呼巢狀超過 9 層時，那次呼叫被跳過，呼叫堆疊錯位。會不會到 9 層看遊戲。
- 多娜多娜：會走到同一段。
- 建議：拿掉上限，或至少在跳過時把框退掉並補回傳值；v14 之前不要設限。
- 信心：high（程式碼）；觸發機率不明。

### 11. `ASSERT` 失敗不再結束

- 位置：`src/vm.c` 約 3575–3590。引入：`14c2618`。
- 上游：印訊息後 `vm_exit(1)`。現在：最多印三次警告，繼續執行。
- 對舊遊戲的後果：斷言失敗後繼續跑。
- 多娜多娜：會走到。
- 建議：`ain->version < 14` 時恢復 `vm_exit(1)`。
- 信心：high。

### 12. 設定過滑鼠位置之後，讀到的滑鼠位置不再更新

- 位置：`src/input.c` 203–225。引入：`4f689ac`。
- 上游：`mouse_get_pos()` 每次讀 SDL 的位置。
- 現在：`mouse_set_pos()` 把座標記在 `override_mouse_x/y`，之後 `mouse_get_pos()` 一律回傳它。整個檔案沒有任何地方把它清回 -1。
- 對舊遊戲的後果：腳本一旦移動過游標（例如「游標自動移到選項」），之後讀到的滑鼠位置就固定不動。
- 多娜多娜：只有在腳本設定游標位置時才會。
- 建議：滑鼠移動事件時清掉覆寫值，或只讓覆寫維持到下一次事件處理。
- 信心：high。

### 13. `SystemService.GetGameName`／`GetGameFolderPath` 的 C 簽名改了

- 位置：`src/hll/SystemService.c` 約 69、475。引入：`99ac346`。
- 上游：`void f(struct string **out)`（輸出參數）。現在：`struct string *f(void)`（回傳值）。沒有依版本切換（`static_library_replace` 沒有用在這裡）。
- 對舊遊戲的後果：舊遊戲的 AIN 宣告是輸出參數的形式，呼叫後輸出字串不會被寫入。拿它組檔案路徑的地方會得到空字串。
- 多娜多娜：會走到，用的是新簽名。
- 建議：依 AIN 裡的宣告（或版本）在 `PreLink` 換回舊實作。
- 信心：high（程式碼）；前提是舊遊戲的宣告確實是上游實作所對應的形式。

### 14. `CommonSystemData` 會寫檔，而且直接覆寫

- 位置：`src/hll/CommonSystemData.c` 全檔（`csd_save()` 約 104、`csd_load()` 約 133）。引入：`1dae88b`（commit 訊息自己標了 NOT VERIFIED）。
- 上游：只存在記憶體，不讀不寫檔。
- 現在：每次 `SetData*` 都以 `"wb"` 寫到腳本指定的路徑，格式是自訂的 `CSD1`；讀檔時魔數不符就不載入，下一次 `Set` 就把那個檔蓋掉。`GetData*` 另外加了型別檢查。
- 對舊遊戲的後果：資料會跨次啟動保留（與上游不同）；若那個路徑上已有原版引擎寫的檔，會被蓋成 `CSD1` 格式。
- 多娜多娜：會走到。
- 建議：寫入前若既有檔不是 `CSD1` 就不要覆寫；v14 之前是否要落盤另外決定。
- 信心：high。

### 15. `SystemService.UpdateView` 現在有實作（疑問）

- 位置：`src/hll/SystemService.c` 約 115；`src/frame_pacing.c` 138；`src/hll/SACT2.c` `sact_Update()` 約 183。引入：`2dfc74a`、`44964f9`。
- 上游：`HLL_QUIET_UNIMPLEMENTED`，什麼都不做。
- 現在：做 PartsEngine 的更新並呈現畫面；`frame_pacing_update_view_presents()` 沒有版本條件，只看「最近有沒有呼叫過 UpdateView」，成立時 `sact_Update()` 不再自己 `gfx_swap()`。
- 疑問：原先的假設是 v14 之前沒有 `UpdateView`。但上游特地放了一個「安靜的未實作」樁，通常是因為某個上游支援的遊戲頻繁呼叫它。如果有 v14 之前的遊戲宣告並呼叫 `SystemService.UpdateView`，它的畫面更新路徑就整個不同了。需要拿 v11–v13 的 AIN 查這個函式存不存在。
- 建議：查證後，`SystemService_UpdateView()` 開頭加版本條件。
- 信心：medium，寫成疑問。

### 16. 用 `.ain` 路徑啟動時會先去讀目錄裡的 ini

- 位置：`src/system4.c` `config_init_with_ain()` 約 337–348。引入：`6e73d5e`。
- 上游：直接用指定的 `.ain`。現在：先 `config_init_with_dir()`，成功就用 ini 的設定，**指定的 `.ain` 檔名被忽略**。
- 對舊遊戲的後果：目錄裡有 ini 時，載入的可能不是指定的那個 `.ain`；遊戲名稱、存檔目錄、視窗大小也改由 ini 決定。
- 多娜多娜：與版本無關，同樣會走到。
- 建議：ini 的設定可以讀，但 `.ain` 檔名要以命令列為準。
- 信心：high。

### 17. 致命錯誤時不再跳出訊息框

- 位置：`src/system4.c` `error_handler()` 約 450。引入：`6e73d5e`。
- 上游：`SDL_ShowSimpleMessageBox`。現在：只等背景寫圖結束。
- 對舊遊戲的後果：出錯時視窗直接關掉，沒有訊息。
- 建議：恢復訊息框（無視窗的探針另外處理）。
- 信心：high。

---

## 二、影響較小，或只在出錯路徑才不同

| # | 位置 | 引入 | 上游 → 現在 | 對舊遊戲 | 信心 |
|---|---|---|---|---|---|
| 18 | `vm.c` `method_call()` 約 1779–1790 | `14c2618` | `this` 無效時照樣進方法 → 整個方法跳過、回傳 0 | 對已釋放物件呼叫方法時結果不同 | medium |
| 19 | `vm.c` `stack_pop_var`／`stack_peek_var`；`heap.c` `heap_get_page`／`heap_get_string`／`heap_get_delegate_page`／`heap_unref`；`ffi.c` `hll_call` 未實作函式 | `14c2618` | `VM_ERROR` 結束 → 靜默回傳假值繼續 | 上游會報錯停下的情況現在繼續跑，可能帶著錯的資料 | high |
| 20 | `page.c` `copy_page()` 約 440–462 | `14c2618` | 無上限 → 深度 15 或一次複製超過 10000 頁就改成淺複製（共用子物件） | 一次複製很大的結構陣列時，複本與來源變成別名 | high／觸發機率低 |
| 21 | `page.c` `delegate_new_from_method`、`delegate_append` | `14c2618` | `fun < 1` 回空委派／不加入 → 照樣加入 | 函式編號 0 的項目會被加入並呼叫 | medium |
| 22 | `page.c` `variable_initval(AIN_DELEGATE)` | `14c2618` | NULL 頁 → 空的委派頁 | 應該看不出差別；存檔內容不同 | medium |
| 23 | `vm.c` `vm_execute()` 約 5805 | `14c2618` | 無 → 每 262144 個指令呼叫一次 `handle_events()` | 腳本長迴圈中途輸入狀態會變；多半看不出 | high |
| 24 | `input.c` `mouse_event()` | `4f689ac` | 立即反映 → 按下後至少維持 50ms | 短點擊的放開延後 | high |
| 25 | `input.c` `handle_window_event()` | `4f689ac` | 露出／改大小時 `gfx_swap()` → 只設 `scene_is_dirty` | 不重繪的等待迴圈中，視窗內容可能不更新 | medium |
| 26 | `hll/KiwiSoundEngine.c` `Get*Group` | `1dae88b` | 全回 0 → 依混音器名稱查，找不到時 SE 回 1 | 音量設定作用到不同的聲道 | medium |
| 27 | `vm.c` `function_call()` 字串參數的暫存旗標 | `14c2618` | 不加參考 → 沒有暫存旗標的字串多加一次 | 字串洩漏，行為不變 | medium |
| 28 | `heap.c` `global_page_slot = 1`；`resume.c` | `14c2618` | 全域頁在槽 0 → 槽 1 | **疑問**：上游或原版建立的 resume 存檔（全域頁在槽 0）能不能載入，沒有驗證 | low |
| 29 | `vm.c` `FTOI`、`_MSG`（`msgf <= 0`） | `0b8beae` | NaN 轉整數未定義 → 0；`msgf == 0` 視為沒有處理函式，並改找名為 `message` 的函式 | 邊角情況 | medium |
| 30 | `page.c` `delete_page`／`copy_page`、`vm.c` `A_REF` 的 1,000,000 元素上限 | `14c2618` | 無 → 超過視為損毀 | 超大陣列才會遇到 | high |
| 31 | `page.c` 解構子深度 64、`alloc_struct` 深度 50；`_function_call` 的 C 堆疊防護 | `14c2618` | 無 → 超過就跳過 | 極端情況 | high |
| 32 | `hll/MsgSkip.c` `msgskip_save` 開檔失敗 | — | `ERROR` → 回傳 false | 出錯路徑 | high |

---

## 三、v11–v13 另外的問題

`NEW`、`OBJSWAP`、`S_MOD`、`CALLHLL` 的第三個參數、`S_PLUSA`／`S_PLUSA2`（以 `instructions[CALLMETHOD].args[0] == T_INT` 判斷，等同 v11 以後）、`DG_STR_TO_METHOD`，以及 `hll_call` 對 `AIN_REF_HLL_PARAM`／`AIN_HLL_FUNC`／`AIN_HLL_PARAM` 的處理，條件都是「v11 以後」而不是「v14」。

上游的 libsys4（`ed74c9e`，`src/ain.c` 1196–1201）早就把這幾個指令在 v11 以後改成帶參數，而上游的 `vm.c` 仍從堆疊取，兩邊本來就對不上。所以這些改動比較像是補上上游缺的東西，不是迴歸；但 v11–v13 在這個分支上完全沒有驗證過，行為與上游不同是確定的。信心 medium。

---

## 四、看過並判定等價的段落

- `system_call`：依指示跳過（主線已比對）。
- `global_get`／`global_set`／`PUSHGLOBALPAGE`／`call_global_destructors`／`vm_free`：`heap[0]` 換成 `heap[global_page_slot]`，內部一致。
- `set_struct_page`／`unref_call_frame`：保活條件在 v6.1–v13 與上游相同，只差槽號為 0 的情況。
- `scenario_jump`／`scenario_call`、`alloc_scenario_page`、`_function_call` 的區域變數初值（`ain_option_type_is_value()` 在 v14 之前恆為假）。
- `CALLMETHOD`（v14 之前走上游那一行）、`CALLFUNC`／`CALLFUNC2`／`SH_STRUCTREF*_CALLMETHOD_NO_PARAM`／`THISCALLMETHOD_NOPARAM`（只多了無效位址的檢查；`--skip-title` 預設關）。
- `S_ASSIGN`、`SR_ASSIGN`、`DG_ERASE`、`DG_CLEAR`、`DG_ASSIGN`、`DG_PLUSA`、`DG_MINUSA`：v14 之前的分支與上游逐行相同。`DG_SET`／`DG_ADD` 只差槽號 `<= 0`。`DG_CALLBEGIN`、`DG_NEW` 等價。
- `A_REF`：v14 之前落到上游的複製。
- `IFZ`／`IFNZ`／`SWITCH`／`INC`／`DEC`／`S_ADD`／`S_NOTE`／`S_EQUALE`：只是改寫。
- 字串轉碼：`game_to_utf8()`／`utf8_to_game()` 在嚴格判定沒通過時就是 `sjis2utf`／`utf2sjis`；`mbcs_count_char()` 在字串規則不是 GBK 時就是 `sjis_count_char()`。（旗標本身的問題見第 1 條。）
- `vm_init_globals()`：v14 之前的初始化順序與上游相同。
- 堆積：釋放清單改寫、`mmap`、`heap_gc()`（`gc_collect_cycles` 是常數 `false`，只重建釋放清單；`heap_gc_periodic()` 一開頭就 `return`）、`vm_call_insn_limit`（沒有人設成非 0）。
- `delete_page()` 在解構子期間把參考數設成 65536：解構子照常會跑。
- `delegate_numof`／`erase`／`plusa`／`minusa`／`clear`／`get`／`contains`：v14 之前與上游相同；`delegate_page_register`／`delegate_target_freed` 只在 v14 動作。
- `init_struct`、`alloc_struct`、`variable_type`（多了範圍檢查）、`array_copy`。
- `hll_call` 的引數封送：v14 之前既有的型別只多了範圍檢查。
- `src/video.c` 全部（視窗標題轉碼、截圖與效能量測都要環境變數才啟用）。
- HLL：`Math`（`math_uses_r521()` 是 `ain->version >= 14`，之前仍用 `rand`／`srand`）、`SACT2` 的 `GAME_MSG_Get`（原本未實作）、`MsgSkip_PreLink`、`InputString`、`iarray`、`vmDialog`、`Gpx2Plus`、`FileOperation`、`vmString`、`AnteaterADVEngine` 的轉碼。
- libsys4 的 `initialize_instructions()`：v11 之前把數值設回上游的值。

---

## 五、`src/hll/` 清單（上游就有、被改動的 23 個檔；另有 25 個新增檔沒看）

「條件」是新增行裡出現版本或編碼判斷的次數，0 表示改動處完全沒有版本條件。

| 檔 | 規模 | 條件 | 被改的既有函式／備註 |
|---|---|---|---|
| `MainEXFile.c` | +645/−503 | 2 | 幾乎每個既有函式都改了（`Int`、`Float`、`String`、`A*`、`A2*`、`IA2*`、`SA2*`、`RA2*`、`GetRowAt*`、`GetNodeName*`、`GetEXName*`、`ModuleInit`…）。**沒有分析，風險最高。** |
| `Array.c` | +4426/−457 | 8 | 既有函式被改的只有 `Array_NV_sceq`、`Array_NV_eneq` 與匯出表；其餘是新增 |
| `SystemService.c` | +539/−48 | 3 | `GetGameName`、`GetGameFolderPath`（第 13 條）、`UpdateView`（第 15 條）、`GetWindowSetting`、`Get/SetMouseCursorConfig`、`GetDate`，多個原本未實作的改成有實作；`SetAntiAliasingMode` 的匯出行被動過，沒有追 |
| `CommonSystemData.c` | +190/−59 | 0 | 全部（第 14 條） |
| `KiwiSoundEngine.c` | +409/−12 | 0 | `GetMasterGroup`、`GetBGMGroup`、`GetSEGroup`、`GetVoiceGroup`、`GetGimicSEGroup`（第 26 項） |
| `MsgSkip.c` | +438/−9 | 1 | `msgskip_save`、`MsgSkip_SetFlag`／`GetFlag`（多了空指標檢查）、`PreLink` |
| `PartsEngine.c` | +1178/−0 | 1 | 匯出表與 `PreLink`／`ModuleInit`／`ModuleFini`；沒看 |
| `AnteaterADVEngine.c` | +220/−3 | 8 | `PreLink`（`assert` 改成容錯）、轉碼 |
| `Math.c` | +410/−6 | 2 | `Rand`、`SetSeed`、`RandF`、`SetSeedByCurrentTime`、`Abs`（v14 之前等價） |
| `CGManager.c` | +79/−7 | 0 | 原本註解掉的函式改成回固定值的樁 |
| `StoatSpriteEngine.c` | +30/−13 | 2 | `extract_sjis_char`（看 `ain_is_gb18030`，見第 1 條）、三個 `Debug_*` 改成樁 |
| `CharSpriteManager.c` | +23/−6 | 2 | `extract_sjis_char`（同上）、`charsprite_render` |
| `SACT2.c` | +31/−3 | 0 | `sact_Update`（第 15 條）、`sact_TRANS_Update`（沒看）、`GAME_MSG_Get` |
| `FileOperation.c` | +49/−1 | 0 | `get_file_list`（轉碼）、`GetFolderList` |
| `VSFile.c` | +35/−2 | 0 | `ReadString` 拆出輔助函式；沒細看 |
| `iarray.c` | +16/−2 | 0 | `iarray_write_string`、`iarray_read_string`（SJIS 時等價） |
| `InputString.c` | +4/−2 | 1 | 轉碼 |
| `ChipmunkSpriteEngine.c` | +3/−2 | 0 | 匯出表 |
| `ReignEngine.c` | +4/−1 | 0 | `HLL_LIBRARY(SealEngine…)` 那一行移走了；`ffi.c` 的清單裡 `lib_SealEngine` 現在出現兩次 |
| `Gpx2Plus.c`、`vmDialog.c`、`vmString.c` | 各 1–3 行 | 0 | 轉碼／字元計數 |
| `hll.h` | +155/−0 | 0 | 新增巨集與 `static_library_replace` |

---

## 六、沒有看的範圍

- `src/parts/` 全部（約 +6000 行；`parts.c` +1683/−88、`render.c` +327/−46、`input.c` +429/−17、`save.c` +432/−28、`text.c` +142/−14、`construction.c` +1435/−4）。PartsEngine、GoatGUIEngine、GUIEngine 是 v14 之前也在用的，這是沒看的範圍裡最大的一塊。
- `src/text.c` 其餘（+153/−16）、`src/util.c`（+324/−6）、`src/hacks.c` 的 `apply_game_specific_hacks`、`src/asset_manager.c`、`src/audio.c`、`src/audio_mixer.c`（+25/−12，無條件）、`src/font_freetype.c`（+38/−9，無條件）、`src/format.c`、`src/effect.c`、`src/movie_ffmpeg.c`（+131/−9，無條件）、`src/3d/`、`src/screenshot.c`、除錯器、`include/`。
- `src/resume.c`、`src/savedata.c` 只掃過 diff，沒有追載入流程（見第 28 項的疑問）。
- `src/vm.c`：純新增的大段（v14 的新指令、訊息覆蓋層、追蹤、`vm_ask`、`vm_construct_struct`、`vm_call_nopop`、`init_func_flags`）只看頭尾；`method_call` 與 `CALLMETHOD` 的 v14 分支沒細看。
- `src/heap.c` 的標記階段（目前是死碼）；`src/page.c` 的 `init_struct_slot()` 尾段與 v14 專用輔助函式；`src/ffi.c` 的連結／初始化部分。
- `src/input.c` 的鍵盤與控制器部分；`handle_events()` 新增的約 100 行只確認是 `XSYS4_HOLD_KEYS`、`XSYS4_AUTO_CLICK*` 這些環境變數的測試輔助。
- `src/hll/` 的 25 個新增檔，以及上表標「沒看」「沒分析」的部分。
- libsys4 子模組的指標從 `ed74c9e` 換成 `247f544`（9 個檔，+390/−10：`string.c` +261、`utfsjis.c`、`system.c`、`hashtable.c`、`instructions.c`、`little_endian.h`）。只看了 `mbcs_count_char()` 與 `initialize_instructions()`。這不在 `src include` 裡，但舊遊戲全都會經過。
- 所有結論都沒有用 v14 之前的遊戲實跑或反組譯驗證。

## 七、建議的下一步

1. 先修第 1 條與第 12、16、17 條：改動小、與 VM 模型無關、確定性高。
2. 第 2 到 11 條大多可以用同一個做法：在 `14c2618` 動過的共用路徑上，v14 之前回到上游的寫法。第 9 條不分版本都要修。
3. 找一個 v14 之前、上游能跑的遊戲實際開一次。第 1 條若成立，標題畫面就看得出來；這一步能同時回答第 2、7、15、28 條的疑問。
4. `src/parts/` 與 `MainEXFile.c` 需要另外一輪稽核。
