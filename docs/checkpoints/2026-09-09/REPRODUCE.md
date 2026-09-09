# 重建與接續測試

這是2026-09-09的macOS ARM64工程checkpoint，使用者最新試玩仍卡住。先讀 [STATUS](STATUS.md)。目前不是可分發的獨立遊戲.app；遊戲資料須自行提供，build仍依賴本機函式庫。

## 原測試身份

- xsystem4基準：`484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b` 加上本次18個來源檔案的修改。
- libsys4固定submodule：`8c939465910499b4802ec6dd619794ca58ba4708`，保持現有pin。
- macOS 26.6.2 / Apple Silicon；Clang 21、SDK 26.5、Meson 1.11.1、Ninja 1.13.2、SDL 2.32.10、libffi 3.5.2。
- O0：debug；O2：debugoptimized；ASan/UBSan：debug + `-Db_sanitize=address,undefined`。debugger與opengles均停用。
- 最新試玩O2 binary SHA256：`50ef51d1cc1b6fde50dde4082128d09db962cd15c9aeaf7767e2323c0ec948b1`。
- AIN SHA256：`beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947`；AliceStart.ini SHA256：`c09633228076f51426f23208fab5448d3766e56665ea27f25c5db5f77add1587`。

## 一般重建

先依根目錄README安裝相依套件。macOS另需Command Line Tools、Homebrew版bison/flex，及Meson可找到的cglm。本次已存在的環境沒有重新安裝套件。以下命令供重建；發布checkpoint時沒有重新跑遊戲，沒有宣稱新的機器已驗證。

```sh
git clone --recurse-submodules --branch wip/post-checkpoint-2026-07-06 https://github.com/USADA-KARROT/xsystem4.git xsystem4-checkpoint
cd xsystem4-checkpoint
PKG_CONFIG_PATH=/opt/homebrew/opt/libffi/lib/pkgconfig meson setup build-checkpoint . \
  --native-file docs/checkpoints/2026-09-09/tooling/archive/native.ini \
  --buildtype debugoptimized --wrap-mode nofallback \
  -Ddebugger=disabled -Dopengles=disabled
ninja -C build-checkpoint -j4
meson test -C build-checkpoint --print-errorlogs
```

`native.ini`是原Apple Silicon環境設定，其他SDK／Homebrew位置需調整。Meson內建測試通過僅代表所列測試，不涵蓋整個遊戲。

## 隔離手動試玩

以下在repo根目錄執行；將第一行改成自己的遊戲資料**副本**路徑。引擎從repo工作目錄讀取fonts/shaders。每次建立新的輸出目錄，正常關閉由玩家操作，沒有自動時限或自動點擊。這些命令尚未作為新腳本重新實機驗收，原始已實測runner在下一節。

```sh
GAME_COPY='/path/to/your/game-copy'
RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/xsystem4-checkpoint.XXXXXX")"
mkdir "$RUN_DIR/home" "$RUN_DIR/saves"
python3 - "$PWD/build-checkpoint/src/xsystem4" "$GAME_COPY" "$RUN_DIR" <<'PY'
import os, pathlib, subprocess, sys
binary, game, run = map(pathlib.Path, sys.argv[1:])
if not binary.is_file() or not (game / 'dohnadohna.ain').is_file():
    raise SystemExit('Missing binary or game-copy/dohnadohna.ain')
env = {k:v for k,v in os.environ.items() if not k.startswith(('XSYS4_', 'XSYSTEM4_'))}
env.update(XSYSTEM4_HOME=str(run/'home'), XSYS4_STAGE2_PERF='1', XSYS4_STOP_ON_GAME_ERROR='1')
print('Log and isolated saves:', run, flush=True)
with (run/'engine.log').open('wb') as log:
    result = subprocess.run([str(binary), '--echo-message', '--save-folder', str(run/'saves'), str(game)],
                            env=env, stdout=log, stderr=subprocess.STDOUT)
raise SystemExit(result.returncode)
PY
```

## 原測試runner與fixtures

[tooling/archive](tooling/archive/)保留原`build.py`、`make_bundle.py`、`run_engine.py`、`inspect_run.py`。`run_engine.py --until-closed`是最後讓使用者自行關閉時增加的選項；此變更沒有重建或改動引擎binary。

原runner預期如下隔離目錄，不能直接在archive目錄執行：

```text
stage2/
  source/                    # 原測試為484f4bc + 未提交來源patch
  game/                      # 外部提供的獨立遊戲副本
  native.ini
  scripts/                   # 將archive四個.py複製到這裡
  optimized-build/           # Meson build，必須由此source建置
  normal-build/
  asan-build/
  apps/                      # make_bundle.py產出
  runs/                      # 每輪獨立home/saves/log
```

建立對應Meson build後，`python3 stage2/scripts/build.py optimized`會建置、保存source patch/manifest並產生本機app包。原最後一次啟動參數：

```sh
python3 stage2/scripts/run_engine.py optimized user-preview-new \
  --until-closed --bundle --perf --no-frame-capture
```

tag不可重複。不加`--until-closed`時runner有時間上限。開啟`--trace`會改變時序與效能；先用正常版重現，再單獨診斷，不把ASan／trace數據與正常版效能混用。

[fixture與生成器](evidence/stage2/validation/)及其他`*-tests`是當時研究快照，部分會以`git show HEAD`讀修補前版本、引用stage1靜態庫、原AIN或去識別化路徑。重跑前必須將舊版讀取基準固定為484f4bc並設定新的資料／函式庫路徑；不能把它們視為直接可跑的CI套件。已記錄的實測结果可直接閱讀，完整原始環境仍保留本機。

## 接續驗收順序

1. 先重現使用者「標題頁卡住」，記錄每次手動輸入時間、SDL座標／焦點、hit target、事件consumer、scene transition與MSG。
2. 發生多秒停頓時抓main-thread堆疊及RSS／heap趨勢，確認是否與既往GC熱點同源；目前只有present長間隔，無法下根因結論。
3. 修復後重跑首頁停等、單擊一頁，並增加多次啟動與連續操作驗收。
4. 再處理VM_PAGE重複釋放／生命週期、長程記憶體與玩家存讀檔，才評估第三目標「初步可玩測試版」。

每輪保存source commit/diff、libsys4 pin、binary hash、選定診斷選項與操作時間。不要在使用者試玩期間自動點擊或以固定時限關閉視窗。
