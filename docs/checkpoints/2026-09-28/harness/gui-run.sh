#!/bin/bash
# usage: gui-run.sh <name> [seconds=150] [engine args...]
# Unattended GUI run of the optimized build: new game via --skip-title, Return held, auto-click every
# 1.2 s at the window centre, engine framebuffer PNG every 2 s. Stops on assert/ASan/VM error, stack
# overflow, the time limit or a file named STOP in the run directory. Saves and home are isolated.
# RUN_SAVE_SEED=<dir> starts from a copy of <dir> in the run's save folder; RUN_TRACE_SAVE=1 logs
# the SerializeStruct family (see ../tooling/run-gui-bounded.py).
source "$(dirname "$0")/env.sh" || exit 1
NAME=${1:?usage: gui-run.sh <name> [seconds] [engine args...]}; SECS=${2:-150}; shift; shift
RUN=$XS4_WORK/runs/$NAME
[ -e "$RUN" ] && { echo "已存在：$RUN"; exit 2; }
ninja -C "$XS4_OPTIMIZED_BUILD" >"$XS4_WORK/ninja-opt.log" 2>&1 || { tail -20 "$XS4_WORK/ninja-opt.log"; exit 1; }
[ $# -gt 0 ] || set -- --skip-title
RUN_HOLD_KEYS=13 RUN_AUTO_CLICK=1200 RUN_FRAMEBUFFER_SHOTS=1 \
	python3 "$XS4_SRC/docs/checkpoints/2026-09-28/tooling/run-gui-bounded.py" \
	"$XS4_OPTIMIZED_BUILD/src/xsystem4" "$XS4_SRC" "$XS4_GAME" "$RUN" "$SECS" "$@"
LOG=$RUN/engine.log
echo "dialogue lines (MSG): $(grep -ac '^MSG ' "$LOG")"
echo "stack overflows:      $(grep -ac 'call stack overflow' "$LOG")"
echo "framebuffer PNGs:     $(ls "$RUN/framebuffer" 2>/dev/null | wc -l | tr -d ' ')"
echo "save files:           $(ls "$RUN/saves" 2>/dev/null | tr '\n' ' ')"
echo "last error lines:"; grep -a -i -E 'assert|error' "$LOG" | tail -3 | cut -c1-200
