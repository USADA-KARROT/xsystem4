> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 真實 AIN CASTimer 最小生命週期設置

2026-09-20。本文件只做靜態準備及原 AIN metadata／bytecode 解碼，**未執行 VM、遊戲或原 EXE，也未修改 production source**。原 CN AIN SHA-256 為 `beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947`。編譯使用現有 Clang＋pinned libsys4；結果在 `timer-metadata-result.json`、`timer-chain-result.json`，皆 compile/probe exit 0，既有 Debug_SetPartsComment warning 保留。

## 可立即開始的狀態

**唯一 timer global 是 index 15：`time::detail::g_ASTimerManager`，AIN_STRUCT，指向 struct 9。** 使用真實 global page／heap 的所有權建立它，先執行 manager 真 ctor `22325`，再透過正常 VM 建構與銷毀 struct 7。不要同時採用會自動呼叫 ctor 的配置路徑和手動再叫 ctor，否則「恰一次」已被 harness 自己破壞。

| struct | 欄位（0 起算） | 原 AIN lifecycle |
|---|---|---|
| 9 CASTimerManager | 0 timerImpList=array<CASTimerImp>；1 createdFlagList=array<bool>；2 rateTable；3 systemRateTable；4 Rate=float | ctor22325；dtor=-1 |
| 7 CASTimer | 0 handle=int | ctor420；生成初始化421（只有 RETURN）；dtor422 |
| 8 CASTimerImp | 0 preTime；1 passedTime；2 totalTime；3 scaledTotalTime，皆int | ctor=-1；dtor=-1 |
| 233 CASClick | 0 KeyCode；1 keyDownOnObject；2 keyDown；3 firstClickOff；4 trigger；5 keyPress；6 keyDownLoop；7 timer=CASTimer | ctor6365；生成初始化6366；dtor=-1 |

manager ctor22325@`0x9c8c3c` 使用四次 X_A_INIT 初始化成員0–3的空陣列，並把成員4寫成 `0x3f800000`（1.0f）。其間沒有 HLL 呼叫。應優先執行此原 bytecode，而不是假造同名 C manager。timerImpList 必須保留 struct8 的元素型別；createdFlagList 必須保留 bool 型別，不能把四欄全部當無型別的 int array。單純把成員置0也不等價於已執行 X_A_INIT。

## 與現行遊戲路徑的必要區別

現行 `vm.c` 有 `FUNC_FLAG_CASTIMER_MGR=0x04` 和 `FUNC_FLAG_CASTIMER_INST=0x08`，method_call 會轉進 native `cas_timers` 與 CLOCK_MONOTONIC。**正常 production 跑通 timer 不代表這些 AIN timer 函式被執行。** headless fixture 可在初始化 func_flags 後，只清除這兩類攔截旗標以測真正 bytecode；不要移除 production intercept，也不要把 fixture 的效果寫成已改變遊戲路徑。

應記錄旗標改動、函式入口計數和原始 function address，證明實驗確實抵達 f440／f441／f430 等原腳本。manager ctor本身也要避免被name-based interception吞掉。

## 函式與 HLL 最小集合

| fno | 函式 | address／依賴 |
|---:|---|---|
| 420 | CASTimer ctor | `0x1b5f0`；421 → global15 manager.CreateHandle440 → member0=handle |
| 422 | CASTimer dtor | `0x1b630`；global15 manager.ReleaseHandle441(handle) |
| 438 | CheckHandle | `0x1be54`；Array.At(4,66,arg3=1)，false時 system.Error |
| 439 | manager.GC | `0x1bfa2`；刪除尾端連續false，兩個array同步PopBack |
| 440 | CreateHandle | `0x1c120`；Find(false)，有洞直接設true回傳；否則追加imp／bool |
| 441 | ReleaseHandle | `0x1c2cc`；CheckHandle，設flag=false，GC |
| 442 | GetObject | `0x1c338`；CheckHandle後返回timerImpList[handle]，有SP_INC |
| 430 | CASTimerImp.Reset | `0x1bbd2`；preTime=GetTime，另外三個int清0 |
| 431 | CASTimerImp.UpdatePassedTime | `0x1bc3a`；GetTime-preTime，累加total，scaled依rate及Round |
| 432 | CASTimerImp.Get | `0x1bd0c`；先431，再選total／scaledTotal |
| 423／424／425 | CASTimer.Reset／Get／GetScaled | 442取得imp；423→430，424/425→432 |
| 435 | manager.Rate getter | `0x1be1c`；返回member4 float |
| 6463 | math::detail::Round | `0x235828`；純bytecode，不需Math HLL |

