> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# Array 查詢獨立回歸驗證（2026-09-20）

僅驗證本輪 Numof／Count／Find 查詢修復。沒有執行遊戲、修改 production source，亦不代表完整遊戲可玩、其他 Array API 或 ownership 問題已解決。

## 結果

- normal 與 AddressSanitizer＋UndefinedBehaviorSanitizer 均退出 0，各 928 次檢查、12 個預期且捕獲的 VM_ERROR。沒有 sanitizer 錯誤。
- 從實際 CN AIN 載入宣告，由 selector 選擇函式，再經 libffi 呼叫 8 個重載：Numof #20／21、Count #22／23、Find #42／44／46／48。#48 在實際 AIN 的靜態直接呼叫數為 0；本次是依宣告建立的受控覆蓋。
- 計數包含全 false、部分 true、全 true（callback 用非標準 true 值 7，確認計數而非加總）；值搜尋確認不呼叫 VM；Find 檢查首命中、logical index、起迄排他範圍、負 begin、超出 end、反向／空範圍與找不到 -1。
- 驗證 2 槽 interface 與實際 wrapped interface callback f23361／25454 的 value＋metadata、logical index、Numof／Count 槽數換算。值搜尋的 interface／未知類型依目前明確支援範圍拒絕，不假設物件指標相等就是原版語義。
- 不同 string heap 物件但內容相同可命中；string 引用數在查詢前後不變。typed float 涵蓋 +0／-0 相等及 NaN 不等自身。
- 每次正常呼叫檢查陣列值／page／大小未改動及 stack sentinel／深度不變。以受控 callback 模擬重新配置並釋放舊 page、原地 resize，均由 guard 拒絕，ASan 未報 UAF。
- 非法 callback index、body address、return、arity 與未知 HLL signature 被拒絕。另以合成宣告驗證 ref primitive 傳入 `[owner slot, index]`，缺有效 owner 會拒絕；這不是實際遊戲路徑覆蓋。

## 固定 baseline 回歸

從 `baseline/Array.c` 抽取修復前的原函式，不重寫舊行為。在同一份實際 AIN 的宣告與 libffi 呼叫下，false predicate 的 Numof／Count 皆回 4（預期 0）；Find range `(1,4,9)` 回 -1（預期 logical index 2）；predicate Find 忽略 callback 而以函式編號當搜尋值的缺陷亦已重現。

Baseline SHA-256：`9d58815317c646ea06d2aef6415318d72f77dbd979a91af3c9011c217c966d5d`。

## 真實 callback 宣告盤點與限制

`callback-shapes.json` 記錄 91 個 predicate 直接呼叫點，皆可從緊鄰 PUSH 取得 fno 並讀取原 AIN 宣告；全部 return BOOL。參數為 REF_STRUCT 單槽 42 點、WRAP 單槽 40 點、STRING 單槽 4 點、INT 單槽 1 點、WRAP＋VOID 2 點、IFACE＋VOID 2 點。沒有找到 ref primitive 呼叫點。f23361／25454 的 WRAP subtype 均為 IFACE_WRAP（100），詳 `callback-wrap-shapes.txt`。

**fixture 的 vm_call_nopop 是受控替身**：驗證 query 邊界傳參、logical index 與 stack 整理，不執行商業 AIN bytecode，也不證明真實 VM callback 的 retain／release、閉包環境或巢狀 FFI 正確。root 另負責完整 production FFI 與 callback 替身的巢狀 context fixture；兩者仍不等同遊戲執行。

參數拒絕測試將 production VM_ERROR 以 setjmp／longjmp 捕獲，便於逐項斷言；實際引擎的終止路徑不是此 fixture 的驗收項目。LeakSanitizer 未啟用；因此不可據此宣稱無 leak。AIN parser 仍輸出既知 Debug_SetPartsComment 函式名稱前雜訊 warning。

## 重跑與身份

執行 `python3 work/bridge-stage1-20260920/check_array_queries.py`。需要現有 stage2 normal／asan libsys4 靜態庫、Homebrew libffi 與 Apple SDK；不會安裝或建置整個引擎。腳本每次由 production 抽取函式、重新載入本機 AIN，生成 fixture 及 JSON，並將工具鏈命令、binary／AIN／source hash 留在 `array-query-result.json`。完整環境變數未記錄。

- 已測 production Array.c SHA-256：`eae8874e9180432665c16ceada014e2f41cf623f9ce89354bfff875324b3aaa7`。
- 本報告生成時與目前 source 相符：`true`。
- 抽取 production 區段 SHA-256：`c3bba4250a7078f32522e33c0729cf115eb8d170511e2e7ed0260a6d8bf087cb`。
- normal binary SHA-256：`9f32951e1f2fb8a8f3cfb466d594a200cabf7ec47f7ad881942af7a56d0bc18e`。
- ASan／UBSan binary SHA-256：`5493f37e4c5dde7bd298bc357a0b999a2f5d74e9a9e1b4cffdb80f1bb668328c`。

交付：`check_array_queries.py`、`array_queries_fixture.template.c`、生成的 `array_queries_fixture.c`、`array-query-result.json` 及 callback 宣告盤點。fixture 與 production 分離；任何後續修改 production 都應重新產生結果。
