> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# Array 第一階段：Numof / Count / Find 的原生契約

2026-09-20，唯讀分析既有 `dohnadohna_dump_SCY.exe`，SHA-256 `211ce63e0e229fa7d77b2af141149681ec37521e7497914473c739569aa101f0`。沒有執行 EXE、沒有修改原檔或 source。地址為 image base `0x400000` 的 preferred VA。AIN index / 型別來自 `../research-20260920/probes/cn-surface.json`；dump 與現用 protected EXE 的來源限制仍適用，不能當成原廠 source 或已完成動態驗證。

## 可以直接用於實作的結論

1. **Numof 與 Count 是 alias**：#20/#22 同指 `0x64461c`；#21/#23 同指 `0x644660`。後一對計算 predicate 為 true 的元素數，並非無條件 array length。
2. **Find 的 range 是 `[begin,end)`，不是 begin/count**。起點取 `max(begin,0)`，終點取 `min(end,length)`；沒有加法、負索引不從尾端計算。命中回原始 array index，未命中回 -1。
3. **value 與 predicate 必須不同分派**，兩者使用不同 comparator 工廠。值版只確認支持 int / enum / bool / float / string；struct 等型別走原生 error 分支，不能用 heap slot 數字相等冒充通用語義。
4. **ref primitive 的值會解參照**：原生讀 `(page,slot)` 的內容作比較，並非比較 page/slot 編號。

## 分派與原生入口

| AIN index | 宣告 | switch 分支 | helper |
|---:|---|---|---|
| 20 / 22 | Numof(array) / Count(array) | `0x64461c` | array interface `vtable + 0x0c` length |
| 21 / 23 | Numof(array,predicate) / Count(array,predicate) | `0x644660` | `0x648010` |
| 42 | Find(array,value) | `0x6449f7` | `0x648a80` → `0x648b00` |
| 44 | Find(array,begin,end,value) | `0x644a2d` | `0x648b00` |
| 46 | Find(array,predicate) | `0x644a95` | `0x648c40` → `0x648cc0` |
| 48 | Find(array,begin,end,predicate) | `0x644acb` | `0x648cc0` |

所有返回值均以原生 VM type 10 (int) 寫回。Pair alias 是 jump-table 相同地址的事實，不只是程式看起來相似。

## Numof / Count predicate

`0x648010` 先保存初始 length，令 `count=0,last=-1`；接著反覆從 `last+1` 到初始 length 尋找下一個 true 的位置，命中時 count++，找不到則回 count。因此在 array/predicate 沒有修改 array 的正常情況下，每個元素依原順序被檢查一次，回傳命中元素總數；空陣列回 0，完全未命中回 0。

它與 Find(predicate) 共用 comparator 工廠 `0x6461e0`、共用 first-match loop `0x646bb0`。Predicate callable 在 `0x646200` 組裝元素參數，執行 VM callable，將結果轉 bool，再清理暫存值／argument list。不能以數字值是否非零代替呼叫 predicate。

初始 end 會固定在當時 length；每次 first-find helper 又以當時 length 夾住 end。若 predicate 改動 array，原生行為比簡單 `for(i < current_length)` 更細；本階段不能聲稱已完整驗證刪除、追加、釋放等 reentrant mutation 情境。建議一般測試避免把這些未確認行為寫成契約。

## Find range / 空值邊界

Range dispatcher 的 VM arguments 以 0x28 bytes stride 存放：array=arg0、begin=arg1、end=arg2、value/predicate=arg3。`#44` 和 `#48` 將 arg1 放到 EDX，arg2 直接作為 end 傳入；共用 `0x646bb0` 的可讀邏輯是：

```c
int first_match(array, int begin, int end, comparator) {
    int i = max(begin, 0);
    int stop = min(end, array.length);
    for (; i < stop; ++i)
        if (comparator(i)) return i;
    return -1;
}
```

`#42/#46` 不帶 range 的 wrapper 提供 begin=0、end=length。Predicate-range helper `0x648cc0` 還會在有效 array 長度 <= 0 時直接回 -1；value helper 在迴圈 end=0 時得到相同結果。

