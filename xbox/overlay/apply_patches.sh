#!/bin/bash
# Rebuilds C:/claude-work/odinuwp/core/src from the pristine upstream source (core/pristine/src, git archive of
# odin's HEAD: the blobs of the git index, so LF except the files that are CRLF in the index) and the patch series
# overlay/patches/*.patch, applied in name order with git apply (paths a/src/... b/src/..., -p1 from core/).
# Environment: OUT overrides the output folder (default core/src); LIMIT=NNNN applies only the patches whose number
# is up to and including NNNN (used to rebuild the snapshot core/state at a given patch).
set -e
ROOT=C:/claude-work/odinuwp
PRISTINE=$ROOT/core/pristine/src
OUT=${OUT:-$ROOT/core/src}
LIMIT=${LIMIT:-9999}
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$PRISTINE/." "$OUT/"
cd "$ROOT/core"
n=0
for p in "$ROOT"/overlay/patches/*.patch; do
	[ -f "$p" ] || continue
	base=$(basename "$p")
	num=$((10#${base%%-*}))
	if [ "$num" -gt "$((10#$LIMIT))" ]; then
		break
	fi
	n=$((n + 1))
	echo "apply $base"
	git -c core.autocrlf=false apply --whitespace=nowarn -p1 "$p"
done
echo "applied $n patches to $OUT"
