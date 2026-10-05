# 驗證環境（2026-09-28 起的權威版本）

這個目錄是目前唯一有效的 headless 探針與驗證腳本。`../array-overload/` 與 `../first-overload/` 內的 `.c`、`.inc` 是當時的快照，已過時，只作為歷史證據。

所有輸出都寫到 repo 外的 `XS4_WORK`，repo 本身不會被弄髒。

## 需要的東西

- macOS（Apple Silicon 已驗證）、Command Line Tools 的 clang。
- Homebrew：`meson ninja pkg-config libffi sdl2 freetype libpng jpeg-turbo webp cglm libsndfile ffmpeg glew bison flex`。
- 已初始化的 submodule：`git submodule update --init`（libsys4 以 xsystem4 記錄的指標為準，目前是 `247f544`，含 GBK 字元規則；不要自行改指標）。
- 自行合法取得的遊戲**工作副本**，內含 `dohnadohna.ain`（繁中版，sha256 `beefa667…8947`）。絕不可指向原始安裝目錄或母片。

## 變數

| 變數 | 必填 | 說明 |
|---|---|---|
| `XS4_GAME` | 是 | 遊戲工作副本目錄 |
| `XS4_MASTER_GAME` | 建議 | 原始母片目錄；設定後，若 `XS4_GAME` 與它相同，所有腳本拒絕執行 |
| `XS4_WORK` | 否 | 輸出目錄，預設 `~/xsystem4-work`，不可在 repo 內 |
| `XS4_SRC` | 否 | 要測的 checkout，預設為本 repo |

## 指令

```bash
export XS4_GAME=/path/to/game-workcopy XS4_MASTER_GAME=/path/to/original
H=docs/checkpoints/2026-09-28/harness
bash $H/setup.sh                        # 第一次：meson 建兩棵樹（optimized 給 GUI、ASan 給探針）並連結探針
bash $H/verify-step.sh <tag>            # 重建並跑全部 62 個模式；最後一行 VERDICT PASS/FAIL，exit code 同義
XS4_PROBE_GBK=1 bash $H/verify-step.sh <tag>-gbk   # 同上，但每個模式啟動時先開 GBK 字元規則（CN 實際組態）
bash $H/before-check.sh <rev> <mode>..  # 用 <rev> 的 src/include 跑指定模式（證明修正前會失敗），結束自動還原
bash $H/gui-run.sh <name> [秒數]         # 無人值守 GUI：新遊戲、按住 Return、每 1.2 秒點畫面中央、每 2 秒存 framebuffer PNG
RUN_FROM_TITLE=1 RUN_AUTO_CLICK_SEQ="4000,640,700;8000,640,700;14000,152,130" RUN_CLICK_TRACE=1 RUN_SHOTS=9000,500,60 \
  bash $H/gui-run.sh <name> 60         # 從商標與標題開始，照時間表點擊，記錄每次點擊的目標，每 0.5 秒存一張
```

