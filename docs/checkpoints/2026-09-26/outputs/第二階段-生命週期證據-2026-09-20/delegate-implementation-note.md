> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# v14 delegate 局部修正與驗證界線

2026-09-20。修改僅在隔離 `work/stage2/source` 的 delegate 相關 page.c、vm.c、include/vm.h；未改 timer、X_A_INIT、variable_decltype。修改前檔案保存於 `delegate-before/`。主代理同時有其它階段二修改，最終完整 patch 與建置由主代理整合。

已實作：

- v14 tuple obj/env 各擁有一次引用，constructor/append集中retain；DG_NEW_FROM_METHOD不再重複retain。
- v14 tuple slot0/2以 `AIN_REF_INT` 作內部持有引用分類，slot1(fun)仍VOID；因此現有vm_copy的reference類別保留slot alias並retain，delete_page_vars/exit_unref能release。**AIN_REF_TYPE在此repo是case-list macro，不是enum值**，不能直接return；先前review示意碼須依此修正。pre14仍VOID。
- shallow-copy辨認v14 reference marker；append第三槽不再誤存seq，plusa保留來源env。
- erase/clear移除entry後配對release；clear先snapshot，避免release重入看到尚未拆除的entries。
- v14 callback frame額外pin有效obj/env。正常RETURN、scenario frame cleanup、timeout unwind、vm_free均有配對；frame清理先snapshot以避免destructor重入覆蓋call-stack slot。pre14 method keepalive不变。
- observer fixture移除原本模擬opcode的兩行額外heap_ref，改由production constructor持有。

本代理已跑過的結果：

- optimized完整引擎成功建置（delegate-build-optimized.log）。
- 真實observer bytecode連跑20輪，false→true→第三次不再callback均通過；每輪292次caller-page存活檢查、16次setter狀態檢查。
- collection/env/motion root teardown refs均回0；**每輪仍剩4個live slots，20輪80，exit78，不能標示生命週期驗收全過**。
- 首輪殘餘為STRUCT_PAGE index617一頁、VM_STRING一頁、兩個空DELEGATE_PAGE；明細在delegate-observer-optimized.err。依主代理指示不擴大修改一般assignment／初始化ownership。

已加入下一次統一build即執行的production ownership smoke（位於observer_fixture.inc，observer模式首輪前）：v14 create/copy/append/plusa/duplicate/erase/clear/delete、obj==env雙edge，以及pre14 sequence恰好等於live heap slot時不應retain/release。此新增smoke與最後frame snapshot調整，本代理未再次build；等待主代理統一optimized/ASan結果，不能把未跑測試寫成已過。

風險／未驗證：callback內clear自身的動態用例尚未額外執行；env強引用環仍需明確拆環，cycle GC未啟用；相同obj/fun但不同env的delegate identity去重語義未改；resume相容性未擴大驗證。ASan由主代理統一執行，本代理不另聲稱結果。
