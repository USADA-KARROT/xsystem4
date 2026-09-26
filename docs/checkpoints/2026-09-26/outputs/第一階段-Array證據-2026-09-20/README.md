> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 第一階段證據與測試工具

本資料包保存 Numof／Count／Find 的修補依據與驗證。所有重跑命令在原任務根目錄執行；需既有 stage2 source、遊戲 AIN、libsys4 靜態庫與 Homebrew libffi。歸檔檔案保留原工作布局與本機路徑，搬到別台電腦需重新提供這些輸入。

- [本階段報告](../第一階段-Array介面修正-2026-09-20.md)
- [增量修補](../第一階段-Array介面修正-2026-09-20.patch)
- [原版行為契約](native-contract.md)與[native 機器碼摘錄](native-evidence.asm.txt)
- [獨立 query 回歸說明](array-query-review.md)與[結果](array-query-result.json)
- [完整 FFI 整合結果](ffi-results.json)
- [完整引擎編譯結果／source manifest](build-results.json)
- [patch 套用與反向驗證](patch-check.json)

```sh
python3 work/bridge-stage1-20260920/check_array_queries.py
python3 work/bridge-stage1-20260920/check_ffi_queries.py
python3 work/bridge-stage1-20260920/check_builds.py
```

query fixture 抽取當前 production 實作、讀實際 AIN 宣告並用 libffi 呼叫。FFI fixture 直接包含完整 production ffi.c，使用其靜態 linker、CIF 和 hll_call；兩者的 VM callback body 都是受控替身，不能聲稱執行了真實 observer／Join／timer 腳本。ASan/UBSan 有啟用、LeakSanitizer 關閉。未啟動遊戲。

baseline/ 保留修改前兩個 source 檔供缺陷重現。manifest-sha256.json 記錄每個附件的大小與 hash（不含自身）。不含完整 EXE、AIN、遊戲資源、存檔或編譯二進位。
