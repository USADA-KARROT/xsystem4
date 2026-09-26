> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 第四階段證據與重跑

2026-09-26。原始遊戲AIN與資源、二進位、物件檔未複製進此資料夾；它們仍在本機work中。所有檔案僅在本機保存，沒有上傳。

## 閱讀順序

1. `gui-comparison.json`：各輪log計數；前兩輪stage3原始log另存於work/gui-stage3-20260924/runs。
2. `gui/stage4-final-optimized/`：移除臨時診斷後的最後GUI；run.json包含binary/source/資源hash，engine.log仍有assert。runner因error_log退出1。
3. `gui/stage4-first-string/`、`stage4-page-write/`：前置破壞證據；診斷主动退出86/87。`stage4-id-trace/`：主動退出88，记录ID=-1。
4. `probe/final-results.json`、`final-*.out/.err`：6項通過，deleted-event exit87仍失敗；`deleted-baseline-*`保存修正前失敗。
5. `deleted-event-results.md`與`worker-followup.md`：測試邊界、残留與下階段入口。

`stage4-fix.patch`套在本階段開始前的第二階段候選上；`pre-stage4-source.patch`和`final-source.patch`以source HEAD為base保存前後累積差異。本機已套用，不要重套。`diagnostics-on-clean-candidate.patch`只用於診斷，未留在最終候選。build metadata中可找到編譯器指令和來源hash。

## 重跑無畫面測試

以下從原workspace根目錄執行；需本機既有source、build、原始遊戲，這不是可單獨移到別台電腦使用的套件。build_probe.py依所在路徑尋找work，請使用work內原檔，不要直接執行此封存副本。

```bash
ninja -C work/stage2/asan-build
python3 work/string-stage4-20260926/probe/build_probe.py asan
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 work/string-stage4-20260926/probe/runtime-probe-asan work/stage2/game/dohnadohna.ain deleted-event
```

將最後一個參數換成 `personality`、`observer`、`reentrancy`、`click`、`timer`、`heap-reuse` 可分別重跑六項。本次deleted-event預期功能檢查通過後仍因23個slots殘留exit87；不能忽略此exitcode視為PASS。

## 重跑GUI

```bash
python3 work/gui-stage3-20260924/run_gui.py stage4-manual-recheck --seconds 90
```

tag必須未存在。需有GUI權限；出現注意事項後按Return，在title按NewGame。此腳本只啟動、記log、限時及遇錯停止，沒有自動點擊。最新候選仍會Personality assert。獨立saves/home在該run資料夾；不要拿原始存檔測覆寫。

`perf_summary.py`只讀既有log；其present速率不是實際FPS。`manifest.json`列出封存檔案SHA256。
