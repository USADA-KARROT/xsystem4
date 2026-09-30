# 交接：多娜多娜繁中版 xsystem4 macOS 移植（2026-09-28）

給接手的工程師或代理。先讀完本文與 [STATUS.md](STATUS.md)，再動手。

## 現在的位置

- 分支 `wip/post-checkpoint-2026-07-06`，以 `origin` 最新 commit 為準。libsys4 指標為 `247f544`（使用者已同意由 `8c93946` 更新；本機分支 `gbk-rules-20260929`，推送前只存在本機）。
- 成就通知斷言（`2914b40`）、角色對話正文（`1540b85`）與存讀檔持久化（`173ff1d`）已修正。兩次 150 秒 GUI（新存檔、重用存檔）MSG 88、assertion 0、堆疊溢位 0，framebuffer 已確認正文可見；第二次確認設定與 Collection 從檔案讀回。
- Headless 驗證 56 個模式全部符合預期（另以 `XS4_PROBE_GBK=1` 在 GBK 規則下全部重跑通過），0 個 sanitizer 診斷。
- 立繪與名牌不退場的 use-after-free 已修正（`9e30c0f`）：介面參數與參照型 option 參數在呼叫時補上參照，照原版 `0x657430`。到 `6421e6e` 為止已在遠端。
- 字距依原版 GDI 字格修正（`9f81bd9`，已在遠端；側審查 D1–D4，見 `research/gui-visual/spacing-fix.md`）。
- delegate 呼叫的參數複製修正（`2005274`，本機 commit，尚未推送）：一格堆疊對一個參數變數，不再把兩槽參數的 void 伴隨變數當成下一個參數（原版 `0x66dce0`／`0x657430`，見 `research/gui-visual/delegate-args.md`）。
- 翻轉旗標作用在整棵元件樹（`0ab8476`，本機 commit，尚未推送；側審查 D5–D7）：沿父元件鏈 XOR、以錨點為軸鏡像方框與子元件位置（原版 `0x535260` → `0x4e6d80`，見 `research/gui-visual/reverse-inherit.md`）。
- String 字元規則已改照原版的 GBK 規則（`6400e3c`，第 5 項）。
- 據點畫面缺件與 SetButtonEnable（`6d39915`，本機 commit，尚未推送；第 4 項）：父元件立即掛上、pactex 依原版 `部件タイプ` 表設型別等九項（見 `research/gui-visual/base-ui.md`）。底列與「下一步」出現，按下後進入階段選擇；階段選擇之後因教學圖層沒有釋放而停在 `PE_AddController`（根因是場景物件不釋放，已由 `6582e38` 修正）。獨立審查 fix-first，審查項目由 `fb28975` 修正（見下一條）。
- 流暢度（`44964f9`，本機 commit，尚未推送；第 4 項）：照原版 `0x4676f0`／`0x4c5450` 限速並每幀呈現一次，元件時間只推進一次，截圖改在背景執行緒寫檔，訊息視窗文字在繪製時才排版（見 `research/gui-visual/pacing.md`）。一般遊玩固定 58.7 fps，AIN 時間倍率 0.74→0.97。獨立審查 ship（低：結束時沒有等待截圖寫完，`fb28975` 修正）。
- 據點審查的修正（`fb28975`，本機 commit，尚未推送；第 4 項）：停用按鈕不送點擊、偵測元件的點擊判定改用預設狀態、兩槽 Array 元素（`At`／`First`／`Last` 推兩槽，`First(pred)`／`EraseAll`／`Concat`／`Reverse`／`Insert` 以元素為單位）、pactex 的 矩形部件／構築部件型別、結束時等待截圖（見 `research/gui-visual/base-ui.md` §10）。兩槽 `First(pred)` 修正後 `Motion::PartsParamCollection@0` 才找得到 TimeParam，所有 motion 不再一律 1000 ms，開場到據點約提早 11 秒。52 模式兩種組態通過。第二輪獨立審查 fix-first，審查項目由 `6582e38` 修正（見下一條）。
- 第二輪審查的修正（`6582e38`，本機 commit，尚未推送；第 4 項）：v14 delegate 照原版不持有目標物件（`0x652210`／`0x681f70`），場景物件終於會釋放，按下新遊戲後標題按鈕隨圖層消失、教學關閉後據點背景出現、鑑賞模式返回不再 VM_ERROR、階段選擇可以進入春銷；連帶照原版補上解構子內的立即解構、`RemoveController`／`ReleaseActivity` 回傳 delegate index、空的懸停狀態，並刪除錯讀 `<vtable>` 的 CParts 釋放特例；`A_REF` 對陣列照原版複製（數字、字串、通用元素陣列），標題構圖不再被 `GetReverse` 就地反轉；點擊派送與懸停規則一致（見 `research/gui-visual/base-ui.md` §11）。53 模式兩種組態通過。第三輪獨立審查 fix-first，審查項目由 `80db27d` 修正（見下一條）。
- 第三輪審查的修正（`80db27d`，本機 commit，尚未推送；第 4 項）：v14 pactex loader 照原版讀 `オン指針透過`（`0x5547f4` → 元件 +0x1a5），懸停與點擊共用原版的輸入目標（`0x546890`／`0x545e10`／`0x546e20`：可點擊或不穿透游標的第一個命中元件，懸停只有一個目標），標題角色、人材卡的文字、環節卡的角色小圖不再擋住後方按鈕；刪除 v14 fork 依指令數把解構子列入黑名單的邏輯（巢狀解構讓 `CActivityWrap@1` 在第一次長拆除後被永久略過，這也是人材 → 返回 → 下一步後 `Executer.jaf:55` 的根因）。新模式 `third-review` 在 `7cb6ba3` 上 5/5 失敗、修正後全過；54 模式兩種組態通過（見 `research/gui-visual/base-ui.md` §12）。尚未經獨立審查。
- 尚非穩定可玩版：從讀檔畫面讀一般存檔需要 `system.Reset`（stub）、記憶體持續成長，長時間穩定性與完整遊戲流程未驗證。

## 硬規則

