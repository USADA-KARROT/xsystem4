# 2026-09-09 開發進度與交接

**最新狀態：使用者於 2026-09-09 親自試玩後回報「卡住」。問題尚未定位，本 checkpoint 不是穩定可玩版，也尚未達到第三階段的初步可玩測試版。** 較早的受控測試曾通過開場兩頁、停等及單擊換頁，但最新試玩再度記錄到長停頓；以下保留兩者，避免用局部成功覆蓋實際反證。本輪依使用者要求先保存來源、報告與證據，暫停繼續除錯。

## 最新親測：目前首先要解決的問題

`user-preview-20260909-120028` 使用同一份 O2 最佳化來源及 binary，於台灣時間 12:00:29–12:01:26 執行 57.765 秒，使用者自行關閉，沒有自動逾時。`exit_code=0` 只表示此次程序正常結束，不代表遊戲流程通過。

- 日誌沒有 `MSG`，後段 heartbeat 仍包含 `SceneTitle@Run`。目前沒有證據能確認這次進入新遊戲或開場對話。
- 兩個完整 PERF 窗口分別記錄 **8,644.006 ms** 與 **5,350.551 ms** 的最長 present 間隔，長停頓再次出現。對應的最大 swap 呼叫時間僅約 5.521 ms、3.467 ms，但本輪沒有卡頓當下的 CPU sample，不能直接將根因歸為 GC、輸入或其他單一模組。
- 尾段仍約每秒 122–124 次 present；這只能證明部分時段持續提交畫面，不能證明按鈕有效或場景向前推進。日誌欄位雖名為 `avg_fps`，實際量測的是 `SDL_GL_SwapWindow` 完成頻率，包含重複畫面，**不是螢幕物理 FPS，也不是遊戲內容更新率**。
- 有重複的 `Failed to load WAV -1` 警告；尚未證明與卡住有因果關係。本輪未記錄使用者每一次輸入，不臆測點了哪個按鈕或確切停住位置。

本次證據見 [preview 日誌](evidence/stage2/runs/user-preview-20260909-120028/engine.log)、[執行身份](evidence/stage2/runs/user-preview-20260909-120028/run.json)與[摘要](evidence/stage2/runs/user-preview-20260909-120028/summary.json)。較早報告保留在 [第二階段歷史報告](reports/stage2.md)，其「第二目標核心驗收通過」是當時的短程觀察；目前整體狀態以本文件為準。

## 四個階段完成度

| 階段 | 本輪狀態 | 尚待補齊 |
|---|---|---|
| 1. 盤點、固定版本、重建與重現 | 已完成 | 保留當時的上游差距快照；未藉此宣稱遊戲相容性完成 |
| 2. 視窗、中文、基本操作與短程效能 | 修補已完成一輪，受控兩頁測試曾通過；最新親測仍卡住，需重新驗收 | 先重現標題頁操作／場景轉換及長停頓，確認成功條件可重複 |
| 3. 初步可玩測試版 | 尚未達成 | 連續操作、返回／重進、記憶體生命週期、玩家存檔與讀檔 |
| 4. 廣泛相容性與穩定性 | 尚未開始完整驗收 | 更多遊戲流程、UI／文字功能、長時穩定性及發行包 |

## 已保存的來源修補

修補基底為 `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b`，即先前最新 WIP。共涉及 **18 個來源／建置檔案**，新增 `src/parts/message_window.c`；未把不相容的舊 `master` 或所有上游變更一次合併進來。

| 修補範圍 | 實作與目的 | 主要檔案 |
|---|---|---|
| CG 與巢狀 FFI | 依 AIN 宣告選擇 CG 名稱回傳 ABI；保存／復原巢狀呼叫的 receiver、參數等上下文 | `src/ffi.c`, `src/hll/PartsEngine.c` |
| v14 事件與滑鼠 | 依實際事件佇列讀取／Pop 順序；處理短按、長按及延後 UP，避免輸入提前消失 | `src/hll/pe_v14_activity.c`, `src/input.c` |
| 按鈕命中與視窗 | 使用 CN pactex 像素旗標及 alpha 命中；缺省 1280×720，避免透明區截走新遊戲點擊 | `src/parts/input.c`, `src/system4.c` |
| 中文訊息視窗 | 保留原文、背景與文字版面；加入 message sidecar 及渲染／生命週期接線 | `include/parts.h`, `src/hll/pe_v14_message.c`, `src/meson.build`, `src/parts/message_window.c`, `src/parts/parts.c`, `src/parts/parts_internal.h`, `src/parts/render.c` |
| 字串與 delegate 生命週期 | AIN ≥11 的 `S_PLUSA/2` 採兩槽 lvalue；delegate 引用參數補 retain；僅對確認的 interface callback 參數保留引用 | `src/vm.c` |
| Array 重載 | `Erase` 依 index/count、predicate、集合差異三種宣告分流；`IsExist` 分辨值與 predicate，避免將 enum 值誤當函式編號，並處理 interface stride | `src/hll/Array.c` |
| 遊戲 debug 模式 | `IsDebugMode` 預設 false，僅 `XSYS4_GAME_DEBUG=1` 開啟，避免高頻完整 VM 傾印；正常存檔 API 保留 | `src/hll/system.c` |
| GC 與效能診斷 | 跳過在既有停用 cycle sweep 下未被使用的 mark；按 GC 完成時間及配置進展控制壓力回收；可選的每五秒 present 統計 | `src/heap.c`, `src/video.c` |

