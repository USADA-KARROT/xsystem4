# 驗證環境（2026-09-28 起的權威版本）

這個目錄是目前唯一有效的 headless 探針與驗證腳本。`../array-overload/` 與 `../first-overload/` 內的 `.c`、`.inc` 是當時的快照，已過時，只作為歷史證據。

所有輸出都寫到 repo 外的 `XS4_WORK`，repo 本身不會被弄髒。

## 需要的東西

- macOS（Apple Silicon 已驗證）、Command Line Tools 的 clang。
- Homebrew：`meson ninja pkg-config libffi sdl2 freetype libpng jpeg-turbo webp cglm libsndfile ffmpeg glew bison flex`。
- 已初始化的 submodule：`git submodule update --init`（libsys4 固定於 `8c93946`，不要改）。
- 自行合法取得的遊戲**工作副本**，內含 `dohnadohna.ain`（繁中版，sha256 `beefa667…8947`）。絕不可指向原始安裝目錄或母片。

## 變數

| 變數 | 必填 | 說明 |
|---|---|---|
| `XS4_GAME` | 是 | 遊戲工作副本目錄 |
| `XS4_MASTER_GAME` | 建議 | 原始母片目錄；設定後，若 `XS4_GAME` 與它相同，所有腳本拒絕執行 |
| `XS4_WORK` | 否 | 輸出目錄，預設 `~/xsystem4-work`，不可在 repo 內 |
| `XS4_SRC` | 否 | 要測的 checkout，預設為本 repo |

## 指令

```bash
export XS4_GAME=/path/to/game-workcopy XS4_MASTER_GAME=/path/to/original
H=docs/checkpoints/2026-09-28/harness
bash $H/setup.sh                        # 第一次：meson 建兩棵樹（optimized 給 GUI、ASan 給探針）並連結探針
bash $H/verify-step.sh <tag>            # 重建並跑全部 39 個模式；最後一行 VERDICT PASS/FAIL，exit code 同義
bash $H/before-check.sh <rev> <mode>..  # 用 <rev> 的 src/include 跑指定模式（證明修正前會失敗），結束自動還原
bash $H/gui-run.sh <name> [秒數]         # 無人值守 GUI：新遊戲、按住 Return、每 1.2 秒點畫面中央、每 2 秒存 framebuffer PNG
```

- `verify-step.sh` 的輸出在 `$XS4_WORK/logs/verify/<tag>/`，每個模式一個檔案，另有 `summary.txt`。
- `deleted-event` 預期 exit 87：23 個殘留 slot 是既有問題，功能檢查本身通過。其他模式預期 exit 0 且沒有 sanitizer 診斷。
- `before-check.sh` 要求 `src/`、`include/` 沒有未提交修改。它會暫時改寫 checkout 的這兩個目錄，被中斷時也會還原。
- `gui-run.sh` 遇到 assertion、ASan、VM error、堆疊溢位、日誌超過 300 MB 或時間到就停止。結束時印出對白行數（`MSG` 行）、堆疊溢位次數與 framebuffer 張數。在執行目錄建立名為 `STOP` 的檔案可手動停止。
- 存讀檔驗證用的 GUI 變數：`RUN_TRACE_SAVE=1` 讓引擎對 SerializeStruct 系列每次呼叫印一行 `SAVE ...`（`XSYS4_TRACE_SAVE`，上限 200 行）；`RUN_SAVE_SEED=<目錄>` 先把該目錄的檔案複製到本次的 `saves/` 再啟動，種子本身不會被寫入，位於 `XS4_SRC` 或 `XS4_MASTER_GAME` 內時拒絕執行。`run.json` 另記錄 `save_seed` 與開始／結束時每個存檔的大小、sha256、mtime（`saves_manifest_start`／`saves_manifest_end`）。
- 本機若沒有「螢幕錄製」權限，無法擷取單一視窗；請以 framebuffer PNG 作為畫面證據。截圖與存檔都不要提交到 repo。

## 探針結構