1. 原始遊戲安裝目錄是唯一母片，只允許唯讀、列檔、算 hash、複製成工作副本。不可修改、覆蓋、刪除、改名，也不可在裡面產生測試檔、日誌、存檔或快取。所有執行一律使用工作副本，並設定 `XS4_MASTER_GAME` 讓腳本擋下誤用。
2. 原版 EXE 的解殼傾印只能靜態反組譯（例如 capstone），不可執行。它是原版語義的最高權威。
3. 這是公開 repo。不可提交遊戲資產、執行檔、存檔、截圖、完整 AIN 反編譯 dump、個人路徑或帳號資訊。
4. GitHub 是唯一真相，本機目錄隨時可能被清除。每完成一組就推送。
5. 不 force push、不 rebase、不改 submodule 指標。這三件事要先得到使用者同意。（GBK 字元規則一組已得到使用者同意，更新了 libsys4 指標。推送時先推 libsys4 `cn-on-upstream`，確認遠端 SHA 後再推 xsystem4。）
6. 不向上游 nunuhara/xsystem4 開 PR 或 issue。上游貢獻暫緩，由使用者決定時機。
7. 報告與文件使用台灣繁體中文。commit 訊息沿用現有英文風格：`Area: imperative summary`，內文說明根因、原版語義來源與驗證。

## 根因與修法模式

`src/ffi.c` 的 `link_static_library` 只按名稱把 AIN 宣告綁到 C 函式，libffi 的 CIF 卻依 AIN 宣告建立。同名多宣告（overload）或原型不符時，會讀到暫存器殘值、參數錯位、忽略參數，或 C 端沒有推回傳值。這是目前大多數崩潰與錯誤行為的共同根因。

修法是依宣告形狀選 C 實作，不看函式序號：

- `link_static_library` 對每個宣告呼叫 `hll_select_overload(庫名, 宣告, 預設)`。
- Array、Math、String 各有自己的選擇器，位於各自的 `src/hll/*.c`。其他庫集中在 `src/hll/hll_shape_select.c`。
- 不認得的形狀一律回傳原本的綁定。回 NULL 會讓宣告走 UNIMPL 路徑，靜默回 0，比原本更糟。
- 新程式碼不得比舊綁定更糟。不支援的 callback 形狀只印有限次數的警告並回安全值，不要觸發 `VM_ERROR`。
- `ref hll_param` 回傳由 C 端自己推：值元素推兩槽 `[owner, idx]`（v14 時對 owner 做 heap_ref），參照元素推一槽 heap slot。統一經 `Array_At`。
- `hll_arg3` 編碼：1 是 int 類；2 是字串或值 struct；0x10002 以上是 ref struct；0x10003 是介面（兩槽）。

## 每一組修正的流程

1. 在 AIN 反編譯 dump 裡找出所有宣告與呼叫點，統計各形狀的呼叫次數。
2. 從原版 EXE 反組譯確認語義，記下 dispatcher 與分支位址。已知 dispatcher：

   | 庫 | dispatcher | 跳表 |
   |---|---|---|
   | Array | `0x644300` | `0x644f18` |
   | Math | `0x4c6520` | `0x4c6ca0` |
   | String | `0x684120` | `0x684984` |
   | HashMap | `0x654fb0` | |
   | PartsEngine | `0x57b900` | `0x589714` |

3. 請一位獨立的審查者嘗試反駁結論。反駁成立就照改或延後，並記錄原因。先前反駁推翻過 Copy 長度夾限、HashMap `Free` 語義，以及 `GetStructPageList` 的元素數算法。
4. 在 [harness](harness/README.md) 新增 fixture。先用 `before-check.sh <上一個 commit> <mode>` 證明修正前失敗，再實作。
5. `verify-step.sh <tag>` 必須是 `VERDICT PASS`。
6. `gui-run.sh <name> 150` 不得回歸：對白行數不少於 88，不得出現新的堆疊溢位。存讀檔相關的修正要跑兩次（`RUN_TRACE_SAVE=1`；第二次以 `RUN_SAVE_SEED` 重用第一次的存檔）。
7. 一組一個 commit。推送前後都用 `git ls-remote` 與 `gh api repos/USADA-KARROT/xsystem4/branches/wip/post-checkpoint-2026-07-06` 確認遠端 SHA，不要只信 push 的輸出。
8. 更新 [STATUS.md](STATUS.md) 與 README 頂部的狀態行。

## 下一批任務（依優先序）

1. **已完成：成就通知斷言**（`2914b40`，實際為型別失配；研究見 `research/achievement-text/`）：`SceneAchievementNotify.jaf:12 (nonnull) m_act.GetText("TextAchievement")`。第一個成就解鎖時，activity 依名稱找不到文字元件。先確認 activity 如何建立具名元件，以及查名稱走哪個 HLL 呼叫，再判斷是實作缺漏還是另一個原型錯配。
2. **已完成：角色對話正文**（`1540b85`，研究見 `research/dialogue-text/`）：v14 Free/Clear 保留元素型別、ShallowCopy 保留共享 struct/string owner。兩個 before fixture 失敗、修後通過；正式 GUI 已確認正文可見。沒有修改渲染排序，其他未驗證形狀保留原綁定。
3. **已完成：存讀檔持久化**（`173ff1d`，研究見 `research/save-persistence/`）：GetStructPageList、SerializeStruct、DeserializeStruct（就地載入）與存檔註解依原版 v9 實作；ffi 重入時的參數釋放與 option<int> 刪除一併修正。審查後的 `6b65b12` 修掉三個可重現缺陷：值型別 option 在 ASSIGN／X_OP_SET／複製時仍被當 slot、損壞檔的 struct 定義數讓讀檔崩潰、讀檔新建無建構子 struct 的成員停在 null（`save-fixes` 模式）；第二輪 `ff77c18` 補上 `<vtable>` 填寫、三槽 option 的 none 與 option<int> 區域變數洩漏。後續小項：
   - `system.Reset` 是 stub，讀檔畫面讀一般存檔的路徑走不到；先確認原版 Reset 的語義（重新進入 `main` 並保留 GameVariable）。
   - PE_Save／PE_Load 不保存 `2914b40` 的 `component_type_from_state`（`6d39915` 改名為 `component_state_type`，同樣未保存）；ResumeLoad 之後成就通知的 GetText 是否再次失敗尚未驗證（本組兩次 GUI 都沒有觸發 ResumeLoad）。
   - 一般 `NEW` 建立沒有 STRT 建構子的 struct 時，成員仍留在 null；原版 `0x679b30` 會依 `0x656970` 預設初始化。`6b65b12` 只修了讀檔路徑，引擎層要另外評估影響面。
   - DeleteSaveFile 在檔案不存在時原版回 true（`0x5c69f0`）；`init_struct_slot` 的 enum 陣列型別（R2）；A_REF 暫存字串殘留（F13）。
