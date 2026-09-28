# 程式碼蒸餾審查：wip/post-checkpoint-2026-07-06 vs 上游共同祖先

- 審查日期：2026-09-26
- 對象：USADA-KARROT/xsystem4 `e8bd5ab..22e9496`（27 commits；`git log --oneline e8bd5ab..22e9496` 計 27 筆，23 筆 7 月、4 筆 9 月）
- 上游：nunuhara/xsystem4 `upstream/master` = `04333ad`；libsys4 上游 master = `20560d4`，submodule pin = `8c93946`，共同祖先 `ed74c9e`
- 範圍：`git diff e8bd5ab..22e9496 --stat -- src include meson.build` = 73 檔，+19,786 / −1,588（docs/ 排除）
- 方法：唯讀。所有 grep 對 HEAD worktree 與儲存於 scratchpad 的完整 diff（`scratchpad/distill/full.diff`，24,768 行）執行；`git merge-tree --write-tree` 只寫 object，未動 index/worktree。未建置、未執行遊戲。
- 審查標準：未來以「小而專、可獨立審」的 PR 送上游，每個變更都要能讓人逐一審閱。

---

## 0. 一句話結論

wip 分支不是一組可拆的 PR，而是一個「v14 VM 重寫 + Dohna 專屬 workaround + 兩輪診斷儀器」交纏的單體。其中真正可以直接送上游的通用修正只有約 10 個小單元（合計不到 100 行），其餘 19,000 行需要先去除診斷碼、去除名字驅動的遊戲特例、再依 §5 的相依序列拆分。有 6 個無 `ain->version` gate 的 VM/heap/input 行為變更會影響 v<14 遊戲（§6）。

---

## 1. Debug 殘留

### 1.1 `fprintf(stderr, ...)`
0 處新增。`grep -c -a -E '^\+.*fprintf' full.diff` = 0。輸出全部走 libsys4 的 `WARNING/NOTICE/VM_ERROR` 宏：新增行中 `WARNING(` 146 處、`NOTICE(` 25 處、`VM_ERROR(` 13 處、`ERROR(` 1、`puts(` 1（`--skip-title` usage 行）。

### 1.2 `getenv("XSYS4_...")` 開關（全部列出）

base `e8bd5ab` 只有 3 個 getenv（`XSYSTEM4_HOME`、`XDG_DATA_HOME`、`HOME`，system4.c:240-255）。HEAD 新增 16 個變數名：

| 變數 | 讀取位置 | 引入 commit | 分類 | 說明 |
|---|---|---|---|---|
| `XSYS4_STAGE2_TRACE` | vm.c:474；input.c:282；parts/input.c:311,338；parts/message_window.c:220；hll/pe_v14_message.c:265,279,299 | d3d08e8 (9/9) | 臨時診斷 | vm.c 內 `stage2_trace` 出現 123 次，分布 445–4746 行，10 個 static 函式（vm.c:469-697）。預設追蹤 15 個硬編 Dohna fno（vm.c:454-457：`9196, 9197, 27031, ...`）與硬編 global 名 `parts::detail::g_EndPartsBusyLoop(Number)`（vm.c:522-524）。 |
| `XSYS4_STAGE2_FROM_MSG` / `_AFTER_MS` / `_WATCH_FNO` / `_PER_FNO` / `_FNOS` | vm.c:478-506 | d3d08e8 | 臨時診斷 | 上項的子開關。 |
| `XSYS4_STAGE2_PERF` | video.c:440 | d3d08e8 | 臨時量測 | present 間隔 p95 統計（video.c:425-483）。STATUS.md 自述「PERF 為 vsync=0 時的 present 提交頻率…不能當作實際遊戲 FPS」。 |
| `XSYS4_TRACE_FNO` | vm.c:5083 | 2b8597e (7/9) | 臨時診斷 | 硬編 `SDL_GetTicks() > 42000` 與 `trace_left = 6000`（vm.c:5081,5088）。commit message 自述用途是「walking bytecode against the ain dump」。 |
| `XSYS4_GAME_DEBUG` | hll/system.c:122 (`system.IsDebugMode`) | d3d08e8 | 半正式 | 對應 HLL 語意（遊戲開發模式），但以 env 控制而非 config；9/9 STATUS.md:37 有記載。 |
| `XSYS4_HOLD_KEYS` | input.c:696 | 4f689ac (7/6) | 測試設施 | commit 自述「auto-click/HOLD_KEYS test facility (from the fork)」。每次 `handle_events` 強制 `key_state[...] = true`（input.c:707-708）。 |
| `XSYS4_AUTO_CLICK` / `_SEQ` / `_X` / `_Y` / `_COUNT` | input.c:728-752 | 4f689ac | 測試設施 | 硬編預設座標 `(350,180)`（input.c:694）。 |
| `XSYS4_SCREENSHOT_DIR` | video.c:508（在 `gfx_swap()` 內） | 3d445d6 (7/6) | 測試設施 | 有條件（env 未設不跑），每 2 秒、最多 40 張（video.c:511-516）。不是無條件 auto-screenshot；其他 `gfx_save_texture` 呼叫點皆為既有功能（screenshot.c、parts/debug.c、sprite.c）。 |

### 1.3 無條件輸出（不需 env）

