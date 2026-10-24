# Changelog

## 0.6.2 (2026-10-25)

- Installing is: unzip anywhere, double-click `Lordaeron-Play.cmd`.
  - Unzipped next to `Wow.exe`, it runs from there; unzipped in a folder inside the game folder, it installs
    Lumenveil in the game folder; unzipped anywhere else, it asks for the game folder and installs Lumenveil there.
    Settings already in that folder are kept as an update keeps them.
  - The first time, it puts a Lordaeron icon (World of Warcraft's) on the desktop, which starts the game the same way:
    Lumenveil and the client patches then stay up to date by themselves.
  - After a first install it waits for Enter before it starts the game, so the messages can be read.
- The updater speaks the system's language when it hasn't found a game folder yet.

## 0.6.1 (2026-10-25)

- `Lordaeron-Play.cmd` and `Lordaeron-Update.cmd` run the setup by themselves when `lampdata.bin` or `liquiddata.bin`
  is missing, even when lordaeron.eu can't be reached. A first install is now: copy the zip's files next to `Wow.exe`
  and start the game with `Lordaeron-Play.cmd`.

## 0.6.0 (2026-10-25)

- Lordaeron's updater: double-click `Lordaeron-Update.cmd` in the game folder, or start the game with
  `Lordaeron-Play.cmd`, which updates and then starts it. It keeps Lumenveil up to date from lordaeron.eu:
  - it checks each download's SHA-256 before it changes anything, and waits until the game in this folder is closed;
  - your settings stay yours: a setting you changed keeps your value, one you never changed takes the new default, and
    new settings are added; the same for the zones' looks in `CoAVolFog-regions.ini`;
  - it keeps a copy of the version you had: `Lordaeron-Update.cmd -Rollback` goes back to it and skips the version you
    left until a newer one comes out;
  - it runs the setup again, shows what is new and the credits, and speaks English, French, German, Spanish or
    Russian, as your client does;
  - it also keeps the client patches in `Data` (`patch-W.MPQ`, `patch-Z.MPQ`) up to date, and empties the game's
    `Cache` folder when one changes.
- `Lumenveil-Setup.cmd /auto` runs without stopping, for the updater.
- The zip holds the settings as shipped in `Lumenveil\defaults\`, and its README the credits: Lumenveil by the
  Lumenveil authors ([github.com/lumenveil/Lumenveil](https://github.com/lumenveil/Lumenveil)), built on CoAVolFog,
  and this fork's source.

## 0.5.3 (2026-10-25)

From [Lumenveil 0.1.1](https://github.com/lumenveil/Lumenveil/releases/tag/v0.1.1), for antivirus programs that warned
about the DLLs (Microsoft Defender: `Trojan:Win32/Tecabans.STV!cl`, a false positive). Nothing changes in the game.

- `version.dll`, `CoAVolFog.dll` and `lamp_extract.exe` carry version information: product, description, version,
  licence, this fork's address and the original project's.
- The settings window copies and pastes within itself instead of through the Windows clipboard, so the DLL no longer
  uses the clipboard functions.
- Releases are built by GitHub Actions from the tagged source, and GitHub signs a record of each build:
  `gh attestation verify` shows which source and workflow a file came from. See
  [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md#antivirus-warnings).

## 0.5.2 (2026-10-25)

- The shipped settings (`CoAVolFog.ini`) take a look tuned in the game. An existing `CoAVolFog.ini` is kept as it is;
  only a new install, or one whose `CoAVolFog.ini` was deleted, gets them:
  - fog: a little more of it overall (`Density` 0.5, `FarFog` 1.04) but less low ground fog (`GroundFog` 1), the
    Classic client's fog layers where they exist (`DataMode=1`), patchier fog (`Drift` 0.8) in banks that pool more
    in valleys (`BankCover` 0.45, `BankPool` 1.5), the fog ceiling at 200 yd over the ground, more morning mist (3),
    evening haze (0.8) and weather fog (0.6), and the sun lighting the distance haze from every side
    (`AerialPerspective=0`);
  - light: stronger sun and moon light in the fog (`SunScatter` 1.75) with a lower exposure (0.52), the low sun's
    warm light and stronger shafts at dawn and dusk back on (`LowSunLight`, `LowSunShafts` 1), stronger shafts (0.8)
    and surface shadows (0.75), a little less saturation (0.95);
  - lamps: stronger halos (`LampFog` 0.6), 32 lamps at a time, fires at 0.8, lamp light 3, the spell light at 0.5
    and darker nights (0.8);
  - water: the later clients' smooth, bump-mapped look (`WaterReflect` 0.7, `WaterGlitter` 0.4, `WaterClarity`
    0.25, `WaterSmooth` 1, `WaterBump` 1, `WaterWaves` 0.2), and the effect kept under water;
  - surfaces: a stronger sheen (1.3), wet surfaces and puddles in rain on, relief off;
  - `SettingsVersion=1`, so the first start no longer rewrites the water settings.

## 0.5.1 (2026-10-25)

- Campfires, braziers, candles and torches that the server places as game objects (cooking fires, the fires of
  camps and towns, the festival braziers and candles) are lamps like the others: they light the ground, walls and
  characters around them from where they stand, cast lamp shadows, glow in the fog, flicker and show in the water.
  Before, the setup could not find them and only the spell light saw their flames, which it placed on whatever stood
  behind them, so their light slid over the walls as the camera turned.
  - `Lumenveil-Setup.cmd` also reads the lights of every game object model into `lampdata.bin`; run it again after
    installing this version (its first run does the whole setup again).
  - In the game, the objects within 250 yd whose model has a light are read from the client's own list of objects.
  - `ObjectLamps=0` ("Game object lamps" in the settings window) switches them off, for comparisons.
- The spell light leaves the flames of these lamps to the lamps, as it already did for the world's own, over the
  whole size of a fire the server makes larger than its model (Orgrimmar's bonfires stand 1.9 to 2.6 times their
  model's size).

## 0.4.2 (2026-10-23)

- The light of your weapon or armour (`EffectLight`), the lantern and the lamps no longer draw the terrain's
  triangles on rounded ground: on the terrain they light it by the terrain's height map, smooth from one spot to
  the next, instead of by each triangle's tilt, so a lit patch no longer ends on a straight triangle edge. The night
  darkness and the low sun's warm light judge the ground's facing the same way.
- The ground's smooth slope (here and for the sheen and wet ground of 0.4.0) only replaces a facing already close
  to it (within 40 to 60 degrees): characters, creatures and objects standing on the ground keep their own light
  and reflections.

## 0.4.0 (2026-10-23)

- The sun's sheen and wet ground no longer draw the terrain's triangles, seen from the front or looking straight
  down, by day or by night:
  - the dry sheen on bright, smooth ground (snow, ice, pale stone) lies level, as on flat ground, and fades out on
    steep slopes by the terrain's height map, which is smooth from one spot to the next, instead of by each
    triangle's tilt;
  - on wet ground the film's highlight uses the ground's normal straightened toward the vertical, and on steep
    slopes its reflections and highlights fade out (walls keep them); how wet the ground gets, its puddles and its
    shelter follow the height map's slope too. Objects and walls standing above the ground keep their own facing.

## 0.3.1 (2026-10-23)

- `Lumenveil-Setup.cmd` is much faster and runs once instead of three times: one pass reads each terrain tile once
  for the lamps and the water, on all the processor's cores, then places the lamps in the same order as before, so
  `lampdata.bin` and `liquiddata.bin` come out the same. It runs at a below-normal priority instead of Windows'
  background mode, which slowed its reading of the archives.
- When the game's archives and the options are the same as at the last setup, it stops at once.

## 0.2.2 (2026-10-23)

- `CoAVolFog.ini` keeps itself up to date: at the game's start, a setting the file lacks is added with its default
  value, and an older file (no `SettingsVersion`) takes the water look of the later clients once (`WaterReflect=0.7`,
  `WaterGlitter=0.4`, `WaterClarity=0.25`, `WaterSmooth=1`, `WaterBump=1`, `WaterFoam=0`, `WaterWaves=0.2`), then
  `SettingsVersion=1`; what you change afterwards stays.

## 0.2.1 (2026-10-23)

- Water in the style of the later clients' high quality:
  - `WaterSmooth` (0-1, settings window: Water > Smooth): the game's animated water texture under the reflections
    gives way to the zone's own water colours, the shallow and deep ones and their transparency that the game's light
    settings give each hour, the river ones on rivers and lakes and the sea ones on the sea (told apart per spot from
    `liquiddata.bin`). The water gets the shallow colour over the bottom near the shore and the deep one, opaque, from
    3 yards down (8 at sea); where it is under 0.6 yards deep the bottom shows through, so the shore fades instead of
    ending on a hard edge. The log's water line names the colours.
  - When a patch in the game's `Data` folder holds the later clients' water textures
    (`XTextures\ocean\newoceanbump.blp`, `XTextures\foam\waterchop7bw.blp`), `Lumenveil-Setup.cmd` copies them into
    the `Lumenveil` folder as `waterbump.blp` and `waterfoam.blp`. `WaterBump` (0-2): two wide layers of the normal
    map (24 and 41 yard tiles) drift slowly with the wind over the waves for broad smooth undulations and bend the
    reflections. `WaterFoam` (0-2): a narrow, soft band of foam where the water is shallow along the shores, fading
    with depth and distance.

## 0.1.8 (2026-10-23)

- The beam of a spotlight, a searchlight or a lighthouse no longer lights the ground behind it, opposite the camera:
  the spell light now leaves out what glows within 25 yards of those lamps, as it already did within 3 yards of every
  lamp's flame.

## 0.1.7 (2026-10-23)

- The lamps no longer make the game wait for the graphics card each frame (about 2 ms a frame at night, 76 to 91
  frames a second in a test): the lamp list and the lamp tiles are written in system memory and copied by the card
  itself, instead of locking textures in its memory.
- With `PerfLog=1`, the summary also splits the lamps' time on the main thread: the copy of the world for spell
  light, the units, the lamp selection, and the upload (the table, the tile building, the tile lock and the tile
  writing).

## 0.1.4 (2026-10-23)

- Lamp light on the ground no longer shows the terrain's triangles: before it takes a lamp's light, each triangle
  facing up to a 55 degree slope is straightened by the same share toward the vertical (fading out up to 70 degrees),
  so neighbouring triangles get the same light. Walls are left alone.
- The edges of neighbouring terrain triangles no longer cast lamp shadows on the ground: an occluder counts only when
  it stands clearly above the lit spot's surface, as people, props and walls do.

## 0.1.0 (2026-10-21)

The first public release.

- Volumetric fog in layers and banks that lie on the ground, pool in valleys and over water, and drift with the wind;
  dust in dry zones and a humid haze in green ones; morning mist and evening haze.
- Sun and moon light shafts, also from a light outside the picture, with a memory of what the camera has seen.
- Lamps, torches, braziers, candles and lit windows light the world around them at night, with shadows, and glow in
  the fog; distant towns show their lights; darker nights.
- Light from spells, enchants and glowing projectiles; a belt lantern.
- Water with wind-driven waves, lasting reflections, sun glitter, rain rings, ripples and reflected lamps.
- Wet stone and soil, and puddles, in rain (off in the shipped settings).
- Relief on building walls, floors and models, from height maps made in the game (off in the shipped settings).
- A look for each of 37 zones, by hour and weather; a region panel to tune any zone.
- A longer view distance on the old continents.
- Ctrl+F12 switches the whole mod off and on, for comparisons; Ctrl+F7 opens the settings.
- `Lumenveil-Setup.cmd` makes the lamp and water data from the player's own game files.
