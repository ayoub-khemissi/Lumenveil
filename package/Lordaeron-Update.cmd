@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Lordaeron\update.ps1" %* & exit /b
