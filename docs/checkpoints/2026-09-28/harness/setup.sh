#!/bin/bash
# usage: XS4_GAME=<工作副本> setup.sh
# Configures the optimized (GUI) and ASan (headless probe) builds under $XS4_WORK and links the probe.
# Safe to rerun; existing build directories are reused.
source "$(dirname "$0")/env.sh" || exit 1
set -e
export PKG_CONFIG_PATH="/opt/homebrew/opt/libffi/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
[ -f "$XS4_SRC/subprojects/libsys4/meson.build" ] || { echo "libsys4 submodule 未初始化：git -C \"$XS4_SRC\" submodule update --init"; exit 1; }
echo "src=$XS4_SRC ($(git -C "$XS4_SRC" rev-parse --short HEAD)) libsys4=$(git -C "$XS4_SRC/subprojects/libsys4" rev-parse --short HEAD)"
COMMON=(--native-file "$XS4_NATIVE_FILE" --wrap-mode nofallback -Ddebugger=disabled -Dopengles=disabled)
[ -f "$XS4_OPTIMIZED_BUILD/build.ninja" ] || meson setup "$XS4_OPTIMIZED_BUILD" "$XS4_SRC" "${COMMON[@]}" --buildtype debugoptimized
[ -f "$XS4_ASAN_BUILD/build.ninja" ] || meson setup "$XS4_ASAN_BUILD" "$XS4_SRC" "${COMMON[@]}" --buildtype debug -Db_sanitize=address,undefined
ninja -C "$XS4_OPTIMIZED_BUILD" >"$XS4_WORK/ninja-opt.log" 2>&1 || { tail -20 "$XS4_WORK/ninja-opt.log"; exit 1; }
xs4_build_probe
echo "setup ok: engine=$XS4_OPTIMIZED_BUILD/src/xsystem4 probe=$XS4_PROBE_DIR/runtime-probe-asan"
