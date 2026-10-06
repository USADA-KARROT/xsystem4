# Source this file (bash); it only defines variables and functions. Every variable can be set beforehand to override the default.
#
#   XS4_GAME         遊戲「工作副本」目錄（必填，內含 dohnadohna.ain）。絕不可指向原始母片。
#   XS4_MASTER_GAME  原始母片目錄（選填）。有設定時，若 XS4_GAME 與它是同一個目錄就拒絕執行。
#   XS4_WORK         建置、探針與日誌的輸出目錄，必須在 repo 之外（預設 ~/xsystem4-work）。
#   XS4_SRC          要測的 xsystem4 checkout（預設為本檔所在的 repo）。
XS4_HARNESS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
: "${XS4_SRC:=$(git -C "$XS4_HARNESS" rev-parse --show-toplevel)}"
: "${XS4_WORK:=$HOME/xsystem4-work}"
: "${XS4_OPTIMIZED_BUILD:=$XS4_WORK/build-opt}"
: "${XS4_ASAN_BUILD:=$XS4_WORK/build-asan}"
: "${XS4_NATIVE_FILE:=$XS4_SRC/docs/checkpoints/2026-09-09/tooling/archive/native.ini}"
XS4_PROBE_DIR=$XS4_WORK/probe
XS4_AIN_SHA256_EXPECTED=beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947

xs4_die() { echo "harness: $*" >&2; }

[ -n "$XS4_GAME" ] || { xs4_die "XS4_GAME 未設定：請指向遊戲工作副本（不可指向原始母片）"; return 1; }
[ -f "$XS4_GAME/dohnadohna.ain" ] || { xs4_die "找不到 $XS4_GAME/dohnadohna.ain"; return 1; }
if [ -n "$XS4_MASTER_GAME" ] && [ -d "$XS4_MASTER_GAME" ]; then
	if [ "$(cd "$XS4_GAME" && pwd -P)" = "$(cd "$XS4_MASTER_GAME" && pwd -P)" ]; then
		xs4_die "拒絕：XS4_GAME 指向原始母片，請改用工作副本"; return 1
	fi
fi
mkdir -p "$XS4_WORK"
case "$(cd "$XS4_WORK" && pwd -P)/" in
"$(cd "$XS4_SRC" && pwd -P)"/*) xs4_die "XS4_WORK 不可在 repo 內：$XS4_WORK"; return 1 ;;
esac
XS4_AIN="$XS4_GAME/dohnadohna.ain"

# All headless probe modes and their expected exit codes (deleted-event: 23 leftover slots, pre-existing).
XS4_MODES="first-overload overload-shapes overload-shapes-str personality deleted-event heap-reuse
assignment observer reentrancy array-reinit metadata click timer bound-overload findlast order unique
fill-copy-extra fc-selector fc-fill0 fc-fill1-int fc-fill1-str fc-fill-clamp fc-copy0 fc-copy3
fc-copy3-str fc-copy3-dest1 fc-copy-overlap fc-realloc math sort string cif activity-text dialogue-model dialogue-copy
save-list save-roundtrip save-comment save-fixes gbk-string gbk-vm gbk-detect sjis-chars parts-reverse iface-arg
text-metrics delegate-args reverse-inherit base-ui frame-pacing base-ui-review title-review third-review mojibake logo-gloss clip-area input-nesting gauge working-cards alpha-inherit text-default layout-box construction free-box"
xs4_expected_rc() { case "$1" in deleted-event) echo 87 ;; *) echo 0 ;; esac; }

# Copy the probe sources from the repo into the work directory (build outputs stay out of the repo).
xs4_sync_probe() {
	mkdir -p "$XS4_PROBE_DIR"
	cp "$XS4_HARNESS"/probe/*.c "$XS4_HARNESS"/probe/*.inc "$XS4_HARNESS"/probe/*.py "$XS4_PROBE_DIR"/
	cp "$XS4_HARNESS"/deleted_event_fixture.inc "$XS4_WORK"/
}

xs4_timeout() {
	if command -v timeout >/dev/null; then timeout "$@"
	elif command -v gtimeout >/dev/null; then gtimeout "$@"
	else shift; "$@"; fi
}

# Rebuild the ASan tree and relink the probe against the current $XS4_SRC.
# build_probe.py alone does not recompile Array.c and friends, so ninja must run first.
xs4_build_probe() {
	ninja -C "$XS4_ASAN_BUILD" >"$XS4_WORK/ninja-asan.log" 2>&1 || { tail -20 "$XS4_WORK/ninja-asan.log"; return 1; }
	xs4_sync_probe
	(cd "$XS4_PROBE_DIR" && XS4_SRC="$XS4_SRC" XS4_ASAN_BUILD="$XS4_ASAN_BUILD" python3 build_probe.py asan \
		>"$XS4_WORK/build-probe.log" 2>&1) || { tail -20 "$XS4_WORK/build-probe.log"; return 1; }
}

# xs4_run_mode <mode> <outfile>: prints "rc san"
xs4_run_mode() {
	(cd "$XS4_PROBE_DIR" && export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 &&
		xs4_timeout 300 ./runtime-probe-asan "$XS4_AIN" "$1" >"$2" 2>&1)
	local rc=$?
	echo "$rc $(grep -ac 'AddressSanitizer\|UndefinedBehaviorSanitizer\|runtime error' "$2")"
}

export XS4_HARNESS XS4_SRC XS4_WORK XS4_OPTIMIZED_BUILD XS4_ASAN_BUILD XS4_NATIVE_FILE XS4_PROBE_DIR XS4_GAME XS4_AIN
