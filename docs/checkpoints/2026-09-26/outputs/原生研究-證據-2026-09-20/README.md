> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 原生路線研究：證據與重跑工具

日期：2026-09-20。本目錄為本機靜態研究的固定快照，不含完整 EXE、AIN、遊戲資源或玩家存檔。所有程式探針只讀取輸入檔案；不執行遊戲。

- [完整報告](../xsystem4-原版逆向與原生路線研究-2026-09-20.html)
- [可搜尋 API 地圖](../原生研究-API地圖-2026-09-20.html)
- [EXE／原生分派與局部語義](exe/exe-findings.md)
- [AIN／閉包、引用、輸入與 timer 契約](ain/ain-findings.md)
- [本機歷史材料與視覺證據審查](archive/archive-findings.md)
- [現有 Array 綁定缺口獨立審查](archive/binding-review.md)
- [資產身份清單](archive/inventory.json)
- [完整 API 資料](probes/api-map.json)
- [O2 與 ASan/UBSan 驗證](probes/verified/verification.json)

## 主要證據

`exe/` 保留三個 PE 身份、952 格分派表、22 份有界機器碼摘錄及生成工具。可執行區與檔案映射全部驗證；8 個分支局部人工判讀，不代表 952 項 API 都已還原。SCY dump 與現用受保護 EXE 的完整版本一致性未證實。

`ain/` 保留 CN／JAST dump 對照、從原 AIN 解碼的指定函式及探針。JAST 原始 AIN 本輪未重新取得。`probes/` 為全 AIN 結構掃描，包含所有 HLL 宣告與靜態 CALLHLL 位置數，並非執行覆蓋率。

`archive/` 保留現有研究檔案身份、舊測試證據限制與本次原始碼審查。絕對路徑供原電腦追溯；部分舊目錄可能已不存在，相關限制已在子報告標示。

## 重跑 AIN 結構驗證

在本任務根目錄執行；需要本機既有 source 與兩份已建置 libsys4.a。工具不包含這些遊戲／依賴檔案。

```sh
python3 outputs/原生研究-證據-2026-09-20/probes/reproduce_surface.py \
  --source work/stage2/source \
  --lib work/stage2/optimized-build/subprojects/libsys4/libsys4.a \
  --asan-lib work/stage1/wip-asan-build/subprojects/libsys4/libsys4.a \
  --ain work/stage2/game/dohnadohna.ain \
  --out work/research-20260920/recheck
```

本輪已成功執行同一工具，normal／ASan 結果完全相同，所有非 sentinel 函式地址都在指令邊界。ASan/UBSan 驗證範圍限於讀取／解碼；LeakSanitizer 關閉。既有名稱 junk warning 與 sentinel 診斷保存在 verification.json，不能據此宣稱新 VM 或遊戲已通過測試。

EXE 的 `inspect_pe.py` 可用命令列指定來源；其餘 EXE／AIN 工具保留本機來源路徑或特定 hash／位址，重跑前須閱讀程式。若在別台電腦重跑，要改為自己的合法檔案位置，不能將本次 VA 套用到不同 hash 的 EXE。工具會寫分析結果到自身目錄，建議複製到新的工作目錄後執行。

## 檔案完整性與報告驗證

`manifest-sha256.json` 記錄本資料包每個檔案的 bytes 與 SHA-256（不含 manifest 自身）。報告／API 地圖均可離線開啟；沒有外部 JavaScript 或資料請求。報告本地連結、資料統計及互動篩選程式以離線檢查驗證；本輪未有可用瀏覽器連線，未完成真實瀏覽器視覺驗收。

本輪沒有改動現有引擎、遊戲或存檔，也沒有向 GitHub 推送。本資料包代表研究成果，不是可玩版本。
