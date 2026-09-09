# 第二目標：HLL 巢狀呼叫上下文修復

日期：2026-09-09。只修改隔離來源的 `src/ffi.c`；未修改 header、Array、VM、stage1 或原專案，未 commit，未整體建置引擎、啟動遊戲或 UI。

來源：`<WORKSPACE>/work/stage2/source/src/ffi.c`，分支 `fix/stage2-dialogue`。

## 修改與理由

原本每次 HLL 進入會覆寫全域型別／receiver metadata，巢狀呼叫退出又將 outer 所需的 arg3／self_slot 設成 -1，func_obj／param_slot2 則殘留內層值。外層 Array predicate 再執行、清理元素或構造返回參照時可能使用錯誤上下文。

- 第 393–406 行：以本次 C 呼叫的區域變數保存 `hll_current_arg3`、`hll_self_slot`、`hll_func_obj`、`hll_param_slot2`；新呼叫先設定自己的 arg3，其他三項設為既有空值 -1／-1／0，再由正常參數封送填入。
- 第 783–786 行：**在參數清理及返回值處理完成之後**恢復上述四項。不能在 ffi_call 剛返回時便恢復 outer，因本次清理仍需本次 arg3。
- 未實作 HLL 的提前返回分支位於此保存區之前，沒有改寫這四項，維持原本參數退棧／預設返回行為。
- 原本第二槽 metadata 的局部 `extern` 移至同一宣告區；沒有改動各型別封送、callback ABI 或字串所有權規則。

本次原始碼 diff：15 insertions、7 deletions；`git diff --check -- src/ffi.c` 通過。

## 另外審核的 metadata

`xref_null_src_page`／`xref_null_src_var` 是 bytecode X_REF 提供給下一次 HLL **參數封送**的一次性來源，當 null array 被建立與 write-back 後會清成 -1。這不是 Array C 函式持續使用的動態上下文，不能套用「入口保存、出口恢復」而復活已消耗來源。此次維持其原本消耗語意，並加入有限的 write-back 回歸檢查。

`args`／`ptrs`／`heap_ptrs`／`heap_slots`／`fun`／`f`／返回值本來就是 C stack 區域資料，沒有額外共享欄位要保存。這不代表其他 VM ownership、暫存參數位置、closure lexical environment 或多 predicate 參數問題已解決；未擴大改動。

## 有意義的局部驗證

執行：

```sh
python3 <WORKSPACE>/work/stage2/ffi-tests/run.py
```

此 fixture 直接編譯包含**真正 production ffi.c** 的單一 C 檔，使用已安裝的真 libffi 建立函式簽名、封送、派發與返回；沒有複製 save/restore 演算法。以有界的 stack／heap 服務替身和 C 內巢狀 hll_call 模擬 VM 回呼重入，不執行完整 VM bytecode或遊戲。

| 同一 fixture | 結果 |
| --- | --- |
| 修改前 stage1 WIP ffi.c | 58 個檢查、26 個失敗，exit 1（預期） |
| 修改後 stage2 ffi.c | 58 個檢查、0 個失敗，exit 0 |
| 修改後，fixture 編譯啟用 ASan／UBSan | 58 個檢查、0 個失敗，exit 0；無 sanitizer 診斷 |

覆蓋：

1. outer／inner 使用不同的 array、receiver、generic 型別與第二槽值，inner 返回後重複呼叫 outer predicate，四項狀態都必須正確。
2. 三層派發（outer → inner → leaf），無 callback 的 leaf 不應繼承前一個 receiver／second slot，返回後恢復 inner。
3. 未實作 HLL 返回 0，不破壞仍在執行的 inner 上下文。
4. 兩槽 generic 之後的 trailing integer 仍由正確 stack offset 清理，且清理當下使用 outer arg3；正常整數返回值與 stack 數目保持正確。
5. 單槽 ref 類 arg3=2 → 65538 的巢狀情境，以及 null-array write-back 不復活已消耗來源。

證據目錄：`<WORKSPACE>/work/stage2/ffi-tests/`，含 `nested_context.c`、`run.py`、`results.json`、三組 build／run logs。編譯明確使用現有 SDK `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`，沒有安裝套件或變更系統。

## 驗證界線

此結果只證明 HLL 呼叫上下文的可重現缺陷及本補丁的局部效果。**不能宣稱對話自走、double-free、完整遊戲 ASan／UBSan 或存檔已通過。** root 接續統一完整建置、同場景 sanitizer 與可見畫面驗證；若仍自走，應依先前 Join／IsEndWaitSection trace 區分集合內容、receiver／env、timer 與等待退出原因。
