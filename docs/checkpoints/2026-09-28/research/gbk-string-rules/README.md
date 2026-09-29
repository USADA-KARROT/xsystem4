# String 字元規則改用 GBK（2026-09-29，HANDOFF 第 5 項）

程式修正為 xsystem4 `6400e3c`（`String: Use the GBK character rule for GB18030 games`），submodule 指標由 `8c93946` 改到 libsys4 `247f544`。libsys4 在本機分支 `gbk-rules-20260929` 上有兩個 commit：`8181da6`（先以測試記錄 SJIS 現況）與 `247f544`（加入 GBK 規則）。使用者已同意本組修改 libsys4 並更新 submodule 指標。兩個 repo 都尚未推送；推送順序見文末。

原版語義來自原版 EXE 解殼傾印的靜態反組譯（capstone），EXE 沒有執行。設計文件與兩份反駁報告（原版語義、回歸與範圍）留在 repo 外，以下只記錄結論與處置。

## 結論

- CN 原版的 String 庫、VM 的 C_REF／C_ASSIGN 與文字排版都用同一條規則：首位元組 `0x81..0xFE` 就是 2 位元組字元，不檢查尾位元組，不處理 GB18030 四位元組；字元碼是 `(lead << 8) | trail`，單位元組不做正負號延伸；索引越界一律回安全值，不會中止。
- xsystem4 與 libsys4 原本全用 SJIS 規則（`SJIS_2BYTE`：`80..9F`、`E0..FF`），字元碼的首位元組放在低位，越界時 `ERROR`。大部分漢字的首位元組（`A1..DF`）被拆成兩個「字元」，尾位元組 `E0..FE` 又會吞掉下一個位元組。
- 修正後，GBK 規則只在「舊的 GB18030 偵測成立，而且新的嚴格判定也成立」時開啟。SJIS 遊戲走的程式碼與修正前相同（`sjis-chars` 修正前後逐行相同、`test/Run/test.ain` 輸出相同）。
- 名牌「綺□綺□」的根因是預設 gothic 字型 `VL-Gothic-Regular.ttf` 沒有 U+83C8「菈」，**不是**字元規則。本組已驗證：修正後名牌照舊缺字；改用 `HanaMinA.ttf` 當 gothic 字型時完整顯示。字型 fallback 與名牌殘影另案處理。

## 原版語義與位址

String dispatcher `0x684120`，跳表 `0x684984`。「已驗證」表示有逐指令反組譯；本組對 C_REF 的單獨首位元組與 `%F` 另外抽查過。

| 項目 | 原版位址 | 原版語義 | 證據 |
|---|---|---|---|
| 首位元組判斷 | Length `0x686914`、Find `0x686e6d`、Split `0x689489` 等 79 個函式 | `cmp b,0x81; jb; cmp b,0xFE; jbe` → 2 位元組；`0x80`、`0xFF` 為 1 位元組；沒有四位元組判斷 | 已驗證 |
| Length | `0x6868f0` | 依 GBK 規則逐字計數 | 已驗證 |
| PushBack | `0x686970` | 有號 `c > 0xFF` 時寫 `[c>>8, c&0xFF]`，否則 `[c&0xFF]`；經 C 字串串接，遇 NUL 截斷（PushBack(0) 不加位元組） | 已驗證 |
| PopBack／Erase | `0x686a80`／`0x686b80`、`0x686c40` | 依 GBK 規則刪字；Erase 在 index < 0、index ≥ Length 或 length == 0 時不動，length < 0 或超出字尾時刪到字尾 | 已驗證 |
| Find／FindLast | `0x686e00`／`0x686ea0` | 只在字元起點比對；空 key：Find 在非空 self 回 0，FindLast 回最後一字的索引 | 已驗證 |
| GetPart | `0x688290`／`0x6882c0` | begin < 0 夾成 0，length ≤ 0 回空，只換前進規則 | 已驗證 |
| Replace | `0x687df0`，dispatcher case `0x684525` | 在字元起點比對，命中時輸出 replacer；回傳新字串，self 不變；空 key 原版會無限迴圈 | 已驗證 |
| Split | `0x6892f0`（集合 `0x6862a0`、掃描 `0x686440`） | 分隔字串是 GBK 字元集合；containsMode bit0 保留分隔字元、bit1 保留空段；字尾的分隔字元不產生尾端空段 | 已驗證 |
| ToLower／ToUpper | `0x688880`／`0x688b70` | 先處理 `82 60..79` ↔ `82 81..9A`（尾位元組 ±0x21，`0x688c22..0x688c9c`），再跳過其他雙位元組，ASCII 大小寫轉換 | 已驗證 |
| ToInt／ToFloat | `0x686740`／`0x686810` → `0x686040` | 先把 `A3 B0..B9` 轉成數字、`81 44` 轉成 `.`，再 `sscanf` | 已驗證 |
| C_REF | `0x66b660` → `0x67dbb0` | `i ≤ 0` 回第一字；走到 NUL 回 0；雙位元組回 `(lead<<8)\|trail`；單位元組 `movzx`。字尾單獨首位元組回 `lead<<8`（`0x67dbfb` 讀到的是 NUL 本身） | 已驗證（本組抽查 `0x67dbb0..0x67dc0d`） |
| C_ASSIGN | `0x66f4a0` → `0x67dc10` | 先把 c 推回堆疊；越界或 i < 0 不寫入；以 16 位元 `(uint16)c > 0xFF` 判斷新字元寬度；四種寬度組合原地替換 | 已驗證 |
| `%D`／`%F` | `0x677620`（`0x67791c`）、`0x677d30`（`0x6780e9`）→ `0x6772e0` | 數字 → `A3 B0+d`；`-`、`.`、空白仍是 SJIS 的 `81 7C`／`81 44`／`81 40`。`%F` 把 'F' 換成 'f' 後消耗掉，不在結尾補 'F' | 已驗證（本組抽查 `0x6780e9..0x678108`） |
| 顯示路徑 | parts `0x4fbba0`、訊息 `0x5bca80`、`TextOutA` `0x69bb70` | 依 GBK 規則切字，`lfCharSet=0x86` | 參考，本組不改語義 |

