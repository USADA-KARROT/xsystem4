# 第一階段：對話自動前進的靜態對照與最小診斷

查證日期：2026-09-09；本項工作僅讀取來源、既有反組譯及主執行者產生的實測紀錄，未修改引擎或啟動遊戲。沒有宣稱修復成功。

來源簡稱與固定位置：

- **W**：WIP `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b`，`<WORKSPACE>/work/stage1/wip-source`。
- **R**：Rufim `589cf2c`，`<WORKSPACE>/work/stage1/rufim-source`。主執行者處理的建置相容性改動不代表此版本已能通過此中文遊戲開場。
- **AIN**：`<USER_HOME>/Claude/projects/dohna-cn-dump/ain_code.txt`。以下行號均為此既存反組譯的實際行號；函式 ID 來自 `FUNC`，不是 functions.txt 的行號。
- **Runs**：`<WORKSPACE>/work/stage1/runs`。

## 結論與實測界線

下一階段先固定完整 1280×720 畫面與單次開始輸入，再記錄 `Join` 每一步刪掉哪些 motion／section。現有證據不足以將自走歸因於 `SDL_GL_SwapWindow`，也不足以認定「constructor epoch reset」已經修好 timer。

1. 主執行者已使相同 WIP binary 以 `.app` 形式顯示彩色標題與中文新遊戲／讀取選單。隨後實體點擊那一輪的灰黑畫面，真視窗與內部 main_surface 相同，且 heartbeat 還在 `SceneTitle`；這是內容／轉場／輸入狀態問題的線索，不能把它當作已進入 ADV 的對話等待測試。`Runs/dialogue-visible/observations.json:5–13`。
2. 後續 **single-start** 使用既有 `XSYS4_AUTO_CLICK_SEQ=20000,85,82`，成功到雕像開場和 TV；此後沒有追加輸入，但 log 連續出現 `MSG 2` 至 `MSG 40`。原始設定與版本在 `Runs/single-start/run.json:11–20`；輸出在 `engine.log:60–148`。這支持自動前進仍可重現，但 `MSG` 一行不必然等於遊戲的一頁，仍要用實際頁面和 WaitForClick 退出原因驗收。
3. W 預設 800×600（`W/src/system4.c:60–61`）；主執行者確認這份 AliceStart.ini 沒有 ViewWidth／ViewHeight。R 在沒有 ini 尺寸且存在新世代 `PartsEngine.SeekMessage` 時套用 1280×720（`R/src/system4.c:359–368`）。裁切可解釋部分「文字／按鈕不見」，不能單独解釋無輸入仍不斷產生後續對話；主執行者正以隔離 ini 對照。
4. Rufim 目前實測在 `CActivityWrap.jaf:20 (nonnull) m_root` 失敗、未到標題，故只能當靜態語意參考，不能寫成已通過本機開場的替代基線。`Runs/baseline-cn/engine.log:25–26`。W single-start 同樣有 Personality.jaf 的 `id != ""` assertion（`:32–33`），其後仍繼續跑；「退出碼 0」不代表遊戲無錯誤。

## 實際腳本路徑：完成條件在集合內容

| 位置 | 已確認的實際語意 |
| --- | --- |
| AIN:998452–998493，Join(array<string>)，fno 27031 | 移除空 section，確認 Any，將名稱 Concat 至 m_joinSectionNames，再 Unique。不是直接呼叫 native parts motion 的等待。 |
| AIN:998503–998533，lambda 36081 | 先呼叫 EraseEndTask(27034)，再判斷 IsEndWaitSection(27033)。只有 true 時才 EndWaitForClick(0)、清空 joinNames 並設定捕獲的 isFinish。Observer 每幀執行本身符合這份腳本。 |
| AIN:998725–998746，EraseEndTask | Array.EraseAll(m_motions, lambda 36086)。predicate 回傳 `!motion.IsAlive`。 |
| AIN:995230–995249，IsAlive，fno 27001 | 沒有有效 motion 參照時 false，否則為 `!m_isFinish`。要查 motion 參照與完成位元，不應只查經過時間。 |
| AIN:998679–998723，IsEndWaitSection | 外層 EraseAll(joinNames, 36084)；36084 用內層 IsExist(m_motions, 36085) 找 Section 等於捕獲 name 的 motion，找不到才刪 name；最後回傳 joinNames.Empty。**這裡沒有 Array.All。** |
| AIN:525172–525279，WaitForClick，fno 9196 | 迴圈更新畫面及輸入；點擊 parts 或 busyLoop 等條件會退出。 |
| AIN:525281–525291，EndWaitForClick，fno 9197 | 設置 busyLoop 與返回 partsNumber，會使上述等待退出。不能直接壓掉此函式，因正常動畫完成也依賴它。 |

