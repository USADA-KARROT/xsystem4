# 角色對話文字：AIN 生成與讀回路徑

研究日期：2026-09-28。範圍限定遊戲 AIN 宣告／指令的唯讀分析，以及公開引擎程式碼。本文沒有遊戲台詞、完整反編譯碼、個人路徑或帳號資訊。原版 EXE 語義由另一份靜態反組譯調查提供；本文不把引擎目前行為當作原版規格。

## 結論與證據等級

1. `CMessageTextView@CreateDrawChar` 沒有 main/event 的文字生成分支。它依每筆 `CMessageText.MessageWindowName` 找出部件編號，讀取既有文字，再串接該筆 `MessageText`。兩種窗的差異發生在訊息加入模型前的 active-window 選擇，及中間是否經過模型清除。
2. 非空訊息的主要讀回 API 是 `Array.At#1(ref array<hll_param>, int)`，呼叫標記為 2。此路徑沒有 `Array.Get`、`Array.First` 或 `String.GetPart`。不能僅由函式名稱猜測文字切割出錯。
3. `CreateDrawCharList` 開始會先把所有顯示中的文字窗清成空字串。因而 main 的 `SetMessageWindowText bytes=0` 本身不代表原始 MSG 為空：必須區分清除呼叫與真正的內容提交呼叫。
4. 引擎修正前，`Array.Free` 將已知型別的頁設成 NULL；`Array.Clear` 換成無原型別的整數頁。接著 `EmplaceBack`／ffi 以整數陣列回傳兩槽 writable reference，和 `CMessageText` 呼叫點期待的一槽物件不同。已用真 AIN HLL 宣告和真 ffi 合成非空文字 fixture 證實。這項 headless 結論已驗證；最終 36 模式與正式 150 秒 GUI 結果見同目錄 README.md；本文件保留調查時的證據層次。

## MSG 到模型

| 階段 | 函式／位置 | 確認行為 |
|---|---|---|
| VM 入口 | `src/vm.c` 的 `_MSG` | 先輸出 MSG 診斷；AIN 的 msgf 無有效正值時找到 `message`，傳入訊息索引、總數、文字參照。MSG 日誌不代表後續 UI 已收到內容。 |
| 腳本入口 | f6971 `message`，dump 約 L418054 | 先呼叫 f7184，再呼叫訊息 delegate 或預設 f6922。 |
| 字串替換 | f7184 `ReplaceText`，約 L427945 | 對 replace-pair 陣列使用 `String.Replace`；沒有 GetPart。 |
| 預設傳遞 | f6922 `AFL_Message_Message` → f7131 `message::detail::Message` | 檢查 active window，必要時選預設窗；呼叫模型 f7033。 |
| 加入模型 | f7033 `CMessageTextModel@AddMessageText`，約 L421241 | `Array.EmplaceBack` tag 2 建立項目；設定 Type=0、窗名、MsgNum、TextOrigin，組合樣式標記後將內容放到 field 4。 |

`CMessageText` 是 struct 259，七欄為 Type、MessageWindowName、TextOrigin、MsgNum、MessageText、Voice、VoiceFilterName。f7033 的文字組合使用 VM 的 `S_ADD`／`S_MOD`／`S_ASSIGN`，沒有逐字 GetPart 迴圈。

f6980 `S` 使用 `(0, 0, string)` 呼叫相同 `message`。f6994 可以改寫訊息 delegate；本次靜態搜尋沒有在遊戲呼叫端找到對應 override 的直接呼叫，但 delegate 實際是否為空應由執行記錄判斷，不以靜態缺席當 runtime 證明。

## 模型到繪字清單

`CreateDrawCharList` f7048（約 L422207）先呼叫 f7053 Clear，再取得 f7037 Numof，從末尾反向找 Type=2 的 C 標記。找到時先用 `bEffect=false` 生成前段、FixAll，再以傳入的 bEffect 生成後段。這是在模型項目層級分段，不是依字元長度切字串。

重要 label：反向掃描 0x25c292；找到標記 0x25c3fe；掃描結束 0x25c51e。

`CreateDrawChar` f7049（約 L422314）逐項呼叫 f7038 GetMessage：

- f7038 透過 `Array.At#1` tag 2，非空時組成 option `(object, 0)`，空參照組成 `(-1, 1)`。null 分支 0x25b5ba，返回 0x25b5c8。
- f7049 的 Type=0 路徑透過 f7112 將 field 1 的窗名轉成部件號碼，再呼叫 `PartsEngine.GetMessageWindowText`。
- bEffect=true 時直接串接 field 4；false 時包在 time=0 標記之間。
- 0x25c7fe 之後呼叫 `PartsEngine.SetMessageWindowText`，並設定 field 2 的 TextOrigin。
- Type=1 處理語音；Type=2 本身不提交文字。讀到 null option 會跳到 0x25ca4e，這可以出現「Numof 非零、迴圈完成，卻沒有任何非空 SetText」的現象。