| 位置 | 內容 | 引入 |
|---|---|---|
| vm.c:5096-5117 | 每 50M 指令印 `heartbeat:` WARNING + 4 層 call stack + 6 層 base frame；`call_stack_ptr > 100` 時一次全 dump。所有遊戲都跑。 | 14c2618 |
| system4.c:673 | `WARNING("AINCHECK: msgf=%d nr_messages=%d messages=%p", ...)` 縮排錯位（在 `if (gb_score > 5)` 區塊內但縮排在外層），明顯臨時檢查。 | 0b8beae |
| vm.c:97 | `NOTICE("v14 MSG fallback enabled (R=%d A=%d)")` | 14c2618 |
| vm.c:4707-4708 | `WARNING("X_ASSIGN %d past end of page ...")` 每次觸發都印，無節流。 | a4a623f |
| vm.c:5148-5155 | `VM_CALL_TIMEOUT` WARNING（前 10 次）。但 `vm_call_insn_limit` 只在 vm.c:416 初始 0、1697-1706 save/restore、5171 設 0，沒有任何寫入非零值的點 → 整段 5137-5175 是 dead code。 | 14c2618 |
| heap.c:461-463 | `heap_gc: swept ...` WARNING 前 3 次 + 每 4096 次。 | 14c2618 |
| hll/system.c:105 | `system.Exit(%d) — suppressing` 前 5 次。 | 99ac346 |
| heap.c:2018 (diff 行) / heap.c:2073 | `heap_gc: shrunk heap`、`free list exhausted/corrupt` WARNING。 | 14c2618 |

### 1.4 停用碼 / 空殼 / 引用已消失的東西

| 位置 | 內容 |
|---|---|
| heap.c:314-315 | `// TEMPORARILY DISABLED FOR DEBUGGING` `return;` 在 `heap_gc_periodic()` 開頭；vm.c:5133-5134 仍每 10 秒呼叫它。從 14c2618 起就如此，從未啟用。 |
| heap.c:139 | `static const bool gc_collect_cycles = false;` → mark 階段（heap.c:326-372）與 `GC_IS_MARKED` 分支（388）永不執行。`heap_gc()` 實際只做 orphan sweep + 重建 free list。註解仍稱「Periodic GC every 10 seconds to collect cycle-garbage」（vm.c:5127-5128），與實際不符。 |
| vm.c:54-59 | `static void free_deferred_strings(void) {}` 空函式，註解引用「submodule pin 7f8d5298, now GC'd/unrecoverable」；vm.c:5372 仍呼叫。 |
| vm.c:240-242 | 「v14 vtable dispatch removed — see notes/language-update.md」：檔案存在（`notes/language-update.md`），但這是 wip repo 內文件，上游沒有。 |
| pe_v14_activity.c（diff L14478-14502） | 6 個被註解掉的 SJIS/GBK 字串常數，標「unused, kept for reference」。 |
| hll/PartsEngine.c | 1dae88b commit 提到 `#if 0/#if 1 BISECT markers`；HEAD grep `#if 0|BISECT` 無命中（只剩 `HLL_TODO_EXPORT`），已清理。 |

### 1.5 程序層級的全域改動（不是 debug 但屬「為了跑測試而改」）

| 位置 | 內容 | gate |
|---|---|---|
| system4.c:442-447 | `error_handler` 把 `SDL_ShowSimpleMessageBox` 改成 no-op（「blocks in non-interactive environments」）。 | 無（所有平台、所有遊戲） |
| system4.c:449-461, 469-472 | `sigsegv_handler`（SIGSEGV/SIGBUS）用 `backtrace_symbols_fd`；`sigtrap_handler`；`setvbuf(stderr, NULL, _IOLBF, 0)`。`#include <execinfo.h>`（system4.c:22）無平台 `#ifdef`；src/meson.build 只在 darwin 加 `-liconv` 與 stack_size（234-237），未對 execinfo 做 gate → Windows/MinGW 建置預期失敗（未實測）。 | 無 |
| libsys4 7ee607b | `sys_exit` 改 `_exit()`（跳過 atexit）；`sys_verror/sys_vwarning` 加 `fflush(stderr)`；`free_string` double-free 由 `ERROR` 降為 `WARNING` + return。上游 20560d4 未含這些（`git show 20560d4:src/string.c` L37-44 仍 `ERROR`；`src/system.c` L169-173 仍 `exit(code)`）。 | 無 |

---

## 2. 硬編遊戲特例

`grep` 新增行中含字串字面值的 `strcmp/strncmp/strstr/strcasecmp` 共 47 處（scratchpad `full.diff`）。按位置整理：