4. **使用者回報的畫面問題**（研究見 `research/gui-visual/`，依建議順序）：
   - **已完成：立繪與名牌的退場、換位、隱藏**（`9e30c0f`，研究見 `research/gui-visual/uaf.md`）。根因是 v14 呼叫的介面參數（`AIN_IFACE`）與有值的參照型 option 參數以借用方式傳入，被呼叫端返回時卻會釋放；`function_call` 與 delegate 路徑沒有加參照，`Motion::Create`／`Motion::Executer@0` 因此把 `AdvStand.m_parent` 的 sprite 與名牌 root 提早釋放。修正照原版 `0x657430` 補上 retain。新模式 `iface-arg` 修正前 6/6 失敗；150 秒 GUI 修正後三次 MSG 88，立繪會退場，名牌不再疊字，`heap_alloc_slot` 警告歸零。後續另案（詳見 uaf.md〈另案〉）：
     - **已完成：delegate 呼叫把 void 伴隨槽當參數**（`2005274`，研究見 `research/gui-visual/delegate-args.md`）：`delegate_call` 改為一格堆疊對一個參數變數，照原版 DG_CALL 處理常式 `0x66dce0`（取 `nr_arguments` 個值）與 `0x657430`（第 i 格進第 i 個變數）。修正前，兩槽參數在最後時，第一個區域變數被寫成 delegate page；在中間時，之後的參數依錯的型別加參照。真 AIN 重現：Tutorial selector 讓 `ArrayExtensions::Select` 的 delegate page 被釋放；特殊客人收入函式釋放借用的 SpecialCustomer。新模式 `delegate-args` 在 `6421e6e` 上 3/4 失敗，修正後全過；48 模式兩種組態通過；150 秒 GUI MSG 88。特殊客人事件與編輯器路徑在 GUI 走不到，只有 headless 驗證；尚未經獨立反駁者審查。
     - **STRUCT／DELEGATE／ARRAY 參數多加一次參照**（headless 驗證一例）：原版不加，呼叫端的 `A_REF` 已交出所有權，每次呼叫多漏一份。拿掉之前要確認沒有借用傳 struct 的路徑。
     - **已解決：`Motion::Executer` 與 `CParts` 不釋放**：根因是 delegate 強參照循環，`6582e38` 改為弱目標後兩者都會釋放（base-ui.md §11.1）。
     - Tutorial 路徑以 null 物件呼叫 `Motion::Create` 等方法（每 150 秒 34 次 -1）。`2005274` 前後的 GUI 追蹤分布相同：SceneParentStack 的陣列本身就含 null rect，與 delegate 伴隨槽無關。
     - `vm_call_nopop`（HLL 回呼）的 option 規則已在 `4d52a87` 補上（審查者重現）。delegate 路徑的 option 規則在 `2005274` 之後與 `function_call` 相同；三槽 option<介面> 當 delegate 參數在本 AIN 沒有實例。
     - 立繪最終站位與原版實機截圖逐格對照尚未做。跨側移動的 ReverseLR 已由 `0ab8476` 處理（下列 D5），但開場 150 秒內沒有跨側移動，只有 headless 與臨時注入驗證。
   - **已完成：左側角色翻轉**（`4a82758`）。
   - **已完成：字型缺字**（`05d2441`，逐字 fallback 到 HanaMinA，前進量不變）。
   - **已完成：字距**（`05d2441` 修法 A；`9f81bd9` 修法 B 與側審查 D1–D4，研究見 `research/gui-visual/spacing-fix.md`）。GBK 字元規則且無 .fnl 時照原版 `0x69c7a0` 的字格排版：e = max(ceil 太さ, ceil 縁取り)（各自不超過字級）、字寬依首位元組、前進量 = 字寬 + 2e + 字距；`TextSurfaceManager.GetFontWidth` 回傳同一個字格（`0x69fb30`），backlog 量到的寬度與繪字一致（24 px）；`PE_SetFont`／`PE_SetMessageWindowTextFont` 保存太さ；缺字 fallback 只在這個組態啟用。SJIS 與有 .fnl 的遊戲不變（`text-metrics` 的 SJIS 輸出與 `05d2441` 之前逐行相同）。後續：
     - backlog、DungeonSelector、成就通知的折行與裁切沒有在畫面上確認（測試腳本走不到）。
     - 太さ只算進字格，字形粗細仍依 `weight`；行高與字形 y 位置、行寬含最後一個字距（spacing.md R4）沒有改；`bold_weight` 不會被 PE_Save 存下。
     - `9f81bd9` 尚未經獨立反駁者審查。
   - **已完成：側審查 D5–D7**（`0ab8476`，研究見 `research/gui-visual/reverse-inherit.md`）：翻轉原本只作用在 CG 類元件自己的方框內，TEXT／FLAT 與子元件不理會（`AdvStand@Move` 跨側的 ReverseLR、戰鬥的 `PlayerViewPartsLayer@Reverse` 無效，D5）；surface area 加翻轉會錯位（D6）；`parts-reverse` 沒測繪製與點擊判定（D7）。原版 `0x535260` 複製父元件的累積參數，`0x4e6d80` 把旗標 XOR 進累積狀態並乘上 `S(±1)·T(-origin)·S·R·T(pos)`，所以翻轉以錨點為軸，父元件翻轉時子元件位置也鏡像。修正把旗標移到 `parts_params`（local／global），所有型別共用 `parts_anchor_transform`／`parts_box_transform`，點擊判定與 `Parts_GetPartsUpperLeftPos` 跟著翻轉。新模式 `reverse-inherit` 在 `b5d8b9e` 上 3/3 失敗、修正後全過（RI3 以真 bytecode 跑 `AdvStand@Move` 跨側與 `Motion::EndAll`）；49 模式兩種組態通過；兩次 150 秒 GUI MSG 88、assertion 0、堆疊溢位 0。後續：
     - 開場 150 秒內沒有跨側移動（追蹤 40 次 `SetComponentReverseLR` 都打在剛建立的立繪影像上）；珀爾諾換到左側是新的「■立繪（左左）」，不是 Move。rect 根的翻轉以臨時注入驗證：立繪以錨點精確鏡像，朝向與原版翻轉的立繪相同。真正的跨側移動、戰鬥 `PlayerViewPartsLayer@Reverse` 與 TEXT 翻轉都沒有 GUI 畫面驗證。
     - 放在左側的立繪再移到右側會保持鏡像（葉 1 XOR rect 0），依 AIN 推定是原版行為，沒有原版畫面佐證。
     - 父元件的倍率與旋轉仍不作用在子元件位置上（既有簡化）；翻轉旗標不寫進 parts 存檔；尚未經獨立反駁者審查。
   - 字距審查（`9f81bd9`，兩位審查者皆 ship）留下的低嚴重度項目：新欄位 `text_style.bold_weight` 沒寫進 parts 存檔（`iarray_write_text_style`），PE_Save→PE_Load 後有太さ 的樣式每字少 2 px，量字寬與繪字再度不一致；修法是把存檔版本升到 4 並寫出 bold_weight（不要用 weight/1000 反推）。另有兩個走不到的次要差異：同一文字元件改字級時字級快取過期、`Parts_SetPartsFontBoldWeight` 在 SetFont 之後設定的順序。
   - **原版逐句比對**（`research/gui-visual/visual-compare.md`，Wine 原版對照開場 130 句）：對白場景的立繪、背景、名牌、色彩一致。
   - **已完成：據點畫面缺件與 `SetButtonEnable`**（`6d39915`，研究見 `research/gui-visual/base-ui.md`）。第一個失敗點是 v14 `Parts_SetParentPartsNumber` 延後掛上，`activity::detail::Load` 讀檔後走訪不到子元件，使用者元件一個也沒建立；原版 `0x58f060` 立即掛上。其後依序修正：pactex 依原版 `部件タイプ` 名稱表（`0x4eda70`／`0x5b8f90`）設型別、UC 名稱與 `數據`，低階元件的數字（24）與 ＣＧ判定（27）狀態；`SetComponentType` 對低階元件只改狀態（`0x535e20`）；`GetActivityParts`、`Get/SetUserComponentData`；`Array.Add` 兩槽（`0x644455`）；`MainEXFile.Col` 回 list 元素數（`0x4b00e0`）；`編輯上表示`；父元件倍率作用在子元件位置與文字；字型數字；`SetButtonEnable`／`IsButtonEnable`（`0x590b70`／`0x590ba0`，停用時顯示 `／無効` CG）。新模式 `base-ui` 在 `22339c1` 上 5/5 失敗、修正後全過；50 模式兩種組態通過；150／220 秒 GUI MSG 88、assertion 0、堆疊溢位 0。以點擊序列關閉教學後按「下一步」，遊戲進入階段選擇「今天要選哪件事做呢？」。後續（依影響排序，詳見 base-ui.md §7）：
     - **已解決：場景物件不釋放**（`6582e38`，base-ui.md §11.1）：原版 delegate 對物件是弱參照（`0x652210` 只登記反向清單、物件釋放時 `0x681f70` 刪掉項目），xsystem4 v14 的強參照造成循環。修正後教學圖層與底圖會釋放、階段選擇後的 `AddController` 不再出錯。controller ID 與堆疊位置仍未分開（原版 `0x53d3a0` 插在作用中者之後、`0x53d500` 移除後由下一層作用），已測路徑都是後進先出。
     - **輸入只給作用中的 controller**（`0x53e690` 只對作用中者傳輸入旗標）：仍未實作。場景會釋放後已不再受上一項阻擋，可以接著做；實作後要重跑教學、對話框與鑑賞模式的點擊情境。
     - **跨 controller 的繪製順序**：原版教學框內看得到 SceneAzito 的模糊背景，xsystem4 依 controller 排序，SceneAzito 在教學底圖之下；是否依 z 全域排序未驗證。
     - Motion 結束不套終值（MoveParent 停在 0.975 倍、Footer 的 ToDo／Tips 停在交替中途）；alpha clipper 不作用在子元件（教學框外溢出）；構築部件（D6 模糊背景、`FillCircle` type 102、`Create` 應為透明）；低階元件的其他狀態型別（20、22、23 等；25、26 已由 `fb28975` 補上）；`SetNumeralFont` 等仍是 stub；新欄位不寫進 parts 存檔。
   - **已完成：據點審查的修正**（`fb28975`，研究見 `research/gui-visual/base-ui.md` §10）。審查（fix-first）的中 2 項：停用按鈕（`SetButtonEnable` false）仍送出點擊，Day 1 點灰色的「成员」會進 MEMBER，原版沒有反應（Wine `deep/d035`、`d036`；AIN 的點擊 lambda 與 `SceneHome@Exit` 都不看 Enable），改為 v14 派送吃掉這次點擊（不送 MouseClick、不送全畫面點擊）；v14 派送以目前狀態判定，而按下時 ＣＧ判定部件已切到只有空 CG 的按下狀態，人材狀態頁的「返回」按不動，改用預設狀態的判定區。低 3 項：頁首與據點環節橫幅右偏 73 px 的根因是 `Array.At`／`First`／`Last` 對兩槽元素（0x10003）只推一槽，AIN 以兩槽接收（`X_MOV 4 2; X_ASSIGN 2`），`AnimateText@AdjustPos` 的 `totalWidth` 被堆疊錯位吃掉；一併修正 `First(pred)`、`EraseAll`、`Concat`、`Reverse`、`Insert` 的步幅；pactex 的 矩形部件（25，四角外框大小的不繪製矩形）與 構築部件（26，空步驟建構築狀態，有步驟的保留舊 CG 後備）回報型別，鑑賞模式與 StandView 的 nonnull 斷言消失；流暢度審查的低項：`vm_exit`、VM 錯誤、`sys_error` handler 與 `main` 結尾等待背景截圖。新模式 `base-ui-review`（BR1–BR7，含真 bytecode `Motion::PartsParamCollection@0`、`AnimateText@AdjustPos`）在 `1e56cd6` 上 7/7 失敗、修正後全過；52 模式兩種組態通過；150 秒 GUI MSG 88、assertion 0、堆疊溢位 0；自動點擊下停用的成员／商店不反應、人材→卡片→返回回到一覽、鑑賞模式不再斷言、橫幅文字中心 639.0（原版 639.5）。後續：
     - **回到人材一覽後再按「返回」沒有反應**：STATUS 返回一覽後 `PE_BeginInput` 不再被呼叫，點擊進不了派送；修正前（審查者 r3）相同，每幀有 `PE_SetPartsRotateY`（未實作）呼叫。原版 `d028` 可返回據點。`6582e38` 之後仍然如此，YesNo 對話框取消後 STATUS 的「返回」也一樣。
     - **三槽 option 回傳被截成兩槽**：`function_return` 以 `ain_return_slots_type` 把 `option<T>` 一律當兩槽，`option<wrap<iwrap<T>>>`（原版 `0x653420` 為 3 槽）的回傳只留最上面兩槽；影響 `PlayerAction@InnerAction::get`、`BattleSkillCollection@Find`、`SceneBattle@GetAvailableBattleSkill` 等 13 個函式（多在戰鬥）。修法：回傳槽數照 `ss_type_slot_count`，`retvals` 緩衝放寬到 3 槽以上，並補 fixture（`SceneParentStack@Get` 可當真 bytecode 案例）。
     - motion 改用各自的 Time 後，開場到據點環節橫幅約 78→67 秒；與原版時間軸沒有對照。關閉教學後的 0.975 倍已由 `6582e38` 解決（教學圖層會釋放）。
     - 原版擋掉停用按鈕點擊的位置、低階 widget 用哪個狀態判定點擊、矩形四角與 `矩形模式` 的語義都未逐指令確認；矩形部件有了判定區後對懸停外觀的影響未驗證。
   - **已完成：第二輪審查的修正**（`6582e38`，研究見 `research/gui-visual/base-ui.md` §11）。第二輪審查（`22339c1`..`ce5c75a` 整體與 `fb28975`）fix-first：高 1、中 1、低 2。高（按下新遊戲後標題按鈕仍可點）與低（標題→鑑賞模式→返回 VM_ERROR）的根因相同：v14 delegate 對目標物件加參照（`c3b5ff0`），場景把自己的方法交給按鈕事件就形成循環，`SceneTitle`／`SceneTutorial`／`SceneOmake` 與 `SceneContext` 從不解構，圖層從不 `EraseLayer`。原版 `DG_NEW_FROM_METHOD`（`0x66bec0` → `0x6522c0` → `0x652210`）只把 delegate 登記到物件的反向清單、只對 lambda 的環境加參照，物件釋放時 `0x681f70` → `0x652970` 刪掉目標是它的項目；照此實作（`src/page.c` 的對照表，ResumeLoad 後重建）。場景會釋放後依序補上：解構子內放掉的物件立即解構（原版 C++ 參照計數；修正前第二次進鑑賞模式頁首與按鈕消失）、`RemoveController`／`ReleaseActivity` 寫出 delegate index 清單（`0x53d500`／`0x55e400`，`DeletedEvent` 因此會觸發）、刪除把 `<vtable>` 當元件號碼的 CParts 釋放特例、空的 on-cursor ＣＧ狀態不讓元件在游標下消失（推定）。中（標題構圖約 3 秒後被打亂）：原版 `A_REF` 對陣列一律複製（`0x6794d0` → `0x67ffb0`），xsystem4 v14 共用頁，`GetReverse<IRectParts&>` 就地反轉 `TitleCharacterView.m_uiCharacters`；改為複製數字、字串與通用元素陣列（元素共用，與原版 wrap／介面元素相同），struct 值與巢狀陣列仍共用。低（派送略過系統元件）：`InputDisabler` 洩漏是略過的起因，改為與懸停相同的規則，可點擊的收到點擊、只擋游標的讓點擊變成全畫面點擊（推定）。新模式 `title-review`（TR1–TR7）在 `ce5c75a` 上 7/7 失敗、修正後全過；53 模式兩種組態通過；150 秒 GUI MSG 88（對白與 `fb28975` 相同）、assertion 0、峰值 RSS 607→506 MB；自動點擊下標題構圖 25 秒內不變（與 Wine `run2` 相同）、新遊戲後點舊按鈕位置沒有反應、鑑賞模式往返兩次後可開新遊戲、據點背景出現且可經階段選擇進入春銷。後續：
     - **A_REF 的其餘複製**：struct 值陣列、巢狀陣列、struct 值本身的 `A_REF`、`X_SET` 的內容複製仍共用頁；要先讓 v14 陣列保留元素型別，才能照 `0x67ffb0` 逐元素處理。
     - **controller 語義**：ID 與位置分開、`AddController(-1)` 的插入位置、移除後由下一層作用、輸入只給作用中者，都還沒做（見上面據點畫面的後續）。
     - **據點環節／春銷環節的橫幅文字留在畫面上**：色帶離開後文字仍在；`fb28975` 已如此，只是被沒有釋放的教學底圖蓋住。推定 `PhaseBar@FadeIn(false)` 移動的 `Clipper` 沒有帶走或裁切 `AnimateText` 的字元件（未驗證）。
     - **YesNo 對話框取消後 STATUS 的「返回」沒有反應**：與一覽的「返回」相同，`PE_BeginInput` 沒有再被呼叫。
     - 推定項：空懸停狀態的規則、只擋游標的元件改送全畫面點擊、`is_lambda` 對應原版 +0x54；物件為 -1 的 lambda 仍以 local page 當物件；29 個介面實作的 vtable 偏移大於 1，通用兩槽陣列拆除時會 unref 偏移槽（既有風險）。據點背景沒有模糊、YesNo 對話框後方黑底都與原版不同或未對照。
   - **已完成：第三輪審查的修正**（`80db27d`，研究見 `research/gui-visual/base-ui.md` §12）。第三輪審查（`6d39915`..`7cb6ba3` 整體與 `6582e38`）兩份都是 fix-first：高 1、中 1、低 2。高：v14 pactex loader 不讀 `オン指針透過`，`pass_cursor` 全是 0，`6582e38` 讓擋游標的元件吃掉點擊後，原版標成穿透的 407 個裝飾（標題角色、人材卡的文字與數值、環節卡的角色小圖、STATUS 研修面板）擋住後方按鈕。原版解析器 `0x553650`（區塊在元件 +0x84）把 `點擊許可`、`オン指針透過`、`鼠標指針ピクセル判定` 以 `== 1` 讀到 +0x1a4／+0x1a5／+0x1ad；輸入更新 `0x546890` 以 `0x545e10` 與判斷式 `0x546e20`（可點擊，或滑動中，或不穿透游標）找一個元素，同時當懸停與按下的目標。修正：loader 讀 `オン指針透過`，新增 `v14_input_target`，v14 懸停只有這個元件，點擊派送也用它。中：`delete_struct` 對超過 40 萬指令的解構子永久列入黑名單（v14 fork 為從未啟用的 `vm_call` 指令上限留下的保險），`6582e38` 的巢狀解構讓指令數包含巢狀解構子，`CActivityWrap@1` 在第一次長拆除後被永久略過；刪除，只留 `CParts3DLayerManager`。低（人材 → 返回 → 下一步 → `Executer.jaf:55`）：根因同中項，離開人材一覽時 `SceneWorkerList` 的 wrap 花 487,655 指令被列入黑名單，第二個 `SceneHome` 的 wrap 不執行 `Release`，Footer 留在 `CUserComponentManager`；原版在 `SceneHome` 解構時由 `CActivityWrap@1` → `Release` → `AFL_Activity_Release` 移除。新模式 `third-review`（RV1–RV5）在 `7cb6ba3` 上 5/5 失敗、修正後全過；54 模式兩種組態通過；格點比對（ce5c75a 規則對本組）在標題、鑑賞模式、據點、人材一覽、STATUS、環節選擇都是 0 點不同；人材卡 (70,350) 進 STATUS、標題色帶 (300,340)／(1100,620) 到鑑賞模式／退出遊戲、環節卡 (350,420) 選到春銷；人材 → 返回 → 下一步與春銷 Start 都沒有黑名單與 Executer 斷言；150 秒 GUI MSG 88（對白不變）。後續：
     - **卡死與斷言路徑**（都不是 `6582e38`／`80db27d` 引入，`ce5c75a` 相同，`22339c1` 走不到；前三條本組已重跑確認）：
       1. 系統選單：據點點選單鈕 (1228,588) 開啟、返回 (1180,678) 關閉後，之後的點擊都沒有進入派送（`PE_BeginInput` 沒有再被呼叫），據點完全無法操作。
       2. 成就：標題點成就 (152,240)、返回 (1180,678) 回到標題後，點成就、新遊戲、畫面中央都沒有進入派送，無法開始新遊戲（鑑賞模式往返正常）。
       3. 春銷：進入春銷後按 Start (1180,675)，每次都出現 `DecisionTimerView.jaf:19: (nonnull) m_act . GetHGauge("Gauge")` 斷言，`gui-run.sh` 就此停止；推定是 §10.4 第 7 項沒有建立的橫ゲージ部件型別（未驗證）。
       4. 審查者另記（兩版相同，本組未重跑）：標題 → 配置在 `PE_AddController` 出錯；標題 → 讀取 → 返回之後輸入失效；系統選單 → 劇情回顧在 `PE_AddController` 出錯。
       - 第 1、2 條與「人材一覽的返回」「YesNo 對話框取消後的返回」推定是同一類問題：子畫面返回後 `PE_BeginInput` 不再被呼叫（未驗證）。建議先追 AIN 在子畫面關閉後重新進入輸入迴圈的路徑。
     - **`點擊許可` 沒有讀**：原版同一層也把它讀到 +0x1a4。pactex 中為 1 的約 50 個元件（ClickTarget、InputGuard、ClickGuard、縮圖的 Target、Left／Right、DragRange 等）目前要等 AIN 設 Clickable，否則只擋游標（全畫面點擊）；讀入的影響要另外評估。
     - **懸停訊息**：v14 的 `parts_msg_push` 直接返回，MouseEnter／Leave／On 從未送進 AIN；原版懸停人材卡時卡片變黃（Wine `deep/d024`），xsystem4 不變。
     - 擋游標的元件收到按下時原版送出什麼仍未追到（沿用「全畫面點擊」的推定）；判斷式的滑動條件沒有實作。
   - **已完成：開場 LOGO 的光澤變成黃色光條**（2026-09-30，研究見 `research/gui-visual/logo-gloss.md`）：中文版 pactex 的 `描畫フィルタ`／`加算色` 是 GBK 鍵，loader 只認 SJIS；濾鏡也不傳給子元件。loader 加 GBK 鍵，CG 路徑沿用最近祖先的濾鏡（只在 v14；證據只有 v14 的原版畫面，舊引擎維持各自的濾鏡，第四輪審查）。LOGO 與 Wine 原版逐幀一致（光澤只在深色部分、白底不留光條）；標題背景三層的濾鏡同時讀入，配色與原版一致。新模式 `logo-gloss`。未處理：原版光澤邊緣的柔和漸層、回合結束／戰鬥背景／`加算色` 6 個元件未逐畫面比對。
   - 其餘下一步依序：訊息視窗系統 UI（NEXT 指示、AUTO／回看鈕、左下鈕位置、逐字顯示）→ 據點轉場與背景 blur → 已讀字色（需有已讀紀錄的存檔）→ 字距 1 px／行距 1.5 px（以實機截圖為準）。
   - **已完成：流暢度**（`44964f9`，研究見 `research/gui-visual/pacing.md`）。原因三個：沒有限速（邏輯迴圈 460–480 Hz、每幀最多呈現三次）、`SystemService.UpdateView` 用自己的時鐘再推進一次元件時間（1.67–1.81 倍）、測試模式截圖在主執行緒壓 PNG。修正照原版：`0x4676f0` 的限速算式（毫秒計時、16.666666 ms、保留截斷的小數）與 `0x4c5450` 的 Sleep(50)／限速／Sleep(1)／呈現，Sleep 做成「到期後的第一個 1 ms 刻度醒來」（原版 `timeBeginPeriod(1)`，macOS 的 sleep 常晚醒數 ms），`OverFrameRateSleep`、`SleepByInactiveWindow`、略過已讀時十幀畫一幀都跟遊戲設定；元件時間只在 `UpdateComponent` 推進；`ChipmunkSpriteEngine.Update`／`TRANS_Update` 畫、`UpdateView` 呈現，`system.Peek` 不再呈現；截圖讀回後在背景執行緒寫檔；訊息視窗文字在繪製時才排版。新模式 `frame-pacing` 在 `f489d23` 上 5/5 失敗、修正後全過；51 模式兩種組態通過；同條件前後各兩輪 150 秒 GUI：一般遊玩 321–324 次／秒（浮動）→ 58.7，AIN 時間倍率 0.74→0.97，元件時間倍率 1.67–1.70→0.97；測試模式 >50 ms 間隔 40–42→2–3、最長 241–253→72–75 ms；MSG 88、assertion 0、堆疊溢位 0，約 80 秒進入據點並顯示底列。後續：
     - **fps 是 58.7 不是 60**：原版算式在 1 ms 刻度的 Sleep 下每幀固定 17 個刻度（保留的小數是加到下一幀，精確 Sleep 反而是 16 ms）。原版在 Windows 上的實際 fps 沒有量過；若要剛好 60，只能偏離原版算式（例如期限式排程），需要使用者決定。
     - **立繪 DCF 重複解碼**（換句時 >50 ms 長幀的推定主因）：libsys4 `dcf_extract` 每次都重解底圖（約 21–25 ms），150 秒內 200 次解碼只有 80 張不同。底圖快取要改 libsys4（submodule，需要同意）；xsystem4 端可做「名稱→解碼後 cg」的 LRU（每張約 11.5 MB，注意記憶體），或背景解碼（會改變 `SetPartsCG` 的同步語義）。
     - **`SetWindowSetting` type 2**：AIN 是「全螢幕失焦時最小化」，xsystem4 當成 `WAIT_VSYNC`；SDL 2.32 的 macOS vsync 在螢幕休眠時會卡住（診斷：一次 swap 阻塞 159 秒）。審查者靜態確認 type 2 唯一的呼叫路徑 `AFL_Config_SetMinimizeByFullScreenInactive` 在 AIN 內沒有呼叫者，推定遊戲不會打開 vsync（原本「設定畫面切換會開 vsync」的說法沒有證據）；對應仍依 AIN 函式名推定，要反組譯原版 `SetWindowSetting` 確認後再改。
     - **結束時等待截圖**：`sys_exit` 是 `_exit`，`atexit` 的等待不會執行（審查指出）；`fb28975` 已在 `vm_exit`、VM 錯誤、`sys_error` handler 與 `main` 結尾等待。`gui-run.sh` 到時限送 SIGTERM 時仍會遺失正在寫的那一張。
     - **`PE_Update` 的零時間後備**：passedTime 為 0 時改用「距上次後備」的實際時間，可能重複計時；限速後量測中 0 次觸發，ADV 略過時仍會觸發。原版沒有這個後備，移除前要確認沒有場景依賴它。
     - `heap_grow` 一次觸碰全部新 slot（約 21 ms，偶發）；事件處理偶發 23.8 ms；貼圖上傳。
     - ADV 略過（`View_Update` 不呼叫 `UpdateView`）時沿用舊的呈現路徑，不限速；原版此時不畫也不呈現。影片、跳過已讀（Ctrl）、設定畫面切換兩個休眠選項、亮螢幕與前景視窗下的表現都未驗證。
   - 翻轉繼承審查（`0ab8476`，ship）留下的低嚴重度項目：父元件被釋放後，孤兒子元件的 `global.reverse_lr/tb` 仍保留舊父鏈的 XOR，整棵子樹持續鏡像；修法是在 `parts_release` 讓子元件脫離時重算 global 翻轉與位置。
