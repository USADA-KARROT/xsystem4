> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# v14 delegate ownership：production source 有界審查

2026-09-20。只讀 `work/stage2/source`，沒有改 source、沒有執行舊 EXE。本報告是靜態因果與修正建議；真實 observer callback/refcount 結果由主代理另行驗證。

## 結論

**已確認 ownership 不對稱，不只是懷疑。** `DG_NEW_FROM_METHOD` 在 v14 retain object / environment，但 `DELEGATE_PAGE` 的欄位型別仍全部回傳 VOID，因此 copy 不 retain、delete 不 release；erase / clear 也只搬移／清零數字。另有独立資料格式錯誤：append 的第三欄仍寫 pre-v14 sequence，而 v14 call 將它當 environment slot；plusa 也丟失來源 env。

不能單独加 release，否則現有未 retain 的 copy、append，以及 callback 執行時借用的 obj/env 會轉為 premature free。最小安全修正是一組 v14-only ownership 配套；pre-v14 保持既有 `(obj,fun,seq)` 弱引用。

## 已確認路徑與缺口

| 操作 | production 現況 | 後果 |
|---|---|---|
| `DG_NEW_FROM_METHOD` (`vm.c:4447`) | v14 `heap_ref(env=local_page)`；obj>0 時 retain obj；obj==-1 時改為同一 local_page 再 retain 一次 | 建立的 tuple 已實際持有兩條 edge；obj==env 時是同一 slot 的兩份 ownership |
| `delegate_new_from_method_env` (`page.c:1100`) | 只寫 tuple；v14 第三欄=env，pre14 第三欄=seq；helper 本身不 retain | caller-dependent ownership，不可把所有 helper 建出的 entry 當已 owned |
| `DG_SET` / `DG_ADD` (`vm.c:4270/4294`) | 走 new/append helpers，没有與 NEW_FROM_METHOD 相同的 retain | 目前是 borrowed tuple，不能只統一 destructor release |
| `variable_type(DELEGATE_PAGE)` (`page.c:275`) | 每格皆 `AIN_VOID` | `delete_page_vars`→`variable_fini`不 release obj/env；`exit_unref`也不遍歷這兩欄 |
| `copy_page` (`page.c:368`) | 每格 `vm_copy(value,VOID)`，即數字複製 | `DG_COPY`、`DG_ASSIGN`、v14 `A_REF`複本没有新 ownership，原本 leak 掩蓋了此問題 |
| `delegate_append` (`page.c:1122`) | 新增第三欄一律 `heap_get_seq(obj)`；不 retain | v14 env 被填入無關 seq；後續加 env release 會誤減另一個 heap slot |
| `delegate_plusa` (`page.c:1192`) | 傳入只含 obj,fun 的 append；來源第三欄被丟棄 | 兩個 closure context 不能可靠保留 |
| `delegate_erase` / `delegate_clear` (`page.c:1171/1227`) | 只搬移 tuple 或歸零 nr_vars | 即使 entry 原本有 retain，也没有配對 release |
| `delegate_call` (`vm.c:1571`) | obj/env 借用到 call frame，env 取第三欄 | callback 清除自己後，如開始正確 release entry，執行中的 frame 可能失去最後一份引用 |

### 為何 copy 是確定缺口

`vm_copy` (`vm.c:950`) 的 `AIN_VOID` 經 default 原值返回；相反地 `AIN_REF_TYPE` 會 retain 同一 slot，`AIN_STRUCT` 會 `vm_copy_page` 深複製。故把 delegate obj/env 改成 STRUCT 型別会錯誤複製閉包環境，破壞 observer 對原 local page 的 alias；不能這樣修。

`copy_page_shallow` (`page.c:350`) 也依 `variable_type` 決定 retain；目前 `type_is_heap_ref` 的列舉和數值 fallback 50..79 不包含 `AIN_REF_TYPE`，若採用下述最小 metadata 方案，這裡也要一致處理，或让 v14 delegate fast-path 在進入 recursive depth-limit 前完成。

### 為何 callback frame 必須納入

`keep_alive_enabled` (`vm.c:1019`) 明確只適用 v6.1..v13，`set_struct_page` 不對 v14 retain obj。`env_page` 只有數字欄位，`function_return` / `unref_call_frame`目前也没有為 v14 env 配對 ownership。

例如 delegate 是 object/env 的最後 owner，callback 內執行 Clear：entry 正確 release 後，仍在執行的 `PUSHSTRUCTPAGE` / `X_GETENV` 會指向已釋放 slot。這是「補 release」直接導致的風險，而非一般性的假設。應只為 **v14 delegate call frame** 增加 obj/env pin，保持目前一般 method call 的 pre14 keepalive 政策不變。

## 最小安全修法（不改 pre14）

### 1. 統一 entry 持有規則

僅 v14，tuple 每個有效 obj/env edge 各持有一次引用；若 obj==env，仍是兩次 retain / 兩次 release，因為現在 NEW_FROM_METHOD 也是如此。`fun` 是純數字，絕不能 retain。0/-1 為空；reserved globals slot 的處理由既有 heap API 保持。

把 retain 集中在 `delegate_new_from_method_env` 與新增的 env-aware append helper；`DG_NEW_FROM_METHOD`保留 obj==-1→local_page 的選擇，但移除它外面的重複 retain。不要只加 constructor retain 而保留 opcode 既有 retain，否則每次建立又多 leak 一輪。

為 v14 `delegate_append` 的預設 env 明確使用 0，不能寫 heap sequence。需要從另一個 delegate 複製 entry 的路徑必須把來源 env 傳過去；`delegate_plusa`使用 env-aware append。只有成功新增 entry 時才 retain，duplicate/no-op 不加引用。

