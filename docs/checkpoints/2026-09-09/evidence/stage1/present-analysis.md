# Stage 1：灰色視窗的靜態追蹤與最小診斷

查證：2026-09-09。本文件只讀程式與既有 log；沒有修改原 repo、建置、執行遊戲、操作 UI 或宣稱修復。基準為 WIP `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b`，對照 Rufim `589cf2c7599761e30fc7b9a48ef6d8e106ef76df`。

WIP 原始碼根目錄：`<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/worktrees/xsystem4-cn-on-upstream`。下列相對路徑／行號皆以此版本為準；Rufim 從本任務 `work/repo` 用 `git show 589cf2c:<path>` 读取。

## 結論

1. **已確認整條 present 程式路徑存在，沒有找到「根本少呼叫 SDL_GL_SwapWindow」的靜態缺口。** WIP 在 `PartsEngine.UpdateComponent`、`SystemService.UpdateView`、SACT update 等路徑都會呼叫 `gfx_swap`。但是否在灰窗那次執行中進入、跑完，必須用目前同一次執行的計數與時間戳查證。
2. **內部截圖與真正被顯示的來源不一定相同。** `video.c:441` 截 `main_surface`；`video.c:459` 實際 blit 的是 `view->handle`。`effect.c:332` 可以把 `view` 切到效果 texture，直到 `effect_fini`（:358）才重設。第一個 GL 診斷必須同時記錄 main/view，再看 default framebuffer 的內容。
3. **Rufim 並無現成的 macOS SwapWindow 修正可移植。** 兩線的核心 `bind default FBO → viewport → clear → shader blit → SDL_GL_SwapWindow → bind main FBO` 相同；Rufim 只額外加慢幀／heap audit 等觀測。Rufim 的上層 frame driver 不同，不能把整份 UpdateComponent／SystemService 直接代換。
4. **最小有價值成果是同一次 run 的四段證據：** main texture → selected view → default backbuffer（swap 前）→ 桌面可見視窗，並確認 swap 真的返回。沒有這四段之前不宜把問題定性為 Cocoa／vsync／shader。

## WIP 程式路徑

### 主執行緒與 context

- `src/system4.c:465` 是 C main；`:674` 同步呼叫 `vm_execute_ain`。
- `src/vm.c:4894–4897` 在 `vm_execute_ain` 記錄 `pthread_self()` 作 main_thread_id；沒有建立 VM worker thread。
- `src/hll/SACT2.c:100–112` 的 sact init 由遊戲 HLL 路徑同步進 `gfx_init`。
- `src/video.c:224–277`：SDL_Init(VIDEO|AUDIO)、要求 desktop GL 3.1 core、建立 SHOWN/RESIZABLE/OPENGL window、SDL_GL_CreateContext、glew、設定 swap interval（預設 0）、shader 與 main texture 初始化。
- 全 src 搜尋只找到 `src/debugger_dap.c:1039` 有 SDL_CreateThread（Debugger reader）；沒有發現顯式 `SDL_GL_MakeCurrent`、pthread_create 或 VM/renderer 轉移到另一 thread 的程式。因此「GL 從非主執行緒呼叫」目前是**低順位假說**，用 `pthread_main_np()`／SDL_ThreadID／current context 一次就能判定。
- `gfx_init` 的 GL 3.1 request 是兩線共用；若 context creation 真失敗，現碼會 ERROR，與大量成功 framebuffer 圖不一致。先印實際 driver/version/profile，再決定是否需調整 macOS context request，勿先盲改 3.2/4.1。

### 各 frame driver

- `src/hll/PartsEngine.c:945–968` `PE_v14_UpdateComponent`：
  - 重入時只 handle_events＋PE_UpdateComponent，**不 render/present**。
  - 最外層 handle_events、sprite plugins、motion、PE_Update 後，每 ≥16ms scene_render＋gfx_swap。
  - 若 PE_Update 中 callback 進 VM 永不返回，該最外層就永遠到不了 :963–964。故要印「進入／PE_Update 返回／swap返回」計數，不能只看 HLL 有被呼叫。
- `src/hll/SystemService.c:108–142` UpdateView：自己計時間、事件、plugins、motion、PE_Update、每 ≥16ms scene_render＋gfx_swap，未到時 SDL_Delay(1)。它與 UpdateComponent 是兩套 throttle，static 未證明實際哪條在灰窗時工作。
- `src/hll/SACT2.c:178–187` sact_Update：只有 scene_is_dirty 才 scene_render＋gfx_swap。
- `src/scene.c:76–91` scene_render 直接 gfx_clear＋逐一 sprite render；本函式不檢查 dirty，也不顯式綁 main FBO，依賴呼叫鏈進來時 FBO 正確。
- `src/vm.c:4740–4746` 每 256K instructions 只 pump events，**有意不 gfx_swap**（註解記舊 mid-scene swap 曾 crash）。不要為了灰窗直接把 swap 加回 VM 內部，此舉會在任意 GL/scene 中間狀態呈現。

