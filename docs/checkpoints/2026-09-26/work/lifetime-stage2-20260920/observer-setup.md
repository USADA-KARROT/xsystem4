> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 真實 Observer／Join callback 的最小 headless 設定

2026-09-20。只讀原 CN AIN、既有技術反組譯與 production VM/page 程式；沒有修改引擎或執行遊戲。原 AIN SHA-256：`beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947`。下列 ID／address 專指此檔，其他版本應由名稱和完整型別解析。

**先執行 `vm_call(20752, observer_slot)`，讓原始 Execute bytecode 自己透過 delegate 呼叫 f36081。** 不要直接用 C 改寫 notify 結果，也不要給 Execute 的 local page 額外測試用保活引用，那會遮蔽原來的 caller 提早釋放問題。

## 最小實際執行鏈

```text
20752 CObserver.Execute
 ├─ 20746 IsEnd.get
 ├─ 20753 IsEnable → Delegate.Empty（空 enable delegate => true）
 └─ DG_CALL type248 → 36081 Join notify(ref bool End)
     ├─ 27034 EraseEndTask → Array.EraseAll → 36086 → 27001 IsAlive.get
     ├─ 27033 IsEndWaitSection → Array.EraseAll → 36084
     │   └─ Array.IsExist(predicate) → 36085 → 27005 → 26981 → 27098
     └─ 條件 true 時：7879 → 9197、Array.Free、兩次 X_ASSIGN
    返回 20752 → 20748 IsEnd.set
```

此鏈只需真實 HLL `Array`（library4）#18 EraseAll(predicate)、#57 IsExist(predicate)、#24 Empty、#6 Free，以及 `Delegate`（library10）#3 Empty。它不需要建立視窗、timer、PartsEngine 更新或實際點擊。

**`AFL_Parts_EndWaitForClick` 並不呼叫 PartsEngine HLL。** f7879@`0x2ae83e` 轉呼 f9197@`0x2ec344`，後者只把 globals[2] `parts::detail::g_EndPartsBusyLoop` 設 1、globals[3] `g_EndPartsBusyLoopNumber` 設參數（本 callback 傳 0）。可先把 number 設 12345，成功後應變 0。

最早 smoke test 可令 collection 的兩個陣列皆空，直接驗證 true 分支、delegate ref bool 寫回與全域變數。接著才加入下述一個 motion，測 false→true，避免一次引入過多依賴。

## 必需型別與狀態

以下成員 index 以原 AIN metadata 核對，包含被 dump 隱藏的 VOID companion。

| AIN struct | 欄位與所需 seed |
|---|---|
| 508 `task::detail::CObserver`（3 members，ctor/dtor=-1） | [0] empty delegate247；[1] notify delegate248；[2] IsEnd=false。 |
| 612 `Motion::ExecuterCollection`（2 members，ctor37657，dtor=-1） | [0] 一維 `array<wrap<Executer>>`，含一個 motion；[1] 一維 string array，含技術字串 `probe`。 |
| 611 `Motion::Executer`（10 members，ctor/dtor=-1） | [0] MotionSet608；[1] IParts 的 heap slot；[2] interface metadata=0；[5] m_isFinish=false。其他欄位使用 production 型別初始化，不填隨意有效 slot。 |
| 608 `Motion::MotionSet`（2 members，ctor26983，dtor=-1） | [1] SectionParam617；[0] m_param 可為空。 |
| 617 `Motion::SectionParam`（3 members，ctor36091，dtor=-1） | [0] Name string=`probe`；[1] ExecuteType、[2] DisableInput 本鏈不讀。 |
| 389 `IParts`（0 members，ctor/dtor=-1） | 可配置有正確型別的空 shell 作為 motion[1]。這條鏈只檢查是否 -1，不進行 interface method dispatch；這不代表 shell 能代替真實 Parts。 |

`27001 IsAlive` 先取 motion[1..2]，丟棄 metadata，若首槽為 -1 就 false；否則回傳 `!motion[5]`。不要只改 m_isFinish，卻讓 m_parts 留在 -1，否則第一次 EraseEndTask 就會移除 motion。

以 `alloc_struct(611)` 可取得 production 初始化過的 nested608／617，再在既有欄位設定 Name 與 IParts。`alloc_struct` 不等於執行所有 constructor；本有限測試以 seed 已存在物件狀態為界，不宣稱 ctor 路徑通過。覆寫原有 owning 欄位前，依 production ownership 釋放其原值；新增 owner 引用時對應 retain，避免 fixture 自己製造 leak。

陣列必須保留宣告對應的一維、logical element／實際 slot 數；motions 的這個 WRAP 是 **一槽 object reference**，不是 `[object,metadata]`。實際 bytecode 的 Array type context 為 `65538`，joinNames 為 `2`。不要把 `array.struct_type=611` 誤作 stride611；production stride 也取決於 generic type context。

## captured environment 與 delegate 建立

f27031（Join）@`0x595558` 有四個 local：

| index | 型別／意義 |
|---|---|
| 0 | array<string> sections |
| 1 | bool isWaiting |
| 2 | bool isFinish |
| 3 | InputDisabler606 |

建立真實 `LOCAL_PAGE(index=27031,nr_vars=4)` 作 capture environment，使用 `variable_initval` 設定每個 local；[2]=0。此最小 subchain 不建構 InputDisabler，故 [3] 保持合法的 -1；不要配置它再讓 teardown 跑需要 Parts 的 destructor。可以把 [0] seed 為獨立的 `['probe']` array，使狀態更接近 Join；f36081 只直接讀寫 environment[2]。

