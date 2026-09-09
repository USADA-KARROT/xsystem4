# Stage 2 VM ownership 有界獨立審閱

2026-09-09。只讀核對 Stage 1 → Stage 2 的 S_PLUSA/S_PLUSA2 與 delegate_copy_argument，沒有修改 production、啟動遊戲或重跑全套測試。**未發現這兩項新增 diff 造成的實質 crash／引用不平衡，沒有需先阻擋整合的修改。** 此結論不涵蓋其他 WIP VM 行為。

## S_PLUSA / S_PLUSA2

- `source/src/vm.c:3429–3448` 的新分支消耗 `[page,index,rhs]`，保留 lvalue heap slot，回傳新 string heap slot。`result = string_ref(result)` 在 `heap_unref(rhs)` 前，接著 `stack_push_string` 接管該 string reference；沒有額外 string_ref 或漏掉 RHS 的消耗。
- 真正 gate 是 `instructions[CALLMETHOD].args[0] == T_INT`，libsys4 `src/instructions.c:425–436` 對 **version>=11** 設此模式。因此不能把它描述成「只改 v14」；舊模式分支與 Stage 1 相同。Rufim `src/vm.c:3502–3533` 使用相同模式分支，官方 upstream/master 的對照仍只有 legacy body（唯讀擷取 `validation/official-vm-review.c:1524–1536`）。
- 實際 CreateDrawChar（AIN 7049，反組譯約422404–422435）的 RHS 經 A_REF 或 S_ADD；`source/src/vm.c:3718–3724` 的 string A_REF 建立獨立 heap slot，並對同一 string object 加引用。`string_append` 經 `string_realloc/cow_check` 在共享字串時複製，因此 `a += a` 的正常 A_REF 路徑不會在 realloc 後使用已釋放 RHS。
- 已核對 `validation/string-plusa-result.json` 的 case SHA256 與目前 production 完全吻合。現有 ASan/UBSan fixture 覆蓋 versions 6/11/14、兩 opcode、local/struct lvalue、GB18030、共用 string object 的 self-append/COW、RHS 消耗與 return DELETE 後 lvalue 存活。編譯/測試 exit=0，stderr 空。
- 精確限制：fixture 的 self-append 是「兩個 heap slot 共用 string object」，不是「lhs/rhs 相同 heap slot」。正常 A_REF 已確保前者；本輪沒找到後者的實際 AIN callsite，不能把測試表述為任意破壞 ownership 的 raw-slot alias 都安全。也沒有據此要求擴大修改既有 string library。

## delegate_copy_argument

- `source/src/vm.c:1553–1570,1616` 對 AIN>=14 的 AIN_REF_TYPE 宏家族做一次 heap_ref，回傳的 slot 數值不變，ref bool 的 companion index 仍原樣複製。macro 包含 REF_BOOL/INT/FLOAT/STRING/STRUCT/ARRAY 等（libsys4 `include/system4/ain.h:121–140`）；不會對普通 bool/int 或 companion AIN_VOID 做 retain。
- 這與本機 `source/src/page.c:142–167` 的 variable_fini 對相同 REF 家族 unref 相配；callee local page 釋放經 `delete_page_vars:289–293`。每次 callback 多出的 +1 只屬於該次 callee，返回後還給 caller。delegate 最後清 stack 的原有分支僅 pop 借用參數，沒有再減一次 caller 引用。
- 官方 upstream 的 delegate_call 經 vm_copy 複製參數，vm_copy 的 REF case 亦 heap_ref；Rufim 也保留一般 REF 的 vm_copy 語意，另對其自行改過的 borrowed wrap/iface 有配對策略。本輪只補本機 REF family，沒有直接搬用 Rufim 其他 ownership 規則。
- 已讀 `validation/delegate_reference_fixture.c` / `delegate-reference-result.json`：使用真實 AIN delegate248、callee36081 的 `[REF_BOOL,VOID]`，caller20752 的 bool local；執行 production delegate_call 和 production variable_fini，連續兩 callback 各 1→2→1，End alias 寫回保持、caller 最後僅 free 一次，並覆蓋 reference family、普通值、null、legacy gate。ASan/UBSan test_exit=0。其 frame/heap 是受控替身，不是完整 VM；AIN 36081 的實際 nr_vars=2，因此這個修复入口沒有多餘 companion 的落點問題。
- AIN<14 的 raw-copy 行為保持 Stage 1，不是此次修正已證明舊版全部 ownership 正確。其與官方 vm_copy 的歷史差異不屬於本次新增 regression，沒有擴查整個 legacy delegate 實作。

整合驗收仍以 root 的真實 GUI／VM trace 為準：確認 callback 返回後 20752 caller local page 存活、End=1 可讀，observer 隨後移除且對話等待不再被舊 observer 結束。這是驗證已修入口的預期，不是本靜態審閱聲稱整體 ADV 已通過。