### 事件鏈

- `src/input.c:645–653` handle_events 限制每毫秒最多一次；後面 SDL_PollEvent 正常處理 Cocoa/SDL 事件。
- `src/input.c:595–620` EXPOSED 與 SIZE_CHANGED 只標 dirty／更新 scale，沒有立即 swap。註解記舊 macOS 在 global init 的 expose 直接 swap 可能卡 Cocoa vsync。
- `src/input.c:625–642` handle_window_events 用 SDL_PumpEvents＋只取 QUIT/WINDOWEVENT，避免消耗滑鼠鍵盤事件。
- `src/input.c:778–780` APP_DIDENTERFOREGROUND 仍直接 gfx_swap，理論上可在 event pump 中發生，但 static 無本次觸發證據。
- `src/video.c:342–370` 用 SDL_GL_GetDrawableSize 計算 viewport；window size 與 drawable size 分開。應記錄實際值再判 Retina／resize；程式目前沒有每 frame 更新 viewport。

### FBO 到可見視窗

- `src/video.c:293–310` main texture = GL_RGB，掛 main_surface_fb 的 COLOR_ATTACHMENT0。
- `src/video.c:424–445` 可選自動 PNG 在 present 前抓 main texture。截圖只證明到此；不能證明後面的 shader blit／swap 完成。
- `:447` glBindFramebuffer(GL_FRAMEBUFFER,0) 同時切 read/draw default；`:448` 使用 sdl.viewport；`:449` clear；`:456–464` 用 default_shader 取 view->handle 繪全視窗 quad；`:466` SDL_GL_SwapWindow；`:467–468` 重綁 main_surface_fb＋logical viewport。
- `:486–529` shader program、uniform、texture unit 0、VAO/VBO 都在 render job 時设置；**不重設所有全域 GL state**（blend/depth/cull/scissor/color mask）。多數 draw 函式會 restore blend（`src/draw.c:209–213`），所以 state leak 屬待量測假說，不是已找到 bug。
- `:705–722` gfx_set/reset_framebuffer 不是堆疊式還原：reset 固定回 main FBO。`gfx_save_texture`（:751）透過 gfx_get_pixels（:742）建立 READ FBO，最後也回 main。它**不能直接作 default backbuffer 擷取工具**。
- effect 有另一個顯示 texture：`src/effect.c:326–332` 設 view；`:336–351` 更新效果及 swap；`:355–360` reset view。若 main 有圖而 effect.view 沒更新或未退出，可造成截圖與視窗不同。

## 與 Rufim 的限定對照

| 項目 | WIP | Rufim 589cf2c | 對此次診斷的用途 |
|---|---|---|---|
| gfx_swap | video.c:447–468 | video.c:433–454 | 核心內容一致；不是可以直接拿來修灰窗的差異 |
| capture | swap前 main texture PNG | 無該段；swap後多 slow frame／heap audit | WIP 的 capture 不代表 window；Rufim 工具證明 swap後診斷容易分離 |
| UpdateComponent | 有 events/plugins/motion/render/swap＋重入 guard | PartsEngine.c:813–819 只 set message rates、caret、PE_Update | 不應直接替換，替換反而可能移除 WIP 所需 frame driver |
| SystemService.UpdateView | 完整 pipeline | SystemService.c 約:332 為 quiet unimplemented | Rufim 用不同 caller；不能將此視為 WIP 自訂 pipeline 錯誤的證明 |
| SACT update | dirty才render+swap | SACT2.c:179–187 同樣 | 可設 caller tag 判斷是否正在走這條 |
| expose/resize | 延後到 regular frame | input.c:923–944 直接swap | 不宜照抄：WIP 留有 macOS global-init hang 的歷史原因 |
| renderer thread | 僅 debugger worker | 同樣僅 debugger worker | 非主執行緒假說兩線皆無靜態支持 |

## 建議最小診斷（一次建置、先觀測，尚未實作）

以單一 opt-in env（例如 `XSYS4_PRESENT_TRACE=1`）限制至前 3 幀＋之後每秒 1 筆；不要每 frame 列數千行，不改遊戲 timing／render 語義。若將來要 A/B 試單一 GL state 修正，另開 flag，不和第一輪觀測混在一起。

