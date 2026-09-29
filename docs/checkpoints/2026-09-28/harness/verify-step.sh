#!/bin/bash
# usage: verify-step.sh <tag> [extra probe modes...]
# Rebuilds both trees from $XS4_SRC, relinks the probe, runs every mode in XS4_MODES plus the extra
# ones, and writes $XS4_WORK/logs/verify/<tag>/summary.txt. Exit code 0 only when every mode has its
# expected exit code and no sanitizer report.
source "$(dirname "$0")/env.sh" || exit 1
TAG=${1:?usage: verify-step.sh <tag> [extra modes...]}; shift
OUT=$XS4_WORK/logs/verify/$TAG; rm -rf "$OUT"; mkdir -p "$OUT"
S=$OUT/summary.txt
AIN_SHA=$(shasum -a 256 "$XS4_AIN" | cut -d' ' -f1)
{
	echo "tag=$TAG head=$(git -C "$XS4_SRC" rev-parse --short HEAD) diff_sha=$(git -C "$XS4_SRC" diff -- src include | shasum -a 256 | cut -c1-12) ain=$(echo $AIN_SHA | cut -c1-12) libsys4=$(git -C "$XS4_SRC/subprojects/libsys4" rev-parse --short HEAD) probe_gbk=${XS4_PROBE_GBK:-0}"
	[ "$AIN_SHA" = "$XS4_AIN_SHA256_EXPECTED" ] || echo "WARNING: AIN sha256 與預期不同（預期 ${XS4_AIN_SHA256_EXPECTED:0:12}）"
	ninja -C "$XS4_OPTIMIZED_BUILD" >"$OUT/ninja-opt.log" 2>&1; echo "ninja optimized rc=$? errors=$(grep -c 'error:' "$OUT/ninja-opt.log")"
	xs4_build_probe >"$OUT/build-probe.txt" 2>&1; echo "asan build + probe link rc=$?"
} >"$S"
FAIL=0
grep -q 'rc=[^0]' "$S" && FAIL=1
for m in $XS4_MODES "$@"; do
	read -r rc san < <(xs4_run_mode "$m" "$OUT/$m.txt")
	want=$(xs4_expected_rc "$m"); mark=ok
	if [ "$rc" != "$want" ] || [ "$san" != 0 ]; then mark=FAIL; FAIL=1; fi
	printf "%-20s rc=%-3s want=%-3s san=%-3s %-4s %s\n" "$m" "$rc" "$want" "$san" "$mark" \
		"$(grep -av 'Junk at start' "$OUT/$m.txt" | grep -av '^\s*$' | tail -1 | cut -c1-90)" >>"$S"
done
[ $FAIL = 0 ] && echo "VERDICT PASS" >>"$S" || echo "VERDICT FAIL" >>"$S"
cat "$S"
exit $FAIL