f7055 Draw 會在非 skip 時呼叫 f7048(true)；f7056 DrawAll 呼叫 f7048(false) 並 FixAll。因此分析文字消失時要記錄 f7048／7049／7038，而不只看渲染器。

## 四個 SetMessageWindowText 呼叫點

| 函式 | dump 行 | 內容來源 |
|---|---:|---|
| f7049 CreateDrawChar | 422432 | 既有文字 + 模型 field 4，真正的內容提交 |
| f7053 Clear | 422713 | 固定空字串，對所有 showing window 清除 |
| f7054 ClearMessageTextAndSetHome | 422747 | 固定空字串，單一指定窗清除 |
| f12841 CMessageWindowParts@Text::set | 606100 | 一般文字窗屬性 setter 的參數 |

GetMessageWindowText 有三處：f7049（422393）、f12839（606117）、f12840（606136）。

## 清除後型別遺失

f7029 `CMessageTextModel@Clear`（約 L421039）先為既有訊息更新已讀狀態，然後 `Array.Free` tag 2。此函式唯一的 `PUSH 7029` 方法呼叫位於 f7142「所有訊息窗隱藏」（約 L426840）。

另一路 f7146 指定窗文字清除呼叫 f7030 `ClearMessageText`，以 ShallowCopy／Where／EraseAll 按 field 1 篩選，然後 f7056 DrawAll。這條和 Free 的全清路徑應分別 trace。

修正前公開引擎流程：

1. `Array_Free` 釋放元素／page，將 `*array` 設為 NULL。
2. ffi 的 `AIN_REF_ARRAY` 分支取出 NULL 並傳給 HLL，沒有重建宣告型別。
3. `Array_EmplaceBack` 遇到 NULL 以 `AIN_ARRAY_INT` 建立一元素頁。tag 2 不含 CMessageText 的 struct ID，不能單靠 tag 重建原型別。
4. `alloc_page` 僅設定 page type／index／count；其 array union metadata 可能保留 page-cache 舊值。不能以一次碰巧為 struct259 當作型別正確。
5. ffi 的 `AIN_WRAP` 特別處理 EmplaceBack，看到整數頁便回傳 `(array_slot, index)` 兩槽。f7033 等待的一槽 struct owner 因而失配。

### 真 ffi before 證據

在 6855c2f，新增 `dialogue-model` fixture，命令為 `before-check.sh 6855c2f dialogue-model`，退出 86，sanitizer 訊息 0。before 工具已還原 HEAD 的 src/include，當時 dirty=0。

| 測試 | 清除前 | 清除兩次後 |
|---|---|---|
| Free → EmplaceBack | 回 1 槽，array type17，struct259，At#1 回同物件，非空合成文字可讀 | 回 2 槽，array type14，殘存 struct259，語義失敗 |
| Clear → EmplaceBack | 同上 | 回 2 槽，array type14，struct0，語義失敗 |
| 清除結果 | — | Numof=0、Empty=true，兩次均正確 |
| 清除前元素的外部 holder | append／At 驗證參照計數 | 清除後仍 refs=1，合成文字保持，兩次均正確 |

fixture 在解讀錯誤回傳型別前直接記錄失敗，避免把陣列當 struct 而 crash。首次 fixture 開發曾直接寫入 v14 尚為 null 的 string field，已改為正確初始化後重跑；正式 before 證據是上表的 san=0 結果。

後續 fixture 另補 typed int/string metadata、string holder，以及 null／v13 compatibility guard。已直接讀取 `logs/verify/dialogue-typed-clear/summary.txt` 與 `dialogue-model.txt`：35 個模式符合預期、sanitizer=0、`VERDICT PASS`；以上新增斷言全部 PASS。這份記錄仍是同個 6855c2f 基底加當時未提交修正，不代表已提交／推送。

### typed empty 的查詢相容性

將 NULL 改為 typed empty 後，查詢 API 不能比舊 NULL 路徑更嚴格。獨立檢查發現三個入口原先會在零元素頁先驗證型別／callback：`Array_FindValueRange`、`Array_CountIf`、`Array_FindIfRange`。這可能觸發無必要的 unsupported struct／invalid callback VM_ERROR。新 guard 應僅讓空集合提早回 -1／0／-1，不改非空集合的比較或 callback 語義。