v14 VM 只實作 C_REF／C_ASSIGN；`S_LENGTH`、`S_FIND` 等落入「未定義命令」分支，CN AIN 也沒有用到。

## 根因與影響

- String 庫：`Length` 110 處、`GetPart` 76 處、`Split` 43 處、`Find`／`Contains` 50 處等都用 SJIS 規則。例：4 字名字的 Length 是 7；面板 CG 名 `立繪／X` 的 GetPart(3) 從字中間開始；以全形斜線 `A3 AF` 分割時每個 `A3`、`AF` 都被當分隔字元。
- VM：C_REF 回傳 little-endian 碼，`SYS_ToUpper` 等以 `0x8281` 比較的 bytecode 永遠不成立；`SYS_AddPunct("")` 的 C_REF(-1) 讓引擎中止。
- `%D` 輸出 SJIS 全形數字（`82 50`…），在 GBK 下是別的字。
- iarray（PartsEngine 存檔、backlog）：寫入端以 SJIS 規則配對，字尾若落在 SJIS 首位元組位置就以有號 char 寫出，讀回時被 `string_push_back` 當雙位元組，多一個 `FF`。CN STR0 的 6,143 筆非 ASCII 字串中有 703 筆會這樣損壞（本組以真 AIN 實測）。

## 修法

### libsys4（`8181da6`、`247f544`，基底 `8c93946`）

| 檔案 | 內容 |
|---|---|
| `tests/test_string_sjis.c`（`8181da6`） | 先在 `8c93946` 的原始碼上記錄 SJIS 現況：`sjis_index`／`sjis_count_char`、`string_copy/find/push_back/pop_back/erase/get_char/set_char`、全形數字（含 `%F` 結尾的 'F'）、`string_to_integer`，越界 `ERROR` 以 `sys_error_handler` + `longjmp` 捕捉；單位元組預期值寫成 `(int)(char)0xB1`，不依平台的 char 正負號 |
| `include/system4/utfsjis.h`、`src/utfsjis.c` | `sys4_set_string_charset()`／`sys4_get_string_charset()`（預設 SJIS，不認得的值印警告並維持原狀）、`GBK_LEAD`、`mbcs_index`、`mbcs_count_char`（SJIS 模式直接呼叫 `sjis_*`） |
| `include/system4/string.h`、`src/string.c` | 每個受影響函式開頭加 GBK 分支，SJIS 本體不改：`string_copy` 改呼叫 `mbcs_index`；`string_push_back`／`string_pop_back` 的舊本體搬到新匯出的 `string_push_back_sjis`／`string_pop_back_sjis`；`string_erase`、`string_find`、`string_get_char`、`string_set_char`、全形數字與 `string_to_integer`（新匯出 `string_zen2han_number`）各有 GBK 分支 |
| `tests/test_string_gbk.c` | GBK 案例、越界不觸發 ERROR、COW、切回 SJIS，以及 20,000 筆隨機位元組字串上 `mbcs_*` 與 `sjis_*` 在 SJIS 模式下相同 |

