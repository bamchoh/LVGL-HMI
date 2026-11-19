"""UI tests of the designer (Windows): starts the built designer on a throw-away copy of
projects/demo, runs ui_tests.js inside its page over the Chrome DevTools Protocol (WebView2
remote debugging) and reports. Exit code 0 = all passed.

usage: python -I tools/designer/test/ui_test.py [path to hml_designer.exe]
needs: aiohttp (pip install aiohttp); the designer built (build/bin/hml_designer.exe).
The renderer files next to the exe are used: build first, or copy tools/designer/renderer/*
to build/bin/designer/.
"""
import asyncio, json, os, shutil, subprocess, sys, tempfile, time, urllib.request
import aiohttp

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
PORT = 9339


async def evaluate(expr):
    targets = json.loads(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=5).read())
    page = next(t for t in targets if t.get("type") == "page" and "index.html" in t.get("url", ""))
    async with aiohttp.ClientSession() as s:
        async with s.ws_connect(page["webSocketDebuggerUrl"], max_msg_size=64 * 1024 * 1024) as ws:
            await ws.send_json({"id": 1, "method": "Runtime.evaluate",
                                "params": {"expression": expr, "awaitPromise": True, "returnByValue": True}})
            while True:
                msg = json.loads((await ws.receive()).data)
                if msg.get("id") == 1:
                    break
    res = msg.get("result", {})
    if "exceptionDetails" in res:
        raise RuntimeError(res["exceptionDetails"].get("exception", {}).get("description", str(res["exceptionDetails"])))
    return res.get("result", {}).get("value")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "bin", "hml_designer.exe")
    if not os.path.exists(exe):
        print("designer not found:", exe); return 2
    work = tempfile.mkdtemp(prefix="hml-ui-test-")
    proj = os.path.join(work, "demo")
    shutil.copytree(os.path.join(ROOT, "projects", "demo"), proj, ignore=shutil.ignore_patterns("logs", "project.local.json"))
    project = os.path.join(proj, "project.json").replace("\\", "/")
    env = dict(os.environ, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f"--remote-debugging-port={PORT}",
               WEBVIEW2_USER_DATA_FOLDER=os.path.join(work, "webview"))
    p = subprocess.Popen([exe, project], env=env)
    failed = 1
    try:
        deadline = time.time() + 60
        while True:                                   # until the page has loaded the project
            try:
                if asyncio.run(evaluate("typeof M !== 'undefined' && !!M.st.project && !!M.st.path && !!document.getElementById('status-prob')")):
                    break
            except Exception:
                pass
            if time.time() > deadline or p.poll() is not None:
                print("the designer page did not come up"); return 2
            time.sleep(1)
        time.sleep(1)
        results = asyncio.run(evaluate(open(os.path.join(HERE, "ui_tests.js"), encoding="utf-8").read()))
        failed = 0
        for r in results:
            print(("ok      " if r["ok"] else "FAILED  ") + r["name"] + ("" if r["ok"] else "\n          " + r.get("msg", "")))
            failed += 0 if r["ok"] else 1
        print(f"{len(results) - failed} passed, {failed} failed")
        # the throw-away project does not belong in the list of recent projects
        asyncio.run(evaluate("(async()=>{ App.recent = App.recent.filter(p => p.indexOf('hml-ui-test-') < 0); await host.settings({ recent: App.recent }); return true })()"))
    finally:
        p.kill(); p.wait()
        time.sleep(1)
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