HLL索引由原 AIN直接解碼確認：

| library,index,arg3 | 用途 | 可控邊界 |
|---|---|---|
| system 0,30,0 | GetTime() → int | 唯一所需時鐘。綁定 fixture 可控整數毫秒，固定單調遞增；production implementation原本回vm_time() |
| system 0,26,0 | Error(string) → string | 不應正常發生；fixture直接記錯並終止該case，勿回假成功 |
| Array 4,42,1 | Find(createdFlagList,false) | 本輪已修的值版；須接真HLL bridge |
| Array 4,11,2 | EmplaceBack(timerImpList) | 生成struct8，返回wrap<CASTimerImp> |
| Array 4,11,1 | EmplaceBack(createdFlagList) | 返回可寫回的wrap<bool>兩槽reference |
| Array 4,20,1 | Numof(createdFlagList) | 追加後算handle=length-1 |
| Array 4,66,1 | At(createdFlagList,handle) | CheckHandle取得ref bool |
| Array 4,72,1 | Last(createdFlagList) | GC讀尾端ref bool |
| Array 4,24,1 | Empty(createdFlagList) | GC空陣列／迴圈檢查 |
| Array 4,10,1／2 | PopBack(flags／imps) | GC成對收尾，imp要正確release |

完整符號／index／metadata見 `timer-metadata.tsv`。原始opcode和立即參數見 `timer-bytecode.json`；這份只解碼16個短技術函式，不含遊戲劇情或整份AIN。

## 最小驗收順序

1. **初始化與第一個timer A。** clock=1000；建立manager，再經正常VM生命週期建立A。預期handle0、flags=[true]、兩array長度1，imp.preTime=1000，total／scaledTotal／passed=0。ctor420入口恰一次。
2. **時間是真腳本算出的。** clock=1007，呼叫424，應回7；clock=1010再呼叫424，應回10。可將manager.Rate設2.0後用425驗scaled增量，但不要把已累計時間整段乘新rate。Reset423應重新基準並清累計。Get/Reset都要確定未走native攔截。
3. **洞重用。** 再建B，得到handle1；釋放A最後owner，觀察dtor422一次、flags=[false,true]、兩array仍長2；建C應重用handle0且不新增imp。**原f440洞重用分支沒有Reset430**，只設flag=true；不要把「重用自動清時間」寫成script預期。若案例需新基準，明確呼叫Reset423。
4. **尾端GC。** 在A已釋放、B/C活躍情況下釋放C後B，或先B再C，最後flags與imp兩array長度都應0。每個物件的dtor422各一次。注意handle值可以重用，計數要綁物件／配置epoch，不能只用handle當identity。
5. **重複。** 做數十輪建立／釋放，再與manager初始化後的活躍heap／typed page數比較。允許allocator容量保留；觀察活躍owner及可達物件，不以總heap容量當洩漏。

恰一次必須用真實建構／最後owner釋放觸發，加上函式入口trace或fixture-only計數。只直接呼叫f422一次再看到flag清掉，不能證明typed cleanup會自動呼叫它；也不要手動f422後再釋放同物件而製造二次析構。manager在所有timer銷毀之前必須存活，因為dtor依global15存取它。

重用測試刻意保留B，使A留下內部洞；如果每次只建一個再釋放，GC總把尾巴清空，只測到重新append，未覆蓋Find(false)分支。