| 位置 | 硬編內容 | gate | 能否改成宣告驅動 |
|---|---|---|---|
| vm.c:245-249, 256-276 `init_func_flags` | `strstr` `"<lambda"`、`"CDebug"`、`"CASTimerManager"` / `"RCASTimerManager"`、`"CASTimer@Get"`/`"@Reset"`/`"@GetScaled"`、`"RunResult<SceneTitle"`、`"Run<SceneLogo>"` | 無 version gate（v<14 也掃一遍，名字不會命中） | `"<lambda"` 已同時用 `ain->functions[i].is_lambda`，可只留 is_lambda。其餘是 Dohna 類別名，AIN 沒有對應 metadata，無法宣告驅動。 |
| vm.c:283-299 `struct_flags` | `strstr` `"RCASTimer"`、`"VariableTimer"` | `ain->version >= 14` | Dohna 專屬。 |
| vm.c:305-410 `native_cas_timer_intercept`；vm.c:1379-1432 呼叫點 | 以函式名 `strstr` `"CASTimerManager"`、`"CreateHandle"`、`"GetObject"`、`"Rate::get"`/`"Rate::set"`、`"@Get"`、`"@Reset"`、`"GetScaled"`、`"@0"`/`"@1"`/`"@2"` 分派；`CAS_TIMER_MAX 2048`，timer id = `abs(struct_page) % 2048`（vm.c:368） | 由 func_flags 名字驅動 | 不可。本質是「遊戲的 CASTimer 類在此 VM 上頁面損毀（vm.c:306-307 自述）所以原生代替」，是 workaround 不是功能，上游不會收。d3cd045 再補 epoch reset 也在同一路徑。 |
| vm.c:89-97, 2580-2591 | `ain_get_function(ain, "R")` / `"A"` | `v14 && msgf < 0` | 半通用（R/A 是 System4 腳本慣例名，但仍是名字驅動）。 |
| vm.c:2935-2952 | `ain_get_function(ain, "message")` | `v14 && msgf == 0` | 半通用；是否其他 v14 遊戲同樣 `msgf=0` + 名為 `message` 未驗證。 |
| vm.c:2594-2612, 2725-2745 `--skip-title` | `"RunResult<SceneTitle"`、`"Run<SceneLogo>"` | `config.skip_title` | Dohna 專屬；CLI 旗標對其他遊戲無效。 |
| vm.c:5432-5438 | `strstr(sn, "Manager")`/`"Collection"`/`"Map<"`/`"IdArray"` 決定 v14 全域 struct 建構順序 | v14 | 啟發式；AIN 無初始化順序 metadata。字串比對太寬泛，任何含 "Manager" 的類別都被歸為 infra。 |
| page.c:559 | `strstr(s->name, "CDebug")` 跳過 constructor | `init_global_struct_v14`（v14 路徑） | Dohna 專屬。 |
| page.c:578-585 | `"parts::detail::CParts3DLayerManager"` 預先進 dtor 黑名單 | `ain->version >= 14` | Dohna 專屬。 |
| page.c:599-622 | `"parts::detail::CParts"` → `delete_struct` 時自動 `parts_release(values[0])` | 無 version gate（名字不命中則 index=-1） | Dohna/PartsEngine v14 專屬；把 VM 的 page.c 耦合到 parts 子系統（extern parts_try_get/parts_release），上游會反對。 |
| ffi.c:777-786 | `libraries[libno].name == "Array"` && `f->name == "EmplaceBack"` 回傳 (slot, index) 2-slot | `ain->version >= 14` | 可改：依 `f->return_type` 與 `hll_arg3` 元素型別判斷，而不看函式名。 |
| ffi.c:1164-1176 | `lib->name == "Array"` && `"Erase"`/`"IsExist"`/`"Numof"`/`"Count"`/`"Find"` 換函式指標 | 無 | 可改：`array_query_function(const struct ain_hll_function*)` 已接收宣告，方向正確；但 link 層不該用名字硬編，應由 Array.c 內以宣告的參數型別選實作。 |
| ffi.c:1192-1197 `resolve_library_name` | `"AnteaterADVLogList"` → `"AnteaterADVEngine"` | 無 | 可改為 `struct static_library` 的 alias 欄位或第二個 `HLL_LIBRARY` 名。 |
| hll/Array.c:60-63 `array_elem_is_2slot` | `etype == 3 || etype == 5`（魔數） | `hll_current_arg3` | 正面例子（宣告驅動），但 3/5 應命名為 enum。 |
| hll/Array.c（diff） | `strcmp(f->name, "Find")`、`"Numof"`/`"Count"` | — | 同上，應由參數型別選。 |
| system4.c:646-654 | `ain_get_library("PartsEngine")` + `ain_get_library_function(...,"SeekMessage")` → 預設 1280x720 | v14 | 啟發式。上游是否有等價機制未驗證。 |
| hll/pe_v14_activity.c:379-380 | SJIS `"\x83\x81\x83\x62..."`（メッセージウィンドウ）與 GBK `"\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda"`（信息窗口）位元組字面值 | pactex loader | 資源格式專屬；至少要以具名常數表（檔案上方已有 `SJIS_*`/`GBK_*` 常數，這兩個卻是 inline）。 |
| parts/input.c:326；parts/parts.c:2105, 2158 | `parts_no >= 1000001000` / `1000000000`（GetFreeNumber 區段） | v14 | Dohna 專屬魔數。 |
| hll/system.c:100-108 | `system.Exit()` 不退出（「game assertions call Exit(1) for non-fatal errors」） | 無（但 `system` 庫只有 v14 遊戲會 link） | 行為改變：遊戲 assert → Exit(1) 被吞掉，繼續執行。這也是為何目前 `Personality.jaf:27 assert` 後還能看到 free-list 異常。 |
| vm.c:112-146 overlay | 硬編 `1280`、`720`、`box_h 160`、`FONT_GOTHIC 24` | v14 msg fallback | Dohna 解析度硬編。 |

硬編 fno / libno：無直接 `fno == <數字>` 或 `libno == <數字>`（`grep -E '\b(fno|libno|...)\s*==\s*[0-9]{2,}'` 無命中）。唯一硬編 fno 清單是 stage2 trace 的 15 個（vm.c:454-457）。

---

## 3. 與上游重複 / 衝突

### 3.1 靜態合併試驗（`git merge-tree --write-tree upstream/master HEAD`，2026-09-26）

CONFLICT 檔案：`src/audio_mixer.c`、`src/parts/motion.c`、`src/parts/parts_internal.h`、`src/parts/text.c`、`subprojects/libsys4`（gitlink）。與 9/9 audit.md 對 `fa09b00` 的「4 檔 + 1 子模組」一致。

wip 修改的既有檔（53 個）與上游同期修改的檔（55 個）交集 29 個（完整清單見附錄 A）。

### 3.2 libsys4：instruction width 修正已被上游獨立解決

- wip 7ee607b 在 `initialize_instructions()` 逐項手寫 `instructions[X].ip_inc = 2 + n*4`（libsys4 src/instructions.c）。
- 上游 05a4d58 + 67b740f（已在 20560d4）改為 `set_nr_args(opcode, n)` 同時設 `nr_args` 與 `ip_inc`，並補 `nr_stack_args`、`DG_EXIST`/`DG_NEW`/`DG_STR_TO_METHOD` metadata（`git diff ed74c9e..20560d4 -- src/instructions.c`）。
- 結論：wip 版應丟棄，改 rebase 到上游；`tests/test_instructions.c`（f35b9a1）要對上游 API 重寫。9/9 audit.md:204 已指出 `src/instructions.c` 有內容衝突。

### 3.3 上游 5d032d7（Fix various memory leaks）vs wip GC 節流

