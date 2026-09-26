> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# GUI 第三階段：現有效能與停止工具審查

2026-09-24，唯讀審查；未啟動遊戲、未 attach process、未改 production。已交付同目錄 `perf_summary.py`，只解析既有紀錄。root 負責新 runner、實際 GUI 與停止控制。

## 最少侵入的短測

使用當前 **optimized** binary，記錄 binary／source hashes、isolated save folder、視窗尺寸與操作時間；開 `XSYS4_STAGE2_PERF=1`，第一輪不要開遊戲 debug、opcode trace、週期 PNG capture。原 `scripts/run_engine.py` 有 `--perf --no-frame-capture --seconds N`，預設卻會啟用每兩秒一張、最多40張的 framebuffer PNG。截圖發生在 swap 前，會污染 present interval；必要的視覺證據以少量人工時間點記錄，註明它覆蓋的視窗。

初輪以「能否到達指定 GUI 狀態／MSG、能否操作」及 PERF 並列，不把持續 present 視為劇情推進。若需定位 callback，再以同場景短 rerun 開 bounded call trace；若觀察長卡住，root 可另決定短 CPU sample，該時段不併入乾淨基準。本子任務不擴寫或執行外部 PID 採樣。

## XSYS4_STAGE2_PERF 的精確意義

來源：`work/stage2/source/src/video.c:427–496`，呼叫點在 `gfx_swap` 的 `SDL_GL_SwapWindow` 前後。counter 用 `SDL_GetPerformanceCounter/Frequency`；log 的 t_ms 另用 SDL ticks。

| 欄位 | 實際意義 |
|---|---|
| `t_ms` | 報告輸出時的 `SDL_GetTicks64()`，不是 Unix time，也不等於 subprocess 啟動後精確時間 |
| `window_ms` | 上次報告基準至本次已完成 swap 的 wall time；至少5秒，卡住後可能遠大於5秒 |
| `presents` | window內完成的 swap 間隔數；第一個完成的 swap 只建立基準，不计數 |
| `samples` | 供 p95 使用的間隔／swap樣本數，最多前4096個；不是 reservoir sampling |
| `avg_fps` | `presents * 1000 / window_ms`，即完成 present 的平均頻率；可能重複同一畫面 |
| `p95_interval_ms` | 相鄰 **swap完成時點** 間隔的 nearest-rank p95：排序後第 ceil(0.95*n) 個 |
| `max_interval_ms` | 完整 window 的最大 swap完成間隔，含之前 CPU工作、等待、render、capture與該次 swap |
| `avg_swap_ms` | 完整 window 的平均 `SDL_GL_SwapWindow` wall time |
| `p95_swap_ms` | 前最多4096個 swap wall-time樣本的 nearest-rank p95 |
| `max_swap_ms` | 完整 window 的最大 swap wall time |
| `window_flags` | **報告當下**的 SDL flags；不是整個 window 的狀態。0x4 shown、0x40 minimized、0x200 input focus、0x400 mouse focus |
| `vsync` | 報告當下 `SDL_GL_GetSwapInterval()` 的結果；此 logger 不設定 swap interval |

啟用條件是 env非空且不等於字串 `0`。只有完成 swap 才呼叫報告 helper，**無獨立定時器／退出 flush**。因此啟動到首個 present 未計入；結束前不足5秒的尾窗、最後一次present後永久卡住的時間，都可能未出現在任何 PERF row。缺少新 row 不是量到了0 FPS；應保留 runner elapsed、最後 PERF tick及「未報告尾段」的限制，不能把两种时钟直接相減當精確卡住秒數。

聚合應用 `sum(presents)*1000/sum(window_ms)`，不能平均每窗FPS。可以列每窗p95範圍，但不能重建全場p95。`samples<presents` 時 p95只看window前段；mean與max仍看完整window。只有max而沒有每frame原始資料，因此只能數「至少含一次 >50ms／>1s 間隔的window」，不能報精確long-frame次數。最大interval和最大swap未必是同一frame，不能把兩個最大值相減成CPU時間。

`max_interval` 很大而 `max_swap` 小，只能支持停頓主要在 swap call 之外；CPU sample／call trace才能再定位具體工作。不能僅據此認定GC、回呼或驅動之一已是根因。

## 回呼與卡住 trace

來源 `vm.c:444–719`、`function_return:1879`。可使用既有 runner 的 `--trace --trace-after N --trace-count 8 --trace-fnos ...`；或對應 env：

- `XSYS4_STAGE2_TRACE=1`：主開關。
- `XSYS4_STAGE2_FNOS`：逗號分隔，最多32個合法 fno。
- `XSYS4_STAGE2_PER_FNO`：每函式1–64次，預設8；取得的是門檻後最先幾次，非均勻抽樣。
- `XSYS4_STAGE2_AFTER_MS`：SDL tick門檻，0–3600000。
- `XSYS4_STAGE2_FROM_MSG`：直到實際 MSG index **>=** 此值才 armed；若未進 ADV，trace永遠不會啟動。
- `XSYS4_STAGE2_WATCH_FNO`：該函式活躍local page的 assign-before／assign-after／unref-last，最多32筆。這是 ownership診斷，不是效能counter。

