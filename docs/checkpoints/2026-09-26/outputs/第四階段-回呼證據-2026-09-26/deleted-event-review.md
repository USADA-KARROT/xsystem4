> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 首筆 delegate/string 錯型：AIN 限定證據

2026-09-26；續 Personality 審查，未修改 production、未執行 GUI。本輪 root 已捕捉更早的 `PAGE_WRITE_AUDIT`，因此 Personality 暫列後續症狀。

## 已知 runtime 證據

`work/gui-stage3-20260924/runs/stage4-page-write/engine.log:28`：slot282366、ref1073741825（TEMP_FLAG|1）、seq2009824、oldtype1（VM_STRING），將寫入 page type4（DELEGATE_PAGE），instruction **0x38b372 DG_PLUSA**。呼叫鏈是 f25144 lambda ← f24980 OptionalExtensions::Match ← f14312 DeletedEvent::add ← CParts Attach／constructor ← CSpriteParts ← Motion ← SceneTitle FadeInButton。

這證明錯型寫入，不單憑此證明 slot 原先為 delegate、何時被回收，或誰是第一個錯誤 owner。

## 真實型別與指令

同一 `personality_metadata.c` 以原 AIN parser 查得：

| fno | entry | 宣告 |
|---|---|---|
| 14312 CParts@DeletedEvent::add | 0x38b276 | arg0 DELEGATE136；local1 WRAP<STRUCT327>；local2 REF_TYPE/struct327 |
| 24980 OptionalExtensions::Match<DG_DeletedHandler?, DG_DeletedHandler> | 0x51755e | arg0 WRAP<OPTION<DELEGATE136>>，arg1 VOID，arg2 DELEGATE824 some，arg3 DELEGATE825 none；local4 WRAP<DELEGATE136>；return OPTION<DELEGATE136> |
| 25144 some lambda | 0x38b358 | arg0 WRAP<DELEGATE136>，nr_args=nr_vars=1；return OPTION<DELEGATE136> |
| 25145 none lambda | 0x38b38e | 無參數／local；return OPTION<DELEGATE136> |

delegate824 的參數本身也是 WRAP<DELEGATE136>，不是 STRING、泛型占位字串，也不是 primitive ref 二槽。原始數字型別是 WRAP=82、OPTION=86、DELEGATE=63、VOID=0。

f25144 全部 body（技術片段）：

```
38b358 PUSHLOCALPAGE
38b35a PUSH 0             ; destination = lambda.local[0] lvalue
38b360 PUSHLOCALPAGE
38b362 X_GETENV
38b364 PUSH 0
38b36a X_REF 1            ; source = outer DeletedEvent::add.local[0]
38b370 A_REF
38b372 DG_PLUSA
38b374 PUSH 0             ; option discriminant
38b37a RETURN
```

`DG_PLUSA` 的目的地是 lambda.local0 所指 delegate；X_GETENV 所讀 outer local0 是要新增的 handler，不是目的地。none lambda 則取 outer.local1 `set`、field64，將 outer.local0 經 A_REF 配合 tag0，以 `X_OP_SET 2` 寫入 option。

Match 從 self=[set,64] 讀 option tag，tag<1 才取 value；將其存 local4（X_ASSIGN@0x5175c4、SP_INC@0x5175ca），再透過 delegate824 把 local4 傳 some lambda（DG_CALL@0x51761e）。因此最小 trace 只需 set.field64/65、Match.local4、lambda.local0 三個視角，記 slot+seq+type+ref。

dump 精確入口：`<USER_HOME>/Claude/projects/dohna-cn-dump/ain_code.txt:642660` 附近是 add/兩個 lambda；`:920357` 是 Match。可重跑 metadata 見同目錄 `.c` 與 `.tsv`。

## 有界 production 對照與候選

1. `page.c:variable_initval(AIN_WRAP)` 配置 VM_PAGE 的 wrap placeholder；`_function_call` 只初始化非 args locals。f25144 無非 args local，故它不會自行配置一個 STRING default。目前證據不支持「lambda 宣告被讀成 STRING」。
2. `vm.c:delegate_copy_argument` 目前只對 AIN_REF_TYPE retain。delegate824 的 WRAP<DELEGATE> 參數從 caller stack 複製給 f25144，未 retain；但 `page.c:variable_fini` 明確會 unref AIN_WRAP。這是 **靜態 ownership 候選**：Match.local4 的 SP_INC owner 與 lambda borrowed arg 是否在兩層 return 都被釋放，需用同 slot 的 ref/seq 證明。它可解釋第一次 add 之後 field64 留下 stale slot，第二次被重用成 TEMP string，但目前尚未完成此時間線驗證。
3. `DG_PLUSA` 先以 heap_get_delegate_page 檢查 dst，後面卻只以有效 ref/slot 範圍決定 `heap_set_page(dst_i,result)`，沒有 heap.type==VM_PAGE 的寫入條件。因此任何傳入的活 STRING slot 都會遭錯型覆寫。這是已被 audit 命中的放大點，光把 heap_set_page 改 type 或對 mismatch 建空 delegate 會掩蓋上游 wrong owner，尚不是充分修正。

有限下一步：追一次空→有 handler 的 none branch，接著兩次 some branch。在每次 enter/return 記 field64/65、Match.local4、lambda.local0、DG_PLUSA dst/add 的 ref/seq/type；若第一輪 cleanup 已把 field64 owner 釋放，可在獨立真 bytecode fixture 驗 delegate arg retain 修正。fixture 應驗原 owner 存活、handler 數目、return option 與最後 teardown，而不是只證明不再 free_string warning。