5. **記憶體成長**：heap 在 120 秒內長到 1730 萬個 slot。`44964f9` 限速後，150 秒 GUI 的峰值 RSS 從 1.0–1.4 GB 降到 0.5–0.6 GB（推定是邏輯幀少了八成，暫時配置跟著減少），成長本身沒有處理。`9e30c0f` 之後，150 秒 GUI 的峰值 RSS 從 1.76／2.05 GB 降到 1.33／1.38 GB，配置器的「skipped in-use」「free list 耗盡或損壞」警告消失（那是 use-after-free 的下游）。`6582e38` 讓 delegate 不再持有目標物件後，場景物件、`Motion::Executer` 與 `CParts` 都會釋放，150 秒 GUI 的峰值 RSS 從 607 MB（`fb28975`）降到 499–541 MB（三種點擊情境）。`80db27d` 刪除解構子黑名單後，春銷 Start 情境峰值 RSS 548→528 MB（審查者 `7cb6ba3` 對照），150 秒 GUI 537 MB。仍有的成長來源候選：STRUCT／DELEGATE／ARRAY 參數多加的參照；heap slot 數的成長沒有重新量測。
6. **已完成：String 字元規則**（`6400e3c`，libsys4 `8181da6`、`247f544`；研究見 `research/gbk-string-rules/`）：libsys4 加入執行期 GBK 規則（預設 SJIS、舊本體不改），xsystem4 只在舊偵測與嚴格判定都成立時開啟，String 各函式、C_REF／C_ASSIGN、`%D`、iarray 讀取照原版語義。使用者已同意更新 submodule 指標。後續另案：
   - 字型 fallback：名牌「綺□綺□」是 VL Gothic 沒有 U+83C8，HanaMinA 探針已確認；約 11.4% 的對白含缺字。
   - 名牌殘影：已由 `9e30c0f` 解決（名牌 root 被提早釋放，`Hide` 落空）。`Motion::GetCompiled` 解析名牌字串得到的 `<Time>` 是 1000、字串寫 `Time:150` 的差異，推定就是兩槽 `Array.First(pred)` 的堆疊錯位：`Motion::PartsParamCollection@0` 找不到 TimeParam，Time 停在預設 1000。`fb28975` 修正後 fixture BR4 以真 bytecode 得到 150，GUI 追蹤中有 150 ms 的 motion（名牌淡出本身的時間與原版未逐格對照）。
   - 舊 GB18030 偵測會誤判 SJIS 遊戲（`ain_is_gb18030` 仍依舊判準），修正會改變 SJIS 遊戲行為，需使用者決定。
   - `utf2sjis`／`sjis2utf` 轉碼路徑、`Int.ToCharacter`、ReplaceRegex（CN 3 處，仍是 stub）。
   - **已完成：視窗標題亂碼**（2026-09-30，`8502bf5`）：`AliceStart.ini` 的 GameName 是 GBK（`B6 E0 C4 C8 …`），`sjis2utf` 解成「ｶ狷ﾈｶ狷ﾈｷｱﾖﾐｰ?」；改以 GB18030 解碼，標題為「多娜多娜繁中版 - XSystem4」。
   - **已完成：其餘亂碼**（2026-09-30，研究見 `$PORT/reports/mojibake-20260930/`（repo 外）的三份調查）。`src/util.c` 新增 `game_charset_is_gbk()`（`ain_is_gb18030` 且字元規則為 GBK，與字元規則同一個條件）、`game_to_utf8`／`utf8_to_game`（GBK 模式用 GB18030，轉不過或 SJIS 遊戲照舊 `sjis2utf`／`utf2sjis`）、`display_game0/1`（原本直接印原始位元組的地方：GBK 模式轉碼，SJIS 不變）。SJIS 遊戲逐位元組不變，AIN 偵測之前也一律走 SJIS。
     - 存檔根目錄：`config_init` 在 AIN 偵測前算出亂碼名；偵測後（`mkdir_p` 之前）由 `save_dir_for_game_charset` 改為 `<home>/多娜多娜繁中版/SaveData`（與原版 `Documents/AliceSoft/多娜多娜繁中版` 同名）。只有舊目錄時整個改名（`renamex_np` RENAME_EXCL，不覆蓋）；兩者都在時用新的、舊的不動並警告；改名失敗沿用舊目錄；`--save-folder` 與 `.xsys4rc` 的 `save-folder` 不動。
     - 檔名：`unix_path` 在 GBK 模式先解碼再換分隔字元（GBK 尾位元組可以是 0x5C，例如「運」`DF 5C`）；啟動時 `migrate_legacy_save_files` 把存檔資料夾裡檔名等於「恰好一個」AIN 字串舊名的檔案改名（`debug::detail::UpdateDumpData` 的兩個傾印檔；`sjis2utf` 多對一，對到兩個以上字串時不改）。`savedir_path` 不在存取時改名（兩個名稱都在時，刪掉新檔會讓舊檔被搬回來，第四輪審查）。`sjis2utf` 有損，所以都是「重算舊名、看是否存在」，不反推。
     - `FileOperation.GetFileList`／`GetFolderList` 回傳 GB18030 名稱；`TextFile` 在 GBK 模式經 `unix_path` 開檔；`SystemService.GetGameFolderPath` 回傳 GB18030；Option+S 截圖檔名先轉 UTF-8 再 `path_join`；剪貼簿、`InputString`、CALLSYS 的 MsgBox／Error／GetSaveFolderName、除錯器、`vmDialog`、`Gpx2Plus`、hacks 的名稱比對一併改用同一組函式（CN 版多數跑不到，SJIS 不變）。
     - 記錄檔：`display_sjis*`（`--echo-message` 的 MSG、VM_ERROR、stack trace、斷言）改用 `game_to_utf8`；`system.MsgBox`／`Error`、SerializeStruct 系列、`savedata.c`／`MainEXFile` 誤用 `display_utf0` 之處、vm.c 的 v14 診斷（C 堆疊將滿那一則除外）改用 `display_game0/1`。
     - 搬遷規則（`move_legacy`，根目錄、SaveFolder 子資料夾、啟動改名共用）：只有舊的時改名、不覆蓋；改名失敗後重新檢查兩邊（另一個行程可能已搬走，`renamex_np` 回 ENOENT），不會回傳已不存在的舊路徑；兩邊都在時用新的並警告；Windows 跳過（窄字元 API 對不上 `mkdir_p` 建的資料夾）。
     - 審查（兩位，fix-first 高 1）：兩個實例同時啟動時第二個會沿用已被搬走的舊路徑（已修，同上）；「兩邊都在用新的」是刻意取捨（對抗驗證駁回中項）；訊號處理函式裡改回印原始名稱；`MainEXFile.AddEX` 的 SJIS 記錄照舊印完整路徑；其餘記錄（SerializeStruct 的 why、page／heap 診斷、pactex、ADVEngine、SealEngine、String regex、savedata 型別名）補上 `display_game0/1/2`。Windows 上 SJIS 遊戲的 `savedata.c`／`MainEXFile` 記錄改成原始 SJIS（原本誤用 `display_utf0`）。
     - 新模式 `mojibake`（MJ1–MJ5）：轉碼三種組態、根目錄四種情境、`savedir_path` 解碼且不改名、啟動改名（含近似名稱、含路徑分隔字元、兩個字串共用舊名的反例）、GetFileList＋TextFile；在 `36cb68b` 上 5/5 失敗、修正後全過。
     - 仍未處理：自訂人材（`SaveData/User/*.txt`）本身不能用，不是亂碼問題：`system.GetSaveFolderName` 回傳相對的 `SaveData`（原版推定為存檔資料夾的絕對路徑），CGManager 不認 `<save>`，`parts.c` 的 `savedir_path("/User/…")` 被當絕對路徑。`<memory>` 開頭的傾印應是記憶體存檔，xsystem4 寫成實體檔。`HTTPDownloader.SJISToUTF8`／`UTF8ToSJIS` 未實作。`display_utf*`（Windows 路徑）未動。