## 首個應觀察的既有橋接阻礙

只讀production `Array_EmplaceBack` 發現可能的直接阻礙：目前函式對bool／struct皆只 `return new_val`；bool分支new_val=0。可是原f440對bool的EmplaceBack後使用 `X_ASSIGN 2`，期待可寫回的 `(page,index)`。現行ffi對AIN_WRAP回傳走default只push一槽。這是**靜態發現的候選問題，尚未由本子任務執行證實**。應先用真AIN fixture取得失敗與stack shape證據，再針對該HLL return ABI做局部修正，不能偷偷用C重建CreateHandle代替。

Array.EmplaceBack(arg3=2)還依page.array.struct_type選struct，所以保留timerImpList元素metadata很重要。f438和f439使用的ref bool回傳涉及兩槽別名與callee cleanup；這正是生命週期測試要穿過的路徑，不宜用簡化bool stub繞開。

## 可選的 CASClick 測試，不需要GUI

在timer基礎成功後，正常建立struct233：ctor6365→generated init6366，後者NEW CASTimer放member7。CASClick沒有自訂dtor，但釋放最後owner仍須typed cleanup其內嵌timer，因此是「父類別dtor=-1仍需子成員析構」的直接case。

呼叫Init6368(key,false)，避免firstKeyDownCancel狀態干擾。6374 IsKeyPress(onObject)只多一個native依賴：`AFL_IsKeyDown` f6346@0x2314d0 → **IbisInputEngine.Key_IsDown(lib17,index7,arg3=0)**；fixture把它接到可控key-state表即可。它不需建立PartsEngine／renderer或開始view update loop。

固定onObject：第一次keydown→true並Reset timer；clock到起點+349→false；+350→true並Reset；再+49→false、+50→true；keyup→false並清keyPress／keyDownLoop。始終不按鍵應false。這些是6374原script規則；不要讓host input stub自己產生repeat事件。

首輪可略過CASClick，先完成timer與manager的真bytecode生命週期。關閉攔截的headless實驗通過，也不代表現行native timer攔截或整個遊戲輸入／畫面已驗證。

## 實驗開始後的首個失敗：X_A_INIT抹除宣告型別

主代理實際執行headless原script回報：manager ctor22325成功（live=5），隨後timer ctor420→CreateHandle440在`0x1c13c`的`CALLHLL Array.Find (#42,arg3=1)`，因page.a_type=79被第一階段型別guard拒絕。此為主代理動態觀察；本子任務只讀程式碼定位原因。這個失敗比上節EmplaceBack候選更早，不應把兩者混成同一問題。

完整member type樹已另從原AIN解析至`timer-member-types.txt`。型別資訊仍在AIN，損失發生在建立runtime array page：

1. `page.c:init_struct_slot:422–434`原先會檢視`member->type.array_type`，初步建立具體typed空陣列；但manager真ctor隨即DELETE這些初值，再X_A_INIT。
2. `vm.c:X_A_INIT:4896–4900`只取global的`.type.data`或`variable_type()`；後者`page.c:238–267`只回top-level data、outer struc/rank，沒有nested subtype。四個陣列因此都變成AIN_ARRAY(79)。
3. `vm.c:4929–4936`的size=0分支再次建generic79，並把`array.struct_type`設成elem_slots=1。timerImpList需要的struct8身份也因此消失。該分支對非generic具體陣列還一律改成ARRAY_INT，同樣不保留float/string等身份。

最小修補應局限在X_A_INIT取得完整變數宣告，依array_type明確解析單槽primitive／struct元素，再用具體container型別與真正struct id建立page；不能把arg3=1當作bool/int的猜測來源，也不能讓Find取消未知型別拒絕。若共用helper，宜專門傳回完整變數型別描述或X_A_INIT所需的陣列描述，不直接改全VM的variable_type語意。多槽interface/option等仍可保持既有路徑，並明列尚未覆蓋。
