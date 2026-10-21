@echo off
setlocal
title Lumenveil uninstall
cd /d "%~dp0"

if not exist "Wow.exe" (
  echo Lumenveil-Uninstall.cmd must be in the game folder, next to Wow.exe.
  pause
  exit /b 1
)
tasklist /fi "imagename eq Wow.exe" 2>nul | find /i "Wow.exe" >nul
if not errorlevel 1 (
  echo Please close the game first.
  pause
  exit /b 1
)

echo This removes Lumenveil and its settings from this game folder:
echo   version.dll, CoAVolFog.dll, CoAVolFog.ini, CoAVolFog-regions.ini, lampdata.bin, liquiddata.bin,
echo   CoAVolFog.log, CoAVolFog-spots.txt, the Lumenveil folder and the two Lumenveil .cmd files.
choice /c YN /m "Remove Lumenveil"
if errorlevel 2 exit /b 0

for %%F in (version.dll CoAVolFog.dll CoAVolFog.ini CoAVolFog-regions.ini lampdata.bin liquiddata.bin CoAVolFog.log CoAVolFog-spots.txt Lumenveil-Setup.cmd) do if exist "%%F" del /q "%%F"
if exist "CoAVolFog-spots" rmdir /s /q "CoAVolFog-spots"
if exist "Lumenveil" rmdir /s /q "Lumenveil"
echo Lumenveil is removed. The game runs as before.
pause
(goto) 2>nul & del /q "%~f0"
