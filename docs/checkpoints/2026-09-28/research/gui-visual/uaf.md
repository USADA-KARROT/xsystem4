# 立繪與名牌不退場：介面參數少一次參照（2026-09-29，`9e30c0f`）

## 結論

- **根因**：v14 呼叫函式時，呼叫端以借用方式傳入介面參數（`AIN_IFACE`，型別 89，兩槽 `[物件, vtable 偏移]`）。被呼叫函式的 local page 在返回時會釋放這個參數（`page.c` 的 `variable_fini`），但 `function_call` 搬參數時沒有替它加參照。一加一減對不起來，每次呼叫物件就淨少一個參照。delegate 路徑（`delegate_copy_argument`）同樣漏了 `AIN_IFACE`。「有值、內容是參照／wrap／介面」的 option 參數也有同樣的缺陷。
- **症狀鏈**：`AdvStand@MoveIn` 呼叫 `Motion::Create(IParts)`，後者再把同一個 `IParts` 交給 `NEW Motion::Executer`。兩次返回各少一個參照，所以立繪第一次登場時，`AdvStand.m_parent` 持有的 sprite 就被釋放了。這個 slot 隨後被重用（多半是字串）。之後 `MoveOut`、`Move`、`IsMotion` 從重用的 slot 讀 vtable，拿到函式號 -1，VM 就略過呼叫（`vm.c`「Invalid funcno: skip call」）。所以立繪不退場、不換位。名牌的 root（`CActivityWrap.m_root`，`CButtonParts`）在 `AdvNamePlate@FadeIn` 經同一點被釋放，`Hide` 因此落空，舊名字留在新名字底下。
- **修法**：照原版的參數複製規則 `0x657430` 補上 retain（見〈修法〉）。沒有加任何「退場就刪除」的特例，也沒有改 -1 的略過行為。
- **驗證**：新 headless 模式 `iface-arg` 在修正前 6/6 失敗、修正後全過；46 個模式在預設與 GBK 組態都 `VERDICT PASS`。150 秒 GUI 修正前後各跑兩次，修正後另在 HEAD 再跑一次：MSG 都是 88，assertion 與堆疊溢位都是 0。framebuffer 上，講完話的角色會退場，名牌不再疊字。`heap_alloc_slot` 警告從每次 15 筆降為 0，峰值 RSS 從 1.76／2.05 GB 降到 1.33／1.38 GB。

## 釋放點

以下取自 headless 探針與 GUI 追蹤。GUI 追蹤用的是臨時 patch，只在設定 `XS4DBG_UAF` 時開啟，未提交。

1. `AFL_Parts_CreateSprite` 建立 sprite，`AdvStand@0` 結束時 ref=1（由 `m_parent` 持有）。
2. `AdvStand@MoveIn` 裡 `CSpriteParts@Core::get` 的 `SP_INC`：ref 1→2（多出的一份在 `<dummy>` 區域變數）。
3. `Motion::Executer@0`（fno 27007）在 ip 0x590df6 `RETURN`：ref 2→1。被釋放的是參數 `parts : IParts`，它進入時沒有加參照。
4. `Motion::Create`（fno 26970）在 ip 0x58fee0 `RETURN`：ref 1→0，sprite 被釋放。C 呼叫堆疊：`heap_unref < delete_page_vars < delete_page < heap_unref < function_return`。
5. `MoveIn` 的 `.LOCALDELETE <dummy>` 打在 ref 0 的 slot 上。`heap_unref` 遇到 ref≤0 直接返回（`heap.c`），所以多出的這次釋放沒有任何訊息。
6. 這個 slot 被重用後，`Move`／`MoveOut` 讀 `<vtable>[vtoff+33]` 得到 -1，VM 略過呼叫。

GUI 追蹤（修正前）顯示 10 組立繪全部在同一個 `RETURN`、同一個參數被釋放。只因介面參數釋放而歸零的物件有：`CButtonParts` 51 個、`CSpriteParts` 24 個。

`heap_alloc_slot: skipped in-use entries` 是下游結果：名牌 root 被釋放後，`CActivityWrap@Root::get` 的 `SP_INC` 把已經在 free list 上的 slot 從 ref 0 加到 1，配置器遇到它就只能跳過，嚴重時會報 free list exhausted／corrupt 並擴張 heap。40 筆 skip 全部可以追回這條路徑。

任務列出的其他可疑位置在追蹤中都沒有參與 ref 的錯誤變化：`X_ASSIGN` 的 v14 所有權、`function_return` 的 WRAP 例外、`X_REF` 對 wrap box 的解包、`.LOCALDELETE`。

