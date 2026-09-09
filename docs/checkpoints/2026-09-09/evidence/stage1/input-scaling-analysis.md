# WIP 滑鼠／Retina 座標有限靜態診斷

日期：2026-09-09。來源：WIP `484f4bc` 的未修改 `work/stage1/wip-source`，對照 Rufim `589cf2c` 的 `work/stage1/rufim-source`。僅讀程式與本機 SDL2 2.32.10 headers；沒有上網、改碼、操作 UI 或探查執行中的 process。

**結論：程式確實把 window 座標與 drawable-pixel viewport 混用；drawable/window 比例不為 1 時，座標轉換必定錯誤。Rufim 未修正此換算。可是本次點擊失敗是否由這個問題觸發，仍必須取得當次 SDL window、drawable、logical size 和 raw mouse 數值。** 兩版建立視窗都沒有 `SDL_WINDOW_ALLOW_HIGHDPI`，所以不能僅憑電腦是 Retina 就判定此次比例必為 2。

另有一個可直接確認的 WIP 缺陷：`mouse_set_pos` 設定的 game-coordinate override 沒有正常滑鼠移動解除機制。內建 auto click 使用這個 override，繞過真實滑鼠換算，因此「auto click 能進 RunGame」不能證明真實滑鼠命中座標正確。

## 1. 單位與資料來源

| 量 | 來源與單位 | WIP 行號 |
|---|---|---|
| `wx, wy` | `SDL_GetMouseState`，相對 focus window 的滑鼠座標，與 window client space 對齊 | `src/input.c:211–214` |
| `sdl.w, sdl.h` | 引擎 logical/game surface 尺寸，由 `gfx_set_window_logical_size(w,h)` 指派；不能假設永遠等於視窗尺寸 | `src/video.c:312–319` |
| `display_w, display_h` | `SDL_GL_GetDrawableSize`，OpenGL drawable **pixels** | `src/video.c:342–345` |
| `sdl.viewport` | 完全以 drawable 尺寸計算的實際 render rectangle，單位也是 drawable pixels | `src/video.c:347–369` |
| rendered viewport | 傳給 `glViewport`，此用途的 pixel 單位正確 | `src/video.c:448` |
| window size | 程式只在 `init_window_size` 讀取 SDL_GetWindowSize，以判斷是否縮小視窗；沒有拿來校正 input | `src/video.c:207` |

本機 SDL header 的 API 契約可驗證上述單位，不需 Web：

- `/opt/homebrew/include/SDL2/SDL_mouse.h:89–104`：GetMouseState 相對 focus window；`:164–174`：WarpMouseInWindow 接受 window 內座標。
- `/opt/homebrew/include/SDL2/SDL_video.h:1020–1044`：GetWindowSize 是 client area 的 screen coordinates，可與實際 pixels 不同。
- 同檔 `:2121–2142`：GL_GetDrawableSize 回傳 underlying drawable pixels，供 glViewport 使用，可與 GetWindowSize 不同。
- 同檔 `:482–486`：以 WindowSize 與 DrawableSize 的比值取得真正縮放比例；`:117–119`／`:2125–2128` 說明高 DPI flag／平台條件。

## 2. 所有 viewport 更新路徑

全 `src/`、`include/` 搜尋 `sdl.viewport`，只有 `gfx_update_screen_scale()` 寫入它；沒有另一個函式把 viewport 改回 points。

1. 啟動：`gfx_init()` → `gfx_set_window_logical_size(config.view_width, config.view_height)`（video.c:271）→ 設定 sdl.w/h → `gfx_update_screen_scale()`（:318）。
2. resize：`handle_window_event()` 收到 `SDL_WINDOWEVENT_SIZE_CHANGED`（input.c:618–620）→ `gfx_update_screen_scale()` → 標記 scene dirty。
3. 上述 public API 在主倉庫的唯一實際 logical-size 呼叫者是 gfx_init；其他搜尋結果是宣告／定義。

`gfx_update_screen_scale` 有三種情況，全部在 drawable-pixel space：

- 長寬比相同：viewport `(0,0,display_w,display_h)`。
- letterbox：寬=display_w，高按 game aspect 算，y 為上下置中偏移。
- pillarbox：高=display_h，寬按 game aspect 算，x 為左右置中偏移。

沒有 `SDL_WINDOWEVENT_DISPLAY_CHANGED` 的顯式處理。如果跨顯示器時 drawable 比例改變但未伴隨 SIZE_CHANGED，viewport 也可能過期；這是需觀察事件序列的風險，不能靜態確認本次已發生。啟動後 `init_window_size()` 若縮小視窗，也是依後續 size event 更新 viewport。

## 3. 可確認的換算缺口

WIP input.c:213–214 現行公式：

```text
game_x = (window_x - viewport_x) * logical_w / viewport_w
game_y = (window_y - viewport_y) * logical_h / viewport_h
```

減法兩側的單位不同。若 window client size=`Ww×Wh`，drawable=`Dw×Dh`，應先換到同一單位：

```text
drawable_x = window_x * Dw / Ww
drawable_y = window_y * Dh / Wh
game_x = (drawable_x - viewport_x) * logical_w / viewport_w
game_y = (drawable_y - viewport_top_y) * logical_h / viewport_h
```

這只是診斷公式，未改碼。OpenGL viewport.y 是從底部算；一般化時 `viewport_top_y = Dh - viewport_y - viewport_h`。目前 viewport 永遠置中，top/bottom margin 通常相同，整數除法可能只有一 pixel 差，因此不能把 32px 標題列差異歸因於 OpenGL y 軸方向。

