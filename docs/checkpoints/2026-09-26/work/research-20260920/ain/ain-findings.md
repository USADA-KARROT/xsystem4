> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# AIN 腳本可替換性與最小 v14 核心契約

2026-09-20。只讀現有 CN／JAST dump、Stage 2 原始碼與既有紀錄；另以固定 libsys4 解析原 CN AIN，沒有啟動遊戲、修改引擎或分析商業 EXE 邏輯。分析程式與 JSON 僅寫入本目錄，不保存完整 AIN 或劇情表。

**結論：等待、點擊重複、timer 管理和 observer 決策大部分已有 AIN 腳本可讀，重寫核心不必重新猜測這些遊戲邏輯。真正需補齊的是 VM 的型別、閉包、引用生命週期，以及 native HLL 的精確契約。** 不需要沿用 xsystem4 每一個內部資料結構，但必須保留 AIN 可觀察的效果。只讓字幕出現或強制完成等待，不算相容實作。

## 資料身分與規模

原 CN AIN 是 `work/stage2/game/dohnadohna.ain`，SHA-256 `beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947`。root 的原檔 probe 與本子調查的 dump 統計相符：

| 項目 | CN 原檔／dump | JAST 現有 dump |
|---|---:|---:|
| AIN | 原檔 version 14 | 本輪未解析原始 JAST AIN |
| 腳本函式 | 37,742 | 37,737 |
| struct/interface 宣告總數 | 1,210 | 1,210 |
| globals / delegates | 263 / 1,779（原檔） | 本輪不另推算 |
| HLL library / 函式宣告 | 36 / 1,701 | 36 / 1,701 |

root 原檔全 CODE 靜態解碼為 10,626,832 bytes、2,040,196 指令位置、99 種 opcode；1,562 個不同 HLL 函式有直接 CALLHLL 引用。這是靜態存在的呼叫，不是實際必用功能清單，也不是執行次數。PartsEngine 宣告868個、Array84個；新引擎不應因開場只碰到一小部分，就假設其餘不需要。完整統計在 `../probes/surface-summary.json`。

CN dump README 標示繁中1.01／GB18030；JAST標示英文1.02／CP932。**兩份 libraries.txt 完全同 SHA-256**：`95402d7f3969b812927ed9cdeae12915f512724eec2e882e25ff7b2cd32589d3`。支持共用宣告驅動的 HLL 介面；但這是既有 dump 的相同，尚不是兩份 EXE 實作完全相同的證明。

## 五個深讀函式與真實鏈

`chain-bytecode.json` 保存從原 CN AIN 解碼的地址、opcode 和立即參數；`dump-evidence.json` 保存五個技術函式的符號化指令、來源行號、必要型別與兩版配對。dump 中 `.LOCALREF` 等是展開前巨集，不能拿其行數當真實指令數。獨立 CFG probe 不追入 CALL 目標，追蹤條件分支、JUMP 與 DG_CALL 完成分支，避開內嵌 lambda 定義；所選函式沒有 SWITCH。

| CN fno / 起始地址 | 簽名與已確定行為 | 新核心契約 |
|---|---|---|
| 6374 / `0x232616` | `bool CASClick@IsKeyPress(int nOnObject)`：首次按下立刻true並Reset timer；同一物件持續按住後，先等350ms、之後每50ms再次true；放開清除press／loop狀態。 | 真實按下／放開狀態與時間來源須一致；不能以每幀重複Click代替。350／50ms 是腳本常數，不需native另造重複規則。 |
| 20752 / `0x4a598c` | `void CObserver@Execute()`：若IsEnd或IsEnable=false便返回；設local endObserver=false，把它的reference傳給notify delegate，完成後寫入自身IsEnd。 | ref bool 必須是可寫回caller local的別名；callee清理不得銷毀仍在執行的caller frame。 |
| 20761 / `0x4a5d14` | `void CObserverManager@UpdateEvent(int passedTime,int scaledPassedTime)`：遍歷m_list並Execute，之後移除第一個IsEnd observer，若集合為空就取消BeginUpdateEvent。 | foreach的容器／元素reference要正確處理；`Erase(predicate)`是一次first-match刪除，不可全部刪除，也不可把predicate當index。 |
| 27031 / `0x595558` | `void Motion::ExecuterCollection@Join(array<string> sections)`：清空字串、Concat／Unique section；已有join時直接返回；否則設local isFinish=false，註冊捕捉該frame的observer，建立InputDisabler並更新迴圈直到isFinish。 | callback必須持有正確receiver與lexical environment；更新迴圈是腳本控制流程，native應提供更新／輸入服務，不能硬編完成Join。 |
| 36081 / `0x59568a` | Join的`void lambda(ref bool End)`：先EraseEndTask與IsEndWaitSection；條件成立後EndWaitForClick、Free join sections，同時寫captured isFinish與ref End為true。 | 同一次callback必須讓兩個不同frame的bool均可見，且返回後兩frame仍在合法生命週期內。 |

