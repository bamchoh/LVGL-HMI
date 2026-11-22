#!/usr/bin/env python3
"""Generate a synthetic HML project to see how the runtime copes with size (stdlib only).

    python tools/make_stress_project.py <out_dir> [--preset light|medium|heavy|extreme] [options]

    hml_runtime <out_dir>/project.json --stats 5       # logs how busy the main loop is

What is generated, each part sized by an option (the presets set them all):
  tags        float tags t00000 ...; the first --changing of them are driven by the sim driver,
              a new random value every --change-ms
  screens     --screens screens with --widgets widgets each (labels, bars, LEDs bound to tags,
              every screen to other tags) plus one chart; Prev / Next buttons in the header
  data log    a group of --log-tags tags sampled every second, and all changing tags every 10 s
  alarms      --alarms alarms on changing tags (threshold in the middle: they raise and clear a lot)
  scripts     --scripts Lua scripts, each summing 50 tags every 200 ms
  operation log and system log are on

Nothing in it talks to the outside (no Modbus, no OPC UA server unless --opcua).
"""
import argparse
import json
import os

PRESETS = {
    #            tags  changing change_ms screens widgets log_tags alarms scripts
    "light":   (  100,     50,     500,      3,     30,     20,     10,     1),
    "medium":  (  500,    250,     250,     10,     60,    100,     50,     5),
    "heavy":   ( 2000,   1000,     200,     30,    100,    300,    200,    10),
    "extreme": (10000,   5000,     100,     60,    150,   1000,   1000,    30),
}


