@echo off
rem Build the HML runtime with MSVC + Ninja. Usage: build.cmd [Debug|Release]
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
rem Configure only the first time, or when a build type is given: after that Ninja runs CMake again
rem by itself when a CMakeLists.txt changes (configuring every time cost about 20 s).
if "%1"=="" if exist "%~dp0build\build.ninja" goto build
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
:build
cmake --build "%~dp0build" || exit /b 1
echo.
echo Built: %~dp0build\bin\hml_runtime.exe