## 原版依據

以下是對 EXE 解殼傾印的靜態反組譯，image base 0x400000，只讀未執行。

- `0x657430` 把參數搬進新的函式頁，呼叫點在 0x66a082、0x66a166、0x66a45b。它依變數型別查位元組表 `0x657508`，再經跳表 `0x6574fc` 分派：
  - **加參照後存入**（`0x6574ab` 呼叫 `0x679f10`，即 `inc [page+0x1c]`）：型別 18–21、51、67、80（各種 REF）、82（WRAP）、87、89（IFACE）、93（REF_ENUM）。
  - **直接存入**：0、10–13（含字串 12、struct 13）、47、63（delegate）、79（陣列）、92。這些由呼叫端以 `A_REF` 交出所有權。
  - **其他型別**：回傳失敗。
- option（86）另有處理：用 `0x653420` 取槽數，檢查最後一槽（判別槽）是否為 0。為 0 表示有值，再以 `0x6535e0` 取內含型別後依上表分派；不為 0 時以型別 0 直接存入。`0x6535e0` 會剝掉所有 option 層（86／87）；遇到 wrap 時經 `0x653660` 對應成參照型別（int→18、float→19、bool→51、string→20、struct→21、delegate→67、array→80、wrap→82、option→87、enum→93、iface_wrap→89）。這些對應結果全部屬於「加參照」那一類。
- bytecode 佐證：
  - 呼叫端一律以 `X_REF 2` 借用推送介面參數；暫存值放在 `<dummy>` 區域變數，呼叫後才 `.LOCALDELETE`。
  - AIN 中沒有任何函式自己 DELETE 介面參數。
  - 具名介面區域變數以 `SP_INC` 賦值、之後不做 `.LOCALDELETE`，由 local page 拆除時釋放。
- 引擎內既有的一致做法：`vm_call_nopop` 本來就替 `AIN_IFACE` 加參照；`delegate_copy_argument` 的註解也寫明 callee local page 擁有參照型參數。

## 修法（`src/vm.c`）

- `function_call` 遇到 `AIN_IFACE` 參數時加一次參照。CALLFUNC、CALLFUNC2、CALLMETHOD、NEW 的建構子，以及 `vm_call` 都經過這裡。
- `function_call` 遇到 `AIN_OPTION` 參數時：若判別槽（第 `ss_type_slot_count` 槽）為 0，且剝掉 option 層後的內含型別是 REF／REF_ENUM／WRAP／IFACE，就對值槽加一次參照。空 option 和值型 option 不動；值型 option 在返回時本來就不會釋放。
- `delegate_copy_argument` 加入 `AIN_IFACE`，`delegate_call` 的兩槽參數複製也套用同一條 option 規則。
- 只對仍存活的 slot（`heap_index_valid`）加參照。已釋放的舊值不會因此被救活，也不會被第二次放回 free list。
- 刻意不動的部分：`function_call` 對 STRUCT／DELEGATE／ARRAY 參數仍加參照，但原版不加（見〈另案〉）。拿掉之後，任何以借用方式傳 struct 的路徑都會變成新的 UAF，所以要另寫 fixture 再處理。

影響面：這項修改作用於所有 v14 函式呼叫。依據是原版只有一套參數複製常式，三個呼叫點都用它；而且 AIN 全體的呼叫端與被呼叫端所有權模式一致（上節）。46 個 headless 模式在兩種組態都沒有回歸。

## 驗證

### Headless：`iface-arg`（`harness/probe/iface_arg_fixture.inc`）

每個案例各自 fork 隔離。失敗時會印出 ref 在哪個 opcode、哪個函式降低。

| 案例 | 路徑 | 修正前（`7e16dee`） | 修正後 |
|---|---|---|---|
| IA1 | CALLFUNC `CUserComponentActivity::ReleaseComponent(IUserComponent)` | 借用物件被釋放（ref 0） | ref 不變 |
| IA2 | CALLMETHOD `CUserComponentSet@UserComponent::set`，存進成員 | 呼叫後 ref 1（應為 2）；呼叫端放手後成員指向已釋放物件 | 呼叫端＋成員各一份，釋放 set 時恰好釋放一次 |
| IA3 | delegate `DG_ReleaseUserComponentHandler` | 物件被釋放 | ref 不變 |
| IA4 | 真 `AdvStand` 建構後依序 MoveIn／Move／MoveOut | MoveIn 後 sprite 被釋放：Executer@0 RETURN 2→1、Motion::Create RETURN 1→0 | 每步 `m_parent` 都是同一個 CSpriteParts、ref=1，`vtable[vtoff+33]` 仍解析為 `CSpriteParts@Core::get` |
| IA5 | `CustomerEffect@Personality::set(option<wrap<Personality>>)` | 呼叫後 ref 1，成員指向已釋放物件 | 呼叫端與成員各一份 |
| IA6 | delegate `<ArrayFindFunc@Item&?>(option<wrap<Item>>)` → 真 lambda → `OptionalExtensions::HasValue` | 物件被釋放（其中一次在 `HasValue` 的 RETURN，2→1） | 回傳 true，ref 不變 |

