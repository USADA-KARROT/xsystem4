> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 第四階段發現

- 原 callback 參數 AIN_WRAP 是 borrowed heap slot；local 清理會 release，但 delegate_copy_argument 未 retain。導致仍被 optional field 持有的事件 delegate 提前釋放，再被字串配置重用，之後 DG_PLUSA 把 delegate page 寫入 VM_STRING slot。
- 第一筆診斷 free_string(ref0,size4) 實為 delegate page；更早 page-write 診斷確認 slot282366 VM_STRING 被 lambda25144 DG_PLUSA@38b372 覆寫。最小修正讓 AIN_WRAP 跟 REF_TYPE 一樣保留有效 heap 引用，涵蓋所有 WRAP callback arguments。
- 真原始 DeletedEvent add 後段：baseline 第2次 add 已失效；修正後3次 add/count1→2→3與384次字串配置通過。teardown仍live23、exit87；不能列全測試PASS。
- 其餘六種 ASan+UBSan headless 測試通過；LSan未啟用。
- optimized GUI原10,627行double-free警告，本次修正後0；但仍Personality assertion、assert後freelist異常、X_ASSIGN clamp，MSG0。
- 診斷GUI確認 ctor id=-1，不是有效空字串；caller WorkerCreator28980從Take<string>36371結果第0項做X_REF→A_REF→NEW。容器alias本身符合原腳本，不能當成根因。獨立Personality正常string輸入40輪通過。
- 所有perf為vsync0 present提交率；無法證明實際遊玩FPS或長時間穩定。
- 最後候選只含最小VM變更，heap.c/libsys4 string.c已還原到本階段baseline，臨時診斷留獨立patch。
