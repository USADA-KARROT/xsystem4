# Stage 2：CG 名稱 getter 的 v14 ABI 修復

限定修改 `work/stage2/source/src/hll/PartsEngine.c`，來源為 `fix/stage2-dialogue`、基底 `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b`。未提交 Git commit、未修改 Stage 1／原專案、未啟動遊戲或完整建置引擎。單檔 patch：`work/stage2/abi-fix.patch`。

## 問題及修復

本次 Dohna AIN 的 `Parts_GetPartsCGName` 是 `string(int Number, int State)`，舊綁定卻指向 `void PE_GetPartsCGName(int, struct string **, int)`。這與 Stage 1 UBSan 所見 `cg_name=0x1` 一致：State 被當成輸出指標，且回傳型別及參數數量也不符。

- 新增 `PE_v14_GetPartsCGName`，用區域輸出指標呼叫既有 CG getter，直接回傳其字串副本。
- 沒有 CG／state 無效時回傳 `string_ref(&EMPTY_STRING)`，提供呼叫端可以釋放的有效字串所有權。
- PreLink 只在 AIN 宣告同時符合 **string 回傳、兩個參數、兩個參數均 int** 時切換；並檢查參數陣列存在。套用 `GetPartsCGName` 和 `Parts_GetPartsCGName` 兩個 alias。
- 舊三參數輸出形式維持原有綁定，其他 getter 未擴大修改。以實際宣告選擇，沒有僅以 AIN version 判定。

## 有限驗證

執行 `python3 work/stage2/validation/check_cg_name_abi.py`，編譯及測試皆 exit 0，ASan／UBSan 無輸出。`git diff --check -- src/hll/PartsEngine.c` 通過。

Fixture 從正式 `PartsEngine.c` 抽出新增函式、從正式 `parts.c` 抽出未修改舊 getter，使用真實 libffi 呼叫及已建置之 libsys4 字串實作；只有 parts／HLL registry 以最小資料替身提供。兩個 alias 均驗證：

1. 舊 `void(int, ref string, int)` 綁定保留，輸出副本正確；沒有 CG 時仍保留呼叫者原字串。
2. 新 `string(int, int)` 綁定被選中，兩個 int 經 libffi 呼叫成功，回傳獨立副本，釋放後原 CG 字串保持有效。
3. 各 100 次空 CG 回傳及釋放後 EMPTY_STRING refcount 回到原值；無效 state 也回傳有效空字串。
4. 回傳型別、參數數量、任一參數型別不符合，或宣告／參數陣列不存在時，不切換原綁定。

結果與完整編譯參數：`work/stage2/validation/cg_name_abi_result.json`；不含環境變數。Fixture 二進位 SHA-256：`b5d344aa96091f3ecd869875e837d883692ffa5902a1062f89673555bbc3875c`。

這是 ABI 及所有權的隔離測試，並非完整 PartsEngine link／畫面／遊戲整合驗證。下一步由主 agent 統一建置並重跑 Stage 1 的 `AdvEventCg@Set` 崩潰入口，確認此 UBSan 消失，再記錄下一個實際阻塞；不宣稱已解決所有 VM_PAGE／文字顯示問題。
