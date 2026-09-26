# 重建與重跑

本checkpoint的引擎來源就是repository當前程式碼，不需重套outputs的歷史patch。歷史patch多以`484f4bc`或前一階段為基底，不可任意疊加。先閱讀[最新狀態](STATUS.md)。

## 引擎

在macOS安裝專案README所列Meson、Ninja、Clang及相依庫，並初始化submodule：

```bash
git submodule update --init --recursive
meson setup build-checkpoint --buildtype=debugoptimized -Ddebugger=disabled -Dopengles=disabled
ninja -C build-checkpoint
```

ASan/UBSan改用獨立build目錄、`--buildtype=debug -Db_sanitize=address,undefined`。這些步驟需可用的SDK／相依庫；既有測試在macOS arm64及Homebrew完成。編譯器不同可能產生不同binary hash。

## 第四階段無畫面fixture

從repo根目錄建立隔離布局，以保留封存工具的相對路徑假設：

```bash
XSYS4_REPRO_ROOT="$(mktemp -d)"
cp -R docs/checkpoints/2026-09-26/work "$XSYS4_REPRO_ROOT/work"
ln -s "$PWD" "$XSYS4_REPRO_ROOT/work/stage2/source"
meson setup "$XSYS4_REPRO_ROOT/work/stage2/asan-build" "$PWD" --buildtype=debug -Db_sanitize=address,undefined -Ddebugger=disabled -Dopengles=disabled
ninja -C "$XSYS4_REPRO_ROOT/work/stage2/asan-build"
python3 "$XSYS4_REPRO_ROOT/work/string-stage4-20260926/probe/build_probe.py" asan
```

自行提供同版本合法AIN；參數路徑用引號包住。例如：

```bash
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$XSYS4_REPRO_ROOT/work/string-stage4-20260926/probe/runtime-probe-asan" "/path/to/dohnadohna.ain" deleted-event
```

`deleted-event`目前應在三次add與字串重用功能檢查後，因23個live slots殘留而exit87；**這仍是失敗**。分別換成`personality`、`observer`、`reentrancy`、`click`、`timer`、`heap-reuse`重跑其他六項。此工具會由當前vm.c生成instrumented-vm.inc，再連結當前engine objects；不是用手寫callback取代原AIN結果。CParts UI建構與handler派送不在DeletedEvent fixture範圍。

AIN SHA256須對照run.json：`beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947`。不同遊戲版本的函式編號、struct欄位可能不同，不能沿用此fixture並聲稱相同驗證。

## GUI

在相同隔離布局中另外建立`work/stage2/optimized-build`，並自行準備完整遊戲副本為`work/stage2/game`（包括適用的AliceStart.ini）。封存`native.ini`中去識別的路徑須按實際資料位置設定，不能直接拷回原遊戲。執行：

```bash
python3 "$XSYS4_REPRO_ROOT/work/gui-stage3-20260924/run_gui.py" new-unique-tag --seconds 90
```

macOS需允許GUI操作；注意事項頁按Return，到title按NewGame。runner不會自動點擊，會把home與saves放在該run內，遇錯或限時停止。當前候選仍會人物ID assertion；勿解讀成驗收已通過。`perf_summary.py`僅摘要既有log，present頻率不是實際FPS。

## 歷史工具的額外限制

Array工具依賴自行產生的`ain_code.txt`及baseline/Array.c；公開版個人路径被替換為`<USER_HOME>`，需先設定DUMP路徑。部分FFI測試使用`work/stage1/wip-asan-build`內libsys4而非stage2，需要重建該歷史基底或明確修改連結位置並重新記錄身份。

研究報告／API地圖生成器也保留在work/research-20260920；部分依賴本機商業原檔與技術dump，未隨repo分發。outputs為交付快照、work為工作快照，兩者可能含不同fixture版本；以各自build/source hash判讀，不應混為同一輪。其他歷史build_probe腳本可能自動ninja或依賴當時source baseline；不能把最新來源回跑結果冒充歷史結果。

公開log經過路徑去識別化，因此內嵌舊manifest仍識別本機原始證據；請使用publication-manifest.json核對公開副本。發布時逐byte核對引擎來源並驗證附件hash，沒有為這次Git保存另外重跑遊戲。