- 上游改 `src/parts/parts.c`：`parts_state_free` 補 `free_string(anim.cg_name)`、`parts_animation_set_cg`/`parts_gauge_set_cg*` 補 `cg_free`、numeral font 改用 `asset_exists`；另 AnteaterADVEngine.c、parts/save.c 各 1-2 行。
- wip 改 `src/parts/parts.c`：`parts_state_free` 加 `parts_clear_hit_mask`（parts.c:175）、`parts_release` 加 `parts_message_window_free`（parts.c:1002）、`PE_Update` 時間 fallback（1172-1188）、`PE_RemoveController` 泛化（2211-2247）。
- 同檔不同函式；merge-tree 顯示 parts.c 自動合併（不在 CONFLICT 清單）。
- 語意層面：wip 的 heap 壓力 GC（heap.c:505-521）+ orphan sweep（heap.c:380-457）是在 VM heap 層掩蓋 refcount 洩漏；上游是在 parts 層修真正 leak。兩者不互斥，但 wip 的 sweep 會把上游想暴露的 bug 藏起來。上游 5d032d7 的 4 個 `free_string`/`cg_free` 修正 wip 目前沒有。

### 3.4 message_window.c vs 上游 parts 文字系統

- 上游 `upstream/master` 對 message window 只有 `parts->message_window` bool 與 `PE_Set/GetPartsMessageWindowShowLink`（parts.c:1887-1895）、render.c:596 依 `parts_message_window_show` 隱藏。沒有 `PE_SetMessageWindowText` 等 API。
- wip 新增 `struct parts_message_window` sidecar（message_window.c:14-22）、9 個 `PE_*MessageWindow*` 函式、render.c:628-633 在 `parts_render` 尾端對每個 parts 呼叫 `parts_message_window_render_text`。
- 無直接重複；但 render.c 掛勾對 v<14 每幀多一次呼叫（`mw==NULL` 早退）。Rufim 分支據 9/9 audit 有自己的 v14 message window 實作 → 未來 add/add 衝突風險。

### 3.5 其他

- `parts/message.c`：`PE_GetMessageType` 空佇列 v14 回 -1（message.c:100-105）、`parts_msg_push` v14 no-op（message.c:48-51）。gate 正確，上游 Rance IX 路徑不受影響。
- vm.c:78-236 overlay 訊息系統：2b8597e 之後主路徑已改走 `"message"` 函式，overlay 是 fork 時代遺留的 fallback，且與遊戲原生 R/A 並存（vm.c:2580-2591 同時呼叫 `vm_msg_handle_R()` 又讓遊戲 R 跑）。
- `audio_mixer.c:707-709`：`mixer_get_numof` 從 `config.mixer_nr_channels` 改 `nr_mixers`。base 註解明說「Return the number of mixers specified in System40.ini, even if xsystem4 added more mixers」。全版本行為變更且是 CONFLICT 檔。
- `movie_ffmpeg.c`（diff L50-96）：APEG `SOND` chunk 抽成 `/tmp/xsys4_apeg_audio_<pid>.ogg`，硬編 `/tmp`、無 `mkstemp`。
- 14c2618 自述保留上游 9ed1f52（keep-alive）與 dc105ae（ffi wrap entries）、刻意不採 fab789d（DG_STR_TO_METHOD 簽章檢查）。這三點要在 PR 說明中重述，否則審者會問。
- GB18030（text.c:150-190, parts/text.c:27-43, asset_manager.c:144-186, CharSpriteManager.c:156, StoatSpriteEngine.c:99, MainEXFile.c:132）依 `ain_is_gb18030` 全域旗標（hacks.c:42）；引入 `iconv` 依賴（src/meson.build:237 只在 darwin 加 `-liconv`，Linux glibc 內建、Windows 需 libiconv，未 gate）。上游無等價；可能被要求下沉到 libsys4 utfsjis 層。

---

## 4. 註解與 commit message 的 AI 樣板痕跡

| 項目 | 證據 |
|---|---|
| `Co-Authored-By: Claude ...` | xsystem4：15 / 27 commits（`git log --format='%b' e8bd5ab..22e9496 \| grep -c Co-Authored-By` = 15）。libsys4：6 / 6（7ee607b Opus 4.6、c72780b Opus 4.8、其餘 Fable 5）。 |
| 「Do not merge as-is」commit | 1dae88b 標題 `WIP: UNIMPL HLL batch port from fork — NOT VERIFIED, bisect in progress`，body 末句 `Do not merge as-is.`。歷史需 squash。 |
| commit message 內部參照 | `gui-run-log fb34-fb48`、`wb_064-074`、`reports/handoff-2026-07-06.md`、`scripts/abi-audit.py`、`fork Fix #263`/`#177`、`KEYDBG-traced`、`bisect rounds 1-8`（散見 cbf5774、2b8597e、d6af8eb、02f061c、69000c6、99ac346、c72780b）。對上游讀者無意義。 |
| 原始碼註解引用內部文件/fork | vm.c:54-59（submodule pin 7f8d5298）、vm.c:242 與 2727（`notes/language-update.md`）、Array.c:412（`gui-run-log fb39-fb47`）、PartsEngine.c:913、hll/system.c:40、pe_v14_message.c:18/233/245（「the fork's verified semantics」）、SystemService.c:68（`Fix #263`）、include/xsystem4.h:102。 |
| Emoji | 0 個（Unicode emoji 範圍掃描無命中）。 |
| 非 ASCII 標點 | em dash `—`：base src/include 0 處，HEAD 217 處（`grep -r -c '—'`）。箭頭 `→` 十餘處。上游 base 全 ASCII 註解；送 PR 前需替換。 |
| 過度解釋型註解 | ffi.c:768-771（4 行解釋 UBSan 訊息文字）、heap.c:581-584、vm.c:1014-1020（6 行解釋為何 v14 不 keep-alive，含「verified in the reference fork」）、parts.c:2059-2063、parts/input.c:303-308、hll.h:29-41。上游風格是一到兩行。 |
| 措辭 | `handle gracefully`（ADVEngine.c diff L2705）1 處；`graceful prelink`（99ac346 message）。其餘未見 remarkable/robust/seamless 等。 |
| 檔頭 | message_window.c:1-3 用 `Copyright (C) 2026 xsystem4 contributors` + SPDX 短式；base 檔案（例如 parts/message.c:1-15）用作者名 + GPL 全文段。上游是否接受 SPDX 短式未驗證。 |
| 註解與實作不符 | vm.c:5127-5128「Periodic GC every 10 seconds to collect cycle-garbage」但 `heap_gc_periodic` 直接 return（heap.c:314-315）、`gc_collect_cycles=false`（heap.c:139）。 |