已在 fixture 的每次 Free/Clear 後加四個真 ffi 斷言：range Find(value)=-1、range Find(predicate)=-1、Count(predicate)=0、Numof(predicate)=0；predicate 使用空 callback `(-1,-1)`，要求 VM steps 不增加、堆疊平衡、外部物件 refs=1。這批查詢斷言已由最終 `dialogue-array-lifetime` 的 36 模式驗證通過。

三入口關聯宣告的完整統計（直接 CALLHLL）：

| 宣告 | 參數形狀 | tag 次數 | 合計 |
|---|---|---|---:|
| Count | ref array | 1:12；2:45；65538:52；65539:14；196610:5 | 128 |
| Count#1 | ref array, hll_func | 2:3；65538:9 | 12 |
| Numof | ref array | 1:62；2:297；65538:136；65539:24 | 519 |
| Numof#1 | ref array, hll_func | 2:3；65538:4 | 7 |
| Find | ref array, hll_param | 1:2；2:11 | 13 |
| Find#1 | ref array, int begin, int end, hll_param | 1:9；2:3 | 12 |
| Find#2 | ref array, hll_func | 1:1；2:25；65538:42；65539:4 | 72 |
| Find#3 | ref array, int begin, int end, hll_func | 無直接 CALLHLL | 0 |

上述所有宣告回 int。Count#1 與 Numof#1 進同一 CountIf；Find／Find#1 共用 FindValueRange；Find#2／Find#3 共用 FindIfRange。fixture 採 range 形狀覆蓋底層入口，即使 Find#3 在此 AIN 沒有直接 callsite，其實際宣告仍存在。

## HLL 宣告及全 AIN 呼叫形狀統計

以下數字是所有直接 CALLHLL，tag 是該指令第三欄，不是 struct ID 的直接斷言。

| API 宣告形狀 | 呼叫數依 tag | 合計 |
|---|---|---:|
| `Free(ref array<hll_param>) -> void` | 1:16；2:93；65538:57；65539:11 | 177 |
| `Clear(ref array<hll_param>) -> void` | 1:3；2:5；65538:7；65539:1 | 16 |
| `EmplaceBack(ref array<hll_param>) -> wrap<?>` | 1:1；2:18 | 19 |
| `At#1(ref array<hll_param>, int) -> ref hll_param` | 1:16；2:125；65538:110；65539:16 | 267 |
| `First#1(ref array<hll_param>) -> ref hll_param` | 1:4；2:18；65538:20；65539:3 | 45 |
| `First#3(ref array<hll_param>, hll_func) -> ref hll_param` | 1:1；2:38；65538:57；65539:11；196610:1 | 108 |
| `Numof(ref array<hll_param>) -> int` | 1:62；2:297；65538:136；65539:24 | 519 |
| `Numof#1(ref array<hll_param>, hll_func) -> int` | 2:3；65538:4 | 7 |
| `String.GetPart(ref string, int) -> string` | 0:16 | 16 |
| `String.GetPart#1(ref string, int, int) -> string` | 0:76 | 76 |
| `String.Replace(ref string, string, string) -> string` | 0:8 | 8 |
| `PartsEngine.SetMessageWindowText(int, string, int, string, int, int) -> void` | 0:4 | 4 |
| `PartsEngine.GetMessageWindowText(int) -> string` | 0:3 | 3 |

At 有兩份相同宣告，呼叫只使用 At#1。First 有兩份無 predicate 及兩份有 predicate 宣告。Array.Get 沒有宣告，也沒有 CALLHLL。String.GetPart 兩個宣告目前按宣告形狀選擇實作；本次主要文字流沒有呼叫它，不應把 GBK 字元議題混入這組。

## Runtime 診斷下一步

1. 以 f7033 記錄 msg index、內容 bytes、窗名長度／hash、模型 slot；不輸出台詞。
2. 在 Free／Clear 前後記錄 array slot、type、struct_type、rank、size。
3. f7038 記錄索引、回傳 object、option tag；f7049 記錄 Type 與窗名 hash。
4. SetMessageWindowText 記錄 caller fno、parts number、bytes，區分 f7053／7054 的清除與 f7049 的內容。
5. 視窗 owner 與 render gate 用專用 runtime trace 比對。前段 event 900028 的成功渲染不能當成 main 900040 已正常。

root 已回傳的 runtime 摘要指出 f7033 收到 main 非空資料、Numof 非零，但 f7038 回 null option；此處是跨代理證據摘要，本文未直接重跑 GUI。修正後仍須對 main 分別確認非空內容提交及 framebuffer，而不是僅看 MSG 88 的門檻。
