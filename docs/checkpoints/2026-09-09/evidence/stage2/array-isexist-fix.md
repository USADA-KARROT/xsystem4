# Array.IsExist：開場卡住的重載錯綁

此輪卡住已定位為值搜尋被當成 callback 執行。這項證據不支持將停滯歸因於新 GC 的 mark gate；GC 的相同 heap snapshot 回收語意驗證仍成立，allocator 的 free-list 問題由另一項修復處理。

## 實機與 AIN 證據

- `perf/normal-gc-36851-sample.txt` 的主執行緒 2,420 / 2,420 samples 在 `Array_IsExist → vm_call_nopop` 內。
- `perf/optimized-gc-37055-sample.txt` 的主執行緒 2,397 / 2,397 samples 同樣在這條路徑。O2 不會修正這個錯誤控制流。
- `normal-gc-perf/engine.log` 與 optimized 手動輪 heartbeat 持續顯示 `AdvSystemButton@SetShortcut`（31722）→ `AFL_Parts_AddProcessList`（4）→ `parts::detail::AddProcessList`（374）→ `parts::detail::AddConstructProcess`（373）。
- 實際 AIN 31722 的 `sc` 是 `wrap<array<AdvShortcutType>>`。於位址 `0x675556` 呼叫 `Array:56 IsExist`、arg3=1；傳入的是陣列與 enum 值，沒有 callback 的 `(object, function)` 配對。
- 同一份 AIN 實際宣告：`Array:56` 為 `bool IsExist(ref array<hll_param>, hll_param)`；`Array:57` 為 `bool IsExist(ref array<hll_param>, hll_func)`。兩者分別是參數型別 74 與 95。
- 修正前 `Array.c` 只有 predicate 實作，FFI 依名稱把兩個宣告都連到它。enum 4 因而被當作函式 4 執行。函式 4 要求 3 個參數；舊 callback loop 最多只推 2 個，进一步錯用 VM stack 資料。373 / 374 本身沒有 `IsExist` 呼叫。

## 有限修復

`src/hll/Array.c` 增加 `array_isexist_function()`，依實際宣告選擇值搜尋或 predicate；`src/ffi.c` 僅在靜態連結 `Array.IsExist` 的區域使用這個選擇器。

值搜尋比較整數 / enum 原值；參照型字串以內容比較；兩槽值只以第一槽作值比較，第二槽 metadata 用原始整數比較。值搜尋不呼叫 VM、不取得或釋放陣列元素的 ownership。

predicate 按 logical element stride 迭代，把兩槽元素的 metadata 傳入第二槽，保留舊的一槽參數路徑與兩槽 callback 的零 companion fallback。callback 必須有 1–2 個參數；不把缺參數值從 stack 中取出。未知 HLL signature 讓既有未連結 HLL 診斷處理，避免默默使用錯 ABI。`AIN_HLL_FUNC_71` 及舊 typed ref-array 宣告可由同一選擇器處理。

測試 actual AIN predicate 36095 時另確認 `vm_call_nopop` 的參數 copy 漏 retain `IFACE`，但 callback 返回時 `variable_fini` 會 unref。這會讓陣列仍持有的物件提前釋放。最終僅在 `vm_call_nopop` 的 retain switch 補 `IFACE`、`IFACE_WRAP`；第二槽型別為 `VOID`，不被 retain。`FUNC_TYPE` 未擴充。

`OPTION` 未納入最終修復：本 AIN 同時有 primitive OPTION，而現有 variable_fini 不檢查 inner type。僅以 retain/unref 對稱並不能證明把 primitive payload 當 heap slot 是正確語意。候選 OPTION case 已移除，既有行為保持原狀；此處不宣稱 primitive 或 heap-backed OPTION ownership 已解決。

## 驗證與限制

`validation/check_array_isexist.py` 直接抽取 production Array 函式、`vm_call_nopop` 和 `variable_fini`，連實際 AIN parser 與 libffi，使用受控 VM frame / heap fixture 執行 ASan + UBSan。結果位於 `validation/array-isexist-result.json`。

- 驗證實際 AIN 的兩個 overload、31722 的真實 `CALLHLL`。
- 重現舊值 4 被當成 f4 / nargs3；新版本多個命中 / 未命中 / 負數 / 超過函式範圍的值均不執行 callback。
- 驗證內容相同但 heap slot 不同的字串、兩槽 metadata、陣列不變與空陣列。
- 以 actual f36095 參數宣告重現舊 IFACE callback 清理使 caller ref 歸零；新版本多次 callback 後 caller ref 保持 1，metadata 不被 retain / unref，最後真正 owner 釋放才清理一次。
- actual REF_STRUCT predicate f22399、heap-backed IFACE_WRAP 清理對稱性與 stack balance 均通過；OPTION 明確列為本修復範圍之外。
- 三個來源檔語法檢查及 `git diff --check` 通過；只有原有 unused 警告。ASan fixture 的 LeakSanitizer 未啟用，不能據此宣稱無記憶體洩漏。

fixture 的 callback body / frame allocator 為受控替身；production copy / retain / cleanup 和 Array/FFI C ABI 是直接使用來源。整合後 GUI 進度與 FPS 仍以 root 的下一輪實測為準，這份文件不代替實際遊玩驗收。
