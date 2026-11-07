"""Drive the designer page through the Chrome DevTools Protocol (WebView2 remote debugging).
usage: python -I cdp.py "<js expression>" [...]   (each expression is awaited; result printed as JSON)
"""
import asyncio, json, sys, urllib.request
import aiohttp

PORT = 9222

async def main(exprs):
    targets = json.loads(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json").read())
    page = next((t for t in targets if t.get("type") == "page" and "index.html" in t.get("url", "")), None)
    if not page:
        print("no page target; targets:", [t.get("url") for t in targets]); return 2
    async with aiohttp.ClientSession() as s:
        async with s.ws_connect(page["webSocketDebuggerUrl"], max_msg_size=64 * 1024 * 1024) as ws:
            mid = 0
            async def call(method, **params):
                nonlocal mid
                mid += 1
                await ws.send_json({"id": mid, "method": method, "params": params})
                while True:
                    msg = json.loads((await ws.receive()).data)
                    if msg.get("id") == mid:
                        return msg
            for e in exprs:
                r = await call("Runtime.evaluate", expression=e, awaitPromise=True, returnByValue=True, timeout=30000)
                if "error" in r:
                    print("ERROR", r["error"]); continue
                res = r["result"]
                if "exceptionDetails" in res:
                    print("EXCEPTION", json.dumps(res["exceptionDetails"].get("exception", {}).get("description", res["exceptionDetails"]))[:1500])
                else:
                    v = res["result"].get("value", res["result"].get("description"))
                    out = json.dumps(v, ensure_ascii=False)
                    print(out if len(out) < 4000 else out[:4000] + " ...")
    return 0

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(asyncio.run(main(sys.argv[1:])))
