# Designer test helpers (developer only)

Drive the running designer through the Chrome DevTools Protocol (needs Python 3 + aiohttp):

    set WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222
    build\bin\hml_designer.exe projects\demo\project.json
    python -I tools\designer\test\cdp.py "M.st.screens.length" "App.addWidget('button'); M.st.sel"

Each argument is a JS expression evaluated in the page (promises are awaited).
`shot.ps1 -Out file.png` captures the designer window.

## UI tests (Windows, local)

`ui_test.py` starts the built designer on a throw-away copy of `projects/demo` and runs
`ui_tests.js` inside its page (mouse, keyboard and button events on the real page): several
widgets selected and aligned, dragging with snapping, copy and paste across screens, the palette
drop, the problems list, the language.

```
python -I tools/designer/test/ui_test.py        # needs aiohttp and build/bin/hml_designer.exe
```

It is not part of CI (it needs a desktop session with WebView2). Run it after changing the
renderer; the files next to the exe are used, so build first (or copy `renderer/*` to
`build/bin/designer/`).
