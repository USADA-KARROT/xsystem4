# 上游整合差距分析 2026-09-26

wip/post-checkpoint-2026-07-06 (`22e9496`) 對 nunuhara/xsystem4 master (`04333ad`) 的整合難度量化、逐 commit 分類與分批 cherry-pick 順序。全部為靜態分析（scratch clone 內 merge-tree 與序列 cherry-pick 模擬），沒有建置、沒有執行遊戲。

## 0. 摘要

| 項目 | 數值 | 證據 |
|---|---|---|
| 共同祖先 | `e8bd5ab` | `git merge-base 22e9496 upstream/master` |
| 上游新增 commit（含 merge / 非 merge） | 68 / **48** | `git rev-list --count [--no-merges] e8bd5ab..upstream/master` |
| 9/9 audit 基準 `fa09b00` 之後新增（含 merge / 非 merge） | 21 / 17 | `git rev-list --count [--no-merges] fa09b00..upstream/master` |
| wip 端非 merge commit | 27 | `git rev-list --count --no-merges e8bd5ab..22e9496` |
| wip 端 src/include/meson 改動 | 73 檔 +19,786/−1,588 | `git diff --stat e8bd5ab 22e9496 -- src include meson.build` |
| 上游端 src/include/meson 改動 | 55 檔 +1,952/−503 | 同上換 upstream/master |
| 整體 merge-tree 衝突 | **4 檔 + 1 submodule**（與 9/9 相同，沒有新增） | 第 2 節 |
| 序列 cherry-pick 模擬 | 48 個中 **42 個文字上乾淨、6 個衝突** | 第 3 節 |
| 分類 | A=10、B=20、C=11、D=5、純 pin=2 | 第 4 節 |
| libsys4 衝突 | `src/instructions.c` 1 處（語義等價） | 第 7 節 |

任務文字中的「68 個非 merge commit」實際是含 merge 的總數；非 merge 為 48。9/9 audit 的「47 筆」同樣是 `e8bd5ab..fa09b00` 含 merge 數（非 merge 31）。

## 1. 環境與方法

- scratch clone：`<scratchpad>/upstream-merge/xsystem4`（`git clone --no-checkout` 自本機 worktree，`upstream` remote fetch 後 `upstream/master = 04333ad689d7e517a93d58141bb214c19ba16129`）
- libsys4 scratch clone：同目錄 `libsys4/`（`upstream/master = 20560d4d6d41a7196bd6b8409bcbb974f9de4af0`，共同祖先 `ed74c9e`）
- worktree `<PORT>/worktrees/xsystem4-cn-on-upstream` 未做任何 merge/checkout，僅讀取。
- 序列模擬在 scratch clone 的 `sim` 分支上進行（從 `22e9496` 起逐個 `git cherry-pick -x`，衝突則記錄後 `--abort`，純 submodule pin 衝突則丟棄 pin 後 continue），結束於 `52a078c`，42 個 commit 進入。
- 中間產物：`scratchpad/upstream-merge/{mt-xsystem4.txt, mt-xsystem4-msgs.txt, cherry-sim.txt, cherry-seq.txt, conflict-*.txt, upstream-commit-files.txt, upstream-textlayout-diffs.txt, wip-text-diff.txt, mt-libsys4.txt}`。

## 2. 整體 merge-tree：22e9496 × upstream/master

`git merge-tree --write-tree 22e9496 upstream/master` → 結果樹 `bc8bfa224e0c3b925e06be39962e909807fa34b3`，exit 1。

| 衝突檔 | 衝突塊（結果樹行號） | 兩側內容 | 9/9 audit 第 8 節 |
|---|---|---|---|
| `src/audio_mixer.c` | 518–541 | wip：`channel_open()` 內 `if (type == ASSET_SOUND \|\| type == ASSET_VOICE)` 設定 volume/loop/mixer_no 的整段；上游（d5b27a0）：整段刪除、搬到 `channel_open_archive_data(dfile, type, metadata_no)` | 已列 |
| `src/parts/motion.c` | 544–548 | wip：一個空行 + `// TODO: use skip`；上游（fa09b00）：刪除該註解、實作 skip | 已列 |
| `src/parts/parts_internal.h` | 96–102 | wip：`char ch[5]; float advance;`；上游（a19c200）：`char ch[4]; int advance;` | 已列 |
| `src/parts/parts_internal.h` | 452–456 | wip：`struct parts_message_window *message; int alpha_clipper_parts_no;`；上游（461eca1）：兩行皆無（`alpha_clipper_parts_no` 搬進 `struct parts_params`） | **9/9 未細列**（461eca1 是 9/14 進的，但 9/9 audit 對 fa09b00 做時尚未存在；檔案層級仍是同 4 檔） |
| `src/parts/text.c` | 101–107 | wip：`extract_multibyte_char(str, ch->ch)` + `ceilf(text_style_width(...))`；上游（a19c200）：`extract_sjis_char(str, ch->ch)` + `gfx_size_char(&t->ts, ch->ch)` | 已列 |
| `subprojects/libsys4` | gitlink | base `ed74c9e` / wip `8c93946` / 上游 `20560d4` | 已列 |

