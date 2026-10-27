@echo off
rem Removes generalszh.exe, its firewall rule and the desktop shortcut. The game itself is not changed.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -Uninstall %*
set ZH_RESULT=%ERRORLEVEL%
if not defined ZH_NO_PAUSE pause
exit /b %ZH_RESULT%