GBK 分支的細節：

- 以 size 為界的迴圈一律用「首位元組且 `i+1 < size`」決定步進，不會停在 0；遇到字尾的單獨首位元組算 1 個字（原版會讀到 NUL 之後，刻意不仿）。
- `string_get_char`：`i ≤ 0` 回第一字；`i == Length` 靜默回 0；`i > Length` 或 `i < 0` 印警告；單獨首位元組回 `lead << 8`（同原版）；單位元組無號。
- `string_set_char`：`i < 0` 或 `i ≥ Length` 不動（負數或超過 Length 時印警告，`i == Length` 靜默）；只有要寫入時才 `cow_check`；寫入的位元組以 NUL 結尾時（`c == 0` 或 `0x4100` 這類）在該處截斷，等同原版的 C 字串。
- 越界警告整個程序最多 8 行，之後靜默，不呼叫 `ERROR`。

### xsystem4（`6400e3c`）

| 檔案 | 內容 |
|---|---|
| `src/hacks.c`、`include/xsystem4.h` | `gb18030_detect_strings()`：legacy（舊判準逐字照抄）、boundary、sjis_invalid、nonascii、utf8_valid 五個分數；嚴格判定為 `boundary > 5 && sjis_invalid >= 16 && utf8_valid*2 < nonascii`。`gbk_string_rules_enable()` 設 `ain_is_gb18030` 並開 libsys4 的 GBK 規則 |
| `src/system4.c` | 偵測改呼叫上述函式，位置與舊日誌不變；嚴格判定成立時呼叫 `gbk_string_rules_enable()` 並印 `GBK string rules enabled`，否則印 `GB18030 check not confirmed`。`XSYS4_STRING_CHARSET=sjis|gbk` 只覆寫字元規則 |
| `src/hll/String.c` | GBK 分支：ToInt／ToFloat 先正規化；Length、Insert 用 `mbcs_*`；Erase、FindLast、Replace、Split、ToLower／ToUpper、SearchAll 照原版；GetPart 依目前規則前進。SearchAll 的寫回抽成 `string_searchall_store()`，SJIS 路徑呼叫同一段程式碼 |
| `src/vm.c`、`src/hll/vmString.c` | `S_LENGTH`／`S_LENGTH2`、`vmString.GetLength` 改用 `mbcs_count_char`（SJIS 模式等價） |
| `src/hll/iarray.c` | 寫入端不動。讀取端在 GBK 模式下是寫入端的精確反函數（`c ≥ 0x100` 為一對，否則單一位元組）；SJIS 模式改呼叫 `string_push_back_sjis`（就是舊行為） |
| `src/hll/InputString.c` | Backspace 改呼叫 `string_pop_back_sjis`：緩衝區內容是 `utf2sjis` 的輸出 |
| `src/text.c`、`src/parts/text.c`、`src/hll/CharSpriteManager.c`、`src/hll/StoatSpriteEngine.c` | 只改 `ain_is_gb18030` 分支：首位元組後接 NUL 時算 1 位元組；四位元組形式被 NUL 截短時只取 NUL 之前的位元組；`0x80`／`0xFF` 後接 NUL 時算 1 位元組。完整字元的寬度與修正前相同 |

`ain_is_gb18030` 的偵測條件與所有既有使用點（Trim、regex 的 `\s`、`text.c`、`asset_manager.c` 等）都沒有改。

## 設計與反駁的處理

**採納並實作**