**結論：與 9/9 audit 第 8 節相比，衝突檔集合完全相同，沒有新增檔案。** fa09b00 之後上游 17 個非 merge commit 對 wip 全部自動合併；唯一差異是 `parts_internal.h` 多了第二個衝突塊（461eca1）。自動合併的檔案共 28 個（`Auto-merging` 行，見 `mt-xsystem4-msgs.txt`）。

自動合併結果樹的殘留檢查（`git grep` 於 `bc8bfa2`）：
- `parts->alpha_clipper_parts_no`：0 處（461eca1 對 render.c/debug.c/save.c 的改動全部自動套用）。
- `text_style_width`：僅 `src/parts/text.c:103`（衝突塊 wip 端）；`include/gfx/font.h` 的定義已被上游刪除，`CharSpriteManager.c:188`、`StoatSpriteEngine.c:125` 已自動改為 `gfx_size_char`。
- `gfx_size_char_kerning`：0 處（上游刪除，wip 未使用）。
- `channel_open_archive_data(` 呼叫點：`audio.c:414` 已自動變成三參數版；`wav_prepare_from_archive_data` 各呼叫點已自動加 `AUDIO_NO_METADATA`。
- `PE_SetSpeedupRateByMessageSkip` 定義：只剩 `motion.c:557`（wip 原本 `parts.c:2171` 的定義被 fa09b00 的刪除自動套用）。
- `HLL_EXPORT(Asin` / `HLL_EXPORT(Acos`：**各 2 處**（`Math.c:316-317` 上游 `asinf/acosf`、`Math.c:346-347` wip `Math_Asin/Math_Acos`）→ 見第 6.7 節。

## 3. 序列 cherry-pick 模擬（cherry-seq.txt）

從 `22e9496` 依上游拓樸序逐個 cherry-pick：

| 結果 | 數量 | commit |
|---|---|---|
| CLEAN | 40 | 其餘 |
| CLEAN（丟棄 libsys4 pin 後進入） | 2 | 81f9113、275d717 |
| CONFLICT（純 submodule pin，空 commit） | 2 | 77d9e77、5bb36c7 |
| CONFLICT（程式碼） | 4 | d5b27a0 `audio_mixer.c`；a19c200 `parts_internal.h, parts/text.c`；fa09b00 `motion.c`；461eca1 `parts_internal.h, render.c` |

注意兩點：
1. 「文字乾淨」不等於「可編譯」。證據：序列中 fa09b00 被跳過後 addc26c 仍 CLEAN 進入，但 `sim:src/parts/motion.c:551` 引用 `msgskip_speedup_rate`，該變數只在 fa09b00 定義 → 編譯失敗。同理 81f9113 需要 libsys4 的 `include/system4/zlib.h`（libsys4 `625b0b8`），275d717 需要 libsys4 `e3d994f`（`flat_key_data_graphic.align`）。
2. 逐一獨立模擬（`cherry-sim.txt`，`--merge-base=<c>^`）會出現 9 個假警報（62d38aa、5cc163a、9654950、d68baa7、167d75d、960c97e 等），都是上游自身順序依賴，不是與 wip 的衝突；序列模擬已排除。

## 4. 48 個上游非 merge commit 分類

分類定義：A=與 wip 無檔案交集、可直接 cherry-pick；B=有檔案交集但語義獨立、序列模擬乾淨；C=直接碰 wip 核心或需手解；D=Dohna 不需要。「Dohna 用到」依 `<cn-dump>/libraries.txt` 的 HLL 名單（system ADVEngine AFAFactory AnteaterADVLogList Array CGManager ChipmunkSpriteEngine Clipboard CommonSystemData CrayfishLogViewer Delegate EXWriter FileDialog FileOperation Float HashMap HTTPDownloader IbisInputEngine InputString InstallInfo Int KiwiSoundEngine MainEXFile MarmotModelEngine Math MsgSkip OutputLog PartsEngine PassRegister SealEngine String Sys43VM SystemService TextFile TextSurfaceManager VSFile）。

