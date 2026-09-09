# Delegate ref 參數 ownership：observer 完成標記消失的根因

2026-09-09，修改限於 `src/vm.c` 的 delegate 參數複製；無遊戲 fno 特例、無强制 IsEnd、無永久保留 local page。UI／遊戲由主執行者操作，本分析僅讀其產物並執行小型 fixture。

## 實測確認的因果

`runs/pixel-string-watch/engine.log` 使用 normal 引擎、實際單次點擊進入開頭。MSG2 於 67.326 秒觸發觀察。67.361 秒的第一個 CObserver.Execute local page 是 slot 1696819：

1. Execute 初始化 endObserver=0，page ref=1。
2. 36081 在 X_ASSIGN 位址 0x5956fe，確實將同一個 page 的 index0 由 0 寫成 1；寫回成功，沒有落到 dummy_var。
3. 36081 RETURN 位址 0x595706 隨即出現該 page 的 `unref-last`，此時 caller frame 20752 仍在堆疊。
4. 隨後 CObserver.IsEnd setter 20748 收到 0；Execute 返回時其 local page 已無效。

前六個完整 watch 樣本都有同樣的寫入成功→callee RETURN 釋放 caller page 鏈。當 36081 的 local page 被清理，它的 End 參數也被清理，導致引用指向的 caller page 提早釋放。因而 Array.Erase(predicate) 即使正確綁定，也看不到 observer.IsEnd=true，舊 observer 持續呼叫 EndWaitForClick，造成後續對話等待迅速退出。

## 型別與程式碼證據

真 AIN 中 36081 的參數與 delegate 248 都是 `AIN_REF_BOOL=51` 加一個 void companion，保存 `[caller local page, variable index]`。`AIN_REF_TYPE` 是展開許多 case 的宏，**包含 AIN_REF_BOOL**；`variable_fini` 對其執行 heap_unref。因此 raw-copy 該 page slot 到 callee 後，callee cleanup 必須有對應的 owned reference。

原 `delegate_call` 只複製值，沒有取得這份 reference；普通 `function_call` 已有對 AIN_REF_TYPE 的 heap_ref。官方上游 delegate_call 則使用 vm_copy，其 AIN_REF_TYPE 分支同樣 heap_ref 並保留原 slot，不會深複製 ref bool 的目標頁。

## 有限修復

新增 `delegate_copy_argument`：AIN 14 以上的 AIN_REF_TYPE 參數，對有效第一槽做 heap_ref 並返回原值。delegate copy 使用此 helper；第二槽 variable index 仍原樣複製。callback 仍寫同一個 caller page，callee cleanup 則把 ref 由 2 還為 1。

AIN 14 以下沿目前原路徑；其它 value、wrap、interface 的既有複製語意未在這輪擴大調整。不更動 variable_fini／全域 page 生命週期政策。

## Production fixture

`validation/check_delegate_reference.py` 抽取未改寫的 production delegate_call、slot helpers、delegate_copy_argument、以及 production variable_fini。以真 AIN 宣告配合最小 frame／heap allocator，執行參數複製、callback cleanup 和 delegate 最終參數出棧。

- 舊 production delegate_call：End alias 先寫為 1，callee cleanup 提早釋放 caller page；重現成功。
- 新 production delegate_call：同一 delegate 連續兩個 handler 均指向原 caller page，ref 1→2→1，End 保持 1。
- delegate 最終參數清理保持 caller sentinel 與 stack 正確；caller 最後只釋放一次。
- ref bool／int／float／long／string／struct／array 家族保持 alias 並平衡引用；void companion／primitive 不增加 ref；null／pre-v14 helper 路徑維持原值。
- ASan＋UBSan 全 PASS，production vm.c syntax-only 和 diff --check 通過。macOS fixture 使用 detect_leaks=0，未驗證 LeakSanitizer。

結果含 SHA256：`validation/delegate-reference-result.json`。原 watch 關鍵行：`validation/observer-page-lifetime-evidence.json`。fixture 的 callback 寫值動作為小型替身；其正確性已由上述 production X_ASSIGN watch 證實。實際對話停住、一次點擊前進與後續遊戲流程仍由下一輪整體 normal／ASan 實測驗收。
