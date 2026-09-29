# 交接：多娜多娜繁中版 xsystem4 macOS 移植（2026-09-28）

給接手的工程師或代理。先讀完本文與 [STATUS.md](STATUS.md)，再動手。

## 現在的位置

- 分支 `wip/post-checkpoint-2026-07-06`，以 `origin` 最新 commit 為準。libsys4 指標為 `247f544`（使用者已同意由 `8c93946` 更新；本機分支 `gbk-rules-20260929`，推送前只存在本機）。
- 成就通知斷言（`2914b40`）、角色對話正文（`1540b85`）與存讀檔持久化（`173ff1d`）已修正。兩次 150 秒 GUI（新存檔、重用存檔）MSG 88、assertion 0、堆疊溢位 0，framebuffer 已確認正文可見；第二次確認設定與 Collection 從檔案讀回。
- Headless 驗證 47 個模式全部符合預期（另以 `XS4_PROBE_GBK=1` 在 GBK 規則下全部重跑通過），0 個 sanitizer 診斷。
- 立繪與名牌不退場的 use-after-free 已修正（`9e30c0f`）：介面參數與參照型 option 參數在呼叫時補上參照，照原版 `0x657430`。到 `ca2ebff` 為止已在遠端。
- 字距依原版 GDI 字格修正（`9f81bd9`，本機 commit，尚未推送；側審查 D1–D4，見 `research/gui-visual/spacing-fix.md`）。
- String 字元規則已改照原版的 GBK 規則（`6400e3c`，第 5 項）。
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
   - PE_Save／PE_Load 不保存 `2914b40` 的 `component_type_from_state`；ResumeLoad 之後成就通知的 GetText 是否再次失敗尚未驗證（本組兩次 GUI 都沒有觸發 ResumeLoad）。
   - 一般 `NEW` 建立沒有 STRT 建構子的 struct 時，成員仍留在 null；原版 `0x679b30` 會依 `0x656970` 預設初始化。`6b65b12` 只修了讀檔路徑，引擎層要另外評估影響面。
   - DeleteSaveFile 在檔案不存在時原版回 true（`0x5c69f0`）；`init_struct_slot` 的 enum 陣列型別（R2）；A_REF 暫存字串殘留（F13）。
4. **使用者回報的畫面問題**（研究見 `research/gui-visual/`，依建議順序）：
   - **已完成：立繪與名牌的退場、換位、隱藏**（`9e30c0f`，研究見 `research/gui-visual/uaf.md`）。根因是 v14 呼叫的介面參數（`AIN_IFACE`）與有值的參照型 option 參數以借用方式傳入，被呼叫端返回時卻會釋放；`function_call` 與 delegate 路徑沒有加參照，`Motion::Create`／`Motion::Executer@0` 因此把 `AdvStand.m_parent` 的 sprite 與名牌 root 提早釋放。修正照原版 `0x657430` 補上 retain。新模式 `iface-arg` 修正前 6/6 失敗；150 秒 GUI 修正後三次 MSG 88，立繪會退場，名牌不再疊字，`heap_alloc_slot` 警告歸零。後續另案（詳見 uaf.md〈另案〉）：
     - **delegate 呼叫把 void 伴隨槽當參數**（已用臨時探針驗證）：`delegate_call` 的複製迴圈沒有跳過兩槽參數後的伴隨槽，會把堆疊上多讀的一格（例如 delegate page 的 slot 號碼）寫進被呼叫函式的第一個區域變數。返回時該 slot 可能被多減一次參照。要先寫 fixture，再讓迴圈像 `delegate_param_slots` 一樣跳過伴隨槽。
     - **STRUCT／DELEGATE／ARRAY 參數多加一次參照**（headless 驗證一例）：原版不加，呼叫端的 `A_REF` 已交出所有權，每次呼叫多漏一份。拿掉之前要確認沒有借用傳 struct 的路徑。
     - **`Motion::Executer` 結束後不釋放**：修正後 Executer 正常註冊，但移出集合後停在 ref=2（GUI 追蹤觀察，推定是 delegate 強參照循環）。`parts::detail::CParts` 從未釋放，`ReleaseParts` 也一直沒被呼叫。
     - Tutorial 路徑以 null 物件呼叫 `Motion::Create` 等方法（每 150 秒 34 次 -1）。
     - `vm_call_nopop`（HLL 回呼）的 option 規則已在 `4d52a87` 補上（審查者重現）。三槽的 option<介面> 當 delegate 參數時被當成兩槽，仍未驗證。
     - 立繪最終站位與原版實機截圖逐格對照尚未做；跨側移動的 ReverseLR 受下列 D5 影響。
   - **已完成：左側角色翻轉**（`4a82758`）。
   - **已完成：字型缺字**（`05d2441`，逐字 fallback 到 HanaMinA，前進量不變）。
   - **已完成：字距**（`05d2441` 修法 A；`9f81bd9` 修法 B 與側審查 D1–D4，研究見 `research/gui-visual/spacing-fix.md`）。GBK 字元規則且無 .fnl 時照原版 `0x69c7a0` 的字格排版：e = max(ceil 太さ, ceil 縁取り)（各自不超過字級）、字寬依首位元組、前進量 = 字寬 + 2e + 字距；`TextSurfaceManager.GetFontWidth` 回傳同一個字格（`0x69fb30`），backlog 量到的寬度與繪字一致（24 px）；`PE_SetFont`／`PE_SetMessageWindowTextFont` 保存太さ；缺字 fallback 只在這個組態啟用。SJIS 與有 .fnl 的遊戲不變（`text-metrics` 的 SJIS 輸出與 `05d2441` 之前逐行相同）。後續：
     - backlog、DungeonSelector、成就通知的折行與裁切沒有在畫面上確認（測試腳本走不到）。
     - 太さ只算進字格，字形粗細仍依 `weight`；行高與字形 y 位置、行寬含最後一個字距（spacing.md R4）沒有改；`bold_weight` 不會被 PE_Save 存下。
     - `9f81bd9` 尚未經獨立反駁者審查。
   - **側審查 D5–D7【低】**（詳見 uaf.md〈側審查〉）：翻轉不作用在 TEXT／FLAT 與子元件（`AdvStand@Move` 跨側的 ReverseLR、戰鬥的 `PlayerViewPartsLayer@Reverse` 無效，屬功能缺口，D5）；surface area 加翻轉會錯位（D6）；`parts-reverse` 沒測繪製與點擊判定（D7）。
5. **記憶體成長**：heap 在 120 秒內長到 1730 萬個 slot。`9e30c0f` 之後，150 秒 GUI 的峰值 RSS 從 1.76／2.05 GB 降到 1.33／1.38 GB，配置器的「skipped in-use」「free list 耗盡或損壞」警告消失（那是 use-after-free 的下游）。仍有的成長來源候選：STRUCT／DELEGATE／ARRAY 參數多加的參照、`Motion::Executer` 與 `CParts` 不釋放（見第 4 項）。
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