- `before-check.sh 7e16dee iface-arg`：rc=89，6/6 案例失敗，無 sanitizer 報告。修正前後的 commit 各跑一次，結果相同。
- `verify-step.sh`（HEAD `9e30c0f`）：46 個模式 `VERDICT PASS`；`XS4_PROBE_GBK=1` 也是 46 個 `VERDICT PASS`。`deleted-event` 仍為既有的 rc 87。
- headless 調查時的中間組態：只補 IFACE、未補 option 規則時 IA5 失敗；補齊 `function_call`、但缺 delegate 的 option 規則時 IA6 失敗。可見 option 的兩條規則各自有案例覆蓋（這兩組是調查時的結果，本次未重跑）。

### GUI（`gui-run.sh <name> 150`）

修正前用 `7e16dee` 另外匯出建置，修正後用本 checkout。

| 執行 | MSG | assertion | 堆疊溢位 | `skipped in-use` | `free list exhausted/corrupt` | 峰值 RSS |
|---|---:|---:|---:|---:|---:|---:|
| 修正前 1 | 88 | 0 | 0 | 10 | 5 | 1.76 GB |
| 修正前 2 | 88 | 0 | 0 | 10 | 5 | 2.05 GB |
| 修正後 1 | 88 | 0 | 0 | 0 | 0 | 1.33 GB |
| 修正後 2 | 88 | 0 | 0 | 0 | 0 | 1.38 GB |
| 修正後 3（HEAD `9e30c0f`） | 88 | 0 | 0 | 0 | 0 | 1.33 GB |

修正後的警告種類沒有新增；修正前的 `copy_page: limit hit` 也消失了。

以臨時追蹤 patch 各跑 150 秒（修正前的數字取自 GUI 追蹤調查，同一份 patch 套在 `7e16dee` 上）：

| 項目 | 修正前 | 修正後 |
|---|---:|---:|
| `AdvStand.m_parent` 的 sprite 提早釋放 | 10 | 0 |
| 因介面參數釋放而歸零的物件 | 75 | 0 |
| CALLMETHOD 拿到函式號 -1 | 334 | 34 |
| 　其中 `AdvStand@Move`／`MoveOut`／`IsMotion::get` | 11／10／18 | 0 |
| 　其中 `AdvNamePlate@Hide` | 39 | 0 |
| 對已釋放 slot 加參照（REF0）／減參照（UNREF0） | 44／150 | 0／1 |
| `heap_alloc_slot` skip | 40 | 0 |
| X_REF 越界（`Motion::ExecuterCollection`，var_idx 78／80） | 12 | 0 |
| X_REF 讀到壞 slot | 68 | 0 |
| `GETPAGE_BAD` | 495 | 1 |

修正後剩下的 34 次 -1，物件 slot 都是 0（null），全部在 SceneHome 的 Tutorial 路徑（`Tutorial::SetPartsZandClipper` 15、`Motion::Executer@0` 10、`Motion::Create` 5 等），和本根因無關。

framebuffer 比對：每 2 秒一張，第 12–39 張（共 28 張），逐張目視判讀。

- **名牌**：修正前兩次各有 11、12 張是疊字，例如「珀爾諾」底下留著前一個名字的「菈」、「阿熊」後面接著「綺菈」。修正後三次都是 0 張；只看得到單一名字的淡入淡出中間態。
- **立繪**：
  - 修正前，扎帕講完話後一直留在畫面右上方直到第 39 張；後段同時疊著 4–5 個立繪，其中白髮角色同時出現兩份（左側舊的一份沒退場，新的一份在中央）。
  - 修正後三次執行，扎帕都在第 16–18 張之間退場（時間點依執行快慢略有差異）。之後白髮與粉髮角色一起淡出（修正後 2 的第 33 張可見中間態），第 34–37 張只剩主角，下一位角色從中央登場。

