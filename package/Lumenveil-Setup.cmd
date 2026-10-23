@echo off
setlocal
title Lumenveil setup
cd /d "%~dp0"

if not exist "Wow.exe" (
  echo Lumenveil-Setup.cmd must be in the game folder, next to Wow.exe.
  echo Copy all of Lumenveil's files there and run it again.
  pause
  exit /b 1
)
if not exist "Lumenveil\lamp_extract.exe" (
  echo The Lumenveil folder is missing. Copy all of Lumenveil's files into the game folder and run this again.
  pause
  exit /b 1
)

set "LOCALE="
for /d %%L in ("Data\????") do if exist "%%L\locale-%%~nxL.MPQ" set "LOCALE=%%~nxL"
if not defined LOCALE (
  echo No language folder with a locale archive was found in Data.
  echo Lumenveil needs the 3.3.5a client, build 12340.
  pause
  exit /b 1
)

echo Lumenveil reads your own game files once, to find where the world's lamps and water are.
echo This takes one to five minutes. Nothing is changed in your game files and nothing is sent anywhere.
echo Language: %LOCALE%
echo.

echo [1/3] Lamps, torches, braziers and windows...
"Lumenveil\lamp_extract.exe" --data "%~dp0Data" --out "%~dp0lampdata.bin" --locale %LOCALE% --background > "Lumenveil\setup-lamps.log" 2>&1
if errorlevel 1 (
  echo Failed. The details are in Lumenveil\setup-lamps.log.
  pause
  exit /b 1
)

echo [2/3] Lakes and the sea...
"Lumenveil\lamp_extract.exe" --data "%~dp0Data" --liquids "%~dp0liquiddata.bin" --maps 0,1,530,571 --locale %LOCALE% --background > "Lumenveil\setup-water.log" 2>&1
if errorlevel 1 (
  echo Failed. The details are in Lumenveil\setup-water.log.
  pause
  exit /b 1
)

echo [3/3] Water ripples and foam, when your game's archives hold them...
"Lumenveil\lamp_extract.exe" --data "%~dp0Data" --water-textures "%~dp0Lumenveil" --locale %LOCALE% > "Lumenveil\setup-water-textures.log" 2>&1
if errorlevel 1 (
  echo Failed. The details are in Lumenveil\setup-water-textures.log.
  pause
  exit /b 1
)

echo.
echo Done. Start the game as usual; if it is running, restart it.
echo   Ctrl+F12  switches Lumenveil off and on
echo   Ctrl+F7   opens the settings window
echo Run this again only after installing a patch or texture pack that changes the world.
pause