| sha | 日期 | 主旨 | 分類 | 序列 | 交會點 / 備註 |
|---|---|---|---|---|---|
| d8479ad | 07-07 | parts: Fail construction build without target texture | A | CLEAN | `parts/construction.c` wip 未動 |
| c0cdf4f | 05-17 | debugger: Add "3d" debugger commands | B | CLEAN | 動 `ReignEngine.c` 200 行；wip 只改該檔尾端 4 行（註解掉 `HLL_LIBRARY(SealEngine…)`） |
| 6d05546 | 07-15 | input: Handle numpad keys with NumLock disabled | B | CLEAN | wip `input.c` +205 行（v14 輸入），不同區域 |
| 81f9113 | 07-18 | Use libsys4 wrappers for zlib operations | B* | CLEAN-pin-dropped | 無 .c 交集；**需 libsys4 ≥ 625b0b8**（`system4/zlib.h`）；Dohna 用 PassRegister |
| d17fc30 | 07-21 | pl_mpeg: Support MPEG-2 program stream headers | A | CLEAN | 只動 `include/pl_mpeg.h` |
| f0c4db7 | 07-22 | movie_ffmpeg: Use nb_samples for audio frame length | B | CLEAN | 1 行；wip `movie_ffmpeg.c` +140 行別處 |
| d5b27a0 | 07-25 | KiwiSoundEngine: Fix BGM loop for Rance 9 | **C** | CONFLICT | `audio_mixer.c:518-541`：wip 的 `ASSET_VOICE` 分支 vs 上游把 metadata 邏輯搬進 `channel_open_archive_data(dfile, type, metadata_no)`；KiwiSoundEngine 是 Dohna 用的（wip +399 行） |
| b68e162 | 07-06 | 3d: Implement DrawShadow mesh attribute | B | CLEAN | `3d_internal.h` wip +1；Dohna SealEngine 宣告有 `SetInstanceDrawShadow`（seal-hll.txt:43），wip `SealEngine.c:588` 已寫 `ri->draw_shadow` |
| 09a0e33 | 07-29 | 3d: Disable specular with zero power | A | CLEAN | `3d/renderer.c` wip 未動 |
| a2524ba | 08-02 | PartsEngine: Stabilize save format for Rance 9 | A | CLEAN | `parts/save.c` wip 未動；升 `CURRENT_SAVE_VERSION`（wip 3 → 上游最終 7） |
| 68e8257 | 08-04 | game_compatibility.md: Add Rance 9 (JA) | D | CLEAN | 文件 |
| 77d9e77 | 08-06 | Update libsys4 | pin | CONFLICT | 純 gitlink → a8df981…39eec1a；由第 7 節 libsys4 整合取代 |
| e699fb0 | 08-08 | DAP: ensure messages are null-terminated | A | CLEAN | `debugger_dap.c` |
| 33f1295 | 08-09 | Resolve archive paths in asset_manager_load_archive | B | CLEAN | `gamedir_path` 從 `CGManager.c` 搬到 `asset_manager.c`；wip 兩檔各 +86/+51 行別處；Dohna 用 CGManager |
| 5bb36c7 | 08-09 | Update libsys4 | pin | CONFLICT | 純 gitlink → cbc57ce |
| 62d38aa | 08-11 | parts: Reference construction process CGs by name | B | CLEAN | `parts_internal.h` 改 construction 結構（wip 未動該區） |
| 9bb871d | 08-11 | parts: Fix loss of line breaks in saved multi-line text | A | CLEAN | `parts/save.c` |
| d800e12 | 08-14 | Math: Replace rand() with mt19937 | B | CLEAN | 上游改 `Rand/RandF/SetSeed`；wip `Math.c` +113 行是 `Asin/Acos/Floor/Round/Bezier/Clamp/MTRand*`，不同符號；Dohna Math 宣告有 `Rand/RandF/MTRand` |
| 4e20a49 | 08-13 | parts: use ceilf(ch->advance) for text layout | **C** | CLEAN | `render.c` `x += ceilf(ch->advance)`（wip 改了同函數簽名但非同行）；`ChipmunkSpriteEngine.c` `SP_GetFontWidth` 取整（Dohna 用 Chipmunk）；見第 5 節 |
| 5cbc697 | 08-14 | parts: use bold width for text layout | **C** | CLEAN | `src/text.c` `gfx_size_text` edge_advance 改 `ceilf(bold_width)*2`（wip 同函數加了 GB18030 skip 與 null guard，不同行）；見第 5 節 |
| a19c200 | 08-14 | parts: clean up text layout logic | **C** | CONFLICT | `parts_internal.h:96-102` `ch[5]/float` vs `ch[4]/int`；`parts/text.c:101-107` `extract_multibyte_char` vs `extract_sjis_char`；刪 `text_style_width`/`gfx_size_char_kerning`；見第 5 節 |
| e3bf653 | 08-14 | Fix crash in Shaman's Sanctuary | B | CLEAN | `motion.c` PARTS_MOTION_CG；wip 只加一個空行 |
| 5d032d7 | 08-14 | Fix various memory leaks | B | CLEAN | `parts.c`：上游在 `parts_state_free` 的 ANIMATION case 加 `free_string(cg_name)`（hunk @190）；wip 在同函數開頭加 `parts_clear_hit_mask`（hunk @172），不同 hunk；`AnteaterADVEngine.c` wip +66 行別處 |
| 5fb3c49 | 08-14 | parts: clear cached font when rerendering text | **C** | CLEAN | `parts/text.c` `parts_text_rerender` 加 1 行；與 GB18030 無關；歸 C 只因同檔同批 |
| e76762d | 08-14 | Implement EFFECT_DOWN_UP_CROSSFADE | A | CLEAN | shader + `effect.c` |
| 05820dc | 08-16 | parts: RemoveController returns delegate indices | B(!) | CLEAN | 上游把 `erase_number_list` 內容從 `p->no` 改為 `p->delegate_index`；wip 重寫了同函數 `PE_RemoveController`（v14 EraseLayer 依 index 移除）但該行未動 → 自動合併。**Dohna v14 的 EraseLayer 期望 parts no 還是 delegate index：未驗證** |
| 135cae2 | 08-20 | Mitigate UI performance issue in Rance 9 | D | CLEAN | `hacks.c` Rance9 專屬 bytecode 重寫；wip 只加 `ain_is_gb18030` 宣告；無害 |
| bf3430d | 08-23 | Improve EFFECT_VWAVE_CROSSFADE | A | CLEAN | shader |
| b30a107 | 08-23 | Implement EFFECT_VWAVE_SCROLL_CROSSFADE | A | CLEAN | shader + `effect.c/h` |
| 4233c7d | 08-24 | Fix EFFECT_CROSSFADE_MOSAIC | A | CLEAN | shader |
| fa09b00 | 08-25 | Implement PartsEngine.SetSpeedupRateByMessageSkip | **C** | CONFLICT | `motion.c:544-548` 純排版（wip 多一空行）；上游同時刪 `parts.c` 的舊 `PE_SetSpeedupRateByMessageSkip`（wip 該函數在 `parts.c:2171`，整體 merge 自動刪除）。Dohna PartsEngine 宣告無此函數，功能上不需要，但檔案上是 addc26c 的前置 |
| 02f29b6 | 08-23 | 3d: Implement SealEngine.{Set,Get}EdgeReductionRate | B(!) | CLEAN | 上游加 `plugin->edge_reduction_rate` 進 outline 計算並在 `ReignEngine.c` SEAL_EXPORTS 綁定；**wip 的 `HLL_LIBRARY(SealEngine…)` 在 `ReignEngine.c:2728` 已註解掉，改由 `hll/SealEngine.c` 提供**，而 `SealEngine.c:1289` 只把值存進 `se_plugin_ext[p].edge_reduction_rate`，不進渲染 → 合併後需把 wip setter 改寫入上游的 plugin 欄位才生效 |
| e14b4bc | 08-23 | 3d: Implement per-instance billboard vertices and UVs | B | CLEAN | `3d/reign.c`、`renderer.c`、`reign.h`；wip `3d/reign.c` +42 行（external-archive plugins） |
| 5cc163a | 08-23 | 3d: Load billboard frame images by name | B | CLEAN | 同上 |
| 0211877 | 08-23 | 3d: Implement SealEngine.{Set,Get}InstanceGrayscaleRate | B(!) | CLEAN | 上游加 `inst->grayscale_rate` + shader uniform；Dohna SealEngine 宣告有 `Set/GetInstanceGrayscaleRate`（seal-hll.txt:40-41）；wip `SealEngine.c:573-582` 只存 `se_instance_ext[p][i].grayscale_rate` → 同 02f29b6，需接到上游 `RE_instance.grayscale_rate` |
| 9654950 | 09-13 | 3d: Implement SealEngine.SetDrawOption 1 (lighting) | B | CLEAN | `renderer.c` wip 未動；`3d/reign.c` 別處 |
| 11ad7a5 | 09-13 | 3d: Implement UV tiling | B | CLEAN | `3d/model.c` +8；wip `model.c` +109（procedural polygon models），不同區域 |
| addc26c | 08-23 | parts: Implement PartsEngine.GetComponentSpeedupRateByMessageSkip | **C** | CLEAN(但編不過) | 依賴 fa09b00 的 `msgskip_speedup_rate`；`PartsEngine.c:612` wip 是 `HLL_TODO_EXPORT`，上游改 `HLL_EXPORT` |
| 267f7bb | 08-23 | parts: Support PartsFunc func_id assignments in Blade Briders | C/D | CLEAN | `PartsEngine.c` `PartsFunc` 分派表重編號；Dohna PartsEngine 宣告無 `PartsFunc`（grep 無結果）；wip `PartsEngine.c` +759 行為 v14 訊息佇列，不同區域 |
| d68baa7 | 08-24 | parts: Save and load 3DLayer components | B | CLEAN | 新檔 `3d/save.c`（不依賴 libsys4 新 API）；`meson.build` 加一行；升存檔版本 |
| 275d717 | 08-24 | parts: Honor FLAT graphic key alignment | B* | CLEAN-pin-dropped | **需 libsys4 ≥ e3d994f**（`uk2`→`align`） |
| 167d75d | 08-28 | parts: Implement PartsFunc 112 (GetText) | C/D | CLEAN | 依賴 267f7bb（case 編號為 canonical 後）；加 `PE_GetText` 到 `parts/text.c`；Dohna 不用 PartsFunc |
| 6ea8af9 | 09-03 | parts: Fix global params after releasing a parent | B(!) | CLEAN | 上游把 `root_pos` 改成 `root_params` 並讓無 parent 的 parts 也走 `parts_combine_params`；wip 在 `parts_release` 加 `parts_message_window_free`（相鄰 hunk 自動合併）。v14 parts 的 global 計算行為會改變，需實測 |
| 461eca1 | 09-14 | parts: Inherit alpha clipper from parent parts | **C** | CONFLICT | `parts_internal.h:452-456`（wip 在 `alpha_clipper_parts_no` 前插 `message` 欄位、上游刪 `alpha_clipper_parts_no`）；序列時 `render.c` 也衝突（a19c200 被跳過所致），整體 merge 則自動合併 |
| e93cdda | 09-22 | SengokuRanceFont: Fall back to Gothic TTF | D | CLEAN | Dohna 不用 SengokuRanceFont |
| 03f9e7e | 08-02 | Implement SystemService.GetSystemBufferName | D | CLEAN | Blade Briders 專屬；Dohna SystemService 宣告無此函數；wip `SystemService.c` +422 行無同名 |
| e61f4b3 | 08-23 | Math: Add Asin() and Acos() | **C** | CLEAN(語義衝突) | 上游 `HLL_EXPORT(Asin, asinf)` 回傳弧度；wip `Math.c:58-66` `Math_Asin = rad2deg(asinf(x))` 回傳度（與上游自己的 `Math_Sin = sinf(deg2rad(x))` 一致）；合併後同一 `HLL_LIBRARY` 內 Asin/Acos 各出現兩次 |
| 960c97e | 09-17 | Update game_compatibility.md | D | CLEAN | 文件 |

