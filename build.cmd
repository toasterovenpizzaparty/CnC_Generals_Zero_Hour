@echo off
rem Builds generalszh.exe on Windows. Downloads its own compiler, CMake and Ninja into .tools\
rem the first time; nothing is installed system-wide. See BUILDING.md.
rem   build.cmd             build\win32\dist\generalszh.exe
rem   build.cmd -Install    ... and install it into the Zero Hour folder
rem   build.cmd -Clean      rebuild from scratch
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" %*
set ZH_RESULT=%ERRORLEVEL%
if not defined ZH_NO_PAUSE pause
exit /b %ZH_RESULT%
