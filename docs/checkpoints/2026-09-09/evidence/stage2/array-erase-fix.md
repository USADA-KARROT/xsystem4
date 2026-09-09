# 第二階段：Array.Erase 重載修復及觀測依據

2026-09-09。這份變更沒有啟動遊戲或 UI；整體建置與對話驗收由主執行者進行。

## 可重現的錯綁

`src/ffi.c` 的 `link_static_library` 原本只按函式名稱綁定。實際中文遊戲 AIN 的 Array 庫包含以下三個都叫 `Erase` 的宣告；反組譯裡的 `#1/#2` 是區別同名宣告的表示法，不是 runtime 需要剝掉的字串後綴。

| 實際庫函式索引 | 返回型別 | 引數 |
| --- | --- | --- |
| 15 | bool（47） | ref array（80，inner=74）、int（10）、int（10） |
| 16 | bool（47） | ref array（80）、hll_func（95） |
| 17 | bool（47） | ref array（80）、wrap（82）<array（79）> |

這些數字由 fixture 用現有 libsys4 `ain_open` 讀取 `work/stage2/game/dohnadohna.ain` 驗證。修復的 dispatch 使用宣告本身的 argc／型別，不使用這三個索引或遊戲 fno。

原 export 僅有 `void Array_Erase(struct page **array, int index, int length)`，因此 predicate 宣告收到同一個 C 函式。observer 的 callback fno 被當成 index，通常直接因超出陣列範圍返回，既不執行 predicate，也不移除 observer；C prototype 的參數數與返回型別同時不符實際宣告。

## 本輪 trace 如何支持這個方向

`runs/abi-ffi-check/engine.log` 這輪由 runner 在 120.268 秒停止，設定只有 20 秒的單次開始 hook；最終記到 MSG 2–32，沒有新增 ASan／UBSan error 訊息。仍有 `Personality.jaf:27` assertion、X_ASSIGN clamping 及 WAV -1，不能寫成整體無錯。

- 19.167 秒的 IsAlive=1、motion_finish=0、36086=0；13 個 motion 到第一次 Join 時仍存在。19.208 秒 Section 與捕獲 name 都是 len5、hash24d471a9，36085=1、36084=0、IsEndWaitSection=0。這證明這段觀测中 Section 匹配與「仍需等待」的結果合理，不能把集合從開始就瞬間清空當根因。
- 同時 36081 連續使用三個已完成 Join 的 env（local isFinish=1）與一個現行 Join（isFinish=0）。25.985–25.986 秒連續四次由 36081 發出 EndWaitForClick；26.038 秒還有相同來源。這是完成 observer 沒有及時消失的具體線索。
- 遊戲的 CObserverManager.UpdateEvent（AIN 841388 附近）確實呼叫 Array.Erase 的 predicate 重載，predicate 25697 讀 observer.IsEnd。既有綁定使這條清理沒有正常運作。
- 第一段文字前仍有正常長度的動畫 Join：19.206→25.987 秒、27.056→40.299 秒、40.403→45.840 秒、45.847→51.108 秒。不能把慢了一些當成對話等待已修復。
- MSG 2/3 之後，真正 message ExecuteKeyWait 進入的 9196（caller=7878、caller2=7061）在 51.122→51.127 秒只有 5 毫秒即返回 0。後續零 motion 的 Join 也只 5–6 毫秒；對話仍自走。
- 額度在 19 秒階段已耗盡，沒有錄到 51.122 秒等待內部究竟哪個 caller 發出 EndWait。因此 observer 錯綁是已確認缺陷且符合症狀，尚不能憑本輪宣稱它是所有對話問題的唯一根因。新增 FROM_MSG 設定正是為下一輪驗證這件事。

計時器方面，19.208 秒的前八個 task.Update 是八個不同 receiver、m_time 都剛為 0，這不是同一 timer 反覆不前進的證據。19.308 秒觀察到部分 RCASTimer AddTime 將 4511 改成 4531，manager 後續 delta 也有 20、30、4、3、2 等。可以排除「所有 RCASTimer 都完全停住」，不能證明每個 task 的 timer 身分與註冊完全正確。本次沒有改 timer。

## 實作

