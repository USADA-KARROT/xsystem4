# 交接：多娜多娜繁中版 xsystem4 macOS 移植（2026-09-28）

給接手的工程師或代理。先讀完本文與 [STATUS.md](STATUS.md)，再動手。

## 現在的位置

- 分支 `wip/post-checkpoint-2026-07-06`，以 `origin` 最新 commit 為準。libsys4 固定於 `8c93946`。
- 新遊戲可推進開場旁白並進入據點場景。自動點擊 120 秒推進 88 句對白後，停在成就通知的斷言。
- Headless 驗證 33 個模式全部符合預期，0 個 sanitizer 診斷。
- 尚非可玩版：存讀檔沒有持久化、角色對話框不顯示文字、記憶體持續成長。

## 硬規則

1. 原始遊戲安裝目錄是唯一母片，只允許唯讀、列檔、算 hash、複製成工作副本。不可修改、覆蓋、刪除、改名，也不可在裡面產生測試檔、日誌、存檔或快取。所有執行一律使用工作副本，並設定 `XS4_MASTER_GAME` 讓腳本擋下誤用。
2. 原版 EXE 的解殼傾印只能靜態反組譯（例如 capstone），不可執行。它是原版語義的最高權威。
3. 這是公開 repo。不可提交遊戲資產、執行檔、存檔、截圖、完整 AIN 反編譯 dump、個人路徑或帳號資訊。
4. GitHub 是唯一真相，本機目錄隨時可能被清除。每完成一組就推送。
5. 不 force push、不 rebase、不改 submodule 指標。這三件事要先得到使用者同意。
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
6. `gui-run.sh <name> 150` 不得回歸：對白行數不少於 88，不得出現新的堆疊溢位。
7. 一組一個 commit。推送前後都用 `git ls-remote` 與 `gh api repos/USADA-KARROT/xsystem4/branches/wip/post-checkpoint-2026-07-06` 確認遠端 SHA，不要只信 push 的輸出。
8. 更新 [STATUS.md](STATUS.md) 與 README 頂部的狀態行。

## 下一批任務（依優先序）

1. **成就通知斷言**：`SceneAchievementNotify.jaf:12 (nonnull) m_act.GetText("TextAchievement")`。第一個成就解鎖時，activity 依名稱找不到文字元件。先確認 activity 如何建立具名元件，以及查名稱走哪個 HLL 呼叫，再判斷是實作缺漏還是另一個原型錯配。
2. **角色對話框不顯示文字**：日誌裡 `MSG` 有內容，框體與說話者頭像都有畫，但文字沒出現；旁白用的視窗有字。比較兩種視窗的文字元件建立與繪製路徑。
3. **存讀檔持久化**：`Array.SYSTEMONLY_GetStructPageList` 回傳殘值，所以 `system.SerializeStruct` 拿到 NULL，直接回 true 卻什麼都沒寫。成就、設定與共有存檔都因此靜默失效。它必須和 `system.DeserializeStruct` 一起修，否則第二次啟動會讀到檔案卻丟掉資料。原版位置是 Array case 83 `0x644ecd` 跳到 `0x64a2d0`。反駁者證實實機清單只配 1 個 slot，先前提議的算法是錯的。細節見 [第二批調查](research/batch2-investigation.txt) 與 [未合併提案](research/unmerged/)。
4. **記憶體成長**：heap 在 120 秒內長到 1730 萬個 slot，峰值 RSS 約 1.9 GB，配置器偶有「free list 耗盡或損壞」警告。這是既有問題，修正前後數字相同。
5. **String 字元規則**：CN 原版全面使用 GBK 首位元組 0x81..0xFE，xsystem4 的 `Length`、`Find`、`GetPart` 等使用 SJIS 規則，中文會被切錯。影響 `Length` 110 處、`GetPart` 76 處，而且牽涉 libsys4，要先得到使用者同意。
6. 其他延後項目：
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
  - `upstream-gap-2026-09-26.md`、`distill-review-2026-09-26.md`：與上游的差距及程式碼蒸餾審查。
- [../2026-09-26/](../2026-09-26/STATUS.md)：前一輪由 Codex 完成的研究與修正。