---

## 5. 可獨立成 PR 的最小單元（由最通用到最 Dohna 專屬）

格式：編號 — 一句話 / 檔案 / 依賴。「不依賴」= 可對上游 master 直接 cherry-pick 或重做。

### A. libsys4（獨立 repo）

- **A1** utfsjis 三個 walker 在截斷 2-byte 字元時不越過 NUL / `src/utfsjis.c` +8 行（8c93946）/ 不依賴。上游 20560d4 `sjis_has_hankaku` 仍會 `src++` 越過（L155-163）。
- **A2** `LittleEndian_getDW` 改無號運算消 signed-shift UB / `src/little_endian.h` 1 行（fbaa0a0）/ 不依賴。上游 L26-36 仍 `d0 + (d1 << 16)`。
- **A3** `ht_create` memset 用 `sizeof(struct ht_bucket*)` / `src/hashtable.c` 1 行（d0afb3d）/ 不依賴。
- **A4** `ht_remove_int` / `src/hashtable.c` +18、`include/system4/hashtable.h` +1（c72780b）/ 不依賴，但 PR 說明需給使用場景（xsystem4 `parts_release` 真正移除 parts_table 項）。
- **A5** tests harness（f35b9a1）/ `meson.build` + `tests/` / 依賴 A4；instructions 測試需改對上游 `set_nr_args`。上游 libsys4 無 tests 目錄，接受度未知。
- **不送**：7ee607b 的 instruction width（上游已修）、`free_string` double-free 降級、`sys_exit`→`_exit`。`fflush(stderr)` 可單獨提但要說明理由。

### B. xsystem4 通用修正（不依賴 v14）

- **B1** `AIN_BOOL` HLL 回傳只讀低位元組 / `src/ffi.c:767-773` 1 行實質（440dc23）/ 不依賴。arm64 通用。
- **B2** `FTOI` NaN → 0 / `src/vm.c:3317-3322`（0b8beae）/ 不依賴。
- **B3** `iarray_write_string` 不越過 NUL / `src/hll/iarray.c:83-86`（3c13b81）/ 不依賴；與 A1 同類，可一起送。
- **B4** `file_extension(argv[0])` NULL 檢查 / `src/system4.c:608,611`（6e73d5e）/ 不依賴。
- **B5** `gfx_font_get_size` / `gfx_size_text` NULL 防護 / `src/text.c:117,224-226` / 不依賴（要從同檔的 GB18030 改動拆出來）。
- **B6** `audio_callback` `master==NULL` 防護 / `src/audio_mixer.c:106-109` / 不依賴（同檔 `mixer_get_numof` 改動不能一起送）。
- **B7** `movie_ffmpeg.c` 對 `video.stream`/`audio.stream`/`video.queue` NULL 檢查 / diff L27-42 / 不依賴（APEG 抽取另議）。
- **B8** `RE_plugin_new_with_archive` + `owns_archive` 重構 / `src/3d/reign.c:223-275`、`include/reign.h:177-179,241`（4e09a67）/ 不依賴；SealEngine 用它但 PR 本身不需 SealEngine。
- **B9** `model_create_polygon` / `src/3d/model.c`、`3d_internal.h:227` / 不依賴。
- **B10** `asset_get_archive` / `src/asset_manager.c:124-132`、`include/asset_manager.h:22,46` / 不依賴。
- **B11** `resume.c` 對 `page->index`/`fno`/`name` 的範圍與 NULL 檢查 / `src/resume.c:167-185, 205-216, 245-247, 348-351, 355, 362, 366, 392` / 不依賴；但 `delete_heap`/`heap_rebuild_free_list`（553-575）依賴 D2，要拆開。
- **B12** `PE_RemoveController` 依 index 移除並重編號 / `src/parts/parts.c:2211-2247` / 不依賴；需說明 v14 `EraseLayer` 用法，上游可能要求保留 `index != -1` 的 VM_ERROR 語意。
- **B13** `parts_clear_hit_mask` / `src/parts/parts.c:175, 666-676` / 依賴 `hit_mask` 欄位是否存在於上游 `parts_internal.h`（CONFLICT 檔，未驗證）。
- **B14** X_ASSIGN clamp / `src/vm.c:4699-4709`（a4a623f）/ 依賴 D6：上游是否已實作 `case X_ASSIGN` 未驗證；若無，這不是獨立修正而是 v14 VM 的一部分。

### C. 編碼 / GB18030（中等通用：其他中文版 AliceSoft 遊戲可能受益）

- **C1** `ain_is_gb18030` 偵測 + text.c gb18030 helpers + parts/text.c `extract_multibyte_char` + CharSpriteManager/StoatSpriteEngine 各 1 處 / `src/system4.c:657-675`（去掉 673 行 AINCHECK）、`src/text.c:150-190,232,261`、`src/parts/text.c:27-43`、`src/hacks.c:42`、`include/xsystem4.h` / 依賴 iconv（meson 需跨平台 gate）。上游可能要求下沉到 libsys4。
- **C2** `asset_cg_load_by_name` SJIS↔GBK fallback / `src/asset_manager.c:144-186` / 依賴 C1。
- **C3** MainEXFile `sjis_to_gbk_string` / 依賴 C1；但 MainEXFile.c 整檔是 v14 重寫（+1044/−），要與 D 系列一起拆。

