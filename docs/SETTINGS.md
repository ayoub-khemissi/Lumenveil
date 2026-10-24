# Settings

There are three places to change how Lumenveil looks.

1. **The settings window, Ctrl+F7.** Every effect has a slider, grouped by topic, and the next frame shows the
   change. **Save** writes what you changed into `CoAVolFog.ini`; **Revert** reloads the file. These are the global
   settings, used wherever no zone setting applies.
2. **`CoAVolFog.ini`**, next to `Wow.exe`. Every setting has a comment above it saying what it does and its range.
   Changes made in a text editor apply within a second while the game runs, except `Enable` and `EngineHooks`, which
   need a restart.
3. **The region panel, Ctrl+F3**, and **`CoAVolFog-regions.ini`**. A zone can have its own look for dawn, day, dusk
   and night, in clear weather, rain, snow and sandstorms. A zone setting overrides only the settings it names; the
   rest come from the global settings. See [Zones](#zones).

If a change makes things worse, delete the line from `CoAVolFog.ini` (it then takes its built-in default) or put back
the file from the zip.

## The settings that matter most

| Setting | What it does | Turn it down if |
|---|---|---|
| `Quality` | 1 quarter resolution and 16 steps, 2 half and 24, 3 half and 32 | the frame rate is low |
| `Density` | The amount of fog everywhere; 0 = no fog | the world looks washed out |
| `ZoneDensity` | The fog amount per zone, as `zone:factor` pairs (`148:0.5, 17:2`), `*:factor` for every zone not listed (`*:0.2, 148:0.5`); the window's "This zone" slider writes it | one zone is too foggy |
| `Banks` | 0 even layers, 1 banks of fog on the ground everywhere, 2 banks only in valleys and over water at dawn and dusk | forests are too foggy at dawn |
| `FogCeiling` | While flying high, no fog above this height over the ground, so you look down on it | |
| `Shafts`, `MoonShafts` | Strength of the sun's and the moon's light shafts | |
| `ShaftGlare` | Softens the glare around a low sun; lower = softer | the sun blinds at dawn and dusk |
| `NightDarkness` | How much darker nights are than in the plain game (0-0.9); lit places and indoors are left alone | you can't see at night |
| `LampLight` | How strongly lamps, torches and fires light their surroundings | towns look orange |
| `LampFog` | How strongly lamps glow in the fog around them | |
| `LampCount` | How many lamps light the world at once (up to 128); the nearest and brightest are chosen | the frame rate drops in cities |
| `LampDayLight` | How much of their light lamps keep by day (0-1) | lamps look odd by day |
| `LampShadows` | Shadows from lamps (0-1) | |
| `FireStrength` | Fires, braziers and campfires alone, on top of `LampLight` | fires are too bright |
| `WindowLight` | Light from lit windows | |
| `FarLights` | Distant lamps seen as points of light at night | |
| `EffectLight` | Light from spells, enchants and glowing projectiles | |
| `ObjectLamps` | Campfires, braziers and candles the server places light the world like the other lamps (1) or not (0) | a fire's light seems to move with the camera |
| `Lantern` | A lantern at your belt: 0 off, 1 in the dark, 2 always (Ctrl+F6) | |
| `WaterReflect`, `WaterGlitter`, `WaterWaves` | Reflections, sun glitter and waves on water | |
| `WetSurfaces`, `Puddles` | Wet stone and soil, and puddles, in rain; 0 in the shipped settings | |
| `Relief`, `ReliefModels`, `ReliefDepth` | Relief on and off (Ctrl+F4); relief on building walls, floors and models (`ReliefModels`, 0 in the shipped settings); its depth in yards | the frame rate is low |
| `FarClipMax` | How far the view distance can be lifted on the old continents; 0 = the game's own limit | the game runs out of memory |
| `Saturation`, `Exposure` | The colour and brightness of the final picture | |

The full list is in `CoAVolFog.ini`, each setting with its comment. How the effects work is in
[HOW-IT-WORKS.md](HOW-IT-WORKS.md).

## Keys

| Setting | Default | What it does |
|---|---|---|
| `ToggleKey` | Ctrl+F12 | Switches Lumenveil off and on. It is on at every start |
| `OverlayKey` | Ctrl+F7 | Shows and hides the settings window |
| `RegionKey` | Ctrl+F3 | Shows and hides the region panel |
| `ReliefKey` | Ctrl+F4 | Switches relief on and off and saves `Relief` |
| `LanternKey` | Ctrl+F6 | Puts the lantern out or lights it, and saves `Lantern` |
| `SpotKey` | Ctrl+F5 | Saves your place, camera, game time, weather and the settings to `CoAVolFog-spots.txt` |

A key is written `F1`-`F24`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Pause`, `ScrollLock`, a letter
or a digit, with any of `Ctrl+`, `Shift+`, `Alt+` in front: `ToggleKey=Ctrl+Shift+F11`. The keys work only when
`Overlay=1` (the default). `ToggleKey`, `OverlayKey` and `RegionKey` never reach the game; the others also do whatever
the game has bound to them. On laptops whose F-keys send media keys, hold Fn.

## Zones

`CoAVolFog-regions.ini` holds sections like `[Zone 10 Night Rain]`: zone 10 (Duskwood), at night, in rain. The time
words are `Dawn`, `Day`, `Dusk`, `Night` and `Any`; the weather words `Clear`, `Rain`, `Snow`, `Sand` and `Any`;
`[All Night Any]` applies to every zone. A setting missing from a section is looked up in the less specific ones, then
in the global settings. Between the hours the look blends over, and a new zone fades in over four seconds.

The region panel (Ctrl+F3) edits these sections while you look at the result, and can hold the time of day or the
weather on your screen alone (the server and other players keep theirs) to see each slot. It writes only
`CoAVolFog-regions.ini`.

The tuned zones are listed by their zone number in the file, with a comment line giving each zone's name. Deleting
the file gives every zone the global look.

## Logs

`CoAVolFog.log` next to `Wow.exe` says at every start what the mod found in the client (hooks, the device, data
files, keys) and writes a summary every 60 seconds. `LogLevel=2` adds more; `PerfLog=1` adds a timing of each of the
mod's passes every 60 seconds.
