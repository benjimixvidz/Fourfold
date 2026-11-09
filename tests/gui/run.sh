#!/usr/bin/env bash
# GUI test: the local build in a throwaway VCV Rack (dev mode, its own user folder: yours is not
# touched), driven by synthetic X events through a fake pointer (your pointer and focus are not
# touched either), then the module state Rack saved after each action is checked. The patch holds
# a Tetrad saved by 2.7.0 (no RUN setting). Without a monitor (screen off), Rack runs in a nested
# X server (Xephyr) instead.
#   tests/gui/run.sh   (or: make gui-test)
# RACK_APP: the folder of Rack 2 Free (required; make passes it on). Needs X11, xrandr,
# xdotool, xwd, ffmpeg, zstd, python3; Xephyr when no monitor is on.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
P="$(cd "$HERE/../.." && pwd)"
S="$P/build/gui"
RACK_DIR_APP="${RACK_APP:-}"
[ -x "$RACK_DIR_APP/Rack" ] || { echo "set RACK_APP to the folder of Rack 2 Free (it holds the Rack program)"; exit 1; }
W="$S/w"; mkdir -p "$S/out"
pids=()
trap 'for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done; wait 2>/dev/null || true' EXIT
if ! xrandr --listmonitors 2>/dev/null | grep -q "^ *0:"; then
    command -v Xephyr >/dev/null || { echo "no monitor and no Xephyr: cannot open Rack"; exit 1; }
    n=57; while [ -e "/tmp/.X11-unix/X$n" ]; do n=$((n + 1)); done
    Xephyr ":$n" -screen 1500x800x24 -ac -nolisten tcp > "$S/xephyr.log" 2>&1 &
    pids+=($!)
    export DISPLAY=":$n"
    for _ in $(seq 1 40); do xrandr --listmonitors >/dev/null 2>&1 && break; sleep 0.25; done
    echo "no monitor: Rack runs in Xephyr on $DISPLAY"
fi
g++ -std=c++20 -O2 -o "$S/x-send" "$HERE/x_send.cpp" -lX11
gcc -O2 -shared -fPIC -o "$S/fake-pointer.so" "$HERE/fake_pointer.c" -ldl
XSEND="$S/x-send"
rm -rf "$W"; mkdir -p "$W/user/plugins/Fourfold" "$W/patch"
cp -r "$P/plugin.so" "$P/plugin.json" "$P/res" "$W/user/plugins/Fourfold/"
cat > "$W/user/settings.json" <<JSON
{"showTipsOnLaunch": false, "tipIndex": -1, "cpuMeter": true, "windowSize": [1400, 700], "windowPos": [40, 40], "discordUpdateActivity": false, "autosaveInterval": 0.5}
JSON
cp "$HERE/patch.json" "$W/patch/patch.json"
tar -c -C "$W/patch" patch.json | zstd -q -o "$W/patch/live.vcv"
export FAKE_POINTER="$W/pointer"; echo "-100 -100" > "$FAKE_POINTER"
cd "$RACK_DIR_APP"
LD_PRELOAD="$S/fake-pointer.so" ./Rack -d -u "$W/user" "$W/patch/live.vcv" > "$W/rack.log" 2>&1 &
pid=$!
pids+=("$pid")
wid=""
for _ in $(seq 1 80); do wid=$(xdotool search --pid "$pid" --name "live.vcv" 2>/dev/null | head -1 || true); [[ -n $wid ]] && break; sleep 0.25; done
[[ -z $wid ]] && { echo "no window"; tail -3 "$W/rack.log"; exit 1; }
sleep 5
shot() { xwd -id "$wid" -silent > "$W/s.xwd" && ffmpeg -loglevel error -y -i "$W/s.xwd" "$S/out/$1.png"; echo "shot $S/out/$1.png"; }
shot start
read -r MX MY < <(python3 -I "$HERE/origin.py" "$S/out/start.png")
echo "Tetrad at $MX,$MY"
# snap NAME: the module's saved state after the last action (Rack's autosave, every 0.5 s).
snap() { sleep 1.6; cp "$W/user/autosave/patch.json" "$S/out/$1.json" && echo "snap $1"; }
# Module coordinates (from Tetrad's top-left corner) to window coordinates.
xm() { local cmd=$1 x=$2 y=$3; shift 3; "$XSEND" "$wid" "$cmd" $((x + MX)) $((y + MY)) "$@"; sleep 0.4; }
source "$HERE/actions.sh"
kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true
grep -E "warn|error" "$W/rack.log" | grep -iv fontconfig | head -10 || true
python3 -I "$HERE/check_gui.py" "$S/out"