### 2. metadata + copy/delete 必須同時一致

碼量較小的方案：

```c
case DELEGATE_PAGE:
    if (ain->version >= 14 && varno >= 0 && varno < page->nr_vars)
        return varno % 3 == 1 ? AIN_VOID : AIN_REF_TYPE;
    return AIN_VOID;  // pre14: object + sequence remain weak/non-owning
```

這讓現有 `vm_copy(AIN_REF_TYPE)` 保留同一 obj/env slot 並 retain，也讓 `variable_fini` / `exit_unref` 配對 release。另補 shallow-copy 的 `AIN_REF_TYPE`判定，或採 v14 delegate 專用 fast-path，避免 depth fallback 漏 retain。

替代是完全不改 variable_type，另做 delegate-specific copy/delete/exit helper；但需同步所有 delete_page_vars、exit_unref 與 copy paths，範圍較大。不要只在一般 `free_page`加 release：GC、cache、deferred deletion 等呼叫它的語義不同。

### 3. erase/clear release，先 detach 再釋放

Erase 先保存被移除 tuple 的 obj/env，完成搬移與 nr_vars 更新，再 release 兩個 edge。Clear 先 snapshot 原 tuples，將頁面設為空，然後 release snapshot；第二次 Clear 是 no-op。Release 可能經 destructor 重入，因此不能先 release 還掛在頁面上的 entry，或在可能間接銷毀頁面後繼續讀它。

Delete 也應防重入：`delete_page`目前先把 `heap[slot].page=NULL`才`delete_page_vars`，這是可利用的既有保護。單純正常 erase/clear 則需自行先 detach。

`DG_ASSIGN`已先 copy 再 delete destination，方向正確；補足 copy retain 後 self-assignment 仍應可保護 obj/env。測試不能只驗證 tuple 數字相同，要驗證同一 slot 與 refcounts。

### 4. active callback 独立持有 obj/env

在 v14 `delegate_call`快照所選 obj/env，為進入的 frame retain；一般 RETURN、scenario/unref_call_frame、timeout / exit unwind 均對稱 release。可加明確 frame ownership 欄位以區分其它 borrowed env 使用者；不要單靠 env_page>0 就一律 unref，因為 `vm_call_hll`也會借用 parent env。

`_function_call`以 designated initializer 清空未指定欄位，可使新增 ownership fields 預設為 false/0。若 pin 在 `_function_call`前取得，失敗分支也要 release；frame pop 後以 snapshot 釋放，避免 destructor 重入把現行 frame 指標改掉。

### 5. 保留的邊界

- `delegate_contains` / erase 現在以 obj+fun 比對、不含 env；`Delegate_Equals`卻逐格比較。不同 env 的相同 obj/fun closure 是否應 deduplicate 屬另一個 identity 契約，這輪未用原生證明。不要把改善 ownership 測試當成此語義已確認。
- `Delegate` HLL Set/Add 目前只給 obj=-1、func，沒有保存 HLL callable 的 context。這是獨立的 callable ABI 風險；直接 DG_NEW_FROM_METHOD observer 成功不能替它背書。
- pre14 的第三欄 seq 不能被 retain、release 或標成 heap ref；seq garbage collection / weak method entries 保留現狀。
- resume loader 直接載入 tuple 和既存 saved refcounts (`resume.c:743`)；不能在 load 時再盲目 retain 一次。舊 v14 snapshots 可能已保存不一致的 seq/env 或 refcounts，這輪不承諾兼容遷移。

## GC 與殘餘限制

`heap.c:139` 的 `gc_collect_cycles=false`，`heap_gc_periodic`直接 return。新增正確的 pair release 可修正非循環 ownership，不會自動解掉 `local env → delegate → same env` 的強引用環。不要把高 refcount 一律判成此修法失敗，也不要以 GC 會補救作為不配對 release 的理由。

若未來重新啟用 cycle collection，`gc_scan_page(DELEGATE_PAGE)`目前只掃 tuple[0]，v14 還必須掃 tuple[2]；call-stack root marking 也漏 env_page。目前不建議為本階段測試順便打開 GC。

## 建議驗證項目

1. obj與env不同：建立 +1/+1；drop delegate 回 baseline。
2. obj==env：建立 +2；drop 回 baseline。
3. copy / DG_COPY / A_REF：複本是不同delegate page，但 obj/env 是同一slot，各多一份ownership；刪原本後複本可正常call。
4. plusa保存不同來源env，成功追加增加ref；duplicate no-op不增加ref。
5. erase一次、clear一次、clear兩次、delete，各只釋放實際owned entries。
6. callback內Clear/Erase本身，仍能在後續instruction讀obj/env；frame return再回baseline。
7. pre14第三欄放一個恰好等於valid heap slot的seq，copy/clear/delete不能變更那個slot的refcount。
8. cyclic env案例獨立記錄，別混入無環應回baseline的驗收。

## 讀取版本

靜態審查結束時的 SHA-256，後續主代理實作可能改變檔案與行號：

| 檔案 | SHA-256 |
|---|---|
| src/page.c | `07fcea7e45518dff76bde4fe18f1318a4a2f3f237831d4e9614e1f5ea6f13f7f` |
| src/vm.c | `b2484f765564d2cc412ff26f70071e2643bd7a1cce129f6ad993922e05004041` |
| src/heap.c | `f80013691446559208b618b42959641cba7ce8d2a41b95b1c1da319506f4d8e0` |
| src/hll/Delegate.c | `f10a7bc77a3972d73a8f74bcb76c407647445cf9c312f0fb7b73a93827bab5cb` |
