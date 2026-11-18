#!/bin/sh
# Real photos faded synthetically, restored by restore:faded, by Daniell's
# pyrestore4.py and by three baselines (tests/photos.py; GIMP's White
# Balance re-implemented, a per channel stretch, gray world): Delta E to
# the unfaded photo. Needs the photos (tests/images/fetch.py), Python 3
# with NumPy and Pillow, the gegl program, and the build of
# tests/check.sh (or $BUILD). Runs isolated (tests/common.sh), lists the
# user's folders before and after. The images are in tests/output/photos.
#
#   tests/photos.sh [--size N] [--per-photo] [--props "name=value ..."]
#   tests/photos.sh --slides [--props ...]   the faded DOCUMERICA slides
#                   instead (tests/slides.py): the cast left, and sheets
#                   to look at in tests/output/slides (or $SLIDES_OUT;
#                   e.g. SLIDES_OUT=tests/output/slides-map with
#                   --props show=colour-map for the plots)
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
build=${BUILD:-tests/output/build-check}
case $build in /*) ;; *) build=$top/$build ;; esac
[ -f "$build/faded.so" ] || { echo "no $build/faded.so: run tests/check.sh first" >&2; exit 2; }
mod="$out/photos-modules-$$"
rm -rf "$mod"
mkdir -p "$mod"
cp "$build/faded.so" "$mod/"
GEGL_PATH="$mod:$(pkg-config --variable=pluginsdir gegl-0.4)"
export GEGL_PATH
pass_env="GEGL_PATH"
status=0
snapshot_begin photos
if [ "$1" = --slides ]; then
    shift
    in_gimp python3 "$here/slides.py" "${SLIDES_OUT:-$out/slides}" "$@" || status=1
else
    in_gimp python3 "$here/photos.py" "$out/photos" "$@" || status=1
fi
snapshot_end photos || status=1
rm -rf "$mod"
exit $status
