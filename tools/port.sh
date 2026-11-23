#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Drive the port headless on Xvfb :78 (X11 video, dummy audio) for tests.
#
#   tools/port.sh start [twtw args]    e.g. tools/port.sh start --page 104
#   tools/port.sh click X Y            left click at logical (game) coordinates
#   tools/port.sh move X Y
#   tools/port.sh key KEYSYM
#   tools/port.sh shot FILE.png        screenshot, scaled back to the logical 638x459
#   tools/port.sh stop
#
# The debug log (pages, pop-ups, media) goes to work/port.log.
# The window is 1276x918 (2x the 638x459 working area); its position is read from X.
export DISPLAY=:78 SDL_VIDEO_DRIVER=x11 SDL_AUDIO_DRIVER=dummy
export XDG_DATA_HOME="$(cd "$(dirname "$0")/.." && pwd)/work/xdg"   # keep test settings out of the user profile
unset WAYLAND_DISPLAY
R=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}
win() { xdotool search --name "Jak to dzia" | head -1; }
origin() { eval "$(xdotool getwindowgeometry --shell "$(win)")"; OX=$X; OY=$Y; }
case "$1" in
start)
    shift
    [ -S /tmp/.X11-unix/X78 ] || { Xvfb :78 -screen 0 1600x1200x24 -nolisten tcp >/dev/null 2>&1 & sleep 1; }
    (SDL_LOGGING=app=debug "$R/build/twtw" "$R/Jak To Działa.iso" "$@" > "$R/work/port.log" 2>&1 &)
    timeout 10 xdotool search --sync --name "Jak to dzia" >/dev/null
    sleep 0.5
    xdotool windowfocus "$(win)"
    ;;
click) origin; xdotool mousemove $((OX + 2 * $2 + 1)) $((OY + 2 * $3 + 1)) click 1 ;;
move) origin; xdotool mousemove $((OX + 2 * $2 + 1)) $((OY + 2 * $3 + 1)) ;;
key) xdotool key "$2" ;;
shot)
    origin
    import -window root -crop "1276x918+$OX+$OY" +repage "$T/twtw-port-full.png"
    python3 -I -c "
from PIL import Image
Image.open('$T/twtw-port-full.png').convert('RGB').resize((638, 459), Image.NEAREST).save('$2')"
    ;;
stop) pkill -x twtw; sleep 0.5 ;;
*) sed -n '2,13p' "$0"; exit 2 ;;
esac
