# How Lumenveil works

This page is for people who want to change Lumenveil or understand what it does to the game. Players don't need it.

## Getting into the game

`Wow.exe` loads `version.dll` by name, and Windows looks in the program's own folder first. Lumenveil's `version.dll`
loads `CoAVolFog.dll` and forwards every real `version.dll` function to the copy in the Windows system folder.

`CoAVolFog.dll` then checks the client before it touches anything: the executable's time stamp, and a hash of the
client's code at every place it will change or call (`engine/src/client_code.h`; the source holds the hashes, never the
client's bytes). If one doesn't match the 12340 client, that feature stays off and the log says which. It changes the
client only in memory, never on disk:

- It answers the client's request for `Direct3DCreate9` with a wrapper around the real Direct3D 9 (or DXVK, or
  ReShade, whichever the game would have loaded), so it sees every draw call and can add its own passes.
- It redirects four calls in the client's world rendering, so it knows when the opaque world, the liquids and the
  whole world are drawn, and can insert its passes between them.
- It retargets the client's far-clip clamp, so the view distance on the old continents can go past the client's
  limit, and the client's water drawing calls, so it knows which draws are water.
- With the settings window on, it watches the game window's keyboard and mouse messages to show its own windows (Dear
  ImGui) and take its own keys.

It reads a few values of the running client: the time of day, the sun and moon direction, the light and fog colours
of the zone, the weather, the zone, the camera, the player's position, whether the player is indoors, and where the
characters and creatures near the camera stand (to place the light of their spells at them) and which corpses can be
looted (to light their sparkle). It calls a few of the client's own functions: its indoor test; its object list walk;
its collision test, to learn the height of the ground around the camera; its far-clip
setter, to apply the view distance again when the mod is switched off or on; and, only while the region panel holds
them, its clock and weather setters, which change the time of day and the weather on the player's own screen and
nowhere else. It sends nothing to the server and runs none of the game's scripts.

## The passes

Each frame, after the client has drawn the world and before it draws its interface, Lumenveil:

1. **Copies the depth of every pixel.** Everything else needs to know how far each pixel is.
2. **Marches the fog.** For each pixel of a half- or quarter-resolution target, it steps along the view ray and adds
   up the fog's density and the light it scatters toward the camera: layers of haze and ground fog shaped by the
   zone's own fog colours, banks of fog that stand on the ground and pool in valleys (drift noise in a 3D texture),
   dust or humid haze by climate. Each step asks whether the sun or moon reaches it, from the depth buffer, from a
   height map of the terrain around the camera, and from a shadow map seen from the light that remembers what the
   camera has seen. That is what makes the light shafts, including those from a sun outside the picture.
3. **Smooths the fog over time**, re-projecting last frame's result, with a guard against moving things.
4. **Lights the world with lamps.** The positions of the lamps, torches, braziers, candles and lit windows come from
   `lampdata.bin`, which the setup tool makes from the world's models. Each frame the most important ones near the
   camera get a slot; a full-resolution pass adds their light to every pixel within reach, with the client's own
   point-light falloff and a screen-space shadow test, and a low-resolution pass draws their glow in the fog.
5. **Lights the world with spells.** It compares the picture before and after the client draws its glowing effects,
   finds where something bright appeared, and turns that into short-lived lights.
6. **Draws the water** over the client's own: waves summed from many sine waves that follow the wind, reflections
   from the screen and from a memory of what the camera has seen, sun glitter, rain rings and ripples.
7. **Composites.** The fog is blended over the picture, nights are darkened where nothing lights them, the lamp light
   is added in the game's own way, wet surfaces get their sheen, and a colour grade per zone is applied.

Relief is done differently: Lumenveil watches the client create its pixel shaders for buildings and models, and
makes a version of each that offsets the texture lookup along a height map (parallax occlusion mapping). The
height maps are made in the game from the textures themselves, on a background thread.

All shaders are HLSL compiled to Shader Model 3.0. The CPU work is a few hundred microseconds a frame; the GPU work
depends on the settings.

## Settings and zones

`CoAVolFog.ini` is read at start and again whenever the file changes. `CoAVolFog-regions.ini` overrides any setting
for a zone, an hour slot and a weather; the settings blend across hours and fade between zones.

## The setup tool

`lamp_extract.exe` reads the game's MPQ archives in the client's own order with libmpq. For lamps it walks every map's
placed models and buildings and finds light sources by their emitters, their light records, their names and their
glowing glass, with colour and flicker rules ported from Everwood. For water it reads the liquid of every map chunk.
It writes a compact binary file that the DLL maps at start. Both files describe the player's own copy of the game and
stay on their computer.

Lights that a server places as game objects (some campfires, braziers and lamp posts) are not in the game's files,
so the setup tool can't find them and they give no light.

## The code

| Folder | What is there |
|---|---|
| `engine/src` | The DLL: hooks, the Direct3D 9 wrapper, the renderer, settings, the windows, the data readers |
| `engine/shaders` | The HLSL passes |
| `engine/proxy` | The `version.dll` loader |
| `engine/tools/lamp_extract` | The setup tool |
| `engine/tests` | The test harness: renders scenes through the real DLL on a hidden window and checks the results, about 1,200 checks |
| `third_party` | Dear ImGui for the settings window; libmpq, zlib and bzip2 for the setup tool; the licences |

The engine's code has no comments by the original project's rule; the code is meant to say what it does through its
names. Every change of behaviour comes with a check in the harness. See [BUILDING.md](BUILDING.md) and
[CONTRIBUTING.md](../CONTRIBUTING.md).
