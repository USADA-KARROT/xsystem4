> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# Observer residual4：引用原因與最小修正（2026-09-24）

本子任務未改 production／舊 fixture。複製 probe 至本目錄，只在獨立副本加入 heap retain/release 診斷；經 root 授權後，再於產生的 VM 副本測試 X_ASSIGN transfer 候選。原始 AIN 未更動，執行原 f20750、20752、36081 等 bytecode；沒有啟動遊戲。

## 確定因果：不是 fixture 種資料漏釋放

`baseline.err` 的第一輪結尾為4個slot，各ref=1：SectionParam617（slot16）、其Name字串（slot19）、兩個空delegate（slot24/25）。fixture四個root owners（observer／motion／collection／environment）均已正常釋放；沒有用手工補 unref 隱藏殘留。

1. **兩個空delegate。** 原 f20750 的 `DG_NEW; X_ASSIGN 1; POP` 在地址 `0x9e921c` 與 `0x9e923c` 發生 X_ASSIGN。DG_NEW剛建立的ref=1被X_ASSIGN加成2，POP只丟stack值。之後替換notify或刪observer，各只釋放該欄位的一份owner，所以各剩1。這是原始初始化 bytecode 的 production VM 執行結果，seed 在執行前沒有配置這兩個delegate。
2. **SectionParam與其Name。** seed完成時617 ref=1，Name ref=3，分別由SectionParam、joinNames、capture sections三個合法owner持有。false callback中，f26981 `0x59038c SP_INC` 對SectionParam取得回傳值的ownership，ref1→2；f27005 `0x590bf0 X_ASSIGN` 再加成3。其local在RETURN `0x590c02`清理只3→2；之後MotionSet銷毀只2→1，造成617殘留。617仍擁有Name，故Name也剩1。**字串不是另一個獨立的A_REF leak。** trace中真正比較用的暫時字串slots41/42各ref1且已由S_EQUALE釋放。

注意RETURN清理trace顯示current fno=36085，是因function_return已pop callee frame；`ip=0x590c02`仍是f27005的RETURN，不能因此歸因到36085自己的local清理。

## 最小修正契約

在 **v14 X_ASSIGN** 不再自動retain新值或unref舊值；保留stack／邊界檢查與寫入／推回語義。原bytecode已使用DELETE釋放舊owner，使用A_REF或SP_INC取得分享／回傳所需owner。pre14分支維持原處理。

只移除new retain而保留old unref仍不正確：共享舊值先被DELETE釋放其中一份owner，欄位尚保存舊slot值，接著X_ASSIGN若再次unref會錯刪另一位owner。不能以DG_NEW特例、fno硬編或fixture teardown補釋放取代此契約。

私有候選只有一個條件差異：X_ASSIGN的type-aware refcount區塊從 `if (xa_page)` 改成 `if (xa_page && ain->version < 14)`。root後續採用等價production修正，但本目錄不修改該source。

## 已測對照與身份界線

| 組合 | 實際結果 |
|---|---|
| 舊VM診斷版（只加trace） | Observer第一輪live0→4；完整精確引用歷程在baseline.err。 |
| 私有transfer＋當時舊Array objects，optimized | Observer20輪，每輪live0；owned／borrowed／舊值共享三種opcode片段全部通過且live0。Timer第0輪三次ctor/dtor與clock/reset/hole/GC通過、live5=baseline5；第1輪clock斷言失敗，沒有完成20輪。 |
| 私有transfer＋root更新後的Array objects，ASan/UBSan | Observer20輪每輪live0、三种ownership片段live0；Timer20輪／60次ctor+dtor及所有既有clock/reset/hole/GC斷言通過，完整teardown live0。無ASan/UBSan診斷。 |
| 私有transfer＋新Array objects，另外拿掉FFI EmplaceBack primitive wrap retain（負對照） | 第一個timer ctor後handle預期0的斷言立即失敗。不要因X_ASSIGN改transfer就拿掉這個回傳owner。 |

第二列與第三列的Array object版本不同，**不得把Timer20輪成功全部歸因單一X_ASSIGN修改**。舊Array source SHA為`a7925f709d80286fb05d30b9329f08c46b84f4b0f11e391b34d63331b5bf78a6`；後來root source為`dbcef6f62c44bef68e6cd3b0ba4f4624f7c4e200ca8f31602cf90a441f153ce4`，root獨立修正PopBack保留typed empty page。

本副本固定VM baseline SHA `a65e443e37592a4f27e1ec9ac156310007654978767628c75664edbce36d8dfd`。root採納production修正後，我由最後成功的instrumented副本反向移除診斷／候選條件，驗證SHA完全一致，保存 `baseline-vm.c`，避免混用後續VM source。`baseline-heap.c`／`baseline-ffi.c`亦固定。build JSON列出pinned source、實際binary與instrumented code hash；其中source_sha256是當時workspace狀態，必須配合pinned_source_sha256閱讀。

## 為什麼timer tuple-wrap的+1要保留

EmplaceBack primitive回傳 `[array owner slot,index]`。它是暫時的owning wrap，caller在使用後以DELETE釋放，而陣列member本來也持有owner；FFI的`heap_ref(hll_self_slot)`提供的是這份回傳owner。X_ASSIGN transfer恰好把已準備好的owner交給接收處，不能額外retain，也不能把回傳owner本身取消。

负對照只移除了FFI EmplaceBack這個retain，沒有同時改At/Last；所以直接證明的是EmplaceBack +1的必要性。At/Last既有保留的+1與上述真實timer20輪相容，這不是把所有Array回傳形式全面驗收。

## 可整合的有限回歸片段

`xassign_contract_standalone.inc` 可放在包含production VM、`live_slots()`等harness helpers之後。呼叫 `test_xassign_contract()`，成功回0。這份與實測 `xassign_contract.inc` 的唯一差異，是移除只供本子probe列印用的review_phase赋值，沒有改測試邏輯；未再新增測試執行。

涵蓋：

- owned `DG_NEW → X_ASSIGN → POP`：欄位ref應1，刪欄位後0。
- borrowed `X_ASSIGN → SP_INC`：加一個owner後ref應2，刪欄位後1，再刪原owner後0。
- shared old `DELETE → X_ASSIGN(-1)`：DELETE後其餘owner的ref應維持1，不可重複釋放。

片段執行production opcode handler並使用原AIN的opcode位置／operand，但由harness種入stack operands；它們不是完整原函式執行。Observer與Timer結果則來自原bytecode函式執行，兩種證據清楚分開。

## 範圍限制

本結論定位並消除這個Observer四slot案例；沒有聲稱完整Join入口、所有初始化形式、cycles、完整遊戲／存讀檔已驗收。LeakSanitizer未啟用，殘留以VM live slots／逐slot引用與ASan/UBSan檢查；不能等同整個process沒有leak。root已有最終production驗收，應以root統一build的結果作正式交付依據，本副本主要提供獨立因果與負對照。