- `src/hll/Array.c` 新增 `array_erase_function`，只接受可辨識的 Erase 宣告型別。
- `src/ffi.c` 只在 `link_static_library` 的 Array.Erase 綁定位置呼叫 selector，保留原有 ffi_cif 建立流程。已先與修改 FFI 呼叫上下文的 agent 協調，沒有改動其工作區段。
- 索引範圍版本改為回傳 bool；仍刪除指定範圍、保留多槽索引換算，先夾限長度再乘 stride，避免超大 length 算術溢位。legacy void-return 宣告仍能綁定，C 返回值會由 ffi 丟棄。
- predicate 版本刪除**第一個**匹配項目，保持順序。不能直接用 EraseAll 取代；Rufim 的同名修復也明確區別兩者。每次 callback 使用既有 vm_call_nopop，保留 VM stack，再讓 range helper 釋放被刪除的 array 引用。
- wrap-array 版本按來源集合做差集；既有 AIN 的用法是選取的 emitter IDs 減去一組 IDs、addedSkills 減 current。int／物件以值或身分比對，string 以內容比對，避免相同文字但不同 heap slot 無法刪除；來源與目標是同一 page 時安全清空。
- 兩槽元素以 logical element 的第一槽釋放 array 引用，第二槽保留原始 metadata 語意；source tuple 比對也只允許第一槽做 string 內容比對。這與既有 PushBack／Where 的 ownership 相符。舊 range 實作在 stride 展開後逐槽 unref，本來就可能把 interface 的 vtable offset 誤當 heap slot；新的 predicate/source 路徑會增加觸發機會，因此本輪一併修正 Erase 區域。
- 未辨識的 Erase 宣告不再用不相容 prototype 執行；selector 回傳 NULL，linker 保持 fun=NULL，實際呼叫時會由既有 `hll_call` 印出 `UNIMPL HLL: Array.Erase (args=..., cnt=...)`（前五次、之後每百萬次），並按宣告清除引數、回傳既有預設值。這是有診斷的未實作路徑，不是 fail-fast；也沒有默默 fallback 到 range prototype。沒有對其他 Array overload 做擴大改動。

wrap-array 的 set subtraction 語意由上述 AIN 呼叫點判讀，尚未和原版 Windows 引擎逐一對照所有特殊型別或重複元素組合。本 fixture 覆蓋本遊戲可見的整數與字串情境。

## 驗證與限制

`validation/check_array_erase.py` 從目前 production Array.c 抽取未改寫的 selector／實作，使用真 libffi 呼叫、真 AIN 宣告與小型 page/heap/observer fixture。

- 同一 `[alive, ended, ended, alive]` 樣本：改前 range binding 無 predicate callback、沒有刪除；改後每次只刪除第一個 ended，alive 保留，第二次清掉另一個 ended，第三次回傳 false。
- 被刪除 heap 物件的 array 引用各釋放一次，存活物件不被釋放；callback 後 stack 恢復。
- range bool ABI、負 index、零 length、超大 length、刪空及 null array 通過。
- wrap-array 的整數差集、來源不變、source=self、不同 heap slot 的同內容字串、無匹配通過。
- 新增兩槽 range／predicate 樣本，第二槽刻意使用有效 heap slot 號碼：同一樣本先驗證舊實作確實額外 unref metadata；新實作只 unref 第一槽，存活元素與第二槽不受影響，callback 收到完整兩槽引數。
- 新增兩槽 source 樣本，兩個不相等的 metadata 數字刻意對應內容相同的 live string：仍判定不相等；只有 metadata 完全相同才移除，並只釋放物件第一槽引用。
- legacy void range 與未知宣告拒絕綁定通過。
- production Array.c、ffi.c 單檔 `-fsyntax-only` 通過；`git diff --check` 通過。
- fixture 以 AddressSanitizer + UndefinedBehaviorSanitizer 執行，全部 PASS。macOS 此 runtime 不支援 `detect_leaks=1`，測試改用 0；未聲稱 LeakSanitizer 通過。

結果與來源 SHA256 在 `validation/array_erase_result.json`，生成的 C 在 `validation/array_erase_fixture.c`。fixture 的 VM callback 是「讀取 observer ended」的替身，不是完整遊戲 VM、renderer 或實際點擊流程；對話仍需下一輪 FROM_MSG=2 的 normal／ASan 實測確認。

## 有界 ownership 審查留下的範圍

本次僅修 Erase 的已確認錯綁與兩槽移除。既有 `Array_EraseAll`、`Array_PopBack` 等仍有按 physical slot 處理的程式；這些不是本次新增，而且尚未針對目前遊戲路徑驗證，需後續 Array 泛型操作審查。不能把這份兩槽 fixture 通過寫成整個 Array 庫已全面相容所有 interface／option 情境。
