"""Read-only live regression probe. Run from outside the tested workspace:

    python3 tests/wm/watch_close.py FIRST MIDDLE LAST

Use IDs from `stonefly windows --json`. Start with three half columns on the
same active workspace, with the last two filling the viewport. When prompted,
close the middle window (Super+q, then repeat with client close). Do not send
any more input. This script only queries scene coordinates; it never focuses
or closes a client. The initial visible pair supplies the expected positions.
"""

import argparse
import json
import subprocess
import time


def windows():
    reply = subprocess.check_output(["stonefly", "windows", "--json"], timeout=2)
    return {row["id"]: row for row in json.loads(reply)}


def matches(window, cell):
    return (window is not None and window["scene_enabled"] and not window["minimized"]
            and window["workspace"] == cell["workspace"] and window["output"] == cell["output"]
            and abs(window["x"] - cell["x"]) <= 1 and abs(window["w"] - cell["w"]) <= 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("first", "middle", "last"):
        parser.add_argument(key)
    args = parser.parse_args()
    initial = windows()
    a, b, c = (initial[key] for key in (args.first, args.middle, args.last))
    if not (all(matches(w, w) and w["workspace"] == b["workspace"] and w["output"] == b["output"]
                and abs(w["w"] - b["w"]) <= 1 for w in (a, b, c))
            and a["x"] + a["w"] <= b["x"] and b["x"] + b["w"] <= c["x"]):
        parser.error("expected three ordered, settled, half-width columns on one workspace")
    print("Close the middle client now, then leave input idle.", flush=True)
    deadline = time.monotonic() + 30
    while args.middle in windows():
        if time.monotonic() >= deadline:
            raise SystemExit("middle client did not unmap")
        time.sleep(0.05)
    deadline = time.monotonic() + 5
    since = None
    while time.monotonic() < deadline:
        state = windows()
        if matches(state.get(args.first), b) and matches(state.get(args.last), c):
            if since is None:
                since = time.monotonic()
            elif time.monotonic() - since >= 0.5:
                print("PASS: surviving scene nodes settled in the two original visible cells")
                return
        else:
            since = None
        time.sleep(0.05)
    raise SystemExit("survivors did not settle in the visible cells without input")


if __name__ == "__main__":
    main()
