# Current State

## Session Summary (2026-04-01)

### Key Commits
- `173d8a2` Fix #236: stop double-converting CN .ex file strings ← BIG FIX (EX lookups, GameCallback, PlayerCollection)
- `edeb160` Fix #237: implement Parts_SetPartsCGThread as synchronous CG load

### What's working
- GB18030 text rendering, v14 MSG display, GAME_MSG_Get
- Title screen: perfect match with CN reference
- SceneAzito renders fully with Chinese UI text
- **EX lookups work — GameCallback executes correctly**
- **PlayerCollection initializes (no more assert failures)**
- **Game advances past azito into TV scene after Schedule click!**
- **AdvMessageWindow, AdvCaption, AdvEpisode, etc. activities all load in TV scene**
- Parts_SetPartsCGThread: synchronous CG load (unblocks async load queue)
- PartsEngine click detection confirmed working

### Remaining
- ❌ APEG video decoder — TV shows white screen. Format fully reverse-engineered.
- ❌ TV scene progression — game loads TV scene + AdvMessageWindow but WaitForClick
  doesn't exit. NEXT button (at ~1175,695) is a clickable parts in AdvMessageWindow_main,
  but clicking it doesn't advance. WaitForClick needs GetClickPartsNumber()>0 to exit.
  AdvMessageWindow may not have rendered or registered its NEXT parts correctly.
- ❌ ADV scenes with character sprites — blocked by TV scene

### Test commands
```bash
# Skip title, auto-click Schedule:
XSYS4_AUTO_CLICK_SEQ="16000,70,695;19500,1175,695;21000,1175,695" \
SDL_AUDIODRIVER=dummy ~/xsystem4-dev/xsystem4/builddir/src/xsystem4 --skip-title \
"$HOME/Downloads/多娜多娜 一起幹壞事吧"
```

### Root cause of Fix #236
CN .ex files store strings in GBK (already translated). Previous code applied
sjis_to_gbk_string as the conv function when reading .ex, garbling all block
names. This caused EX_String("Callback_回合Start_Pre") to always miss →
GameCallback::Run never executed callbacks → game stuck at azito.

### APEG Format (reverse-engineered)
- Header: "APEG"(4) + data_offset(4) + num_streams(4) + width(2) + height(2)
- Chunks: TMNL (thumbnail), SOND (Ogg audio), IPIC/PPIC/BPIC (I/P/B frames)
- Each chunk: tag(4) + body_size(4) + body[body_size] (first 4 bytes = timestamp_ms)
- Op.apeg: 1280x720, 30fps, 102.6s, 3078 frames (208 I + 892 P + 1978 B)
- Pixel codec: proprietary (NOT standard MPEG DCT)
