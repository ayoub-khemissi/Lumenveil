# Third-party notices

Lumenveil includes or builds on the following open-source software. Each keeps its own licence. The full texts of the
licences below are in [`third_party/licenses`](third_party/licenses), and the source of everything compiled into a
release is in this repository.

## In the game files (`version.dll`, `CoAVolFog.dll`)

### CoAVolFog

Volumetric fog renderer for the World of Warcraft 3.3.5a client, by jealous-sound.
<https://github.com/jealous-sound/coa-vfog>

Licence: GNU General Public License, version 2. Lumenveil is a modified version of CoAVolFog: the source in `engine/`
is CoAVolFog's with Lumenveil's changes on top, released under the same licence (see [LICENSE](LICENSE)).
[engine/CHANGES.md](engine/CHANGES.md) lists the files added and changed, and when.

### Everwood (benilla-everwood_graphics)

Rules for the colour, kind and flicker of lamps and fires, ported from Everwood's `fire_light.rs` and `flicker.rs`.
<https://github.com/pkuzic/benilla-everwood_graphics>

Licence: MIT. Copyright (c) 2026 Sam. Everwood is dual-licensed MIT OR Apache-2.0; Lumenveil uses it under the MIT
licence (`third_party/licenses/Everwood-MIT.txt`).

### Dear ImGui 1.92.9b

The in-game settings window and region panel, built from the copy in `third_party/imgui` (the core files and the
Win32 and Direct3D 9 backends, unchanged).
<https://github.com/ocornut/imgui>

Licence: MIT. Copyright (c) 2014-2026 Omar Cornut (`third_party/licenses/DearImGui-MIT.txt`).

## In the setup tool (`Lumenveil\lamp_extract.exe`)

The setup tool also contains code from Lumenveil and CoAVolFog (GPL v2) and the Everwood rules above.

### libmpq 0.4.2

Reads the game's MPQ archives on the player's computer. Copyright (c) 2003-2008 Maik Broemme and the libmpq authors.
Homepage given in its README: <http://www.babelize.org/>

Licence: GNU General Public License, version 2 or later (`third_party/libmpq/COPYING`). The copy in
`third_party/libmpq` comes with a `config.h` for Windows.

### dirent.h for Visual Studio

Used by libmpq's Windows build. Copyright (C) 2006 Toni Ronkko. Licence: MIT, in the file's header
(`third_party/libmpq/win/dirent.h`).

### zlib 1.3.1

Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler.

Licence: the zlib licence, in the header of `third_party/zlib/zlib.h` and in `third_party/licenses/zlib.txt`.

### bzip2 / libbzip2 1.0.6

Copyright (C) 1996-2010 Julian R Seward. Homepage given in its README: <http://www.bzip.org/>

Licence: a BSD-style licence, in `third_party/bzip2/LICENSE`.

## Not included

Lumenveil doesn't include or redistribute any part of the World of Warcraft client, its program code, its data, or
data derived from it. See [LEGAL.md](LEGAL.md).