关键原始指令位置：

- `20752: 0x4a59e4 DG_CALLBEGIN [248]`；`0x4a59ea DG_CALL [248,0x4a59fa]`。delegate248宣告為 `void DG_Observer_NotifyHandler(ref bool End)`；原檔參數佔兩槽：`AIN_REF_BOOL(51)`＋`AIN_VOID(0)` companion。表面的一個bool參數不代表只搬一槽。
- `36081: 0x5956ea X_GETENV`；之後取environment索引2（Join的isFinish）；`0x5956f8 X_ASSIGN [1]` 和 `0x5956fe X_ASSIGN [1]` 依序寫captured local及ref End。不是只需將回傳值設為true。
- `20761: 0x4a5e7e CALLHLL [4,16,65538]`，即 `Array.Erase(ref array, hll_func)`；先foreach Execute，後呼叫predicate移除。對照 `27034 EraseEndTask` 則是 `Array.EraseAll`。兩者不可互換。
- `27031` 的 Array calls 包括 #18 EraseAll、#26 Any、#12 Concat、#61 Unique；型別上下文立即值是2。observer集合呼叫的上下文為65538。必須結合完整宣告／元素型別理解，不能只按HLL名稱或參數個數綁定。

點擊釋放另核對 `6371 CASClick@IsClick`：按下時記錄keyDown和物件；放開後才回報click，且指定物件時必須相同。因此第一次「按住不放」和完整「按下→放開」不能等價。

## timer 與銷毀：不只是執行自訂 destructor

原 CN struct metadata 新確認：

- CASTimer（struct7）constructor=420、destructor=422；f420@`0x1b5f0`向manager.CreateHandle，f422@`0x1b630`呼叫manager.ReleaseHandle。f441@`0x1c2cc`檢查handle、清createdFlagList，再呼叫manager.GC。這些是 AIN 腳本。
- CASTimerImp（struct8）包含 preTime/passedTime/totalTime/scaledTotalTime；CASClick（struct233）member7是CASTimer，CASJoyClick（struct236）member8是CASTimer。
- **CASClick、CASJoyClick、CObserver的自訂destructor均為-1。** VM仍需依成員型別清理巢狀timer／delegate等資源；沒有自訂destructor不表示沒有釋放工作。CObserver@2 f20750是舊delegate DELETE後DG_NEW的生成初始化，不能因名稱@2把它當析構函式。
- CObserver（struct508）有兩個delegate欄位及IsEnd bool；從Array移除最後一個owner後，closure、捕捉環境與內含資源不能永久滯留，亦不能在callback執行途中提早釋放。替代核心可採不同memory策略，但必须證明這些觀察等價，並保障slot／handle重用不讓舊引用指向新物件。

Stage 2既有watch證據已實際見到「callback成功寫End=1，但callee清理把caller page最後引用釋放」；這支持caller生命週期是必要契約。本輪仍是靜態研究，不另宣稱整個ownership模型已驗證。

## 腳本／native 分工