| 來源 | 內容 | 處置 |
|---|---|---|
| 語義 1、回歸 R5 | iarray 讀回時，合法 GBK 字串多一個 `FF`；設計把現象寫在錯的案例上 | GBK 模式的讀取端改成寫入端的精確反函數；fixture 以全部 STR0 非 ASCII 字串往返（修正前 703 筆改變，修正後 0 筆），另保留 SJIS 模式下「綺菈 → 多 FF」的控制組 |
| 語義 2 | 名牌負對照的前提未驗證：Hide 的 Motion 字串經過本組改動的 Split／Trim／SearchAll | 新增 headless 案例：以真 AIN 的 `Motion::GetCompiled` 解析名牌 Show／Hide 字串，SJIS 規則（修正前的組態）與 GBK 規則的結果樹相同。GUI 上名牌改成觀察項，不列為通過條件；並用字型探針直接確認缺字根因 |
| 語義 3 | `%F` 不補 'F' | GBK 模式不補；本組抽查 `0x6780e9..0x678108` 與 `0x6781f8` 相符 |
| 語義 4 | C_REF 遇字尾單獨首位元組回 `lead << 8` | 照原版；測試預期為 `0xB100` |
| 語義 5、回歸 R8 | `mbcs_char_bytes` 遇 NUL 回 0 會卡死 | 不匯出這個 API；所有以 size 為界的迴圈用永不為 0 的步進；`C_ASSIGN` 寫入尾位元組為 0 的碼時截斷；測試含內嵌 NUL 的 Find |
| 語義 6 | ReplaceRegex 的 CN 呼叫是 3 次，不是 0 次 | 更正（`backlog::detail::SYS_EraseTag`、`Enemy@0`、編輯器）；仍是 stub，列入另案 |
| 語義 7 | InputString 混用兩種規則 | Backspace 固定走 SJIS（`string_pop_back_sjis`） |
| 語義 8 | 證據等級偏高 | 名牌根因以字型探針升為已驗證；JAST 誤判維持「推定（高）」（位元組由 UTF-8 dump 回編），fixture 另以合成 SJIS 清單重現 |
| 回歸 R1 | 渲染器 NUL 防護沒涵蓋 `0x80`／`0xFF` | `ain_is_gb18030` 分支內 `0x80`／`0xFF` 後接 NUL 時算 1 位元組；fixture 經 `build_probe.py` 產生的包裝函式直接測試 |
| 回歸 R2 | before-check 不還原 libsys4 | 修正前對照把 submodule 暫時切回 `8c93946`（真正的舊組態），另跑一次「舊 src + `247f544`」；結束時切回 `247f544` |
| 回歸 R3 | 漏掉 `test/Run/test.ain` | 修正前後各跑一次複本，輸出逐行相同 |
| 回歸 R4 | 既有模式從不在 GBK 下跑；模式數是 40 | 新增 `XS4_PROBE_GBK=1`，44 個模式兩輪都通過；文件的 41 改為正確數字 |
| 回歸 R6 | 嚴格判定對 UTF-8／CP949／Big5 也成立 | 加上「非 ASCII 字串中合法 UTF-8 不到一半」的條件與 `XSYS4_STRING_CHARSET` 覆寫；CP949／Big5 無法以統計與 GBK 分開，但三者的首位元組範圍相同，字元切割一致 |
| 回歸 R7 | 四位元組規則收緊會改變被誤判的 SJIS 遊戲的非越界切字 | 只做截斷防護，四位元組的判斷條件不變 |
| 回歸 R9 | GBK 版 zen2han 要有 NUL 防護 | 單獨首位元組只複製 1 位元組；測試 `ToInt("1\xA3") == 1` |
| 回歸 R10 | `i == Length` 也發警告會把額度用光 | `i == Length` 靜默回 0 |
| 回歸 R12 | L1 寫死 signed char | 改成 `(int)(char)0xB1` |
| 回歸 R13 | 正式路徑與測試入口分岔 | `system4.c` 的嚴格分支直接呼叫 `gbk_string_rules_enable()` |

**不同意或部分不同意**

| 來源 | 建議 | 理由 |
|---|---|---|
| 回歸 R5 | iarray 讀取端在兩種模式都改 | 規則 3 要求 SJIS 遊戲完全不變。只在 GBK 模式改，SJIS 模式呼叫 `string_push_back_sjis`（就是舊本體） |
| 回歸 R1 | 在 SJIS 分支加 `src[1] ? 2 : 1`；或在 GB 分支讓 `0x80`／`0xFF` 一律算 1 位元組 | SJIS 分支會影響真正的 SJIS 遊戲，依規則 3 不動。`0x80`／`0xFF` 只在後接 NUL 時改成 1，其他情況維持修正前的 2，理由同 R7：被舊偵測誤判的 SJIS 遊戲只改變越界情況。CN 的 STR0（38,310 筆）與 MSG1（57,532 筆）裡以 GBK 規則走訪時單獨出現的 `0x80`／`0xFF`、字尾首位元組、首位元組接數字都是 0 筆，所以 CN 兩種寫法結果相同 |
| 回歸 R11 | `Int.ToCharacter` 在 GB 模式照 PushBack 規則 | CN 使用 0 次，原版語義沒有查。本組不改，列為已知不一致 |
| 設計 §4.4 | GUI 跑 180 秒 | 依任務要求用 150 秒 |
| 設計 §4.2 | `Split(遼|x, |)` 預期 1 段 | 設計表中的修正前後數字（1 段／2 段）對應的是「遼x」（`DF 7C 78`）；fixture 用 `DF 7C 78` |

