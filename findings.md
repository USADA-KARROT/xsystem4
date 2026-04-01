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
- ❌ SceneLogo 動畫/警告標語：Fix #248 後待驗證是否完全正常
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