### D. v14 VM 核心（大；必須序列化拆分；全部依賴 A 系列 + 上游 instructions metadata）

- **D1** `struct function_call` 擴充（`env_page`、`delegate_obj_ref`、`delegate_env_ref`、`is_method`、`is_delegate_call`、`dg_return_slots`、`base_sp`）+ 2-slot 值型別（IFACE/OPTION/WRAP）堆疊約定 / `include/vm.h:156-165`、vm.c / 依賴上游 `nr_stack_args`。
- **D2** heap intrusive free list + `global_page_slot=1` + slot 0 null guard / `src/heap.c`、`include/vm/heap.h:86-97`、`src/vm.c:5325-5337` / 不依賴 v14 但影響所有遊戲（§6），需獨立審。v<14 存檔 round-trip 未驗證（resume.c 以 `GLOBAL_PAGE` tag 而非 slot 號序列化，可能無影響）。
- **D3** `hll_call(libno, fno, hll_arg3)` 簽章 + `AIN_WRAP`/IFACE/OPTION marshalling + hll.h wrap helpers / `src/ffi.c:308-720`、`src/hll/hll.h:29-121`、`src/vm.c:2874-2877` / 依賴 D1。v<14 走 `hll_arg2 = -1`（vm.c:2875）自然不進 2-slot 分支。
- **D4** page.c v14 struct 初始化（wrap 成員、繼承、`init_global_struct_v14`、`variable_decltype`）/ `src/page.c` / 依賴 D1。
- **D5** delegate 3-slot entry + `env_page` 閉包 + lambda struct_page 回溯 / `src/vm.c:1560-1660`、`src/page.c:1127-1310` / 依賴 D1、D4。
- **D6** `X_*` opcode 家族 / `src/vm.c` / 依賴 D1。
- **D7** v14 全域 -1 預填 + 值型別歸零（cbf5774）/ `src/vm.c:5339-5348, 5440+` / 依賴 D4。
- **D8** `msgf <= 0` 處理 + `"message"` 函式解析（0b8beae、2b8597e）/ `src/vm.c:2930-2955` / 依賴 D6；overlay（vm.c:78-236）應一併移除。
- **D9** Array.c v14 generic array（HEAD 3068 行 vs base 498 行；base 只有 NV_*/NN_*/NS_* 向量運算全 TODO，v<14 遊戲的 array 走 VM opcode 不走此庫）/ `src/hll/Array.c` / 依賴 D3。需拆：Alloc 保留元素語意、2-slot stride（d6af8eb）、EmplaceBack、query callback（c3b5ff0）。
- **D10** 小型 v14 HLL 模組各自一個 PR / `hll/String.c`(529) `Int.c`(90) `Float.c`(37) `HashMap.c`(261) `TextFile.c`(182) `Delegate.c`(100) `Sys43VM.c`(61) `Clipboard.c`(36) `FileDialog.c`(40) `InstallInfo.c`(24, 2 stub) `TextSurfaceManager.c`(41) / 依賴 D3（wrap helpers）。
- **D11** `hll/system.c`（527 行）/ 依賴 6e73d5e 的 `add_value_to_gsave`/`gsave_to_vm_value` export（savedata.c）；`system_Exit` 壓制（100-108）必須改回真正 exit 或實作正確語意。

### E. PartsEngine v14

- **E1** `parts/message.c` v14 gate（空佇列 -1、push no-op）/ `src/parts/message.c:48-51,100-105` / 不依賴（只對 v14 生效）。
- **E2** `pe_v14_message.c` ring buffer + `static_library_register` PostLink 機制 / `src/hll/pe_v14_message.c`(410)、`src/ffi.c` register / 依賴 D3。
- **E3** `parts/message_window.c` + render.c 掛勾 + `PE_*MessageWindow*` / `src/parts/message_window.c`(242)、`render.c:114-141,614-633`、`include/parts.h:26-40` / 依賴 E2。
- **E4** DOWN-transition click + whole-screen click + `g_EndPartsBusyLoop` global 寫入 / `src/parts/input.c:303-370` / 依賴 E2；global 寫入是 Dohna 專屬。
- **E5** `PE_Update` 時間 fallback、`PE_Set/GetComponentType` v14 raw、`PE_SetPartsPixelDecide` / `src/parts/parts.c:1172-1188, 1990, 2059-2107` / 各自小；PE_Update fallback 依賴 F1（CASTimer 壞才需要）。
- **E6** `pe_v14_activity.c` .pactex loader / 1170 行 / 依賴 E2、E3、C1。
- **E7** `PartsEngine.c` +759 + `pe_v14_prelink.h`(594) + `pe_v14_stubs.h`(615) / 依賴 E2。兩個 header 是「讓遊戲不因 UNIMPL 中止」的 stub 表；上游慣例是 `HLL_TODO_EXPORT`/`HLL_WARN_UNIMPLEMENTED` 宏，需轉換。
- **E8** `SealEngine.c`(2099) / 依賴 B8、B9；99ac346 停用 `ReignEngine.c` 的共用 SealEngine binding（ReignEngine.c diff 5 行），需與上游協調兩套宣告。

### F. Dohna 專屬 workaround（不建議上游；或需以另一種形式重做）

