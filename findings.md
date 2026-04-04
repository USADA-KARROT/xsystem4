# Current State

## Session Summary (2026-04-01)

### Key Commits Today
- `a3eabb4` Fix #238: mouse_set_pos override for headless auto-click
  - Root cause: SDL_WarpMouseInWindow doesn't update SDL_GetMouseState in unfocused windows
  - Effect: auto-click now correctly sets position; first click at (70,695) advances SceneAzito → RunTurnStart


- `173d8a2` Fix #236: stop double-converting CN .ex strings — BIG FIX
  - Root cause: `load_ex_file` applied sjis_to_gbk_string to CN .ex (already GBK), garbling all block names
  - Effect: EX_String lookups now work, GameCallback executes, PlayerCollection initializes
- `edeb160` Fix #237: implement Parts_SetPartsCGThread as synchronous CG load
  - Root cause: stub returned false, CgPartsQueue.State never reached 2, PartsAsyncLoadQueue blocked forever
  - Effect: game advances past azito into TV/ADV scene
- `7e3e004` Reduce assert warning verbosity
- `6ed8bff` Fix #235: SJIS→EUC-JP fallback + debug trace cleanup
- `6568796` Fix #234: SJIS→GBK for .ex loading

### What's working
- GB18030 text rendering, v14 MSG, GAME_MSG_Get
- Title screen: perfect match with CN reference
- SceneAzito renders with Chinese UI text
- EX lookups, GameCallback callbacks, PlayerCollection initialization
- Parts_SetPartsCGThread: synchronous CG load unblocks async queue
- Game reaches TV scene after Schedule click
- AdvMessageWindow, AdvCaption etc. activity files all load in TV scene