反方向也缺比例：WIP `mouse_set_pos`（input.c:221–223）把 game 座標轉成 drawable pixels，直接傳給接受 window 座標的 `SDL_WarpMouseInWindow`。正確逆向還需乘 `Ww/Dw`、`Wh/Dh`。若有 letterbox/pillarbox，viewport offset 也應在同單位下換算，不能只把結果武斷乘 2。

具體例子（**示例，非本次 runtime 實測值**）：window 800×600、drawable 1600×1200、logical 800×600、無黑邊，真實 window(170,164) 應得到 game(170,164)，現碼卻得到 game(85,82)。若 drawable 同樣為800×600，則這個比例缺口在該次不會造成錯位。

同源缺口還出現在 touch 合成事件：input.c:289–290 的 `e->x/e->y` 直接和 drawable viewport 做 `SDL_PointInRect`，高 DPI／黑邊條件下也可能把 viewport 內外誤判。本次 macOS 普通滑鼠不必然走 SDL_TOUCH_MOUSEID 路徑，故這不是當次根因的證據。

## 4. auto click 為何不能排除滑鼠缺陷

WIP 的具體控制流：

1. `override_mouse_x/y` 初值為 -1（input.c:201）。
2. `mouse_set_pos(x,y)` 不論是否測試模式，都保存 game-coordinate override（:219–220）。遊戲 HLL 的 SetCursorPos 類函式也可呼叫它（例如 hll/InputDevice.c:58、SystemService.c:157、SACT2.c:768）。
3. `mouse_get_pos` 只要 override_x >= 0 就直接回傳它（:205–209），甚至不進 SDL_PumpEvents／GetMouseState／viewport 轉換。
4. 全倉庫沒有真實 `SDL_MOUSEMOTION` 解除 override 的分支。除非再呼叫 mouse_set_pos 換值或特殊負值，真實滑鼠移動不會更新 engine 看見的位置。
5. auto click sequence（:734）與 periodic auto click（:747）都呼叫 mouse_set_pos，並直接設 key_state，所以 game(85,82) 能命中，可能是完全靠 override。
6. `PE_UpdateInputState`（parts/input.c:176）又從 mouse_get_pos 取座標，故這個差异直接影響 parts hit test／點擊派發。

因此應將「全新 process、完全未執行 auto click／遊戲 warp」與「已執行過 mouse_set_pos」分開比較。不能在一次 process 中先 auto click 再拿手動滑鼠結果去推論 DPI；即使遊戲本身先呼叫 SetCursorPos，也可能啟動 override。

## 5. Rufim 對照

| 項目 | Rufim 現況 | 判斷 |
|---|---|---|
| 真實滑鼠 window→game | input.c:513–514 與 WIP 相同公式 | **未修 drawable/window 比例缺口** |
| game→warp | input.c:536–537 同樣直接用 drawable viewport | **未修比例缺口** |
| viewport 來源 | video.c:349–376，同樣只用 DrawableSize；resize hook=input.c:942 | 沒有用 WindowSize 校正 input |
| high-DPI window flag | video.c:259 與 WIP 一樣只有 OPENGL/SHOWN/RESIZABLE | 本次是否高 DPI 仍需 runtime 確認 |
| 測試座標 override | 只在 test_input_enabled + test_mouse_x>=0 時作用；input.c:993–1015 用真實 motion 解除，辨識自身 warp 到達事件 | **有改善 WIP override 卡住的相近問題**，但不是 Retina 修正 |

不能整份替換 input.c；Rufim 的 test-input、key-edge、wheel 等設計與 WIP 不同。若後續決定修，應各自建立 window↔drawable↔game 的共用轉換與可解除 override 行為，並保留既有 v14 click 語意。

## 6. 對此次實測的解讀與最小確認清單

Root 回報：「可見內容800×600，sky點新遊戲 x169,y162（含32px標題列）後變灰且仍 SceneTitle；engine auto click game(85,82) 能進 RunGame。」目前可確認這兩條輸入路徑不同，不能單凭其座標差異宣告確切比例。

尤其如果 sky 座標確實相對包含32px標題列的截圖，先扣除標題列會得到 content(169,130)，並不是(170,164)。將此值直接除2得到約(84,65)，也不是(85,82)。截圖像素、sky工具座標、client points 的原點與單位必須先對齊；標題列應由外部座標映射扣除一次，SDL_GetMouseState 已是 window-relative，不應再在引擎裡硬扣32。

只需在**同一次點擊、同一frame附近**收集下列值，就能判斷是否命中已知缺口：

1. `SDL_GetWindowSize` Ww/Wh 與 `SDL_GL_GetDrawableSize` Dw/Dh。
2. `sdl.w/h` 和完整 `sdl.viewport{x,y,w,h}`。
3. raw `SDL_GetMouseState` wx/wy、當次 button event x/y、mouse_get_pos 最後傳回的 game x/y。
4. `override_mouse_x/y`，確認是否實際略過真實座標換算。
5. screenshot pixels／工具點擊座標對 client origin 的映射，以及是否已有 mouse_set_pos 被呼叫。

判定方式：若 Dw/Ww、Dh/Wh≠1 且 override 未啟用，代入原公式與同單位公式即可證明此 frame 的位置錯誤；若比例為1，優先查 screenshot／工具坐標映射與 override，而非加固定Retina倍率。即使確認 input 錯位，也不能單憑它宣告「視窗變灰」的 present-chain 問題已解釋，這兩個症狀需分開保留證據。
