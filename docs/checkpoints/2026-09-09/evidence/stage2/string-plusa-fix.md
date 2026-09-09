# Stage 2 — S_PLUSA / S_PLUSA2 lvalue 修正

日期：2026-09-09。範圍僅隔離副本 `source/src/vm.c` 的兩個 opcode 共用 case；不修改 AIN、原專案或 Stage 1。

已確定問題：目前 Dohna v14 AIN 的 `CMessageTextComposer@CreateDrawChar`（f7049）在 `S_PLUSA2` 前把 `[local_page, local_index, rhs_string_heap_slot]` 放上 stack，WIP 卻把 `local_index` 當作左值字串的 heap slot。結果 append 到別的字串、目標對話文字仍空，並且每次留下 1 個額外 stack item。`integrated-dialogue/engine.log` 首次 f7049 返回時 `raw_sp=17/base=14`，該輪恰有 3 次 append；後續 5 次 append 的輪次也多出 5 槽。

修正由 `initialize_instructions` 的 `CALLMETHOD.args[0] == T_INT` 選擇現代指令契約（目前 libsys4 對 version >= 11 設定此 metadata），與 Rufim 的 gate 一致。現代 branch 使用 `stack_pop_var()` 解開 `[page,index]`，append 真正左值 heap string，保留獨立回傳值的 string reference，再消耗 rhs，最後 push 新的 owned string heap slot。舊版單槽 heap lvalue branch 原樣保留。

兩 opcode 在 official upstream 及 Rufim 均共用「回傳一個 owned string 給 caller DELETE」契約。本修正沒有將 S_PLUSA2 改成 void；處理自串接和 COW，先 retain 結果再釋放 rhs。

驗證：`validation/check_string_plusa.py` 從 production `vm.c` 抽取真實 case，同時從 HEAD 抽取未修正 case。小型 VM page/stack/heap fixture 配合真實 libsys4 string 函式及 `initialize_instructions`，以 AddressSanitizer/UndefinedBehaviorSanitizer 執行。

- 舊 case 成功重現：對話左值不變、heap[index] 的 canary 被改、stack 多留 1 槽。
- 新 case 通過 version 6 / 11 / 14、兩 opcode、local / struct lvalue、GB18030、多槽 stack delta、self append、COW、rhs 消耗、caller DELETE 後左值仍有效。
- 結束時所有 fixture heap slots 及 EMPTY_STRING reference count 均回到基準。
- 結果 `validation/string-plusa-result.json`：compile/test exit 0；case SHA-256 `9e360c602ffe59cfb1375a063718867c2b9387948209695729e2568844d7b51f`。
- fixture binary SHA-256 `49e424d8e8e95d660fc205fbd0eaa9288954b11c8afd2b27e5470a2134d3f2ea`。

root 隨後的 `runs/pixel-string-watch/engine.log` 已驗證：f7049 首輪 `raw_sp=14/base=14`，Text setter 累積 `20 → 21 → 59` bytes（lines 100–107），後续6項message的輪次也 stack平衡。這證明該資料流與堆疊缺陷已修；畫面文字可見和持續等待仍需後續 render/ref-bool 整合驗收，不能由此宣稱完整 ADV 已恢復。
