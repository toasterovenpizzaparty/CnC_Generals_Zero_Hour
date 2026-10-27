@echo off
rem Installs generalszh.exe into your Command & Conquer Generals Zero Hour folder.
rem See README.txt. Pass -GameDir "<folder>" to choose the folder yourself.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
set ZH_RESULT=%ERRORLEVEL%
if not defined ZH_NO_PAUSE pause
exit /b %ZH_RESULT%