這條鏈至少有三種不同故障模式：motion 提早被判定死亡而刪除、Section／捕獲 name 比對錯誤而把 joinNames 清空、或上述集合語意正確但呼叫了錯誤時機的 Join。最小 trace 應先區分這三類。

## 可定位的引擎差異

### 1. 優先查 FFI 巢狀回呼的上下文與所有權

W 在每次 HLL 進入時覆寫全域 `hll_current_arg3`、`hll_self_slot`（`W/src/ffi.c:394–399`），HLL_FUNC 只把 fno 傳給 C，receiver 放在全域 `hll_func_obj`（`:611–626`），退出又將前兩者設為 -1（`:777–778`），沒有保存／復原外層上下文。

上述 IsEndWaitSection 確實構成巢狀 HLL：外層 EraseAll 的 arg3=2 → VM predicate 36084 → 內層 IsExist 的 arg3=65538 → VM predicate 36085。內層返回後，外層 `Array_EraseAll` 在 `W/src/hll/Array.c:208` 才讀 `array_elem_is_ref()`；其判斷依賴已被改成 -1 的全域（`:47–48`）。因此這條實際路徑存在外層元素清理使用錯誤型別上下文的具體問題。這首先顯示所有權／清理錯誤風險；尚不能僅憑此推定 predicate 回傳值或自走的直接原因。

此外，內層任一不同 receiver 的 HLL callback 可覆寫 hll_func_obj，使外層下一次 callback 用錯 receiver；本次 36084／36085 都以 collection 的 PUSHSTRUCTPAGE 傳入，**不能宣稱這個特定巢狀組合一定發生 receiver 改變**。應記錄 callback 前後值確認。

R 使用每次呼叫自有的 `(obj,fno)` 陣列副本（`R/src/ffi.c:600–613`），保存與復原目前 HLL 宣告及引用追蹤範圍（`:689–708`）；Array predicate 接收這個 pair，`vm_call_hll_func` 明確設置 receiver 並依參數型別處理 copy／borrow（`R/src/vm.c:1416–1483`）。R 的 EraseAll 逐個刪除邏輯元素並重新檢查 array（`R/src/hll/Array.c:973–985`）；W 則迭代預先捕獲的原始 page slots、最後重建 array（`W/src/hll/Array.c:173–229`）。

**最小修復入口**：先在隔離分支把 W 的 HLL 呼叫上下文保存／復原做完整，不只修單一 Array 函式；審核 `hll_param_slot2`、receiver、自身 heap slot 及返回清理使用的 metadata。先用一個巢狀 EraseAll→IsExist、小型字串／wrap 陣列與不同 receiver 的樣本驗证，再重跑同一開場。不要把這當成已確認的一行修復。

### 2. timer 歷史解釋有確定缺口，但直接 motion timer 是 RCASTimer

W `init_func_flags` 只將 CASTimer 的 Get／Reset／GetScaled 標記為 INST，且明確排除 RCASTimer（`W/src/vm.c:264–272`）。`native_cas_timer_intercept` 最前面拒絕未標記函式（`:317–318`），所以後面的 `@0/@1/@2` constructor reset 分支（`:398–408`）對 CASTimer 實例 constructor 不可達；manager constructor 在另一分支提前返回。這是真實的靜態缺陷，原註解所述「已透過 constructor reset 解決」不成立。

另外，handle 只在 `<2048` 初始化（`:321–327`），讀寫映射用 `abs(handle)%2048`（`:366–367`）；handle 與槽位不是永久一對一。`GetScaled` 沒有乘 `cas_timer_rate`（`:342–352` 對照 `:383–389`）。以上應獨立追蹤，不能據此直接宣稱此輪自走根因。

**關鍵限定**：ExecuterTask.Update 的時間來自 **RCASTimer@GetScaled (27258)**，不是 native CASTimer：`AIN:998782–998821`。RCASTimer 自身 m_time 經 `AddTime(27257)` 累加，GetScaled 只是讀取它（`AIN:1004802–1004848`）；RCASTimer constructor 將 AddTime delegate 註冊到 manager。manager 在 end-update event 收到每幀 delta（`AIN:1004620–1004693`），UpdateTime 再通知訂閱者（`:1004725–1004800`）。

native CASTimer 仍可能**間接**影響 delta：`view::detail::CalcPassedTime` 讀 g_FrameTimer.Get／GetScaled，分別 Clamp 至 50／150 ms，然後 Reset（`AIN:848494–848563`）；`parts::detail::Update` 呼叫 begin/end update events（`:524893–524913`）。因此先記錄每幀 delta、RCASTimer 的 m_time 與 m_span、AddTime 的 receiver 和每幀次數，比泛改所有計時器更能辨別原因。

