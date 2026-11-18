#!/usr/bin/env python3
"""Tap the touch screen of the device from a shell (stdlib only): UI checks without a finger.

    python3 tap.py [--project <project.json>] X,Y[,WAIT_MS] ...

X,Y are screen coordinates; WAIT_MS is the pause after the tap (default 400). Example, on the
3.5" LCD project: press "Setup", then the first user of the login screen:

    python3 tools/rpi/tap.py --project projects/lcd35/project.json 440,20,900 95,70

How it works: an input event written to /dev/input/eventN is delivered to everything that reads
the device, so the runtime sees a real touch. The user needs write access to the device (group
"input"). Screen coordinates are turned back into raw touch values with the display size and
the touch settings of the project ("display": { "width", "height", "input",
"touch": { "swap_axes", "calibration": [min_x, min_y, max_x, max_y] } }); without --project
the values of projects/lcd35 are used. The options override either.

To see the result, grab the frame buffer:  cat /dev/fb0 > shot.raw   (RGB565, width x height)

Note: whoever may run this can operate the HMI without touching it, user levels included.
That is anyone who can log in as the runtime's user; nothing new is opened by this script.
"""
import argparse
import glob
import json
import re
import struct
import sys
import time

EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
ABS_X, ABS_Y, ABS_PRESSURE, BTN_TOUCH = 0, 1, 24, 0x14A
EVENT = struct.Struct("llHHi")          # struct input_event: timeval, type, code, value


def load_project(path):
    """display settings of a project.json (// comments allowed)"""
    with open(path, encoding="utf-8") as f:
        text = re.sub(r"^\s*//.*$", "", f.read(), flags=re.M)
    return json.loads(text).get("display", {})


def find_device():
    """the first event device that reports absolute X (a touch screen)"""
    for dev in sorted(glob.glob("/sys/class/input/event*")):
        try:
            with open(dev + "/device/capabilities/abs") as f:
                if int(f.read().split()[-1], 16) & 0x3 == 0x3:
                    return "/dev/input/" + dev.rsplit("/", 1)[1]
        except (OSError, ValueError, IndexError):
            pass
    return "/dev/input/event0"


def main():
    ap = argparse.ArgumentParser(description="Inject taps into the touch screen's input device.")
    ap.add_argument("taps", nargs="+", metavar="X,Y[,WAIT_MS]")
    ap.add_argument("--project", help="project.json to take the display size and touch settings from")
    ap.add_argument("--device", help="input device (default: display.input of the project, else the first touch device)")
    ap.add_argument("--size", metavar="WxH", help="screen size, e.g. 480x320")
    ap.add_argument("--calibration", metavar="MINX,MINY,MAXX,MAXY", help="raw values of the screen edges (after the axis swap)")
    ap.add_argument("--swap-axes", dest="swap", action="store_true", default=None, help="the touch panel's X is the screen's Y")
    ap.add_argument("--no-swap-axes", dest="swap", action="store_false")
    ap.add_argument("--hold-ms", type=int, default=90, help="how long a tap is held (default 90)")
    args = ap.parse_args()

    # projects/lcd35: 480x320, ADS7846
    width, height, swap, calib, device = 480, 320, True, [150, 3850, 3850, 220], None
    if args.project:
        d = load_project(args.project)
        width, height = int(d.get("width", 800)), int(d.get("height", 480))
        touch = d.get("touch", {})
        swap = bool(touch.get("swap_axes", False))
        calib = touch.get("calibration") or [0, 0, 4095, 4095]
        device = d.get("input") or None
    if args.size:
        width, height = (int(v) for v in args.size.lower().split("x"))
    if args.calibration:
        calib = [int(v) for v in args.calibration.split(",")]
    if args.swap is not None:
        swap = args.swap
    device = args.device or device or find_device()
    if len(calib) != 4:
        sys.exit("calibration: four numbers (min_x, min_y, max_x, max_y)")
    min_x, min_y, max_x, max_y = calib

    taps = []
    for t in args.taps:
        p = t.split(",")
        if len(p) not in (2, 3):
            sys.exit("a tap is X,Y or X,Y,WAIT_MS: " + t)
        taps.append((int(p[0]), int(p[1]), int(p[2]) if len(p) == 3 else 400))

    try:
        f = open(device, "wb", buffering=0)
    except OSError as e:
        sys.exit("cannot open %s for writing: %s (is the user in the 'input' group?)" % (device, e.strerror))

    def send(events):
        f.write(b"".join(EVENT.pack(0, 0, t, c, v) for t, c, v in events) + EVENT.pack(0, 0, EV_SYN, 0, 0))

    with f:
        for sx, sy, wait in taps:
            # what the runtime does backwards: screen -> calibrated raw -> (swapped) panel axes
            cx = round(min_x + sx * (max_x - min_x) / max(1, width - 1))
            cy = round(min_y + sy * (max_y - min_y) / max(1, height - 1))
            raw_x, raw_y = (cy, cx) if swap else (cx, cy)
            # Two reports one count apart: the kernel drops values that did not change, and a
            # tap on the same spot as the last one would otherwise carry no position at all.
            for jitter in (0, 1):
                send([(EV_ABS, ABS_X, raw_x + jitter), (EV_ABS, ABS_Y, raw_y + jitter), (EV_ABS, ABS_PRESSURE, 180 + jitter), (EV_KEY, BTN_TOUCH, 1)])
                time.sleep(args.hold_ms / 2000)
            send([(EV_ABS, ABS_PRESSURE, 0), (EV_KEY, BTN_TOUCH, 0)])
            time.sleep(wait / 1000)


if __name__ == "__main__":
    main()