截圖不提交；比對用的 framebuffer 留在 repo 外的 `XS4_WORK/runs/`。

## 同源而一併消失的現象

- `heap_alloc_slot` 的 skipped in-use 與 free list exhausted／corrupt 警告（見上）。
- `Motion::ExecuterCollection` 的 vtable 讀取越界（XREF_OOB）。只補 IFACE 的實驗就已降為 0，屬同一個 UAF 的下游。
- `Motion::Executer@0` 在 `SR_ASSIGN` 讀到已釋放 slot 的 `GETPAGE_BAD`：只補 IFACE 的實驗仍有 494 次，完整修正後為 0。推定是 option 參數規則消除的：`Motion::ParsedObjectCache@Get` 經 `IdArray` 的 option 路徑取得 MotionSet。兩條規則沒有分開驗證。

## 另案（未修）

- **已修正（`2005274`）：delegate 呼叫把伴隨槽當成參數**。delegate 型別的 `nr_arguments` 包含兩槽參數後面的 void 伴隨槽，`delegate_call` 的複製迴圈卻把伴隨槽再當成一個參數，多讀一格堆疊。兩槽參數在最後時，第一個區域變數會被寫成 delegate page 的 slot；在中間時，之後的參數依錯的型別加參照。真 AIN 重現：Tutorial 的 selector lambda 每次呼叫都讓 `ArrayExtensions::Select` 的 delegate page 少一個參照；特殊客人收入函式的 `wrap<SpecialCustomer>` 被提早釋放。修正後照原版 `0x66dce0`／`0x657430`，一格對一個變數。〈驗證〉追蹤表中修正後剩下的 1 次 `GETPAGE_BAD` 位在這個 Select 的 `DG_CALL`，與 `2005274` 修正前 GUI 追蹤中 Select 遇到 ref 0 delegate page 的事件吻合；修正後該事件為 0（GETPAGE_BAD 本身未用原追蹤 patch 重測）。詳見 [delegate-args.md](delegate-args.md)。
- **STRUCT／DELEGATE／ARRAY 參數多加一次參照**（headless 已驗證一例，未修）：原版 `0x657430` 對這些型別不加參照，呼叫端的 `A_REF` 已經交出一份。探針以 `A_REF` 呼叫 `AdvStand@GetMoveFrom(CASPos)` 後，ref 從 1 變 2，CASPos 存活數也從 1 變 2，每次呼叫多漏一份。可能是記憶體成長的來源之一。拿掉之前，要先確認沒有任何路徑以借用方式傳 struct。
- **Motion::Executer 沒被釋放**（GUI 追蹤觀察，未由本次重測）：修正前大多數 Executer 在 `Motion::Create` 當場就被釋放（`ExecuterCollection@Add` 拿到 -1）。修正後它們正常註冊，但被 `EraseEndTask` 移出集合後仍停在 ref=2，75 秒內 86 個一個都沒釋放。推定是 `DG_NEW_FROM_METHOD` 與 `A_REF` 複製的 delegate 形成強參照循環。pre-v14 的方法 delegate 目標是弱參照；v14 原版語義未驗證。即使有這個洩漏，修正後的峰值 RSS 仍比修正前低。
- **`parts::detail::CParts` 從未釋放**，所以 `ReleaseParts` 一直沒被呼叫（GUI 追蹤觀察）。這是另一個洩漏，與本根因無關。
- **Tutorial 路徑以 null 物件呼叫方法**：修正後剩下的 34 次 -1 都屬這類，見上。`2005274` 的前後追蹤中分布完全相同，selector lambda 收到的 rect 本身就是 0，和 delegate 伴隨槽無關（見 delegate-args.md）。
- **已修正（`4d52a87`）：`vm_call_nopop` 沒有 option 規則**。兩位審查者都以真 ffi 重現：`Array.Where`（arg3 196610）經 HLL 回呼呼叫 `ItemStock@ToItem` 的 lambda（參數 `option<wrap<Item>>`）時，每呼叫一次謂詞，元素就少一個參照；受影響的還有 `MapView@GetNode`、`GetAvailableNodes`、`GetAvailableEdges`（商店、道具欄、地城地圖）。IA6 只涵蓋 `delegate_call`，所以沒抓到。修正後在全部參數複製完才套用 option 規則；新案例 IA7 走遊戲的實際呼叫形狀，在 `4e4e87d` 上失敗、修正後通過。
- **option<介面>（三槽）當 delegate 參數**：`2005274` 之後 delegate 路徑一格對一個變數，option 規則與 `function_call` 相同，也會檢查三槽 option 的最後一槽。以臨時探針列舉本 AIN 全部 delegate，沒有任何一個帶三槽 option 參數，沒有實例可測。

