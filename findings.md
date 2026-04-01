# Current State

## Session Summary (2026-04-01)

### Key Commits Today
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

### Where we stopped (WaitForClick blocker)
- TV scene loads AdvMessageWindow_main (NEXT button at parts#900015)
- NEXT button hitbox = (1263,653,44,44) — right edge of 1280px screen
- parts::detail::WaitForClick only exits when GetClickPartsNumber()>0
- Tried clicking (1285,675) but no progress — possible hitbox coordinate issue
- 9 clickable parts exist; none hit at tested positions

### Next steps to try
1. Click center of hitbox: (1263+22, 653+22) = (1285, 675) ← tried, failed
2. Try clicking exactly (1265, 655) — top-left of hitbox
3. Check if AdvMessageWindow is hidden (alpha=0 or show=false) during TV scene
4. Check if AdvMessageWindow NEXT is a different parts number during TV scene vs ADV scene

### Test command
```bash
XSYS4_AUTO_CLICK_SEQ="16000,70,695;19500,1285,675" \
SDL_AUDIODRIVER=dummy ~/xsystem4-dev/xsystem4/builddir/src/xsystem4 \
  --skip-title "$HOME/Downloads/多娜多娜 一起幹壞事吧"
```

### APEG Format (reverse-engineered, not yet decoded)
- Header: "APEG"(4) + data_offset(4) + num_streams(4) + width(2) + height(2)
- Chunks: TMNL (thumbnail), SOND (Ogg audio), IPIC/PPIC/BPIC (I/P/B frames)
- Each chunk: tag(4) + body_size(4) + body[body_size], first 4 body bytes = timestamp_ms
- Op.apeg: 1280x720, 30fps, 102.6s, 3078 frames (208 I + 892 P + 1978 B)
- Pixel codec: proprietary, not standard MPEG DCT
