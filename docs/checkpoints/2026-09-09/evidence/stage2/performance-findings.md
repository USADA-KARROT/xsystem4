# 卡頓：已定位的 CPU 瓶頸、錯誤控制流與有限修復

2026-09-09。以下依主執行者的實際遊戲取樣、isolated 存檔與 production code 檢查；不把 sanitizer 的速度當一般版本的 FPS。

## 1. 虛假的遊戲 debug mode 觸發週期性整機狀態保存

`perf/asan-35222-sample.txt` 的三秒取樣中，主執行緒 2,036 個樣本有 1,734（85.2%）在 system.ResumeSave → vm_save_image。工作包含整個 heap 的轉換／序列化、壓縮、MT19937 XOR 編碼與寫檔。這只代表該段取樣，不代表整場遊戲的 CPU 比例。

它不是每一幀都保存，也不只是一個初始化 save：AIN 的 debug::detail::UpdateDumpData（4219）被註冊到 BeginUpdateEvent，預設 storage 間隔 5,000ms、memory 間隔 60 個更新 frame。`system_IsDebugMode` 原本對所有 AIN14 都回 true；主程式因此開啟開發用的週期 dump。程式每次同步序列化整個 heap/call stack/value stack/function names；此引擎也把 `<memory>` 檔名寫入 isolated save folder。

兩份完成的 ASan dump：

| 種類 | 壓縮檔大小 | header 原始 payload |
| --- | ---: | ---: |
| storage dump | 24,614,517 bytes | 223,675,442 bytes |
| memory dump | 25,532,022 bytes | 232,402,446 bytes |

讀取 memory dump 的真實保存內容，確認 config slot198 的三項值為 `1 / 5000 / 60`，且保存的 call stack 是 `ExecuteKeyWait → WaitForClick → View_Update → CallBeginUpdateEvent → debug::UpdateDumpData → save`。這排除僅憑檔名推測來源。

原註解稱「IsDebugMode 不回 true，主更新就不執行」，與本 AIN 相反：main 在 0x49a3e0 無條件 AddUserComponent，0x49a3e6 才檢查 debug；true 分支只加 callback4219，於 0x49a414 回到共同流程。已查所有 48 個 IsDebugMode callsite，主要為開發工具、debug menu／output／dump 等分支。

修復 `system_IsDebugMode` 預設 false，只接受 `XSYS4_GAME_DEBUG=1` 明確啟用。VM debugger 或 sanitizer 不自動啟用遊戲 debug。ResumeSave 保存 API 完整保留，沒有略過玩家存檔或回想狀態保存。

`perf/check_game_debug.py` 使用 production 函式和真 AIN 機器碼檢查：預設／空字串／0／其它值 false、只有1 true、ASan env 不影響、4219 false 分支立即返回、正常 component 註冊在 gate 之前。ASan＋UBSan 全 PASS；結果 `perf/game-debug-result.json`。

## 2. 一般版本的長幀來自重複、無用途的 GC mark

關閉遊戲 debug、未開 VM trace 的 normal O0 版本仍出現嚴重停顿。`normal-perf` 在 t=82,266ms 的 26,571.971ms window 僅有 3 次 present，最長間隔 26,507.902ms，swap 最長卻只有 2.055ms；下一次 present 又隔了 6,910.256ms。`perf/normal-36476-sample.txt` 在這段期間反覆出現 heap_alloc_slot → heap_gc → gc_scan_page，證據指向 CPU collector，不是單純畫面交換等待。

靜態確認的兩個 algorithm 問題：

- 舊 sweep 條件是 `if (!GC_IS_MARKED(i) && 0)`，cycle collection 已被原 WIP 停用，但仍每次昂貴地 mark 整個可達圖；mark 結果沒有其它使用者。
- >=4M heap 且 free<1024 時，每個 allocation 都立即跑完整 GC。較小 heap 的五秒限制記錄在 GC **開始**前；當一次 GC 超過五秒，下次 allocation 立刻又符合觸發門檻，形成重複整圖掃描。

沒有重啟既有不完整的 cycle collector。新增命名常數 `gc_collect_cycles=false`，將 mark 與 cycle sweep 放在同一 gate；這一版單獨變更保留原有 ref<=0 orphan、free-list 重建與 shrink 邏輯。此處的既有 orphan 分支有 `type != 0` 限制，而 VM_PAGE=0；不能描述為「所有 page orphan 都能回收」。壓力 GC 統一要求上次**完成後**五秒，且至少再經過 `max(1024, heap_size/8)` 次 allocation；首次符合壓力條件仍執行。這讓沒有回收到足夠空間的 collector 不會在下一個 allocation 再掃一次，並保留 free-list 耗盡後的既有配置流程。

