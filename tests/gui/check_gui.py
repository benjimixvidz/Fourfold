"""Assertions on the module state Rack saved after each GUI action of actions.sh (RUN input menu)."""
import json, os, sys
OUT = sys.argv[1]
VERSION = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "plugin.json")))["version"]
def load(name):
    d = json.load(open(f"{OUT}/{name}.json"))
    m = next(x for x in d["modules"] if x["model"] == "Tetrad")
    return m, {p["id"]: p["value"] for p in m["params"]}
fails = 0
def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    fails += not cond
m0, p0 = load("s0_opened")
check(m0["version"] == VERSION, "the module saved by this build (version %s)" % m0["version"])
check(m0["data"].get("runMode") == 0, "a patch from 2.7.0 (no RUN setting) opens in Gate")
check(m0["data"].get("runLatch") is True, "and playing")
m1, p1 = load("s1_trigger")
check(m1["data"].get("runMode") == 1, "menu RUN input > Trigger: trigger mode")
check(m1["data"].get("runLatch") is True, "a new mode starts playing")
m2, p2 = load("s2_gate")
check(m2["data"].get("runMode") == 0, "menu RUN input > Gate: gate mode")
same = lambda a, b: a["data"]["state"] == b["data"]["state"] and a["data"].get("ppqn") == b["data"].get("ppqn")
check(same(m0, m1) and same(m1, m2), "the RUN mode changes neither the patterns nor the PPQN")
check(p0 == p1 == p2, "nor any parameter")
m3, p3 = load("s3_undo")
check(m3["data"].get("runMode") == 1, "Ctrl+Z: the RUN mode back to trigger")
m4, p4 = load("s4_redo")
check(m4["data"].get("runMode") == 0, "Ctrl+Shift+Z: gate again")
m5, p5 = load("s5_bypass")
check(m5.get("bypass") is True, "Ctrl+E bypasses the module")
m6, p6 = load("s6_randomize")
check(m6.get("bypass") in (None, False), "Ctrl+E again: no longer bypassed")
check(m6["data"].get("runMode") == 0 and m6["data"].get("runLatch") is True,
      "Randomize leaves the RUN mode and play / pause alone")
EXPERT = max(p4)   # the EXPERT view, the last parameter (not randomized)
check(p6.get(EXPERT) == p4.get(EXPERT), "Randomize leaves the EXPERT view alone")
d = json.load(open(f"{OUT}/s7_duplicate.json"))
tetrads = [x for x in d["modules"] if x["model"] == "Tetrad"]
check(len(tetrads) == 2, "Ctrl+D duplicates Tetrad (%d now)" % len(tetrads))
if len(tetrads) == 2:
    a, b = tetrads
    check(a["data"].get("runMode") == b["data"].get("runMode") and a["data"]["state"] == b["data"]["state"],
          "the duplicate keeps the RUN mode and the patterns")
print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
