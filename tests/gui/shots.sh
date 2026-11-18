#!/bin/sh
# The filter's dialog in GIMP on a Broadway display, driven with a
# headless Chrome, for the screenshots in docs/: the Colors menu, the
# dialog of Restore Faded Colors on a faded DOCUMERICA slide (public
# domain, tests/images), opened by its name from Search Actions (/), the
# dialog with Show set to Colour map (the plot on the canvas), and
# the filter applied with OK, the image saved, and the XCF checked in
# headless GIMP: the layer keeps the filter, live, with its settings.
# Run tests/gimp-check.sh first (it puts the modules in place). Lists
# the user's folders before and after and fails if anything changed.
# GIMP runs with --new-instance and a throwaway profile (start.sh).
#
#   tests/gui/shots.sh                          all
#   RESTORE_SHOTS="dialog" tests/gui/shots.sh   some (colors dialog live)
#
# The positions were measured on screenshots (2026-11-18, GIMP 3.2.8,
# the interface font DejaVu Sans as gimp-devtools pins it), except SHOW
# (see there).
#
# Prints PASS or FAIL per step and exits non-zero if one failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
docs=$top/docs
images=$top/tests/images
mkdir -p "$docs"
status=0
pass () { echo "PASS  gui_$1"; }
fail () { echo "FAIL  gui_$1"; status=1; }

# the dialog: the canvas made last that is neither the whole page nor
# small; Broadway draws each window as a canvas with its shadow (26
# pixels here), the title bar's middle 36 pixels below the canvas's top
dialog_rect () {
    $cdp "$view" "eval:(() => { const c = [...document.querySelectorAll('canvas')]
      .map(e => e.getBoundingClientRect())
      .filter(r => r.width < 1200 && r.height > 150 && r.height < 780).pop();
      return c ? [c.left, c.top, c.width, c.height].map(Math.round).join(',') : '' })()" \
      2>/dev/null | tail -1
}

# moves the dialog to the top right corner of the page; sets vx, vy (its
# visible top left corner) and vw (its visible width)
move_dialog () {
    r=$(dialog_rect)
    [ -n "$r" ] || return 1
    l=${r%%,*}; r=${r#*,}; t=${r%%,*}; r=${r#*,}; w=${r%%,*}
    vw=$((w - 52))
    vx=$((1278 - vw))
    vy=120
    gx=$((l + w / 2)); gy=$((t + 36))
    $cdp "$view" down:$gx,$gy move:$(( (gx + vx + vw / 2) / 2 )),$(( (gy + vy + 10) / 2 )) \
      move:$((vx + vw / 2)),$((vy + 10)) up:$((vx + vw / 2 + 1)),$((vy + 10)) wait:1200 >/dev/null
}

COLORS=321,49
AUTO=338,400
# on Broadway the search field of Search Actions needs a click before it
# takes the keys
SEARCH_FIELD=360,176
# the Show combo box, the dialog's last row since 2026-11-18 (after
# Amount, which was at 1070,364 with the dialog moved to the top right):
# one row (28 pixels) below Amount, confirmed on the screenshot of the
# run of 2026-11-18
SHOW=1070,392

# search_filter <name>: opens Restore Faded Colors from Search Actions
search_filter () {
    $cdp "$view" key:/ wait:1500 click:$SEARCH_FIELD wait:500 text:"Restore Faded Colors" \
      wait:1500 shot:"$gui_out/$1-search.png" key:Enter wait:6000 >/dev/null
    move_dialog || { fail "$1: no dialog"; return 1; }
    pass "$1: the dialog opened"
}

session () {
    name=$1
    RESTORE_GUI_NAME=$name
    RESTORE_GUI_PHOTO=$images/slide-leakey-546097.jpg
    [ -f "$RESTORE_GUI_PHOTO" ] || { fail "$name: no $RESTORE_GUI_PHOTO (tests/images/fetch.py)"; return; }
    export RESTORE_GUI_NAME RESTORE_GUI_PHOTO
    tries=0
    until start_gimp "$name"; do
        stop_all
        tries=$((tries + 1))
        [ $tries -lt 3 ] || { fail "$name: GIMP did not start (log: $gui_out/gimp-$name.log)"; return; }
    done
    case $name in
    colors)
        # the Colors menu with Auto's submenu open (hovered), where
        # Restore Faded Colors... is
        $cdp "$view" click:$COLORS wait:1500 move:$AUTO wait:1500 \
          shot:"$docs/colors-menu.png" key:Escape key:Escape >/dev/null &&
          pass "colors: Colors > Auto";;
    dialog)
        search_filter dialog || { stop_all; return; }
        $cdp "$view" wait:6000 shot:"$docs/restore-faded-dialog.png" >/dev/null &&
          pass "dialog: the dialog on the slide, default settings";;
    map)
        # Show: Colour map, chosen from the combo box with the keys (the
        # item after Result)
        search_filter map || { stop_all; return; }
        $cdp "$view" wait:4000 shot:"$gui_out/map-before.png" click:$SHOW wait:1000 \
          key:Down key:Enter wait:8000 shot:"$docs/restore-faded-map.png" >/dev/null &&
          pass "map: the dialog with Show set to Colour map (look at it)";;
    live)
        search_filter live || { stop_all; return; }
        # OK: Enter in the dialog
        $cdp "$view" wait:4000 key:Enter wait:4000 shot:"$gui_out/live.png" >/dev/null
        touch "$gui_out/save"
        if wait_for "$gui_out/saved" 60; then
            pass "live: the image saved with the filter"
        else
            fail "live: not saved (log: $gui_out/gimp-live.log)"
        fi;;
    esac
    stop_all
}

snapshot_begin gui
rm -f "$gui_out/gui-live.xcf"
for s in ${RESTORE_SHOTS:-colors dialog live map}; do
    session "$s"
done

# the filter in the saved XCF, in headless GIMP
xcf=$gui_out/gui-live.xcf
if [ -f "$xcf" ]; then
    GIMP3_DIRECTORY="$out/gimp-profile"
    GEGL_PATH="$out/gimp-check-modules:$(pkg-config --variable=pluginsdir gegl-0.4)"
    export GIMP3_DIRECTORY GEGL_PATH
    pass_env="GIMP3_DIRECTORY GEGL_PATH"
    run_prefix="--timeout=600" in_gimp gimp-console-3.2 --no-interface --no-data --no-fonts \
      --batch-interpreter python-fu-eval -b "
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio
img = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path('$xcf'))
for l in img.get_layers():
    for f in l.get_filters():
        c = f.get_config()
        print('FILTER', f.get_operation_name(), round(c.get_property('light-dark'), 2), c.get_property('show'))
" --quit >"$gui_out/xcf-check.log" 2>&1
    if grep -qE "^FILTER restore:faded 1.5 result$" "$gui_out/xcf-check.log"; then
        pass "live: after loading the XCF the layer has the live filter restore:faded, Light-Dark 1.5, Show result"
    else
        fail "live: the XCF's layer: $(grep FILTER "$gui_out/xcf-check.log")"
    fi
fi

snapshot_end gui || status=1
[ $status = 0 ] && echo "gui: all passed" || echo "gui: FAILED (screenshots in docs, logs in $gui_out)"
exit $status