- `probe/runtime_probe.c`：把引擎的 `vm.c` 加上探針鉤子後編入，連結正式的引擎目標檔；`main` 依第二個參數分派模式。
- `probe/build_probe.py`：從 ASan 樹的 `compile_commands.json` 取編譯參數，產生 `runtime-probe-asan`。它不會重編 `Array.c` 等目標檔，所以腳本一律先跑 `ninja`。
- `probe/*_fixture.inc`：各組測試。以真 AIN 的函式宣告與 lambda 呼叫 `hll_call(libno, fno, arg3)`，並在需要時用 `fork` 隔離預期中的崩潰。
- `probe/activity_text_fixture.inc`：自造 EX tree 經正式 pactex loader 建立具名文字元件，再透過真 AIN 宣告與 ffi 查詢。混排三種狀態並插入無關 branch，檢查文字／CG 型別、主文字與 ruby 字型隔離、未知型別保留，以及明確 `SetComponentType` 覆寫。文字為空且無 CG 資源，不需要 GL。
- 此模式的靜態入口由 `build_probe.py` 在 repo 外複製 `parts.c`／`pe_v14_activity.c` 並追加測試 wrapper；僅初始化 parts table/controller 並呼叫正式 static loader，不更改正式邏輯。正式引擎不含測試 API。
- `probe/dialogue_model_fixture.inc`：使用真 AIN／ffi 檢查 typed struct 訊息陣列反覆 Free／Clear 後的 EmplaceBack／At#1 契約、空頁查詢、外部元素所有權、int／string 型別，以及 null／v13 保留行為。只含合成 ASCII 文字。
- `probe/dialogue_copy_fixture.inc`：使用真 AIN／ffi 驗證 ShallowCopy 的 struct／string 元素身分與所有權，涵蓋來源先刪、副本先刪、At#1 讀取及最終釋放；另保留 empty／null 相容檢查。
- `probe/save_persist_fixture.inc`：存讀檔持久化。`save-list` 檢查 `Array.SYSTEMONLY_GetStructPageList`（綁定、X_A_INIT 單槽清單、無效元素整批清空、快取頁 metadata、真 bytecode `AFL_GameSave_StructSave` 傳入的清單）；`save-roundtrip` 以真 AIN struct 與真 `AFL_GameSave_Struct*` bytecode 做 v9 格式、就地讀回、巢狀／陣列／option／delegate、損壞檔、原子寫入、選擇器、ffi 重入後的參數釋放與 option<int> 刪除；`save-comment` 檢查存檔註解。存檔寫在 `$XS4_SAVE_TMP` 或 `$TMPDIR` 下的 mkdtemp 資料夾（位於 `XS4_SRC`／`XS4_MASTER_GAME` 內時拒絕），結束即刪除。
- 選用模式（不在 `XS4_MODES`）：`save-roundtrip` 的 R15 與 `save-localgame` 需要 `XS4_SAVE_ORIG_COPY=<原版存檔的複本資料夾>`，讀入原版引擎寫的 `.asd` 複本並重存比對；不可指向原始存檔資料夾本身，也不可提交。`save-seed` 以 `XS4_SAVE_SEED_DIR=<XS4_WORK 內的資料夾>` 為 GUI 第二次執行準備種子（在 Collection 的已讀事件清單依排序插入 `ZZ_PERSIST_PROBE`，並寫出 `<ConfigVoiceMutedByNsfw>=1` 的 AFConfig.asd）。
- `deleted_event_fixture.inc` 放在上一層，因為 `runtime_probe.c` 以 `../deleted_event_fixture.inc` 引用它。

新增一組測試：

1. 在 `probe/` 新增 `xxx_fixture.inc`，並在 `runtime_probe.c` 加上 `#include` 與 `main` 內的模式分派。
2. 把模式名稱加到 `env.sh` 的 `XS4_MODES`；若預期 exit code 不是 0，更新 `xs4_expected_rc`。
3. 要引用只在新版才存在的 C 函式時，用 `dlsym(RTLD_DEFAULT, "名稱")` 取得位址（參考 `cif_fixture.inc`），否則 `before-check.sh` 在舊版會編譯失敗。
4. 先用 `before-check.sh <修正前的 commit> xxx` 確認失敗，再用 `verify-step.sh` 確認全部通過。