計數：A=10、B=20（含 B* 2、B(!) 4）、C=11、D=5、pin=2，合計 48。

## 5. text layout 四 commit × GB18030

wip 的 GB18030 字元處理（`git diff e8bd5ab 22e9496 -- src/parts/text.c src/parts/parts_internal.h src/text.c`，存於 `wip-text-diff.txt`）：
- `src/parts/parts_internal.h:96` `char ch[5]`（原 `ch[4]`）：4-byte GB18030 + NUL。
- `src/parts/text.c:27-55` `extract_multibyte_char()`：`0x81-0xFE` 首位元組、第二位元組 `0x30-0x39` 判 4-byte，寫 `dst[0..3]` 與 `dst[4]='\0'`。
- `src/text.c:150-196` `gb18030_skip_char_bytes()` / `gb18030_char2unicode()`（iconv GB18030→UTF-32LE）；`char_to_code()` 在 `ain_is_gb18030` 時走此路徑；`gfx_size_text()` 與 `_gfx_render_text()` 的 skip 改為 GB18030-aware。
- `src/hll/StoatSpriteEngine.c:96`、`CharSpriteManager.c:153` 各自的 `extract_sjis_char` 也加了 GB18030 分支並把 buffer 改成 `[5]`（Dohna 不用這兩個 HLL，但改動在）。