## 驗證

### Headless（新增 4 個模式）

| 模式 | 修正前（`7c1daaa` + libsys4 `8c93946`） | 修正後（`6400e3c`） |
|---|---|---|
| `gbk-string` | 41 個案例 FAIL，最後一案（截短的四位元組）ASan heap-buffer-overflow，rc 134 | 80 個檢查全過，rc 0 |
| `gbk-vm` | 18 個 FAIL（little-endian 碼、越界 ERROR、`%D`、iarray 703 筆損壞等），rc 89 | 24 個檢查全過 |
| `gbk-detect` | 沒有 `gb18030_detect_strings`，rc 89 | 真 AIN：legacy 92、boundary 92、sjis_invalid 1419、非 ASCII 6143 中 UTF-8 22，嚴格判定成立；合成 SJIS 清單 legacy 50 但嚴格判定不成立；合成 UTF-8 清單 boundary 76、sjis_invalid 49 仍被 UTF-8 條件排除 |
| `sjis-chars` | 通過 | 通過；91 行 `SJIS ` 輸出與修正前逐行相同 |

- 「舊 src + libsys4 `247f544`」的對照結果與真正的修正前相同，`sjis-chars` 也逐行相同，表示新 libsys4 的預設 SJIS 行為與 `8c93946` 相同。
- `verify-step.sh`：44 個模式（既有 40 + 新 4）`VERDICT PASS`；`XS4_PROBE_GBK=1` 下 44 個模式也全部 `VERDICT PASS`，`deleted-event` 仍為預期的 87，sanitizer 0。
- 名牌 Motion：`Motion::GetCompiled` 解析 Show／Hide 兩個字串，兩種規則的結果樹相同（209 位元組的傾印）。
- libsys4：`meson test --suite libsys4` 的 hashtable、instructions、string_sjis、string_gbk 在 ASan／UBSan 下全部 OK；`test_string_sjis` 先在 `8c93946` 的原始碼上通過後才提交。
- `test/Run/test.ain` 複本：修正前後輸出逐行相同（`strings: 0 failed; 85 passed`；既有的 `structs: member destruct order` 失敗不變）。`XSYS4_STRING_CHARSET=gbk` 時 strings 10 項失敗（預期：SJIS 測試改用 GBK 規則），`sjis` 與無效值時與預設相同。

[驗證摘要](verify-summary.txt) · [修正前對照](before-7c1daaa.txt) · [修正後輸出](after-6400e3c.txt)

### GUI（150 秒，optimized，`--skip-title`）

| 執行 | MSG | assertion | 堆疊溢位 | 峰值 RSS | 其他 |
|---|---:|---:|---:|---:|---|
| 修正前（新存檔） | 88 | 0 | 0 | 1.73 GB | 沒有 `GBK string rules enabled` |
| 修正後（新存檔） | 88 | 0 | 0 | 1.80 GB | `GBK string rules enabled (boundary=92, sjis_invalid=1419, utf8=22/6143)` |
| 修正後，以修正前的存檔為種子 | 88 | 0 | 0 | 2.21 GB | Collection.asd v9 讀回成功並重寫 |
| 修正後，gothic 改用 HanaMinA（只為確認根因） | 88 | 0 | 0 | 2.19 GB | 名牌完整顯示「綺菈綺菈」 |

- 四次執行的 88 行 MSG 內容完全相同（sha256 相同）。GUI 中沒有出現 GBK 越界警告。PE_Save／PE_Load 沒有觸發，iarray 讀取端的改動只由 headless 證明。峰值 RSS 的差異在既有的記憶體成長範圍內，與本組的關係未驗證。
- framebuffer 比對（腳本與截圖都在 repo 外）：
  - 以對白框完全相同配對到 8 組畫格，其中 7 組整張相同；第 8 組的名牌區只差在淡出中的疊層，兩邊都是同樣的兩個星號。
  - **名牌**：修正後「綺□綺□」照舊，上一個名牌的殘影也照舊；HanaMinA 探針顯示完整。缺字屬字型，殘影與字元規則無關。
  - **立繪**（推定改善，未與原版畫面對照）：修正前 t15..t19 同一角色以兩種表情同時出現在畫面左右；修正後每個角色只出現一次。
  - **進度**：修正後在 t35（約 70 秒）進入據點選單；修正前到 t39 仍停在開場對白。原因未驗證。

