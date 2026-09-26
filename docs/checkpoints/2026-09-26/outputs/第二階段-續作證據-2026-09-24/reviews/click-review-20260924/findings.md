> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# CASClick 額外析構診斷（2026-09-24）

**已動態確認：額外的 CASTimer 析構發生在其建構子尚未執行時。** 原因是 `alloc_struct(233)` 預先配置普通 STRUCT 欄位，而原 AIN generated initializer 又先 DELETE 欄位、再 NEW。這會銷毀未建構、handle 為 0 的預設 CASTimer，觸發 manager 的 `system.Error`。

先在本目錄建立 diagnostic fixture 副本、加入入口／opcode／物件身份列印；之後依主代理要求，另編譯私有 page.c counterfactual object。未改 production 或原 fixture，也未進 GUI。optimized probe 使用當前 engine objects；建置 hashes／命令見 `build-optimized.json`。原 AIN timer native interception flags 仍由 fixture 清除，測的是 script 路徑。

## 精確因果

`click.err` 動態證據：

| 時點 | 實際物件及計數 |
|---|---|
| 第 2 行，`alloc_struct(233)` 後 | CASClick parent slot12；member7 已是 CASTimer slot13；ctor0／dtor0 |
| 第 44 行，f6366 `0x9d2212 DELETE` | 從 member7 讀出的 slot13，ref1 |
| 第 45–48 行 | 立即進 f422，self13／seq14／handle0；此時 ctor0／dtor1，隨後 system.Error |
| 第 49–51 行，f6366 `0x9d2214 NEW [7,-1]` | 才第一次進 f420，真正新 timer 是 slot20／seq22 |
| 第 52–55 行，X_ASSIGN＋parent constructor return | member7 由已釋放 slot13 改為 slot20；ctor1／dtor1 |
| 第 56–58 行，最後 parent release | 正常析構 slot20；ctor1／dtor2，fixture 最終恰一次斷言失敗 |

析構入口記錄的 ref65536 是現有 `delete_page` 執行 destructor 時的保護值，不是新引用洩漏。父型別 CASClick 自身沒有自訂 destructor；正常最後釋放仍會透過 typed member cleanup 釋放真正 child20。

f6366 原 bytecode 在 member7 的順序為：

```text
0x9d21fe PUSHSTRUCTPAGE
0x9d2200 PUSH 7
0x9d2206 X_DUP 2
0x9d220c X_REF 1
0x9d2212 DELETE
0x9d2214 NEW 7,-1
0x9d221e X_ASSIGN 1
0x9d2224 POP
```

先前保存的 `work/lifetime-stage2-20260920/timer-bytecode.json` 包含此 f6366。動態執行也證明此 NEW 會走真正 f420，無需額外替它建構 child。

## 最小非類別特例修法

`src/page.c:427` 的 `init_struct_slot` 第一分支對 **所有版本**的 `AIN_STRUCT` 都呼叫 `alloc_struct(member->type.struc)`。這是普通欄位的預先配置來源；沒有呼叫子型別 constructor。`init_struct` 也只會在 version<14 呼叫 constructor。

建議把 **AIN v14 普通 `AIN_STRUCT` 欄位初值設成 -1**，讓 generated initializer 的顯式 NEW 執行配置／建構；保留 version<14 的 eager allocation。可寫為：

```c
if (member->type.data == AIN_STRUCT) {
    page->values[idx].i = ain->version >= 14
        ? -1 : alloc_struct(member->type.struc);
}
```

此方案不依 CASClick／CASTimer 名稱，不增加 destructor blacklist，也不在 handle0 時忽略真正析構。`variable_initval(AIN_STRUCT)` 已經使用 -1；這也讓普通 member 的初始狀態與該一般初值規則一致。證據支持目前 v14 generated-init 路徑；不是對全部版本／全部 class pattern 的完整語義證明。

