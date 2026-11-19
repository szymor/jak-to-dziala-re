#!/bin/bash
# Drive the port headless on Xvfb :78 (X11 video, dummy audio) for tests.
#
#   tools/port.sh start [twtw args]    e.g. tools/port.sh start --page 104
#   tools/port.sh click X Y            left click at 640x480 logical coordinates
#   tools/port.sh move X Y
#   tools/port.sh key KEYSYM
#   tools/port.sh shot FILE.png        screenshot, scaled back to logical 640x480
#   tools/port.sh stop
#
# The debug log (pages, pop-ups, media) goes to work/port.log.
# The window is 1280x960, centred on the 1600x1200 screen at (160, 120).
export DISPLAY=:78 SDL_VIDEO_DRIVER=x11 SDL_AUDIO_DRIVER=dummy
unset WAYLAND_DISPLAY
R=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}
win() { xdotool search --name "Jak to dzia" | head -1; }
case "$1" in
start)
    shift
    [ -S /tmp/.X11-unix/X78 ] || { Xvfb :78 -screen 0 1600x1200x24 -nolisten tcp >/dev/null 2>&1 & sleep 1; }
    (SDL_LOGGING=app=debug "$R/build/twtw" "$R/Jak To Działa.iso" "$@" > "$R/work/port.log" 2>&1 &)
    timeout 10 xdotool search --sync --name "Jak to dzia" >/dev/null
    sleep 0.5
    xdotool windowfocus "$(win)"
    ;;
click) xdotool mousemove $((160 + 2 * $2 + 1)) $((120 + 2 * $3 + 1)) click 1 ;;
move) xdotool mousemove $((160 + 2 * $2 + 1)) $((120 + 2 * $3 + 1)) ;;
key) xdotool key "$2" ;;
shot)
    import -window root -crop 1280x960+160+120 +repage "$T/twtw-port-full.png"
    python3 -I -c "
from PIL import Image
Image.open('$T/twtw-port-full.png').convert('RGB').resize((640, 480), Image.NEAREST).save('$2')"
    ;;
stop) pkill -x twtw; sleep 0.5 ;;
*) sed -n '2,13p' "$0"; exit 2 ;;
esac