- `verify-step.sh` 的輸出在 `$XS4_WORK/logs/verify/<tag>/`，每個模式一個檔案，另有 `summary.txt`。第一行記錄 HEAD、libsys4 SHA 與 `probe_gbk`。
- `XS4_PROBE_GBK=1`：探針在 `init_probe` 之後呼叫 `gbk_string_rules_enable`（以 dlsym 取得；舊版沒有時只設 `ain_is_gb18030`）。正式引擎對 CN AIN 會開這個規則，探針預設不開，所以兩種組態都要跑。`sjis-chars` 與 `gbk-vm` 會自行切回 SJIS。
- `deleted-event` 預期 exit 87：23 個殘留 slot 是既有問題，功能檢查本身通過。其他模式預期 exit 0 且沒有 sanitizer 診斷。
- `before-check.sh` 要求 `src/`、`include/` 沒有未提交修改。它會暫時改寫 checkout 的這兩個目錄，被中斷時也會還原。它**不會**切換 libsys4 submodule；修正涉及 libsys4 時，要另外把 submodule 暫時切回舊 SHA 才是真正的修正前組態，結束後切回（見 `research/gbk-string-rules/README.md`）。
- `gui-run.sh` 遇到 assertion、ASan、VM error、堆疊溢位、日誌超過 300 MB 或時間到就停止。結束時印出對白行數（`MSG` 行）、堆疊溢位次數與 framebuffer 張數。在執行目錄建立名為 `STOP` 的檔案可手動停止。
- `gui-run.sh` 的其他選項（`tooling/run-gui-bounded.py` 轉交給引擎的測試用變數）：`RUN_FROM_TITLE=1` 不帶 `--skip-title`，也不按住 Return、不定時點擊（除非另外給 `RUN_HOLD_KEYS`／`RUN_AUTO_CLICK`），用來測標題 → 新遊戲、鑑賞模式等從標題出發的路徑；`RUN_AUTO_CLICK_SEQ="毫秒,x,y;..."` 在指定時間點擊（`XSYS4_AUTO_CLICK_SEQ`）；`RUN_CLICK_TRACE=1` 在日誌記下每次 v14 點擊的目標（`S2 click target=`，`XSYS4_STAGE2_TRACE`）；`RUN_SHOTS=起點,間隔,張數`（毫秒）改變 framebuffer 截圖的排程（`XSYS4_SCREENSHOT_SCHEDULE`，預設 2000,2000,40）。引擎自己結束（例如 VM 錯誤後）時，`run.json` 的 `stop_reason` 為 `error_log` 或 `exited`。
- 存讀檔驗證用的 GUI 變數：`RUN_STRING_CHARSET=sjis|gbk` 轉給 `XSYS4_STRING_CHARSET`（對照組用）；`RUN_TRACE_SAVE=1` 讓引擎對 SerializeStruct 系列每次呼叫印一行 `SAVE ...`（`XSYS4_TRACE_SAVE`，上限 200 行）；`RUN_SAVE_SEED=<目錄>` 先把該目錄的檔案複製到本次的 `saves/` 再啟動，種子本身不會被寫入，位於 `XS4_SRC` 或 `XS4_MASTER_GAME` 內時拒絕執行。`run.json` 另記錄 `save_seed` 與開始／結束時每個存檔的大小、sha256、mtime（`saves_manifest_start`／`saves_manifest_end`）。
- 本機若沒有「螢幕錄製」權限，無法擷取單一視窗；請以 framebuffer PNG 作為畫面證據。截圖與存檔都不要提交到 repo。

## Control 快進診斷

使用者確認 Control 可快進；AIN 的全文快進檢查也直接讀取 VK 17，沒有已讀／未讀條件，但仍受遊戲的快進許可與單段設定控制。左右 Control 都映射 17；13 是 Return。

```bash
RUN_HOLD_KEYS=13,17 RUN_AUTO_CLICK= RUN_SHOTS=0,500,100 \
  bash $H/gui-run.sh codex-input-control-skip 50
```

本組實測約 7 秒到據點教學，50 秒內 MSG 88 雜湊與正常 run 相同，assertion／overflow 0。`RUN_AUTO_CLICK=` 明確關掉預設週期點擊，避免提早到據點後持續點中畫面；測教學與選單時，要先確認目前頁面再給座標／時間表，不能套用未快進的抵達秒數。run 名稱必須未使用過。

`RUN_HOLD_KEYS` 在整個 run 持續按住指定鍵；不能在執行途中改 shell 環境變數放開。若需要到據點後放開 Control，應使用實際按下／放開操作。這個快進組態適合快速到場景診斷，**不取代固定 150 秒正常回歸**，也不能用來宣稱動畫、文字等待與 Wine 原版逐幀時序相同。

## 探針結構

