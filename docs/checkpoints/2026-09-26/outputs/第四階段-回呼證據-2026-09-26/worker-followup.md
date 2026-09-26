> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# WorkerCreator 傳入 Personality 的 -1：下一階段入口

2026-09-26。有界唯讀補充；未 build、未改 production、未啟動 GUI。

## 已確認的新證據

`work/gui-stage3-20260924/runs/stage4-id-trace/engine.log:57` 起：

- ASSERT@0x5c3458，f28149 Personality@0.local0 **=-1**。
- caller 是 f28980 WorkerCreator@Create 的 **NEW@0x5e8b88**，上一層 f28977 WorkerCreator::Create@0x5e8450；非先前列出的 literal caller，也不能叫作「有效空字串」。
- caller.local3 p=slot180/ref2；local5 iterator=0；local6=slot623521/ref2/seq5466335；local7=0；local8 同 slot623521/ref2/seq5466335。
- local6 與 local8 相同本身符合原腳本：local8 保存 foreach 容器；local6/7 是 `[container,index]` 這一對元素參照，並非兩個應獨立配置的容器。

## NEW 周圍的原 dump

來自 `<USER_HOME>/Claude/projects/dohna-cn-dump/ain_code.txt:1057934` 附近。以下各為前／後20個 dump 指令項；保留 `.LOCAL*`／`.STRUCTREF` 宏，所以不冒充每行皆一個底層 opcode。為可讀性，長亂碼 dummy 名稱簡寫為 `iterator=local5`、`container=local8`；`id` 為 local6/7。

```
; before: 20 dump items
SP_INC
.LOCALASSIGN iterator -1
.LOCALINC iterator               ; loop target 0x5e8b10
.LOCALREF iterator
.LOCALREF container
X_A_SIZE
LT
IFZ 0x5e8b9e
PUSHLOCALPAGE
PUSH 7
.LOCALREF iterator
X_ASSIGN 1
POP
.STRUCTREF WorkerCreator m_worker
PUSH 28066
PUSHLOCALPAGE                   ; 0x5e8b72
PUSH 6                          ; 0x5e8b74
X_REF 2                         ; 0x5e8b7a -> [local6,local7]
X_REF 1                         ; 0x5e8b80 -> container[index]
A_REF                           ; 0x5e8b86 -> owned value/string copy

NEW Personality 28149            ; 0x5e8b88

; after: 20 dump items
CALLMETHOD 1                    ; 0x5e8b92: Worker@AddPersonality f28066
JUMP 0x5e8b10                   ; 0x5e8b98
.LOCALDELETE container          ; loop exit 0x5e8b9e
.LOCALDELETE id
.STRUCTREF WorkerCreator m_worker
PUSH 28011
PUSHSTRUCTPAGE
PUSH 28981
CALLMETHOD 0
X_DUP 1
X_MOV 4 1
A_REF
CALLMETHOD 1
DELETE
.STRUCTREF WorkerCreator m_worker
PUSH 28008
PUSHSTRUCTPAGE
PUSH 28983
CALLMETHOD 0
X_DUP 1
```

## local6 / local8 的來源

1. f28980 從 `m_params`（WorkerCreatorParams，struct764）**field4 FixedPersonalities** 讀出 array，再 A_REF 存入 **local3 p**。
2. 呼叫 **f28986 WorkerCreator@CreateRandomPersonalities**，結果與 p 經 `Array.Concat<string>` 合併。
3. `p, 3 → f36371 ArrayExtensions::Take<string>(ref array<string>, int count)`。其結果存入 **local8 container**。
4. `local6 = local8` 後 SP_INC；每次迴圈把 iterator local5 寫入 local7。故這輪是在 Take 結果的**第0項**做 `X_REF1 → A_REF → NEW`。

f28986 的分支：params field22/23 `option<string> PersonalityCreateMethod` 有值時，名稱經 DG_STR_TO_METHOD 建立 `DG_Function<array<string>,array<string>>`；其輸入是 params **field3 Personalities** 的 A_REF。無有效 delegate 時 fallback **f28987 InnerCreatePersonalities**：params field3 array + field6 PersonalityProbFirst + field7 PersonalityProbAttenuation → `CreatorHelper::Lottery`。目前 trace 未識別本輪選了哪一支，不能往 EX 猜根因。

f36371 Take 的核心：`numof = Math.Min(count, Array.Numof(source))`；每個 i 執行 `source[i] X_REF1 → A_REF → Array.PushBack<string>(result)`，最後 `result A_REF → RETURN`。這是可先用一個已知非空 string array 單獨驗的真實腳本入口，不需整個 Worker 建構流程。

## 最小下一步診斷（尚未執行）

只先記錄以下三個相鄰邊界，限制首筆失敗：

1. **0x5e8b7a 後**：實際 `[array,index]`；array 的 slot/seq/ref/heap.type/page.type/a_type/rank/nr_vars。
2. **0x5e8b80 後、A_REF 前**：原始第0項值及其 heap slot/seq/ref/type；若 VM_STRING，記 ptr/ref/size/有限 bytes。
3. **0x5e8b86 後、NEW 前**：A_REF 返回值；若原元素有效非空但結果=-1，集中查 A_REF；若原元素已=-1或已失效，回看 Take 的 PushBack／返回 A_REF／local8賦值與 source local3 的第0項。

這能先區分「array 存的就是 -1」「array 元素 slot 已被釋放」「取值／複製才變 -1」。目前 ASSERT_LOCAL 只記容器 slot，沒有 element/page metadata，尚不足以選定其中一種。

下一個 headless gate 可限定 f36371：1–3個已知非空 string 的 source，真 Take(3)，讀回每個字串、釋放 source 後 result 仍有效、清理後回到 heap baseline。若此 gate 通過，再往 Concat／CreateRandomPersonalities 回溯；本輪不擴修，也不把已保留的 WRAP retain 與此獨立問題混為一談。
