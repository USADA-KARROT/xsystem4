> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# v14 GUI startup / native CASTimer 唯讀審查

日期：2026-09-24。範圍限目前 `work/stage2/source`、既有 CN AIN 與反組譯；未修改 production、未啟動遊戲。本報告是靜態觸發條件與取證建議，不是本輪 GUI 失敗原因的實測認定。root 回報第一個 baseline 在 AppKit 註冊階段約 0.6 秒 abort，尚未進入 VM，故不能拿該次結果驗證以下問題。其後 GUI 工具啟動遭拒絕，本輪 GUI 驗收未完成；以下內容作為手動短測後的取證目標，本輪不因此修改 production。

來源 SHA-256：
- vm.c：`d96ac12974c52322d8076d299abc8150c00560156f6bbffb35859140e776be2c`
- page.c：`5b3867a9ada93762055a9e6a675a3775399890d838cd48a47511aa8489a68064`
- heap.c：`cbd00c2cc4454973d3a4573bf51da5e806dbb4b915af01b53c7e09d1eddf528f`
- CN dohnadohna.ain：`beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947`

## 最值得先取證的三點

### 1. 已有明確的全域物件「參數建構後，再預設建構」路徑

`vm.c:5365` 在 alloc f22322 執行時設 `vm_in_alloc_phase`。但 `NEW`（`vm.c:2445` 起）只把 ctor=-1 視為 alloc-only；顯式 ctor>0 仍執行。隨後兩輪 global 初始化（`vm.c:5412` 起）呼叫 `init_global_struct_v14`（`page.c:541`），沒有記錄該物件是否已被 alloc 的顯式 NEW 建構。`ginited` 只防兩輪 global pass 互相重複，無法防 alloc→global pass 重複。

原始 CN alloc 的線性 body 有 76 個 NEW，其中 30 個帶顯式 ctor；這個 body 無 JUMP/IFZ/IFNZ，第一個 RETURN 在 0x9c8a60。以下不是僅靠名稱推測：

| 全域欄位 | alloc 的實際 NEW | 後續 STRT 預設 ctor | 可觀測差異 |
|---|---|---|---|
| global241 BattleBackground::Size，struct518 CASSize | 0x9c878e，f20926，參數 2560、1024 | f20924 | 後者把 Width/Height 設 0；預期原有 2560×1024 被重設為 0×0 |
| global243 BattleBackground::CameraCenter，struct515 CASPosF | 0x9c8848，f20862，X=Size.Width/2，Y=BaseY-220 | f20860 | 後者把 X/Y 設 0 |
| global245 BattleBackground::ScreenCenter，struct515 CASPosF | 0x9c8892，f20862，兩個 FLOAT 參數 | f20860 | 後者把 X/Y 設 0 |

這是目前 startup 最具體的錯置路徑；尚未實測 GUI 是否走到受影響場景。不要直接把所有全域 ctor 全部刪掉：alloc 的 ctor=-1 分支確實需要後續初始化，而且某些重複 ctor 可能只是冪等賦值。例如 CASColor 的預設 ctor 只設 Alpha，不能據此宣稱它清空 RGB。

建議最小取證：在 alloc 返回後、global ctor pass 完成後，各記錄 globals 241/243/245 的 slot、heap seq、page type/index、欄位值；同時限定追 f20926→f20924、f20862→f20860 是否對同一 slot/seq 執行。若一致，即可分開修「哪些實例已建構」，而非重新推翻所有初始化。

### 2. GUI 的 native CASTimer 並未被第二階段 real-bytecode timer 驗收覆蓋

headless timer fixture 清除了 CASTimer flags，跑的是原始 AIN；正常 GUI 的 `init_func_flags`（`vm.c:256` 起）仍標記 CASTimerManager 全部 methods，以及 CASTimer Get/Reset/GetScaled。RCASTimer 被刻意排除。native intercept 在 `method_call` 的一般 struct 驗證之前（`vm.c:1382,1401`）。

可直接由目前程式確認的差異：

- CreateHandle f440 只遞增 `cas_next_handle`；ReleaseHandle f441、CheckHandle f438、GC f439、Manager ctor f22325 等未專門處理的 Manager 方法直接回 0，原始方法 body 不執行。因此 GUI 中 global15 CASTimerManager（struct9）的陣列與 Rate 欄位未必經過原 ctor 初始化；native 支援的常用途徑可能暫時掩蓋這件事。這與真 AIN headless 測試的 ctor/dtor 計數不能混為一談。
- Rate getter f435/f436 與 setter f437 的真 AIN 型別都是 FLOAT。native 卻以 `static int cas_timer_rate=1`、`ret->i` 與參數 `.i` 傳遞（`vm.c:314,345,353`）。未被 setter 改寫前，返回位元 0x00000001，而不是 float 1.0 的 0x3f800000。setter/getter 可能保留後來寫入的浮點位元，但預設值仍錯。
- GetScaled f425（`vm.c:384` 附近）回傳未乘 Rate 的 elapsed，並非腳本的完整縮放契約。
- native table 2048 格，handle 只在 1..2047 初始化；instance 以 abs(handle)%2048 索引，0 又改成 1。第 2048、2049 個 handle 都會撞到 timer1，Release 又沒有使舊 epoch 失效。因此反覆建立 timer 達門檻後可能讀到舊 epoch。這是條件性故障，不能宣稱目前短測已達到。
- native table、next_handle、rate 沒有 VM reset 路徑。正常每次獨立 process 啟動會重新初始化；同 process 重啟 VM 才有累積疑慮。
- 函式尾端「constructor reset epoch」註解不能當保證：CASTimer ctor420、generated421、dtor422 沒被 INST flags 選中；Manager methods 又在前段 return。正常 CASTimer ctor 實際仍跑 bytecode，再呼叫 native CreateHandle。