| 埋點 | 要印的最小狀態 | 可回答的問題 |
|---|---|---|
| video.c gfx_init :257之後、初始化完畢 :276 | pthread_main_np（Apple guard）／SDL_ThreadID，expected SDL window/context、SDL_GL_GetCurrentWindow/Context、SDL_GetCurrentVideoDriver、GL_VENDOR/RENDERER/VERSION/SHADING_LANGUAGE_VERSION；requested及actual profile；SDL window ID/flags；window、drawable、logical、viewport尺寸；swap interval getter與set結果 | 建立了哪個真正視窗/context；是否 drawable=0；是否主執行緒 |
| PartsEngine.c:954、:958後、:964後 | 每秒 entered/after_update/presented/reentrant counters；passed_time及caller tag；無需dump全VM | 是卡在遊戲update內，或真的有 regular present |
| SystemService.c:124、:130後、:137後；SACT2:178 | 同樣 caller tag/counters；dirty狀態 | 實際由哪條driver工作，是否都被throttle／dirty阻擋 |
| video.c:424入口 | frame seq、timestamp、current thread/context/window、main/view指標與handle/尺寸、view==&main_surface；GL_DRAW/READ_FRAMEBUFFER_BINDING；實際viewport和drawable | 最有用的同時性證據；main正常但選到別張view |
| video.c:449後及:464後 | 各階段 glGetError（先記錄既有error以免混淆）、default FBO status、GL_DRAW_BUFFER/READ_BUFFER、GL_DOUBLEBUFFER、GL_COLOR_WRITEMASK、depth/cull/scissor/blend開關，scissor box、blend factors/equations、default shader link status及glIsTexture(view) | clear/blit何處失敗；是否寫錯buffer、整張被裁／拒絕／混掉 |
| video.c:466前後 | monotonic start/end，duration，swap_enter/swap_exit計數、last completed frame ID | swap沒被叫、被叫但卡住、還是正常返回 |
| input.c:595 window events | event type/windowID/time、shown/minimized/hidden/focus旗標、size/drawable/viewport更新後值 | 桌面截的是哪個視窗，是否resize/minimize/occlusion條件 |

**default backbuffer 擷取**：在 :464 完成後、:466 前，直接 glReadPixels，讀取目標 default framebuffer。先查 GL_DOUBLEBUFFER，雙buffer用 GL_BACK；單buffer才GL_FRONT。保存原 read FBO／read buffer／pixel pack state，必要時設定 PACK_ALIGNMENT=1／PACK_ROW_LENGTH=0，確保沒有pixel pack buffer綁定，完成後精確還原；不要呼叫 gfx_save_texture，因它會改 READ FBO到main。只取 1–3 張，或先印固定網格的 RGB／非單色統計再決定是否存整張。glReadPixels會同步GPU，故只在diagnostic frame做，不拿此輪數值作效能結論。

至少對同一 frame ID 保存：internal main、selected view（若不同）、pre-swap default backbuffer，並將桌面截圖時間與 window ID對齊。不要從畫面「恰好看起來灰」推測FBO內也灰。

## 根因調查順位（不是已確認概率）

1. **先排除測量／時間不一致，然後看frame是否真正送出。** 既有最後 run `logs/runtime/run-20260709-115511.log` 只有binary git=unknown、VM heartbeat／SceneLogo等，沒有swap enter/exit或同時桌面證據。若 entered>0、after_update不增，查 PE_Update→callback／VM busy loop；若 swap_enter增但exit停，才查阻塞中的SwapWindow。這能用最少改動大幅縮小範圍。
2. **main texture → selected view／default backbuffer 的差異。** 這是目前最具體的靜態不等價點。若view!=main且view/backbuffer灰，追effect lifecycle；若view正常但backbuffer灰，查viewport、shader／VAO、blend/depth/cull/scissor、draw buffer與GL errors。
3. **Cocoa drawable／context／視窗呈現。** 僅在backbuffer有圖、swap返回、桌面同一視窗仍灰時升為最高順位。打印current context與window一致性、main thread、window flags/size/resize；必要時用獨立、同工具鏈的最小 SDL GL 彩色視窗控制實驗，以區別遊戲引擎與SDL/桌面環境。該控制實驗也應先由root決定並執行，這裡未啟動。
4. **全域GL state漏出／texture過期／shader資料。** 若第2步已排除則順位下降。能正常讀出main使整個GL初始化失敗不合理，但不能排除default FBO專有狀態；避免先全面reset state，會掩蓋誰造成錯誤。
5. **非主執行緒、盲目調vsync／context version。** 靜態沒有thread轉移證據、預設vsync=0且兩線相同，先用一次log排除，勿以猜測重寫thread架構或強制不停swap。

## 第一輪結果的分流

- main PNG新增，但`swap_exit`停止：可以精確限定最後成功步驟，優先看enter/exit timing與當下stack。
- main/view內容正常、default backbuffer空：可见視窗問題應先當作GL blit問題，而不是OS。
- main有圖、view是不同texture且灰：先追effect，不必改SDL。
- backbuffer正常、swap穩定返回、桌面同視窗灰：優先SDL/Cocoa/drawable/visibility，再做最小控制樣本。
- 三者及桌面皆有圖：灰窗在當前build未重現，記錄精確SHA/工具鏈/啟動條件與證據，不宣稱修復；轉下一個已重現blocker。

本次沒有找到可以有把握直接提交的灰窗fix；最小下一步是上述定位埋點，不是合併Rufim或恢復任意VM-loop swap。