腳本提供：click／repeat策略、timer handle管理、observer建立／更新／結束、Join決策、motion的條件、InputDisabler作用域。VM提供：CALLMETHOD、DG_CALL、X_GETENV、引用／wrap解析、成員初始化、frame及typed value清理。HLL由原Windows執行環境提供宣告對應的native實作，例如Array查找刪除／concat／free、Delegate.Empty、PartsEngine更新／事件佇列與輸入查詢、system.GetTime／Peek／Sleep／ResumeSave。

36個HLL名稱是AIN的library命名空間，不代表必然存在36個可獨立替換的DLL。單看AIN宣告不能確定每個函式在EXE內如何實作；也不能把宣告當作實際ABI完整規格。root另有EXE代理做原生入口交叉查證，本文件不替該分析預先下結論。

## CN 與 JAST 共用範圍

五核心均找到JAST對應且opcode序列形狀相同；所列timer/click/observer六種struct的成員宣告和id也相同，observer delegates均247／248。這支持同一個宣告驅動的v14核心，搭配版本各自的文字編碼、函式表和資產索引。

不可共用硬編ID：Join callback是CN36081、JAST36075；UpdateEvent的predicate是CN25697、JAST23866。CN同號36081在JAST是其他模板函式；而前段20752／20761／27031仍同號。函式表有5項總量差異，但中途偏移不限於5，不能整表做固定加減。Opcode shape相同不代表常數、呼叫目標、地址與所有語意完全相同；本輪沒有驗证JAST原AIN或完整遊戲行為。

## 保存的是VM狀態，不只是遊戲變數

AIN宣告 `system.ResumeSave`／`ResumeLoad`；現有libsys4 `savefile.h:156` 的rsave包含IP／函式內offset與CRC、value stack、call frames、return records、heap物件、next_seq、函式名稱表。xsystem4 `resume.c:335–405` 實際把這些寫入格式。

既有Stage 2 `<memory>` dump的只讀統計提供實例：save version9、2,097,152個heap位置、28個call frames、19個stack值、37,742個函式名稱；heap含local、string、array、struct、delegate。這是xsystem4生成的現有證據，不是本輪讀取原Windows玩家存檔的結果。

新核心若要求既有Resume存檔相容，必須能重建呼叫點、別名／閉包及物件identity，不能只載入人物狀態。也可明確選擇新格式與不支援舊檔，但那是產品範圍改變；不能將「新核心能從頭運行」宣稱為原版存檔相容。PartsEngine sidecar等native狀態亦需另驗證，VM snapshot不自動涵蓋所有native物件。

## 能決定是否替換核心的有限驗證

先讓新核心使用原AIN的上述真實函式／型別，並提供可控時間、按鍵、更新事件和精確Array/Delegate服務：按下→349ms→350ms→後續50ms→放開；同時建立Join observer、callback雙重bool寫回、Array first-match移除／最後unsubscribe，最後釋放和重建timer／observer，反覆重用slot。驗收要求：結果與腳本推導一致、每次CALL/DG_CALL stack平衡、callback期間caller存活、scope結束後活躍物件數回落、timer destructor恰執行一次、stale reference不命中新物件。

這個驗證不需要CG或完整遊戲啟動，卻直擊Stage 2暴露的問題。通過後才接回現有資源／顯示層走標題→兩頁→存讀檔；若仍需要按函式ID硬補、強制完成或永久保留頁面，就沒有證據表明換核心比整理現有VM更可靠。

## 重跑與限制

- `python3 work/research-20260920/ain/probe_dumps.py`：只讀兩份既有dump，輸出dump-evidence.json；如root原AIN surface存在則附其metadata。
- `python3 work/research-20260920/ain/run_chain_probe.py`：使用目前既有normal-build的固定libsys4，編譯本目錄probe_chain.c並只解碼原AIN；輸出chain-bytecode.json與chain-probe-result.json。這不是執行引擎或遊戲測試。
- 初次編譯缺CoreFoundation link，已補實際依賴；parser stderr含既知Debug_SetPartsComment名稱junk warning，已保留，不當作新錯誤或忽略證據。最終compile/probe皆exit0。
- 任何dump符號化、opcode形狀或此處邏輯推導都不等於完整原EXE語意。沒有原版同步trace、完整HLL契約及保存往返前，不能宣稱新runtime已可相容。