應限定記錄 f440/f441/f423/f424/f425/f435/f436/f437 的 native 分支：this slot/seq/type、handle、timer_id、next_handle、active、epoch、返回值 raw bits/FLOAT；先確認第一個等待是否使用這條路，再決定移除或修補攔截。沒有證據前不要把 Release/Rate 問題算成 9/24 ownership 修改造成的退步。

### 3. 實際 slot 重用使舊的「有效 slot 就是正確物件」假設更危險

9/24 修正讓 refcount=0 的 VM_PAGE 真正回收並重用。這不會自行製造 dangling reference，但會使既存 stale slot 從「仍指舊頁」變成「指另一個活物件」。

- native CASTimer instance 讀 field0 前，只檢查 slot 範圍、union.page 非 NULL、nr_vars>0（`vm.c:1403`）。沒有先驗證 heap.type==VM_PAGE、STRUCT_PAGE、struct index==7；若 stale slot 被重用成 string 或別種 struct，可能錯誤解讀 union 或讀取其他欄位。handle<=0 分支還會寫回 field0（`vm.c:1417`）。
- post-alloc global repair（`vm.c:5380`）驗 heap/page tag，未驗預期 struct index。deep repair（`vm.c:5473` 起）也不是 generation 檢查；錯誤別名若恰好落到另一個 STRUCT_PAGE，仍可能被接受。
- `init_global_struct_v14` 依宣告的 struct members 迭代，未核对實際 page index/nr_vars（`page.c:541` 起）；若前述錯類型物件進入此處，可有欄位誤讀。
- 普通 AIN_STRUCT 欄位現在初值為 -1（`page.c:427` 起），Wrap<struct> 保持 eager allocation。這讓 CASClick generated ctor 的 DELETE -1→NEW CASTimer→X_ASSIGN transfer 成為一次正確建構；不能再以所有普通 struct 欄位都應預先有活頁的舊假設判斷「缺物件」。本次 AIN metadata 抽查未找到「沒有 ctor 但有普通 struct 欄位」的 global，因此沒有證據說這項 null-field 修正本身破壞全域物件。

應用 (slot, seq, heap.type, page.type, page.index) 一起取證；只有 slot 數字重複不足以認定 stale alias。CASClick f6365/f6366、CASTimer f420/f422 可用来確認同一 child seq 是否 ctor/dtor 各一次。普通欄位仍 -1 時，先追父 ctor/generated body 是否執行，勿自動補一個新 struct 以掩蓋原因。

## 日誌盲點與本輪有限建議

現有 STAGE2 function trace 在 execute_instruction 才把 pending 改 active。被 native intercept 的 method 在第一個 instruction 前就 function_return，故通常完全沒有該 fno 的 enter/return；沒有 f440/f441/f424 log **不代表沒有呼叫**。`XSYS4_TRACE_FNO` 另有 SDL_GetTicks()>42000 的硬門檻（`vm.c:5084`），不適合 startup。

一般 bytecode startup 可設：
```
XSYS4_STAGE2_TRACE=1
XSYS4_STAGE2_AFTER_MS=0
XSYS4_STAGE2_PER_FNO=4
XSYS4_STAGE2_FNOS=20926,20924,20862,20860,6365,6366,420,422,22325
```
不要設定 `XSYS4_STAGE2_FROM_MSG`，避免等 MSG 才開始記錄。native 額外取證需要 root 加有限 branch log 或用 debugger；此唯讀審查未加診斷。

執行順序建議：
1. 先確定正式 desktop launch 已進入 VM，保留首個 VM alloc/main notice；AppKit 註冊失敗不算 VM 測試。
2. 先做 globals 241/243/245 的兩個初始化邊界快照；這是最小、明確的 startup 檢驗。
3. 若仍有立即跳過等待或 timer 異常，再抓有限 native handle/Rate 分支；不要以 real-bytecode harness 通過推論 native shim 也正確。
4. 只有出現實際錯類型/seq 不符才追 stale slot；保留 9/24 已通過的 transfer、typed-empty、正常回收契約，不先恢復洩漏來掩蓋別名。

第二階段 observer/timer/selfClear 的零殘留驗收仍成立於其精確測試路徑；本審查沒有將它擴大成正常 GUI、全部初始化或完整遊戲相容性的驗收。
