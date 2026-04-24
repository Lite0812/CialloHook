@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_all.ps1" -Platform x86 -Configuration Release -Target all %*
exit /b %errorlevel%
