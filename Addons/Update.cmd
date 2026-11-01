@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Runtime\Update.ps1" -Interactive
if errorlevel 1 pause