逐 commit：

| commit | 對中文的影響 | 判定 |
|---|---|---|
| 4e20a49 | `render.c` 每字 `x += ceilf(ch->advance)`；`ChipmunkSpriteEngine.SP_GetFontWidth` 取整。中文全形 glyph advance 在整數字號下通常已是整數，影響應限於小數 advance 的字型；Dohna 用 Chipmunk，UI 寬度查詢值可能 +1px。 | 不退化，但寬度可能變化，需實機比對 |
| 5cbc697 | 只改 bold/edge 相關（`gfx_size_text` edge_advance 乘 2、line height 加 bold、`PE_SetFont` 記 `bold_width`）；與字元解碼無關。與 wip `gfx_size_text` 的 GB18030 改動不同行，自動合併（結果樹 `src/text.c:224-236` 兩者並存）。 | 不退化 |
| a19c200 | **關鍵**。上游把 `text_style_width`（inline，含 bold/edge/scale）併入 `gfx_size_char`，新增 `gfx_size_char_unstyled`；`gfx_size_char` → `char_to_code` → wip 的 `gb18030_char2unicode`，字寬查詢路徑保留（結果樹 `src/text.c:161-173` 已證實兩者並存）。衝突兩處必須手解：(1) `parts_internal.h` **保留 `char ch[5]`**，`advance` 可改 `int`；若誤採 `ch[4]`，`extract_multibyte_char` 寫 `dst[4]` 會溢位到相鄰欄位；(2) `parts/text.c` **保留 `extract_multibyte_char`**、寬度行採上游 `gfx_size_char(&t->ts, ch->ch)`。`text_style_width` 被刪後 wip 的 Stoat/CharSprite 呼叫點已由上游同 commit 自動改掉（結果樹 grep 證實只剩衝突塊一處）。`advance` float→int 對中文 proportional 排版的累積誤差：未驗證。 | 手解正確則不退化；解錯（採 `ch[4]`）會記憶體損壞 |
| 5fb3c49 | `parts_text_rerender` 前清 `t->ts.font_size = NULL`，字型快取重取；GB18030 字型仍由 `char_to_code` 決定。 | 不退化 |

