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
for f in "$ref"/faded-*.png "$ref/scene.png"; do
    n=$(basename "$f" .png)
    n=${n#faded-}
    [ "$n" = scene ] && n=unfaded
    python3 "$here/reference/pyrestore4.py" "$f" "$ref/pyrestore4-$n.png" \
      >"$ref/pyrestore4-$n.log" 2>&1 || { echo "FAIL  pyrestore4.py on $n"; status=1; }
done
python3 "$here/reference/compare.py" "$ref" || status=1
snapshot_end reference || status=1
exit $status
