# Free-list candidate：局部通過，整合出現 logo 停滯，暫不交付

此候選已從 stage2 主線回退，保留在 `work/stage2/freelist-candidate/`，交第三目標追查 ownership 後重新驗收。不能把 allocator fixture 通過、低 CPU 或每秒 122 次 present 說成遊戲已正常／效能修好。

## 已確認與局部結果

原 `VM_PAGE=0` 與 free-slot `type=0` 混用；正常 page 在 heap_unref 將 ref 設 0 後被 heap_free_slot 當成重複釋放，slot 沒有即刻回到 free-list。候選以 VM_FREE 獨立標記、涵蓋 init/grow/GC/tail/shrink/load 重建，並保護 deferred destructor drain，修復這個問題；沒有啟用 cycle collector。

候選的 production fixture（ASan+UBSan）通過：舊 guard 重現、100,000 次 page 配置／delete_page／重用不增加 heap、strings／exit_unref／重複 free 無 list 自環、GC 尾端 tag、save-load 重建、待處理 child 與 destructor 中耗盡仍可 grow、inhibit 平衡。syntax-only 也通過。

證據完整保留在 candidate/perf：`check_freelist.py`、`freelist_fixture.c`、`freelist-result.json`、`check_gc_policy.py`、`gc_policy_fixture.c`、`gc-policy-result.json`。候選 heap SHA256：`b72d72d814de19d9fa3eab559260d251c4470a02566e719f39363e7c052c3d8b`。這些驗證沒有涵蓋 WIP 所有 AIN/VM ownership 與 borrowed-reference 生命週期。

## 實機反證

root 的 normal-final-perf（PID 38590，無 sanitizer）在 `screenshots/normal-final-observe.png` 顯示 ALICESOFT logo 與黃色斜形，未進入標題或 MSG。`runs/normal-final-perf/engine.log` 持續至 PERF t=125621ms 仍無 MSG；125秒內定期 present、約122次/秒，只能證明主執行緒仍更新／送出畫面。

可區辨的證據：

- engine.log:9–11，在主遊戲啟動前新出現 `CASJoyClick.m_timer` slot137 的 page_type=3、`CASClick.timer` slot106 的 page_type=-1，觸發既有 deep repair。對照 normal-perf、normal-gc-perf，兩者都沒有這兩條 repair。這與 slot 更早回收／被改為其他型別相符，但尚不能單凭此判定哪個 store/copy 漏 retain。
- engine.log:35–45 的 heartbeat 為 `Motion::Executer@IsAlive::get(27001)` → `EraseEndTask` lambda(36086) → EraseEndTask(27034) → Join observer(36081)，base 是 SceneLogo@Run(31798)。後續多次 heartbeat 仍在同 scene，PartsAsyncLoadQueue／Parts update 持續執行。
- 這支持「logo 的 motion/join 完成條件未成立」這個調查方向，不能稱 swap 卡死。沒有可證據支持的單點修正，因此不加硬編 logo skip、timeout 或 forced-finish。

## 回退與第三目標

root 已保存候選三檔與 full-stage2-candidate.patch，還原 heap.c 至 pre-freelist 的 mark-skip／GC pacing，heap.h／resume.c 回 HEAD；IsExist/nopop 修補保留。當前 heap SHA256 是 `f80013691446559208b618b42959641cba7ce8d2a41b95b1c1da319506f4d8e0`。`perf/gc-policy-result.json` 已對此 source 重跑 ASan+UBSan PASS；當前 header 無 VM_FREE，fixture 使用獨立 tag 常數，實際快照不發生 tag normalization；首 eligible GC 立即執行的政策亦照實驗證。

A/B 回退包含 membership、deferred inhibit、中型首次 GC 時間三項差異，尚不能只歸因「立即重用」一項。第三目標宜先對 logo 初始化的 CASClick/CASJoyClick timer 欄位記錄建立、存入 owner、最後 unref、再配置時的 slot/seq/ref/type；再追查 logo motion executor/timer 在 Join 中的 identity。證明合法 owner 有對等 retain，以及 WeakRef/seq 的失效判斷正確後，才重新套用候選並驗收 logo→標題→首句→人工單擊→下一句。

目前仍未解：positive-ref cycles、ref<=0 page orphan payload、其他 WIP ownership 缺陷及長時記憶體曲線；在主線回退後，正常 page slot 未即刻歸還的既有缺陷也仍存在。新的長停頓改善與遊戲前進情況以 root 的回退後實測為準。