def main():
    ap = argparse.ArgumentParser(description="Generate a synthetic HML project for load tests.")
    ap.add_argument("out_dir")
    ap.add_argument("--preset", choices=PRESETS, default="medium")
    for name, text in (("tags", "number of tags"), ("changing", "tags the sim driver changes"), ("change-ms", "how often they change"),
                       ("screens", "number of screens"), ("widgets", "bound widgets per screen"), ("log-tags", "tags in the 1 s data log group"),
                       ("alarms", "number of alarms"), ("scripts", "number of 200 ms Lua scripts")):
        ap.add_argument("--" + name, type=int, help=text + " (default: from the preset)")
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--height", type=int, default=320)
    ap.add_argument("--opcua", type=int, metavar="PORT", help="also publish every tag on an OPC UA server at this port")
    ap.add_argument("--no-logs", action="store_true", help="leave out data log, alarms, operation log and system log")
    a = ap.parse_args()

    names = ("tags", "changing", "change_ms", "screens", "widgets", "log_tags", "alarms", "scripts")
    v = dict(zip(names, PRESETS[a.preset]))
    for n in names:
        if getattr(a, n) is not None:
            v[n] = getattr(a, n)
    v["changing"] = min(v["changing"], v["tags"])
    tag = lambda i: "t%05d" % (i % v["tags"])
    W, H = a.width, a.height
    compact = W < 600
    font = 12 if compact else 14
    header_h = 36 if compact else 48

    project = {
        "title": "Stress %s: %d tags, %d screens x %d widgets" % (a.preset, v["tags"], v["screens"], v["widgets"]),
        "display": {"width": W, "height": H},
        "start_screen": "s000",
        "key_management": {"enabled": False},
        "tags": [{"name": tag(i), "type": "float"} for i in range(v["tags"])]
                + [{"name": "sum%02d" % k, "type": "float"} for k in range(v["scripts"])],
        "drivers": [{"type": "sim", "items": [
            {"tag": tag(i), "mode": "random", "min": 0, "max": 100, "period_ms": v["change_ms"]} for i in range(v["changing"])]}],
        "screens": ["screens/s%03d.json" % s for s in range(v["screens"])],
        "scripts": [{
            "name": "sum%02d" % k, "trigger": "interval", "interval_ms": 200,
            "code": "local s = 0\nfor i = %d, %d do s = s + tag[string.format('t%%05d', i %% %d)] end\ntag.sum%02d = s\n"
                    % (k * 50, k * 50 + 49, v["tags"], k),
        } for k in range(v["scripts"])],
    }
    if a.opcua:
        project["drivers"].append({"type": "opcua_server", "port": a.opcua, "security": "none"})
    if not a.no_logs:
        project["data_log"] = {"groups": [
            {"name": "fast", "trigger": "interval", "interval_ms": 1000, "tags": [tag(i) for i in range(v["log_tags"])]},
            {"name": "all", "trigger": "interval", "interval_ms": 10000, "tags": [tag(i) for i in range(v["changing"])]},
        ]}
        project["alarms"] = {"items": [
            {"name": "a%04d" % i, "tag": tag(i), "condition": "gt", "value": 50, "message": "alarm %d" % i} for i in range(v["alarms"])]}
        project["operation_log"] = {}
        project["system_log"] = {}

    os.makedirs(os.path.join(a.out_dir, "screens"), exist_ok=True)
    cols = 6 if compact else 8
    cell_w = (W - 16 - (cols - 1) * 4) // cols
    for s in range(v["screens"]):
        base = s * v["widgets"]
        cells = []
        for w in range(v["widgets"]):
            t = tag(base + w)
            kind = w % 4
            if kind in (0, 1):
                cells.append({"type": "label", "w": cell_w, "h": 16, "bind": {"text": {"tag": t, "format": "%.1f"}},
                              "style": {"font": font, "text_color": "#ffffff"}})
            elif kind == 2:
                cells.append({"type": "bar", "w": cell_w, "h": 12, "min": 0, "max": 100, "bind": t})
            else:
                cells.append({"type": "led", "w": 14, "h": 14, "bind": {"on": {"tag": t, "gt": 50}}})
        nxt, prv = "s%03d" % ((s + 1) % v["screens"]), "s%03d" % ((s - 1) % v["screens"])
        screen = {
            "name": "s%03d" % s, "scrollable": False, "style": {"bg_color": "#141a22", "pad": 0},
            "children": [
                {"type": "panel", "x": 0, "y": 0, "w": "100%", "h": header_h, "scrollable": False,
                 "style": {"bg_color": "#1f2a38", "radius": 0, "border_width": 0, "pad_left": 6, "pad_right": 6, "pad_top": 0, "pad_bottom": 0},
                 "children": [
                     {"type": "button", "align": "left_mid", "text": "< Prev", "h": header_h - 8, "style_label": {"font": font},
                      "on_click": {"action": "goto", "screen": prv, "anim": "none"}},
                     {"type": "label", "align": "center", "text": "Screen %d / %d" % (s + 1, v["screens"]),
                      "style": {"font": font + 4, "text_color": "#ffffff"}},
                     {"type": "button", "align": "right_mid", "text": "Next >", "h": header_h - 8, "style_label": {"font": font},
                      "on_click": {"action": "goto", "screen": nxt, "anim": "none"}},
                 ]},
                {"type": "chart", "x": 8, "y": header_h + 6, "w": W - 16, "h": 70, "points": 60, "min": 0, "max": 100, "sample_ms": 500,
                 "series": [{"tag": tag(base + k), "color": c} for k, c in enumerate(("#42a5f5", "#66bb6a", "#ffa726", "#ef5350"))]},
                {"type": "panel", "x": 8, "y": header_h + 82, "w": W - 16, "h": H - header_h - 90, "flex": "row_wrap", "scrollbar": "auto",
                 "style": {"bg_color": "#1b2430", "radius": 6, "border_width": 0, "pad": 6, "pad_column": 4, "pad_row": 4},
                 "children": cells},
            ],
        }
        with open(os.path.join(a.out_dir, "screens", "s%03d.json" % s), "w", encoding="utf-8") as f:
            json.dump(screen, f)
    with open(os.path.join(a.out_dir, "project.json"), "w", encoding="utf-8") as f:
        json.dump(project, f, indent=1)
    print("%s: %d tags (%d changing every %d ms = %d changes/s), %d screens x %d widgets (%d bound widgets), "
          "data log %d tags/s, %d alarms, %d scripts"
          % (a.out_dir, v["tags"], v["changing"], v["change_ms"], v["changing"] * 1000 // max(1, v["change_ms"]),
             v["screens"], v["widgets"], v["screens"] * v["widgets"], v["log_tags"], v["alarms"], v["scripts"]))


if __name__ == "__main__":
    main()
