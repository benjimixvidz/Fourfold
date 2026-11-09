#!/bin/bash
# Coverage of the lines changed since a published version (default: the tag v2.7.0) in src/ and
# core/, by the core tests, the module tests and the sound scenarios (render_dump, saved patches
# included), built with gcov in a throwaway copy of the tree.
# A changed line counts if it is executable and ran in either test program. The delivery plan
# (B5) asks for at least 90 %. Lists the changed lines that never ran.
# Usage: tests/coverage.sh [BASE]   (from the repository root; RACK_DIR as for make)
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
RACK="${RACK_DIR:-$HERE/../..}"   # as the Makefile: the plugin inside Rack's plugins
BASE="${1:-v2.7.0}"
[ -f "$RACK/plugin.mk" ] || { echo "Rack SDK not found at $RACK (set RACK_DIR)"; exit 1; }
RACK="$(cd "$RACK" && pwd)"
T="$(mktemp -d "${TMPDIR:-/tmp}/tetrad-coverage.XXXXXX")"
trap 'rm -rf "$T"' EXIT
git -C "$HERE" ls-files -z | (cd "$HERE" && xargs -0 cp --parents -t "$T") || { echo "copy failed"; exit 1; }
cp "$HERE"/tests/*.cpp "$T/tests/" 2>/dev/null
# The module tests: src/ and core/ built by the SDK's rules, with gcov (EXTRA_FLAGS from the
# environment, so that the Makefile's own flags still apply).
EXTRA_FLAGS="--coverage -O0" make -s -C "$T" RACK_DIR="$RACK" test-module > "$T/module.log" 2>&1 \
    || { echo "module tests failed"; tail -5 "$T/module.log"; exit 1; }
# The sound scenarios and the saved patches (tests/render_dump.cpp) run the same objects.
EXTRA_FLAGS="--coverage -O0" make -s -C "$T" RACK_DIR="$RACK" render-dump > "$T/render.log" 2>&1 \
    && (cd "$T" && build/render_dump fixture tests/fixtures/tetrad-*.json >> "$T/render.log" 2>&1) \
    || { echo "render_dump failed"; tail -5 "$T/render.log"; exit 1; }
# The core tests, with gcov, in their own folder.
mkdir -p "$T/coretests" && (cd "$T/coretests" && g++ -std=c++20 -O0 --coverage -I"$T/core" -o core_tests \
    "$T/tests/core_tests.cpp" "$T"/core/*.cpp > build.log 2>&1 && ./core_tests > core.log 2>&1) \
    || { echo "core tests failed"; exit 1; }
python3 -I - "$T" "$HERE" "$BASE" <<'PY'
import glob, os, re, subprocess, sys
T, HERE, BASE = sys.argv[1:4]
diff = subprocess.run(["git", "-C", HERE, "diff", "-U0", BASE, "--", "src", "core"],
                      capture_output=True, text=True, check=True).stdout
changed, cur = {}, None
for line in diff.splitlines():
    if line.startswith("+++ "):
        cur = None if line.endswith("/dev/null") else line[6:]
    m = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", line)
    if m and cur:
        start, n = int(m.group(1)), int(m.group(2) or 1)
        changed.setdefault(cur, set()).update(range(start, start + n))
def gcov(objdir, gcdas):
    counts = {}
    for gcda in gcdas:   # run where the sources were compiled, so that gcov finds them
        r = subprocess.run(["gcov", "-p", "-o", os.path.dirname(gcda), gcda], cwd=T,
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("FAIL: gcov failed on", os.path.relpath(gcda, T)); sys.exit(1)
        # Read now: a header seen by several objects gets one report per object, same name.
        for f in glob.glob(os.path.join(T, "*.gcov")):
            parse(f, counts)
            os.remove(f)
    return counts
def parse(f, counts):
        src = None
        for l in open(f, errors="replace"):
            parts = l.split(":", 2)
            if len(parts) < 3:
                continue
            c, n = parts[0].strip(), parts[1].strip()
            if n == "0" and parts[2].startswith("Source:"):
                name = parts[2][7:].strip()   # relative to where it was compiled: the copy
                src = os.path.relpath(os.path.realpath(os.path.join(T, name)), os.path.realpath(T))
                continue
            if src is None or not n.isdigit():
                continue
            key = (src, int(n))
            if c in ("#####", "====="):
                counts.setdefault(key, 0)
            elif c != "-":
                counts[key] = counts.get(key, 0) + int(c.rstrip("*"))
a = gcov(os.path.join(T, "build"), glob.glob(os.path.join(T, "build", "**", "*.gcda"), recursive=True))
b = gcov(os.path.join(T, "coretests"), glob.glob(os.path.join(T, "coretests", "*.gcda")))
total = ran = 0
missed = []
for path, lines in sorted(changed.items()):
    for n in sorted(lines):
        ka = a.get((path, n)); kb = b.get((path, n))
        if ka is None and kb is None:
            continue   # not executable
        total += 1
        if (ka or 0) + (kb or 0) > 0:
            ran += 1
        else:
            missed.append(f"{path}:{n}")
if total == 0 or not a or not b:
    print("FAIL: no coverage data (gcov found no executable changed line)"); sys.exit(1)
pct = 100.0 * ran / total
print(f"changed executable lines since {BASE}: {ran} / {total} ran ({pct:.1f} %)")
for m in missed:
    print("  never ran:", m)
print("OK: at least 90 %" if pct >= 90.0 else "FAIL: under 90 %")
sys.exit(0 if pct >= 90.0 else 1)
PY
