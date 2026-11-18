#!/bin/sh
# The original PhotoRestore (Geoff Daniell's pyrestore4.py, ported to
# Python 3 in tests/reference) on the faded scenes of tests/check.c, as a
# reference for restore:faded: Delta E 1976 of each result to the scene.
# Needs Python 3 with NumPy and Pillow on this machine (not in GIMP),
# and the build of tests/check.sh. The images are in
# tests/output/reference.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
build=${BUILD:-tests/output/build-check}
[ -x "$top/$build/check" ] || { echo "no $build/check: run tests/check.sh first" >&2; exit 2; }
ref="$out/reference"
rm -rf "$ref"
mkdir -p "$ref"
status=0
snapshot_begin reference
in_sdk "RESTORE_CHECK_OUT=tests/output/reference $build/check $build" >"$out/reference-check.log" 2>&1
# pyrestore4.py on every faded scene, the grid included, a few at a time
for f in "$ref"/faded-*.png "$ref"/grid*-faded.png "$ref/scene.png"; do
    n=$(basename "$f" .png)
    n=${n#faded-}
    [ "$n" = scene ] && n=unfaded
    case $n in grid*) n=${n%-faded} ;; esac
    echo "$f $n"
done | xargs -P "$(nproc)" -n 2 sh -c '
    python3 "$0" "$2" "$1/pyrestore4-$3.png" >"$1/pyrestore4-$3.log" 2>&1 ||
      echo "NOTE  pyrestore4.py stopped with an error on $3 (see its log)"' \
      "$here/reference/pyrestore4.py" "$ref" | sort | tee "$ref/fails.txt"
# it does on some of the grid (a negative power of zero: its fit gave a
# negative exponent, its "fail"); on the three named fadings it must not
grep -qvE 'on grid[0-9]+ ' "$ref/fails.txt" && status=1
python3 "$here/reference/compare.py" "$ref" || status=1
snapshot_end reference || status=1
exit $status