- **F1** CASTimer 原生攔截 / vm.c:245-410, 1379-1432（14c2618 + d3cd045）。
- **F2** dtor 黑名單 + 400K 指令逾時 + `MAX_DESTRUCTOR_DEPTH=4` + `vm_call_insn_limit` unwinding（dead）/ page.c:564-650、vm.c:5137-5175。
- **F3** CParts 自動 `parts_release` / page.c:588-622。
- **F4** CDebug 略過 / page.c:559、vm.c:246,264,280。
- **F5** `is_infra` 建構順序啟發式 / vm.c:5432-5438。
- **F6** `--skip-title` / system4.c、vm.c:2594-2745。
- **F7** `system.Exit` 壓制、SDL message box 抑制、SIGSEGV/SIGTRAP handler、`setvbuf` / hll/system.c:100-108、system4.c:442-472。
- **F8** 1280x720 預設 / system4.c:646-654。
- **F9** overlay 訊息系統 / vm.c:78-236。
- **F10** 全部 `XSYS4_STAGE2_*`、`TRACE_FNO`、`AUTO_CLICK*`、`HOLD_KEYS`、`SCREENSHOT_DIR`、`GAME_DEBUG`、heartbeat。
- **F11** heap 壓力 GC / orphan sweep / `heap_gc_periodic` 停用碼 / heap.c:139, 300-521。
- **F12** `HEAP_TEMP_FLAG` 字串暫存旗標 / include/vm/heap.h:33-36、vm.c:911, 1315-1327。
- **F13** `mixer_get_numof` 改回 `nr_mixers` / audio_mixer.c:707-709。
- **F14** APEG SOND 抽到 /tmp / movie_ffmpeg.c。

---

## 6. 危險模式與 v<14 回歸風險（`ain->version >= 14` gate 逐處檢查）

`grep -rn 'ain->version' src include` 共 54 處在 vm.c、24 處 page.c、其餘 15 處分散。下表列出「行為改變但沒有 gate」的項目：

| 模式 | 位置（HEAD） | base 對照 | gate | v<14 行為變化 |
|---|---|---|---|---|
| `heap_unref` 遇 `ref <= 0` 靜默 return | heap.c:617-624 | base heap.c:128-131 `heap_double_free(slot); VM_ERROR("double free");` | **無** | v<14 的 double-free 從致命錯誤變靜默。`exit_unref` 仍警告（heap.c:729-732）。這就是任務所指「拿掉 double-free guard」。 |
| `heap_free_slot` 重寫 + intrusive free list | heap.c:574-591；heap.h:94-95 | base heap.c:101-105 `heap_free_stack[--heap_free_ptr]` | **無** | 全版本。slot 重用順序改變（LIFO 改為 sweep 後低位在前，heap.c:385 註解），可能暴露或掩蓋既有 UAF。 |
| `heap_ref` 對 `slot <= 1` 靜默 | heap.c:604-611 | base heap.c:118-122 只擋 `-1` | **無** | base 的 global page 在 slot 0，HEAD 移到 slot 1（heap.c:58）；對 global page 的 `heap_ref`/`heap_unref` 全變 no-op。 |
| deferred drain + `heap_gc_inhibit` | heap.c:634-712 | base 立即遞迴 `delete_page` | **無** | 全版本：unref 改為排隊、`ref=-1` in-progress 標記、drain 時才呼叫 destructor；佇列滿（1M）走 `free_page` 不呼叫 dtor（heap.c:648-668）。destructor 時序對 v<14 是可觀察的行為改變。 |
| dtor 黑名單（預先） | page.c:578-585 | 無 | `ain->version >= 14` | 正確 gate。 |
| dtor 深度限制 4 層、400K 指令逾時黑名單 | page.c:564-565, 627-647 | base page.c:258-264 無限制 | **無** | v<14 遊戲 destructor 巢狀 >4 層被跳過；destructor 跑超過 400K 指令會被永久列入黑名單。 |
| CParts 自動 parts_release | page.c:599-622 | 無 | 無（名字不命中則 -1） | v<14 只多一次名字掃描。 |
| `keep_alive_enabled` 排除 v14 | vm.c:1019-1022 | 上游 9ed1f52 對 v6.1+ | `AIN_VERSION_GTE(6,1) && < 14` | 正確 gate。 |
| `function_call` 參數 ref 語意 | vm.c:1305-1327 | base vm.c:404-413 只對 `AIN_REF_TYPE` `heap_ref` | **無** | HEAD 對 `AIN_STRUCT`/`AIN_DELEGATE`/`AIN_ARRAY_TYPE`/`AIN_ARRAY`/`AIN_WRAP` 也 ref，`AIN_STRING` 走 `HEAP_TEMP_FLAG`。v<14 值型別 struct/array 參數多一個 ref；`variable_fini` 是否對應 unref 未驗證。需 v<14 遊戲回歸。 |
| `HEAP_TEMP_FLAG` | vm.c:911（`stack_push_string`）、heap.c:623 `HEAP_REF()` | base 無 | **無** | 全版本 refcount 高位元被借用；任何直接讀 `heap[i].ref` 的地方（debug.c、resume.c `o->ref = heap[slot].ref` resume.c:203）會看到 0x40000000 位元。resume 序列化含此位元的風險未驗證。 |
| 壓力 GC（alloc 時） | heap.c:511-521 | 無 | **無** | v<14 heap ≥ 10000 且 free < 1024 時跑 `heap_gc()`（O(heap) sweep + 重建 free list）。`gc_collect_cycles=false` 所以不回收活物件，但 free list 順序被重排。 |
| `heap_gc_periodic` | heap.c:312-319 | 無 | 停用（return） | 無效果。 |
| slot 0 null guard / global 移到 1 | heap.c:58；vm.c:5325-5337 | base `heap_free_ptr = 1; // global page at index 0`（heap.c:76） | **無** | 全版本。任何以 `slot == 0` 判斷 global 的程式碼要重查；resume 以 tag 序列化（resume.c:163）但 rsave heap 索引是否含 0/1 未驗證。 |
| `handle_events` 每 256K 指令 + heartbeat | vm.c:5096-5135 | base vm.c:692 已有 `handle_events()` 呼叫（位置未比對） | **無** | 全版本。 |
| input.c mouse hold 50ms / 1ms pump 節流 / `handle_window_events` 改 `SDL_PeepEvents` | input.c:34-85, 117-153, 680-686 | base 直接 `key_state[code] = e->state == SDL_PRESSED` | **無** | 全版本輸入時序改變：UP 最多延遲 50ms；同一 ms 內第二次 `handle_events` 直接 return。 |
| `mixer_get_numof` 回 `nr_mixers` | audio_mixer.c:707-709 | base 回 `config.mixer_nr_channels` 並註明理由 | **無** | 全版本 `SystemService.GetMixerNumof` 語意改變。 |
| libsys4 `free_string` 降級、`sys_exit`→`_exit` | libsys4 7ee607b | 上游仍 `ERROR`/`exit` | **無** | 全版本。`_exit` 跳過 atexit（SDL/audio 清理、未 flush 的 FILE*）。 |
| `error_handler` no-op | system4.c:442-447 | base 顯示 SDL message box | **無** | 全版本。 |
| `execinfo.h` 無條件 include | system4.c:22 | 無 | **無** | MinGW 無 `execinfo.h` → Windows 建置預期失敗（未實測）。 |
| `PE_GetMessageType` -1 / `parts_msg_push` no-op | parts/message.c:104, 50 | — | `>= 14` | 正確。 |
| render.c message window 掛勾 | render.c:628-633 | — | `mw == NULL` 早退 | v<14 僅多一次呼叫。 |
| Array.c | 全檔 | base 498 行全 TODO 的 NV_*/NN_*/NS_* | 3 處 gate + `hll_arg3` 型別驅動 | v<14 不 link 這些函式（走 VM opcode），影響極小；但 base 的 NV_eneq/NV_sceq 兩個實作是否保留未驗證。 |
| ffi.c marshalling | ffi.c:308-720 | — | 由 `AIN_WRAP`/`IFACE`/`OPTION` 型別與 `hll_arg3` 驅動；v<14 `hll_arg2=-1` | 型別 gate 自然成立；但 `hll_call` 簽章改變影響所有呼叫點。 |

