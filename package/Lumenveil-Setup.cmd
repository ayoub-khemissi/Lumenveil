@echo off
setlocal
title Lumenveil setup
cd /d "%~dp0"
set "AUTO="
if /i "%~1"=="/auto" set "AUTO=1"

if not exist "Wow.exe" (
  echo Lumenveil-Setup.cmd must be in the game folder, next to Wow.exe.
  echo Copy all of Lumenveil's files there and run it again.
  if not defined AUTO pause
  exit /b 1
)
if not exist "Lumenveil\lamp_extract.exe" (
  echo The Lumenveil folder is missing. Copy all of Lumenveil's files into the game folder and run this again.
  if not defined AUTO pause
  exit /b 1
)

set "LOCALE="
for /d %%L in ("Data\????") do if exist "%%L\locale-%%~nxL.MPQ" set "LOCALE=%%~nxL"
if not defined LOCALE (
  echo No language folder with a locale archive was found in Data.
  echo Lumenveil needs the 3.3.5a client, build 12340.
  if not defined AUTO pause
  exit /b 1
)

if not defined AUTO (
  echo Lumenveil reads your own game files once, to find where the world's lamps and water are.
  echo This takes about half a minute, and a second when nothing changed since the last run. Nothing is changed in
  echo your game files and nothing is sent anywhere.
  echo Language: %LOCALE%
  echo.
  echo Lamps, windows, lakes, the sea and the water textures, on all your processor's cores...
)
"Lumenveil\lamp_extract.exe" --data "%~dp0Data" --out "%~dp0lampdata.bin" --liquids "%~dp0liquiddata.bin" --liquid-maps 0,1,530,571 --water-textures "%~dp0Lumenveil" --stamp "%~dp0Lumenveil\setup.stamp" --locale %LOCALE% --background > "Lumenveil\setup.log" 2>&1
if errorlevel 1 (
  echo Failed. The details are in Lumenveil\setup.log.
  if not defined AUTO pause
  exit /b 1
)

if defined AUTO exit /b 0
echo.
echo Done. Start the game as usual; if it is running, restart it.
echo   Ctrl+F12  switches Lumenveil off and on
echo   Ctrl+F7   opens the settings window
echo To keep Lumenveil and the client patches up to date, double-click Lordaeron-Update.cmd, or start the game with
echo Lordaeron-Play.cmd. Run this setup again only after installing a texture pack that changes the world.
pause
