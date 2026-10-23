# Changelog

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