`perf/check_gc_policy.py` 直接抽取新舊 production heap_gc 和 allocator。對同一 heap graph 比較每個 slot 的 ref/type/resource/seq、free-list 和 scan-limit，結果完全相同；兩個 ref<=0 string orphan 照常回收，原本保留的 cycle 也維持原語意，而 mark traversal 歸零。排程 fixture 重現 >=4M 的 100 次 allocation 在舊版引發100次GC，新版只1次；另外涵蓋慢GC、completion cooldown／allocation雙門檻、SDL uint32時間wrap、小heap、free門檻、inhibit、耗盡fallback。ASan＋UBSan 全 PASS，結果 `perf/gc-policy-result.json`。這是 **free-list 修復前的獨立 GC policy 驗證**；它證明該 snapshot 的相同回收語意，不能代替後續 allocator 修復後的整合校驗，也不能據此宣稱所有記憶體问题已解決。

## 3. 更快的版本暴露 Array.IsExist 錯誤重載

GC 修復後 `normal-gc-perf` 未抵達首個 ADV 訊息。三秒 sample 的 2,420 / 2,420 主執行緒樣本位於 `Array_IsExist → vm_call_nopop`；O2 手動進入新遊戲後也有 2,397 / 2,397 樣本落在同一條路徑。這一輪的停滯不是 sample 中的 GC。

actual AIN 的 SetShortcut（31722）於 `0x675556` 呼叫 **值搜尋** IsExist，enum=4 被現有唯一的 predicate 實作當作 fno4，進入不相干的 `AFL_Parts_AddProcessList → AddProcessList(374) → AddConstructProcess(373)`。實際函式4需要3個參數，錯誤 callback wrapper 最多只推2個。這個因果同時符合 AIN 宣告、bytecode 與兩輪 heartbeat / CPU sample。

已依 AIN 的 HLL_PARAM74 / HLL_FUNC95 宣告分流值與 predicate。值搜尋不執行 callback；predicate 按 logical stride 保留第二槽 metadata。另只在 `vm_call_nopop` 入口補上 IFACE / IFACE_WRAP 第一槽的 retain，與 callback 返回的 variable_fini 釋放對稱。OPTION 的 primitive payload / heap ownership 尚未釐清，最終來源沒有加入 OPTION case。詳見 `array-isexist-fix.md`；production fixture `validation/check_array_isexist.py` 使用實際 AIN、libffi、production Array / VM argument copy / variable_fini，重現錯 f4 和 IFACE 早釋放，驗證新版本多次 callback 的 stack / ownership / metadata 全部通過 ASan＋UBSan，明確把 OPTION 排除在已解範圍外。

## 4. 正常 page 釋放漏回 free-list 的既有缺陷

獨立審查找到 VM_PAGE=0 與 `heap_free_slot` 的 `ref==0 && type==0` guard 衝突：正常 heap_unref 先把 ref 減至0，再送入 heap_free_slot；guard 因而把正常 page 的釋放誤認成 double free，未放回 free-list，直到後續 GC 重建才補救。這能解釋部分 slot 高速增長及 GC 壓力，不能單憑此推定所有 retained heap 都來自同一原因。

此 allocator 修復由另一個 bounded fixture 獨立驗證，但統一版 `normal-final-perf`（PID38590，130秒、20秒單次 click、無 trace／frame capture）停在 logo 流程。雖然 present window 約 122fps，不能視為遊戲進度或效能驗收成功；初始化已有 timer slot 被重用成其它 page 類型的 deep-repair 警告，支持立即重用暴露其它 ownership 問題的假說。主線因此暫時回退 free-list 候選，保留 GC mark/pacing 與 IsExist 修復，執行最小 A/B。free-list 修復應保留為後續 ownership 工作的候選，尚未達正式整合驗收；舊 GC snapshot 測試不能替代這項校驗。

## 仍需追蹤的 heap 膨脹

ASan snapshot 的 heap capacity=2,097,152；非 null serialized entries=1,794,850，其中 struct891,462、local150,345、delegate268,675、array123,401。陣列合計25,305,548個值槽。struct最多的是 CASTimer323,538、CASJoyClick323,412，其次CASVertex71,756與SPartsUpdateData71,159。

這是明顯的大型 retained heap，但僅憑 snapshot 不能判定每個物件都不應存在。這份 snapshot 取自 debug / GC / free-list / IsExist 等修復之前；需先以統一新版校驗，才能量化各項修復對 heap 的影響。尚未證明所有所有權／閉包保留與 cycle 回收問題均已解決。尤其 v14 delegate 環境與其它類型的 mark coverage 尚未完整，不能直接解除 cycle sweep 停用，否則可能把仍使用的頁釋放。

最小後續觀測可直接用已有 call trace 的 FNOS=4219,6205,9196、PER_FNO=16、FROM_MSG=2，量前16次 save wrapper時間；一般模式應沒有 debug4219造成的保存。更長時間的記憶體研究應按 allocation type／閉包來源量增量，再做可達性及ownership修復；不以跳過必要存檔或永久保留所有page代替。

存檔大小在 `perf/resume-file-sizes.json`，解碼統計在 `perf/asan-35222-resume-stats.txt`；解碼程式 `perf/inspect_resume.c` 只讀現有檔案。所有 sanitizer fixture均未啟用LeakSanitizer，不能宣稱已證明無洩漏。
