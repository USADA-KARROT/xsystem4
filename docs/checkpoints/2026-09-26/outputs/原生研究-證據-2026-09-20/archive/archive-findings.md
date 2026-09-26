> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 本機研究資產盤點 — 2026-09-20

**本機已足以做有證據的深度研究，主要成本可放在引擎行為，而非重新尋找資源格式。現有資料不能證明原生版本已完整可玩，也不能保證重寫後立即解決等待、換頁與呈現。** 本輪只讀取相關原始碼、日誌、資源索引及少量基準圖片，沒有啟動遊戲、下載工具、修補 EXE 或修改歷史檔案。完整路徑、mtime、檔案大小及小於 50 MB 的選定檔案 SHA-256 見 [inventory.json](inventory.json)；mtime 只代表檔案時間，非產出版本證明。

## 已存在，而且有原始材料可核對

| 資產 | 本輪核對 | 可以直接重用 | 邊界 |
|---|---|---|---|
| AFA/PACT 解析腳本 | `scripts/parse_afa_pactex_workcopy.py` 原始碼＋獨立唯讀重查工作副本 | AFA v2 目錄、offset、pactex 樣本提取 | 195 entries 全為 `.pactex`、195/195 `HEAD`，offset 範圍均合法；不等於 195 項完整內容與引擎語意均正確 |
| HLL ABI 稽核 | 本輪實跑 `scripts/abi-audit.py` | v14 AIN 宣告與 C 回傳簽名檢查 | 11 個 `string F(void)` 宣告、10 個已實作均通過；未涵蓋所有 ABI、參數與未實作項 |
| libsys4 歷史測試 | `logs/tests/wave0-libsys4-{asan-,}testlog.txt` 原始日誌 | instruction widths、hashtable removal 的測試與預期值 | 2026-07-06 一般與 ASan/UBSan 皆 2/2；不是遊戲端到端測試 |
| 原版 Wine 基準 | 141 張 PNG 存在；本輪核視 064、066、070、074、090 | 第一頁中文字、NEXT、TV 場景形態等場景基準 | 舊索引時間敘述有誤，詳下節；沒有同步逐事件輸入日誌，不能單憑截圖證明每次點擊與每次換頁一對一 |
| 候選引擎真實 trace | `logs/runtime/run-20260709-091121.log:61–74` | EndWaitForClick 呼叫鏈，避免重新猜測誰結束等待 | log 顯示 binary git 為 unknown，仍需對現在候選版本重新固定 hash |
| CN/JAST AIN dumps | CN 宣告由 ABI 稽核實讀；JAST 文件與檔案存在 | HLL 宣告、functions/structures/delegates、AIN code、EX 資料表 | JAST README 標示 1.02、CP932，所列原始來源 `~/Downloads/DohnaDohna/` 現不存在；要重新建立 dump→AIN hash 對照 |
| 原始資源 manifest | `reports/original-game-manifest.tsv` 有相對路徑、大小、SHA-256 | 追蹤母片與工作副本版本、識別 CN/JAST 混用 | 本輪未重算所有大型包 hash；目錄名稱不能代替資源來源證明 |

以上相對路徑的基底均為 `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/`；JAST dump 位於 `<USER_HOME>/Claude/projects/dohna-jast-dump/`。

這些實證支持保留既有 parser、格式定義、少量測試及可觀察工具。重建獨立原生核心時，沒有理由先把它們全部推倒再做。

## 本輪發現的證據修正

1. **Wine「第一句停住 >20 秒」的引用範圍不準確。** `reports/visual-parity/wine-baseline-index.md` 聲稱 `wb_064–074` 同一句不變；實看 `wb_064.png` 只有 CG，尚無對白。`wb_066.png`、`wb_070.png`、`wb_074.png` 才可見同一句與 NEXT；其檔案時間從 23:10:47.357959 至 23:11:04.557812，跨度 **17.20 秒**。截圖支持第一句持續存在，不能把尚無文字的 064 算入停頁時間。這些時間是既存檔案 mtime，非新一輪連續錄製測量。
2. **TV 白屏確實可與對白共存。** `wb_090.png` 可見白色電視、下方新聞收尾文字及 NEXT。不能只因白屏就認定影片解碼失敗，也不能只因白屏相似就認定候選場景通過。
3. **114 張 legacy 圖片不能證明舊 fork 完整可玩。** `reports/legacy-progress-audit.md` 的 Q8 已指出圖片呈現行動裝置特徵，與另一索引的 fork 標註矛盾；`frame-mapping.tsv` 記錄 2436×1126。此輪只把它們列為來源未充分確定的視覺參考。
4. **「能渲染到 framebuffer」與「桌面可見」曾混在一起。** `worktrees/xsystem4-cn-on-upstream/PORT-STATUS-2026-07-09.md:29–35` 明確修正過去狀態：內部截圖正確，但實際 SDL 視窗灰色。較早 `STATUS.md` 的「畫面 OK」不能單獨作為成功證據。這是歷史更正，不是聲稱 9 月版本仍必然有同一問題。
5. **等待問題已有具體呼叫者，但根因尚未由這些歷史檔案證明。** 2026-07-09 的原始 trace 顯示 `AFL_Parts_EndWaitForClick` ← `Motion::ExecuterCollection@Join` completion lambda (`fno=36081`) ← Observer。它足以縮小調查，無法單獨區分空 motion list、時間推進或等待區間判定哪一項最終有錯。
6. **不存在需要憑舊文字復刻的 missing-HEAD 問題。** 本輪重查工作副本 AFA 的 195 個 pactex，全有標準 HEAD wrapper；Header、SceneEndingMovie、StaffListView 的首 16 bytes 都是 `484541440c0000004558544601000000`。應以實際版本的 bytes 作準。