四個應同批進入（5cbc697 把 bold 加在 line height、a19c200 又搬進 `text_style_height`，分開進會有中間狀態）。

## 6. C 類交會點細述

### 6.1 d5b27a0（audio_mixer.c）
- wip：`channel_open()` 內 `if (type == ASSET_SOUND || type == ASSET_VOICE) { wai = (type==ASSET_SOUND) ? wai_get(no) : NULL; volume=100; loop…; mixer_no = wai ? wai->channel : 1; }`（`22e9496:src/audio_mixer.c:518-537`）。
- 上游：整段搬進 `channel_open_archive_data(dfile, type, metadata_no)`，且 `init_channel()` 已預設 `volume=100, loop_start=0, loop_end=frames, loop_count=1, mixer_no=0, no=-1`（結果樹 `audio_mixer.c:565-572`），metadata 分支只在 `ASSET_SOUND` 設 `mixer_no`、`ASSET_BGM` 設 bgi 值。
- 解法：採上游結構，metadata 分支改 `if (type == ASSET_SOUND || type == ASSET_VOICE) { wai = type==ASSET_SOUND ? wai_get(metadata_no) : NULL; mixer_no = wai ? wai->channel : 1; }`。wip 的 `wav_prepare_voice` → `audio_prepare(&wav, id, ASSET_VOICE, no)` → `channel_open(type, no)` 路徑不需改。

### 6.2 fa09b00 + addc26c（motion.c / parts.c / PartsEngine.c）
- `motion.c:544-548` 衝突是 wip 多插一個空行（`git diff e8bd5ab 22e9496 -- src/parts/motion.c` 只有 `+` 一空行），採上游即可。
- 上游同 commit 刪 `parts.c` 的舊 `PE_SetSpeedupRateByMessageSkip`；結果樹只剩 `motion.c:557` 一個定義。序列 cherry-pick 若跳過 fa09b00，addc26c 會引用未定義的 `msgskip_speedup_rate`（`sim:src/parts/motion.c:551`）。兩者必須同批。
- Dohna PartsEngine 宣告沒有 `SetSpeedupRateByMessageSkip` / `GetComponentSpeedupRateByMessageSkip`（libraries.txt grep 無結果），功能上可不要，但為了後續跟上游對齊建議一起進。

### 6.3 267f7bb + 167d75d（PartsEngine.c PartsFunc）
- 序列乾淨。上游 `PartsFunc` 分派表重編號（Rance9 舊 id +3）並加 case 112 `GetText`；wip `PartsEngine.c` +759 行是 v14 訊息佇列（`parts_enqueue_message*`）與 `pe_v14_*` 綁定，不碰 `PartsFunc`。
- Dohna 宣告無 `PartsFunc`。167d75d 依賴 267f7bb（編號）。

### 6.4 461eca1（alpha clipper → parts_params）
- `parts_internal.h:452-456`：保留 wip 的 `struct parts_message_window *message;`，刪 `int alpha_clipper_parts_no;`（上游已搬進 `struct parts_params`，`parts_internal.h:399`）。
- `render.c`、`debug.c`、`save.c`、`parts.c` 在整體 merge 自動套用（結果樹無 `parts->alpha_clipper_parts_no` 殘留，`render.c:136` 已是 `parts->global.alpha_clipper_parts_no`）。wip 新增的 `message_window.c` / `pe_v14_*.c` 沒有引用該欄位。
- 若走序列 cherry-pick 且 a19c200 已先進，`render.c` 應不再衝突（序列衝突是因 a19c200 被跳過時 `parts_render_text` 上下文不同）；未在該順序下實測。

### 6.5 4e20a49 / 5cbc697 / a19c200 / 5fb3c49
見第 5 節。