本次不建議順帶修改 `is_wrap_struct`（繼承／contained wrap）的 eager allocation。普通 `AIN_STRUCT` 與 `wrap<struct>` 在目前實作走不同路徑，此案例未涵蓋 wrap。`NEW` 的 `STRUCT_FLAG_MEMBER_CTOR` 補救，以及 `init_global_struct_v14` 的遞迴建構亦屬既有假設；對普通欄位置 -1 後它們會跳過空 child，但不應在本修正中再增加 class 特例。

## 補充：真正 NEW 路徑與私有 patch 驗證

已找到原 AIN 的 `NEW 233,-1`，位置 **0x1b8da**；fixture 把 instr_ptr 指向此原指令，直接交給 production `execute_instruction(NEW)`，再執行真正 f6365→f6366 bytecode。沒有以 alloc_struct＋手動 parent ctor 取代 NEW。

**未修正 baseline 仍失敗**，記錄在 `truenew.err`：`struct_flags[233]=0`，同樣 child13／seq14 先進 destructor，再建 child20／seq22，最終 ctor1／dtor2。`STRUCT_FLAG_MEMBER_CTOR` 的建立條件除了 no-arg ctor，還硬性要求子型別名稱含 RCASTimer 或 VariableTimer；普通 CASTimer 不匹配。因此這裡並非 fixture 跳過 NEW 的補建構 fallback 才出現失敗。

依追加授權，`build_probe.py --null-struct-fields` 將上面的普通 AIN_STRUCT 初值修正在**私有 page.c object**實作，再取代連結中的 page.c.o。其餘 production objects 不變；wrap 分支亦不变。最終結果：

| 建置 | true NEW CASClick | observer | timer |
|---|---|---|---|
| optimized | exit0；350／50ms 全過；無 system.Error | 20 輪，每輪 final_live0 | 20 輪，ctor60／dtor60，每輪 live5=baseline5，teardown0 |
| ASan | exit0；350／50ms 全過；無 system.Error | 20 輪，每輪 final_live0 | 20 輪，ctor60／dtor60，每輪 live5=baseline5，teardown0 |

兩版 click 的物件身份都是 **child slot15／seq16**：f420 進一次，parent constructor 返回時 dtor0，最後釋放同一 seq 的 child 使 f422 進一次；manager teardown 後 live0。這裡每版只跑一個 click 配置 epoch，沒有宣稱 click 壓力測試 20 輪。

ASan 設定 `detect_leaks=0`；此處的所有權洩漏判斷來自明確 heap owner baseline／teardown assert，不是 LeakSanitizer。原始結果為 `nullfields-*.out/.err`；建置 hashes／命令為 `build-optimized-nullfields.json`、`build-asan-nullfields.json`。

### Observer seed 的必要調整

既有 observer fixture 直接建立受控 state，原來依賴 eager allocation 提供兩個普通 STRUCT 子欄位。修正後必須由 fixture 明確建立並轉移唯一 owner：

1. struct611.member0 `m_motion`：`alloc_struct(608)` 後直接放入欄位。
2. struct608.member1 `m_section`：`alloc_struct(617)` 後直接放入欄位。

這兩次是 fixture seed，無需額外 heap_ref；最終父物件 typed cleanup 負責 release。私有 fixture 只在欄位為 -1 時補建這兩個物件，沒有手動補 release。原 AIN metadata 顯示 struct611 其他欄位是 iface／option／bool／delegate／array，struct608 另一欄是 array，struct617 是 string／enum／bool，沒有其他普通 STRUCT child 需補。這避免把 fixture 舊的假設失效誤算成 production regression。

測試工具修訂記錄：初版私有原指令搜尋器對 invalid function address 的 unsigned bounds 檢查不足，ASan 有捕獲；已改成 size_t／減法邊界檢查再執行上述最終測試。`obsolete-scanner-bounds-failure-asan.*` 是已排除的工具診斷，不是 production failure。
