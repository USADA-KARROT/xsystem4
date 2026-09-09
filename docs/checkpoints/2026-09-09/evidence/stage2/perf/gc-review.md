> 狀態更新：以下修補描述屬已保存的 free-list candidate（b72d72…），因 normal-final-perf 實機停在 logo 已由 root 回退，不是當前交付版。見 `work/stage2/freelist-candidate-result.md`。目前主線保留 pre-freelist GC mark/pacing（f800136…），其 fixture 已另外重新通過；候選原 fixture/result 已保存 candidate/perf。

# GC 與 free-list 審查／修補（2026-09-09）

範圍：隔離 stage2 source。初次為唯讀審查；發現 page slot 無法正常回收後，root 明確授權修補 `src/heap.c`、必要的 `include/vm/heap.h` 與 `src/resume.c`。原專案與 stage1 未修改；本子任務未執行遊戲或 UI。

## 審查結論

`gc_collect_cycles=false` 同時包住 mark 與 cycle sweep，省去原本無人使用的可達性標記；mark helper 僅寫 GC bitmap、工作堆疊及結構型別 cache，沒有改 VM refcount/page。新 `false && GC_IS_MARKED` 短路也不會讀未配置的 mark array。不能據此宣稱所有 GC/配置時序相同：pacing 改變觸發時間，sweep 會重建 free-list，配置 slot 次序可能改變。

pacing 在 GC **完成後** 記錄時間及 allocation counter，>=4M 與中型 heap 都受相同冷卻/配置進展限制。unsigned 32-bit elapsed 支援 SDL ticks 正常 wrap；空 list 仍進 `heap_grow`，不會因 GC 冷卻或 inhibit 停止配置。中型 heap 首次 GC 已恢復舊有 `now >= 5000` 條件，>=4M 首次仍可立即執行（`src/heap.c:493–522`）。新程序 BSS 初值安全；同程序 heap_init 保留累積 pacing 狀態，可能改變重初始化後首次 GC 時間，但不是 free-list 正確性的必要條件。

## Material finding：VM_PAGE=0 與空 slot tag 混用

舊 `heap_free_slot` 以 `ref==0 && type==0` 判斷已回 list；但 `VM_PAGE` 本身就是 0。正常 `heap_unref`/`delete_page` 釋放 page 後設定 ref=0，再進此 helper，遂被誤判成重複釋放。實體 page 雖已釋放，slot 卻直到下次 GC 全掃才重新可用。舊函式已用 production fixture 重現；這是既有缺陷，新 pacing 降低補救 GC 次數會加重其影響。

修補：

- `include/vm/heap.h:27–33` 新增 `VM_FREE`；`VM_PAGE/VM_STRING` 數值與 `NR_VM_POINTER_TYPES` 保持原值。只作內部空 slot membership，不是存檔 object type。
- `heap.c:564–584` 僅 `ref=0 && type=VM_FREE` 才視為已連入 list。正常 page/string 歸還時清 payload pointer、標記 VM_FREE、推入 list；重複釋放不會新增節點或產生自環。
- heap init/grow、GC sweep 的已配置區／未使用 tail／shrink 重建皆寫同一 tag（`heap.c:85–89,116–121,425–439,471`）。allocation 隨後覆寫真正型別。
- deferred destructor drain 用平衡的 `heap_gc_inhibit/allow` 包住（`heap.c:664–696`），避免 destructor 配置時 GC 將尚待處理的 ref=0 page 加入 list 而提早重用。所有正常出口均恢復計數，既有外層 inhibit 保留；空 list 仍可 grow。
- `resume.c:567–579` 載入完成後重建 free-list，替 ref=0 slot 寫 VM_FREE／NULL。此前 `delete_heap` 已釋放原活物件；載入函式替新活物件寫正式 type/ref。JSON 與 rsave serializer 都先用 ref==0 過濾（`resume.c:90,270`），沒有序列化 VM_FREE，存檔格式不變。dump 可顯示 FREE。

GC dead-slot 原本因 `VM_PAGE=0` 不回收孤兒 page payload。本次保留既有 **orphan string** 清理範圍，避免把 free-list 修補擴為另一套 page/cycle 回收策略；正常 page 資源由原本 delete_page 清理。ref<=0 page orphan payload、positive-ref cycles 與錯誤 ownership 造成的記憶體成長仍未解決，不能宣稱 heap growth／記憶體洩漏已全面修復。

## 驗證

`perf/check_freelist.py` 直接 include 當前 production heap.c，抽取當前 production delete_page/variable_fini 及 resume delete/rebuild；VM destructor 和 allocator 所需外部介面使用小型替身。ASan+UBSan、兩個 C 檔以 normal flags syntax-only 全部通過。涵蓋：

- 舊 production guard 確實漏歸還 page slot；新 helper 只歸還一次。
- 100,000 次實際 heap_unref/delete_page/allocate 重用相同 page slot，heap 保持 4,096，無 GC／grow。
- page/string、exit_unref、重複 free、正常重新配置；整條 list 無環、節點數等於 free_count，活 slot 不在 list。
- GC rebuild 含 tail；production resume delete/rebuild 後 free tag、清空已釋放 pointer、活 slot 保留、重複 free 與重新使用。
- destructor 把 child 放進 deferred queue 後立即配置、可用 slot 已耗盡：pending child/current slot 不被重用、必要 grow 成功、inhibit 恢復。
- 中型 heap 首次 GC 在 4,999ms 不跑／5,000ms 可跑。

`perf/check_gc_policy.py` 已對當前 source 重跑 ASan+UBSan 全部通過。與原 collector 快照比較 **只 normalize ref==0 的新 VM_FREE tag 為舊 type=0**；沒有 normalize ref、資源是否存在、free-list 次序／數量或 scan_limit。故其等效結論僅適用 mark-skip 的 snapshot 結果，不能把新增 free-list 修補宣稱成完全不改配置行為。其他案例涵蓋長 GC 完成後冷卻、allocation budget、ticks wrap、9999/10000、1023/1024、inhibit 及耗盡 fallback。

結果：`perf/freelist-result.json`、`perf/gc-policy-result.json`。兩者綁定 heap source SHA256 `b72d72d814de19d9fa3eab559260d251c4470a02566e719f39363e7c052c3d8b`。Source 已凍結交 root 重建；遊戲長期穩定性、normal/O2 實際卡頓與記憶體曲線由整合實測決定。