`vm_call_insn_limit`（F2）目前沒有任何非零寫入點（vm.c:416, 1697-1706, 5171），整段 unwinding 是 dead code。

---

## 7. 建議的蒸餾順序（不是執行指令，是審查結論）

1. 先送 A1–A4 與 B1–B7（合計 < 100 行，零依賴），每個一個 PR，commit message 重寫、去 Co-Authored-By、去內部參照、ASCII 註解。這批能建立信用。
2. libsys4 rebase 到 20560d4，丟棄 7ee607b 的 instructions 部分。
3. 在 wip 分支內先做「儀器剝離」：移除 §1.2 全部 16 個 env、§1.3 heartbeat、§1.4 停用碼、AINCHECK。剝離後重跑既有 ASan/UBSan probe 確認行為不變，這一步本身不送上游。
4. 對 §6 的無 gate 項目逐一決定：加 `ain->version >= 14` gate，或還原 base 行為。這一步之前不能談 D2（heap）與 D3（ffi）上游 PR。
5. B8–B10（3D/asset API）與 C1（GB18030）可在第 4 步同時進行，它們不碰 heap。
6. D 系列依 D1→D2→D3→D4→D5→D6→D7→D8→D9 順序，每個 PR 附一個能在 v14 AIN 上跑的最小 bytecode 測試（wip 已有 `work/string-stage4-20260926/probe` 的 fixture 可改造）。
7. E、F 系列在 Personality assert 解決、遊戲可玩之前不談上游。

---

## 附錄 A：wip 與上游共同觸碰的 29 個檔案

`include/audio.h include/parts.h include/reign.h src/3d/3d_internal.h src/3d/model.c src/3d/reign.c src/asset_manager.c src/audio.c src/audio_mixer.c src/hacks.c src/hll/AnteaterADVEngine.c src/hll/CGManager.c src/hll/CharSpriteManager.c src/hll/ChipmunkSpriteEngine.c src/hll/KiwiSoundEngine.c src/hll/Math.c src/hll/PartsEngine.c src/hll/ReignEngine.c src/hll/StoatSpriteEngine.c src/hll/SystemService.c src/input.c src/meson.build src/movie_ffmpeg.c src/parts/motion.c src/parts/parts.c src/parts/parts_internal.h src/parts/render.c src/parts/text.c src/text.c`

## 附錄 B：主要查證指令

```
cd <PORT>/worktrees/xsystem4-cn-on-upstream
git diff e8bd5ab..22e9496 --stat -- src include meson.build
git diff e8bd5ab..22e9496 -- src include meson.build > <scratchpad>/distill/full.diff
grep -rn -a -E 'getenv\(' src include
git grep -n -E 'getenv\(' e8bd5ab -- src include
grep -rn -a -E 'ain->version' src include
git merge-tree --write-tree --name-only upstream/master HEAD
git diff --name-only --diff-filter=M e8bd5ab..HEAD -- src include meson.build   # 53
git diff --name-only e8bd5ab..upstream/master -- src include meson.build        # 55
git show 5d032d7 -- src/parts/parts.c src/hll/AnteaterADVEngine.c src/parts/save.c
git log --format='%b' e8bd5ab..22e9496 | grep -c 'Co-Authored-By'               # 15
git log -S'<string>' --format='%h %ad' e8bd5ab..HEAD -- src include             # 引入點追溯
cd subprojects/libsys4
git merge-base 8c93946 20560d4                                                   # ed74c9e
git log --oneline ed74c9e..8c93946; git diff ed74c9e..20560d4 -- src/instructions.c
git show 20560d4:src/string.c | sed -n 37,44p; git show 20560d4:src/system.c | sed -n 169,173p
```

未驗證項目（本文明確標示）：v<14 存檔 round-trip、`variable_fini` 對 struct 參數的 unref 對稱性、上游是否已有 `case X_ASSIGN`、上游 `parts_internal.h` 是否有 `hit_mask`、Windows 建置、上游對 SPDX 檔頭的接受度、其他 v14 遊戲是否同樣 `msgf=0`。