observer 路徑可先限 `20752,36081,20748,9197`；保存疑點另看 `4219,6205,9196`，避免每次同時開一大串。`STAGE2 return elapsed=N` 是從 frame prepare 到 return記錄點的整數毫秒，含 nested calls／等待／該次trace成本，並且**在本frame的 `unref_call_frame` 清理之前記錄**。不能當 exclusive CPU 或包括全部析構尾成本。trace enter可用 `(f,hit,depth)` 對return；只進未回可能仍在等待、被中止或走特殊unwind，不能單獨證明永久死循環。

`XSYS4_TRACE_FNO` 是另一個逐opcode工具：硬門檻>42000ms、最多6000筆，侵入較大，初輪效能基準不建議使用。既有 heartbeat由指令數位元遮罩觸發，並非等wall-time採樣；source的「每50M」註解不是精確固定週期，不能用 heartbeat頻率當FPS或callback耗時。

## 停止：舊 runner 的 env 目前無效

`scripts/run_engine.py` 會設 `XSYS4_STOP_ON_GAME_ERROR=1`，但**當前 source/include 沒有consumer**。`src/hll/system.c:431–443` 的 `system_Error` 只記warning並回傳字串，前10筆後會抑制日志。因此不能把這個env視為停止保證，日志筆數也不等於Error呼叫總數。

舊 runner 的有效stop是時間上限／KeyboardInterrupt：`proc.terminate()`，等8秒，未退出才 `proc.kill()`。只針對它建立的child，沒有同名全域 kill。中止通常不經過正常VM teardown，不保證最後PERF flush或liveheap清理；要記錄 `stopped_by_runner` 與exit code，而非把這種結束當正常離開。

現行 `_vm_error` 會輸出VM stack，進 `dbg_repl` 後 `sys_exit(1)`；與 `system.Error`不同。system4只安裝SIGSEGV／SIGBUS診斷及幾乎no-op的SIGTRAP handler；沒有提供「以signal要求heap統計」接口。若root需要收到system.Error即停止，应由runner監視新增日志後停止該child。`VM_CALL_TIMEOUT` 也要單独記錄：它是runtime已做強制unwind的證據，不能只當效能較慢。

## RSS／live heap 的現有邊界

當前 `video.c`／`vm.c`／`heap.c` 沒有週期RSS或精確live-slot counter輸出。root回報此shell sandbox的 `ps` 為 Operation not permitted；本階段不新增外部PID採樣。runner若能取得特定child的結束peak RSS，只報「整個child生命期的peak」，不能編成RSS曲線。若用 `RUSAGE_CHILDREN`，需區分多子程序聚合，优先特定child的wait usage；RSS原始單位須按平台保留與確認。

`heap_size` 是配置槽**容量**；此版本另保留48GiB虛擬地址區間，既非resident memory，也非live AIN物件量。稀疏 `heap_gc: ...free=...scan=.../...` 只在回收部分發生且被節流時記錄，沒有穩定時間序列。`scan` 是該輪掃描範圍，不是live count。`heap_size - heap_free_count - 2` 在free-list一致且穩定snapshot時可作占用估計，但不能從現有零散ログ提供精確持續值；真live owner需同一時刻計數slots>=2且ref>0。`DEBUG_HEAP` 只在特定正常退出路徑掃描／列印，非當前無侵入GUI遙測。

第二階段fixture的live0已驗證那些受控生命週期，不能外推為新GUI無洩漏。本次GUI若未加診斷快照，liveheap就應標「未量測」。若之后确实需要，另以可辨識的診斷binary在安全事件點統計，與純PERF run分開；不要為取得snapshot重新啟用昂貴遊戲debug保存。

## 現有與新增解析工具

既有 `work/stage2/perf/parse_perf.py RUN_DIR --output FILE` 已正確計算加權FPS、每窗p95範圍、flags、按MSG行序分phase。MSG日志沒有精確timestamp，所以其phase只能視為日志順序分界，不可據此精確切割跨場景PERF window。`scripts/inspect_run.py TAG` 另產生函式耗時及sanitizer摘要，但沒有統計system.Error，也沒有自動驗證遊戲進度。

新增本目錄 `perf_summary.py`：

```sh
python3 work/gui-stage3-20260924/perf_summary.py RUN_DIR --output SUMMARY.json
```

需 `RUN_DIR/engine.log`；`run.json`可選。輸出PERF加權摘要、長間隔window數、MSG索引、bounded函式elapsed、未配對enter、system.Error／timeout／sanitizer日志行數。程式不attach、不launch、不stop，也不自行採樣。它另可讀既有 `--samples FILE.jsonl`（`elapsed_s,rss_kib,cpu_pct`），目前未啟用此輸入，沒有藉此聲稱RSS已量測。

用舊 `runs/user-preview-20260909-120028` 做只讀交叉檢查，與原 `perf-windows.json` 一致：10 windows、8467 presents、55.129秒已報告覆蓋、加權153.585 presents/s、最大間隔8644.006ms、最大swap28.590ms、2個window至少含一次>1s間隔、MSG0。結果存 `historical-preview-summary.json`。這份歷史run使用不同binary，不是2026-09-24修正後基準，也不能以高present頻率宣稱它已進入ADV。

`performance-findings.md` 的既有CPU樣本曾分別指向debug ResumeSave、重複GC mark、錯誤Array.IsExist overload；都是各自snapshot結論。它們可引導本輪檢查，不能預設新binary的任何停頓還是同一根因。
