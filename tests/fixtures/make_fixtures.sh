#!/bin/bash
# Fixtures: a Tetrad module as each published version saves it in a patch (tags v2.6.2, v2.6.7,
# v2.7.0), written to tests/fixtures/tetrad-<version>.json. tests/compare_2.7.0.sh plays each one
# with this tree and with 2.7.0: they must play the same. Run once; the fixtures are committed.
# Usage: tests/fixtures/make_fixtures.sh   (from the repository root; RACK_DIR as for make)
set -u
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
RACK="${RACK_DIR:-$HERE/../..}"   # as the Makefile: the plugin inside Rack's plugins
[ -f "$RACK/plugin.mk" ] || { echo "Rack SDK not found at $RACK (set RACK_DIR)"; exit 1; }
RACK="$(cd "$RACK" && pwd)"   # absolute: the builds below run in other folders
TMP="$(mktemp -d "${TMPDIR:-/tmp}/tetrad-fixtures.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
for v in 2.6.2 2.6.7 2.7.0; do
    d="$TMP/$v"; mkdir -p "$d"
    git -C "$HERE" archive "v$v" | tar -x -C "$d" || { echo "tag v$v not found"; exit 1; }
    make -C "$d" RACK_DIR="$RACK" SLUG=Fourfold VERSION="$v" -j8 all >/dev/null 2>&1 || { echo "$v does not build"; exit 1; }
    flags=(); [ "$v" = 2.7.0 ] && flags=(-DHAS_DRUM)
    # C++17, as these versions were built: in C++20 their `namespace pipe` meets the system's pipe().
    mkdir -p "$d/tests" && cp "$HERE/tests/fixtures/save_fixture.cpp" "$d/tests/"
    g++ -std=c++17 -O1 "${flags[@]}" -isystem "$RACK/include" -isystem "$RACK/dep/include" -I"$d/src" -I"$d/core" \
        -o "$d/save_fixture" "$d/tests/save_fixture.cpp" "$d"/build/src/*.o "$d"/build/core/*.o \
        -L"$RACK" -lRack -Wl,-rpath,"$RACK" -lpthread || { echo "$v: the saver does not build"; exit 1; }
    "$d/save_fixture" "$v" > "$d/tetrad-$v.json" || { echo "$v: the saver failed"; exit 1; }
    mv "$d/tetrad-$v.json" "$HERE/tests/fixtures/tetrad-$v.json"
    echo "tests/fixtures/tetrad-$v.json"
done
