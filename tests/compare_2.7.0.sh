#!/bin/bash
# Sound against the published 2.7.0: the same scenarios (tests/render_dump.cpp) played by this tree
# and by the tag v2.7.0, every output of every sample hashed per phase.
#   Must be identical: phases 1-6 (one patch: internal clock, RUN gate, threshold, NaN, external
#   clock with RESET, PLAY), 8-13 (an irregular clock that never stops the transport: swing, a
#   slowdown, missing pulses, a brief stop, a gap of exactly four periods), 14-15 (Tetrad Pipe:
#   its outputs too), 16 (settings moved during playback), 0b (an opening, after its first beat),
#   and the TOTAL.
#   May differ: 0a (the first beat of a patch opening while its clock strikes: 2.7.1 plays step 1)
#   and 7 (the clock stops for 2 s and starts again: 2.7.1 keeps its tempo). See CHANGELOG.md.
#   Then patches saved by earlier versions (tests/fixtures/, and EXTRA_PATCHES): must play the same.
#   EXTRA_PATCHES="a.vcv b.vcv" tests/compare_2.7.0.sh   adds your own patches (never committed).
# Usage: tests/compare_2.7.0.sh   (from the repository root; RACK_DIR as for make)
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
RACK="${RACK_DIR:-$HERE/../..}"   # as the Makefile: the plugin inside Rack's plugins
[ -f "$RACK/plugin.mk" ] || { echo "Rack SDK not found at $RACK (set RACK_DIR)"; exit 1; }
RACK="$(cd "$RACK" && pwd)"   # absolute: the builds below run in other folders
REF="$(mktemp -d "${TMPDIR:-/tmp}/tetrad-v2.7.0.XXXXXX")"
trap 'rm -rf "$REF"' EXIT
git -C "$HERE" archive v2.7.0 | tar -x -C "$REF" || { echo "tag v2.7.0 not found"; exit 1; }
cp "$HERE/tests/render_dump.cpp" "$REF/tests/"
sed -n '/^# One scenario, every output hashed/,/^\tbuild\/render_dump$/p' "$HERE/Makefile" >> "$REF/Makefile"
SLUGVER=(SLUG=Fourfold VERSION=2.7.0)
make -C "$REF" RACK_DIR="$RACK" "${SLUGVER[@]}" -j8 all >/dev/null 2>&1 || { echo "2.7.0 does not build"; exit 1; }
LINES='^[0-9]+[ab]? |^TOTAL|^steps'
a=$(make -s -C "$REF" RACK_DIR="$RACK" "${SLUGVER[@]}" render-dump 2>/dev/null | grep -E "$LINES")
b=$(make -s -C "$HERE" RACK_DIR="$RACK" render-dump 2>/dev/null | grep -E "$LINES")
echo "--- 2.7.0"; echo "$a"; echo "--- this tree"; echo "$b"
fail=0
pa=$(echo "$a" | grep -vE "^(0a|7) "); pb=$(echo "$b" | grep -vE "^(0a|7) ")
if [ "$(echo "$pa" | grep -cE '^[0-9]')" -eq 16 ] && [ "$pa" = "$pb" ]; then
    echo "OK: phases 1-6, 8-16 and 0b identical to 2.7.0, sample by sample"
else
    echo "FAIL: a phase that must be identical differs (or is missing)"; fail=1
fi
if echo "$b" | grep -q "pipe rises to 10 V: 0)"; then echo "FAIL: a Tetrad Pipe phase sends no gate (empty proof)"; fail=1; fi
# Every phase must play notes: an empty phase would prove nothing (but 0a in 2.7.0: that first beat
# was silent, the very fix of 2.7.1).
if { echo "$b"; echo "$a" | grep -v "^0a "; } | grep -E "^[0-9]" | grep -q "gate-on samples: 0)"; then
    echo "FAIL: a phase plays no note (empty proof)"; fail=1
fi
# Patches saved by earlier versions: tests/fixtures/tetrad-*.json (written by 2.6.2, 2.6.7 and
# 2.7.0, see tests/fixtures/make_fixtures.sh), and any patch given in EXTRA_PATCHES (.vcv or
# patch.json, space-separated; your own patches, never committed). Opened as Rack opens a patch
# and played by both builds: they must play the same.
FIX=("$HERE"/tests/fixtures/tetrad-*.json)
fixfail=0
k=0
for f in ${EXTRA_PATCHES:-}; do
    k=$((k + 1)); out="$REF/extra-$k-$(basename "${f%.*}").json"
    if [ "$(head -c4 "$f" | od -An -tx1 | tr -d ' ')" = "28b52ffd" ]; then
        zstd -q -d -c "$f" | tar -xO --wildcards '*patch.json' > "$out" 2>/dev/null || { echo "FAIL: $f unreadable"; fixfail=1; continue; }
    else
        cp "$f" "$out" || { echo "FAIL: $f unreadable"; fixfail=1; continue; }
    fi
    FIX+=("$out")
done
fa=$("$REF/build/render_dump" fixture "${FIX[@]}" 2>/dev/null | grep "^F ")
fb=$("$HERE/build/render_dump" fixture "${FIX[@]}" 2>/dev/null | grep "^F ")
echo "--- saved patches, 2.7.0 | this tree"
paste -d'|' <(echo "$fa") <(echo "$fb") | awk -F'|' '{print ($1 == $2 ? "  same " : "  DIFF ") $1}'
# Every file must have been read and must hold a Tetrad: one that is not would prove nothing.
for f in "${FIX[@]}"; do
    b=$(basename "$f")
    if echo "$fb" | grep -qF "F $b unreadable"; then echo "FAIL: $b unreadable"; fixfail=1
    elif ! echo "$fb" | awk -v b="$b" '$2 == b || index($2, b "#") == 1 {found = 1} END {exit !found}'; then
        echo "FAIL: no Tetrad in $b"; fixfail=1
    fi
done
if [ "$fixfail" = 0 ] && [ -n "$fa" ] && [ "$fa" = "$fb" ]; then
    echo "OK: every saved patch plays as with 2.7.0"
else
    echo "FAIL: a saved patch plays differently, or was not read"; fail=1
fi
if echo "$fb" | grep -q "gate-on samples: 0)"; then echo "FAIL: a saved patch plays no note (empty proof)"; fail=1; fi
for p in 0a 7; do
    [ "$(echo "$a" | grep "^$p ")" = "$(echo "$b" | grep "^$p ")" ] && echo "phase $p: identical" || echo "phase $p: differs (documented)"
done
exit $fail