notify delegate 的三槽是 `[collection_slot, 36081, environment_slot]`。使用 production `delegate_new_from_method_env`／heap allocation 建立 page，但注意這個 helper **本身不 retain**：原始 `DG_NEW_FROM_METHOD` opcode 另行 retain environment 與 receiver，因此 seed 若要模擬相同所有權，須保留這兩份引用，並單獨記錄 harness 持有的引用。把 delegate 裝入 observer[1] 時轉移或新增 ownership，不要雙重計數。

f20750@`0x9e9204` 是 observer 生成初始化方法，可 `vm_call(20750,observer_slot)`，用真正 DG_NEW 建立兩個空 delegate，再替換 notify；它不是 destructor。只做初始化無 HLL。collection ctor37657@`0x9eac42` 只包含 array init，無直接 HLL，但本鏈可使用 production allocation 的空陣列狀態再填入元素，不必執行完整 Join。

不要以 `vm_call_nopop(36081,2)` 當完整 observer 驗收：該入口的 env 尋找規則來自呼叫 stack，未必是指定的 Join environment。經 observer 的 `DG_CALL` 才覆蓋 delegate triple 的 env 取值、ref bool 參數複製及返回清理。

## 兩輪的預期結果

1. 初始 motion[5]=0、有效 IParts 首槽、section name 與 joinNames 相同。呼叫 Execute：EraseEndTask 保留 motion；IsEndWaitSection 看到同名活 motion，保留 joinNames；notify 返回。應保持 observer[2]=0、env[2]=0、globals[2]=0、globals[3]=12345，兩陣列各有一個 logical element。
2. 由測試 driver 將同一 motion[5] 設 1（此動作模擬 motion 已完成，不取代 callback）。再呼叫 Execute：EraseEndTask 透過真實 predicate 刪除 motion，IsEndWaitSection 刪除已無對應 motion 的 join name；notify 呼叫 EndWaitForClick、Free names，寫 env[2]=1 與 ref End=1。應得到 observer[2]=1、env[2]=1、globals[2]=1、globals[3]=0；motion／join arrays 都 empty。空 array 可能以有效 heap slot＋NULL page 表示，不應要求欄位固定等於 -1。
3. 第三次 Execute 應由 IsEnd early-return；不得再觸发 callback。可由 bytecode step count／有限 trace 驗證，不把額外 busyLoop 寫回當作證據。

## 保證 caller 存活的觀察點

f20752@`0x4a598c` 只有 local[0] `endObserver:BOOL`。於 `0x4a59e4 DG_CALLBEGIN[248]` 前，stack 參數為該 **Execute local page 的 slot 與 index0**。delegate248 的實際宣告為 `REF_BOOL(51), VOID(0)`，notify36081 同樣兩個實體參數槽。

f36081 的關鍵片段（僅技術指令）：

```text
PUSHLOCALPAGE; PUSH 0; X_REF 2       # 取傳入的 [Execute local page, 0]
PUSHLOCALPAGE; X_GETENV; PUSH 2      # Join environment[2]
PUSH 1; X_ASSIGN 1; X_ASSIGN 1; POP
```

原地址為 X_GETENV `0x5956ea`、兩次 X_ASSIGN `0x5956f8`／`0x5956fe`。第一個 assignment 寫 captured isFinish 並保留賦值結果；第二個再寫 caller 的 End。觀察應包括：

- callback 期間 Execute local page 保持有效且仍屬 f20752；callee 持有的 reference 清理後，不得把仍在 stack 的 caller page 釋放。
- delegate 回到 `0x4a59fa` 時 caller local[0]=1，後續 f20748 參數也為1，最後 observer[2]=1。
- Execute 正常返回後，其 local page 可釋放；stack depth／sentinel、call_stack depth 應回到呼叫前。不要因為返回後 page 已釋放就誤報 UAF。
- 在 #include vm.c 的 harness 可啟用現有 `stage2_trace_enabled=true`、`stage2_trace_msg_ready=true`、`stage2_trace_watch_fno=20752`，使用既有 X_ASSIGN／free watch。這是有限診斷；若需硬斷言中途值，於 harness 的執行觀察點檢查，不給該 caller 額外 heap_ref。
- 同時檢查 environment 身分與引用數；只有 env[2] 正確不足以证明 caller local 安全。反之只有 observer IsEnd 正確也不足以證明 environment capture 正確。

## 清理與完整 Join 的下一道門檻

目前 production `variable_type(DELEGATE_PAGE)` 回 AIN_VOID；`delete_page_vars` 因而不會對 delegate triples 執行 obj／env unref，而 DG_NEW_FROM_METHOD 曾 retain 二者。這是本次靜態可見的 ownership 不對稱。**不可在 fixture teardown 手工補 unref 之後，宣稱 production delegate 的 closure lifecycle 已通過。** 可釋放明確屬於 harness 的 owner，並分開報告最後仍被 delegate 保留的 receiver／env 引用及 reachable graph。若修復此點，重複建立→兩次 Execute→刪 observer 的回圈才有可比較的 live-page 基線。

完整 f27031 還會：EraseAll空 sections、Any／assert、Concat／Unique、AFL_Observer_Set 的 manager/event 註冊、InputDisabler ctor 26963 建立前後兩個 rect 並設定輸入，再進入更新迴圈，最後銷毀 InputDisabler（26967→EnableInput）。其 cleanup delegate36082 會 EndSection。這需要實際 Parts object、interface vtable、event/update 與其他 HLL；不能把本 subchain 的通過提升為完整 Join 或點擊流程通過。

本文件提供的是可執行測試設計與靜態契約；真正通過／失敗以 root 的 real VM harness 結果為準。
