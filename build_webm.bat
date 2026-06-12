@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_webm.ps1" %*
exit /b %errorlevel%