### 6.6 上游 3D（02f29b6 / 0211877 / b68e162 / e14b4bc / 5cc163a / 9654950 / 11ad7a5）× wip SealEngine.c
- 不是文字衝突（序列全 CLEAN），是綁定層交會：上游把 SealEngine 功能實作在 `ReignEngine.c` 的 `SEAL_EXPORTS` 並由 `HLL_LIBRARY(SealEngine, REIGN_EXPORTS, TAPIR_EXPORTS, SEAL_EXPORTS)`（上游 `ReignEngine.c:2754`）綁定；wip 把該行註解掉（`22e9496:src/hll/ReignEngine.c:2725-2728`，理由：v14 wrap/2-slot 簽名不同）改用獨立 `src/hll/SealEngine.c`（2,099 行、248 函數、178 處呼叫 `RE_/reign_/model_`）。
- 因此上游在 `ReignEngine.c` 新加的 SealEngine 綁定對 Dohna 無效；但上游在 `src/3d/*` 的底層改動（`inst->grayscale_rate` + shader uniform、`plugin->edge_reduction_rate` 進 outline 厚度、DrawShadow、UV tiling、lighting）會進到 wip 呼叫的 `RE_*` 基礎設施。wip `SealEngine.c:573-582`、`1289-1290` 目前只把 grayscale/edge_reduction 存進私有 `se_instance_ext`/`se_plugin_ext`，合併後應改寫入上游的 `RE_instance.grayscale_rate` / `RE_plugin.edge_reduction_rate` 才會渲染。Dohna 宣告確有 `Set/GetInstanceGrayscaleRate`（seal-hll.txt:40-41）。

### 6.7 e61f4b3（Math Asin/Acos）
- 結果樹 `Math.c:316-317` `HLL_EXPORT(Asin, asinf)/(Acos, acosf)` 與 `Math.c:346-347` `HLL_EXPORT(Asin, Math_Asin)/(Acos, Math_Acos)` 並存。wip 版回傳度（`rad2deg`），與 `Math_Sin/Cos/Tan` 收度一致；上游版回傳弧度。
- HLL 綁定對同名 export 取哪一個：未驗證（`src/vm.c` 的 link 邏輯未逐行讀）。解法：合併時刪上游兩行、保留 wip 的度制版本，或反向；不能兩者都留。

## 7. libsys4：8c93946 × upstream/master (20560d4)

- 共同祖先 `ed74c9e`；上游 16 個非 merge commit，wip 6 個。
- `git merge-tree --write-tree 8c93946 upstream/master` → 結果樹 `95c2691…`，**衝突只有 `src/instructions.c` 一處**（結果樹 432-470 行）：wip（7ee607b）逐條手寫 `instructions[X].ip_inc = 2 + n*4`；上游（67b740f）改用 `set_nr_args(op, n)` helper（同樣設 `ip_inc = 2 + nr_args*4`）並加 `instructions[DG_STR_TO_METHOD].nr_stack_args` 與 `DG_EXIST/DG_NEW/DG_STR_TO_METHOD` 的 OP 標記。兩者對 `ip_inc` 的結果相同；採上游即可。wip `tests/test_instructions.c` 只檢查 `ip_inc == 2 + nr_args*4`，上游 helper 滿足此式（未實跑）。
- `meson.build` 自動合併（wip 加兩個 test target；上游改 zlib backend 為 `libdeflate` optional fallback `zlib`、加 `src/zlib.c`）。`include/system4/instructions.h` 上游無變更（`nr_stack_args` 欄位在 base 已存在）。
- 上游 05a4d58 讓 `ain.c` 讀 VERS 時呼叫 `initialize_instructions()`；wip xsystem4 `vm.c:5302` 也呼叫。兩邊都呼叫為冪等，無害。
- 上游 16 commit 與 wip 檔案交集：只有 `instructions.c`、`meson.build`。其餘（flat writers 22009d4/76c1643/1ee0845/649c913/e3d994f、afa ID fallback 43472ac/d3aea05/cbc57ce、zlib 625b0b8、ajp a8df981、qnt a4867ff/b853346/4359331、ex 8ea7940）可直接進。
- 需注意語義：8ea7940 `ex_get()` 忽略路徑尾端 `.`（Dohna 用 MainEXFile，wip +1,044 行）——Dohna 的 ex 路徑是否含尾端 `.`：未驗證。625b0b8 讓 macOS 建置在有 `libdeflate` 時改用之，否則 zlib。
- xsystem4 側 pin 鏈：81f9113→`a8df981`（含 625b0b8）、77d9e77→`39eec1a`（PR #69 merge）、5bb36c7→`cbc57ce`、275d717→`20560d4`，全部是 upstream/master 祖先。

## 8. 自動合併但需人工確認的語義點