## 可重用工具與應先改造的地方

- `scripts/abi-audit.py`、AFA 解析器皆可作為小而明確的離線工具；擴大 ABI 稽核時必須另定輸入輸出簽名與覆蓋統計。
- `scripts/verify-run.sh`、`bisect-probe.sh` 是真實存在的運行輔助，但會清空共用的隔離 home/截圖目錄，並用 `pkill`。新研究應改成每次獨立輸出目錄與明確 PID，保留歷史證據，避免多路測試互相干擾。本輪沒有執行它們。
- 引擎已有 auto-click、截圖及 caller trace 的歷史輸出。建立新研究應先固定 binary/resource hash 與事件格式，再串既有工具，不必再造一套無從對照的自動點擊器。
- `<USER_HOME>/Downloads/dohnadohna_工作檔案/disasm_range.py` 與 `parse_hll0.py` 確實存在，後者為特定 AIN 路徑的 HLL0 探針；前者 opcode 表僅子集且含重複別名，不應直接當完整 v14 disassembler 的權威。它們分析 AIN bytecode，不是原 EXE 機器碼。
- 同資料夾有 `dohnadohna_dump_SCY.exe`（15,474,176 bytes）與 `dohnadohna_final6.exe`（12,273,664 bytes）。只列為歷史衍生 EXE，不冒稱原廠 source 或已完整逆向成果。
- `dohna-cn-dump/ai_analysis/codex_reply_20260409.txt` 是 Wine 字形裁切的 AI 研究回覆。這份回覆是調查線索，不是原版 click/wait 行為的實驗證據。

## 尚缺什麼

| 缺口 | 現有盤點能說到哪裡 | 下一個有效成果 |
|---|---|---|
| 資源版本綁定 | manifest 與 dumps 已存在，CN/JAST 混用在歷史筆記出現 | 為研究使用的 AIN/EX/Pact/EXE 建立同一份可重現 hash 清單 |
| 精確點擊→等待→換頁對照 | 原版截圖＋候選 caller log 皆有；兩者尚未共享輸入/事件時軸 | 同一場景與動作腳本下的原版事件證據與新核心 trace；明定一點一頁、不點停頁的條件 |
| motion/timer/Observer 的契約 | 候選 completion callback 呼叫鏈已固定 | 比對 callback 建立、完成、解除與 time delta，確定最小可重現差異 |
| 可見呈現 | 歷史內部截圖與桌面輸出曾不一致 | 同時間桌面擷取及 framebuffer 的成對 gate |
| 深處遊戲與存讀檔 | 7/9 handoff 將 gameplay loop、save/load/backlog 列未驗；本輪未找到其完成原始證據 | 以實際可操作流程與存讀檔 round-trip 驗證，不能從 API 存在推論已支援 |
| 真正回歸 fixtures | `fixtures/` 與 `reports/visual-parity/diffs/` 都是空目錄 | 從已定位錯誤抽出小型 fixture 和預期事件，不只累積截圖 |
| 原廠 C/C++ source/PDB/IDA/Ghidra 專案 | 本輪限縮檔名搜索沒有找到 `.pdb/.i64/.idb/.gpr` | 若其他磁碟/封存另有材料，再納入；現階段不能假設可直接取回原核心原始碼 |

「未找到」只限定這輪搜索：上述工程、相關 Claude dump，以及 Downloads/Documents/Developer 的相關檔名；不是全磁碟否定；`<USER_HOME>/Developer` 目錄不存在。沒有搜尋無關私人訊息或憑證。

## 查核時的版本錨點

| 儲存庫 | 本輪讀到的 HEAD | commit 日期 |
|---|---|---|
| `.../worktrees/xsystem4-cn-on-upstream` | `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b` | 2026-07-09 12:00:31 +08:00 |
| `.../worktrees/libsys4-cn-on-upstream` | `8c939465910499b4802ec6dd619794ca58ba4708` | 2026-07-06 16:13:34 +08:00 |
| `<USER_HOME>/xsystem4-dev/xsystem4` | `917f1a224915d4caa441f712dd1343f8048472a2` | 2026-07-06 14:04:13 +08:00 |

這些是本輪實際 `git show` 結果。舊 STATUS 的 safe checkpoint `02f061c` 與 wip `d3cd045` 是歷史錨點，不能代替目前 checkout 的 HEAD；原始執行日誌自稱 `binary git: unknown`，因此也不能僅憑目錄位置把某次截圖精確歸到某個 commit。
