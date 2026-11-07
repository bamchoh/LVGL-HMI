# Designer test helpers (developer only)

Drive the running designer through the Chrome DevTools Protocol (needs Python 3 + aiohttp):

    set WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222
    build\bin\hml_designer.exe projects\demo\project.json
    python -I tools\designer\test\cdp.py "M.st.screens.length" "App.addWidget('button'); M.st.sel"

Each argument is a JS expression evaluated in the page (promises are awaited).
`shot.ps1 -Out file.png` captures the designer window.