7. 其他延後項目：
   - `Array.First` 的 predicate 版逐實體 slot 走訪：兩槽元素已由 `fb28975` 改為逐元素；單槽且 predicate 形狀特殊的路徑仍是舊迴圈。
   - 兩槽介面陣列的 `Insert`：已由 `fb28975` 修正。
   - `Array.Realloc` 縮小時不釋放。
   - `Array.Duplicate` 只在編輯器路徑。
   - `HashMap.Any/Empty/Free` 在 CN 執行不到。
   - PartsEngine 的 `AddChild`、`InsertChild`、`RemoveChild`、`ClearChild`。
   - DeletedEvent 的 23 個殘留 slot。
   - 非 ASCII 字元在 regex `\w`、`\d` 的分類。

## 參考資料

- [research/](research/)：兩輪調查的報告，已去除個人路徑。
  - `hll-overload-scan.md`：全庫同名多宣告掃描。
  - `batch1-*.md`：Array 各組的原版語義與反駁紀錄。
  - `batch2-investigation.txt`：Math、Sort、String、檔案原型、HashMap、GetStructPageList 的調查、反駁與待解問題。
  - `save-persistence/`：serialize_struct v9 格式、原版位址、反駁處理、原版存檔相容與兩次 GUI 結果。
  - `gbk-string-rules/`：GBK 字元規則的原版位址、反駁處理、修正前後對照與 GUI 結果。
  - `upstream-gap-2026-09-26.md`、`distill-review-2026-09-26.md`：與上游的差距及程式碼蒸餾審查。
- [../2026-09-26/](../2026-09-26/STATUS.md)：前一輪由 Codex 完成的研究與修正。
