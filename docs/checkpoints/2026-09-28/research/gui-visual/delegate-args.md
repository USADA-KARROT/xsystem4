# delegate 呼叫把伴隨槽當成參數（2026-09-29，`2005274`）

[uaf.md](uaf.md)〈另案〉第一項的修正。

## 結論

- **根因**：v14 的兩槽參數（介面 `[物件, vtable 偏移]`、option `[值, 判別槽]`、ref int `[page, index]` 等）在變數表裡占兩個變數：參數本身，加上一個存放第二槽的 void 伴隨變數。delegate 型別的 `nr_arguments` 也把伴隨變數算在內。`delegate_call` 複製兩槽參數時已經一次寫入兩個變數，之後又把伴隨變數當成下一個參數處理。結果是之後每個變數都拿到下一格堆疊，最後一個變數更讀過參數區，讀到 `DG_CALLBEGIN` 放在參數上方的 delegate page slot。
- **後果**（都用真 AIN 的 delegate 與函式在 headless 重現）：
  - **兩槽參數在最後**：參數後的第一個區域變數被寫成 delegate page 的 slot 號碼，它原本的初值（例如 wrap 區域變數的新 page）因此洩漏。被呼叫端若先 `DELETE` 這個區域變數，或在返回時釋放它，就會對 delegate page 減一次參照。`Tutorial::MoveSceneParent` 交給 `ArrayExtensions::Select<IParts&, IRectParts&>` 的 selector lambda 正是這樣：每呼叫一次，selector 的 delegate page 就少一個參照。探針讓 Select 走四個真 rect，selector 只跑兩次就被釋放，結果陣列有兩個 null。150 秒 GUI 中也出現了 Select 的 `DG_CALL` 遇到 ref 0 的 delegate page。
  - **兩槽參數在中間**：之後的參數依前一個變數的型別決定要不要加參照。`SpecialCustomerEvent@GetIncome` 由 EX 取得收入函式，型別是 `DG_Function<int, Worker&?, SpecialCustomer&, int>`，變數依序為 option、void、wrap、int。複製時 `wrap<SpecialCustomer>` 被當成 void，沒加參照，返回時卻被釋放，所以借用的 SpecialCustomer 被提早釋放。int 則被當成 wrap，對一個整數做了 `heap_ref`。
- **修法**：`delegate_call` 改成照原版與 `function_call` 的做法，每個參數變數對應一格堆疊（見〈修法〉）。
- **驗證**：新模式 `delegate-args` 的 DA1–DA3 在 `6421e6e` 失敗，修正後全過。48 個模式在預設與 GBK 組態都 `VERDICT PASS`。150 秒 GUI 的 MSG 為 88，assertion 與堆疊溢位都是 0。

## 錯在哪裡

`DG_CALLBEGIN` 之後、`DG_CALL` 之前的堆疊（xsystem4 的配置）：

```
[參數槽 0 .. N-1][delegate page][delegate index][回傳值預留槽]
```

N 是 `delegate_param_slots()`。兩槽型別算 2，伴隨變數跳過。本 AIN 1,779 個 delegate 的 N 都等於 `nr_arguments`（DA0 列舉）。其中 311 個有兩槽參數，合計介面參數 274 個、option 13 個、ref int／bool／float 26 個。

修正前的迴圈：

```
i = 0（介面）: 變數 0 ← 槽 0，變數 1 ← 槽 1         vi = 2
i = 1（void） : 變數 2 ← 下一格 = delegate page slot  vi = 3
```

實例：

| delegate | 被呼叫的真函式 | 修正前 |
|---|---|---|
| `DG_Func<ref IRectParts, IParts&>`（`IRectParts`, void） | `<lambda : Tutorial::MoveSceneParent(ICGParts&, bool)(31, 79)>`（fno 36584），區域變數 2 是 `wrap<iwrap<IParts>>` 的 `<dummy : IRectParts@Core::get>` | 區域變數 2 = delegate page。lambda 先執行 `.LOCALREF <dummy>`、`DELETE`，對 delegate page 減一次參照；原本的 wrap page 洩漏 |
| `DG_Function<int, Worker&?, SpecialCustomer&, int>`（option, void, wrap, int） | EX「慰問品收入計算函數」對應的收入函式（fno 26558，全 AIN 唯一符合此簽名的函式） | 變數 2（customer）以 void 複製、不加參照，返回時釋放；變數 3（matchCount）以 wrap 規則 `heap_ref(整數)`；變數 4（`base`）= delegate page，之後被覆寫 |
| `DG_Func<readonly ref int, CASColor>`（ref int, void） | `CMessageTextView@GetTextFontColorList` 的 lambda（fno 24399） | 區域變數 2（int `type`）= delegate page，下一行 `.LOCALASSIGN type 0` 就覆寫，沒有後果 |
| `activityeditor::detail::DG_InstanceItem_GetGridSizeHandler`（ref bool, void, ref int, void, ref int, void） | 編輯器路徑 | 參數依序錯一格，對 index 做 `heap_ref`，最後讀到 delegate index 與堆疊頂之外（依程式碼推導，沒有執行） |