| # | 位置 | 風險 | 狀態 |
|---|---|---|---|
| 1 | `Math.c` Asin/Acos 重複 export、度/弧度不一致 | 執行期綁定不確定 | 未驗證（見 6.7） |
| 2 | `SealEngine.c` grayscale/edge_reduction 存值不進渲染 | 功能未接上 | 見 6.6 |
| 3 | `parts.c` `PE_RemoveController` 回傳 `delegate_index`（05820dc） | v14 EraseLayer 期望值 | 未驗證 |
| 4 | `parts.c` `root_params` 讓無 parent 也走 `parts_combine_params`（6ea8af9） | v14 parts global 計算 | 未驗證 |
| 5 | `save.c` `CURRENT_SAVE_VERSION` 3→7（a2524ba/62d38aa/d68baa7/461eca1） | wip 未動 save.c，自動升版；上游 `load_parts` 有 `version>2`、`version<7` 分支可讀舊格式；wip 新增欄位（`message`、`pixel_hittest`、`component_type`、`unique_id`、`user_component_name`）本來就未序列化 | 需實測存讀 |
| 6 | addc26c 依賴 fa09b00；167d75d 依賴 267f7bb | 分開進編不過 | 已證實（第 3 節） |
| 7 | 81f9113 依賴 libsys4 ≥625b0b8；275d717 依賴 ≥e3d994f | 分開進編不過 | 依 include/欄位名判斷 |
| 8 | `ChipmunkSpriteEngine.SP_GetFontWidth` 取整（4e20a49） | Dohna UI 寬度查詢值變化 | 未驗證 |
| 9 | `gfx_size_text` 回傳乘 `scale_x`（a19c200） | wip 呼叫點行為變化 | 未逐一檢查呼叫點 |

## 9. 建議分批 cherry-pick 順序

每批結束需可建置；標「解」者需手解衝突。

| 批 | 內容 | 衝突/前置 |
|---|---|---|
| 0 | **libsys4**：在 libsys4 fork 上 merge upstream/master（解 `instructions.c` 一處，採上游 `set_nr_args`，保留 wip tests），跑 `test_instructions`/`test_hashtable`，得新 pin | 1 處 |
| 1 | A 類 10 個：d17fc30、09a0e33、a2524ba、9bb871d、e699fb0、e76762d、bf3430d、b30a107、4233c7d、d8479ad | 無 |
| 2 | B 類（純 2D/共用）：6d05546、f0c4db7、33f1295、62d38aa、d800e12、e3bf653、5d032d7、05820dc(!)、6ea8af9(!)、c0cdf4f | 無；(!) 者需 v14 實測 |
| 3 | B 類 3D：b68e162、11ad7a5、e14b4bc、5cc163a、9654950、02f29b6(!)、0211877(!)、d68baa7；同批把 wip `SealEngine.c` 的 grayscale/edge setter 接到上游欄位 | 無；(!) 見 6.6 |
| 4 | B* 依 libsys4：81f9113、275d717（批 0 完成後） | 無 |
| 5 | C text：4e20a49 → 5cbc697 → **a19c200（解 2 處，保留 `ch[5]` 與 `extract_multibyte_char`）** → 5fb3c49；之後跑中文標題/對話畫面比對 | 2 處 |
| 6 | C audio：**d5b27a0（解 1 處，VOICE 分支搬進 `channel_open_archive_data`）**；測語音/BGM loop | 1 處 |
| 7 | C alpha clipper：**461eca1（解 `parts_internal.h` 1 處；`render.c` 視批 5 是否已進）** | 1–2 處 |
| 8 | C motion/PartsFunc：**fa09b00（解 motion.c 排版）** + addc26c + 267f7bb + 167d75d 同批 | 1 處 |
| 9 | C Math：e61f4b3，**刪上游兩行 export 保留 wip 度制**（或反向，擇一） | 語義 |
| 略 | D：68e8257、960c97e（文件，可直接帶）、135cae2、e93cdda、03f9e7e（無害可帶）；pin：77d9e77、5bb36c7 不 pick | — |

另一條路徑：直接 `git merge upstream/master`（一次解 4 檔 5 塊 + submodule），衝突總量與上表相同，但第 8 節的 9 個語義點不會在衝突中浮現，仍需逐一檢查。

## 10. 附錄：可重現指令

```
S=<scratchpad>/upstream-merge
cd $S/xsystem4
git merge-tree --write-tree --name-only 22e9496 upstream/master          # 4 檔 + submodule
git show bc8bfa224e0c3b925e06be39962e909807fa34b3:src/parts/text.c | sed -n 95,110p
git rev-list --count --no-merges e8bd5ab..upstream/master               # 48
git log --oneline 22e9496..sim | wc -l                                    # 42（序列模擬進入數）
cd $S/libsys4
git merge-tree --write-tree --name-only 8c93946 upstream/master          # src/instructions.c
```
