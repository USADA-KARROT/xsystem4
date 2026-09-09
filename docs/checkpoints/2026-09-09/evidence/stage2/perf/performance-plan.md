# Stage 2 卡頓量測與初步取樣

2026-09-09。有界任務：量測準備＋低量 opt-in counter；未操作 GUI、未修改 VM/存檔或等待語意。

## 目前證據

- 已確認 ASan PID35222 存在後執行 `sample 35222 3 -file work/stage2/perf/asan-35222-sample.txt`；取樣時間 08:27:48.142 +08，run 已運行約4分10秒。此前 `ps` 顯示 CPU99.7%；sample 報告 footprint `2.0G`。
- Main thread 有2036個樣本，1734（85.17%）位於 `system_ResumeSave → vm_save_image → save_rsave_image`。拆成 snapshot 建圖732、serialization/encryption/write749、free snapshot251、close2。這是同一層子樹分類，沒有把祖先/子孫樣本重複相加。
- `Cocoa_GL_SwapWindow` 合計62（3.05%）；`SystemService_UpdateView → SDL_Delay`35（1.72%）。此次 snapshot 的主要 CPU 線索是 save path，不能據此推論正常版全程85%都在存檔，亦不能從sample算出FPS。
- 檔案：`asan-35222-sample.txt` 主執行緒24行、ResumeSave33行、phase35/305/761/1057行；`asan-sample-summary.json`。取樣對應 `../runs/delegate-layout-check/run.json`，為 ASan/UBSan＋STAGE2_TRACE=1。截圖輸出也在該run啟用（最多40張、每2秒一次，取樣時此配額應已用完）；不能把該run當一般效能。
- Meson introspection：目前 **normal-build 與 asan-build 均 buildtype=debug、optimization=0**；normal僅無sanitizer。之後 release/debugoptimized 的結果應單獨列出，不能混在同一數字比較。

## 現有更新/呈現與等待入口

- `src/video.c:405–422` 原本已有 `gfx_get_frame_rate` 的約1秒平均值；StoatSpriteEngine.FPS_Get 轉呼叫它。這沒有 frame interval 分佈或p95，也沒有現成env輸出入口。
- `src/hll/PartsEngine.c:965–988` 的 PE_v14_UpdateComponent 每次 pump/update，但 scene_render＋gfx_swap 節流為約16ms；未達16ms的分支沒有sleep。這只能標為可能的 idle CPU 成本，尚無normal取樣證實它是目前低FPS原因。
- `src/hll/SystemService.c:108–143` 每次更新 motion/components；16ms未到時已有 SDL_Delay(1)。不能宣稱整個 busy loop 都沒有yield。
- `src/hll/SACT2.c:180–189` 的 sact_Update 只在 scene_is_dirty 時 scene_render＋gfx_swap；乾淨場景的 update 呼叫不是呈現幀。
- `src/video.c` 預設 wait_vsync=false，但遊戲可經 SystemService 設定。要看 runtime `SDL_GL_GetSwapInterval`，不猜測實機值。
- `src/vm.c` 的主執行路徑另有定期GC；本輪取樣沒有把它列為主熱點。沒有擴查或修改它。

## 新增低量計數（已完成，僅 video.c）

開啟：`XSYS4_STAGE2_PERF=1`。不需要 STAGE2_TRACE。缺少、空值或`0`時不查高解析timer、不新增I/O；env僅讀一次。

每5秒（若卡死則等待下一次呈現回來時）輸出一行：

`STAGE2_PERF t_ms=... window_ms=... presents=... samples=... avg_fps=... p95_interval_ms=... max_interval_ms=... avg_swap_ms=... p95_swap_ms=... max_swap_ms=... window_flags=... vsync=...`

- 只在 **SDL_GL_SwapWindow 返回後**計數，沒有呈現的update不算。SDL2此函式沒有成功狀態回傳，這是「完成的呈現呼叫」率，不保證每幀內容改變、窗口可見、或等於顯示器掃描刷新率。附window flags以便剔除最小化等狀態。
- frame interval = 相鄰已完成present之間的wall time，會包含VM、存檔、update、render、swap與停頓。swap duration僅括住SDL_GL_SwapWindow，兩者不可混稱GPU render time。
- 第一present只建立baseline；每window mean與max涵蓋全window。p95為nearest-rank、最多4096樣本；若`presents>samples`，p95只代表前4096樣本，log可辨識。正常60Hz五秒約300樣本，不會達cap。
- fixed buffers，不逐幀寫磁碟、不配置每幀記憶體；每5秒sort與一行NOTICE仍有很小量測成本，報告應標「低量PERF開啟」。
- `check_stats.py` 從當前 production 抽出實際helper，ASan+UBSan通過：未開/0沒有timer或I/O、60FPS、p95/outlier、6秒停頓、window reset、4096cap accounting。結果 `stats-result.json`。沒有SDL/GL初始化或GUI。

## 下一轮一般版對照

1. 使用同一份source、INI/1280×720、資產、存檔起點、窗口可見状態；記錄binary SHA256、Meson optimization/sanitizer、run args/env與UI操作時間。
2. 一般版移除 `XSYS4_STAGE2_TRACE`、`XSYS4_TRACE_FNO`、`XSYS4_SCREENSHOT_DIR`，以及本輪不需要的自動點擊/座標override；**用unset而不是TRACE=0**，因部分既有trace只檢查env存在。移除`--echo-message`以避免額外訊息輸出；只開 `XSYS4_STAGE2_PERF=1`。保持一般錯誤日誌。
3. 分場景記錄：標題穩定期、按新遊戲後轉場、ADV文字輸入/動畫、靜止等點擊。每段至少20–30秒；啟動與資產第一次載入另外列出，不用混合平均掩蓋卡頓。每段用5秒windows列 avg_fps、p95/max interval、swap耗時。
4. 同場景一般版再做3秒sample，存normal獨立檔；留前後低量counter視窗以衡量sampler成本。若要取得最乾淨FPS，呈現最終數字時排除sample發生的窗口。這不要求開GUI debugger。
5. 先比較normal/O0與ASan/O0辨識sanitizer/trace影響，再由root隔離build出 optimization=2 的 debugoptimized 一般版重測；現有fullengine unit tests由root统一決定，不因本counter重跑整套。
6. 若normal仍有低FPS且保存棧佔主體，量ResumeSave的次數、每次wall time、heap規模與寫入bytes並與max interval對齊；**先查真正成本，不恢復「沒寫入卻回報成功」的節流**。若swap耗時佔大部分再查vsync/window/driver；若interval很大而swap很小，優先查present前CPU工作。

目前尚無正常版FPS數值；本文件沒有捏造測量結果或先斷言唯一根因。
