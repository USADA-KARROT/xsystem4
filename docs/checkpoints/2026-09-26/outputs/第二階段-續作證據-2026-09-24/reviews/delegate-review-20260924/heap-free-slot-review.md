> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# heap_free_slot 三個 caller 的限定靜態審查

結論：`ref == 0 && type == 0` 不能識別 double-free，因 `VM_PAGE == 0`，而所有正常 caller 都先把 ref 設0。保留範圍 guards、移除此條件，能修正常規 VM_PAGE 未入 free-list；不要改以 `type=0` 當 free sentinel。此结论不代表已有 GC/deferred 重入路徑全部安全。

來源 `work/stage2/source/src/heap.c` SHA-256: `f80013691446559208b618b42959641cba7ce8d2a41b95b1c1da319506f4d8e0`。只讀檢查，未改 production。

| 直接 caller | 進入 free helper 前的狀態 | 重複釋放控制 |
|---|---|---|
| heap_unref queue-overflow fallback（原670行） | 入口實際refs=1才走此路，先ref=0；free_page/free_string後明確type=0。舊guard對所有type必然return。 | 同一入口再次heap_unref會因HEAP_REF<=0 return；fallback不跑destructor，沒有allocation/reentrant GC。 |
| heap_unref deferred drain（原699行） | 正常entry由refs1轉0只入列一次；drain取出時要求ref==0，設-1後delete_page/free_string，最後再ref=0。page type仍VM_PAGE(0)，必被舊guard誤攔。 | 入口拒絕refs<=0；drain跳過ref!=0；delete_struct暫時大ref避免destructor直接重入。正常不觸發GC重建時每個slot入free-list一次。 |
| exit_unref（原761行） | 入口ref<=0警告return，ref>1只decrement。ref==1完成遞迴清理後設0；page type=0必被舊guard誤攔。 | 無destructor、無allocation；正常無循環ownership下入口guard擋後續重複unref。沒有在本任務擴張循環exit處理。 |

`heap_free_slot`為static，全source僅以上三個直接caller，GC自身直接重建free list而不呼叫它。公開heap_unref/exit_unref在已釋放slot上均先return，所以一般free→再free的防護不需要此錯誤type判斷。stale handle撞上已重用slot則是另一種ownership錯誤，原guard也無法辨識。

## 不能忽略的既有重入界線

即使 `gc_collect_cycles=false`、periodic GC disabled，`heap_alloc_slot`仍有pressure GC路徑：heap>=10000且free_count<1024等門檻成立時執行heap_gc。GC重建將ref<=0的slot直接加入free list；它不知道 deferred queue 內哪些ref0 slot正在等候清理。合法destructor可以釋放一個子物件（在deferred_processing=true下排隊ref0），再allocate；若這次allocation觸發pressure GC，該子物件先被GC入free-list，而後drain可能再入列。因此僅入口guard不足以證明此重入路徑必然單次入列。

這不是保留錯誤guard或擴增type sentinel的理由。若要求一般destructor壓力路徑也可證安全，最小前提是deferred drain期間inhibit pressure GC（現有counter機制可用），或建立明確queued/free狀態。不能把小型slot-reuse fixture通過提升成這條壓力路徑也已動態覆蓋。本次只指出此必要邊界，不修改GC或展開cycle研究。

## 最小 drain inhibit 約束核對

再次全 source 搜尋：heap_gc 為 static，只有兩個實際呼叫點。heap_alloc_slot pressure 路徑在 gc_inhibit <= 0 條件內；heap_gc_periodic 目前首行 return 停用，即使未來移除此停用 return，其既有 gc_inhibit > 0 檢查仍會阻止呼叫。沒有第三個繞過 inhibit 的入口。

因此，在最外層 if (!deferred_processing) 開始 drain 前 heap_gc_inhibit()，涵蓋整個 while、delete_page 與 destructor，完成後配對 heap_gc_allow()，足以阻止所識別的 GC 重建與重複入列交錯。nested heap_unref 仍加入同一 deferred queue，持續由外層保護。heap_grow 僅接上新 slot 區域，不重新入列既有 queued slot。counter 支援與 alloc_struct 既有 inhibit 巢狀搭配，需維持配對。

在此約束與既有入口 guards 下，移除錯誤 type0 guard 合理，不需要另設 type sentinel。動態 physical slot reuse、double-unref 與壓力反事實由主代理驗證；此處只確認靜態呼叫覆蓋。