## 未驗證事項

1. 立繪與進度的差異是否就是原版的行為：只有 framebuffer 觀察，沒有和原版畫面對照。
2. 修正後會走到以前走不到的路徑：面板 CG（`立繪／X` 的 GetPart／Split）、結局成就的末字比對、PersonalityIcon、`%D` 組成的 CG 與語音名稱。這些路徑可能暴露其他既有缺陷；150 秒內沒有觸發。
3. 讀檔後的 PartsEngine 文字與 backlog：iarray 讀取端的修正只有 headless 證據（GUI 沒有觸發 ResumeLoad）。舊存檔若已存入被 SJIS 錯切的字串，讀回後不會自動修復。
4. 偵測門檻只用 CN 的真 AIN、合成 SJIS／UTF-8／GBK 清單驗證。JAST 英文版的誤判（舊判準 22 分）是由 UTF-8 dump 以 cp932 回編的位元組算出，屬推定（高）；其他 GB 遊戲與大量使用半形片假名的 SJIS 遊戲未驗證。
5. `ToLower`／`ToUpper` 照原版會把 `82 60..79`／`82 81..9A` 兩段漢字互換；CN 的呼叫點推定都是 ASCII。
6. 字元碼方向在 SJIS 模式維持 little-endian；日文原版 bytecode（例如 `SYS_ToUpper` 的 `0x8281` 常數）其實要求 big-endian，上游在 SJIS 遊戲上可能本來就不一致。依規則 3 不動。
7. `MotionSet` 傾印中 `PartsParamCollection.<Time>` 在兩種規則下都是 1000，而字串寫的是 `Time:150`。這是否與名牌殘影有關未驗證，建議殘影組從這裡查起。
8. `%c`、`Int.ToCharacter`、`Int/Float.ToWideString`、ReplaceRegex、帶 matchList 的 Search／Match 的原版細節未查（CN 使用 0 次，ReplaceRegex 3 次但仍是 stub）。

## 另案

- **字型 fallback**：`ft_font_get_glyph` 在 gothic 字型沒有該字時改用 mincho（可只在 `ain_is_gb18030` 下啟用）。影響約 11.4% 的對白。
- **名牌殘影**：舊名牌的文字零件沒有淡出（見上方第 7 點）。
- **舊偵測誤判 SJIS 遊戲**：建議 `ain_is_gb18030 = legacy > 5 && strict`，但會改變 SJIS 遊戲的既有行為，需主控端決定。
- **轉碼**：`utf2sjis`（IME、檔名、GetSaveFolderName）與 `sjis2utf`（`unix_path`、日誌、MsgBox、MSG 行）在 GB 遊戲中都不對。
- **SJIS 模式既有的越界**：`string_erase` 目標是字尾單獨首位元組時越界；渲染器 SJIS 分支沒有 NUL 防護。
- **SJIS 遊戲的 String 語義**：Replace 寫回 self、Split 忽略 mode、FindLast 空 key、Erase 負值，只在 GB 模式修正。

## 重跑

```bash
export XS4_WORK=... XS4_GAME=... XS4_MASTER_GAME=...
H=docs/checkpoints/2026-09-28/harness
bash $H/verify-step.sh gbk-rules
XS4_PROBE_GBK=1 bash $H/verify-step.sh gbk-rules-gbk
# 真正的修正前組態：libsys4 要一起切回
git -C subprojects/libsys4 checkout --detach 8c93946
bash $H/before-check.sh 7c1daaa gbk-string gbk-vm gbk-detect sjis-chars string
git submodule update subprojects/libsys4      # 切回 xsystem4 記錄的指標
ninja -C "$XS4_ASAN_BUILD" && meson test -C "$XS4_ASAN_BUILD" --suite libsys4
```

推送順序（由主控端負責）：先把 libsys4 的 `gbk-rules-20260929`（`247f544`）以 fast-forward 推到 `origin/cn-on-upstream`，用 `git ls-remote` 與 `gh api` 確認遠端 SHA 後，才推 xsystem4；否則 xsystem4 會指向遠端不存在的 commit。本機另一個 libsys4 worktree 的 `cn-on-upstream` 分支仍在 `8c93946`，推送後要 fast-forward。