之前的案例沒抓到這個問題，是因為被呼叫端都沒有區域變數：`iface-arg` 的 IA3（`ReleaseComponent`）與 IA6（`ItemStock@ToItem` 的 lambda）的 `nr_vars` 都等於 `nr_args`，多寫的那格被 `nr_vars` 擋掉。

## 原版依據

以下是對 EXE 解殼傾印的靜態反組譯（image base 0x400000），只讀、未執行。

- DG_CALL 的處理常式是 `0x66dce0`：
  - 從指令流讀出 delegate 型別號碼，描述在 `[vm+0xa0] + 號碼×0x6c`。
  - `0x66de53` 從堆疊彈出 delegate page。
  - `0x66df0d` 以 `[描述+0x4c]`（delegate 的參數數，含伴隨變數）配置參數向量。
  - `0x66df46` 迴圈取 `values[i] = stack[top − i]`，共取這麼多個，再一次彈出。伴隨槽沒有特別處理。
- 接著依 delegate 項目的物件分派：物件為 -1 時，在 `0x66e21f` 呼叫 `0x66a020`；否則在 `0x66e1ea` 呼叫 `0x66a0e0`。兩者都以被呼叫函式描述的 `[+0x4c]` 為個數，呼叫 `0x657430`（呼叫點 `0x66a082`、`0x66a166`）。
- `0x657430` 讓變數索引從 count−1 往下、值索引從 0 往上（值陣列是堆疊由頂往下的順序），所以第 i 個變數收到第 i 格參數槽。是否加參照看第 i 個變數的型別。void（型別 0）直接存入，也就是兩槽值的第二槽。
- 上游 xsystem4（`14c2618` 之前）也是 `values[i] = stack_peek(nr_arguments + 1 − i)`，一格對一個變數。
- 推定：`[函式描述+0x4c]` 是被呼叫函式的參數數。它和 `0x657430` 使用同一份函式描述的變數表，但結構布局沒有另外驗證。

## 修法（`src/vm.c` 的 `delegate_call`）

- 複製 n = min(`delegate_param_slots`, `nr_arguments`, 區域 page 的 `nr_vars`) 格，第 i 格寫入第 i 個變數。是否加參照照舊由 `delegate_copy_argument` 依第 i 個變數的型別決定：介面、REF、WRAP 加參照，void 伴隨變數不加。
- option 規則改成全部複製完再套用，和 `function_call`、`vm_call_nopop` 一致。三槽 option（option<介面>）現在也會檢查最後一槽。修正前的 delegate 路徑一律把 option 當兩槽、保守不加參照。以臨時探針列舉本 AIN 全部 delegate，沒有任何一個帶三槽 option 參數，所以這一點沒有實際案例。
- 不動的部分：`DG_CALLBEGIN` 與空 delegate 路徑仍依 `delegate_param_slots` 彈出，在本 AIN 等於 `nr_arguments`。STRUCT／DELEGATE／ARRAY 參數多加的參照見 uaf.md〈另案〉，本次沒有處理。

## 驗證

### Headless：`delegate-args`（`harness/probe/delegate_args_fixture.inc`）

每個案例各自 fork 執行。

| 案例 | 內容 | 修正前（`6421e6e`） | 修正後 |
|---|---|---|---|
| DA0 | 全部 delegate 的 `delegate_param_slots` 都等於 `nr_arguments` | 1,779 個全符合（311 個有兩槽參數） | 同左 |
| DA1 | 以真 rect（`AFL_Parts_CreateRect`）經 `DG_Func<ref IRectParts, IParts&>` 呼叫 Tutorial selector lambda | 進入時區域變數 2 = delegate page slot；呼叫後 delegate page 被釋放（ref 0） | 區域變數 2 是自己的新 wrap page；delegate page 的 ref 維持 1，rect 的 ref 不變，回傳的 IParts 存活 |
| DA2 | 真 `ArrayExtensions::Select<IParts&, IRectParts&>` 走四個真 rect，delegate page 照 `DG_NEW_FROM_METHOD`＋`CALLFUNC` 交給 Select | selector 只跑 2 次，結果 2 個 IParts 存活、2 個 null | 跑 4 次，4 個都存活 |
| DA3 | 以 `DG_Function<int, Worker&?, SpecialCustomer&, int>` 呼叫收入函式，不帶 worker（none） | 回傳 −400 正確，但 SpecialCustomer 被釋放（ref 1→0）；作為 int 傳入的 slot 被加參照（ref 1→2） | 回傳 −400，兩者 ref 不變 |

