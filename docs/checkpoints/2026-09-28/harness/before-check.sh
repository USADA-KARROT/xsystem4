#!/bin/bash
# usage: before-check.sh <rev> <mode> [mode...]
# Runs probe modes with src/ and include/ taken from <rev> (normally the commit before a fix), to prove
# the new fixture fails there. Restores HEAD afterwards, also when interrupted.
# Refuses to run when src/ or include/ has uncommitted changes: commit or stash first.
source "$(dirname "$0")/env.sh" || exit 1
REV=${1:?usage: before-check.sh <rev> <mode> [mode...]}; shift
[ $# -gt 0 ] || { echo "至少指定一個 mode"; exit 2; }
cd "$XS4_SRC" || exit 1
if [ -n "$(git status --porcelain -- src include)" ]; then echo "src/include 有未提交修改，拒絕執行"; exit 2; fi
restore() {
	git -C "$XS4_SRC" checkout -q HEAD -- src include
	xs4_build_probe >/dev/null 2>&1
	echo "restored src/include to $(git -C "$XS4_SRC" rev-parse --short HEAD), dirty=$(git -C "$XS4_SRC" status --porcelain -- src include | wc -l | tr -d ' ')"
}
trap restore EXIT
git checkout -q "$REV" -- src include
echo "src/include from $(git rev-parse --short "$REV")"
xs4_build_probe || { echo "build at $REV failed"; exit 3; }
mkdir -p "$XS4_WORK/logs/before"
for m in "$@"; do
	out=$XS4_WORK/logs/before/$(git rev-parse --short "$REV")-$m.txt
	read -r rc san < <(xs4_run_mode "$m" "$out")
	printf "%-20s rc=%-3s san=%-3s %s\n" "$m" "$rc" "$san" "$(grep -av '^\s*$' "$out" | tail -1 | cut -c1-100)"
done