### Current state
- Game flow: skip-title → RunTurnStart (ADV text, rapid bg-clicks to advance) → RunHome/SceneAzito (WaitForClick needs PARTS click)
- ADV text system works: Chinese text renders, CASClick detects bg click via global[2]=g_EndPartsBusyLoop
- global[2] = parts::detail::g_EndPartsBusyLoop (confirmed from AIN globals dump)
- Background click exits RunTurnStart WaitForClick (text-view CASClick) but NOT SceneAzito WaitForClick (nested WaitForClick race)
- SceneAzito WaitForClick needs PARTS click on Schedule button
- ADV scene parts (in screen bounds): parts#900013 at (1200,558), parts#900015 NEXT at (1263,653)
- ✅ SceneAzito Schedule button at (70,695) — confirmed working by Fix #238
- ✅ APEG audio playback + black frame (Fix #241/#242): extract Ogg from SOND chunk → FFmpeg audio-only decode; PartsMovie functions wait for audio completion; TV shows black frame (pixel codec proprietary/unknown)
- ❌ APEG 影片黑畫面（GUI 驗證未完成）：movie::detail::Play → CreatePartsMovie → 音頻 OK，影像 codec 為 proprietary 無法解碼；GUI 下顯示黑畫面 + 正確音頻；headless 因 WaitForClick 無法到達此場景
- ✅ Fix #248: CASTimer struct 內嵌實例共用 timer_id=1 — 改用 abs(struct_page) % 2048 讓每個實例獨立計時，SceneLogo 的 KeyOrTimeWait 現在能正確超時
- ✅ Fix #252: X_ICAST 修復 — 介面 downcast 失敗時未回傳 -1，導致 Motion::PartsParamCollection 把 EasingParam 誤認為 TimeParam，span=0 動畫立即結束。修復後 SceneLogo 時序正確（~12s），ExecuterTask 每幀持續呼叫
- ❌ SceneLogo 動畫視覺效果不套用：
  - ✅ X_ICAST direct type match 已修復（sidx==target_type=615 正確匹配 EasingParam）
  - ✅ Array.Where predicate 正確返回 result=1（EasingParam 被辨識出來）
  - ✅ m_parts 和 m_current 都是有效的（非 -1，nr_vars=10）
  - ✅ Time=1000, Delay=0 正確（X_ICAST 修復 TimeParam 識別）
  - ❌ m_easingParam 陣列仍然是空的（easing_nr=0）
  - ✅ Array.Where 返回的 source 在 Select 內 X_A_SIZE=2（正確看到 2 個 EasingParam）
  - ✅ PushBack 在 Select 迴圈中被呼叫（ref 1-slot, arg3=0x2），元素數量在增長
  - ❌ 但 X_SET 收到的最終 array 仍然 nr_vars=0（**所有三種** param type 都是空的）
  - **推測根因：ArrayExtensions::Select 的 PushBack 修改的 array 和最終返回的 array 不是同一個**
    - PushBack 透過 AIN_REF_ARRAY 寫回 heap[slot].page，寫回機制正確
    - Select 返回 `.LOCALREF result; A_REF; RETURN`，A_REF 在 v14 直接 heap_ref + push slot（引用語義，不複製）
    - function_return 清理 local page 時會 variable_fini(result) → heap_unref
    - **可能原因：** PushBack 的 xrealloc 產生新 page 指標，寫回到 heap[slot].page 正確，
      但某個中間操作（如 DG_CALL 清理、SelectNext iteration 的 stack 操作）意外覆蓋了 heap[slot].page
    - **下一步：** 在 Select 內追蹤 result 的 heap slot 值和 page 指標在 PushBack 前後的變化
    - 注意：CN 版函數號與 JAST 版不同，用 strstr(name) 搜尋比硬編碼函數號更可靠
- ❌ 標題畫面 Logo 被切（底部超出 y=720）：pos=(193,625) origin_mode=5（中心點），Logo CG 高度 > 190px 時底部溢出
- ✅ Fix #246: 標題畫面按鈕點擊修復 — messageType 4→5（SWITCH case 5 = CallFunctionMouseClick），按鈕 delegate 現在正確觸發
- ✅ Fix #247: GetMessagePartsNumber/DelegateIndex/UniqueID 改為只 peek queue head，不用 msg_current — 修正 UniqueID 比對失敗導致按鈕點擊被丟棄
- ✅ Fix #249: 標題畫面按鈕點擊確認正常 — parts#900016 (NewGame) click 正確 exit WaitForClick，遊戲載入 ~45s 後進入 ADV 場景（RunTurnStart），parts#900019 背景點擊可推進文字
- PlayerCollection@Get assert fires (n === null) — player lookup failure, non-fatal

### Test command (working)
```bash
XSYS4_AUTO_CLICK_SEQ="16000,70,695;19000,640,360;22000,640,360;..." \
SDL_AUDIODRIVER=dummy ~/xsystem4-dev/xsystem4/builddir/src/xsystem4 \
  --skip-title "$HOME/Downloads/多娜多娜 一起幹壞事吧"
```

### APEG Format (reverse-engineered, not yet decoded)
- Header: "APEG"(4) + data_offset(4) + num_streams(4) + width(2) + height(2)
- Chunks: TMNL (thumbnail), SOND (Ogg audio), IPIC/PPIC/BPIC (I/P/B frames)
- Each chunk: tag(4) + body_size(4) + body[body_size], first 4 body bytes = timestamp_ms
- Op.apeg: 1280x720, 30fps, 102.6s, 3078 frames (208 I + 892 P + 1978 B)
- Pixel codec: proprietary, not standard MPEG DCT

### Session 2026-04-02 調查進度（未完成）

#### SceneLogo Motion 動畫問題調查

**症狀（headless 截圖確認）：**
- t=2s, 4s, 6s：全部顯示相同靜態畫面（黑底 + ALICESOFT logo 偏右 + 左側黃色縱條）
- t=8s：已進入 SceneTitle
- 正常應為 ~18s（KotW 2000+5000+5000 + Motion::Join 時間）
- 實際只跑 ~7s → 等同 KotW(2000)+KotW(5000) 剛好 7s

**視覺問題根因假設：**
- `Base` parts alpha=0（白背景不顯示）= Motion 初始狀態設定生效但沒有動畫更新
- `Light` parts 停在 X=143（左端）= 初始位置，動畫未執行
- `Alicesoft`/`Logo` parts 靜止可見 = ClipWidth/Scale 未實作或初始值不影響顯示

**時序問題根因假設：**
- Motion::Create → Motion::Executer@Start → CreateTask 返回 false → m_isFinish=true 立即
- Motion::Join("Logo") 完成後，m_motions 裡的 motions 全都是 isFinish=true
- IsEndWaitSection 立即返回 true → Motion::Join 在 1 frame (~16ms) 內完成
- KotW(2000) 正確等 2000ms，KotW(5000) 正確等 5000ms
- **但第三個 KotW(5000) 沒有執行** → 這意味著只有 2 個 KotW 被跑完

**尚未釐清的問題：**
- CreateTask 為什麼失敗：可能是 Motion::ParsedObjectCache 解析字串失敗，m_params 為空
- 第三個 KotW(5000) 是否真的沒執行，還是有其他提早退出的原因
- 視覺上，如果 motions 瞬間完成（end=true, progress=1.0），最終狀態應被套用（Base alpha=255），但實際顯示仍是黑底 → 說明 OnUpdate 從未被呼叫，或套用的 parts property 未影響渲染

**已確認的更新鏈（正確）：**
- WaitForClick → AFL_View_Update → view::detail::View_Update → SystemService.UpdateView（C）+ parts::detail::Update → CallBeginUpdateEvent → ExecuterTask@Update
- ExecuterTask 使用 RCASTimer（純 bytecode）累積時間
- RCASTimer 透過 AFL_Parts_AddEndUpdateEvent 在每 frame 累積 passedTime

**下一步（待執行）：**
1. 確認 Motion::ParsedObjectCache 是否正確解析 "Section:Logo[Alpha:0 255|Time:1000]"
2. 確認 CreateTask 路徑（m_params 是否空）
3. 若 CreateTask 失敗 → 找 ParsedObjectCache::Get 實作 (bytecode fno 27160)，看解析哪一步出錯
4. 另一條路：直接在 C 側確認 parts alpha/X 設定是否生效（IParts 的 SetAlpha 等 HLL 方法是否對應正確實作）

### Fix df2257f (2026-04-01)
- ✅ heap_get_page WARNINGs 27050/27049/27120/27121/27114/27115 修復
  - 根因：delegate_call 的 delegate_param_slots() 對 Motion::IArgument (is_interface struct) 計 1 slot，但呼叫方 push 了 2 slots，導致 lambda ARG 0 收到 dg_page 而非 interface value
  - 修法：加 delegate_arg_is_2slot()，對 AIN_IFACE/OPTION/IFACE_WRAP 或 is_interface struct 計 2 slots；delegate_call 迴圈用 vi 獨立追蹤 local var index