新增文字視窗目前只是基本立即顯字，逐字、ruby、Flat、等待圖示及所有控制碼未全面實作。此輪沒有啟用完整 cycle collector，也沒有證明所有引用生命週期正確。

## 測試結果與可推論的範圍

下列前三個受控驗收與最新 preview 使用同一組最終修補。數值是已保存日誌的實測結果，沒有在發布 checkpoint 時重跑遊戲。

| 測試 | 執行時間 | 已觀察結果 | 限制 |
|---|---:|---|---|
| `normal-acceptance-perf`，O0 | 130.177 秒 | 首頁訊息保持；最大 present 間隔 261.535 ms，首 MSG 後完整窗口最大 33.953 ms | 無連續遊玩；結束有 2 次 `VM_PAGE` 重複釋放警告 |
| `optimized-acceptance`，O2 | 198.960 秒 | 實際新遊戲點擊；首頁保持 43.476 秒，單擊到第二頁後保持 44.333 秒；整輪最大間隔 81.252 ms | 僅兩頁，非全流程；同樣有 2 次 page 警告 |
| `asan-acceptance`，ASan/UBSan | 231.557 秒 | 首頁等待 75.893 秒，單擊第二頁再等 46.334 秒；無 ASan/UBSan error | 同樣有 2 次 page 警告；LeakSanitizer 未啟用，沒有「無洩漏」結論 |
| 最新 `user-preview-20260909-120028`，O2 | 57.765 秒 | 使用者回報卡住；無 MSG；8.644 秒與 5.351 秒長間隔 | 沒有逐次輸入或卡頓 sample，根因未確認 |

較早 `normal-perf` 在 O0、無 sanitizer／VM trace／frame capture 的條件下，量得最長 26,507.902 ms 的 present 間隔。卡頓內抽樣的 2,407 個主執行緒樣本全部落在 `heap_gc`，其中 2,173 個在 `gc_scan_page`。整組 GC／Array 等修補後，受控開場測試改善；**最新親測則說明不能將改善擴張成「卡頓已完全修好」**。新一輪長停頓需重新採樣，不能直接沿用舊樣本診斷。

局部 fixture 已涵蓋真實 AIN／libffi 的 CG ABI、58 項 FFI context、347 項 queue、56 項 input、36 項 pixel、字串 opcode、delegate 引用、Array 重載、GC policy 等；修補範圍的 sanitizer fixture 通過。normal 與 ASan 的 libsys4 內建測試各 2/2 通過。這些結果不替代實機整合驗收。

正常版在約 93 秒時觀察到 RSS 1,155,936 KiB，約 1.10 GiB。正引用 cycle、部分孤兒 payload、page slot 回收及其他 ownership 問題仍未完成；沒有長時間 RSS 曲線。每五秒窗口的 p95 不能平均成全程 p95，缺少後續 present 的尾段也不能填成 0 FPS。

## 已回退的 free-list 候選

候選發現 `VM_PAGE=0` 與 free-slot 的 `type=0` 判斷混用，嘗試用獨立 `VM_FREE` 標記，並涵蓋配置、回收、載入重建及 deferred destructor。局部 ASan/UBSan fixture 在 100,000 次 page 配置／釋放中成功重用 slot、不擴大 heap。

但實機停在 ALICESOFT logo，新增 `CASJoyClick.m_timer` 與 `CASClick.timer` 指向換型／失效 slot 的紀錄。這支持追查 ownership 的方向，尚不能確認漏 retain 的單一入口。候選因此**未套用至目前來源**，保存在 [候選說明](experimental/README.md)與[候選本體](evidence/stage2/freelist-candidate/)供後續比對；不能因為 allocator fixture 通過或約 122 次 present/s 就宣稱成功。其 A/B 回退同時包含 membership、deferred inhibit 及首次 GC 時間差異，不能只歸因立即重用。

候選涉及 `src/heap.c`、`include/vm/heap.h`、`src/resume.c`。目前 production 的 `heap.c` SHA256 為 `f80013691446559208b618b42959641cba7ce8d2a41b95b1c1da319506f4d8e0`；header 不含候選 `VM_FREE`，現行 GC fixture 已在回退後重新通過。

## 下一輪優先順序

