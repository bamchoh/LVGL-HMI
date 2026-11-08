@echo off
rem Build (incremental) and run the unit tests. Usage: test.cmd [extra doctest args, e.g. -tc="*format*"]
setlocal
call "%~dp0build.cmd" >nul || exit /b 1
"%~dp0build\bin\hml_tests.exe" %*