- `probe/runtime_probe.c`：把引擎的 `vm.c` 加上探針鉤子後編入，連結正式的引擎目標檔；`main` 依第二個參數分派模式。
- `probe/build_probe.py`：從 ASan 樹的 `compile_commands.json` 取編譯參數，產生 `runtime-probe-asan`。它不會重編 `Array.c` 等目標檔，所以腳本一律先跑 `ninja`。
- `probe/*_fixture.inc`：各組測試。以真 AIN 的函式宣告與 lambda 呼叫 `hll_call(libno, fno, arg3)`，並在需要時用 `fork` 隔離預期中的崩潰。
- `probe/activity_text_fixture.inc`：自造 EX tree 經正式 pactex loader 建立具名文字元件，再透過真 AIN 宣告與 ffi 查詢。混排三種狀態並插入無關 branch，檢查文字／CG 型別、主文字與 ruby 字型隔離、未知型別保留，以及明確 `SetComponentType` 覆寫。文字為空且無 CG 資源，不需要 GL。
- 此模式的靜態入口由 `build_probe.py` 在 repo 外複製 `parts.c`／`pe_v14_activity.c` 並追加測試 wrapper；僅初始化 parts table/controller 並呼叫正式 static loader，不更改正式邏輯。正式引擎不含測試 API。
- `probe/dialogue_model_fixture.inc`：使用真 AIN／ffi 檢查 typed struct 訊息陣列反覆 Free／Clear 後的 EmplaceBack／At#1 契約、空頁查詢、外部元素所有權、int／string 型別，以及 null／v13 保留行為。只含合成 ASCII 文字。
- `probe/dialogue_copy_fixture.inc`：使用真 AIN／ffi 驗證 ShallowCopy 的 struct／string 元素身分與所有權，涵蓋來源先刪、副本先刪、At#1 讀取及最終釋放；另保留 empty／null 相容檢查。
- `probe/save_fixes_fixture.inc`：`save-fixes`，`173ff1d` 審查後的回歸測試。F1 損壞的 struct 定義數；F2 讀入 `option<int>` 後由真 bytecode `WorkerHistory@SetIncome` 覆寫；F3 `X_OP_SET` 後刪除 struct；F4 複製含 `option<int>` 的 struct；F5 沒有建構子的 struct 由 `vm_construct_struct` 預設初始化；F6 其 `<vtable>` 填入方法清單；F7 三槽 option 預設為 none；F8 `option<int>` 區域變數不配置 slot。只用 dlsym 取新函式，所以也能在修正前的版本編譯。
- `probe/save_persist_fixture.inc`：存讀檔持久化。`save-list` 檢查 `Array.SYSTEMONLY_GetStructPageList`（綁定、X_A_INIT 單槽清單、無效元素整批清空、快取頁 metadata、真 bytecode `AFL_GameSave_StructSave` 傳入的清單）；`save-roundtrip` 以真 AIN struct 與真 `AFL_GameSave_Struct*` bytecode 做 v9 格式、就地讀回、巢狀／陣列／option／delegate、損壞檔、原子寫入、選擇器、ffi 重入後的參數釋放與 option<int> 刪除；`save-comment` 檢查存檔註解。存檔寫在 `$XS4_SAVE_TMP` 或 `$TMPDIR` 下的 mkdtemp 資料夾（位於 `XS4_SRC`／`XS4_MASTER_GAME` 內時拒絕），結束即刪除。
- 選用模式（不在 `XS4_MODES`）：`save-roundtrip` 的 R15 與 `save-localgame` 需要 `XS4_SAVE_ORIG_COPY=<原版存檔的複本資料夾>`，讀入原版引擎寫的 `.asd` 複本並重存比對；不可指向原始存檔資料夾本身，也不可提交。`save-seed` 以 `XS4_SAVE_SEED_DIR=<XS4_WORK 內的資料夾>` 為 GUI 第二次執行準備種子（在 Collection 的已讀事件清單依排序插入 `ZZ_PERSIST_PROBE`，並寫出 `<ConfigVoiceMutedByNsfw>=1` 的 AFConfig.asd）。
- `probe/gbk_chars_fixture.inc`：GBK 字元規則（2026-09-29）。`gbk-string` 經真 ffi 呼叫 String 庫並測渲染器的 GB18030 切字 NUL 防護；`gbk-vm` 執行真 AIN 的 `SYS_AddPunct`／`SYS_DeletePunct`／`SYS_ToUpper`／`SYS_ToLower`、libsys4 的字元讀寫、`%D`、iarray 往返（含全部 STR0 非 ASCII 字串），並比較名牌 Show／Hide 的 Motion 字串在兩種規則下由 `Motion::GetCompiled` 解析出的樹；`gbk-detect` 檢查偵測分數；`sjis-chars` 在 SJIS 規則下跑同一批案例，記錄修正前的行為，修正前後的 `SJIS ` 行必須逐行相同。渲染器的 `gb18030_skip_char_bytes`／`extract_multibyte_char` 是 static，`build_probe.py` 在 repo 外複製 `src/text.c`、`src/parts/text.c` 並加上 `probe_*` 包裝函式。
- `probe/iface_arg_fixture.inc`：`iface-arg`（2026-09-29），v14 參數所有權。以真 AIN 函式與 delegate 借用傳入介面參數（`AIN_IFACE`）與 `option<wrap<T>>` 參數，檢查呼叫前後 ref 不變、存進成員後各持一份：CALLFUNC、CALLMETHOD、delegate、真 `AdvStand` 的 MoveIn／Move／MoveOut（`m_parent` 的 sprite 必須存活且 vtable 可解析），以及 CALLMETHOD 與 delegate 的 option 參數。6 個案例各自 fork；失敗時印出 ref 在哪個 opcode、哪個函式降低。只用修正前就存在的 VM 函式，`before-check.sh` 可直接建置。
- `probe/delegate_args_fixture.inc`：`delegate-args`（2026-09-29），delegate 呼叫的參數複製：一格堆疊對一個參數變數，兩槽參數的第二槽進 void 伴隨變數（原版 `0x66dce0` → `0x657430`）。DA0 列舉全部 delegate，確認 `delegate_param_slots` 等於 `nr_arguments`；DA1 以真 rect（`AFL_Parts_CreateRect`）經 `DG_Func<ref IRectParts, IParts&>` 呼叫 Tutorial selector lambda，檢查進入時區域變數 2 是自己的初值，且 delegate page 與 rect 的 ref 不變；DA2 讓真 `ArrayExtensions::Select<IParts&, IRectParts&>` 走四個真 rect，selector 必須跑四次、結果四個都存活；DA3 以 `DG_Function<int, Worker&?, SpecialCustomer&, int>` 呼叫唯一同簽名的收入函式，回傳值正確，借用的 SpecialCustomer 與當成 int 傳入的 slot ref 都不變。4 個案例各自 fork；DA1／DA2 像 IA4 一樣註冊 PartsEngine。只用修正前就存在的 VM 函式，`before-check.sh` 可直接建置。
- `probe/reverse_inherit_fixture.inc`：`reverse-inherit`（2026-09-29），翻轉旗標作用在整棵元件樹（原版 `0x535260` → `0x4e6d80`：沿父元件鏈 XOR，以錨點為軸鏡像，子元件位置也鏡像）。RI1 以真 AIN 宣告建三層元件，檢查父元件 LR／TB 翻轉後子孫的全域位置、`Parts_IsCursorIn` 的方框、`Parts_GetPartsUpperLeftPosX`、getter 回傳自己的旗標，以及還原；RI2 以 dlsym 取 `parts_box_transform`、`parts_anchor_transform`、`parts_screen_to_box`，在 16 種翻轉組合加旋轉與倍率下與獨立公式比對 80 個點與反變換，另查 TEXT／FLAT 路徑與 surface area；RI3 以真 bytecode 跑 `NEW AdvStand`、`AdvStand@Move` 跨側與 `Motion::EndAll`（先照原版 alloc 設好 `Motion::Instances::executer`），rect 根經 Motion 被翻轉後，立繪影像要以錨點鏡像。3 個案例各自 fork；新函式只用 dlsym，`before-check.sh` 可直接建置。
- `probe/text_metrics_fixture.inc`：`text-metrics`（2026-09-29），CN 的 GDI 字格（`0x69c7a0`）。12 組真 pactex 樣式經真 AIN 宣告（`Parts_SetFont`、`Parts_SetTextCharSpace`、`Parts_SetPartsFontBoldWeight`、`SetMessageWindowTextFont`／`TextSpace`）設到元件上，檢查 `TextSurfaceManager.GetFontWidth`（真 ffi）、每字貼圖寬 `text_style_width`、`gfx_size_text`，以及 `gfx_render_textf` 回傳的前進量。探針沒有 GL，前進量改用「字形永遠載入失敗」的假字型量：不繪製，但排版照跑；字形存在的分支由 GUI 量測涵蓋。另查缺字 fallback 只在 CN 組態啟用。GBK 與 SJIS 各在 fork 出的子行程跑（`gfx_font_init` 每行程一次），字型取 `$XS4_SRC/fonts`；SJIS 的 `SJIS ` 行在修正前後（`05d2441` 之前）必須逐行相同。`build_probe.py` 在 repo 外複製 `src/font_freetype.c` 並加上 `probe_ft_font_fallback`（舊版沒有 fallback 時加回傳 false 的替身）。
- `probe/base_ui_fixture.inc`：`base-ui`（2026-09-29），據點畫面的元件（`research/gui-visual/base-ui.md`）。BU1 以合成 GBK pactex 經正式 loader 建元件樹，再以真 AIN 宣告查：讀檔後立即 `NumofChild`／`GetChild`（原版 `0x58f060` 立即掛上）、依 `部件タイプ` 名稱表的型別（レイアウトボックス 8、ユーザコンポーネント 17、數字部件 24、ＣＧ判定部件 27）、`SetComponentType(n,27,1)` 只改該狀態、`GetUserComponentName` 回 `ユーザコンポーネント名`、`Get/SetUserComponentData`、`GetActivityParts` 的名稱與編號；BU2 `Parts_SetParentPartsNumber` 的立即掛上、循環與未知父元件、0 脫離，以及父元件倍率作用在子元件位置；BU3 `SetButtonEnable`／`IsButtonEnable`；BU4 `Array.Add`／`PopBack`（0x10003）在 `X_A_INIT 0` 陣列上保留兩槽，並跑真 bytecode `SceneParentStack@Push`／`@Pop`；BU5 `MainEXFile.Col` 對平面 list 與 table，並跑真 bytecode `EXHelper::GetStringArray`。5 個案例各自 fork 並設 60 秒 alarm（修正前 BU2 的父元件循環會讓更新迴圈卡住）。`build_probe.py` 在 repo 外複製 `src/hll/MainEXFile.c` 並加上 `probe_mainex_set`，把合成 EX 放進它的靜態狀態；其餘只用修正前就存在的函式，`before-check.sh` 可直接建置。
- `probe/frame_pacing_fixture.inc`：`frame-pacing`（2026-09-30），原版的 60 fps 限速與每幀一次推進、一次呈現（`research/gui-visual/pacing.md`）。FP1 `frame_pacing_limiter_step` 對原版 `0x4676f0` 的 float 算式（固定案例、32 位元計時器回繞、與獨立參考逐位元比對 20,000 步、模擬刻度式與精確 Sleep 的平均週期）；FP2 實際時鐘下 `frame_pacing_sleep(n)` 在第 n+1 個毫秒刻度返回，經真 AIN 的 `ChipmunkSpriteEngine.SYSTEM_Set*` 宣告檢查限速、OverFrameRateSleep、略過已讀時十幀畫一幀並重設限速器、`SleepByInactiveWindow` 的 Sleep(50)；FP3 呈現歸屬；FP4 經真 AIN 宣告的 `UpdateComponent`／`ChipmunkSpriteEngine.Update`／`SystemService.UpdateView`，Alpha motion 每幀只前進遊戲給的 passedTime（用略過中的幀，不需要 GL）；FP5 截圖背景寫檔與同步寫出逐位元組相同。5 個案例各自 fork；新函式只用 dlsym，`before-check.sh` 可直接建置。FP2 有實際時間的上下限（例如限速時每幀 16–18 ms），機器極忙時可能不穩。
- `probe/base_ui_review_fixture.inc`：`base-ui-review`（2026-09-30），`6d39915`／`44964f9` 審查的修正（`research/gui-visual/base-ui.md` §10）。BR1 以真 `PE_UpdateInputState` 點擊停用按鈕（`SetButtonEnable` false）：不得送 MouseClick，也不得改送全畫面點擊與 `g_EndPartsBusyLoop`；BR2 只有普通狀態有判定區的偵測元件（ＣＧ判定部件的形狀）要能點到；BR3 兩槽元素（0x10003）的 `At`／`First`／`Last` 推兩槽、`Last` 取最後一個元素；BR4 兩槽的 `First(pred)`、`EraseAll`、`Concat`、`Reverse`、`Insert`，並以真 bytecode `Motion::PartsParamCollection@0` 檢查它用 `First(pred)` 找到 TimeParam（修正前 Time 停在預設 1000）；BR5 真 bytecode `AnimateText@AdjustPos` 把四個字置中；BR6 合成 GBK pactex 的 矩形部件（25）與 構築部件（26）回報各自型別、矩形大小為四角外框；BR7 背景截圖寫檔後 `vm_exit` 與 `VM_ERROR` 都要等寫完。探針不跑 `_PreLink`，所以 v14 訊息佇列的存取函式在 `pe_v14_message_replace` 後從靜態匯出表取得。7 個案例各自 fork；只用修正前就存在的函式，`before-check.sh` 可直接建置。
- `probe/title_review_fixture.inc`：`title-review`（2026-09-30），第二輪審查的修正（`research/gui-visual/base-ui.md` §11）。TR1 以真 bytecode `SceneTutorial@RegisterEvent` 把場景自己的方法加進按鈕事件：場景的參照數不變、擁有者放手後場景被釋放、按鈕事件裡它的項目被刪掉（原版 `0x652210`／`0x681f70`）；TR2 在外層解構子（真 `backlog::detail::CBackLogUnit@1`）執行中放掉另一個物件的最後參照，它的解構子要在外層繼續前執行；TR3 `RemoveController` 與 `ReleaseActivity` 的清單是被刪元件的 delegate index；TR4 真 bytecode `ArrayExtensions::GetReverse<IRectParts&>`（兩槽）、`GetReverse<ActionTarget&>`（一槽 wrap）、`GetSort<string>` 不改動來源陣列；TR5 可點擊的系統元件（1000001000 以上）接收點擊、只擋游標的元件讓點擊變成全畫面點擊；TR6 空的 on-cursor ＣＧ狀態不讓元件在游標下消失；TR7 CParts 頁釋放時不釋放號碼等於其 `<vtable>` slot 的元件。探針不跑 `_PreLink`，TR3 先執行它再直接呼叫 v14 版的 `RemoveController`。7 個案例各自 fork；只用修正前就存在的函式，`before-check.sh` 可直接建置。
- `probe/third_review_fixture.inc`：`third-review`（2026-09-30），第三輪審查的修正（`research/gui-visual/base-ui.md` §12）。RV1 合成 GBK pactex 經正式 loader 建立 `オン指針透過` 為 1／0／缺少／2 的元件，以真 AIN 宣告 `Parts_GetPartsPassCursor` 查詢（原版 `0x5547f4` 以 `== 1` 讀入元件 +0x1a5），setter 仍可覆寫；RV2 標為 1 的裝飾擋在按鈕前時點擊與懸停都到按鈕，標為 0 的仍擋住（全畫面點擊）且自己是懸停目標；RV3 懸停只有一個目標（原版 `0x546890`）：可點擊但穿透游標的元件在前面時只有它懸停並收到點擊；RV4 以真 `CBackLogUnit@1` 做巢狀解構並在內層把指令計數推進 450,000（模擬長拆除），之後同型別仍執行解構子、`CParts3DLayerManager` 仍不執行；RV5 真 `activity::detail::CActivityWrap`：一個 wrap 的 `Release` 很長之後，下一個 wrap 的解構子仍呼叫 `Release`。步進鉤子 `third_review_probe_step` 在指定的框架開始時放掉物件或推進 `insn_count`。5 個案例各自 fork；只用修正前就存在的函式，`before-check.sh` 可直接建置。
- `probe/mojibake_fixture.inc`：`mojibake`（2026-09-30），GBK 版的遊戲字串以 GB18030 解碼（視窗標題、檔名、存檔資料夾、記錄檔）。MJ1 `game_to_utf8`／`utf8_to_game`／`display_sjis0`／`unix_path` 在 SJIS（預設、只設 `ain_is_gb18030`）逐位元組同 `sjis2utf`／`utf2sjis`，GBK 下 GameName 為「多娜多娜繁中版」、尾位元組 0x5C 不當分隔字元、非 SJIS 字的檔名可來回轉換、不合法的 GBK 退回 SJIS；MJ2 `save_dir_for_game_charset` 的四種情境（只有舊目錄、兩者都有、都沒有、新名稱是檔案）；MJ3 `savedir_path` 解碼且不在存取時改名；MJ4 `migrate_legacy_save_files` 只改名與恰好一個 AIN 字串舊名完全相同的檔案（近似名稱、含路徑分隔字元的字串、兩個字串共用的舊名都不改）；MJ5 `FileOperation.GetFileList` 回傳 GB18030 名稱、`TextFile.OpenReader` 開得了由它組出的路徑。檔案都在 `$TMPDIR` 的 mkdtemp 資料夾（位於 `XS4_SRC`／`XS4_MASTER_GAME` 內時拒絕），結束即刪除。5 個案例各自 fork；新函式只用 dlsym，`before-check.sh` 可直接建置。
- `probe/logo_gloss_fixture.inc`：`logo-gloss`（2026-09-30），v14 pactex loader 讀中文版的 `描畫フィルタ` 與 `加算色`（`research/gui-visual/logo-gloss.md`）。LG1 以合成 GBK activity 經正式 loader 建元件，以 `PE_GetPartsDrawFilter`／`PE_GetAddColor` 查 GBK 鍵、SJIS 鍵、沒有這兩個鍵的元件；LG2 `アルファクリッパー`（GBK、SJIS 鍵，被遮部件排在遮罩之前）在整個 activity 建完後依名稱解析，未知名稱留 0；LG3 v14 `GetComponentMulColor*`／`AddColor*` 讀回設定值、照 Motion 逐通道淡出後為 (0,0,0)、未知元件回 255／0 且不建立元件。子元件繼承濾鏡的繪製規則需要 GL，由 GUI 逐幀比對涵蓋。只用修正前就存在的函式，`before-check.sh` 可直接建置。
- `probe/clip_area_fixture.inc`：`clip-area`（2026-09-30）。CA1 經真 AIN 宣告檢查 ClipArea 的四值讀回、停用保留矩形、相同矩形不重新啟用、改值自動啟用，以及未知元件不被建立；CA2 以合成 GBK／SJIS pactex 檢查 enable=0 仍保留矩形、非零 enable 為真、缺少屬性時關閉；CA3 以 dlsym 取得 `parts_clip_area_transform`，檢查不繪圖父元件也裁切子樹、所有啟用祖先的螢幕矩形取交集、anchor 不含 box origin offset、local scale 與整數截斷、翻轉旗標不改矩形寬高、不相交／退化時跳過繪製、v13 不作用；CA4 在記憶體中經 `PE_Save`／`PE_Load` 驗證 v14 格式保留啟用與停用的矩形、v13 整頁資料不受 ClipArea 變動影響，以及 v14 讀取舊 v3 時預設關閉。4 個案例各自 fork；不使用 GL、不建立存檔，新函式只用 dlsym，`before-check.sh` 可建置。
- `probe/input_nesting_fixture.inc`：`input-nesting`（2026-09-30），v14 `BeginInput`／`EndInput` 的巢狀作用域（原版 `0x58a720`／`0x58a750`）。IN1 經真 AIN HLL 宣告檢查兩層返回後外層仍能點擊、最外層結束後不派送；IN2 三層返回、額外 `EndInput` 不破壞下次開始；IN3 開始時取目前按鈕狀態，已按住的左鍵不憑空變成 DOWN；IN4 內層結束時同樣取狀態，並恢復外層的新點擊；IN5 開始／結束清掉先前點擊編號但保留已排入訊息，按住移動不把內層點擊帶到外層；IN6 執行未改動的真 AIN `parts::detail::BeginInput`／`EndInput` wrapper，反覆進出八次；IN7 v13 保留 `Begin, Begin, End` 就關閉的原有行為；IN8 用 dlsym 取得 manager reset 入口，檢查重設會丟棄舊巢狀作用域、新的 Begin／End 獨立，同時保留訊息佇列及非懸停元件由腳本指定的狀態。八個案例各自 fork，以合成判定方框、正式 `PE_UpdateInputState` 與 v14 訊息佇列驗證；不使用 GL 或遊戲資產，新符號僅以 dlsym 取得，`before-check.sh` 可建置。
- `probe/gauge_fixture.inc`：`gauge`（2026-09-30），v14 橫／豎計量條。HG1 以合成 GBK pactex 的亂序命名狀態、無關 surface 分支驗證型別 22／23 及實際 `PARTS_HGAUGE`／`PARTS_VGAUGE`，保留各 state 的原始分子／分母、反轉與 surface；HG2 真 HLL 型別建立、100／100 建構預設、無貼圖時的浮點數值、反轉、負 surface 及四個 `wrap<int>` 輸出、同名 CG 設定保值、既存錯型別轉換與未知元件不建立；HG3 以 dlsym 取得正式幾何 helper，檢查有效 surface、橫／豎方向與反轉、比例截斷、超額／負分子、零／負分母，並把 source crop 四角的完整 CG UV 乘上正式 render matrix，驗證非零 surface、H/V 正反向半滿在原點與世界縮放下不被二次裁成四分之一；HG4 執行真 `CHGaugeParts@Numerator::set`／`Denominator::set` 的虛擬方法，另一個值必須保留；HG5 記憶體 XPE v5 保存／重讀兩個狀態的原始數值、surface、反轉與空 CG 名稱；HG6 保留 v13 型別與無貼圖 setter 行為，另驗證 v14 讀舊 v3／v4 的 ratio 遷移及後續 ClipArea 對齊，且在任何 gauge getter 可能轉換型別前先確認 22／23 已由載入還原。六個案例各自 fork；無 GL、遊戲素材或存檔檔案，新函式僅以 dlsym 取得，修正前可建置。實際 CG 載入與像素由 GUI 比對涵蓋。
- `probe/working_cards_fixture.inc`：`working-cards`（2026-10-05），春銷對手／顧客卡片的兩條 VM 資料鏈。WC1 取真 `LocalGame@2` 的 `X_A_INIT 1`，檢查兩個不同的 WorkerCollection、各自真建構子及內部清單，並驗證重新初始化、外部持有舊陣列與最終釋放；WC2 使用真 AIN 的直接 float／int／bool／string 陣列宣告及初始化指令，檢查長度、零值／空字串初值與釋放；WC3 真 CustomerCollection 的 Shuffle／GetOrdered 對四個合成顧客產生完整順序，核對身分、PushBack 前堆疊，以及結果與來源分別釋放（精確記錄修前空 Select 也會留下的兩個空暫存物件）；WC4 全 26 個一般物件／18 個介面 wrap delegate 宣告及合成 primitive／舊版控制；WC5 執行真 Tutorial 介面 selector 的 DA1／DA2，保留兩槽契約；WC6 驗證本組未涵蓋的 generic 初始化路徑維持既有行為及釋放不影響哨兵物件。各案例 fork，不需 GL、遊戲素材或存檔。CASMatrix 的巢狀陣列宣告解析仍未修，不能把 WC2 當作完整矩陣驗證。
- `observer`、`reentrancy` 兩個模式在 2026-09-30 改為原版的 delegate 所有權：delegate 持有 lambda 的環境、不持有目標物件（`observer` 的 smoke 案例與 `reentrancy` 的參照數斷言；`reentrancy` 由 harness 保留目標物件的根參照）。在 `ce5c75a` 之前的版本上這兩個模式會失敗，是預期結果。
- `deleted_event_fixture.inc` 放在上一層，因為 `runtime_probe.c` 以 `../deleted_event_fixture.inc` 引用它。