1. **先重現最新標題頁／場景轉換問題。** 使用相同 O2 binary、固定 AIN 與全新存檔，記錄焦點、滑鼠 DOWN/UP、實際命中 part、事件讀取／Pop、`SceneTitle` 回傳及下一場景進入時間；以真正進入首句作成功條件。遇到長停頓立即採 CPU sample，同步保留 present 時間，區分邏輯未前進與執行緒長阻塞。
2. 將新取得的 sample 與輸入／場景紀錄對照後，才修改對應路徑；不繼續盲調 GC、renderer 或平均提交率。若原生版與同一 AIN 的預期操作不明，再使用者提供的 CrossOver 版本作對照。
3. 標題→新遊戲→首頁→單擊第二頁能重複後，追查 `VM_PAGE` 警告、timer ownership 及物件數；先證明引用成對，再考慮重新套用 free-list 候選。
4. 做 20–30 次連續翻頁、返回／重進至少三輪，記錄 RSS 與 heap；接著三個存檔點驗收「存檔→推進→讀回→重啟載入」。訊息 sidecar 未直接寫入 parts 存檔格式，必須確認載入後是否由 AIN 正確重建。

完成上述實機驗收，才能將第三階段標為初步可玩測試版。更多 Array API、primitive OPTION callback、backlog、縮圖、標題亂碼及完整文字控制仍需另行驗收。

## 上游與 CrossOver 對照的保存範圍

本輪開工前盤點確認先前最後進度在 2026-07-09 的 `484f4bc`，不是 `master` 的 `917f1a2`。當時與官方快照 `fa09b0059f3ffcc7b9e853f330e832ea75d74502` 相比，xsystem4 缺 47 筆提交，其中包含 merge；libsys4 對 `cbc57ce5642dadbbeda3c4894e1c2da7e15dbbff` 缺 20 筆。這是固定時間點的圖譜差距，不是 47／20 個未完成獨立功能；本 checkpoint 未完成上游整合。詳細作者、日期及變更對照見 [歷史盤點報告](reports/audit.md)。

Rufim 的實驗分支提供 v14 等參考，但同一 CN 腳本的先前短測在 `CActivityWrap` assertion 失敗，未直接採用。使用者提供的 CrossOver 包裝程式已作唯讀盤點；確認 Wine Crossover 23.7.1-1、wrapper 要求 `renderer=gl`，AIN／ini／Version.txt／exe 與測試副本一致，版本檔為 1.01。本輪未啟動它，未獨立驗收幀率、存讀檔或實際 renderer；「正常可玩」為使用者提供的觀察。

## 可重建身份與資料位置

| 項目 | 固定值 |
|---|---|
| 來源基底 | `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b` |
| libsys4 submodule | `8c939465910499b4802ec6dd619794ca58ba4708` |
| 最終來源 patch SHA256 | `05476b72d3e37c039e4f86f3683739cd5072db8afdb95fb0d9adee06745acd9d` |
| 352 檔 source manifest SHA256 | `1d10e1db25de14115dcf1fdd594548e8b0cb0b0fbe168b18b70f8707894743ba` |
| O2 binary SHA256 | `50ef51d1cc1b6fde50dde4082128d09db962cd15c9aeaf7767e2323c0ec948b1` |
| 遊戲 AIN SHA256 | `beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947` |
| 遊戲 ini SHA256 | `c09633228076f51426f23208fab5448d3766e56665ea27f25c5db5f77add1587` |

測試環境為 macOS arm64，Meson／Ninja、Apple Clang、Homebrew SDL2 及 libffi。O0 使用 debug，O2 使用 debugoptimized 保留 symbols，ASan/UBSan 使用獨立建置；debugger 與 OpenGLES 停用。三個最終驗收建置的來源 manifest 一致。重新編譯所得 binary hash 可以因工具鏈不同而改變；上表用來識別本輪已實測版本，不保證 bit-for-bit 重建。

- [reports](reports/)：上游與本機盤點、第一／第二階段歷史報告；以本文件最新狀態覆蓋舊的完成度描述。
- [evidence/stage2](evidence/stage2/)：整理後的 run 身份、效能及正確性結果。任何刪去本機路徑等的衍生證據，不應再宣稱與原始檔 hash 相同；原始 hash 僅作來源識別。
- [tooling/archive](tooling/archive/)：歷史建置／執行／檢查腳本，含最新讓使用者自行關閉的 `--until-closed` 選項。部分 fixture 依賴本輪目錄配置或外部 AIN，使用前須依工具說明設定環境。
- [experimental/README.md](experimental/README.md)：未套用的 free-list 候選及失敗／回退證據索引。
- [REPRODUCE.md](REPRODUCE.md)：重建及以外部遊戲資料重新執行的說明。

本 checkpoint 保存工程進度與交接材料，不附商業遊戲資產、執行檔或玩家存檔。原始專案、原存檔、CrossOver 參考及已驗收的隔離副本保留。