| 有效 array 條件 | Find 預期 |
|---|---|
| 長度 0 | -1；不呼叫 predicate |
| begin < 0 | 從 index 0 開始 |
| end > length | 最多查到 length-1 |
| end <= 0 | -1 |
| begin >= length | -1 |
| begin >= end | -1 |
| match 在 index end | 不命中，end exclusive |
| range 內命中 | 回原陣列 index，非 range-relative index |

例如 array `[7,1,7,9]`：Find(begin=1,end=2,value=7)=-1；Find(1,3,7)=2；Find(-4,2,7)=0；Find(3,99,9)=3；Find(3,2,9)=-1。這些例子由已讀迴圈直接推導，並非本次執行商業引擎所得測試結果。

### NULL pointer 不等於有效空陣列

`0x6473b0` 只做 pointer null guard；非 null 即 true。NULL 時呼叫 `0x6946a0` report/helper，若該 helper 返回則 guard 回 false。Numof/Count 外層回 0，無 range 的 Find wrappers 在這條路徑也回 0，predicate-range helper 同樣回 0；value-range 沒有相同的前置 guard。

本輪沒有證明 report/helper 是否會拋錯／終止，也沒有證明 host 的「未配置 array」一定等同這裡的 NULL。因此可以明確承諾有效空陣列 Find=-1，**不可把 NULL 路徑的回 0 擴張成一般語義，也不要為複製錯誤分支而把合理的空陣列處理改壞**。

## Value comparator / ref slot

`0x646020` 依 array element type 建立比較器，type byte map `0x64618c` 與 jump table `0x646178` 可直接核對：

| 原生 element type | comparator call operator | 行為 |
|---|---|---|
| 10 int / 92 enum | `0x64bcd0` | 讀元素與傳入值後整數 `cmp` / `sete` |
| 47 bool | `0x64bc60` | 兩側透過 bool accessors，再比較 bool |
| 11 float | `0x64bbe0` | `ucomiss` + ordered equality；+0=-0、NaN 不相等 |
| 12 string | `0x64bb10` | 解析字串後 `0x64a520` 比長度與內容，不是 handle identity |
| 其他 | `0x6460c4` | error helper 路徑；不宣稱支持 |

Int accessor `0x659f20` 最終使用 `0x65a7c0`，後者針對 argument descriptor 的 type 選擇：

- 10/92：直接讀 descriptor `+0x1c` immediate。
- 18/93（ref int/ref enum）：讀 descriptor `+0x20`、`+0x24` 作為 page / slot，交給 `0x67a100`。
- 74（generic HLL parameter）：按 descriptor flags 分流；`0x10001` 與 `0x30001` reference case 也進上述 page / slot reader，其他 case 使用 immediate／另一格值。
- 82/86 等 wrapper：先取得包含的型別再分流；完整 wrapper ABI 未在此輪重寫。

`0x67a100` 先 resolve page，驗證 slot < page byte size / 4，再讀 `page_data[slot]`。slot 的 unsigned bounds check 也排除負 index。這明確支持 value lookup 對 primitive ref 做解參照，但不能單獨決定目前 xsystem4 的臨時 reference slot / ownership ABI；橋接層要接到現有已驗證的 reference resolver，不能直接搬原生 descriptor offset。

## 證據與重跑

- 新的有界摘錄：[native-evidence.asm.txt](native-evidence.asm.txt)。每段首行保留完整 `objdump -D --x86-asm-syntax=intel --start-address=… --stop-address=…` 命令與來源路徑。
- [Array-jump-table.json](../research-20260920/exe/Array-jump-table.json) 可核對 alias 與索引。
- [array-find-loop.asm.txt](../research-20260920/exe/array-find-loop.asm.txt) 是 range clamp 與 first match 的直接證據。
- [array-find-value.asm.txt](../research-20260920/exe/array-find-value.asm.txt)、[array-find-predicate.asm.txt](../research-20260920/exe/array-find-predicate.asm.txt) 是 wrapper 到 loop 的連接。
- [array-value-comparator-factory.asm.txt](../research-20260920/exe/array-value-comparator-factory.asm.txt) 是 element type 分派。

結論的可信範圍：原生静態結構已確認；本輪沒有執行原版的 differential tests。必要實作驗證應涵蓋 alias、predicate 執行次數、begin/end 邊界、原始索引、值/predicate 分派、ref primitive 和字串內容比較。
