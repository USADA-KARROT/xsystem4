# 交接：多娜多娜繁中版 xsystem4 macOS 移植（2026-09-28）

給接手的工程師或代理。先讀完本文與 [STATUS.md](STATUS.md)，再動手。

## 現在的位置

- 分支 `wip/post-checkpoint-2026-07-06`，以 `origin` 最新 commit 為準。libsys4 指標為 `247f544`（使用者已同意由 `8c93946` 更新；本機分支 `gbk-rules-20260929`，推送前只存在本機）。
- 成就通知斷言（`2914b40`）、角色對話正文（`1540b85`）與存讀檔持久化（`173ff1d`）已修正。兩次 150 秒 GUI（新存檔、重用存檔）MSG 88、assertion 0、堆疊溢位 0，framebuffer 已確認正文可見；第二次確認設定與 Collection 從檔案讀回。
- Headless 驗證 49 個模式全部符合預期（另以 `XS4_PROBE_GBK=1` 在 GBK 規則下全部重跑通過），0 個 sanitizer 診斷。
- 立繪與名牌不退場的 use-after-free 已修正（`9e30c0f`）：介面參數與參照型 option 參數在呼叫時補上參照，照原版 `0x657430`。到 `6421e6e` 為止已在遠端。
- 字距依原版 GDI 字格修正（`9f81bd9`，已在遠端；側審查 D1–D4，見 `research/gui-visual/spacing-fix.md`）。
- delegate 呼叫的參數複製修正（`2005274`，本機 commit，尚未推送）：一格堆疊對一個參數變數，不再把兩槽參數的 void 伴隨變數當成下一個參數（原版 `0x66dce0`／`0x657430`，見 `research/gui-visual/delegate-args.md`）。
- 翻轉旗標作用在整棵元件樹（`0ab8476`，本機 commit，尚未推送；側審查 D5–D7）：沿父元件鏈 XOR、以錨點為軸鏡像方框與子元件位置（原版 `0x535260` → `0x4e6d80`，見 `research/gui-visual/reverse-inherit.md`）。
- String 字元規則已改照原版的 GBK 規則（`6400e3c`，第 5 項）。
- 據點畫面缺件與 SetButtonEnable（`6d39915`，本機 commit，尚未推送；第 4 項）：父元件立即掛上、pactex 依原版 `部件タイプ` 表設型別等九項（見 `research/gui-visual/base-ui.md`）。底列與「下一步」出現，按下後進入階段選擇；階段選擇之後因教學圖層沒有釋放而停在 `PE_AddController`。
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
     - **`Motion::Executer` 結束後不釋放**：修正後 Executer 正常註冊，但移出集合後停在 ref=2（GUI 追蹤觀察，推定是 delegate 強參照循環）。`parts::detail::CParts` 從未釋放，`ReleaseParts` 也一直沒被呼叫。
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
     - **場景物件不釋放**：教學結束後 `SceneTutorial`（ref 4，全部來自按鈕事件的 delegate page）與它的 `SceneContext` 都沒有解構，`EraseLayer` 不執行，教學圖層與中央不透明黑的教學底圖留下來（關閉教學後據點背景是黑的）。在階段選擇點選項後，下一個 `AddController` 時作用中的 controller 不在最上層，引擎依現有檢查結束。原版 `0x53d3a0` 會插在作用中者之後，但 xsystem4 的 controller 編號兼作 ID；臨時試做插入後遊戲停住，已撤回。要先確認原版 delegate 對物件是否為強參照，並把 controller ID 與堆疊位置分開。這也是記憶體成長（第 5 項）的候選。
     - **輸入只給作用中的 controller**（`0x53e690` 只對作用中者傳輸入旗標）：實作後教學期間點空白處不會穿透到據點按鈕，但要等上一項解決，否則教學結束後據點無法點擊。
     - **跨 controller 的繪製順序**：原版教學框內看得到 SceneAzito 的模糊背景，xsystem4 依 controller 排序，SceneAzito 在教學底圖之下；是否依 z 全域排序未驗證。
     - Motion 結束不套終值（MoveParent 停在 0.975 倍、Footer 的 ToDo／Tips 停在交替中途）；alpha clipper 不作用在子元件（教學框外溢出）；構築部件（D6 模糊背景、`FillCircle` type 102、`Create` 應為透明）；低階元件的其他狀態型別（20、22、23、25、26 等）；`SetNumeralFont` 等仍是 stub；新欄位不寫進 parts 存檔；停用按鈕是否擋點擊未驗證。
   - 其餘下一步依序：訊息視窗系統 UI（NEXT 指示、AUTO／回看鈕、左下鈕位置、逐字顯示）→ 據點轉場與背景 blur → 已讀字色（需有已讀紀錄的存檔）→ 字距 1 px／行距 1.5 px（以實機截圖為準）。
   - **流暢度**（使用者回報「說不出的卡頓」）：`STAGE2_PERF` 顯示幀率 120–520 fps 不穩（`video.c` 的 `wait_vsync` 預設關閉），150 秒內 29 個 5 秒時段有 20 段出現 >50 ms 長幀、前 80 秒常有 180–250 ms 停頓。先查原版的幀率控制，再逐一追長幀的原因（字形產生、貼圖載入、GC、存檔 fsync、測試模式日誌），並分開量測測試模式與一般模式。
   - 翻轉繼承審查（`0ab8476`，ship）留下的低嚴重度項目：父元件被釋放後，孤兒子元件的 `global.reverse_lr/tb` 仍保留舊父鏈的 XOR，整棵子樹持續鏡像；修法是在 `parts_release` 讓子元件脫離時重算 global 翻轉與位置。
