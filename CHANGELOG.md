# Changelog

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