R 沒有 W 這套 native CASTimer 攔截。它處理 bytecode 的引用／身分：`A_REF` 對借用參數保留同一 struct，對持有者取值做拷貝（`R/src/vm.c:3880–3895`），其周邊解釋 VariableTimer constructor 註冊本體而 Array 存入複本會造成 timer 不更新（`:3863–3875`）。這是有用的診斷模型，**不是本次 ExecuterTask 已被證明遇到同一缺陷**。W 另有主動補呼叫 RCASTimer／VariableTimer 成員 constructor 的機制（`W/src/vm.c:280–299,2139–2164`），應驗證是否正確且只註冊一次。

### 3. closure 環境與 section 名稱應一起驗證

W HLL lambda 將緊鄰前一 VM frame 當 env_page（`W/src/vm.c:1413–1420`），X_GETENV 直接取該頁，否則 fallback this（`:4406–4414`）。R 用保存在 delegate 的環境優先，否則按 lexical parent 找 frame（`R/src/vm.c:493–503,2292–2308`）。

在同步 36084→IsExist→36085 中，36084 恰好也是 36085 的 lexical parent；所以不能僅因策略不同就說這裡一定捕獲錯誤。要驗證 36085 的 X_GETENV 最終确实讀到 36084 的 name slot，以及 36081 設定 isFinish 時環境是尚存的 Join frame。W delegate 環境另由三槽記錄讀出（`W/src/vm.c:1325–1333`），需與產生時的環境相符。

W Unique 只比相鄰原始 slot 數字（`W/src/hll/Array.c:671–690`）；Join 剛做 Concat，沒有明確排序。字串內容相同、heap slot 不同，以及非相鄰重複項的行為應列入 Array 驗收。此差異通常較像重複／多餘等待，現階段優先級低於「列表為何瞬間變空」。

## 最小埋點與下一階段验收

先以相同二進位、獨立存檔、明確 1280×720、單一開始事件重現，記錄自動模式／skip 關閉。只追第一個相關 collection 和前 3 次等待，避免 VM 全指令海量輸出。

| 埋點入口 | 必須記錄的欄位 | 可排除的假設 |
| --- | --- | --- |
| 27031 Join、27034 EraseEndTask、27033 IsEndWaitSection 進出 | 時間、frame、collection heap slot、motions/joinNames array slot與元素數、刪除前後清單 | 初始列表本來就空、提早清理、重建 array 損毀 |
| 36086 predicate、27001 IsAlive | motion slot、ref/page type、參照有效性、m_isFinish、bool 回傳 | 因無效物件／錯 receiver 誤刪 |
| 36084/36085 predicate、27005 Section | callback fno/obj、env_page、parent fno、name／Section 的 heap slot與短字串或雜湊、比較結果 | captured name 錯誤、內容不同、生命週期損壞 |
| W ffi.c:394、620、675附近、777；Array.c:196、208、1603 | HLL 深度、函式名、arg3、self slot、func obj，callback 前與後各一筆 | 巢狀呼叫破壞外層上下文 |
| 27038 task.Update、27253 manager.UpdateTime、27257 AddTime、27258 GetScaled | task/timer receiver、m_span、m_time、delta、每frame觸發次數、註冊 delegate 的 obj | timer 重複更新、身分複製、過早到期 |
| 9196 WaitForClick、9197 EndWaitForClick | 本次等待序號、入口/出口 frame、busyLoop、click parts、skip/auto、呼叫堆疊及退出分支 | 真實輸入、全域等待訊號、正常 Join 完成與意外結束 |

驗收順序：

1. 實際可見 1280×720 完整畫面，視窗與 internal frame 同場景；新遊戲事件只送一次。
2. 第一段應等待的對話文字與 NEXT 指示確實可見，無輸入至少停留 20 秒；同時 log 不繼續越過該頁，WaitForClick 不意外退出。
3. 點一次只推進一頁，下一頁再次停留；按住／放開、快進、auto 另外測，不和基本等待混為一輪。
4. 若先修 FFI 上下文：巢狀 predicate 測試要證明外層 receiver、arg3、元素生命週期與結果都保留，再跑上述開場；ASan／UBSan 的同一路徑無錯誤才可寫 memory check 通過。
5. 先前特定白屏轉場需与 Wine 同場景比較；白色背景本身不是錯誤，但缺字、缺 NEXT、等待失效仍是獨立不合格。

不可作為修復完成證據：只有 PNG 檔案、僅 MSG console 輸出、只有 title 成功、退出碼 0、或把 EndWaitForClick／Join completion 強制禁止。這些都不能代替看得見且能正常停等的中文 ADV。
