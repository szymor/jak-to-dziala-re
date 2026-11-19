#!/usr/bin/env bash
# Drive the original TWTW.EXE headlessly: Wine inside an Xvfb display, xdotool for input.
#
#   tools/orig.sh start            start Xvfb + the app (waits for the main window)
#   tools/orig.sh shot FILE.png    capture the app's top-level window (640x480 incl. frame/caption)
#   tools/orig.sh click X Y        left click at coordinates relative to that window
#   tools/orig.sh key KEYS...      send key(s), xdotool syntax (e.g. Escape, alt+F4)
#   tools/orig.sh wait SECONDS     sleep (keeps scripts readable)
#   tools/orig.sh stop             kill the app, wineserver and Xvfb
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/work"
export DISPLAY="${ORIG_DISPLAY:-:77}"
export WINEPREFIX="$WORK/wineprefix"
TITLE="Jak to dzia"     # main window title prefix

window_origin() {
    # Wine maps the app's top-level window (frame + caption included) as an X window.
    local wid; wid="$(xdotool search --name "$TITLE" | head -n1)"
    eval "$(xdotool getwindowgeometry --shell "$wid")"
    echo "$X $Y $WIDTH $HEIGHT"
}

case "${1:-}" in
start)
    if [ ! -e "/tmp/.X11-unix/X${DISPLAY#:}" ]; then
        Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp >"$WORK/xvfb.log" 2>&1 &
        echo $! >"$WORK/xvfb.pid"
        timeout 10 bash -c "until [ -e /tmp/.X11-unix/X${DISPLAY#:} ]; do sleep 0.2; done"
    fi
    "$ROOT/tools/wine-setup.sh" run >"$WORK/orig-wine.log" 2>&1 &
    timeout 60 bash -c "until xdotool search --name '$TITLE' >/dev/null 2>&1; do sleep 0.5; done"
    echo "started on $DISPLAY"
    ;;
shot)
    read -r X Y W H < <(window_origin)
    import -window root -crop "${W}x${H}+$X+$Y" +repage "${2:?file}"
    ;;
click)
    read -r X Y W H < <(window_origin)
    xdotool mousemove $((X + ${2:?x})) $((Y + ${3:?y})) click 1
    ;;
key)
    shift; xdotool key "$@"
    ;;
wait)
    timeout $(( ${2:?seconds} + 5 )) sleep "$2"
    ;;
stop)
    wineserver -k 2>/dev/null || true
    timeout 20 wineserver -w 2>/dev/null || true   # wait until every Wine process is gone
    if [ -f "$WORK/xvfb.pid" ]; then kill "$(cat "$WORK/xvfb.pid")" 2>/dev/null || true; rm -f "$WORK/xvfb.pid"; fi
    ;;
*)
    sed -n '2,10p' "$0"; exit 2 ;;
esac
