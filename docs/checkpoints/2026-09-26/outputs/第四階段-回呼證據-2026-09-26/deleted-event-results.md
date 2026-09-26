> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# DeletedEvent 真 bytecode 回歸結果

2026-09-26；production 修改由 root 完成，本 agent 只建立 fixture、編譯獨立 headless probe 與執行測試。沒有啟動 GUI。

**結論：AIN_WRAP delegate argument retain 修正消除了可重現的事件 delegate 失效；新增測試的 teardown 仍有 23 個 live slots，不能列為完整 PASS。既有七種相關路徑中的其餘六個測試 mode 均通過。**

## 測試範圍

`deleted_event_fixture.inc` 以真 f14312 local page 種入 local0 handler、local1 真 struct327 CPartsFunctionSet、local2=-1，this 為真 struct354 CParts。由原 AIN **0x38b34c** 執行 add 後段，完整走 f24980 Match、f25145 none、f25144 some、closures 與 RETURN。只省略 GUI FuncSet lookup 前綴；未替換任何 callback 結果、未修改原 AIN。

種入的 option field64=-1 / field65=1。三個獨立 CParts handler 以原 f25047 註冊，但測試只註冊、不派送 handler。CParts UI ctor/dtor 不在測試範圍；fixture 最後以 exit_unref 各釋放一次 harness root，事件／closure 的清理由 production 負責，不用手工補釋放消除洩漏。

每輪 add 後檢查事件 slot、seq、heap/page type、ref=1、option tag0、handler count。每輪配置並回收 128 個 TEMP string slot；共 384 個，檢查活事件未被配置壓力重用。

## 前後對照

| 版本 | 功能結果 | 結束狀態 |
|---|---|---|
| baseline VM `d96ac12974c52322d8076d299abc8150c00560156f6bbffb35859140e776be2c` | 第1次 none：event20/seq22/ref1/count1；第2次 some 回傳後 field仍20，但 slot ref0、seq143、失效 | exit86，fixture 主動失敗；尚未碰到錯型覆寫或 ASan UAF |
| candidate VM `ce20126978fb4a804e3f0586cbc69b424503a993c85a753c78d281a6d7a8fa77` | 第1–3次均 event20/seq22/ref1；count1→2→3；none1次、some2次；字串配置壓力通過 | 功能檢查 PASS；teardown live23/baseline0，exit87 |

baseline binary SHA256 `3d9c9f9aed596ec8cb5c0f72c9a1c533484717a75112d7eeeddccae0ee0bbbde`；final candidate binary SHA256 `6a9bb674619c75f32c0e397d537cb026c2230f75d181ce6d9756e8fbf79ca755`。兩版都為 ASan+UBSan engine objects／完整 production VM 與 FFI，LeakSanitizer 未啟用；因此用 live-slot 與明確所有權檢查判斷殘留，不宣稱 LSan 通過。

baseline 的 `.out/.err`、獨立二進位、build JSON 位於 `probe/deleted-baseline-*`；candidate 最終輸出為 `probe/candidate-deleted-final.{out,err}`，二進位與完整 build identity 為 `probe/deleted-candidate-*`。

## 殘留的有界診斷

釋放 set 和 caller handler owners 後，仍見：3個 f14312 LOCAL_PAGE 各 ref2、每輪2個 some/none DELEGATE_PAGE 各 ref1（共6個）、3個 Match local4 的 wrap placeholder（STRUCT_PAGE/index=-1），以及被上述所有權鏈保留的 handler delegate、CParts 與其 vtable。各 harness root 最後只 release 一次後，live slots仍23。

兩個靜態候選須另行驗證，不能以猜測列為已修：

1. f14312 的兩個 DG_NEW_FROM_METHOD 結果為新建 owned slots；普通 `function_call` 對 AIN_DELEGATE args 一律 retain，Match return 後各剩1，closure env 使 f14312 frame 留下。應區分傳入 owned 暫存與 shared delegate，不能移除所有 delegate args 的 retain。
2. Match.local4 宣告 WRAP<DELEGATE>，`variable_initval(WRAP)` 配置 placeholder；原 bytecode直接 X_ASSIGN+SP_INC，沒有 DELETE 初值，因此原 placeholder 被覆蓋。這與 borrowed-reference wrapper 應如何初始化的契約有關，不能只在這個 fno 特判。

本輪只驗證 root 的一個 AIN_WRAP retain candidate，未擴修這兩個問題。

## candidate 相關回歸

`probe/candidate-results.json` 保存各 mode exit/status；其餘測試維持同一 candidate production VM identity。新增 fixture 後續只改 teardown 診斷，未更動 production。

- Personality：原非空 literal NEW 20輪、caller-owned string/DELETE 20輪，bytes／ref／heap balance PASS。
- observer：20輪 false→true、caller/env 存活及 teardown live0 PASS。
- reentrancy：20輪原 callback self-Clear、frame refs與 teardown live0 PASS。
- click：初次按鍵、350/50ms邊界、release、nested timer ctor/dtor PASS。
- timer：60次 ctor/dtor、reset、hole reuse與 teardown live0 PASS。
- heap-reuse：1000輪 slot generation/reuse、duplicate-unref、exit_unref、live0 PASS。

上述 headless 結果不代表 GUI 或完整遊戲可玩性。GUI 短測由 root 獨立執行並報告。