## 側審查（`4a82758`、`05d2441`、`4dc7b85`）

側審查在 `7e16dee` 另外建置驗證：45 模式在兩種組態都通過，150 秒 GUI MSG 88。左側翻轉的朝向與原版實機截圖一致（水平位置差 1 px），名牌字距也和原版相同。另外找到以下缺陷，都沒有併入本次修正，已列入 HANDOFF：

> D1–D4 已在 `9f81bd9` 修正，見 [spacing-fix.md](spacing-fix.md)。D5–D7 仍待處理。

- **D1【中】**：`05d2441` 讓實際繪字變寬，但腳本用來量字寬的 `TextSurfaceManager.GetFontWidth` 仍用半形 size/2、也不算外框。例如 backlog 樣式（字級 25、外框 1、字距 −3）量到每字 22 px，實際畫出 24 px，接近行寬的行推定會被裁切。修正前兩邊一致。
  - 已驗證的部分：程式碼與 AIN 呼叫鏈。畫面未驗證，測試腳本走不到 backlog。
  - 沒有一併修的原因：要讓量字寬與繪字一致，得同時處理 D2（繪字公式本身和原版不同）。這不是小修正，而且畫面無法驗證。
- **D2【低～中】**：外框沒取 ceil，太さ與外框是相加而不是取 max。event 視窗每字 24 px，原版是 25 px。
- **D3【低】**：半形判斷看 Unicode，不看首位元組。
- **D4【低】**：缺字 fallback 沒有限定 GBK，commit 訊息也沒提這項行為變更。
- **D5【中低】**：翻轉只作用在 CG 類元件本身，不作用在 TEXT／FLAT 與子元件；`AdvStand@Move` 跨側時作用在 rect 上的 ReverseLR 沒有效果。這是功能缺口，不是回歸。
- **D6【低】**：設了 surface area 又翻轉時會錯位。
- **D7【低】**：`parts-reverse` 只測 setter／getter，沒測繪製與點擊判定。

## 未驗證事項

- 立繪最終站位是否與原版完全一致：本次只確認 `Move` 不再被略過、舊立繪會退場，沒有與原版實機截圖逐格對照。跨側移動時的 ReverseLR 仍受 D5 影響。
- 呼叫點 0x66a166、0x66a45b 所在常式各對應哪個 opcode 只是推定：0x66a166 推定屬於由 0x66e1ea 呼叫的 `0x66a0e0`，以 obj 是否為 -1 分派，推定為 delegate 路徑；0x66a082 屬於 `0x66a020`，由從 VM 堆疊彈出參數的 0x66d123 呼叫。
- 巢狀 option（option<option<wrap<T>>>）的參數：依 `0x6535e0` 剝層處理，但沒有查 AIN 中是否有實例，也沒有案例覆蓋。
- `GETPAGE_BAD` 494→0 由哪一條規則消除（見上）。
- 長時間穩定性與完整遊戲流程。
- `9e30c0f` 本身尚未經獨立反駁者審查（HANDOFF 流程第 3 步）；上文〈側審查〉審的是先前三個 commit。

## 重跑

```bash
H=docs/checkpoints/2026-09-28/harness
bash $H/before-check.sh 7e16dee iface-arg      # 修正前：rc=89，6/6 失敗
bash $H/verify-step.sh <tag>                   # 46 模式
XS4_PROBE_GBK=1 bash $H/verify-step.sh <tag>-gbk
bash $H/gui-run.sh <name> 150
```

## 審查與追加修正（2026-09-29）

- 兩位獨立審查者（參照計數、遊戲行為）都確認 `9e30c0f` 的修法符合原版 `0x657430`，沒有新的雙重釋放或洩漏；46 模式在預設與 GBK 組態都通過。行為審查者另把立繪站位與原版實機截圖逐格對照（同一句台詞的四人站位一致），並確認名牌第 12–62 張都沒有疊字。
- 兩人共同找到 `vm_call_nopop` 的 option 缺口，已在 `4d52a87` 修正（見上）。修正後 46 模式在兩種組態 `VERDICT PASS`；150 秒 GUI MSG 88、assertion 0、堆疊溢位 0、`heap_alloc_slot` 警告 0，峰值 RSS 1,309,130,752 bytes。