新增一組測試：

1. 在 `probe/` 新增 `xxx_fixture.inc`，並在 `runtime_probe.c` 加上 `#include` 與 `main` 內的模式分派。
2. 把模式名稱加到 `env.sh` 的 `XS4_MODES`；若預期 exit code 不是 0，更新 `xs4_expected_rc`。
3. 要引用只在新版才存在的 C 函式時，用 `dlsym(RTLD_DEFAULT, "名稱")` 取得位址（參考 `cif_fixture.inc`），否則 `before-check.sh` 在舊版會編譯失敗。
4. 先用 `before-check.sh <修正前的 commit> xxx` 確認失敗，再用 `verify-step.sh` 確認全部通過。

- `probe/alpha_inherit_fixture.inc`：`alpha-inherit`（2026-10-05），v14 alpha遮罩沿父元件繼承，最近可解析且非editor-hidden的本地遮罩覆蓋祖先。AC1真HLL setter、三層繼承、覆蓋、清除、未知號碼、Show=false與編輯隱藏；AC2重新掛父、脫離、遮罩釋放／重建、無貼圖與自身遮罩；AC3 v13保留本地行為。使用正式resolver，舊版以原本只讀本地欄位的路徑對照；無GL／素材，實際像素另由GUI核對。
- `probe/text_default_fixture.inc`：`text-default`（2026-10-05），v14 pactex 文本部件狀態的預設 `[文本]`（原版文字狀態 loader `0x5c2d20` 在 `0x5c2fac` 讀入）。TD1 三個狀態各自的文字與空 `[文本]`；TD2 `文本位置` 排在 `文本` 之前仍讀到正確文字、只有 `文本位置`／`文本裝飾` 時保持空（精確鍵名）；TD3 SJIS 鍵 `テキスト`、文字狀態帶有 `文本裝飾` 的字級並已排版（style 與文字的先後在最終狀態不可觀察）；TD4 真 AIN 宣告 `Parts_SetText` 覆蓋佔位文字、`Parts_AddPartsText` 接續；TD5 沒有 `[文本]` 時為空。排版會建立字元貼圖，每個案例在 fork 中建立無視窗 CGL context 並換上字形永遠載入失敗的字型（不繪製、照常排版）。只用修正前就存在的函式，`before-check.sh` 可直接建置。