5. **記憶體成長**：heap 在 120 秒內長到 1730 萬個 slot。`9e30c0f` 之後，150 秒 GUI 的峰值 RSS 從 1.76／2.05 GB 降到 1.33／1.38 GB，配置器的「skipped in-use」「free list 耗盡或損壞」警告消失（那是 use-after-free 的下游）。仍有的成長來源候選：STRUCT／DELEGATE／ARRAY 參數多加的參照、`Motion::Executer` 與 `CParts` 不釋放、場景物件因按鈕事件的 delegate 循環不解構（`SceneTutorial`，見第 4 項據點畫面的後續）。
6. **已完成：String 字元規則**（`6400e3c`，libsys4 `8181da6`、`247f544`；研究見 `research/gbk-string-rules/`）：libsys4 加入執行期 GBK 規則（預設 SJIS、舊本體不改），xsystem4 只在舊偵測與嚴格判定都成立時開啟，String 各函式、C_REF／C_ASSIGN、`%D`、iarray 讀取照原版語義。使用者已同意更新 submodule 指標。後續另案：
   - 字型 fallback：名牌「綺□綺□」是 VL Gothic 沒有 U+83C8，HanaMinA 探針已確認；約 11.4% 的對白含缺字。
   - 名牌殘影：已由 `9e30c0f` 解決（名牌 root 被提早釋放，`Hide` 落空）。`Motion::GetCompiled` 解析名牌字串得到的 `<Time>` 是 1000、字串寫 `Time:150` 的差異仍未查（未驗證是否影響淡出時間）。
   - 舊 GB18030 偵測會誤判 SJIS 遊戲（`ain_is_gb18030` 仍依舊判準），修正會改變 SJIS 遊戲行為，需使用者決定。
   - `utf2sjis`／`sjis2utf` 轉碼路徑、`Int.ToCharacter`、ReplaceRegex（CN 3 處，仍是 stub）。
   - 推送後，本機另一個 libsys4 worktree 的 `cn-on-upstream` 分支要 fast-forward 到 `247f544`。
7. 其他延後項目：
   - `Array.First` 的 predicate 版逐實體 slot 走訪。
   - 兩槽介面陣列的 `Insert` 沒有保留兩槽。
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