- `before-check.sh 6421e6e delegate-args iface-arg`：`delegate-args` rc=84，3/4 案例失敗；`iface-arg` 通過（見上，原本就沒涵蓋這個情況）。sanitizer 0。
- `verify-step.sh`（HEAD `2005274`）：48 個模式 `VERDICT PASS`；`XS4_PROBE_GBK=1` 也是 48 個 `VERDICT PASS`。`deleted-event` 仍為既有的 rc 87。

### GUI（`gui-run.sh <name> 150`）

| 執行 | MSG | assertion | 堆疊溢位 | `heap_alloc_slot` 警告 | 峰值 RSS |
|---|---:|---:|---:|---:|---:|
| 修正後（`2005274`） | 88 | 0 | 0 | 0 | 1.09 GB |
| 追蹤：修正前（`6421e6e`＋臨時追蹤） | 88 | 0 | 0 | 0 | 1.14 GB |
| 追蹤：修正後（`2005274`＋臨時追蹤） | 88 | 0 | 0 | 0 | 1.34 GB |

- 峰值 RSS 在執行之間的差異大於前後差異，不當成本次修正的效果。
- 修正前後的 88 行 MSG 內容逐行相同。
- framebuffer 第 16、26、34、39 張目視：講完話的立繪會退場，名牌只有單一名字，與 `9e30c0f` 之後的行為相同。

臨時追蹤只在設定 `XS4DBG_DGARGS` 時開啟，未提交。它記錄三件事：經過「兩槽參數加伴隨變數」delegate 的呼叫、`DG_CALL` 遇到已釋放的 delegate page、CALLMETHOD 拿到函式號 -1。兩次追蹤執行各 150 秒：

| 項目 | 修正前 | 修正後 |
|---|---:|---:|
| 經過受影響 delegate 的呼叫（最後一筆記錄的累計） | 35,287 | 35,297 |
| 　記錄到的被呼叫函式中，有區域變數而會被多寫一格的 | 2 個：Tutorial selector、`GetTextFontColorList` 的 lambda | 同左（修正後不再多寫） |
| `DG_CALL` 遇到 ref 0 的 delegate page | 1（`ArrayExtensions::Select<IParts&, IRectParts&>` 的 selector） | 0 |
| CALLMETHOD 拿到函式號 -1 | 34 | 34 |

- 其餘記錄到的是 Motion 相關的 lambda 與 observer（`DG_Observer_NotifyHandler` 等），被呼叫函式沒有區域變數，多寫的那格被 `nr_vars` 擋掉。追蹤以 delegate 為單位抽樣記錄（每個 delegate 的第 1、10、100…次，以及每累計 1000 次），同一 delegate 綁到的其他函式不一定都被記到（未驗證）。
- 記錄到的有害實例只有 Tutorial selector；DA3 的特殊客人事件在這段時間內走不到。
- 34 次 -1 的分布修正前後完全相同：`Tutorial::SetPartsZandClipper` 15、`Motion::Executer@0` 10、`Motion::Create` 5、Tutorial 其他 3、`SceneHome@0` 1。其中 selector lambda 那一次的物件本身就是 0，也就是 SceneParentStack 的陣列裡原本就有 null rect。所以 uaf.md 列出的「Tutorial 路徑以 null 物件呼叫方法」和本缺陷無關，仍屬另案。
- uaf.md 在 `9e30c0f` 之後記錄到 1 次 `GETPAGE_BAD`，位置也在這個 Select 的 `DG_CALL`，與上表的 ref 0 事件吻合。GETPAGE_BAD 計數本身沒有用原追蹤 patch 重測（未驗證）。

## 未驗證事項

- 特殊客人事件（DA3 的收入函式）與編輯器路徑的 delegate 在 GUI 中走不到，只有 headless 與程式碼推導。
- `[函式描述+0x4c]` 是被呼叫函式的參數數，屬推定（見〈原版依據〉）。
- 三槽 option 參數的 delegate 規則：本 AIN 沒有實例，沒有案例覆蓋。
- GUI 修正前的追蹤中，selector 在 Select 內被呼叫幾次、每次少幾個參照沒有逐次記錄；只知道迴圈結束時 delegate page 的 ref 是 0。
- 150 秒 GUI 中經過受影響 delegate 的所有被呼叫函式是否都已列出（追蹤是抽樣記錄）。
- 長時間穩定性與完整遊戲流程。
- 本修正尚未經獨立反駁者審查（HANDOFF 流程第 3 步）。

## 重跑

```bash
H=docs/checkpoints/2026-09-28/harness
bash $H/before-check.sh 6421e6e delegate-args   # 修正前：rc=84，3/4 失敗
bash $H/verify-step.sh <tag>                    # 48 模式
XS4_PROBE_GBK=1 bash $H/verify-step.sh <tag>-gbk
bash $H/gui-run.sh <name> 150
```
