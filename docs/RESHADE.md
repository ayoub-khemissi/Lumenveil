# ReShade with Lumenveil

[ReShade](https://reshade.me) is a separate, free post-processing tool. It works on the finished picture, after
Lumenveil has drawn its fog, lights and water, so the two add up. Lumenveil doesn't need ReShade. The screenshots in
the README were taken with ReShade running the preset described below, in both shots of each pair.

ReShade, DXVK and the effect packages named here are other people's projects with their own licences. Lumenveil
doesn't include them. Download them from their own pages.

## The setup in the screenshots

- **Lumenveil** in the game folder, as in [INSTALL.md](INSTALL.md).
- **DXVK** as the game's `d3d9.dll`. It runs the game's Direct3D 9 on Vulkan.
- **ReShade 6.8** hooked into that Vulkan output.
- **The preset** [`extras/reshade/ReShade-Lumenveil.ini`](../extras/reshade/ReShade-Lumenveil.ini). It uses five effects
  from two packages:

  | Effect | Package | On | What it does here |
  |---|---|---|---|
  | SMAA (`MartysMods_SMAA.fx`) | iMMERSE | always | Anti-aliasing. Edge detection mode 3, threshold 0.1, 32/16 search steps, corner rounding 25. It stands in for the game's own multisampling, which Lumenveil turns off. |
  | Launchpad (`MartysMods_LAUNCHPAD.fx`) | iMMERSE | always | Works out surface normals for MXAO (normals mode 2, intensity 0.5, quality 4, radius 0.5). |
  | FakeHDR (`FakeHDR.fx`) | SweetFX | always | Deepens the mid-tones and adds contrast (power 1.518, radii 0.793 and 0.870). |
  | MXAO (`MartysMods_MXAO.fx`) | iMMERSE | Ctrl+F10 | Ambient occlusion: darker corners and contact shadows (quality preset 2, radius 2.5, amount 0.473, fade 0.35). |
  | Vibrance, Curves (`Vibrance.fx`, `Curves.fx`) | SweetFX | Ctrl+F10 | A grade: a little less saturation (−0.25) and a little more contrast (0.15). |

  Ctrl+F10 switches MXAO, Vibrance and Curves together. The game also uses Ctrl+F10 for the tenth stance-bar slot,
  which few classes fill; change the key in ReShade if yours does.
- **DXVK settings** in a `dxvk.conf` next to `Wow.exe`:

  ```ini
  d3d9.samplerAnisotropy = 16
  d3d9.maxFrameLatency = 1
  ```

  The first gives every texture 16x anisotropic filtering. The second keeps at most one frame queued for the
  graphics card. It is meant to go with the game's own frame wait switched off, by adding the line
  `SET gxFixLag "0"` to `WTF\Config.wtf` while the game is closed. Then the processor prepares the next frame while
  the graphics card draws this one, and the input lag stays about one frame.
- **ReShade's depth settings** (global preprocessor definitions, on the Home tab of ReShade's menu):

  ```
  RESHADE_DEPTH_LINEARIZATION_FAR_PLANE=150.0
  RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=0
  RESHADE_DEPTH_INPUT_IS_REVERSED=0
  RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=0
  ```

  MXAO, Launchpad and SMAA's edge detection read the depth. With a far plane other than 150 the ambient occlusion
  comes out too strong or too weak.
- **Keys** chosen in ReShade's settings: F12 opens ReShade's menu, F11 switches all its effects off and on, Print
  Screen saves a screenshot. ReShade's defaults work just as well. Lumenveil's own keys hold Ctrl. With ReShade's
  setting **Force shortcut modifiers** on, as here, F12 for ReShade and Ctrl+F12 for Lumenveil don't collide.

The 37 zone looks in `CoAVolFog-regions.ini` were judged with this preset on. FakeHDR darkens the mid-tones and warms
the picture, so without it those zones look a little brighter and cooler than in the screenshots.

## Installing ReShade with Lumenveil

Install Lumenveil first ([INSTALL.md](INSTALL.md)) and check that it runs (Ctrl+F7 opens its window). Then use one of
the two routes below. The first is the setup the screenshots were taken with.

### Route 1: DXVK and ReShade on Vulkan (as in the screenshots)

1. Download DXVK from its releases page on GitHub (`doitsujin/dxvk`). Copy `x32\d3d9.dll` from the archive into the
   game folder, next to `Wow.exe`. The game is a 32-bit program: the `x64` file doesn't work. The screenshots were
   taken with DXVK 3.1.1.
2. Save the two `dxvk.conf` lines above as `dxvk.conf` in the game folder.
3. Start the game once to check that it runs with DXVK. A `Wow_d3d9.log` appears in the game folder.
4. Download ReShade from [reshade.me](https://reshade.me) and run its setup. Pick `Wow.exe` and choose **Vulkan** as
   the rendering API, because with DXVK the game draws through Vulkan. ReShade then installs itself as a Vulkan layer
   for the whole computer and turns it on for `Wow.exe`. Windows asks for administrator rights for that step.
5. When the setup lists effect packages, tick **SweetFX by CeeJay.dk** and **iMMERSE by Marty McFly**.
6. Copy [`extras/reshade/ReShade-Lumenveil.ini`](../extras/reshade/ReShade-Lumenveil.ini) into the game folder.
7. Start the game, open ReShade's menu (Home by default), skip or finish its tutorial, and pick
   `ReShade-Lumenveil.ini` from the preset list at the top of the Home tab.
8. On the same tab, open **Edit global preprocessor definitions** and set the four depth values above.

To remove it later, run ReShade's setup again and choose Uninstall, and delete `d3d9.dll` and `dxvk.conf` from the
game folder.

### Route 2: ReShade on Direct3D 9, without DXVK

1. Run ReShade's setup, pick `Wow.exe` and choose **Direct3D 9**. ReShade then puts its own `d3d9.dll` into the game
   folder.
2. Tick the same two effect packages, then follow steps 6 to 8 of route 1.

Lumenveil's own test program passes all its checks with ReShade 6.8 loaded this way as `d3d9.dll`. The preset's
effects, and their depth in particular, were checked in the game only on route 1. If MXAO shows nothing or covers the
whole picture, open **Add-ons** in ReShade's menu, look at the depth buffers listed under **Generic Depth**, and pick
the one that changes as you move.

## Things to know

- **Order.** Lumenveil draws its effects while the game draws its frame. ReShade works on the finished frame, so its
  effects come after Lumenveil's. ReShade's own fog and depth-of-field effects (AdaptiveFog, ATMOSPHERE and the like)
  draw a second fog over Lumenveil's; leave them off.
- **Anti-aliasing.** Lumenveil needs the depth of every pixel, so it tells the game that multisampling isn't
  available. SMAA in the preset takes its place.
- **Comparing.** Ctrl+F12 switches Lumenveil alone. ReShade, DXVK and texture packs stay on, so a pair taken with it
  shows what Lumenveil adds on top of them.
- **Frame rate.** MXAO is the most costly effect in the preset. Leave the Ctrl+F10 grade off if the game runs slowly.
- **Online play.** Some servers don't allow ReShade, DXVK or other changes to the client, and anti-cheat software may
  detect them. Read the rules of the server you play on first.
