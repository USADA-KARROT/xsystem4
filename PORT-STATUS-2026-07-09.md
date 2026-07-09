# Dohna Dohna CN port — status at project pause (2026-07-09)

Branch layout:
- `cn-on-upstream` @ 02f061c — safe checkpoint (title screen, NewGame click,
  opening CG, TV scene; verified via internal screenshots).
- `wip/post-checkpoint-2026-07-06` @ d3cd045 — all later work, NOT merged.
- libsys4 `cn-on-upstream` @ 8c93946.

## Fixes on the wip branch (all committed, each message has full rationale)

1. 6e66a1e  system: ResumeSave writes for real (fork's 2s throttle faked success)
2. 1dae88b  UNIMPL HLL batch port (audio/voice path, Kiwi v14 API, CGManager
   string API, PartsEngine construction/panel/movie + 594 prelink stubs)
3. 69000c6  parts: v14 Parent::get contract (pending parent, 0-for-none) —
   fixed the SceneLogo infinite Attach loop
4. cbf5774  vm: zero value-typed globals left at -1 by the v14 alloc pass —
   fixed skip-mode self-enable (RestoreState read -1 as true)
5. 2b8597e  vm: dispatch _MSG through the named "message" handler (msgf=0 in
   the CN AIN) — dialogue text now reaches CMessageTextModel;
   adds XSYS4_TRACE_FNO single-function opcode trace
6. d6af8eb  Array: 2-slot stride only when the element type says so —
   struct_type doubles as struct id; Numof/At/Erase corrupted every
   struct-element list (message list read as empty)
7. d3cd045  vm: reset CASTimer slot epoch on construction (slot-collision
   inherited stale epochs; made waits fire instantly, heap-layout dependent)

## Open problems (in dependency order)

1. **Window presents nothing (grey)** — discovered last, most fundamental:
   the game renders into the framebuffer (internal XSYS4_SCREENSHOT_DIR
   captures are correct) but the visible SDL window stays grey. All prior
   "screen works" claims were based on internal captures only, never
   eyeballed. Fix the present chain (gfx_swap → SDL_GL_SwapWindow) first,
   and from then on verify with desktop `screencapture` + internal capture
   side by side.
2. **Dialogue self-advances** — every page's key-wait is ended by the
   Motion::ExecuterCollection@Join completion lambda (fno 36081) firing
   AFL_Parts_EndWaitForClick via the per-frame Observer (verified by a
   caller dump). The per-line motion completes instantly: suspects are
   (a) Join called with an empty motion list (motion creation fails),
   (b) motion time advance (RCASTimer@AddTime dt source),
   (c) IsEndWaitSection logic. Ruled out: clicks, key states, wheel,
   g_EndPartsBusyLoop residue, CASClick latches, skip/auto globals.
3. **Message window text not visible** — the text chain is verified up to
   the display composer (MSG → message() → model → CreateDrawChar reads the
   element and appends msg[4]); whether PE_SetText's parts texture reaches
   the screen is untestable until (1) is fixed.
4. Title screen: right menu column + artwork logo not rendered.
5. Wave 4 (double free of VM_PAGE seen twice), Wave 6 (save/load/backlog
   verification), gameplay loop beyond the TV scene (never reached by the
   old fork either), Wave 7.

## Local (non-repo) references on the dev machine

`~/xsystem4-dev/dohnadohna-mac-port/`: STATUS.md, reports/handoff-2026-07-06.md
(§8 = post-checkpoint), logs/tests/gui-run-log.md (fb1–fb53: every hypothesis,
run and verdict), reports/visual-parity/wine-baseline-index.md + 141 frames
(authoritative behaviour reference from the original engine under
wine-crossover: dialogue always waits for clicks; the white TV screen is
genuine), patches/, screenshots/. The machine may be wiped; this file plus
the commit messages are the durable hand-off.
